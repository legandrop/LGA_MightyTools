#include "platform/FileSystemOps.h"

#include "core/AutomatedRun.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>

#include <libproc.h>
#include <sys/attr.h>
#include <sys/proc_info.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

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
        // Otro volumen montado en esta carpeta cuenta como enlace: no se entra ni se vacia. El volumen de
        // datos (/System/Volumes/Data) tiene el mismo st_dev que "/": se pregunta si es punto de montaje.
        struct stat parent;
        const QString up = QFileInfo(path).absolutePath();
        if (lstat(nativePath(up).c_str(), &parent) == 0 && parent.st_dev != info.st_dev) {
            return Kind::Link;
        }
        attrlist request {};
        request.bitmapcount = ATTR_BIT_MAP_COUNT;
        request.dirattr = ATTR_DIR_MOUNTSTATUS;
        struct
        {
            uint32_t length;
            uint32_t mountStatus;
        } __attribute__((aligned(4), packed)) reply;
        if (getattrlist(nativePath(path).c_str(), &request, &reply, sizeof(reply), FSOPT_NOFOLLOW) == 0
            && (reply.mountStatus & DIR_MNTSTATUS_MNTPOINT) && !QFileInfo(path).isRoot()) {
            return Kind::Link;
        }
        return Kind::Dir;
    }
    return Kind::File;
}

Result removeFile(const DirEnumerator::NativeString &path)
{
    if (AutomatedRun::active() && !AutomatedRun::mayModify(DirEnumerator::toDisplay(path))) {
        return Result::Denied;
    }
    // unlink sobre un enlace simbolico borra el enlace, nunca su destino. Un archivo trabado desde el
    // Finder ("Locked", UF_IMMUTABLE) da EPERM y se queda: la traba es una decision del usuario.
    if (unlink(path.c_str()) == 0) {
        return Result::Removed;
    }
    return fromErrno(errno);
}

Result removeDir(const DirEnumerator::NativeString &path)
{
    if (AutomatedRun::active() && !AutomatedRun::mayModify(DirEnumerator::toDisplay(path))) {
        return Result::Denied;
    }
    struct stat info;
    if (lstat(path.c_str(), &info) != 0) {
        return fromErrno(errno);
    }
    // Un enlace de carpeta se borra como enlace (rmdir no lo acepta); lo que hay del otro lado no se toca.
    if (S_ISLNK(info.st_mode)) {
        return unlink(path.c_str()) == 0 ? Result::Removed : fromErrno(errno);
    }
    if (rmdir(path.c_str()) == 0) {
        return Result::Removed;
    }
    return fromErrno(errno);
}

namespace {

// Los archivos abiertos por los procesos del usuario, en una foto que dura 2 s: preguntar por cada
// archivo recorreria todos los procesos cada vez. Un proceso de otro usuario no se puede leer y no
// cuenta (la limpieza solo toca carpetas del usuario).
std::shared_ptr<const std::unordered_set<std::string>> openFilesSnapshot()
{
    static QMutex mutex;
    static std::shared_ptr<const std::unordered_set<std::string>> current;
    static QElapsedTimer age;
    QMutexLocker locker(&mutex);
    if (current && age.isValid() && age.elapsed() < 2000) {
        return current;
    }
    auto files = std::make_shared<std::unordered_set<std::string>>();
    std::vector<pid_t> pids(4096);
    int bytes = proc_listpids(PROC_ALL_PIDS, 0, pids.data(), int(pids.size() * sizeof(pid_t)));
    if (bytes > int(pids.size() * sizeof(pid_t)) - int(sizeof(pid_t))) {
        pids.resize(pids.size() * 4);
        bytes = proc_listpids(PROC_ALL_PIDS, 0, pids.data(), int(pids.size() * sizeof(pid_t)));
    }
    const int count = bytes > 0 ? bytes / int(sizeof(pid_t)) : 0;
    std::vector<proc_fdinfo> fds;
    for (int i = 0; i < count; ++i) {
        const pid_t pid = pids[size_t(i)];
        if (pid <= 0) {
            continue;
        }
        const int size = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, nullptr, 0);
        if (size <= 0) {
            continue;
        }
        fds.resize(size_t(size) / sizeof(proc_fdinfo) + 16);
        const int got = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, fds.data(), int(fds.size() * sizeof(proc_fdinfo)));
        for (int j = 0; j < got / int(sizeof(proc_fdinfo)); ++j) {
            if (fds[size_t(j)].proc_fdtype != PROX_FDTYPE_VNODE) {
                continue;
            }
            vnode_fdinfowithpath info;
            if (proc_pidfdinfo(pid, fds[size_t(j)].proc_fd, PROC_PIDFDVNODEPATHINFO, &info, sizeof(info)) == int(sizeof(info))
                && info.pvip.vip_path[0] != '\0') {
                files->insert(info.pvip.vip_path);
            }
        }
    }
    current = files;
    age.start();
    return current;
}

} // namespace

bool isInUse(const DirEnumerator::NativeString &path)
{
    // El sistema da las rutas reales (/private/var/..., no /var/...). Quien pregunta ya trae una ruta real
    // (la limpieza trabaja sobre rutas reales y no entra en enlaces): sin realpath por archivo, que era la
    // mitad del tiempo de revisar una carpeta grande.
    return openFilesSnapshot()->count(std::string(path)) > 0;
}

bool isCloudPlaceholder(const DirEnumerator::NativeString &path)
{
    // Un archivo o carpeta de nube que no esta bajado (iCloud, Google Drive, Dropbox: File Provider).
    struct stat info;
    return lstat(path.c_str(), &info) == 0 && (info.st_flags & SF_DATALESS) != 0;
}

QString canonicalPath(const QString &path)
{
    // realpath seguiria un enlace final: se resuelve la carpeta madre y se le pega el nombre del ultimo
    // tramo TAL COMO ESTA EN EL DISCO (APFS no distingue mayusculas: "library" y "Library" son lo mismo).
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
    QString name = info.fileName();
    attrlist request {};
    request.bitmapcount = ATTR_BIT_MAP_COUNT;
    request.commonattr = ATTR_CMN_NAME;
    struct
    {
        uint32_t length;
        attrreference_t name;
        char buffer[NAME_MAX * 3 + 1];
    } __attribute__((aligned(4), packed)) reply;
    const QByteArray full = (parent + name).toUtf8();
    if (getattrlist(full.constData(), &request, &reply, sizeof(reply), FSOPT_NOFOLLOW) == 0 && reply.name.attr_length > 1) {
        const char *onDisk = reinterpret_cast<const char *>(&reply.name) + reply.name.attr_dataoffset;
        name = QString::fromUtf8(onDisk, int(reply.name.attr_length) - 1);
    }
    return parent + name;
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
    if (AutomatedRun::active() && !AutomatedRun::mayModify(link)) {
        return false;
    }
    return symlink(QDir::cleanPath(target).toUtf8().constData(), nativePath(link).c_str()) == 0;
}

} // namespace FileSystemOps
