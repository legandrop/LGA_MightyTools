#ifndef MIGHTYTOOLS_FOLDERSWITCH_STATE_H
#define MIGHTYTOOLS_FOLDERSWITCH_STATE_H

#include "core/Shortcut.h"

#include <QDateTime>
#include <QString>
#include <QStringList>

class ModuleContext;

// Estado de Folder Switch: lo persistido (seccion [folderSwitch] de settings.ini, via ModuleContext)
// mas el ultimo switch, que vive SOLO en memoria (D-16 no toca esto: en el origen -- AppState.cpp de
// LGA_FolderSwitch -- `lastSwitch` nunca se escribe en el .ini y se resetea en cada arranque; ver
// Docs/Inventario_Opciones.md, "El ultimo switch NO persiste entre reinicios").
//
// `context == nullptr` sirve para probar la logica de deduplicado en --self-test sin un ModuleContext
// real: en ese modo nada se lee ni se escribe, todo vive en memoria desde los defaults.
class FolderSwitchState
{
public:
    struct LastSwitch
    {
        QString path;
        QString source; // "Explorer", "XYplorer" o "Recent"
        bool applied = false;
        QDateTime when;
        bool isValid() const { return !path.isEmpty(); }
    };

    static constexpr int kMaxRecentFolders = 5;

    explicit FolderSwitchState(ModuleContext *context = nullptr);

    // Los atajos de fabrica: Ctrl+Alt+O y Ctrl+Alt+Shift+O, iguales a la version AutoHotkey/Qt de
    // origen. `configuredShortcuts` del descriptor y el self-test los usan sin construir el modulo.
    static Shortcut defaultManualShortcut();
    static Shortcut defaultRecentShortcut();

    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    bool autoSwitch() const { return m_autoSwitch; }
    void setAutoSwitch(bool autoSwitch);

    Shortcut manualShortcut() const { return m_manualShortcut; }
    Shortcut recentShortcut() const { return m_recentShortcut; }
    void setManualShortcut(const Shortcut &shortcut);
    void setRecentShortcut(const Shortcut &shortcut);

    // La mas reciente primero, sin repetidas (ver sameFolder), hasta kMaxRecentFolders.
    QStringList recentFolders() const { return m_recentFolders; }
    // La pone primera (si ya estaba, la mueve) y recorta a kMaxRecentFolders. No persiste "ultimo
    // switch": eso es setLastSwitch(), aparte.
    void addRecentFolder(const QString &path);

    // En memoria, nunca en el .ini (ver comentario de la clase).
    LastSwitch lastSwitch() const { return m_lastSwitch; }
    void setLastSwitch(const LastSwitch &lastSwitch) { m_lastSwitch = lastSwitch; }

    // Misma carpeta sin importar mayusculas/minusculas ni una barra final de mas. Publico para poder
    // probarlo directo desde --self-test.
    static bool sameFolder(const QString &a, const QString &b);

private:
    ModuleContext *m_context = nullptr;
    bool m_enabled = true;
    bool m_autoSwitch = true;
    Shortcut m_manualShortcut;
    Shortcut m_recentShortcut;
    QStringList m_recentFolders;
    LastSwitch m_lastSwitch;
};

#endif // MIGHTYTOOLS_FOLDERSWITCH_STATE_H
