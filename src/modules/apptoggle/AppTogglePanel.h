#ifndef MIGHTYTOOLS_APPTOGGLEPANEL_H
#define MIGHTYTOOLS_APPTOGGLEPANEL_H

#include "modules/apptoggle/AppToggleState.h"

#include <QHash>
#include <QIcon>
#include <QWidget>

class AppToggleModule;
class CurrentPageStack;
class QHideEvent;
class QLabel;
class QLineEdit;
class QPushButton;
class ShortcutRow;
class StatusCard;

// El panel copia la forma y los materiales del de Window Layouts: tarjeta de estado, lista con el icono
// de cada app, sus teclas, el lapiz y su interruptor, y el editor en su propia pagina.
class AppTogglePanel : public QWidget
{
public:
    AppTogglePanel(AppToggleModule *module, bool interactive, QWidget *parent = nullptr);
    void refresh();
    // Captura de QA: el editor de una entrada nueva (0) o de la entrada `id`, con un error abajo o el
    // grabador abierto con ese texto.
    void showEditorForCapture(int id, const QString &error = QString(), const QString &recording = QString());

protected:
    void hideEvent(QHideEvent *event) override;

private:
    QWidget *createListPage();
    QWidget *createEditorPage();
    void rebuildList();
    void editApp(int id = 0);
    void updateEditor();
    void saveEditor();
    void browse();
    void resetDeleteConfirmation();
    void updateFolderPlaceholder();
    QIcon iconFor(const QString &path);

    AppToggleModule *m_module = nullptr;
    bool m_interactive = false;
    CurrentPageStack *m_pages = nullptr;
    QWidget *m_listPage = nullptr;
    QWidget *m_editorPage = nullptr;
    QWidget *m_rows = nullptr;
    StatusCard *m_statusCard = nullptr;
    QLabel *m_editorError = nullptr;
    QLineEdit *m_name = nullptr;
    QLineEdit *m_path = nullptr;
    QLineEdit *m_arguments = nullptr;
    QLineEdit *m_folder = nullptr;
    ShortcutRow *m_shortcut = nullptr;
    QPushButton *m_remove = nullptr;
    QHash<QString, QIcon> m_icons;
    AppEntry m_draft;
    bool m_confirmDelete = false;
};

#endif // MIGHTYTOOLS_APPTOGGLEPANEL_H
