#include "modules/dictation/DictationFlow.h"

#include "core/I18n.h"

#include <QDebug>

namespace {

using OverlayState = DictationOverlay::State;
using Action = DictationTrigger::Action;

// Transcribiendo sin respuesta del motor (colgado, no caído: una caída avisa sola): se da por fallido.
constexpr qint64 kFinalTimeoutMs = 30000;

} // namespace

DictationFlow::DictationFlow(Io &io, QObject *parent)
    : QObject(parent)
    , m_io(io)
{
}

void DictationFlow::setContext(const QStringList &vocabulary, const QString &prompt)
{
    m_vocabulary = vocabulary;
    m_prompt = prompt;
}

void DictationFlow::press(qint64 nowMs)
{
    m_lastNow = nowMs;
    if (m_phase == Phase::Transcribing || m_phase == Phase::Typing) {
        qInfo() << "[Dictado] Atajo ignorado: todavia" << (m_phase == Phase::Typing ? "escribe" : "transcribe")
                << "el dictado anterior";
        return;
    }
    const Action action = m_trigger.press(nowMs);
    if (action == Action::Start) {
        start();
    } else if (action == Action::Finish) {
        finish();
    }
}

void DictationFlow::release(qint64 nowMs)
{
    m_lastNow = nowMs;
    if (m_trigger.release(nowMs) == Action::Finish) {
        finish();
    }
}

void DictationFlow::escape()
{
    if (m_phase == Phase::Recording) {
        m_trigger.cancel();
        cancel();
    } else if (m_phase == Phase::Transcribing) {
        cancel();
    }
}

void DictationFlow::tick(qint64 nowMs)
{
    m_lastNow = nowMs;
    if (m_phase == Phase::Recording && m_trigger.tick(nowMs) == Action::Finish) {
        qInfo() << "[Dictado] Tope de duracion: se termina solo";
        finish();
    } else if (m_phase == Phase::Transcribing && nowMs - m_finishAt > kFinalTimeoutMs) {
        fail(I18n::tr("The dictation engine did not answer."));
    }
}

void DictationFlow::start()
{
    m_target = m_io.foregroundWindow();
    m_confirmed.clear();
    m_previous.clear();
    m_live.clear();
    m_pending.clear();
    const QString problem = m_io.engineProblem();
    if (!problem.isEmpty()) {
        m_trigger.reset();
        m_io.showOverlay(m_target);
        m_io.overlayText(QString(), QString());
        m_io.overlayState(OverlayState::Error, problem);
        m_io.hideOverlay(kErrorMs);
        return;
    }
    setPhase(Phase::Recording);
    m_io.setActive(true);
    m_io.showOverlay(m_target);
    m_io.overlayText(QString(), QString());
    m_io.overlayLevel(0.0f);
    // Con el motor cargando, el audio se guarda igual en su proceso: lo dicho mientras carga no se pierde.
    m_io.overlayState(m_io.engineReady() ? OverlayState::Listening : OverlayState::Loading, QString());
    m_io.engineBegin();
    if (m_phase != Phase::Recording) {
        return; // el motor fallo enseguida (fail ya lo mostro)
    }
    if (!m_io.startCapture() && m_phase == Phase::Recording) {
        fail(I18n::tr("Could not open the microphone."));
    }
}

void DictationFlow::finish()
{
    if (m_phase != Phase::Recording) {
        return;
    }
    m_trigger.reset();
    // stopCapture() entrega los ultimos bloques mientras la fase sigue en Recording: llegan al motor.
    m_io.stopCapture();
    if (m_phase != Phase::Recording) {
        return;
    }
    m_io.engineFinish();
    m_finishAt = m_lastNow;
    setPhase(Phase::Transcribing);
    m_io.overlayState(OverlayState::Transcribing, QString());
}

void DictationFlow::cancel()
{
    const Phase was = m_phase;
    setPhase(Phase::Idle);
    m_trigger.reset();
    m_io.setActive(false);
    if (was == Phase::Recording) {
        m_io.stopCapture();
    }
    m_io.engineCancel();
    if (!m_live.trimmed().isEmpty()) {
        record(m_live, false, I18n::tr("Cancelled"));
    }
    m_io.hideOverlay(0);
}

void DictationFlow::fail(const QString &message)
{
    const Phase was = m_phase;
    setPhase(Phase::Idle);
    m_trigger.reset();
    m_io.setActive(false);
    if (was == Phase::Recording) {
        m_io.stopCapture();
    }
    m_io.engineCancel();
    qWarning().noquote() << "[Dictado] Dictado cortado:" << message;
    if (!m_live.trimmed().isEmpty()) {
        record(m_live, false, message);
    }
    m_io.overlayState(OverlayState::Error, message);
    m_io.hideOverlay(kErrorMs);
}

