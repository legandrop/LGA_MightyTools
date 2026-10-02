#ifndef MIGHTYTOOLS_FOLDERSWITCH_PANEL_H
#define MIGHTYTOOLS_FOLDERSWITCH_PANEL_H

#include "core/Shortcut.h"
#include "modules/folderswitch/FolderSwitchState.h"

#include <QDateTime>
#include <QString>
#include <QWidget>

class Chip;
class ElidedLabel;
class QCheckBox;
class QFrame;
class QLabel;
class ShortcutRow;
class StatusCard;

// Panel de Folder Switch (canvas, secciones 2 "Folder Switch" y 3 "Folder Switch · estados"):
// tarjeta de estado (Switching is on/paused), "Switch automatically", los dos atajos editables
// (ShortcutRow) y la tarjeta "Last folder". No escribe nada por su cuenta -- mismo criterio que
// GeneralPage: avisa con senales y quien arma el panel (FolderSwitchModule) decide y llama
// setState() de vuelta. Asi la captura de QA lo dibuja sin tocar el modulo real.
class FolderSwitchPanel : public QWidget
{
    Q_OBJECT

public:
    struct ViewState
    {
        bool enabled = true;
        bool autoSwitch = true;
        Shortcut manualShortcut;
        Shortcut recentShortcut;
        bool manualRegistered = true;
        bool recentRegistered = true;
        FolderSwitchState::LastSwitch lastSwitch;
        // macOS: falta el permiso de Accesibilidad (leer los dialogos y escribirles la carpeta). En
        // Windows siempre false.
        bool needsAccessibility = false;
    };

    explicit FolderSwitchPanel(QWidget *parent = nullptr);

    void setState(const ViewState &state);
    const ViewState &state() const { return m_state; }

    ShortcutRow *manualRow() const { return m_manualRow; }
    ShortcutRow *recentRow() const { return m_recentRow; }

signals:
    void toggleRequested(bool on);
    // macOS: "Open Settings" de la tarjeta de estado cuando falta el permiso de Accesibilidad.
    void accessibilityRequested();
    void autoSwitchToggled(bool on);
    void manualShortcutRecorded(const Shortcut &shortcut);
    void recentShortcutRecorded(const Shortcut &shortcut);

private:
    QFrame *buildShortcutsCard();
    QFrame *buildLastFolderCard();
    void updateLastFolderCard();

    ViewState m_state;

    StatusCard *m_status = nullptr;
    QCheckBox *m_autoSwitch = nullptr;
    ShortcutRow *m_manualRow = nullptr;
    ShortcutRow *m_recentRow = nullptr;

    QFrame *m_lastCard = nullptr;
    Chip *m_lastSourceChip = nullptr;
    Chip *m_lastResultChip = nullptr;
    QLabel *m_lastTimeLabel = nullptr;
    ElidedLabel *m_lastFieldValue = nullptr;
    QLabel *m_lastCaption = nullptr;
};

#endif // MIGHTYTOOLS_FOLDERSWITCH_PANEL_H
