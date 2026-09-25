#include "core/AppPaths.h"
#include "core/BuildTree.h"

#include <QDir>
#include <QFileInfo>

#ifdef Q_OS_WIN
#include <windows.h>
#include <iterator>
#endif

namespace {

QString g_argv0;

// Sube desde la carpeta del exe hasta la raiz del repo (CMakeLists.txt + src/). Solo se usa cuando
// isBuildTree() ya dijo que es un arbol de build, asi que la raiz existe a pocos niveles.
QString findSourceRoot(const QString &fromDir)
{
    QDir dir(fromDir);
    for (int level = 0; level <= 5; ++level) {
        if (QFileInfo::exists(dir.filePath(QStringLiteral("CMakeLists.txt")))
            && QFileInfo(dir.filePath(QStringLiteral("src"))).isDir()) {
            return dir.absolutePath();
        }
        if (dir.isRoot() || !dir.cdUp()) {
            break;
        }
    }
    return fromDir;
}

} // namespace

namespace AppPaths {

void init(const char *argv0)
{
    g_argv0 = QString::fromLocal8Bit(argv0 ? argv0 : "");
}

QString exeDir()
{
#ifdef Q_OS_WIN
    // Sin pasar por QCoreApplication: sin instancia, applicationDirPath() da vacio, y lanzada por la
    // Run key el directorio de trabajo es System32 (no sirve de referencia).
    wchar_t buffer[4096] = {};
    const DWORD len = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    if (len == 0 || len >= std::size(buffer)) {
        return QString();
    }
    return QFileInfo(QString::fromWCharArray(buffer, static_cast<int>(len))).absolutePath();
#else
    return QFileInfo(g_argv0).absoluteDir().absolutePath();
#endif
}

bool isBuildTree()
{
    return LgaBuildTree::isBuildTree(exeDir());
}

QString rootDir()
{
    const QString exe = exeDir();
    if (isBuildTree()) {
        return findSourceRoot(exe);
    }
#ifdef Q_OS_MACOS
    const QString support = QDir::homePath() + QStringLiteral("/Library/Application Support/LGA/LGA_MightyTools");
    QDir().mkpath(support);
    return support;
#else
    return exe;
#endif
}

QString configFile(const QString &name)
{
    return QDir::cleanPath(QDir(rootDir()).filePath(QStringLiteral("config/") + name));
}

QString logFile()
{
    return QDir::cleanPath(QDir(rootDir()).filePath(QStringLiteral("debug.log")));
}

} // namespace AppPaths
