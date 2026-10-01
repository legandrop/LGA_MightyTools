#include "platform/FileSystemOps.h"

#include "core/AutomatedRun.h"

#include <QDir>

#include <windows.h>
#include <winioctl.h>

#include <cstring>
#include <vector>

namespace {

// Etiqueta de reanalisis "sustituto de nombre": junction, symlink y los demas enlaces que apuntan a
// otro lugar. Es el bit que mira IsReparseTagNameSurrogate.
bool isNameSurrogate(DWORD tag)
{
    return (tag & 0x20000000) != 0;
}

FileSystemOps::Result fromError(DWORD error)
{
    switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
        return FileSystemOps::Result::Missing;
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
        return FileSystemOps::Result::InUse;
    case ERROR_ACCESS_DENIED:
        return FileSystemOps::Result::Denied;
    case ERROR_DIR_NOT_EMPTY:
        return FileSystemOps::Result::NotEmpty;
    default:
        return FileSystemOps::Result::Failed;
    }
}

// Abre sin pedir ningun acceso (alcanza para preguntar por la ruta y la identidad) y sin seguir un
// enlace final.
HANDLE openForInfo(const QString &path)
{
    const DirEnumerator::NativeString native = FileSystemOps::nativePath(path);
    return CreateFileW(native.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                       FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
}

} // namespace

namespace FileSystemOps {

DirEnumerator::NativeString nativePath(const QString &path)
{
    DirEnumerator::NativeString native = DirEnumerator::nativeDir(path);
    // nativeDir termina en barra; la raiz de una unidad ("\\?\C:\") la conserva.
    if (native.size() > 7 && native.back() == L'\\') {
        native.pop_back();
    }
    return native;
}

Kind kind(const QString &path)
{
    const DirEnumerator::NativeString native = nativePath(path);
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(native.c_str(), GetFileExInfoStandard, &data)) {
        return Kind::Missing;
    }
    if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        // La etiqueta solo la da la busqueda por nombre.
        WIN32_FIND_DATAW find;
        const HANDLE handle = FindFirstFileExW(native.c_str(), FindExInfoBasic, &find, FindExSearchNameMatch, nullptr, 0);
        if (handle != INVALID_HANDLE_VALUE) {
            FindClose(handle);
            if (isNameSurrogate(find.dwReserved0)) {
                return Kind::Link;
            }
        }
    }
    return (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? Kind::Dir : Kind::File;
}

bool isInUse(const DirEnumerator::NativeString &path)
{
    // Pedir el borrado SIN compartir: si alguien lo tiene abierto, Windows lo rechaza. Cerrar el handle
    // no borra nada (no se marca para borrar).
    const HANDLE handle = CreateFileW(path.c_str(), DELETE, 0, nullptr, OPEN_EXISTING,
                                      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        return error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND;
    }
    CloseHandle(handle);
    return false;
}

bool isCloudPlaceholder(const DirEnumerator::NativeString &path)
{
    // Los mismos atributos que mira el listado de carpetas: sin conexion, o que se baja al abrirlo.
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & (0x00001000 | 0x00040000 | 0x00400000)) != 0;
}

Result removeFile(const DirEnumerator::NativeString &path)
{
    if (AutomatedRun::active() && !AutomatedRun::mayModify(DirEnumerator::toDisplay(path))) {
        return Result::Denied;
    }
    if (DeleteFileW(path.c_str())) {
        return Result::Removed;
    }
    DWORD error = GetLastError();
    if (error == ERROR_ACCESS_DENIED) {
        // Un archivo de solo lectura se rechaza con "acceso denegado": se le saca el atributo y se reintenta.
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY)
            && SetFileAttributesW(path.c_str(), attributes & ~DWORD(FILE_ATTRIBUTE_READONLY))) {
            if (DeleteFileW(path.c_str())) {
                return Result::Removed;
            }
            error = GetLastError();
        }
    }
    return fromError(error);
}

