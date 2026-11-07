#include "modules/dictation/DictationModule.h"

#include "app/ModuleContext.h"
#include "core/I18n.h"
#include "modules/dictation/DictationEngine.h"
#include "modules/dictation/DictationPanel.h"
#include "modules/dictation/DictationQa.h"
#include "platform/TextTyper.h"

#include <QDebug>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QTimer>
#include <QWidget>

namespace {

// Ids locales de los atajos: el del dictado y Esc (solo mientras hay un dictado en curso). Esc va dos veces:
// sola y con los modificadores del atajo, porque en modo mantener Win (o lo que tenga el atajo) sigue apretado
// y el gancho compara los modificadores exactos: Win+Esc no es Esc.
constexpr int kShortcutId = 1;
constexpr int kEscapeId = 2;
constexpr int kEscapeHeldId = 3;
constexpr int kTickMs = 100;

#if defined(Q_OS_MACOS)
// ⌃⌥H (en Qt/mac «Meta» es Control), el par de Win+H. No ⌥ + una letra: desde macOS 15 Carbon rechaza los
// atajos con solo Option (u Option+Shift). ⌃H solo no: en la Terminal y en los campos de texto borra. Los
// atajos de la app no admiten Espacio (Shortcut::isSupportedKey).
const QString kDefaultShortcut = QStringLiteral("Meta+Alt+H");
#else
const QString kDefaultShortcut = QStringLiteral("Meta+H");
#endif
const QString kDefaultLanguage = QStringLiteral("es");
// Palabras de VFX y de desarrollo que Whisper no conoce o escribe en inglés ("Nook Studio", "pujear").
// Probadas con grabaciones reales: con "pullear" en la lista, "pushear" salía "pullear".
const QString kDefaultWords = QStringLiteral(
    "Nuke, NukeX, Nuke Studio, Hiero, comp, compo, roto, DMP, matte painting, keying, tracking, plate, render, "
    "shot, After Effects, Photoshop, DaVinci Resolve, Houdini, Blender, Maya, Mighty Tools, comitear, pushear, "
    "mergear, branch, repo, release, deploy, bug, GitHub, Python, Qt");
// La lista de antes: quien la tiene guardada sin cambios recibe la nueva.
const QString kOldDefaultWords = QStringLiteral("Nuke, NukeX, Nuke Studio, Hiero, comp, roto");
constexpr int kDefaultUnload = 10;

// Un micrófono: la cápsula, el soporte en U y el pie. Mismo trazo que los vecinos (1,4 sobre 16).
void paintDictationIcon(QPainter &painter, const QRectF &rect, const QColor &color)
{
    const qreal scale = qMin(rect.width(), rect.height()) / 16.0;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.translate(rect.center().x() - 8 * scale, rect.center().y() - 8 * scale);
    painter.scale(scale, scale);
    painter.setPen(QPen(color, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(5.6, 1.4, 4.8, 8.4), 2.4, 2.4);
    QPainterPath stand;
    stand.moveTo(3.4, 7.4);
    stand.arcTo(QRectF(3.4, 2.8, 9.2, 9.2), 180, 180);
    stand.moveTo(8, 12.0);
    stand.lineTo(8, 14.6);
    stand.moveTo(5.6, 14.6);
    stand.lineTo(10.4, 14.6);
    painter.drawPath(stand);
    painter.restore();
}

} // namespace

DictationModule::DictationModule(ModuleContext &context)
    : Module(context)
    , m_flow(*this)
{
    connect(&m_flow, &DictationFlow::finished, this, &DictationModule::addHistory);
    m_clock.start();
}

DictationModule::~DictationModule()
{
    stop();
}

// ------------------------------------------------------------------ Vida

void DictationModule::start()
{
    if (m_started) {
        return;
    }
    m_started = true;
    // Al arrancar la app no se carga el motor (memoria de video para algo que quizas no se use hoy): se carga
    // al prenderla el usuario o con el primer dictado, que guarda el audio mientras carga (~1 s).
    m_warmUp = !context().startingWithApp() && preloadAllowed();
    applyFlowSettings();
    registerShortcut();
    openModels();
    onPieceChanged();
}

void DictationModule::stop()
{
    if (!m_started) {
        return;
    }
    m_started = false;
    // Un dictado en curso se corta sin escribir nada: micrófono cerrado, motor cancelado, Esc retirado.
    m_flow.shutdown();
    unregisterAll();
    delete m_tick;
    m_tick = nullptr;
    delete m_overlay.data();
    delete m_typer;
    m_typer = nullptr;
    delete m_capture;
    m_capture = nullptr;
    stopEngine();
    closeModels();
    m_engineError.clear();
}

void DictationModule::registerShortcut()
{
    if (!m_started) {
        return;
    }
    if (!m_hotkeys) {
        m_hotkeys = context().hotkeys();
        connect(m_hotkeys, &ModuleHotkeys::activated, this, [this](int id) {
            if (id == kShortcutId) {
                m_flow.press(m_clock.elapsed());
            } else if (id == kEscapeId || id == kEscapeHeldId) {
                m_flow.escape();
            }
        });
        connect(m_hotkeys, &ModuleHotkeys::released, this, [this](int id) {
            if (id == kShortcutId) {
                m_flow.release(m_clock.elapsed());
            }
        });
    }
    m_hotkeys->unregisterHotkey(kShortcutId);
    m_hotkeys->declare(kShortcutId, Shortcut());
    m_shortcutError.clear();
    const Shortcut keys = shortcut();
    const QString takenBy = m_hotkeys->declare(kShortcutId, keys);
    if (!takenBy.isEmpty()) {
        m_shortcutError = I18n::tr("%1 is already used by %2.").arg(keys.displayText(), takenBy);
    } else if (!m_hotkeys->registerPriorityHotkey(kShortcutId, keys)) {
        // Con prioridad: el gancho ve la liberación (modo mantener) y Win + letra le gana a Windows. En mac,
        // Carbon avisa al soltar, pero no le gana a otra app ni a macOS.
#if defined(Q_OS_MACOS)
        m_shortcutError = I18n::tr("Another app or macOS uses %1.").arg(keys.displayText());
#else
        m_shortcutError = I18n::tr("Could not activate %1 with priority.").arg(keys.displayText());
#endif
    }
    if (!m_shortcutError.isEmpty()) {
        qWarning().noquote() << "[Dictado]" << m_shortcutError;
    }
}

void DictationModule::unregisterAll()
{
    if (!m_hotkeys) {
        return;
    }
    disconnect(m_hotkeys, nullptr, this, nullptr);
    m_hotkeys->unregisterAll();
    m_hotkeys->declare(kShortcutId, Shortcut());
    m_hotkeys = nullptr;
    m_escRegistered = false;
    m_escHeldRegistered = false;
}

void DictationModule::openModels()
{
    if (m_models) {
        return;
    }
    m_models = new DictationModels(modelsRoot(), DictationModels::defaultPieces(), this);
    connect(m_models, &DictationModels::changed, this, &DictationModule::onPieceChanged);
    m_models->refresh();
}

void DictationModule::closeModels()
{
    // Una descarga a medias se corta y su .part se borra (DictationModels).
    delete m_models;
    m_models = nullptr;
}

void DictationModule::onPieceChanged()
{
    if (m_started && m_warmUp && piecesReady()
        && (m_engineState == EngineState::Off || m_engineState == EngineState::Error)) {
        m_warmUp = false;
        ensureEngine();
    }
    notifyChanged();
}

bool DictationModule::preloadAllowed() const
{
    return !context().automatedRun();
}

void DictationModule::ensureEngine()
{
    if (!m_models || !m_models->allReady()) {
        return;
    }
    if (!m_engine) {
        m_engine = new DictationEngine(this);
        connect(m_engine, &DictationEngine::ready, this, [this]() {
            m_engineState = EngineState::Ready;
            m_engineError.clear();
            m_flow.engineReady();
            notifyChanged();
        });
        connect(m_engine, &DictationEngine::unloaded, this, [this]() {
            m_engineState = EngineState::Unloaded;
            notifyChanged();
        });
        connect(m_engine, &DictationEngine::failed, this, [this](const QString &error) {
            m_engineState = EngineState::Error;
            m_engineError = error;
            if (m_engine->engineDamaged() && m_models) {
                // No se cargo nada: el motor queda en error en su fila, con el tacho para bajarlo de nuevo.
                m_models->markDamaged(DictationModels::kEngine, error);
            }
            m_flow.engineFailed(error);
            notifyChanged();
        });
        connect(m_engine, &DictationEngine::partial, &m_flow, &DictationFlow::partial);
        connect(m_engine, &DictationEngine::final, &m_flow, &DictationFlow::final);
        connect(m_engine, &DictationEngine::limitReached, &m_flow, &DictationFlow::limitReached);
    }
    if (m_engine->isRunning()) {
        return;
    }
    // Antes de start(): si falla enseguida, failed() deja el estado en Error.
    m_engineState = EngineState::Loading;
    m_engine->setIdleUnloadMinutes(unloadMinutes());
    m_engine->start(m_models->engineDir(), m_models->modelPath());
    notifyChanged();
}

void DictationModule::stopEngine()
{
    // Destruirlo mata el proceso del motor y devuelve toda su memoria de video.
    delete m_engine;
    m_engine = nullptr;
    m_engineState = EngineState::Off;
}

void DictationModule::applyFlowSettings()
{
    const QString code = language();
    m_flow.setContext(vocabularyFrom(customWords()), contextPhrase(code));
    m_flow.setReplacements(replacements());
    m_flow.setMaxDurationMs(DictationEngine::kMaxSeconds * 1000);
}

void DictationModule::addHistory(const DictationFlow::Entry &entry)
{
    m_history.prepend(entry);
    while (m_history.size() > kHistoryMax) {
        m_history.removeLast();
    }
    emit changed();
}

void DictationModule::notifyChanged()
{
    emit statusChanged();
    emit changed();
}

// ------------------------------------------------------------------ DictationFlow::Io

quintptr DictationModule::foregroundWindow()
{
    return TextTyper::foregroundWindow();
}

QString DictationModule::engineProblem()
{
    if (!piecesReady()) {
        return I18n::tr("Download the dictation model in Mighty Tools first.");
    }
    return QString();
}

bool DictationModule::engineReady()
{
    return m_engine && m_engine->isReady();
}

void DictationModule::engineBegin()
{
    ensureEngine(); // la primera vez sin precarga, o despues de descargarse por inactividad
    if (!m_engine || m_flow.phase() != DictationFlow::Phase::Recording) {
        return;
    }
    const QString code = language();
    m_engine->begin(code == QLatin1String("auto") ? QString() : code, vocabularyFrom(customWords()), contextPhrase(code));
}

void DictationModule::engineFeed(const QVector<float> &samples)
{
    if (m_engine) m_engine->feed(samples);
}

void DictationModule::engineFinish()
{
    if (m_engine) m_engine->finish();
}

void DictationModule::engineCancel()
{
    if (m_engine) m_engine->cancel();
}

bool DictationModule::startCapture()
{
    if (!m_capture) {
        m_capture = new AudioCapture(this);
        connect(m_capture, &AudioCapture::samples, &m_flow, &DictationFlow::samples);
        connect(m_capture, &AudioCapture::level, &m_flow, &DictationFlow::level);
        connect(m_capture, &AudioCapture::failed, this,
                [this](AudioCapture::Error, const QString &message) { m_flow.captureFailed(message); });
    }
    return m_capture->start(microphone());
}

void DictationModule::stopCapture()
{
    if (m_capture) m_capture->stop();
}

bool DictationModule::type(quintptr target, const QString &text)
{
    if (!m_typer) {
        m_typer = new TextTyper(this);
        connect(m_typer, &TextTyper::finished, &m_flow, &DictationFlow::typed);
    }
    return m_typer->type(target, text);
}

void DictationModule::stopTyping()
{
    if (m_typer) m_typer->stop();
}

void DictationModule::showOverlay(quintptr target)
{
    if (context().automatedRun()) {
        qInfo() << "[Dictado] (automatizada) la ventanita no se muestra";
        return;
    }
    if (!m_overlay) {
        m_overlay = new DictationOverlay(); // ventana propia; se borra al apagar
    }
    m_overlay->showFor(target);
}

void DictationModule::overlayState(DictationOverlay::State state, const QString &message)
{
    if (m_overlay) m_overlay->setState(state, message);
}

void DictationModule::overlayText(const QString &confirmed, const QString &provisional)
{
    if (m_overlay) m_overlay->setText(confirmed, provisional);
}

void DictationModule::overlayLevel(float level)
{
    if (m_overlay) m_overlay->setLevel(level);
}

void DictationModule::hideOverlay(int delayMs)
{
    if (m_overlay) m_overlay->hideSoon(delayMs);
}

void DictationModule::setActive(bool active)
{
    if (active) {
        if (!m_tick) {
            m_tick = new QTimer(this);
            m_tick->setInterval(kTickMs);
            connect(m_tick, &QTimer::timeout, this, [this]() { m_flow.tick(m_clock.elapsed()); });
        }
        m_tick->start();
        if (!m_hotkeys) {
            return;
        }
        // Cada Esc con su propio flag: si una no se pudo registrar, la otra igual se registra y se retira.
        Shortcut escape;
        escape.key = Qt::Key_Escape;
        if (!m_escRegistered) {
            m_escRegistered = m_hotkeys->registerPriorityHotkey(kEscapeId, escape);
        }
        escape.modifiers = shortcut().modifiers;
        if (!m_escHeldRegistered && escape.modifiers != Qt::NoModifier) {
            m_escHeldRegistered = m_hotkeys->registerPriorityHotkey(kEscapeHeldId, escape);
        }
        return;
    }
    if (m_tick) m_tick->stop();
    if (m_hotkeys && m_escRegistered) m_hotkeys->unregisterHotkey(kEscapeId);
    if (m_hotkeys && m_escHeldRegistered) m_hotkeys->unregisterHotkey(kEscapeHeldId);
    m_escRegistered = false;
    m_escHeldRegistered = false;
}

// ------------------------------------------------------------------ Ajustes

Shortcut DictationModule::shortcut() const
{
    const Shortcut keys = Shortcut::fromPortableString(context().value(QStringLiteral("shortcut"), kDefaultShortcut).toString());
    return keys.isValid() ? keys : Shortcut::fromPortableString(kDefaultShortcut);
}

QString DictationModule::validateShortcut(const Shortcut &keys) const
{
    if (!keys.isValid()) {
        return I18n::tr("Choose a shortcut.");
    }
    ModuleHotkeys *hotkeys = m_hotkeys ? m_hotkeys : (context().captureMode() ? nullptr : context().hotkeys());
    const QString takenBy = hotkeys ? hotkeys->declaredByOtherModule(keys) : QString();
    return takenBy.isEmpty() ? QString() : I18n::tr("Already used by %1.").arg(takenBy);
}

bool DictationModule::setShortcut(const Shortcut &keys, QString *error)
{
    const QString invalid = validateShortcut(keys);
    if (!invalid.isEmpty()) {
        if (error) *error = invalid;
        return false;
    }
    const Shortcut previous = shortcut();
    context().setValue(QStringLiteral("shortcut"), keys.toPortableString());
    registerShortcut();
    if (!m_shortcutError.isEmpty()) {
        // El sistema no lo tomo: vuelve el anterior y se dice por que.
        if (error) *error = m_shortcutError;
        context().setValue(QStringLiteral("shortcut"), previous.toPortableString());
        registerShortcut();
        notifyChanged();
        return false;
    }
    notifyChanged();
    return true;
}

QString DictationModule::language() const
{
    const QString code = context().value(QStringLiteral("language"), kDefaultLanguage).toString();
    return languageCodes().contains(code) ? code : kDefaultLanguage;
}

void DictationModule::setLanguage(const QString &code)
{
    if (!languageCodes().contains(code)) return;
    context().setValue(QStringLiteral("language"), code);
    applyFlowSettings();
    emit changed();
}

QString DictationModule::microphone() const
{
    return context().value(QStringLiteral("microphone"), QString()).toString();
}

void DictationModule::setMicrophone(const QString &id)
{
    context().setValue(QStringLiteral("microphone"), id);
    emit changed();
}

QString DictationModule::customWords() const
{
    const QString words = context().value(QStringLiteral("customWords"), kDefaultWords).toString();
    return words == kOldDefaultWords ? kDefaultWords : words;
}

QString DictationModule::defaultWords()
{
    return kDefaultWords;
}

void DictationModule::setCustomWords(const QString &words)
{
    context().setValue(QStringLiteral("customWords"), vocabularyFrom(words).join(QStringLiteral(", ")));
    applyFlowSettings();
    emit changed();
}

DictationText::Replacements DictationModule::replacements() const
{
    return replacementsFromJson(context().value(QStringLiteral("replacements"), QString()).toString());
}

void DictationModule::setReplacements(const DictationText::Replacements &list)
{
    context().setValue(QStringLiteral("replacements"), replacementsToJson(list));
    applyFlowSettings();
    emit changed();
}

QString DictationModule::modelsRoot() const
{
    const QString root = QDir::cleanPath(QDir::fromNativeSeparators(context().value(QStringLiteral("modelsRoot"), QString()).toString().trimmed()));
    return fullPath(root) ? root : DictationModels::defaultRoot();
}

bool DictationModule::fullPath(const QString &path)
{
#if defined(Q_OS_MACOS)
    // En mac, una ruta absoluta de verdad: "/" y algo mas, sin "~" ni relativas.
    const QString p = path.trimmed();
    return p.size() >= 2 && p.startsWith(QLatin1Char('/')) && !p.startsWith(QLatin1String("//"));
#else
    // Unidad + barra ("C:/x") o UNC ("//servidor/recurso"). QDir::isAbsolutePath acepta tambien "\x" y
    // "C:x", que Windows resuelve contra la unidad o la carpeta actual (regla 8 de Doc_Borrado_Seguro).
    const QString p = QDir::fromNativeSeparators(path.trimmed());
    if (p.size() >= 3 && p.at(0).isLetter() && p.at(0).unicode() < 128 && p.at(1) == QLatin1Char(':') && p.at(2) == QLatin1Char('/')) {
        return true;
    }
    if (!p.startsWith(QLatin1String("//"))) {
        return false;
    }
    const QStringList parts = p.mid(2).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    return parts.size() >= 2 && parts.at(0) != QLatin1String("?") && parts.at(0) != QLatin1String(".");
#endif
}

void DictationModule::setModelsRoot(const QString &root)
{
    const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(root.trimmed()));
    if (!fullPath(clean) || clean == modelsRoot()) {
        return;
    }
    // La de por defecto no se guarda: si la app cambia de carpeta, la sigue.
    context().setValue(QStringLiteral("modelsRoot"),
                       QDir(clean) == QDir(DictationModels::defaultRoot()) ? QString() : QDir::toNativeSeparators(clean));
    if (m_started) {
        m_flow.shutdown();
        stopEngine();
        closeModels();
        openModels();
        onPieceChanged();
        return;
    }
    notifyChanged();
}

