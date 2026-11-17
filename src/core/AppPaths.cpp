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
        // La raiz del repo la decide LgaBuildTree, la misma funcion que dijo que es un arbol de build.
        return LgaBuildTree::appRootDir(exe);
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
