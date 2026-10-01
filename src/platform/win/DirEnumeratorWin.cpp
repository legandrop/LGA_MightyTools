#include "platform/DirEnumerator.h"

#include <QDir>

#include <windows.h>

namespace {

constexpr size_t kBufferBytes = 256 * 1024;
const wchar_t kLongPrefix[] = L"\\\\?\\";

// FILETIME (intervalos de 100 ns desde 1601) a segundos desde 1970.
qint64 fileTimeToSecs(const LARGE_INTEGER &time)
{
    constexpr qint64 kEpochDelta = 116444736000000000LL;
    return time.QuadPart <= kEpochDelta ? 0 : (time.QuadPart - kEpochDelta) / 10000000LL;
}

} // namespace

namespace DirEnumerator {

NativeChar separator()
{
    return L'\\';
}

NativeString nativeDir(const QString &path)
{
    QString native = QDir::toNativeSeparators(QDir::cleanPath(path));
    // \\?\ : sin limite de 260 caracteres y sin que Windows reinterprete nombres con punto o espacio final.
    NativeString result = kLongPrefix;
    result += native.toStdWString();
    if (result.back() != L'\\') {
        result += L'\\';
    }
    return result;
}

QString toDisplay(const NativeString &path)
{
    QString text = QString::fromStdWString(path);
    if (text.startsWith(QLatin1String("\\\\?\\"))) {
        text.remove(0, 4);
    }
    // "C:\" queda con su barra; el resto, sin la final.
    while (text.size() > 3 && text.endsWith(QLatin1Char('\\'))) {
        text.chop(1);
    }
    return text;
}

QString nameToString(const NativeChar *name, int length)
{
    return QString::fromWCharArray(name, length);
}

NativeString nameFromString(const QString &name)
{
    return name.toStdWString();
}

int appendUtf16(std::u16string &out, const NativeChar *name, int length)
{
    // wchar_t de Windows ya es UTF-16.
    out.append(reinterpret_cast<const char16_t *>(name), size_t(length));
    return length;
}

bool enumerate(const NativeString &dir, std::vector<char> &scratch, const Visitor &visit)
{
    if (scratch.size() < kBufferBytes) {
        scratch.resize(kBufferBytes);
    }
    // FILE_FLAG_BACKUP_SEMANTICS es lo que permite abrir una carpeta; no pide ningun privilegio.
    const HANDLE handle = CreateFileW(dir.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    // FileFullDirectoryInfo (14): todo lo que hace falta sale del indice del directorio.
    while (GetFileInformationByHandleEx(handle, static_cast<FILE_INFO_BY_HANDLE_CLASS>(14), scratch.data(),
                                        static_cast<DWORD>(scratch.size()))) {
        const auto *info = reinterpret_cast<const FILE_FULL_DIR_INFO *>(scratch.data());
        for (;;) {
            const int length = int(info->FileNameLength / sizeof(wchar_t));
            const bool dot = info->FileName[0] == L'.' && (length == 1 || (length == 2 && info->FileName[1] == L'.'));
            if (!dot) {
                Entry entry;
                entry.name = info->FileName;
                entry.nameLength = length;
                entry.isDir = (info->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                // Con el atributo de punto de reanalisis, EaSize trae la etiqueta. Los enlaces de nombre
                // (junction, symlink y los demas "name surrogate", bit 0x20000000) apuntan a otro lado
                // y se saltean; una carpeta de OneDrive o de una app empaquetada tambien es un punto de
                // reanalisis, pero su contenido es de este volumen.
                if (info->FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                    entry.isLink = (info->EaSize & 0x20000000) != 0;
                }
                // OFFLINE (0x1000), RECALL_ON_OPEN (0x40000), RECALL_ON_DATA_ACCESS (0x400000): no esta
                // bajado de la nube.
                entry.isCloud = (info->FileAttributes & (0x00001000 | 0x00040000 | 0x00400000)) != 0;
                entry.size = quint64(info->EndOfFile.QuadPart);
                entry.allocated = quint64(info->AllocationSize.QuadPart);
                entry.modifiedSecs = fileTimeToSecs(info->LastWriteTime);
                entry.createdSecs = fileTimeToSecs(info->CreationTime);
                visit(entry);
            }
            if (info->NextEntryOffset == 0) {
                break;
            }
            info = reinterpret_cast<const FILE_FULL_DIR_INFO *>(reinterpret_cast<const char *>(info) + info->NextEntryOffset);
        }
    }
    CloseHandle(handle);
    return true;
}

} // namespace DirEnumerator
