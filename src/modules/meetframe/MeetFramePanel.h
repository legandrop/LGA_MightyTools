#ifndef MIGHTYTOOLS_MEETFRAMEPANEL_H
#define MIGHTYTOOLS_MEETFRAMEPANEL_H

#include <QHash>
#include <QWidget>

class MeetFrameModule;
class QPushButton;
class QVBoxLayout;
class SegmentedSwitch;
class ShortcutRow;
class StatusCard;
class SwitchRow;

// Panel de Meet Frame: el estado (con abrir / cerrar el marco), los dos atajos, lo que se tapa (avisos,
// escritorio, grupos de apps), las apps propias y la imagen (puntero y cuadros por segundo).
class MeetFramePanel : public QWidget
{
    Q_OBJECT

public:
    MeetFramePanel(MeetFrameModule *module, bool interactive, QWidget *parent = nullptr);

public slots:
    void refresh();

private:
    QWidget *buildShortcutCard();
    QWidget *buildHideCard();
    QWidget *buildAppsCard();
    QWidget *buildPictureCard();
    void rebuildApps();
    void showAddMenu();
    void browse();
    void statusAction();

    MeetFrameModule *m_module = nullptr;
    bool m_interactive = false;
    StatusCard *m_status = nullptr;
    ShortcutRow *m_toggle = nullptr;
    ShortcutRow *m_pause = nullptr;
    SwitchRow *m_notifications = nullptr;
    SwitchRow *m_desktop = nullptr;
    QHash<QString, SwitchRow *> m_groups;
    QWidget *m_appRows = nullptr;
    QVBoxLayout *m_appLayout = nullptr;
    QPushButton *m_add = nullptr;
    SwitchRow *m_cursor = nullptr;
    SegmentedSwitch *m_fps = nullptr;
    SegmentedSwitch *m_aspect = nullptr;
    QString m_appsKey;
};

#endif // MIGHTYTOOLS_MEETFRAMEPANEL_H
