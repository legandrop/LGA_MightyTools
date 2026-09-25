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

    // ---- Persistente: cada cuantos minutos (uno solo para todos) y que discos, en el orden en que
    // se agregaron.
    int diskCheckMinutes() const { return m_diskCheckMinutes; }
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

    // Cada setter escribe (si corresponde) y avisa SOLO si el valor cambio.
    void setDiskCheckMinutes(int minutes);
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
    int m_diskCheckMinutes = DiskSpace::kDefaultIntervalMinutes;
    QList<DiskWatch> m_diskWatches;
    QHash<QString, DriveInfo> m_drives;
    QDateTime m_lastDiskCheck;
};

#endif // MIGHTYTOOLS_DISKSTATE_H