int DictationModule::unloadMinutes() const
{
    const int minutes = context().value(QStringLiteral("unloadMinutes"), kDefaultUnload).toInt();
    return unloadChoices().contains(minutes) ? minutes : kDefaultUnload;
}

void DictationModule::setUnloadMinutes(int minutes)
{
    if (!unloadChoices().contains(minutes)) return;
    context().setValue(QStringLiteral("unloadMinutes"), minutes);
    if (m_engine) m_engine->setIdleUnloadMinutes(minutes);
    emit changed();
}

// ------------------------------------------------------------------ Motor y modelo

QList<DictationModule::PieceInfo> DictationModule::pieces() const
{
    if (context().captureMode()) {
        return m_capturePieces;
    }
    QList<PieceInfo> list;
    for (const DictationModels::Piece &piece : DictationModels::defaultPieces()) {
        PieceInfo info;
        info.id = piece.id;
        info.title = piece.title;
        info.license = piece.license;
        info.size = piece.size;
        if (m_models) {
            info.state = m_models->state(piece.id);
            info.progress = m_models->progress(piece.id);
            info.retryAttempt = m_models->retryAttempt(piece.id);
            info.error = m_models->errorText(piece.id);
        }
        list << info;
    }
    return list;
}

bool DictationModule::piecesReady() const
{
    for (const PieceInfo &piece : pieces()) {
        if (piece.state != DictationModels::State::Ready) return false;
    }
    return true;
}

