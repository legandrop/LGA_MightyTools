#include "actions/ActionRunner.h"
#include "platform/InputInjector.h"

#include <QDebug>
#include <QTimer>

#include <cmath>

namespace {

// Despues de soltar los modificadores, antes de mandar nada: Nuke tiene que ver los key-up.
constexpr int kAfterReleaseMs = 15;
// Para que abra el menu contextual del knob (version AutoHotkey: 50 ms).
constexpr int kMenuOpenMs = 50;
// Entre la flecha y Enter (version AutoHotkey: 30 ms).
constexpr int kBeforeEnterMs = 30;
// Despues del click en el Dope Sheet, para que el panel tome el foco.
constexpr int kAfterFocusClickMs = 30;
// Entre Ctrl+A y F (version AutoHotkey: 10 ms).
constexpr int kBetweenKeysMs = 10;
// Antes de devolver el puntero.
constexpr int kBeforeRestoreMs = 15;

} // namespace

ActionRunner::ActionRunner(InputInjector *injector, QObject *parent)
    : QObject(parent)
    , m_injector(injector)
{
}

QPoint ActionRunner::spotToNative(const QRect &frame, const QPointF &spot)
{
    return QPoint(frame.left() + static_cast<int>(std::lround(spot.x() * frame.width())),
                  frame.top() + static_cast<int>(std::lround(spot.y() * frame.height())));
}

QPointF ActionRunner::nativeToSpot(const QRect &frame, const QPoint &nativePoint)
{
    if (frame.width() <= 0 || frame.height() <= 0) {
        return QPointF(-1, -1);
    }
    return QPointF(static_cast<double>(nativePoint.x() - frame.left()) / frame.width(),
                   static_cast<double>(nativePoint.y() - frame.top()) / frame.height());
}

bool ActionRunner::runAddKeyframe()
{
    if (m_busy) {
        qDebug() << "[ActionRunner] Add keyframe ignorado: ya hay una secuencia corriendo";
        return false;
    }
    qInfo() << "[ActionRunner] Add keyframe";
    InputInjector *in = m_injector;
    start({
        {0, [in]() { in->releaseModifiers(); }},
        // La version AutoHotkey movia el puntero al caret si habia uno. Los campos de Nuke no
        // exponen su caret al sistema, asi que se usa siempre la posicion del puntero.
        {kAfterReleaseMs, [in]() { in->clickAt(InputInjector::Button::Right, in->cursorPos()); }},
        {kMenuOpenMs, [in]() { in->tapKey(InputInjector::Key::Down); }},
        {kBeforeEnterMs, [in]() { in->tapKey(InputInjector::Key::Return); }},
    });
    return true;
}

bool ActionRunner::runFrameDopeSheet(const QRect &nukeFrame, const QPointF &spot)
{
    if (m_busy) {
        qDebug() << "[ActionRunner] Frame Dope Sheet ignorado: ya hay una secuencia corriendo";
        return false;
    }
    if (!nukeFrame.isValid()) {
        qWarning() << "[ActionRunner] Frame Dope Sheet sin marco de Nuke valido";
        return false;
    }
    const QPoint target = spotToNative(nukeFrame, spot);
    qInfo() << "[ActionRunner] Frame Dope Sheet: marco" << nukeFrame << "punto" << spot << "->" << target;
    InputInjector *in = m_injector;
    // El puntero se guarda al empezar y se devuelve al final: el usuario no lo ve moverse.
    auto origin = std::make_shared<QPoint>(in->cursorPos());
    start({
        {0, [in]() { in->releaseModifiers(); }},
        {kAfterReleaseMs, [in, target]() { in->clickAt(InputInjector::Button::Left, target); }},
        {kAfterFocusClickMs, [in]() { in->tapKey(InputInjector::Key::A, true); }},
        {kBetweenKeysMs, [in]() { in->tapKey(InputInjector::Key::F); }},
        {kBeforeRestoreMs, [in, origin]() { in->moveCursor(*origin); }},
    });
    return true;
}

void ActionRunner::start(const QVector<Step> &steps)
{
    m_steps = steps;
    m_index = 0;
    m_busy = true;
    runNext();
}

void ActionRunner::runNext()
{
    if (m_index >= m_steps.size()) {
        m_busy = false;
        m_steps.clear();
        emit finished();
        return;
    }
    const Step step = m_steps.at(m_index++);
    const auto execute = [this, step]() {
        step.run();
        runNext();
    };
    if (step.delayBeforeMs <= 0) {
        execute();
    } else {
        QTimer::singleShot(step.delayBeforeMs, this, execute);
    }
}
