#include "platform/FileSystemOps.h"

#include <QDir>
#include <QFileInfo>

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>

namespace {

FileSystemOps::Result fromErrno(int error)
{
    switch (error) {
    case ENOENT:
        return FileSystemOps::Result::Missing;
    case EBUSY:
    case ETXTBSY:
        return FileSystemOps::Result::InUse;
    case EACCES:
    case EPERM:
        return FileSystemOps::Result::Denied;
    case ENOTEMPTY:
    case EEXIST:
        return FileSystemOps::Result::NotEmpty;
    default:
        return FileSystemOps::Result::Failed;
    }
}

} // namespace

namespace FileSystemOps {

DirEnumerator::NativeString nativePath(const QString &path)
{
    DirEnumerator::NativeString native = DirEnumerator::nativeDir(path);
    if (native.size() > 1 && native.back() == '/') {
        native.pop_back();
    }
    return native;
}

Kind kind(const QString &path)
{
    struct stat info;
    if (lstat(nativePath(path).c_str(), &info) != 0) {
        return Kind::Missing;
    }
    if (S_ISLNK(info.st_mode)) {
        return Kind::Link;
    }
    if (S_ISDIR(info.st_mode)) {
        // Otro volumen montado en esta carpeta cuenta como enlace: no se entra ni se vacia.
        struct stat parent;
        const QString up = QFileInfo(path).absolutePath();
        if (lstat(nativePath(up).c_str(), &parent) == 0 && parent.st_dev != info.st_dev) {
            return Kind::Link;
        }
        return Kind::Dir;
    }
    return Kind::File;
}

// El borrado en macOS esta DESHABILITADO: este codigo todavia no se compilo ni se probo en una Mac, y
// las raices protegidas y las reglas de limpieza son las de Windows. SystemPaths::cleanupSupported()
// devuelve false y la ventana de limpieza no se ofrece; estas dos funciones no borran nada pase lo que
// pase. Lo que falta para habilitarlo esta en el roadmap (fase de macOS).
Result removeFile(const DirEnumerator::NativeString &path)
{
    Q_UNUSED(path);
    Q_UNUSED(fromErrno);
    return Result::Failed;
}

Result removeDir(const DirEnumerator::NativeString &path)
{
    Q_UNUSED(path);
    return Result::Failed;
}

bool isInUse(const DirEnumerator::NativeString &path)
{
    Q_UNUSED(path);
    return false;
}

bool isCloudPlaceholder(const DirEnumerator::NativeString &path)
{
    Q_UNUSED(path);
    return false;
}

QString canonicalPath(const QString &path)
{
    // realpath seguiria un enlace final: se resuelve la carpeta madre y se le pega el nombre.
    const QFileInfo info(QDir::cleanPath(path));
    if (kind(path) == Kind::Missing) {
        return QString();
    }
    if (info.isRoot()) {
        return info.absoluteFilePath();
    }
    char resolved[PATH_MAX];
    if (!realpath(info.absolutePath().toUtf8().constData(), resolved)) {
        return QString();
    }
    QString parent = QString::fromUtf8(resolved);
    if (!parent.endsWith(QLatin1Char('/'))) {
        parent += QLatin1Char('/');
    }
    return parent + info.fileName();
}

Identity identity(const QString &path)
{
    Identity id;
    struct stat info;
    if (lstat(nativePath(path).c_str(), &info) == 0) {
        id.volume = quint64(info.st_dev);
        id.file = quint64(info.st_ino);
        id.valid = true;
    }
    return id;
}

bool createDirLink(const QString &link, const QString &target)
{
    Q_UNUSED(link);
    Q_UNUSED(target);
    return false;
}

} // namespace FileSystemOps
