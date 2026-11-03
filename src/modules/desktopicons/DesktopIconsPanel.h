#ifndef MIGHTYTOOLS_DESKTOPICONSPANEL_H
#define MIGHTYTOOLS_DESKTOPICONSPANEL_H

#include <QHash>
#include <QPair>
#include <QWidget>

#include <functional>

class DesktopIconsModule;
class QLineEdit;
class QPushButton;
class StatusCard;
class SwitchRow;

class DesktopIconsPanel : public QWidget
{
public:
    DesktopIconsPanel(DesktopIconsModule *module, bool interactive, QWidget *parent = nullptr);
    void refresh();
    // Captura "confirm-delete": el borrado de la primera fila esperando el segundo click.
    void armFirstDeleteForCapture();

protected:
    void hideEvent(QHideEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void rebuildList();
    void armDelete(const QString &id);
    void saveSnapshot();
    void runLater(const std::function<void()> &action);
    void scrollToTop();

    DesktopIconsModule *m_module = nullptr;
    bool m_interactive = false;
    SwitchRow *m_daily = nullptr;
    StatusCard *m_result = nullptr;
    QLineEdit *m_name = nullptr;
    QWidget *m_rows = nullptr;
    QWidget *m_listHint = nullptr;
    QString m_firstId;
    // Por fila: el tacho y su confirmacion (borrar en dos clicks).
    QHash<QString, QPair<QPushButton *, QPushButton *>> m_deleteButtons;
};

#endif // MIGHTYTOOLS_DESKTOPICONSPANEL_H
