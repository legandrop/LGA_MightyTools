#include "platform/RecycleBin.h"

// Deshabilitado en macOS hasta probarlo en una Mac (ver FileSystemOpsMac.cpp): nada se vacia ni se mueve.
namespace RecycleBin {

Info query(const QString &volumeRoot)
{
    Q_UNUSED(volumeRoot);
    return Info();
}

bool empty(const QString &volumeRoot)
{
    Q_UNUSED(volumeRoot);
    return false;
}

bool moveToTrash(const QString &path, QString *error)
{
    Q_UNUSED(path);
    if (error) {
        *error = QStringLiteral("not available on macOS yet");
    }
    return false;
}

} // namespace RecycleBin
