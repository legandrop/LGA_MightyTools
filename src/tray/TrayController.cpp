#include "tray/TrayController.h"

#include "actions/ActionRunner.h"
#include "core/AppSettings.h"
#include "core/DiskMonitor.h"
#include "platform/AutoStart.h"
#include "platform/HotkeyService.h"
#include "platform/InputInjector.h"
#include "platform/LocalDrives.h"
#include "platform/NukeWatcher.h"
#include "platform/SystemInput.h"
#include "ui/CalibrationDialog.h"
#include "ui/CalibrationSession.h"
#include "ui/HelpDialog.h"
#include "ui/MainWindow.h"

#ifdef Q_OS_WIN
#include "updates/UpdateService.h"
#endif

#include <QAction>
#include <QApplication>
#include <QDebug>
#include <QDesktopServices>
#include <QIcon>
#include <QMenu>
#include <QScopeGuard>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QUrl>

namespace {

// Ids de HotkeyService, uno por accion.
constexpr int kAddKeyframeId = 1;
constexpr int kFrameDopeSheetId = 2;

// En macOS el permiso de Accesibilidad no avisa cuando cambia: se consulta cada tanto.
constexpr int kAccessibilityPollMs = 2000;

// Primer chequeo de discos que puede avisar: un rato despues de arrancar, para no sumar una
// notificacion al inicio de la sesion.
constexpr int kFirstDiskCheckMs = 20000;
constexpr int kDiskNotificationMs = 10000;

int hotkeyId(ShortcutAction action)
{
    return action == ShortcutAction::AddKeyframe ? kAddKeyframeId : kFrameDopeSheetId;
}

} // namespace

TrayController::TrayController(bool dryRunInput, QObject *parent)
    : QObject(parent)
    , m_injector(std::make_unique<InputInjector>(dryRunInput))
{
    m_state = new AppState(AppState::Persistence::Settings, this);
    m_window = new MainWindow(m_state, MainWindow::Mode::Normal);
    m_window->setShortcutValidator(
        [this](ShortcutAction action, const Shortcut &shortcut) { return validateShortcut(action, shortcut); });

    m_menu = new QMenu();
    m_menuActions = buildTrayMenu(m_menu);
    connect(m_menuActions.toggle, &QAction::triggered, this, [this]() { m_state->setEnabled(!m_state->enabled()); });
    connect(m_menuActions.settings, &QAction::triggered, this, &TrayController::showSettings);
    connect(m_menuActions.calibrate, &QAction::triggered, this, &TrayController::startCalibration);
    connect(m_menuActions.updates, &QAction::triggered, this, &TrayController::checkForUpdatesManual);
    connect(m_menuActions.quit, &QAction::triggered, this, &TrayController::quit);
    connect(m_window, &MainWindow::helpRequested, this, &TrayController::showHelp);
    connect(m_window, &MainWindow::calibrateRequested, this, &TrayController::startCalibration);
    connect(m_window, &MainWindow::checkUpdatesRequested, this, &TrayController::checkForUpdatesManual);
    connect(m_window, &MainWindow::accessibilityRequested, this, &TrayController::openAccessibilitySettings);

    DiskMonitor::Sources diskSources;
    diskSources.listAll = &LocalDrives::list;
    diskSources.query = &LocalDrives::query;
    m_diskMonitor = new DiskMonitor(m_state, diskSources, this);
    connect(m_diskMonitor, &DiskMonitor::lowSpace, this, &TrayController::notifyLowSpace);
    connect(m_window, &MainWindow::diskReadingsRequested, m_diskMonitor, [this]() { m_diskMonitor->checkNow(false); });
    connect(m_window, &MainWindow::driveListRequested, m_diskMonitor, &DiskMonitor::refreshAll);

    m_tray = new QSystemTrayIcon(this);
    m_tray->setContextMenu(m_menu);
    connect(m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) { onTrayActivated(static_cast<int>(reason)); });
    // Las notificaciones de la app llevan a Settings: la de disco bajo muestra ahi el disco, y la
    // de "calibrate first", el boton para calibrar.
    connect(m_tray, &QSystemTrayIcon::messageClicked, this, &TrayController::showSettings);

    m_watcher = new NukeWatcher(this);
    m_hotkeys = new HotkeyService(this);
    m_runner = new ActionRunner(m_injector.get(), this);
    connect(m_hotkeys, &HotkeyService::activated, this, &TrayController::onHotkey);
    connect(m_watcher, &NukeWatcher::nukeInFrontChanged, m_state, &AppState::setNukeInFront);

    m_state->setNukeInFront(m_watcher->nukeInFront());
    refreshAccessibility();
    if (SystemInput::needsAccessibilityPermission()) {
        m_accessibilityTimer = new QTimer(this);
        m_accessibilityTimer->setInterval(kAccessibilityPollMs);
        connect(m_accessibilityTimer, &QTimer::timeout, this, &TrayController::refreshAccessibility);
        m_accessibilityTimer->start();
    }

    connect(m_state, &AppState::changed, this, &TrayController::refreshFromState);
    refreshFromState();
    m_tray->show();
    m_diskMonitor->start(kFirstDiskCheckMs);
    if (m_injector->dryRun()) {
        qWarning() << "[TrayController] dryRunInput activo: las acciones solo loguean, no tocan el mouse ni el teclado";
    }

