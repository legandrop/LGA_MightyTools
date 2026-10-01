#include "platform/SystemPaths.h"

#include "core/AppSettings.h"
#include "core/AutomatedRun.h"
#include "platform/FileSystemOps.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QStorageInfo>

#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>
#include <tlhelp32.h>

namespace {

QString known(REFKNOWNFOLDERID id)
{
    PWSTR raw = nullptr;
    QString path;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &raw)) && raw) {
        path = QString::fromWCharArray(raw);
    }
    if (raw) {
        CoTaskMemFree(raw);
    }
    return path;
}

// La ruta real si existe (nombres largos, sin subst); si no, la misma, limpia.
QString real(const QString &path)
{
    if (path.isEmpty()) {
        return QString();
    }
    const QString canonical = FileSystemOps::canonicalPath(path);
    return canonical.isEmpty() ? QDir::toNativeSeparators(QDir::cleanPath(path)) : canonical;
}

void add(QStringList &list, const QString &path)
{
    const QString value = real(path);
    if (!value.isEmpty() && !list.contains(value, Qt::CaseInsensitive)) {
        list.append(value);
    }
}

QString env(const char *name)
{
    return qEnvironmentVariable(name);
}

} // namespace

namespace SystemPaths {

bool cleanupSupported()
{
    return true;
}

CleanupBases cleanupBases()
{
    CleanupBases bases;
    bases.profile = real(known(FOLDERID_Profile));
    bases.localAppData = real(known(FOLDERID_LocalAppData));
    bases.roamingAppData = real(known(FOLDERID_RoamingAppData));
    // %TEMP% puede venir en formato corto (8.3): real() lo deja en su forma larga.
    bases.temp = real(QDir::tempPath());
    bases.systemRoot = real(known(FOLDERID_Windows));
    bases.uvCacheDir = real(env("UV_CACHE_DIR"));
    bases.pipCacheDir = real(env("PIP_CACHE_DIR"));
    bases.cargoHome = real(env("CARGO_HOME"));
    return bases;
}

QStringList protectedFolders()
{
    QStringList list;
    for (const QStorageInfo &storage : QStorageInfo::mountedVolumes()) {
        add(list, storage.rootPath());
    }
    for (REFKNOWNFOLDERID id : {FOLDERID_UserProfiles, FOLDERID_Profile, FOLDERID_Public, FOLDERID_Desktop, FOLDERID_Documents,
                                FOLDERID_Downloads, FOLDERID_Pictures, FOLDERID_Music, FOLDERID_Videos, FOLDERID_RoamingAppData,
                                FOLDERID_LocalAppData, FOLDERID_LocalAppDataLow}) {
        add(list, known(id));
    }
    // AppData: la carpeta madre de Roaming.
    const QString roaming = known(FOLDERID_RoamingAppData);
    if (!roaming.isEmpty()) {
        add(list, QFileInfo(roaming).absolutePath());
    }
    add(list, QDir::tempPath());
    return list;
}

QStringList protectedTrees()
{
    QStringList list;
    for (REFKNOWNFOLDERID id : {FOLDERID_Windows, FOLDERID_ProgramFiles, FOLDERID_ProgramFilesX86, FOLDERID_ProgramFilesX64,
                                FOLDERID_ProgramData}) {
        add(list, known(id));
    }
    // La carpeta desde la que corre la app y la de sus ajustes: tampoco sus archivos, uno por uno.
    add(list, QCoreApplication::applicationDirPath());
    add(list, QFileInfo(AppSettings::filePath()).absolutePath());
    return list;
}

QStringList cloudFolders()
{
    QStringList list;
    add(list, known(FOLDERID_SkyDrive));
    for (const char *name : {"OneDrive", "OneDriveConsumer", "OneDriveCommercial"}) {
        add(list, env(name));
    }
    return list;
}

QSet<QString> runningPrograms()
{
    QSet<QString> names;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return names;
    }
    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            QString name = QString::fromWCharArray(entry.szExeFile).toLower();
            if (name.endsWith(QLatin1String(".exe"))) {
                name.chop(4);
            }
            names.insert(name);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return names;
}

void revealInFileManager(const QString &path)
{
    if (AutomatedRun::active()) {
        qInfo().noquote() << QStringLiteral("[SystemPaths] (automatizada, sin abrir) explorador en %1").arg(path);
        return;
    }
    const QString native = QDir::toNativeSeparators(path);
    PIDLIST_ABSOLUTE item = ILCreateFromPathW(reinterpret_cast<LPCWSTR>(native.utf16()));
    if (!item) {
        return;
    }
    SHOpenFolderAndSelectItems(item, 0, nullptr, 0);
    ILFree(item);
}

void openSystemCleanup()
{
    if (AutomatedRun::active()) {
        qInfo() << "[SystemPaths] (automatizada, sin abrir) limpieza de Windows";
        return;
    }
    ShellExecuteW(nullptr, L"open", L"ms-settings:storagesense", nullptr, nullptr, SW_SHOWNORMAL);
}

} // namespace SystemPaths
