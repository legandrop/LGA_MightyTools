#ifndef MIGHTYTOOLS_DISKSTATE_H
#define MIGHTYTOOLS_DISKSTATE_H

#include "modules/diskspace/DiskSpace.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>

class ModuleContext;

// Estado de Disk Space que ve el usuario: la UNICA fuente de verdad de la herramienta. La tarjeta,
// la linea de la lista, el menu de la bandeja y el tooltip leen de aca (es la parte de discos del
// AppState de Nuke Shortcuts).
//
// Con un ModuleContext lee y escribe la seccion [diskSpace] de settings.ini. Sin contexto (captura,
// self-test) no toca el disco nunca.
class DiskState : public QObject
{
    Q_OBJECT

public:
    explicit DiskState(ModuleContext *context, QObject *parent = nullptr);

    // ---- Persistente: cada cuantos minutos se repite el aviso (uno solo para todos) y que discos, en
    // el orden en que se agregaron.
    int remindMinutes() const { return m_remindMinutes; }
    QList<DiskWatch> diskWatches() const { return m_diskWatches; }
    bool isWatched(const QString &root) const;

    // ---- De esta sesion
    // Ultima lectura de cada disco local, por raiz. Un disco vigilado que no esta aca no esta
    // enchufado. La llena DiskMonitor (o el fixture de la captura).
    bool driveReading(const QString &root, DriveInfo *drive) const;
    QList<DriveInfo> drives() const;
    QDateTime lastDiskCheck() const { return m_lastDiskCheck; }
    // Los vigilados que estan enchufados y por debajo de su umbral, en el orden de la lista.
    QList<DiskWatch> lowWatches() const;

    // Historial de avisos de cada disco vigilado (si estaba bajo y cuando se aviso por ultima vez).
    // Se guarda con el disco en la seccion: apagar y prender Disk Space, o reiniciar la app, no
    // adelanta un recordatorio. Dejar de vigilar un disco borra su historial.
    DiskSpace::AlertState alertState(const QString &root) const;
    void setAlertState(const QString &root, const DiskSpace::AlertState &alert);
    // "Remind me again in" del aviso: el proximo recordatorio de `root` llega a `minutes` de `now`.
    // Solo un disco vigilado y una opcion de remindChoices(); si no, no hace nada.
    void snooze(const QString &root, int minutes, const QDateTime &now);

    // Cada setter escribe (si corresponde) y avisa SOLO si el valor cambio.
    void setRemindMinutes(int minutes);
    // Un disco nuevo toma la unidad y el valor del ultimo de la lista; sin ninguno, 50 GB.
    void addDiskWatch(const QString &root, const QString &name);
    void removeDiskWatch(const QString &root);
    void setDiskThreshold(const QString &root, int value, DiskWatch::Unit unit);
    // Lecturas: `listedAll` reemplaza todo (listado completo de discos locales); si no, solo toca las
    // raices de `queried` (un chequeo que lee nada mas los vigilados, para no despertar otros discos).
    void setDriveReadings(const QList<DriveInfo> &readings, const QStringList &queried, bool listedAll,
                          const QDateTime &checkedAt);

signals:
    void changed();

private:
    void writeDiskWatches();

    ModuleContext *m_context = nullptr;
    int m_remindMinutes = DiskSpace::kDefaultRemindMinutes;
    QList<DiskWatch> m_diskWatches;
    QHash<QString, DiskSpace::AlertState> m_alerts;
    QHash<QString, DriveInfo> m_drives;
    QDateTime m_lastDiskCheck;
};

#endif // MIGHTYTOOLS_DISKSTATE_H