Result removeDir(const DirEnumerator::NativeString &path)
{
    if (AutomatedRun::active() && !AutomatedRun::mayModify(DirEnumerator::toDisplay(path))) {
        return Result::Denied;
    }
    // Sobre un junction o un symlink de carpeta, RemoveDirectoryW borra el enlace y deja el destino.
    if (RemoveDirectoryW(path.c_str())) {
        return Result::Removed;
    }
    DWORD error = GetLastError();
    if (error == ERROR_ACCESS_DENIED) {
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY)
            && SetFileAttributesW(path.c_str(), attributes & ~DWORD(FILE_ATTRIBUTE_READONLY))) {
            if (RemoveDirectoryW(path.c_str())) {
                return Result::Removed;
            }
            error = GetLastError();
        }
    }
    return fromError(error);
}

QString canonicalPath(const QString &path)
{
    const HANDLE handle = openForInfo(path);
    if (handle == INVALID_HANDLE_VALUE) {
        return QString();
    }
    std::wstring buffer(1024, L'\0');
    DWORD length = GetFinalPathNameByHandleW(handle, buffer.data(), DWORD(buffer.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (length >= buffer.size()) {
        buffer.resize(length + 1);
        length = GetFinalPathNameByHandleW(handle, buffer.data(), DWORD(buffer.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    }
    CloseHandle(handle);
    if (length == 0 || length >= buffer.size()) {
        return QString();
    }
    QString result = QString::fromWCharArray(buffer.data(), int(length));
    if (result.startsWith(QLatin1String("\\\\?\\UNC\\"))) {
        return QString(); // una ruta de red: la limpieza solo trabaja en discos locales
    }
    if (result.startsWith(QLatin1String("\\\\?\\"))) {
        result.remove(0, 4);
    }
    return result;
}

Identity identity(const QString &path)
{
    Identity id;
    const HANDLE handle = openForInfo(path);
    if (handle == INVALID_HANDLE_VALUE) {
        return id;
    }
    BY_HANDLE_FILE_INFORMATION info;
    if (GetFileInformationByHandle(handle, &info)) {
        id.volume = info.dwVolumeSerialNumber;
        id.file = (quint64(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
        id.valid = true;
    }
    CloseHandle(handle);
    return id;
}

bool createDirLink(const QString &link, const QString &target)
{
    if (AutomatedRun::active() && !AutomatedRun::mayModify(link)) {
        return false;
    }
    const DirEnumerator::NativeString nativeLink = nativePath(link);
    if (!CreateDirectoryW(nativeLink.c_str(), nullptr)) {
        return false;
    }
    const HANDLE handle = CreateFileW(nativeLink.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        RemoveDirectoryW(nativeLink.c_str());
        return false;
    }
    // Buffer de un punto de montaje: cabecera de 8 bytes, cuatro USHORT y los dos nombres. El destino va
    // en la forma del nucleo ("\??\C:\carpeta"); el nombre para mostrar queda vacio.
    const std::wstring substitute = L"\\??\\" + QDir::toNativeSeparators(QDir::cleanPath(target)).toStdWString();
    const USHORT nameBytes = USHORT(substitute.size() * sizeof(wchar_t));
    const USHORT dataLength = USHORT(8 + nameBytes + sizeof(wchar_t) + sizeof(wchar_t));
    std::vector<char> buffer(8 + dataLength, 0);
    const ULONG tag = IO_REPARSE_TAG_MOUNT_POINT;
    std::memcpy(buffer.data(), &tag, 4);
    std::memcpy(buffer.data() + 4, &dataLength, 2);
    const USHORT zero = 0;
    const USHORT printOffset = USHORT(nameBytes + sizeof(wchar_t));
    std::memcpy(buffer.data() + 8, &zero, 2);         // SubstituteNameOffset
    std::memcpy(buffer.data() + 10, &nameBytes, 2);   // SubstituteNameLength
    std::memcpy(buffer.data() + 12, &printOffset, 2); // PrintNameOffset
    std::memcpy(buffer.data() + 14, &zero, 2);        // PrintNameLength
    std::memcpy(buffer.data() + 16, substitute.data(), nameBytes);
    DWORD returned = 0;
    const bool ok = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, buffer.data(), DWORD(buffer.size()), nullptr, 0, &returned, nullptr);
    CloseHandle(handle);
    if (!ok) {
        RemoveDirectoryW(nativeLink.c_str());
    }
    return ok;
}

} // namespace FileSystemOps
