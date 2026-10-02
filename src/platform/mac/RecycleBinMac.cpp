#include "platform/RecycleBin.h"

#include "core/AutomatedRun.h"
#include "platform/DirEnumerator.h"
#include "platform/FileSystemOps.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <sys/stat.h>
#include <unistd.h>

#include <vector>

// La Papelera de macOS es una carpeta por volumen: ~/.Trash para el disco de arranque y
// <volumen>/.Trashes/<uid> para los demas. Se mide y se vacia recorriendola con DirEnumerator, sin seguir
// enlaces (un enlace se borra el, nunca su destino). No se usa el Finder: no hace falta el permiso de
// Automatizacion ni suena. Leerla pide acceso total al disco: sin el, query() responde que no sabe.
namespace {

QString trashOf(const QString &volumeRoot)
{
    const QString root = QDir::cleanPath(volumeRoot);
    if (root == QLatin1String("/")) {
        return QDir::homePath() + QStringLiteral("/.Trash");
    }
    return root + QStringLiteral("/.Trashes/") + QString::number(getuid());
}

// La carpeta de la Papelera, solo si es lo que tiene que ser: una carpeta de verdad (no un enlace), del
// usuario y en el mismo volumen. Un disco externo podria traer un .Trashes/<uid> que apunte a otro lado.
bool trustedTrash(const QString &trash, const QString &volumeRoot)
{
    struct stat info;
    struct stat root;
    if (lstat(QFile::encodeName(trash).constData(), &info) != 0 || stat(QFile::encodeName(volumeRoot).constData(), &root) != 0) {
        return false;
    }
    return S_ISDIR(info.st_mode) && info.st_uid == getuid() && info.st_dev == root.st_dev
           && FileSystemOps::kind(trash) == FileSystemOps::Kind::Dir;
}

struct Walk
{
    qint64 bytes = 0;
    qint64 items = 0;
    bool failed = false;
};

// Suma (y si `remove`, borra) lo que hay DENTRO de `dir`, de abajo hacia arriba.
void walk(const DirEnumerator::NativeString &dir, bool remove, std::vector<char> &scratch, Walk &walkState, bool topLevel)
{
    struct Child
    {
        DirEnumerator::NativeString path;
        bool isDir = false;
    };
    std::vector<Child> children;
    const bool opened = DirEnumerator::enumerate(dir, scratch, [&](const DirEnumerator::Entry &entry) {
        Child child;
        child.path = dir;
        child.path.append(entry.name, size_t(entry.nameLength));
        // Un enlace o un volumen montado adentro no se recorre: se borra (o se cuenta) como entrada.
        child.isDir = entry.isDir && !entry.isLink;
        if (!child.isDir) {
            walkState.bytes += qint64(entry.allocated);
        }
        if (topLevel) {
            ++walkState.items;
        }
        children.push_back(std::move(child));
    });
    if (!opened) {
        walkState.failed = true;
        return;
    }
    for (Child &child : children) {
        if (child.isDir) {
            walk(child.path + '/', remove, scratch, walkState, false);
            if (remove && FileSystemOps::removeDir(child.path) != FileSystemOps::Result::Removed) {
                walkState.failed = true;
            }
        } else if (remove && FileSystemOps::removeFile(child.path) != FileSystemOps::Result::Removed) {
            walkState.failed = true;
        }
    }
}

} // namespace

namespace RecycleBin {

Info query(const QString &volumeRoot)
{
    Info info;
    const QString trash = trashOf(volumeRoot);
    if (!QFileInfo::exists(trash)) {
        info.ok = true; // sin Papelera en ese volumen: vacia
        return info;
    }
    if (!trustedTrash(trash, volumeRoot)) {
        return info;
    }
    std::vector<char> scratch;
    Walk state;
    walk(DirEnumerator::nativeDir(trash), false, scratch, state, true);
    if (!state.failed) {
        info.ok = true;
        info.bytes = state.bytes;
        info.items = state.items;
    }
    return info;
}

bool empty(const QString &volumeRoot)
{
    if (AutomatedRun::active()) {
        qInfo().noquote() << QStringLiteral("[RecycleBin] (automatizada, sin vaciar) %1").arg(volumeRoot);
        return false;
    }
    const QString trash = trashOf(volumeRoot);
    if (!trustedTrash(trash, volumeRoot)) {
        return false;
    }
    std::vector<char> scratch;
    Walk state;
    walk(DirEnumerator::nativeDir(trash), true, scratch, state, true);
    const Info after = query(volumeRoot);
    return after.ok && after.items == 0;
}

bool moveToTrash(const QString &path, QString *error)
{
    if (AutomatedRun::active()) {
        qInfo().noquote() << QStringLiteral("[RecycleBin] (automatizada, sin mover) %1").arg(path);
        if (error) {
            *error = QStringLiteral("automated run");
        }
        return false;
    }
    // QFile::moveToTrash usa NSFileManager trashItemAtURL: si no se puede, falla; nunca borra definitivo.
    QFile file(path);
    if (file.moveToTrash()) {
        return true;
    }
    if (error) {
        *error = file.errorString();
    }
    return false;
}

} // namespace RecycleBin
