#ifndef MIGHTYTOOLS_DISKREMINDERTOAST_H
#define MIGHTYTOOLS_DISKREMINDERTOAST_H

#include "modules/diskspace/DiskSpace.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QResizeEvent;

// Construir el aviso no lo presenta ni guarda ajustes: las acciones salen por señales.
class DiskReminderToast : public QWidget
{
    Q_OBJECT
public:
    DiskReminderToast(DriveInfo drive, DiskWatch watch, int minutes, QWidget *parent = nullptr);
    QString root() const { return m_watch.root; }
    DiskWatch::Unit unit() const { return m_watch.unit; }
    int threshold() const { return m_watch.value; }

signals:
    void reminderRequested(int minutes, int value);
    void cleanupRequested();
    void settingsRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    bool thresholdValue(int *value) const;
    void updateError();

    DiskWatch m_watch;
    QComboBox *m_minutes = nullptr;
    QLineEdit *m_value = nullptr;
    QLabel *m_error = nullptr;
};

#endif // MIGHTYTOOLS_DISKREMINDERTOAST_H
