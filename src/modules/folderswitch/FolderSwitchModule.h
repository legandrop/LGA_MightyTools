#ifndef MIGHTYTOOLS_FOLDERSWITCH_MODULE_H
#define MIGHTYTOOLS_FOLDERSWITCH_MODULE_H

#include "app/Module.h"
#include "core/Shortcut.h"
#include "modules/folderswitch/FolderSwitchState.h"

#include <QString>

#include <memory>

#include <windows.h>

class ForegroundWatcher;

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
// Etapa 1 (este archivo): logica completa de deteccion/inyeccion, los dos atajos declarados y
// registrados en ModuleContext::hotkeys(), y el estado persistido (FolderSwitchState). Sin panel real
// (createPanel() es un placeholder) ni popup de carpetas recientes: eso es la etapa 2, cuando el
// atajo de recientes ya tiene un menu que mostrar (D-16: atajos editables).
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
    bool isPaused() const override;

    // Leido por el panel de la etapa 2 (chips "In use" junto a cada atajo) y por --self-test.
    bool manualShortcutRegistered() const { return m_manualRegistered; }
    bool recentShortcutRegistered() const { return m_recentRegistered; }
    // Titulo de la otra herramienta de Mighty Tools que ya declaro esa combinacion; vacio si el
    // atajo esta libre o si lo que lo rechazo fue el sistema operativo (otra app fuera de esta app).
    QString manualShortcutTakenBy() const { return m_manualTakenBy; }
    QString recentShortcutTakenBy() const { return m_recentTakenBy; }
    // Solo true entre start() y stop(): lo usa --self-test para confirmar que stop() no deja el
    // observador de ventana al frente vivo.
    bool hasForegroundWatcher() const { return m_foregroundWatcher != nullptr; }

    // Para el panel de la etapa 2 (D-16: atajos editables con el lapiz, como en Nuke Shortcuts).
    // Declara y registra la combinacion nueva; si el sistema o otra herramienta la rechazan, deja la
    // anterior como estaba y devuelve false.
    bool setManualShortcut(const Shortcut &shortcut);
    bool setRecentShortcut(const Shortcut &shortcut);

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

    FolderSwitchState m_state;
    std::unique_ptr<ForegroundWatcher> m_foregroundWatcher;

    Shortcut m_manualShortcut;
    Shortcut m_recentShortcut;
    bool m_manualRegistered = false;
    bool m_recentRegistered = false;
    QString m_manualTakenBy;
    QString m_recentTakenBy;

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

#endif // MIGHTYTOOLS_FOLDERSWITCH_MODULE_H
