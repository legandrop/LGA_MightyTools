#include "core/BuildTree.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QProcessEnvironment>

// Criterio y comentarios en BuildTree.h. Copia de LGA_Base_QT_C_Py (src/utils/BuildTree.cpp) con
// dos cambios propios de esta app: el marcador de la raiz del repo es `src/` (no hay `py_scr/`), y
// macOS usa el MISMO criterio por estructura que Windows en vez del predicado viejo por nombre.

namespace {

QMutex &cacheMutex()
{
    static QMutex mutex;
    return mutex;
}

QHash<QString, bool> &cache()
{
    static QHash<QString, bool> map;
    return map;
}

// Cuantos niveles se sube buscando el `CMakeCache.txt`. En Windows el ejecutable vive en `build/`
// y, como mucho, un nivel mas abajo: dos alcanzan. En macOS vive en
// `build/<App>.app/Contents/MacOS/`, tres niveles abajo del `CMakeCache.txt`. Un tope corto evita
// que una instalacion colgada varios niveles abajo de CUALQUIER arbol de build ajeno pase por build.
#ifdef Q_OS_MACOS
constexpr int kMaxLevelsUp = 3;
#else
constexpr int kMaxLevelsUp = 2;
#endif

// Override explicito. Devuelve 1 o 0, o -1 si no hay override valido.
int envOverride()
{
    const QString raw =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("LGA_BUILD_TREE")).trimmed();
    if (raw.isEmpty()) {
        return -1;
    }
    static const QStringList kTrue{QStringLiteral("1"), QStringLiteral("true"),
                                   QStringLiteral("yes"), QStringLiteral("on")};
    static const QStringList kFalse{QStringLiteral("0"), QStringLiteral("false"),
                                    QStringLiteral("no"), QStringLiteral("off")};
    const QString value = raw.toLower();
    if (kTrue.contains(value)) {
        return 1;
    }
    if (kFalse.contains(value)) {
        return 0;
    }
    qWarning().noquote() << QString("[BuildTree] LGA_BUILD_TREE=%1 no es un valor valido "
                                    "(1/0, true/false, yes/no, on/off); se ignora")
                                .arg(raw);
    return -1;
}

// La carpeta del arbol de build que contiene a `appDir`, o vacio si no hay ninguna a mano.
QString findBuildRoot(const QString &appDir)
{
    QDir dir(appDir);
    for (int level = 0; level <= kMaxLevelsUp; ++level) {
        if (QFileInfo::exists(dir.filePath(QStringLiteral("CMakeCache.txt")))) {
            return dir.absolutePath();
        }
        if (dir.isRoot() || !dir.cdUp()) {
            return QString();
        }
    }
    return QString();
}

// Forma de la raiz del repo: el `CMakeLists.txt` y la carpeta de scripts conviven ahi. Se mira
// en el PADRE de la carpeta que tiene el `CMakeCache.txt`, y ninguna instalacion tiene las dos
// cosas. `deploy/` cuelga de la raiz del repo, asi que la cumple: lo que lo deja afuera es que
// no tenga `CMakeCache.txt`, y por eso `deploy.bat` borra el que copia de `build-release/`.
bool looksLikeSourceRoot(const QDir &dir)
{
    return QFileInfo::exists(dir.filePath(QStringLiteral("CMakeLists.txt")))
        && QFileInfo(dir.filePath(QStringLiteral("src"))).isDir();
}

bool evaluate(const QString &appDir)
{
    const int forced = envOverride();
    if (forced >= 0) {
        return forced == 1;
    }
    // Hacen falta LAS DOS cosas: un `CMakeCache.txt` cerca Y que arriba de el este la raiz
    // del repo.
    const QString buildRoot = findBuildRoot(appDir);
    if (buildRoot.isEmpty()) {
        return false;
    }
    QDir parent(buildRoot);
    return parent.cdUp() && looksLikeSourceRoot(parent);
}

} // namespace

namespace LgaBuildTree {

bool isBuildTree(const QString &appDir)
{
    if (appDir.trimmed().isEmpty()) {
        return false;
    }
    const QString key = QDir::cleanPath(QDir::fromNativeSeparators(appDir));
    {
        QMutexLocker locker(&cacheMutex());
        const auto it = cache().constFind(key);
        if (it != cache().constEnd()) {
            return *it;
        }
    }

    const bool result = evaluate(key);

    QMutexLocker locker(&cacheMutex());
    cache().insert(key, result);
    return result;
}

void warnIfInvalidOverride()
{
    envOverride();   // avisa por qWarning si el valor no es valido
}

void clearCache()
{
    QMutexLocker locker(&cacheMutex());
    cache().clear();
}

} // namespace LgaBuildTree