void DictationFlow::captureFailed(const QString &message)
{
    if (m_phase == Phase::Recording) {
        fail(message);
    }
}

void DictationFlow::samples(const QVector<float> &block)
{
    if (m_phase == Phase::Recording) {
        m_io.engineFeed(block);
    }
}

void DictationFlow::level(float value)
{
    if (m_phase == Phase::Recording) {
        m_io.overlayLevel(value);
    }
}

void DictationFlow::engineReady()
{
    if (m_phase == Phase::Recording) {
        m_io.overlayState(OverlayState::Listening, QString());
    }
}

void DictationFlow::partial(const QString &text)
{
    // Con poco audio, una pasada en vivo suele devolver el vocabulario o la frase de contexto: no se muestra.
    if (m_phase != Phase::Recording || DictationText::echoesContext(text, m_vocabulary, m_prompt)) {
        return;
    }
    const DictationText::Split split = DictationText::stabilize(m_confirmed, m_previous, text);
    m_confirmed = split.confirmed;
    m_previous = text;
    m_live = split.display();
    m_io.overlayText(split.confirmed, split.provisional);
}

void DictationFlow::final(const QString &text)
{
    if (m_phase != Phase::Transcribing) {
        return;
    }
    const QString result = DictationText::applyReplacements(text.trimmed(), m_replacements).trimmed();
    m_io.setActive(false);
    if (result.isEmpty()) {
        // No se entendio nada (silencio, ruido): no se escribe nada.
        qInfo() << "[Dictado] Final vacio: no se escribe nada";
        setPhase(Phase::Idle);
        m_io.hideOverlay(0);
        return;
    }
    setPhase(Phase::Typing);
    m_pending = result;
    m_io.overlayText(result, QString());
    if (!m_io.type(m_target, result)) {
        typed(TextTyper::Result::Failed, 0);
    }
}

void DictationFlow::engineFailed(const QString &message)
{
    if (m_phase == Phase::Recording || m_phase == Phase::Transcribing) {
        fail(message);
    }
}

void DictationFlow::limitReached()
{
    if (m_phase == Phase::Recording) {
        qInfo() << "[Dictado] El motor llego a su tope: se termina";
        finish();
    }
}

void DictationFlow::typed(TextTyper::Result result, int count)
{
    if (m_phase != Phase::Typing) {
        return;
    }
    const QString text = m_pending;
    m_pending.clear();
    setPhase(Phase::Idle);
    const QString note = noteFor(result, count > 0);
    record(text, result == TextTyper::Result::Typed, note);
    if (result == TextTyper::Result::Typed || result == TextTyper::Result::LoggedOnly) {
        m_io.hideOverlay(700);
        return;
    }
    m_io.overlayState(OverlayState::Error, note + QStringLiteral(". ") + I18n::tr("The text is in Recent, in Dictation."));
    m_io.hideOverlay(kErrorMs);
}

void DictationFlow::shutdown()
{
    const Phase was = m_phase;
    m_trigger.reset();
    if (was == Phase::Idle) {
        return;
    }
    setPhase(Phase::Idle);
    m_io.setActive(false);
    if (was == Phase::Recording) {
        m_io.stopCapture();
    }
    if (was != Phase::Typing) {
        m_io.engineCancel();
    }
    m_pending.clear();
    m_io.hideOverlay(0);
}

QString DictationFlow::noteFor(TextTyper::Result result, bool partly)
{
    switch (result) {
    case TextTyper::Result::Typed: return I18n::tr("Typed");
    case TextTyper::Result::LoggedOnly: return I18n::tr("Not typed: automated test run");
    case TextTyper::Result::FocusChanged:
        return partly ? I18n::tr("Partly typed: the window changed") : I18n::tr("Not typed: the window changed");
    case TextTyper::Result::Elevated: return I18n::tr("Not typed: the window runs as administrator");
    case TextTyper::Result::NoTarget: return I18n::tr("Not typed: the window closed");
    case TextTyper::Result::Failed: break;
    }
    return I18n::tr("Not typed: Windows did not take the keys");
}

void DictationFlow::setPhase(Phase phase)
{
    if (m_phase == phase) {
        return;
    }
    m_phase = phase;
    emit phaseChanged();
}

void DictationFlow::record(const QString &text, bool typed, const QString &note)
{
    Entry entry;
    entry.when = QDateTime::currentDateTime();
    entry.text = text.trimmed();
    entry.typed = typed;
    entry.note = note;
    emit finished(entry);
}
