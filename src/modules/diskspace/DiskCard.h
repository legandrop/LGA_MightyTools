#ifndef MIGHTYTOOLS_DISKCARD_H
#define MIGHTYTOOLS_DISKCARD_H

#include "modules/diskspace/DiskSpace.h"

#include <QFrame>
#include <QMap>
#include <QWidget>

class DiskState;
class Chip;
class ElidedLabel;
class QLabel;
class QMenu;
class QPushButton;
class QSpinBox;
class QVBoxLayout;

// Barra de uso de un disco: lo ocupado sobre el total y una marca donde empieza a contar como bajo.
// Pasada la marca, la barra se pinta de ambar.
class UsageBar : public QWidget
{
public:
    explicit UsageBar(QWidget *parent = nullptr);
    // used y mark en fraccion del total (0..1). connected = false: solo el riel, atenuado.
    void set(bool connected, double used, double mark, bool low);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    bool m_connected = false;
    double m_used = 0.0;
    double m_mark = 0.0;
    bool m_low = false;
};

// Una fila de la tarjeta: keycap del disco, nombre, umbral editable (numero + GB/%), el boton para
// dejar de vigilarlo, la barra y debajo cuanto queda libre.
class DriveRow : public QWidget
{
    Q_OBJECT

public:
    explicit DriveRow(const QString &root, QWidget *parent = nullptr);

    QString root() const { return m_root; }
    // drive = nullptr: el disco no esta enchufado.
    void update(const DiskWatch &watch, const DriveInfo *drive);
    // El campo tiene el teclado (el usuario esta escribiendo): refrescar no lo pisa.
    bool isEditing() const;

signals:
    void thresholdChanged(const QString &root, int value, DiskWatch::Unit unit);
    void removeRequested(const QString &root);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void onUnitClicked(DiskWatch::Unit unit);

    QString m_root;
    DiskWatch m_watch;
    Chip *m_keycap = nullptr;
    ElidedLabel *m_name = nullptr;
    QLabel *m_missing = nullptr;
    QSpinBox *m_value = nullptr;
    QPushButton *m_gb = nullptr;
    QPushButton *m_percent = nullptr;
    QPushButton *m_remove = nullptr;
    UsageBar *m_bar = nullptr;
    QLabel *m_free = nullptr;
    QLabel *m_threshold = nullptr;
};

// Tarjeta "Watched drives" del panel de Disk Space (D-07 de Nuke Shortcuts): "Remind me every", una fila por disco
// vigilado y "Add drive...". Crece con cada disco; el panel de la ventana tiene scroll.
// Lee todo de DiskState y escribe en DiskState. Con interactive = false (captura de QA) no conecta
// nada.
class DiskCard : public QFrame
{
    Q_OBJECT

public:
    DiskCard(DiskState *state, bool interactive, QWidget *parent = nullptr);

    void refresh();

    // Arma el menu de "Add drive..." con los discos locales sin vigilar. Publico para la captura.
    void fillAddMenu(QMenu *menu) const;
    // El menu de "Remind me every" ("15 min" tildado). Publico para la captura.
    void fillRemindMenu(QMenu *menu) const;
    QPushButton *addButton() const { return m_addButton; }

signals:
    // Antes de abrir el menu de discos: el modulo (que tiene el sistema) lista los discos.
    void drivesRefreshRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void rebuildRows(const QList<DiskWatch> &watches);
    void showAddMenu();
    void showRemindMenu();

    DiskState *m_state = nullptr;
    bool m_interactive = false;

    Chip *m_chip = nullptr;
    QLabel *m_emptyCaption = nullptr;
    QWidget *m_remindRow = nullptr;
    QPushButton *m_remindButton = nullptr;
    QLabel *m_lastCheck = nullptr;
    QWidget *m_listBlock = nullptr;
    QVBoxLayout *m_rowsLayout = nullptr;
    QStringList m_rowRoots;
    QMap<QString, DriveRow *> m_rows;
    QPushButton *m_addButton = nullptr;
};

#endif // MIGHTYTOOLS_DISKCARD_H
