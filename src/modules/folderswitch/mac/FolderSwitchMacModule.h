#ifndef MIGHTYTOOLS_FOLDERSWITCH_MAC_MODULE_H
#define MIGHTYTOOLS_FOLDERSWITCH_MAC_MODULE_H

#include "app/Module.h"
#include "core/Shortcut.h"
#include "modules/folderswitch/FolderSwitchPanel.h"
#include "modules/folderswitch/FolderSwitchState.h"
#include "modules/folderswitch/mac/MacFileDialogs.h"
#include "ui/HelpSection.h"

#include <QPointer>
#include <QString>

class QTimer;
class RecentFoldersPopup;

// Folder Switch en macOS (D-40). Hace lo mismo que el de Windows (FolderSwitchModule.cpp, que no se
// toca): cambio automatico al volver del Finder a un dialogo, atajo manual y popup de recientes. Reusa
// la logica (FolderSwitchLogic), el estado (FolderSwitchState), el panel y el popup; lo propio de mac
// esta en MacFileDialogs (Accesibilidad para los dialogos, Apple Events para el Finder).
//
// Diferencia con Windows: en mac el observador compartido (context().foreground()) avisa cuando cambia
// la APP activa, no la ventana. Un dialogo se reconoce preguntando por la ventana enfocada de la app que
// queda al frente; y como el usuario abre el dialogo dentro de la misma app (Cmd+O en Nuke, sin cambio de
// app), al pasar al Finder se pregunta por el dialogo de la app de la que viene.
class FolderSwitchMacModule : public Module
{
    Q_OBJECT

public:
    explicit FolderSwitchMacModule(ModuleContext &context);
    ~FolderSwitchMacModule() override;

    void start() override;
    void stop() override;
    ModuleStatus status() const override;
    QWidget *createPanel(QWidget *parent) override;
    void fillTrayMenu(QMenu *menu) override;
    bool isPaused() const override;

    QStringList captureStates() const override;
    bool applyCaptureState(const QString &state) override;
    QWidget *createCaptureWidget(const QString &state, QWidget *parent) override;

    void setEnabled(bool enabled);
    void setAutoSwitch(bool autoSwitch);
    bool setManualShortcut(const Shortcut &shortcut);
    bool setRecentShortcut(const Shortcut &shortcut);
    QString validateShortcut(const Shortcut &candidate) const;

private slots:
    void onForegroundChanged(quintptr hwnd, quint32 pid, const QString &exeName);
    void onHotkeyActivated(int localId);

private:
    void declareAndRegisterShortcuts();
    void handleManualHotkey();
    void handleRecentHotkey();
    void scheduleSwitch(const MacFileDialogs::Dialog &dialog);
    void applyFolder(const MacFileDialogs::Dialog &dialog, const QString &path, const QString &source);
    void recordFinderFolder();
    void refreshAccessibility();
    void openAccessibilitySettings();
    FolderSwitchPanel::ViewState currentViewState() const;
    void refreshPanel();

    FolderSwitchState m_state;
    bool m_foregroundAcquired = false;
    bool m_accessibilityGranted = true;
    bool m_deniedNoticeShown = false; // el aviso de "permitir controlar el Finder", una vez por sesion
    QTimer *m_accessibilityTimer = nullptr;

    bool m_manualRegistered = false;
    bool m_recentRegistered = false;
    QString m_manualTakenBy;
    QString m_recentTakenBy;

    QPointer<FolderSwitchPanel> m_panel;
    RecentFoldersPopup *m_recentPopup = nullptr; // vivo solo durante exec()
    QString m_pendingRowFixture;

    // La app activa antes de la actual, y el ida-y-vuelta dialogo -> Finder -> dialogo.
    qint64 m_prevPid = 0;
    qint64 m_lastFinderSeenMs = 0;
    MacFileDialogs::Dialog m_lastDialog; // el ultimo dialogo de archivos visto al frente
    MacFileDialogs::Dialog m_pendingReturnDialog;
    MacFileDialogs::Dialog m_lastSwitchedDialog;
};

ModuleDescriptor folderSwitchDescriptor();
HelpSection folderSwitchHelp(const SettingsReader &value);

#endif // MIGHTYTOOLS_FOLDERSWITCH_MAC_MODULE_H
