#include "platform/SystemPaths.h"

// La limpieza de discos no esta habilitada en macOS (ver FileSystemOpsMac.cpp): la ventana no se
// ofrece y nada de esto se usa todavia.
namespace SystemPaths {

bool cleanupSupported()
{
    return false;
}

CleanupBases cleanupBases()
{
    return CleanupBases();
}

QStringList protectedFolders()
{
    return {};
}

QStringList protectedTrees()
{
    return {};
}

QStringList cloudFolders()
{
    return {};
}

QSet<QString> runningPrograms()
{
    return {};
}

void revealInFileManager(const QString &path)
{
    Q_UNUSED(path);
}

void openSystemCleanup()
{
}

} // namespace SystemPaths
