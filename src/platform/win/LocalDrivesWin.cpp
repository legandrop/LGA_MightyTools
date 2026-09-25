#include "platform/LocalDrives.h"

#include <QDir>
#include <QStorageInfo>

#include <windows.h>

namespace LocalDrives {

bool isLocalVolume(const QStorageInfo &storage)
{
    // Discos internos y externos por USB (los grandes se reportan como fijos; los pendrives, como
    // removibles). Quedan afuera los de red, los CD/DVD y los RAM disk.
    const QString root = QDir::toNativeSeparators(storage.rootPath());
    const UINT type = GetDriveTypeW(reinterpret_cast<LPCWSTR>(root.utf16()));
    return type == DRIVE_FIXED || type == DRIVE_REMOVABLE;
}

void describe(const QStorageInfo &storage, DriveInfo *drive)
{
    drive->label = DiskSpace::labelForRoot(storage.rootPath(), QString());
    // Sin etiqueta de volumen, el mismo nombre que usa el Explorador.
    drive->name = storage.name().isEmpty() ? QStringLiteral("Local Disk") : storage.name();
}

} // namespace LocalDrives