#ifdef Q_OS_WIN
    // parentWindow es m_window (normalmente oculto): sus dialogos de resultado igual se centran en
    // pantalla al no tener un padre visible.
    m_updateService = new UpdateService(m_window, this);
    if (m_state->checkUpdatesAtStartup()) {
        m_updateService->scheduleAutomaticCheck();
    } else {
        qInfo() << "[TrayController] Chequeo de updates al arrancar: desactivado por el usuario";
    }
#endif

    runFirstLaunchSetupIfNeeded();
}

TrayController::~TrayController()
{
    m_hotkeys->unregisterAll();
    delete m_menu;
    delete m_window;
}

void TrayController::runFirstLaunchSetupIfNeeded()
{
    // Patron de FrameRev y FolderSwitch: el "se abre con el sistema por defecto" se activa en el
    // primer arranque de una copia instalada, y no lo escribe el instalador. La marca se guarda SOLO
    // cuando corre una copia instalada: un arranque desde build/ no consume el primer arranque de la
    // instalacion futura.
    const auto settings = AppSettings::open();
    if (settings->value(QStringLiteral("firstRunCompleted"), false).toBool()) {
        return;
    }
    const AutoStart::Availability availability = AutoStart::availability();
    if (!availability.available) {
        qInfo() << "[TrayController] Primer arranque: no se activa el inicio automatico (" << availability.text << ")";
        return;
    }
    settings->setValue(QStringLiteral("firstRunCompleted"), true);
    const bool ok = AutoStart::setEnabled(true);
    qInfo() << "[TrayController] Primer arranque: inicio con la sesion activado ok:" << ok;
    showSettings();
}

void TrayController::applyTrayIcon()
{
    m_tray->setIcon(trayIcon(!m_state->enabled()));
}

void TrayController::refreshFromState()
{
    // Menu, icono y tooltip leen el mismo AppState que la tarjeta de estado de Settings.
    const bool enabled = m_state->enabled();
    refreshTrayMenu(m_menuActions, enabled);
    applyTrayIcon();
    refreshDiskWarnings();
    updateRegistrations();
}

void TrayController::refreshDiskWarnings()
{
    const QStringList lines = diskWarningLines(*m_state);
    QStringList tooltip{m_state->enabled() ? QStringLiteral("LGA Mighty Tools")
                                           : QStringLiteral("LGA Mighty Tools — paused")};
    for (const DiskWatch &watch : m_state->lowWatches()) {
        DriveInfo drive;
        m_state->driveReading(watch.root, &drive);
        tooltip.append(QStringLiteral("%1 %2 free").arg(drive.label, DiskSpace::formatBytes(drive.freeBytes)));
    }
    m_tray->setToolTip(tooltip.join(QLatin1Char('\n')));
    // Solo si cambio: el menu podria estar abierto, y cada chequeo avisa changed().
    if (lines == m_diskWarningLines) {
        return;
    }
    m_diskWarningLines = lines;
    refreshTrayDiskWarnings(m_menu, m_menuActions, lines);
    for (QAction *action : m_menuActions.diskWarnings) {
        connect(action, &QAction::triggered, this, &TrayController::showSettings);
    }
}

void TrayController::notifyLowSpace(const DriveInfo &drive, const DiskWatch &watch)
{
    m_tray->showMessage(QStringLiteral("%1 is running low").arg(drive.label),
                        QStringLiteral("%1 free of %2. You asked to be warned under %3.")
                            .arg(DiskSpace::formatBytes(drive.freeBytes), DiskSpace::formatBytes(drive.totalBytes),
                                 DiskSpace::thresholdText(watch)),
                        QSystemTrayIcon::Warning, kDiskNotificationMs);
}

void TrayController::updateRegistrations()
{
    // setRegistration() avisa changed(), que vuelve a llamar aca: sin la guarda, un atajo rechazado
    // se reintentaria varias veces en la misma pasada.
    static bool running = false;
    if (running) {
        return;
    }
    running = true;
    const auto done = qScopeGuard([]() { running = false; });

    const bool wanted = m_state->enabled() && m_state->nukeInFront() && m_state->accessibilityGranted();
    for (const ShortcutAction action : {ShortcutAction::AddKeyframe, ShortcutAction::FrameDopeSheet}) {
        const int id = hotkeyId(action);
        Shortcut &registered = action == ShortcutAction::AddKeyframe ? m_registeredAddKeyframe : m_registeredFrame;
        const Shortcut current = m_state->shortcut(action);
        if (!wanted) {
            if (m_hotkeys->isRegistered(id)) {
                m_hotkeys->unregisterHotkey(id);
                registered = Shortcut();
                m_state->setRegistration(action, AppState::Registration::Idle);
            }
            // Un Failed se conserva: el aviso sigue en la tarjeta hasta el proximo intento.
            continue;
        }
        if (m_hotkeys->isRegistered(id) && registered == current) {
            continue;
        }
        const bool ok = m_hotkeys->registerHotkey(id, current);
        registered = ok ? current : Shortcut();
        m_state->setRegistration(action, ok ? AppState::Registration::Registered : AppState::Registration::Failed);
    }
}

