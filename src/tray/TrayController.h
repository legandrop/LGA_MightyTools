#ifndef MIGHTYTOOLS_TRAYCONTROLLER_H
#define MIGHTYTOOLS_TRAYCONTROLLER_H

#include "core/AppState.h"
#include "tray/TrayMenu.h"

#include <QObject>
#include <QString>

#include <memory>

class ActionRunner;
class CalibrationSession;
class DiskMonitor;
class HotkeyService;
class InputInjector;
class MainWindow;
class NukeWatcher;
class QMenu;
class QSystemTrayIcon;
class QTimer;
class UpdateService;

// El corazon de la app. Arma el icono de la bandeja (o de la barra de menu), la ventana de Settings
// y conecta:
//  - NukeWatcher -> AppState::nukeInFront, y con eso registra los atajos SOLO mientras Nuke esta al
//    frente (un atajo global registrado se come la combinacion en todas las apps).
//  - HotkeyService -> ActionRunner: cada atajo dispara su secuencia de clicks y teclas.
//  - El calibrador (dialogo + CalibrationSession) -> AppState::dopeSheetSpot.
//  - En macOS, el permiso de Accesibilidad -> AppState::accessibilityGranted.
//  - DiskMonitor -> la notificacion de disco bajo y sus lineas en el menu.
class TrayController : public QObject
{
    Q_OBJECT

public:
    // dryRunInput = true: las acciones solo loguean sus pasos, no mueven el mouse ni aprietan teclas.
    TrayController(bool dryRunInput, QObject *parent = nullptr);
    ~TrayController() override;

public slots:
    void showSettings();
    void startCalibration();

private slots:
    void onTrayActivated(int reason);
    void onHotkey(int id);
    void quit();
    void checkForUpdatesManual();

private:
    void applyTrayIcon();
    void refreshFromState();
    // Registra o suelta los atajos segun AppState: activos, Nuke al frente y (mac) con permiso.
    void updateRegistrations();
    QString validateShortcut(ShortcutAction action, const Shortcut &shortcut) const;
    void refreshAccessibility();
    void openAccessibilitySettings();
    void showHelp();
    void refreshDiskWarnings();
    void notifyLowSpace(const DriveInfo &drive, const DiskWatch &watch);
    // Primer arranque de una copia INSTALADA: activa el inicio con la sesion una sola vez y abre
    // Settings para que se vea. Desde build/ o deploy/ no hace nada.
    void runFirstLaunchSetupIfNeeded();

    AppState *m_state = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_menu = nullptr;
    TrayMenuActions m_menuActions;
    MainWindow *m_window = nullptr;
    NukeWatcher *m_watcher = nullptr;
    HotkeyService *m_hotkeys = nullptr;
    std::unique_ptr<InputInjector> m_injector;
    ActionRunner *m_runner = nullptr;
    CalibrationSession *m_calibration = nullptr;
    bool m_windowWasVisibleBeforeCalibration = false;
    QTimer *m_accessibilityTimer = nullptr;
    UpdateService *m_updateService = nullptr;
    DiskMonitor *m_diskMonitor = nullptr;
    QStringList m_diskWarningLines;

    // Lo que quedo registrado para cada accion, para re-registrar si el usuario cambia el atajo.
    Shortcut m_registeredAddKeyframe;
    Shortcut m_registeredFrame;
};

#endif // MIGHTYTOOLS_TRAYCONTROLLER_H
