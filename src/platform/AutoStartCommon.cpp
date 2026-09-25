#include "platform/AutoStart.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace {

// Si `appPath` (limpio, con '/') cuelga de `rootDirLiteral`. Se compara por COMPONENTE de ruta: un
// `startsWith` a secas haria que `C:/x/build2` cuente como adentro de `C:/x/build`.
[[maybe_unused]] bool pathIsInside(const QString &appPath, const char *rootDirLiteral)
{
    const QString rootDir = QDir::cleanPath(QDir::fromNativeSeparators(QLatin1String(rootDirLiteral)));
    if (rootDir.isEmpty()) {
        return false;
    }
    // En Windows la misma ruta puede llegar con distinta capitalizacion (`C:` vs `c:`).
    return appPath.startsWith(rootDir + QLatin1Char('/'), Qt::CaseInsensitive);
}

} // namespace

namespace AutoStart {

bool runsFromDevelopmentTree()
{
    const QString appPath = QDir::cleanPath(QDir::fromNativeSeparators(QCoreApplication::applicationFilePath()));
    // Solo la carpeta que CONTIENE al exe, nunca la ruta entera: `D:\Builds\Apps\X.exe` es una
    // instalacion legitima y su carpeta es `Apps`. En macOS la carpeta del ejecutable es
    // `Contents/MacOS`, asi que se mira la que contiene al `.app`.
    QDir dir = QFileInfo(appPath).dir();
#ifdef Q_OS_MACOS
    dir.cdUp(); // Contents
    dir.cdUp(); // <App>.app
    dir.cdUp(); // carpeta que contiene al bundle
#endif
    const QString folder = dir.dirName();
    if (folder.contains(QLatin1String("build"), Qt::CaseInsensitive)
        || folder.contains(QLatin1String("deploy"), Qt::CaseInsensitive)) {
        return true;
    }
#ifdef LGA_BUILD_TREE_DIR
    if (pathIsInside(appPath, LGA_BUILD_TREE_DIR)) {
        return true;
    }
#endif
#ifdef LGA_SOURCE_TREE_DIR
    if (pathIsInside(appPath, LGA_SOURCE_TREE_DIR)) {
        return true;
    }
#endif
    return false;
}

QString checkboxText()
{
#ifdef Q_OS_MACOS
    return QStringLiteral("Open at login");
#else
    return QStringLiteral("Start with Windows");
#endif
}

} // namespace AutoStart
