#ifndef MIGHTYTOOLS_WINDOWLAYOUTSPANEL_H
#define MIGHTYTOOLS_WINDOWLAYOUTSPANEL_H

#include "modules/windowlayouts/WindowLayoutsState.h"

#include <QWidget>

class QCheckBox;
class QHideEvent;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class ShortcutRow;
class StatusCard;
class WindowLayoutsModule;
class WorkAreaPreview;

class WindowLayoutsPanel : public QWidget
{
public:
    WindowLayoutsPanel(WindowLayoutsModule *module, bool interactive, QWidget *parent = nullptr);
    void refresh();
    void showEditorForCapture(bool priority = false);

protected:
    void hideEvent(QHideEvent *event) override;

private:
    QWidget *createListPage();
    QWidget *createEditorPage();
    void rebuildList();
    void editLayout(int id = 0);
    void updateEditor();
    void saveEditor();
    void selectArea(bool pickWindow);
    void applyQuickPreset(int index);
    void resetDeleteConfirmation();
    void updatePriorityHint();

    WindowLayoutsModule *m_module = nullptr;
    bool m_interactive = false;
    QStackedWidget *m_pages = nullptr;
    QWidget *m_listPage = nullptr;
    QWidget *m_editorPage = nullptr;
    QWidget *m_rows = nullptr;
    StatusCard *m_statusCard = nullptr;
    QLabel *m_editorError = nullptr;
    QLabel *m_workAreaText = nullptr;
    QLineEdit *m_name = nullptr;
    QPushButton *m_preset = nullptr;
    QPushButton *m_horizontal = nullptr;
    QPushButton *m_vertical = nullptr;
    WorkAreaPreview *m_preview = nullptr;
    ShortcutRow *m_shortcut = nullptr;
    QCheckBox *m_priorityCheck = nullptr;
    QLabel *m_priorityHint = nullptr;
    QPushButton *m_remove = nullptr;
    WindowLayout m_draft;
    bool m_autoName = false;
    bool m_confirmDelete = false;
};

#endif // MIGHTYTOOLS_WINDOWLAYOUTSPANEL_H