void DictationModule::download(const QString &id)
{
    // Lo pidio el usuario: al quedar listo, el motor se carga para el primer dictado.
    m_warmUp = preloadAllowed();
    if (m_models) m_models->download(id);
}

void DictationModule::downloadMissing()
{
    m_warmUp = preloadAllowed();
    if (m_models) m_models->downloadMissing();
}

void DictationModule::cancelDownload(const QString &id)
{
    if (m_models) m_models->cancel(id);
}

bool DictationModule::removePiece(const QString &id, QString *error)
{
    if (!m_models) {
        return false;
    }
    // El motor no puede tener cargado lo que se borra: se apaga antes.
    m_flow.shutdown();
    stopEngine();
    const bool removed = m_models->remove(id, error);
    notifyChanged();
    return removed;
}

void DictationModule::refreshModels()
{
    if (m_models) m_models->refresh();
}

QList<AudioCapture::Device> DictationModule::devices() const
{
    if (context().captureMode()) {
        return {{QStringLiteral("{0.0.1.00000000}.{qa-usb}"), QStringLiteral("Microphone (USB Audio Device)")},
                {QStringLiteral("{0.0.1.00000000}.{qa-headset}"), QStringLiteral("Headset Microphone (Wireless)")}};
    }
    return AudioCapture::devices();
}

