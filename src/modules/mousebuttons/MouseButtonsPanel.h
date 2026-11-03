#ifndef MIGHTYTOOLS_MOUSEBUTTONSPANEL_H
#define MIGHTYTOOLS_MOUSEBUTTONSPANEL_H

#include "modules/mousebuttons/MouseButtonsState.h"

#include <QHash>
#include <QIcon>
#include <QWidget>

class CurrentPageStack;
class MouseButtonsModule;
class QHideEvent;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class ShortcutRow;
class StatusCard;
class SwitchRow;

// La forma y los materiales de App Toggle y Window Layouts: tarjeta de estado, lista con el icono de cada
// app, su boton, lo que mantiene, el lapiz y su interruptor, y el editor en su propia pagina.
class MouseButtonsPanel : public QWidget
{
public:
    MouseButtonsPanel(MouseButtonsModule *module, bool interactive, QWidget *parent = nullptr);
    void refresh();
    // Captura de QA: el editor de una regla nueva (0) o de la regla `id`, con el desplegable de boton abierto.
    void showEditorForCapture(int id, bool buttonMenuOpen);

protected:
    void hideEvent(QHideEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *createListPage();
    QWidget *createEditorPage();
    void rebuildList();
    void editRule(int id = 0);
    void updateEditor();
    void saveEditor();
    void browse();
    void resetDeleteConfirmation();
    void fillButtonMenu(QMenu *menu) const;
    QIcon iconFor(const QString &path);

    MouseButtonsModule *m_module = nullptr;
    bool m_interactive = false;
    CurrentPageStack *m_pages = nullptr;
    QWidget *m_listPage = nullptr;
    QWidget *m_editorPage = nullptr;
    QWidget *m_rows = nullptr;
    StatusCard *m_statusCard = nullptr;
    QLabel *m_editorError = nullptr;
    QLineEdit *m_app = nullptr;
    QPushButton *m_button = nullptr;
    ShortcutRow *m_keys = nullptr;
    SwitchRow *m_holdLeft = nullptr;
    QPushButton *m_remove = nullptr;
    QMenu *m_captureMenu = nullptr;
    QHash<QString, QIcon> m_icons;
    MouseRule m_draft;
    bool m_confirmDelete = false;
};

#endif // MIGHTYTOOLS_MOUSEBUTTONSPANEL_H
