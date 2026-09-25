#ifndef MIGHTYTOOLS_FOLDERSWITCH_MODULE_H
#define MIGHTYTOOLS_FOLDERSWITCH_MODULE_H

#include "app/Module.h"
#include "core/Shortcut.h"
#include "modules/folderswitch/FolderSwitchPanel.h"
#include "modules/folderswitch/FolderSwitchState.h"
#include "ui/HelpSection.h"

#include <QPointer>
#include <QString>

#include <windows.h>

class FolderSwitchPanel;
class RecentFoldersPopup;

// Modulo Folder Switch (Windows unicamente): cambia la carpeta de los dialogos Abrir/Guardar a la
// que el usuario tiene abierta en Explorer o XYplorer, con un atajo manual (Ctrl+Alt+O) y otro para
// elegir entre las ultimas 5 carpetas (Ctrl+Alt+Shift+O). Puerto de LGA_FolderSwitch v0.10
// (TrayController + DialogSwitcher + UiaSwitcher + FolderResolver + WindowUtils) sobre el contrato
// Module/ModuleContext de Mighty Tools (plan, 4.2).
//
// Este archivo solo incluye <windows.h> porque el modulo entero es Windows-unicamente
// (platforms = PlatformWindows en el descriptor) y ModuleRegistry.cpp lo incluye detras de
// `#if defined(Q_OS_WIN)`: nunca se parsea al compilar para mac.
//
// Etapa 2 (este archivo): panel real (FolderSwitchPanel), popup de carpetas recientes
// (RecentFoldersPopup) que el atajo de recientes abre de verdad, entrada de bandeja
// "Pause/Resume switching", capturas de QA y el observador de ventana al frente COMPARTIDO
// (context().foreground(), plan 4.4) en vez de uno propio.
class FolderSwitchModule : public Module
{
    Q_OBJECT

public:
    explicit FolderSwitchModule(ModuleContext &context);
    ~FolderSwitchModule() override;

    void start() override;
    void stop() override;
    ModuleStatus status() const override;
    QWidget *createPanel(QWidget *parent) override;
    void fillTrayMenu(QMenu *menu) override;
    bool isPaused() const override;

    QStringList captureStates() const override;
    bool applyCaptureState(const QString &state) override;
    QWidget *createCaptureWidget(const QString &state, QWidget *parent) override;

    // Leido por el panel (chips "In use" junto a cada atajo) y por --self-test.
    bool manualShortcutRegistered() const { return m_manualRegistered; }
    bool recentShortcutRegistered() const { return m_recentRegistered; }
    // Titulo de la otra herramienta de Mighty Tools que ya declaro esa combinacion; vacio si el
    // atajo esta libre o si lo que lo rechazo fue el sistema operativo (otra app fuera de esta app).
    QString manualShortcutTakenBy() const { return m_manualTakenBy; }
    QString recentShortcutTakenBy() const { return m_recentTakenBy; }
    // Solo true entre start() y stop(): lo usa --self-test para confirmar que stop() suelta el
    // observador de ventana al frente compartido (context().foreground()).
    bool hasForegroundWatcher() const { return m_foregroundAcquired; }
    // Solo true mientras el popup de recientes esta abierto: lo usa --self-test.
    bool hasRecentPopup() const { return m_recentPopup != nullptr; }

    // Interruptor general y "Switch automatically": los llama el panel y la bandeja.
    void setEnabled(bool enabled);
    void setAutoSwitch(bool autoSwitch);
    // D-16: atajos editables con el lapiz. Declara y registra la combinacion nueva; si el sistema o
    // otra herramienta la rechazan, deja la anterior como estaba y devuelve false.
    bool setManualShortcut(const Shortcut &shortcut);
    bool setRecentShortcut(const Shortcut &shortcut);
    // Motivo de rechazo para el validador de un ShortcutRow (declaredByOtherModule ANTES de probe,
    // ver ModuleContext.h): "Already used by %1." o "%1 is taken by another app.", vacio si sirve.
    QString validateShortcut(const Shortcut &candidate) const;

private slots:
    void onForegroundChanged(quintptr hwnd, quint32 pid, const QString &exeName);
    void onHotkeyActivated(int localId);

private:
    enum class ManagerType { None, Explorer, XYplorer };

    void declareAndRegisterShortcuts();
    void handleManualHotkey();
    void handleRecentHotkey();
    QString resolveLastManagerPath() const;
    void scheduleSwitch(HWND dialogHwnd);
    void performSwitch(HWND dialogHwnd);
    void applyFolder(HWND dialogHwnd, const QString &path, const QString &source);
    void recordManagerFolder(HWND managerHwnd, ManagerType type);
    FolderSwitchPanel::ViewState currentViewState() const;
    void refreshPanel();

    FolderSwitchState m_state;
    bool m_foregroundAcquired = false;

    bool m_manualRegistered = false;
    bool m_recentRegistered = false;
    QString m_manualTakenBy;
    QString m_recentTakenBy;

    QPointer<FolderSwitchPanel> m_panel;
    RecentFoldersPopup *m_recentPopup = nullptr; // vivo solo durante exec(); ver handleRecentHotkey()
    // "recording"/"rejected": createPanel() los aplica sobre la fila recien construida, porque
    // applyCaptureState() siempre corre ANTES de que exista el panel (ver Module.h, "Captura").
    QString m_pendingRowFixture;

    // Estado del ultimo manager (Explorer/XYplorer) visto en foreground, y del ida-y-vuelta entre un
    // dialogo y ese manager. Mismos campos que TrayController en el origen.
    HWND m_lastManagerHwnd = nullptr;
    ManagerType m_lastManagerType = ManagerType::None;
    qint64 m_lastManagerSeenMs = 0;
    HWND m_lastDialogHwnd = nullptr;
    HWND m_pendingReturnDialog = nullptr;
    HWND m_lastSwitchedDialogHwnd = nullptr;
    HWND m_prevManagerHwnd = nullptr;
    ManagerType m_prevManagerType = ManagerType::None;
};

// Descriptor estatico: id, titulo, descripcion y bullets EXACTOS del canvas de diseno aprobado
// (+Building_Blocks/canvas/MightyTools.html, entrada "fs" de MODS), icono vectorial, fabrica, atajos
// configurados, self-test y simulate-action.
ModuleDescriptor folderSwitchDescriptor();

// Seccion de la ayuda unica (canvas, seccion 6 "Folder Switch"), registrada en
// ModuleRegistry::helpProviders().
HelpSection folderSwitchHelp(const SettingsReader &value);

#endif // MIGHTYTOOLS_FOLDERSWITCH_MODULE_H
