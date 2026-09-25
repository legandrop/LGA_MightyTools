#ifndef MIGHTYTOOLS_NUKESHORTCUTSMODULE_H
#define MIGHTYTOOLS_NUKESHORTCUTSMODULE_H

#include "app/Module.h"
#include "modules/nukeshortcuts/NukeShortcutsState.h"
#include "ui/HelpSection.h"

#include <QPointer>

class ActionRunner;
class CalibrationDialog;
class CalibrationSession;
class NukeShortcutsPanel;
class NukeWatcher;
class QTimer;

// Nuke Shortcuts (id estable "nukeShortcuts"): Add keyframe y Frame Dope Sheet con atajos globales,
// solo con Nuke al frente (D-18), y el calibrador del Dope Sheet. Portado de Nuke Shortcuts v2.06:
// lo que era del TrayController (registrar segun Nuke al frente, el runner, el calibrador, el
// permiso de Accesibilidad) vive aca.
//
// Todo lo que consume nace en start() y muere en stop(): el NukeWatcher (hook de ventana al frente;
// se muda a un servicio compartido con Folder Switch), los atajos, el ActionRunner, el sondeo del
// permiso en mac y una calibracion en curso.
class NukeShortcutsModule : public Module
{
    Q_OBJECT

public:
    explicit NukeShortcutsModule(ModuleContext &context);
    ~NukeShortcutsModule() override;

    void start() override;
    void stop() override;
    ModuleStatus status() const override;
    QWidget *createPanel(QWidget *parent) override;
    void fillTrayMenu(QMenu *menu) override;
    bool isPaused() const override;

    QStringList captureStates() const override;
    bool applyCaptureState(const QString &state) override;
    QWidget *createCaptureWidget(const QString &state, QWidget *parent) override;

    NukeShortcutsState *state() const { return m_state; }
    // El motivo por el que el grabador rechaza `shortcut` para `action` (vacio = sirve).
    QString validateShortcut(ShortcutAction action, const Shortcut &shortcut) const;

    // Ids locales de ModuleHotkeys, uno por accion.
    static constexpr int kAddKeyframeId = 1;
    static constexpr int kFrameDopeSheetId = 2;

private:
    void onHotkey(int localId);
    void declareShortcuts();
    // Registra o suelta los atajos segun el estado: activos, Nuke al frente y (mac) con permiso.
    void updateRegistrations();
    void refreshAccessibility();
    void openAccessibilitySettings();
    void startCalibration();
    void finishCalibration();
    bool needsPermission() const;

    NukeShortcutsState *m_state = nullptr;
    bool m_started = false;
    NukeWatcher *m_watcher = nullptr;
    ActionRunner *m_runner = nullptr;
    QTimer *m_accessibilityTimer = nullptr;
    CalibrationSession *m_calibration = nullptr;
    QPointer<CalibrationDialog> m_calibrationDialog;
    QPointer<NukeShortcutsPanel> m_panel;
    bool m_updating = false;
    // Lo que quedo registrado para cada accion, para re-registrar si el usuario cambia el atajo.
    Shortcut m_registeredAddKeyframe;
    Shortcut m_registeredFrame;
    // Estado de captura que no es del NukeShortcutsState (grabando, rechazado).
    QString m_captureState;
};

ModuleDescriptor nukeShortcutsDescriptor();
HelpSection nukeShortcutsHelp(const SettingsReader &value);

#endif // MIGHTYTOOLS_NUKESHORTCUTSMODULE_H
