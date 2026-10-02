#include "platform/DirEnumerator.h"

#include <QDir>

#include <dirent.h>
#include <fcntl.h>
#include <sys/attr.h>
#include <sys/stat.h>
#include <sys/vnode.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>

namespace DirEnumerator {

NativeChar separator()
{
    return '/';
}

NativeString nativeDir(const QString &path)
{
    NativeString result = QDir::cleanPath(path).toUtf8().toStdString();
    if (result.empty() || result.back() != '/') {
        result += '/';
    }
    return result;
}

QString toDisplay(const NativeString &path)
{
    QString text = QString::fromUtf8(path.c_str(), qsizetype(path.size()));
    while (text.size() > 1 && text.endsWith(QLatin1Char('/'))) {
        text.chop(1);
    }
    return text;
}

QString nameToString(const NativeChar *name, int length)
{
    return QString::fromUtf8(name, length);
}

NativeString nameFromString(const QString &name)
{
    return name.toUtf8().toStdString();
}

int appendUtf16(std::u16string &out, const NativeChar *name, int length)
{
    const QString text = QString::fromUtf8(name, length);
    out.append(reinterpret_cast<const char16_t *>(text.utf16()), size_t(text.size()));
    return int(text.size());
}

namespace {

// Entradas de la raiz "/" que son otra vista del mismo arbol: contarlas lo sumaria dos veces.
bool isRootAlias(const NativeString &dir, const char *name)
{
    if (dir != "/") {
        return false;
    }
    return std::strcmp(name, ".nofollow") == 0 || std::strcmp(name, ".resolve") == 0 || std::strcmp(name, ".vol") == 0;
}

// Respaldo para un sistema de archivos que no acepta getattrlistbulk: readdir + fstatat.
bool enumerateSlow(int fd, const NativeString &dir, const Visitor &visit)
{
    DIR *handle = fdopendir(fd);
    if (!handle) {
        close(fd);
        return false;
    }
    struct stat self;
    const bool haveSelf = fstat(fd, &self) == 0;
    while (const dirent *item = readdir(handle)) {
        const char *name = item->d_name;
        if ((name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) || isRootAlias(dir, name)) {
            continue;
        }
        struct stat info;
        if (fstatat(fd, name, &info, AT_SYMLINK_NOFOLLOW) != 0 || S_ISSOCK(info.st_mode) || S_ISFIFO(info.st_mode)
            || S_ISCHR(info.st_mode) || S_ISBLK(info.st_mode)) {
            continue;
        }
        Entry entry;
        entry.name = name;
        entry.nameLength = int(std::strlen(name));
        entry.isDir = S_ISDIR(info.st_mode);
        // Un enlace simbolico, u otro volumen montado adentro de esta carpeta.
        entry.isLink = S_ISLNK(info.st_mode) || (entry.isDir && haveSelf && info.st_dev != self.st_dev);
        entry.isCloud = (info.st_flags & SF_DATALESS) != 0;
        entry.size = quint64(info.st_size);
        entry.allocated = quint64(info.st_blocks) * 512;
        entry.modifiedSecs = qint64(info.st_mtime);
        entry.createdSecs = qint64(info.st_birthtime);
        visit(entry);
    }
    closedir(handle); // cierra tambien fd
    return true;
}

template <typename T>
T take(const char *&field)
{
    T value;
    std::memcpy(&value, field, sizeof(T)); // los atributos vienen alineados a 4: memcpy, no puntero
    field += sizeof(T);
    return value;
}

} // namespace

bool enumerate(const NativeString &dir, std::vector<char> &scratch, const Visitor &visit)
{
    // getattrlistbulk trae nombre, tipo, fechas y tamanos de muchas entradas por llamada: es el
    // equivalente de FileFullDirectoryInfo de Windows. Medido en una Mac con 4,2 M de archivos: el disco
    // entero en ~19 s con 8 hilos, contra 3,5 min con readdir + fstatat (una llamada por archivo).
    // Listar solo los nombres ya lleva ~40 % de ese tiempo: el resto es APFS leyendo cada inodo.
    constexpr size_t kBufferBytes = 256 * 1024;
    if (scratch.size() < kBufferBytes) {
        scratch.resize(kBufferBytes);
    }
    // O_NOFOLLOW: si la carpeta misma es un enlace, no se lista lo que hay del otro lado.
    const int fd = open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        return false;
    }
    attrlist request {};
    request.bitmapcount = ATTR_BIT_MAP_COUNT;
    request.commonattr = ATTR_CMN_RETURNED_ATTRS | ATTR_CMN_NAME | ATTR_CMN_ERROR | ATTR_CMN_OBJTYPE | ATTR_CMN_CRTIME
                         | ATTR_CMN_MODTIME | ATTR_CMN_FLAGS;
    request.dirattr = ATTR_DIR_MOUNTSTATUS;
    request.fileattr = ATTR_FILE_TOTALSIZE | ATTR_FILE_ALLOCSIZE;