// ------------------------------------------------------------------ Estado

ModuleStatus DictationModule::status() const
{
    if (!m_shortcutError.isEmpty() || m_engineState == EngineState::Error) {
        return {ModuleTone::Error, I18n::tr("Needs attention")};
    }
    for (const PieceInfo &piece : pieces()) {
        if (piece.state == DictationModels::State::Error) return {ModuleTone::Error, I18n::tr("Download failed")};
    }
    for (const PieceInfo &piece : pieces()) {
        if (piece.state == DictationModels::State::Downloading && piece.retryAttempt > 0) {
            // Corto: es el estado de la barra lateral (nav-fit). La pagina lo dice entero.
            return {ModuleTone::Active, I18n::tr("Retrying %1/%2").arg(piece.retryAttempt).arg(DictationModels::kAttempts)};
        }
        if (piece.state == DictationModels::State::Downloading) {
            return {ModuleTone::Active, I18n::tr("Downloading %1%").arg(piece.progress)};
        }
        if (piece.state == DictationModels::State::Verifying) return {ModuleTone::Active, I18n::tr("Verifying")};
    }
    if (!piecesReady()) {
        return {ModuleTone::Attention, I18n::tr("Needs download")};
    }
    if (m_engineState == EngineState::Loading) {
        return {ModuleTone::Active, I18n::tr("Loading")};
    }
    return {ModuleTone::Active, I18n::tr("Ready · %1").arg(shortcut().displayText())};
}

