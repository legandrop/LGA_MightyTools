#include "modules/nukeshortcuts/NukeShortcutsModule.h"
#include "core/I18n.h"

#include "app/HotkeyHub.h"
#include "app/ModuleContext.h"
#include "app/ModuleContextImpl.h"
#include "app/ModuleHost.h"
#include "app/SettingsStore.h"
#include "modules/nukeshortcuts/ActionRunner.h"
#include "modules/nukeshortcuts/CalibrationDialog.h"
#include "modules/nukeshortcuts/CalibrationSession.h"
#include "modules/nukeshortcuts/NukeShortcutsPanel.h"
#include "platform/InputInjector.h"
#include "platform/NukeWatcher.h"
#include "platform/SystemInput.h"
#include "ui/ShortcutRow.h"

#include <QAction>
#include <QCoreApplication>
#include <QDebug>
#include <QDesktopServices>
#include <QEventLoop>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QUrl>

#include <cstdio>

namespace {

const QString kId = QStringLiteral("nukeShortcuts");

// En macOS el permiso de Accesibilidad no avisa cuando cambia: se consulta cada tanto.
constexpr int kAccessibilityPollMs = 2000;

int hotkeyId(ShortcutAction action)
{
    return action == ShortcutAction::AddKeyframe ? NukeShortcutsModule::kAddKeyframeId : NukeShortcutsModule::kFrameDopeSheetId;
}

// Icono de la herramienta (canvas `I.nuke`): dos rombos, el del Dope Sheet abierto.
void paintNukeIcon(QPainter &painter, const QRectF &rect, const QColor &color)
{
    const qreal scale = qMin(rect.width(), rect.height()) / 16.0;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.translate(rect.center().x() - 8 * scale, rect.center().y() - 8 * scale);
    painter.scale(scale, scale);
    painter.setPen(QPen(color, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    QPainterPath path;
    path.moveTo(5, 3.5);
    path.lineTo(8.5, 8);
    path.lineTo(5, 12.5);
    path.lineTo(1.5, 8);
    path.closeSubpath();
    path.moveTo(11, 5);
    path.lineTo(13.5, 8);
    path.lineTo(11, 11);
    path.lineTo(8.5, 8);
    painter.drawPath(path);
    painter.restore();
}

Shortcut configured(const SettingsReader &value, ShortcutAction action)
{
    const Shortcut stored =
        Shortcut::fromPortableString(value(NukeShortcutsState::shortcutKey(action), QString()).toString());
    if (stored.isValid()) {
        return stored;
    }
    return action == ShortcutAction::AddKeyframe ? Shortcut::defaultAddKeyframe() : Shortcut::defaultFrameDopeSheet();
}

// --simulate-action <add-keyframe|frame-dope-sheet>: la secuencia REAL de ActionRunner con un
// InputInjector en solo loguear (no mueve el mouse ni aprieta nada) e imprime los pasos.
int simulateAction(const QString &which, const QStringList &)
{
    InputInjector injector(true);
    ActionRunner runner(&injector);
    QEventLoop loop;
    QObject::connect(&runner, &ActionRunner::finished, &loop, &QEventLoop::quit);
    bool started = false;
    if (which == QLatin1String("add-keyframe")) {
        started = runner.runAddKeyframe();
    } else if (which == QLatin1String("frame-dope-sheet")) {
        started = runner.runFrameDopeSheet(QRect(0, 0, 3440, 1440), QPointF(0.89, 0.72));
    } else {
        return 2; // no es de esta herramienta
    }
    if (!started) {
        std::fprintf(stderr, "simulate-action: la secuencia no arranco\n");
        return 1;
    }
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();
    for (const QString &step : injector.steps()) {
        std::printf("%s\n", qPrintable(step));
    }
    return runner.isBusy() ? 1 : 0;
}

// Modulo de prueba del self-test: declara una combinacion como lo haria Folder Switch.
class ClaimModule : public Module
{
public:
    ClaimModule(ModuleContext &context, const Shortcut &shortcut) : Module(context), m_shortcut(shortcut) {}
    void start() override { context().hotkeys()->declare(1, m_shortcut); }
    void stop() override { context().hotkeys()->declare(1, Shortcut()); }
    ModuleStatus status() const override { return {ModuleTone::Active, QStringLiteral("On")}; }
    QWidget *createPanel(QWidget *) override { return nullptr; }

private:
    Shortcut m_shortcut;
};

void selfTest(const std::function<void(bool, const QString &)> &check)
{
    // Nuke por el nombre del ejecutable.
    for (const char *yes : {"Nuke15.1.exe", "Nuke16.0.exe", "nuke14.0.exe", "Nuke15.1", "Nuke15.1v4", "Nuke.exe"}) {
        check(NukeWatcher::isNukeExecutable(QString::fromLatin1(yes)), QStringLiteral("es Nuke: %1").arg(QLatin1String(yes)));
    }
    for (const char *no : {"LGA_MightyTools.exe", "NukeShortcuts.exe", "NukeX.exe", "Nuke15.1.exe.bak", "explorer.exe",
                           "Nukeitall.exe", ""}) {
        check(!NukeWatcher::isNukeExecutable(QString::fromLatin1(no)), QStringLiteral("no es Nuke: '%1'").arg(QLatin1String(no)));
    }

    // Atajos: ida y vuelta por el texto del .ini, y textos invalidos.
    for (const Shortcut &s : {Shortcut::defaultAddKeyframe(), Shortcut::defaultFrameDopeSheet()}) {
        check(Shortcut::fromPortableString(s.toPortableString()) == s, QStringLiteral("ida y vuelta: %1").arg(s.toPortableString()));
    }
    check(Shortcut::defaultAddKeyframe().toPortableString() == QLatin1String("Ctrl+Shift+D"),
          QStringLiteral("texto portable de Add keyframe"));
    for (const char *bad : {"", "Ctrl+Shift+", "Ctrl+Shift+!", "Ctrl+Space", "Ctrl+A, Ctrl+B", "basura"}) {
        check(!Shortcut::fromPortableString(QString::fromLatin1(bad)).isValid(),
              QStringLiteral("atajo invalido rechazado: '%1'").arg(QLatin1String(bad)));
    }

    // Punto calibrado: fraccion <-> nativo, con marcos en otra posicion y otro tamano.
    const QRect frameA(100, 50, 2000, 1000);
    const QPoint click(1880, 770);
    const QPointF spot = ActionRunner::nativeToSpot(frameA, click);
    check(ActionRunner::spotToNative(frameA, spot) == click, QStringLiteral("ida y vuelta del punto en el mismo marco"));
    const QRect frameB(-1920, 0, 1000, 500); // otro monitor, a la izquierda, mitad de tamano
    const QPoint moved = ActionRunner::spotToNative(frameB, spot);
    check(moved == QPoint(-1920 + 890, 360), QStringLiteral("el punto sigue a la ventana: %1,%2").arg(moved.x()).arg(moved.y()));
    check(ActionRunner::nativeToSpot(QRect(), click).x() < 0, QStringLiteral("marco vacio no da un punto valido"));

    // ---- La herramienta entera en un host de prueba (corrida automatizada: atajos contados).
    const Shortcut claimed = Shortcut::fromPortableString(QStringLiteral("Ctrl+Alt+K"));
    QList<ModuleDescriptor> descriptors;
    ModuleDescriptor claim;
    claim.id = QStringLiteral("claim");
    claim.title = QStringLiteral("Folder Switch");
    claim.create = [claimed](ModuleContext &context) -> std::unique_ptr<Module> {
        return std::make_unique<ClaimModule>(context, claimed);
    };
    descriptors << claim << nukeShortcutsDescriptor();
    MemorySettingsStore store;
    HostOptions options;
    options.automatedRun = true;
    ModuleHost host(descriptors, &store, options);

    host.setEnabled(QStringLiteral("claim"), true);
    host.setEnabled(kId, true);
    auto *module = static_cast<NukeShortcutsModule *>(host.module(kId));
    check(module != nullptr, QStringLiteral("se prende en el host"));
    if (!module) {
        return;
    }
    NukeShortcutsState *state = module->state();
    check(module->validateShortcut(ShortcutAction::AddKeyframe, claimed) == QLatin1String("Already used by Folder Switch."),
          QStringLiteral("grabador: una combinacion de otra herramienta -> \"Already used by Folder Switch.\""));
    check(module->validateShortcut(ShortcutAction::AddKeyframe, state->shortcut(ShortcutAction::FrameDopeSheet))
              == QLatin1String("Already used by Frame Dope Sheet."),
          QStringLiteral("grabador: la combinacion de la otra accion -> \"Already used by Frame Dope Sheet.\""));
    check(module->validateShortcut(ShortcutAction::AddKeyframe, Shortcut::fromPortableString(QStringLiteral("Ctrl+Alt+Q"))).isEmpty(),
          QStringLiteral("grabador: una combinacion libre sirve"));

    // Solo con Nuke al frente (D-18). El aviso real de ventana al frente no llega en la prueba: se
    // fija a mano.
    state->setNukeInFront(false);
    check(host.hotkeyHub()->registeredCount() == 0, QStringLiteral("sin Nuke al frente: 0 atajos registrados"));
    state->setNukeInFront(true);
    check(host.hotkeyHub()->registeredCount() == 2, QStringLiteral("con Nuke al frente: 2 atajos registrados"));
    state->setPaused(true);
    check(host.hotkeyHub()->registeredCount() == 0 && module->isPaused(), QStringLiteral("en pausa: 0 atajos registrados"));
    state->setPaused(false);
    check(host.hotkeyHub()->registeredCount() == 2, QStringLiteral("al volver de la pausa: 2 registrados"));

    // Cambiar el atajo a uno declarado por otra herramienta (por fuera del grabador): queda tomado
    // y la tarjeta dice por quien.
    state->setShortcut(ShortcutAction::AddKeyframe, claimed);
    check(state->registration(ShortcutAction::AddKeyframe) == NukeShortcutsState::Registration::Failed
              && state->conflictWith(ShortcutAction::AddKeyframe) == QLatin1String("Folder Switch")
              && host.hotkeyHub()->registeredCount() == 1,
          QStringLiteral("choque: un atajo de otra herramienta queda como tomado por Folder Switch"));
    check(module->status().tone == ModuleTone::Error, QStringLiteral("choque: la fila de la lista va en rojo"));
    state->setShortcut(ShortcutAction::AddKeyframe, Shortcut::defaultAddKeyframe());
    check(host.hotkeyHub()->registeredCount() == 2 && module->status().tone != ModuleTone::Error,
          QStringLiteral("choque: volver al atajo propio lo registra de nuevo"));
    check(store.value(QStringLiteral("nukeShortcuts/shortcuts/addKeyframe")).toString() == QLatin1String("Ctrl+Shift+D"),
          QStringLiteral("settings: el atajo se guarda en [nukeShortcuts]"));

    host.setEnabled(kId, false);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    check(host.module(kId) == nullptr && host.hotkeyHub()->registeredCount() == 0,
          QStringLiteral("apagada: sin objeto y 0 atajos"));
    host.setEnabled(QStringLiteral("claim"), false);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    // configuredShortcuts lee la seccion sin construir la herramienta (grabador de las demas).
    store.setValue(QStringLiteral("nukeShortcuts/shortcuts/frameDopeSheet"), QStringLiteral("Ctrl+Alt+F"));
    const QList<Shortcut> list = nukeShortcutsDescriptor().configuredShortcuts(host.reader(kId));
    check(list.size() == 2 && list.at(1) == Shortcut::fromPortableString(QStringLiteral("Ctrl+Alt+F")),
          QStringLiteral("configuredShortcuts: los atajos de la seccion, con la herramienta apagada"));
}

} // namespace

// ---------------------------------------------------------------- NukeShortcutsModule

NukeShortcutsModule::NukeShortcutsModule(ModuleContext &context)
    : Module(context)
{
    m_state = new NukeShortcutsState(context.captureMode() ? nullptr : &context, this);
    connect(m_state, &NukeShortcutsState::changed, this, [this]() {
        if (m_started) {
            updateRegistrations();
        }
        emit statusChanged();
    });
}

NukeShortcutsModule::~NukeShortcutsModule()
{
    stop();
}

bool NukeShortcutsModule::needsPermission() const
{
    // En la captura el estado "falta el permiso" se puede dibujar en cualquier plataforma.
    return SystemInput::needsAccessibilityPermission() || context().captureMode();
}

void NukeShortcutsModule::start()
{
    if (m_started) {
        return;
    }
    m_started = true;
    // Nuke al frente sale del servicio compartido del host: un solo hook con Folder Switch.
    m_watcher = new NukeWatcher(context().foreground(), this);
    m_runner = new ActionRunner(context().injector(), this);
    connect(context().hotkeys(), &ModuleHotkeys::activated, this, &NukeShortcutsModule::onHotkey);
    connect(m_watcher, &NukeWatcher::nukeInFrontChanged, m_state, &NukeShortcutsState::setNukeInFront);

    refreshAccessibility();
    if (SystemInput::needsAccessibilityPermission()) {
        m_accessibilityTimer = new QTimer(this);
        m_accessibilityTimer->setInterval(kAccessibilityPollMs);
        connect(m_accessibilityTimer, &QTimer::timeout, this, &NukeShortcutsModule::refreshAccessibility);
        m_accessibilityTimer->start();
    }
    m_updating = true;
    m_state->setNukeInFront(m_watcher->nukeInFront());
    m_updating = false;
    updateRegistrations();
    if (context().dryRunInput()) {
        qWarning() << "[NukeShortcuts] Inyector en solo loguear: las acciones no tocan el mouse ni el teclado";
    }
}

void NukeShortcutsModule::stop()
{
    if (!m_started) {
        return;
    }
    m_started = false;
    // Una calibracion en curso se corta y la ventana vuelve.
    if (m_calibrationDialog) {
        m_calibrationDialog->reject();
    }
    if (m_calibration) {
        delete m_calibration;
        m_calibration = nullptr;
        context().restoreWindow();
    }
    ModuleHotkeys *hotkeys = context().hotkeys();
    disconnect(hotkeys, nullptr, this, nullptr);
    hotkeys->unregisterAll();
    // Retira sus declaraciones: otra herramienta ya puede usar esas combinaciones.
    hotkeys->declare(kAddKeyframeId, Shortcut());
    hotkeys->declare(kFrameDopeSheetId, Shortcut());
    m_registeredAddKeyframe = Shortcut();
    m_registeredFrame = Shortcut();
    delete m_accessibilityTimer;
    m_accessibilityTimer = nullptr;
    delete m_runner;
    m_runner = nullptr;
    delete m_watcher;
    m_watcher = nullptr;
}

void NukeShortcutsModule::declareShortcuts()
{
    ModuleHotkeys *hotkeys = context().hotkeys();
    for (const ShortcutAction action : {ShortcutAction::AddKeyframe, ShortcutAction::FrameDopeSheet}) {
        const QString other = hotkeys->declare(hotkeyId(action), m_state->shortcut(action));
        if (!other.isEmpty()) {
            qInfo() << "[NukeShortcuts]" << NukeShortcutsState::actionTitle(action) << "ya es de" << other;
        }
        if (!other.isEmpty()) {
            m_state->setRegistration(action, NukeShortcutsState::Registration::Failed, other);
        } else if (!m_state->conflictWith(action).isEmpty()) {
            // La otra herramienta la solto (o cambio el atajo): se vuelve a intentar desde cero.
            m_state->setRegistration(action, NukeShortcutsState::Registration::Idle);
        }
    }
}

void NukeShortcutsModule::updateRegistrations()
{
    // setRegistration() avisa changed(), que vuelve a llamar aca: sin la guarda, un atajo rechazado
    // se reintentaria varias veces en la misma pasada.
    if (m_updating || !m_started) {
        return;
    }
    m_updating = true;
    declareShortcuts();
    ModuleHotkeys *hotkeys = context().hotkeys();
    const bool wanted = !m_state->paused() && m_state->nukeInFront() && m_state->accessibilityGranted();
    for (const ShortcutAction action : {ShortcutAction::AddKeyframe, ShortcutAction::FrameDopeSheet}) {
        const int id = hotkeyId(action);
        Shortcut &registered = action == ShortcutAction::AddKeyframe ? m_registeredAddKeyframe : m_registeredFrame;
        const Shortcut current = m_state->shortcut(action);
        const QString conflict = m_state->conflictWith(action);
        if (!conflict.isEmpty()) {
            // Tomado por otra herramienta que declaro primero: no se intenta.
            hotkeys->unregisterHotkey(id);
            registered = Shortcut();
            continue;
        }
        if (!wanted) {
            if (hotkeys->isRegistered(id)) {
                hotkeys->unregisterHotkey(id);
                registered = Shortcut();
                m_state->setRegistration(action, NukeShortcutsState::Registration::Idle);
            } else if (m_state->registration(action) == NukeShortcutsState::Registration::Registered) {
                m_state->setRegistration(action, NukeShortcutsState::Registration::Idle);
            }
            // Un Failed se conserva: el aviso sigue en la tarjeta hasta el proximo intento.
            continue;
        }
        if (hotkeys->isRegistered(id) && registered == current) {
            continue;
        }
        const bool ok = hotkeys->registerHotkey(id, current);
        registered = ok ? current : Shortcut();
        m_state->setRegistration(action, ok ? NukeShortcutsState::Registration::Registered
                                            : NukeShortcutsState::Registration::Failed);
    }
    m_updating = false;
}

QString NukeShortcutsModule::validateShortcut(ShortcutAction action, const Shortcut &shortcut) const
{
    const ShortcutAction other =
        action == ShortcutAction::AddKeyframe ? ShortcutAction::FrameDopeSheet : ShortcutAction::AddKeyframe;
    if (m_state->shortcut(other) == shortcut) {
        return I18n::tr("Already used by %1.").arg(NukeShortcutsState::actionTitle(other));
    }
    // Otra herramienta (prendida o apagada) antes que otra app: RegisterHotKey tambien rechaza los
    // duplicados del mismo proceso y el mensaje diria "another app".
    const QString tool = context().hotkeys()->declaredByOtherModule(shortcut);
    if (!tool.isEmpty()) {
        return I18n::tr("Already used by %1.").arg(tool);
    }
    if (!context().hotkeys()->probe(shortcut)) {
        return I18n::tr("%1 is taken by another app.").arg(shortcut.displayText());
    }
    return QString();
}

void NukeShortcutsModule::onHotkey(int localId)
{
    if (!m_started) {
        return;
    }
    // El atajo solo esta registrado con Nuke al frente, pero el aviso de "cambio de ventana" llega
    // encolado: si el usuario lo aprieta justo al salir de Nuke, puede llegar aca estando en otra app.
    // Se pregunta AHORA; si no es Nuke, se suelta el atajo (el estado lo hace al pasar a false) y la
    // combinacion se le devuelve a la app del frente, como si esta app no existiera.
    if (!m_watcher->isNukeInFrontNow()) {
        const Shortcut shortcut =
            m_state->shortcut(localId == kAddKeyframeId ? ShortcutAction::AddKeyframe : ShortcutAction::FrameDopeSheet);
        qInfo() << "[NukeShortcuts] Atajo" << shortcut.toPortableString() << "fuera de Nuke: se devuelve";
        m_state->setNukeInFront(false);
        context().hotkeys()->unregisterAll();
        if (!context().dryRunInput()) {
            context().hotkeys()->passThrough(shortcut);
        }
        return;
    }
    if (localId == kAddKeyframeId) {
        m_runner->runAddKeyframe();
        return;
    }
    if (localId != kFrameDopeSheetId) {
        return;
    }
    if (!m_state->hasDopeSheetSpot()) {
        qInfo() << "[NukeShortcuts] Frame Dope Sheet sin calibrar";
        context().notify(I18n::tr("Frame Dope Sheet"),
                         I18n::tr("Calibrate the Dope Sheet first: one click, from the tray menu."),
                         ModuleContext::NoticeIcon::Info, 6000);
        return;
    }
    m_runner->runFrameDopeSheet(m_watcher->frontNukeFrame(), m_state->dopeSheetSpot());
}

void NukeShortcutsModule::startCalibration()
{
    // Nunca en una corrida automatizada: el calibrador espera un click real sobre Nuke.
    if (m_calibration || m_calibrationDialog || !m_started || context().automatedRun()) {
        return;
    }
    QWidget *window = context().window();
    CalibrationDialog dialog(window && window->isVisible() ? window : nullptr);
    m_calibrationDialog = &dialog;
    dialog.centerOn(window);
    const int result = dialog.exec();
    m_calibrationDialog = nullptr;
    // La herramienta pudo apagarse con el dialogo abierto (stop() lo rechaza).
    if (result != QDialog::Accepted || !m_started) {
        return;
    }
    // La ventana puede tapar a Nuke: se oculta mientras se espera el click.
    context().hideWindowTemporarily();
    m_calibration = new CalibrationSession(m_watcher, context().injector(), this);
    connect(m_calibration, &CalibrationSession::calibrated, this, [this](const QPointF &spot) {
        m_state->setDopeSheetSpot(spot);
        finishCalibration();
        context().restoreWindow();
        context().showPanel();
    });
    connect(m_calibration, &CalibrationSession::cancelled, this, [this]() {
        finishCalibration();
        context().restoreWindow();
    });
    m_calibration->start();
}

void NukeShortcutsModule::finishCalibration()
{
    if (m_calibration) {
        m_calibration->deleteLater();
        m_calibration = nullptr;
    }
}

void NukeShortcutsModule::refreshAccessibility()
{
    m_state->setAccessibilityGranted(SystemInput::accessibilityTrusted(false));
}

void NukeShortcutsModule::openAccessibilitySettings()
{
    if (context().automatedRun()) {
        qInfo() << "[NukeShortcuts] (automatizada) se abriria Ajustes > Accesibilidad";
        return;
    }
    // El cartel del sistema agrega la app a la lista (apagada); el panel de Ajustes es donde se
    // prende.
    SystemInput::accessibilityTrusted(true);
    QDesktopServices::openUrl(QUrl(QStringLiteral("x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility")));
}

ModuleStatus NukeShortcutsModule::status() const
{
    if (needsPermission() && !m_state->accessibilityGranted()) {
        return {ModuleTone::Attention, I18n::tr("Accessibility needed")};
    }
    if (m_state->paused()) {
        return {ModuleTone::Paused, I18n::tr("Paused")};
    }
    if (m_state->registration(ShortcutAction::AddKeyframe) == NukeShortcutsState::Registration::Failed
        || m_state->registration(ShortcutAction::FrameDopeSheet) == NukeShortcutsState::Registration::Failed) {
        return {ModuleTone::Error, I18n::tr("Shortcut taken")};
    }
    if (!m_state->hasDopeSheetSpot()) {
        return {ModuleTone::Attention, I18n::tr("Not calibrated")};
    }
    return {ModuleTone::Active, m_state->nukeInFront() ? I18n::tr("On · Nuke in front") : I18n::tr("On")};
}

bool NukeShortcutsModule::isPaused() const
{
    return m_state->paused();
}

QWidget *NukeShortcutsModule::createPanel(QWidget *parent)
{
    const bool interactive = !context().captureMode();
    auto *panel = new NukeShortcutsPanel(m_state, interactive, needsPermission(), parent);
    if (interactive) {
        panel->setValidator([this](ShortcutAction action, const Shortcut &shortcut) { return validateShortcut(action, shortcut); });
        connect(panel, &NukeShortcutsPanel::calibrateRequested, this, &NukeShortcutsModule::startCalibration);
        connect(panel, &NukeShortcutsPanel::accessibilityRequested, this, &NukeShortcutsModule::openAccessibilitySettings);
    } else if (m_captureState == QLatin1String("recording")) {
        panel->shortcutRow(ShortcutAction::AddKeyframe)->showRecordingFixture(QStringLiteral("Ctrl + Shift + ..."));
    } else if (m_captureState == QLatin1String("rejected")) {
        panel->shortcutRow(ShortcutAction::AddKeyframe)
            ->setError(I18n::tr("%1 Kept %2.")
                            .arg(I18n::tr("%1 is taken by another app.").arg(QStringLiteral("Ctrl+Alt+K")),
                                 QStringLiteral("Ctrl+Shift+D")));
    }
    m_panel = panel;
    return panel;
}

void NukeShortcutsModule::fillTrayMenu(QMenu *menu)
{
    QAction *toggle = menu->addAction(m_state->paused() ? I18n::tr("Resume shortcuts") : I18n::tr("Pause shortcuts"));
    connect(toggle, &QAction::triggered, this, [this]() { m_state->setPaused(!m_state->paused()); });
    QAction *calibrate = menu->addAction(I18n::tr("Calibrate Dope Sheet..."));
    connect(calibrate, &QAction::triggered, this, &NukeShortcutsModule::startCalibration);
}

QStringList NukeShortcutsModule::captureStates() const
{
    // Canvas, secciones 2, 3 y 5: la tarjeta de estado en todas sus variantes, los atajos grabando y
    // rechazados, sin calibrar, y lo que se abre afuera (dialogo y burbujas del calibrador).
    return {QStringLiteral("on"),
            QStringLiteral("outside-nuke"),
            QStringLiteral("paused"),
            QStringLiteral("taken"),
            QStringLiteral("taken-both"),
            QStringLiteral("conflict"),
            QStringLiteral("permission"),
            QStringLiteral("not-calibrated"),
            QStringLiteral("recording"),
            QStringLiteral("rejected"),
            QStringLiteral("calibrate-dialog"),
            QStringLiteral("calibrate-bubble"),
            QStringLiteral("calibrate-bubble-move"),
            QStringLiteral("calibrate-bubble-outside")};
}

bool NukeShortcutsModule::applyCaptureState(const QString &state)
{
    if (!context().captureMode() || !captureStates().contains(state) || m_panel) {
        return false;
    }
    // Estado limpio (sin contexto: no toca settings.ini) con los datos del canvas.
    m_state->deleteLater();
    m_state = new NukeShortcutsState(nullptr, this);
    connect(m_state, &NukeShortcutsState::changed, this, &Module::statusChanged);
    m_captureState = state;
    m_state->setNukeInFront(state != QLatin1String("outside-nuke"));
    if (state != QLatin1String("not-calibrated")) {
        m_state->setDopeSheetSpot(QPointF(0.89, 0.72));
    }
    const bool idle = state == QLatin1String("outside-nuke");
    for (const ShortcutAction action : {ShortcutAction::AddKeyframe, ShortcutAction::FrameDopeSheet}) {
        m_state->setRegistration(action, idle ? NukeShortcutsState::Registration::Idle : NukeShortcutsState::Registration::Registered);
    }
    if (state == QLatin1String("paused")) {
        m_state->setPaused(true);
    } else if (state == QLatin1String("taken")) {
        m_state->setRegistration(ShortcutAction::FrameDopeSheet, NukeShortcutsState::Registration::Failed);
    } else if (state == QLatin1String("taken-both")) {
        m_state->setRegistration(ShortcutAction::AddKeyframe, NukeShortcutsState::Registration::Failed);
        m_state->setRegistration(ShortcutAction::FrameDopeSheet, NukeShortcutsState::Registration::Failed);
    } else if (state == QLatin1String("conflict")) {
        m_state->setRegistration(ShortcutAction::FrameDopeSheet, NukeShortcutsState::Registration::Failed,
                                 QStringLiteral("Folder Switch"));
    } else if (state == QLatin1String("permission")) {
        m_state->setAccessibilityGranted(false);
    }
    return true;
}

QWidget *NukeShortcutsModule::createCaptureWidget(const QString &state, QWidget *parent)
{
    if (state == QLatin1String("calibrate-dialog")) {
        auto *dialog = new CalibrationDialog(parent);
        dialog->setWindowFlags(Qt::Widget);
        dialog->fitHeight();
        dialog->setFixedSize(dialog->size());
        return dialog;
    }
    if (state.startsWith(QLatin1String("calibrate-bubble"))) {
        auto *bubble = new CalibrationBubble(parent);
        bubble->setWindowFlags(Qt::Widget);
        if (state == QLatin1String("calibrate-bubble")) {
            bubble->showOverNuke(QPointF(0.89, 0.72));
        } else {
            bubble->showOutside(state == QLatin1String("calibrate-bubble-outside"));
        }
        return bubble;
    }
    return nullptr;
}

ModuleDescriptor nukeShortcutsDescriptor()
{
    ModuleDescriptor d;
    d.id = kId;
    d.title = QStringLiteral("Nuke Shortcuts");
    d.description = I18n::tr(
        "Two shortcuts for Nuke: set a key on the knob under the pointer, and frame every key in the Dope Sheet.");
    d.offBullets = {I18n::tr("Registers two shortcuts, only while Nuke is in front"),
                    I18n::tr("Clicks and types in Nuke for you"),
                    I18n::tr("Needs one calibration click on the Dope Sheet")};
    d.platforms = PlatformWindows | PlatformMac;
    d.paintIcon = &paintNukeIcon;
    d.create = [](ModuleContext &context) -> std::unique_ptr<Module> { return std::make_unique<NukeShortcutsModule>(context); };
    d.configuredShortcuts = [](const SettingsReader &value) {
        return QList<Shortcut>{configured(value, ShortcutAction::AddKeyframe), configured(value, ShortcutAction::FrameDopeSheet)};
    };
    d.selfTest = &selfTest;
    d.simulateAction = &simulateAction;
    return d;
}

HelpSection nukeShortcutsHelp(const SettingsReader &value)
{
    HelpSection section;
    section.title = QStringLiteral("Nuke Shortcuts");
    section.steps = {
        I18n::tr("Put the pointer over a knob in Nuke and press %1 to set a key.")
            .arg(HelpSection::strong(configured(value, ShortcutAction::AddKeyframe).displayText())),
        I18n::tr("Calibrate the %1 once: one click on an empty spot.").arg(HelpSection::strong(QStringLiteral("Dope Sheet"))),
        I18n::tr("Press %1 to select every key in the Dope Sheet and frame them.")
            .arg(HelpSection::strong(configured(value, ShortcutAction::FrameDopeSheet).displayText())),
    };
    section.note = I18n::tr("The shortcuts only work while Nuke is in front; other apps keep these keys.");
    return section;
}
