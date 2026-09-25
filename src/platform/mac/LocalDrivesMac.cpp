#include "platform/LocalDrives.h"

#include <QStorageInfo>

namespace LocalDrives {

bool isLocalVolume(const QStorageInfo &storage)
{
    const QString root = storage.rootPath();
    if (root != QLatin1String("/") && !root.startsWith(QLatin1String("/Volumes/"))) {
        return false; // /System/Volumes/*, /dev, /private/var/vm: volumenes del sistema, no discos
    }
    static const QList<QByteArray> network = {"smbfs", "nfs", "afpfs", "webdav", "cifs", "ftp", "autofs"};
    return !network.contains(storage.fileSystemType());
}

void describe(const QStorageInfo &storage, DriveInfo *drive)
{
    // En mac no hay letras: el keycap lleva el nombre del volumen ("Macintosh HD", "Cache").
    drive->label = storage.displayName().isEmpty() ? DiskSpace::labelForRoot(storage.rootPath(), QString())
                                                   : storage.displayName();
    drive->name = QString();
}

} // namespace LocalDrives