QWidget *DictationModule::createPanel(QWidget *parent)
{
    auto *panel = new DictationPanel(this, !context().captureMode(), parent);
    connect(this, &DictationModule::changed, panel, &DictationPanel::refresh);
    if (m_captureState == QLatin1String("replacements-edit")) panel->showReplacementEditorForCapture();
    if (m_captureState == QLatin1String("confirm-delete")) panel->armPieceDeleteForCapture(DictationModels::kModel);
    return panel;
}

QStringList DictationModule::captureStates() const
{
    return {QStringLiteral("ready"),  QStringLiteral("missing"), QStringLiteral("downloading"),
            QStringLiteral("retrying"), QStringLiteral("error"),  QStringLiteral("recent"),
            QStringLiteral("replacements-edit"), QStringLiteral("confirm-delete")};
}

bool DictationModule::applyCaptureState(const QString &state)
{
    if (!context().captureMode() || !captureStates().contains(state)) {
        return false;
    }
    m_captureState = state;
    m_capturePieces.clear();
    for (const DictationModels::Piece &piece : DictationModels::defaultPieces()) {
        PieceInfo info;
        info.id = piece.id;
        info.title = piece.title;
        info.license = piece.license;
        info.size = piece.size;
        info.state = DictationModels::State::Ready;
        info.progress = 100;
        m_capturePieces << info;
    }
    PieceInfo &engine = m_capturePieces[0];
    PieceInfo &model = m_capturePieces[1];
    m_engineState = EngineState::Ready;
    m_engineError.clear();
    m_history.clear();
    if (state == QLatin1String("missing")) {
        engine.state = model.state = DictationModels::State::Missing;
        engine.progress = model.progress = 0;
        m_engineState = EngineState::Off;
    } else if (state == QLatin1String("downloading")) {
        model.state = DictationModels::State::Downloading;
        model.progress = 42;
        m_engineState = EngineState::Off;
    } else if (state == QLatin1String("retrying")) {
        // Se corto a la mitad y espera el segundo intento (regla de red).
        model.state = DictationModels::State::Downloading;
        model.progress = 42;
        model.retryAttempt = 2;
        m_engineState = EngineState::Off;
    } else if (state == QLatin1String("error")) {
        model.state = DictationModels::State::Error;
        model.progress = 0;
        model.error = I18n::tr("The download failed: %1").arg(QStringLiteral("Connection closed by the server."));
        m_engineState = EngineState::Off;
    } else if (state == QLatin1String("recent")) {
        const QDateTime now = QDateTime::currentDateTime();
        const auto entry = [&now](int minutesAgo, const QString &text, bool typed, const QString &note) {
            return DictationFlow::Entry{now.addSecs(-60 * minutesAgo), text, typed, note};
        };
        m_history = {entry(1, QStringLiteral("Che, revisá el comp en Nuke Studio y fijate si el roto anda bien."), true,
                           DictationFlow::noteFor(TextTyper::Result::Typed, false)),
                     entry(4, QStringLiteral("Mandale el render a Lucía cuando termine."), false,
                           DictationFlow::noteFor(TextTyper::Result::FocusChanged, false)),
                     entry(12, QStringLiteral("Abrí Hiero con la secuencia nueva."), false,
                           DictationFlow::noteFor(TextTyper::Result::Elevated, false)),
                     entry(30, QStringLiteral("Esto no va"), false, I18n::tr("Cancelled"))};
    } else if (state == QLatin1String("replacements-edit")) {
        context().setValue(QStringLiteral("replacements"),
                           replacementsToJson({{QStringLiteral("jairo"), QStringLiteral("Hiero")},
                                               {QStringLiteral("nuke estudio"), QStringLiteral("Nuke Studio")}}));
    }
    return true;
}