    bool first = true;
    for (;;) {
        const int count = getattrlistbulk(fd, &request, scratch.data(), scratch.size(), 0);
        if (count < 0) {
            if (first && (errno == ENOTSUP || errno == EINVAL)) {
                return enumerateSlow(fd, dir, visit);
            }
            break; // la carpeta se fue a mitad de camino: queda lo leido
        }
        if (count == 0) {
            break;
        }
        first = false;
        const char *record = scratch.data();
        for (int i = 0; i < count; ++i) {
            const char *field = record;
            const uint32_t length = take<uint32_t>(field);
            const attribute_set_t returned = take<attribute_set_t>(field);
            record += length;
            // Los atributos vienen en el orden de los bits, y solo los que el sistema devolvio.
            if (returned.commonattr & ATTR_CMN_ERROR) {
                take<uint32_t>(field);
            }
            if (!(returned.commonattr & ATTR_CMN_NAME)) {
                continue;
            }
            const char *nameField = field;
            const attrreference_t nameRef = take<attrreference_t>(field);
            const char *name = nameField + nameRef.attr_dataoffset;
            const int nameLength = nameRef.attr_length > 0 ? int(nameRef.attr_length) - 1 : 0; // sin el '\0'
            const uint32_t type = (returned.commonattr & ATTR_CMN_OBJTYPE) ? take<uint32_t>(field) : VNON;
            const timespec created = (returned.commonattr & ATTR_CMN_CRTIME) ? take<timespec>(field) : timespec {};
            const timespec modified = (returned.commonattr & ATTR_CMN_MODTIME) ? take<timespec>(field) : timespec {};
            const uint32_t flags = (returned.commonattr & ATTR_CMN_FLAGS) ? take<uint32_t>(field) : 0;
            const uint32_t mountStatus = (returned.dirattr & ATTR_DIR_MOUNTSTATUS) ? take<uint32_t>(field) : 0;
            const off_t totalSize = (returned.fileattr & ATTR_FILE_TOTALSIZE) ? take<off_t>(field) : 0;
            const off_t allocSize = (returned.fileattr & ATTR_FILE_ALLOCSIZE) ? take<off_t>(field) : 0;
            // Sockets, FIFOs y dispositivos no ocupan lugar y no son de nadie para borrar (un socket de
            // una app abierta vive en la carpeta temporal).
            if (nameLength <= 0 || isRootAlias(dir, name) || type == VSOCK || type == VFIFO || type == VCHR || type == VBLK) {
                continue;
            }

            Entry entry;
            entry.name = name;
            entry.nameLength = nameLength;
            entry.isDir = type == VDIR;
            // Un enlace simbolico, u otro volumen montado aca (/System/Volumes/Data, un disco en
            // /Volumes): lo de adentro no es de esta carpeta. Las carpetas "firmlink" de la raiz (/Users,
            // /Applications...) no son puntos de montaje: por ahi se entra al volumen de datos una sola vez.
            entry.isLink = type == VLNK || (entry.isDir && (mountStatus & DIR_MNTSTATUS_MNTPOINT));
            // Archivo o carpeta de nube sin bajar (iCloud, Google Drive, Dropbox): listarla la traeria de la red.
            entry.isCloud = (flags & SF_DATALESS) != 0;
            entry.size = quint64(qMax<off_t>(totalSize, 0));
            entry.allocated = quint64(qMax<off_t>(allocSize, 0));
            entry.modifiedSecs = qint64(modified.tv_sec);
            entry.createdSecs = qint64(created.tv_sec);
            visit(entry);
        }
    }
    close(fd);
    return true;
}

} // namespace DirEnumerator
