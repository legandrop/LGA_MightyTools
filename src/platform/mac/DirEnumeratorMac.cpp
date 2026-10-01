#include "platform/DirEnumerator.h"

#include <QDir>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

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

bool enumerate(const NativeString &dir, std::vector<char> &scratch, const Visitor &visit)
{
    Q_UNUSED(scratch);
    DIR *handle = opendir(dir.c_str());
    if (!handle) {
        return false;
    }
    const int fd = dirfd(handle);
    struct stat self;
    const bool haveSelf = fstat(fd, &self) == 0;
    while (const dirent *item = readdir(handle)) {
        const char *name = item->d_name;
        if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) {
            continue;
        }
        struct stat info;
        if (fstatat(fd, name, &info, AT_SYMLINK_NOFOLLOW) != 0) {
            continue;
        }
        Entry entry;
        entry.name = name;
        entry.nameLength = int(std::strlen(name));
        entry.isDir = S_ISDIR(info.st_mode);
        // Un enlace simbolico, u otro volumen montado adentro de esta carpeta.
        entry.isLink = S_ISLNK(info.st_mode) || (entry.isDir && haveSelf && info.st_dev != self.st_dev);
        entry.size = quint64(info.st_size);
        entry.allocated = quint64(info.st_blocks) * 512;
        entry.modifiedSecs = qint64(info.st_mtime);
        entry.createdSecs = qint64(info.st_birthtime);
        visit(entry);
    }
    closedir(handle);
    return true;
}

} // namespace DirEnumerator