// ------------------------------------------------------------------ Textos y conversiones

QStringList DictationModule::languageCodes()
{
    return {QStringLiteral("es"), QStringLiteral("en"), QStringLiteral("pt"), QStringLiteral("it"),
            QStringLiteral("fr"), QStringLiteral("de"), QStringLiteral("auto")};
}

QString DictationModule::languageName(const QString &code)
{
    if (code == QLatin1String("en")) return I18n::tr("English");
    if (code == QLatin1String("pt")) return I18n::tr("Portuguese");
    if (code == QLatin1String("it")) return I18n::tr("Italian");
    if (code == QLatin1String("fr")) return I18n::tr("French");
    if (code == QLatin1String("de")) return I18n::tr("German");
    if (code == QLatin1String("auto")) return I18n::tr("Auto-detect");
    return I18n::tr("Spanish");
}

QList<int> DictationModule::unloadChoices()
{
    return {0, 5, 10, 30, 60};
}

QString DictationModule::unloadText(int minutes)
{
    return minutes <= 0 ? I18n::tr("Never") : I18n::tr("%1 min").arg(minutes);
}

QStringList DictationModule::vocabularyFrom(const QString &words)
{
    QStringList list;
    // Coma o renglón: una lista pegada de a un término por línea también vale.
    for (const QString &word : words.split(QRegularExpression(QStringLiteral("[,\\r\\n]")))) {
        const QString clean = word.simplified();
        if (!clean.isEmpty() && !list.contains(clean, Qt::CaseInsensitive)) list << clean;
    }
    return list;
}