QString TrayController::validateShortcut(ShortcutAction action, const Shortcut &shortcut) const
{
    const ShortcutAction other =
        action == ShortcutAction::AddKeyframe ? ShortcutAction::FrameDopeSheet : ShortcutAction::AddKeyframe;
    if (m_state->shortcut(other) == shortcut) {
        return QStringLiteral("Already used by %1.").arg(AppState::actionTitle(other));
    }
    if (!m_hotkeys->probe(shortcut)) {
        return QStringLiteral("%1 is taken by another app.").arg(shortcut.displayText());
    }
    return QString();
}

void TrayController::onHotkey(int id)
{
    // El atajo solo esta registrado con Nuke al frente, pero el aviso de "cambio de ventana" llega
    // encolado: si el usuario lo aprieta justo al salir de Nuke, puede llegar aca estando en otra app.
    // Se pregunta AHORA; si no es Nuke, se suelta el atajo (el estado lo hace al pasar a false) y la
    // combinacion se le devuelve a la app del frente, como si esta app no existiera.
    if (!m_watcher->isNukeInFrontNow()) {
        const Shortcut shortcut =
            m_state->shortcut(id == kAddKeyframeId ? ShortcutAction::AddKeyframe : ShortcutAction::FrameDopeSheet);
        qInfo() << "[TrayController] Atajo" << shortcut.toPortableString() << "fuera de Nuke: se devuelve";
        m_state->setNukeInFront(false);
        m_hotkeys->unregisterAll();
        if (!m_injector->dryRun()) {
            m_hotkeys->passThrough(shortcut);
        }
        return;
    }
    if (id == kAddKeyframeId) {
        m_runner->runAddKeyframe();
        return;
    }
    if (id != kFrameDopeSheetId) {
        return;
    }
    if (!m_state->hasDopeSheetSpot()) {
        qInfo() << "[TrayController] Frame Dope Sheet sin calibrar";
        m_tray->showMessage(QStringLiteral("Frame Dope Sheet"),
                            QStringLiteral("Calibrate the Dope Sheet first: one click, from the tray menu."),
                            QSystemTrayIcon::Information, 6000);
        return;
    }
    m_runner->runFrameDopeSheet(m_watcher->frontNukeFrame(), m_state->dopeSheetSpot());
}

void TrayController::startCalibration()
{
    if (m_calibration) {
        return; // ya hay una en curso
    }
    CalibrationDialog dialog(m_window->isVisible() ? m_window : nullptr);
    dialog.centerOn(m_window);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    // La ventana de Settings puede tapar a Nuke: se oculta mientras se espera el click.
    m_windowWasVisibleBeforeCalibration = m_window->isVisible();
    m_window->hide();
    m_calibration = new CalibrationSession(m_watcher, m_injector.get(), this);
    const auto finish = [this]() {
        m_calibration->deleteLater();
        m_calibration = nullptr;
    };
    connect(m_calibration, &CalibrationSession::calibrated, this, [this, finish](const QPointF &spot) {
        m_state->setDopeSheetSpot(spot);
        finish();
        showSettings();
    });
    connect(m_calibration, &CalibrationSession::cancelled, this, [this, finish]() {
        finish();
        if (m_windowWasVisibleBeforeCalibration) {
            showSettings();
        }
    });
    m_calibration->start();
}

void TrayController::refreshAccessibility()
{
    m_state->setAccessibilityGranted(SystemInput::accessibilityTrusted(false));
}

void TrayController::openAccessibilitySettings()
{
    // El cartel del sistema agrega la app a la lista (apagada); el panel de Ajustes es donde se
    // prende.
    SystemInput::accessibilityTrusted(true);
    QDesktopServices::openUrl(
        QUrl(QStringLiteral("x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility")));
}

void TrayController::showHelp()
{
    HelpDialog dialog(m_state->shortcut(ShortcutAction::AddKeyframe), m_state->shortcut(ShortcutAction::FrameDopeSheet),
                      m_window);
    dialog.execOver(m_window);
}

void TrayController::showSettings()
{
    m_window->show();
    m_window->raise();
    m_window->activateWindow();
}

void TrayController::onTrayActivated(int reason)
{
    if (reason == QSystemTrayIcon::DoubleClick || reason == QSystemTrayIcon::Trigger) {
#ifdef Q_OS_MACOS
        // En la barra de menu de macOS el click abre el menu; no hay doble click.
        return;
#else
        showSettings();
#endif
    }
}

void TrayController::quit()
{
    qApp->quit();
}

void TrayController::checkForUpdatesManual()
{
#ifdef Q_OS_WIN
    if (m_updateService) {
        m_updateService->checkForUpdates(true);
    }
#endif
}
