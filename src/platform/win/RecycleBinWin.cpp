#include "platform/RecycleBin.h"

#include "core/AutomatedRun.h"

#include <QDebug>
#include <QDir>
#include <QFile>

#include <windows.h>
#include <shellapi.h>

namespace {

std::wstring rootOf(const QString &volumeRoot)
{
    QString native = QDir::toNativeSeparators(volumeRoot);
    if (!native.endsWith(QLatin1Char('\\'))) {
        native += QLatin1Char('\\');
    }
    return native.toStdWString();
}

} // namespace

namespace RecycleBin {

Info query(const QString &volumeRoot)
{
    Info info;
    SHQUERYRBINFO data;
    data.cbSize = sizeof(data);
    data.i64Size = 0;
    data.i64NumItems = 0;
    if (SUCCEEDED(SHQueryRecycleBinW(rootOf(volumeRoot).c_str(), &data))) {
        info.ok = true;
        info.bytes = qint64(data.i64Size);
        info.items = qint64(data.i64NumItems);
    }
    return info;
}

bool empty(const QString &volumeRoot)
{
    if (AutomatedRun::active()) {
        qInfo().noquote() << QStringLiteral("[RecycleBin] (automatizada, sin vaciar) %1").arg(QDir::toNativeSeparators(volumeRoot));
        return false;
    }
    const HRESULT result = SHEmptyRecycleBinW(nullptr, rootOf(volumeRoot).c_str(),
                                              SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
    // Con la Papelera ya vacia devuelve un error: lo que importa es como quedo.
    const Info after = query(volumeRoot);
    return SUCCEEDED(result) || (after.ok && after.items == 0);
}

bool moveToTrash(const QString &path, QString *error)
{
    if (AutomatedRun::active()) {
        qInfo().noquote() << QStringLiteral("[RecycleBin] (automatizada, sin mover) %1").arg(QDir::toNativeSeparators(path));
        if (error) {
            *error = QStringLiteral("automated run");
        }
        return false;
    }
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