QString DictationModule::contextPhrase(const QString &language)
{
    // Una frase en el registro del usuario (voseo, puntuacion): Whisper la imita en la pasada final. Solo en
    // castellano; en otro idioma lo empujaria a traducir.
    return language == QLatin1String("es")
               ? QStringLiteral("Che, ¿vos ya revisaste el comp en Nuke Studio? Fijate si el roto anda bien.")
               : QString();
}

QString DictationModule::replacementsToJson(const DictationText::Replacements &list)
{
    QJsonArray array;
    for (const auto &pair : list) {
        array.append(QJsonArray{pair.first, pair.second});
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

DictationText::Replacements DictationModule::replacementsFromJson(const QString &json)
{
    DictationText::Replacements list;
    const QJsonArray array = QJsonDocument::fromJson(json.toUtf8()).array();
    for (const QJsonValue &value : array) {
        const QJsonArray pair = value.toArray();
        const QString from = pair.at(0).toString().simplified();
        if (pair.size() == 2 && !from.isEmpty()) list.append({from, pair.at(1).toString().simplified()});
    }
    return list;
}

qint64 DictationModule::enginePidForQa() const
{
    return m_engine ? m_engine->workerPid() : 0;
}

bool DictationModule::escapeRegisteredForQa() const
{
    return m_escRegistered || m_escHeldRegistered;
}

// ------------------------------------------------------------------ Descriptor y ayuda

ModuleDescriptor dictationDescriptor()
{
    ModuleDescriptor d;
    d.id = QStringLiteral("dictation");
    d.title = QStringLiteral("Dictation");
    d.description = I18n::tr("Speak into any text field with a shortcut.");
    d.offBulletsFor = [](const SettingsReader &value) {
        const Shortcut keys = Shortcut::fromPortableString(value(QStringLiteral("shortcut"), kDefaultShortcut).toString());
        return QStringList{I18n::tr("Press %1 in any text field and speak").arg((keys.isValid() ? keys : Shortcut::fromPortableString(kDefaultShortcut)).displayText()),
                           I18n::tr("Shows the text as you speak and types it when you finish"),
                           I18n::tr("Runs on this computer: the model is downloaded once")};
    };
    d.offBullets = d.offBulletsFor([](const QString &, const QVariant &fallback) { return fallback; });
    d.platforms = PlatformWindows | PlatformMac;
    d.paintIcon = &paintDictationIcon;
    d.create = [](ModuleContext &context) -> std::unique_ptr<Module> {
        return std::make_unique<DictationModule>(context);
    };
    d.configuredShortcuts = [](const SettingsReader &value) {
        const Shortcut keys = Shortcut::fromPortableString(value(QStringLiteral("shortcut"), kDefaultShortcut).toString());
        return QList<Shortcut>{keys.isValid() ? keys : Shortcut::fromPortableString(kDefaultShortcut)};
    };
    d.selfTest = &DictationQa::selfTest;
    d.simulateAction = &DictationQa::simulateAction;
    return d;
}

HelpSection dictationHelp(const SettingsReader &value)
{
    const Shortcut keys = Shortcut::fromPortableString(value(QStringLiteral("shortcut"), kDefaultShortcut).toString());
    HelpSection help;
    help.title = QStringLiteral("Dictation");
    help.steps = {I18n::tr("Download the model from the tool's page. It's done once and stays on this computer."),
                  I18n::tr("Click in any text field and press %1. Tap it to start and again to finish, or hold it while you speak.")
                      .arg(HelpSection::strong((keys.isValid() ? keys : Shortcut::fromPortableString(kDefaultShortcut)).displayText())),
                  I18n::tr("The text shows up as you speak and is typed when you finish. Esc cancels, also while it types.")};
#if defined(Q_OS_MACOS)
    help.note = I18n::tr("Nothing goes through the clipboard. If the window changes before typing, the text waits in Recent, on the tool's page. "
                         "The first time, macOS asks for the microphone and for Accessibility, which is needed to type in other apps.");
#else
    help.note = I18n::tr("Nothing goes through the clipboard. If the window changes before typing, the text waits in Recent, on the tool's page. "
                         "With an administrator window in front the shortcut doesn't arrive, so Windows opens its own dictation.");
#endif
    return help;
}
