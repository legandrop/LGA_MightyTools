#include "platform/LocalDrives.h"

#include <QStorageInfo>

#include <algorithm>

namespace {

bool read(const QStorageInfo &storage, DriveInfo *drive)
{
    if (!storage.isValid() || !storage.isReady() || storage.bytesTotal() <= 0) {
        return false;
    }
    drive->root = storage.rootPath();
    drive->totalBytes = storage.bytesTotal();
    // bytesAvailable y no bytesFree: es lo que el usuario puede escribir de verdad (descuenta
    // cuotas y lo reservado para el sistema).
    drive->freeBytes = storage.bytesAvailable();
    LocalDrives::describe(storage, drive);
    return true;
}

} // namespace

namespace LocalDrives {

QList<DriveInfo> list()
{
    QList<DriveInfo> drives;
    for (const QStorageInfo &storage : QStorageInfo::mountedVolumes()) {
        DriveInfo drive;
        if (!storage.isReadOnly() && isLocalVolume(storage) && read(storage, &drive)) {
            drives.append(drive);
        }
    }
    std::sort(drives.begin(), drives.end(), [](const DriveInfo &a, const DriveInfo &b) { return a.root < b.root; });
    return drives;
}

bool query(const QString &root, DriveInfo *drive)
{
    const QStorageInfo storage(root);
    // QStorageInfo de una ruta devuelve el volumen que la contiene: si el disco se desenchufo y la
    // raiz ya no existe, en mac seria el disco del sistema. Tiene que ser el mismo volumen.
    if (storage.rootPath() != root) {
        return false;
    }
    return read(storage, drive);
}

} // namespace LocalDrives
