#include "platform/SystemPaths.h"

#include "core/AppSettings.h"
#include "core/AutomatedRun.h"
#include "platform/FileSystemOps.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QRegularExpression>
#include <QStorageInfo>

#include <CoreFoundation/CoreFoundation.h>
#include <fcntl.h>
#include <libproc.h>
#include <sys/xattr.h>
#include <unistd.h>

#include <vector>

namespace {

// La ruta real si existe; si no, la misma, limpia.
QString real(const QString &path)
{
    if (path.isEmpty()) {
        return QString();
    }
    const QString canonical = FileSystemOps::canonicalPath(path);
    return canonical.isEmpty() ? QDir::cleanPath(path) : canonical;
}

void add(QStringList &list, const QString &path)
{
    const QString value = real(path);
    if (!value.isEmpty() && !list.contains(value, Qt::CaseInsensitive)) {
        list.append(value);
    }
}

QString home(const char *relative)
{
    return QDir::homePath() + QLatin1Char('/') + QLatin1String(relative);
}

// El bundle .app desde el que corre la app (applicationDirPath es Contents/MacOS adentro de el).
QString appBundle()
{
    QDir dir(QCoreApplication::applicationDirPath());
    if (dir.dirName() == QLatin1String("MacOS") && dir.cdUp() && dir.dirName() == QLatin1String("Contents") && dir.cdUp()
        && dir.path().endsWith(QLatin1String(".app"))) {
        return dir.path();
    }
    return QCoreApplication::applicationDirPath();
}

// El identificador (CFBundleIdentifier) de la app que contiene ese ejecutable, en minusculas; vacio si
// no esta dentro de un .app. Cacheado por ruta: las apps no cambian de identificador.
QString bundleIdOf(const QString &executable)
{
    const int at = executable.lastIndexOf(QLatin1String(".app/Contents/"));
    if (at < 0) {
        return QString();
    }
    const QString bundle = executable.left(at + 4);
    static QMutex mutex;
    static QHash<QString, QString> cache;
    QMutexLocker locker(&mutex);
    const auto found = cache.constFind(bundle);
    if (found != cache.constEnd()) {
        return found.value();
    }
    QString id;
    const QByteArray path = QFile::encodeName(bundle);
    if (CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8 *>(path.constData()),
                                                               CFIndex(path.size()), true)) {
        if (CFBundleRef ref = CFBundleCreate(nullptr, url)) {
            if (CFStringRef identifier = CFBundleGetIdentifier(ref)) {
                id = QString::fromCFString(identifier).toLower();
            }
            CFRelease(ref);
        }
        CFRelease(url);
    }
    cache.insert(bundle, id);
    return id;
}

// La carpeta esta sincronizada por un proveedor de nube (File Provider): el Escritorio y Documentos con
// iCloud, por ejemplo, llevan esta marca.
bool hasFileProviderMark(const QString &path)
{
    return getxattr(QFile::encodeName(path).constData(), "com.apple.file-provider-domain-id", nullptr, 0, 0, XATTR_NOFOLLOW) >= 0;
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
    bases.profile = real(QDir::homePath());
    bases.library = real(home("Library"));
    bases.caches = real(home("Library/Caches"));
    bases.appSupport = real(home("Library/Application Support"));
    // $TMPDIR es /var/folders/.../T: real() lo deja en /private/var/folders/.../T.
    bases.temp = real(QDir::tempPath());
    bases.uvCacheDir = real(qEnvironmentVariable("UV_CACHE_DIR"));
    bases.pipCacheDir = real(qEnvironmentVariable("PIP_CACHE_DIR"));
    bases.cargoHome = real(qEnvironmentVariable("CARGO_HOME"));
    return bases;
}

QStringList protectedFolders()
{
    QStringList list;
    for (const char *path : {"/", "/Users", "/Users/Shared", "/Volumes", "/private/tmp", "/private/var"}) {
        add(list, QLatin1String(path));
    }
    for (const QStorageInfo &storage : QStorageInfo::mountedVolumes()) {
        add(list, storage.rootPath());
    }
    add(list, QDir::homePath());
    for (const char *name : {"Desktop", "Documents", "Downloads", "Pictures", "Music", "Movies", "Public", "Library",
                             "Library/Application Support", "Library/Caches", "Library/Containers", "Library/Group Containers",
                             "Library/Logs", "Library/Preferences"}) {
        add(list, home(name));
    }
    add(list, QDir::tempPath());
    return list;
}

QStringList protectedTrees()
{
    QStringList list;
    for (const char *path : {"/System", "/Library", "/Applications", "/usr", "/bin", "/sbin", "/opt", "/private/etc",
                             "/private/var/db", "/private/var/root", "/private/var/vm", "/cores"}) {
        add(list, QLatin1String(path));
    }
    // Datos del usuario que viven en ~/Library: correo, mensajes, notas, calendarios, contactos, Safari,
    // fotos, cuentas. Desde la ventana se puede borrar definitivo: no se ofrece nada de esto.
    for (const char *name : {"Applications", "Library/Keychains", "Library/Mail", "Library/Messages", "Library/Mobile Documents",
                             "Library/Group Containers/group.com.apple.notes", "Library/Calendars",
                             "Library/Application Support/AddressBook", "Library/Safari", "Library/Photos", "Library/Accounts",
                             "Pictures/Photos Library.photoslibrary", "Music/Music", "Movies/TV"}) {
        add(list, home(name));
    }
    // Las hermanas de la carpeta temporal del usuario (/private/var/folders/xx/yyy/C y /0): caches del sistema.
    const QString temp = real(QDir::tempPath());
    for (const char *sibling : {"C", "0"}) {
        add(list, QFileInfo(temp).absolutePath() + QLatin1Char('/') + QLatin1String(sibling));
    }
    // La app y sus ajustes: tampoco sus archivos, uno por uno.
    add(list, appBundle());
    add(list, QFileInfo(AppSettings::filePath()).absolutePath());
    return list;
}

QStringList cloudFolders()
{
    QStringList list;
    add(list, home("Library/Mobile Documents")); // iCloud Drive
    // Google Drive, Dropbox, OneDrive y los demas de File Provider: toda la carpeta (una subcarpeta por
    // cuenta). Sin listarla: sin acceso total al disco, listarla mostraria un cartel (D-43).
    add(list, home("Library/CloudStorage"));
    // Las carpetas de sincronizacion de antes de File Provider, en la carpeta del usuario.
    const QDir homeDir(QDir::homePath());
    for (const QString &name : homeDir.entryList({QStringLiteral("Dropbox*"), QStringLiteral("OneDrive*"),
                                                  QStringLiteral("Google Drive*"), QStringLiteral("Creative Cloud Files*")},
                                                 QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
        add(list, homeDir.filePath(name));
    }
    // Escritorio y Documentos sincronizados con iCloud. Sin acceso total al disco no se pregunta (seria tocar
    // carpetas privadas); igual el escaneo no entra y no se pueden elegir (DeleteGuard::forVolume).
    if (hasFullDiskAccess()) {
        for (const char *name : {"Desktop", "Documents"}) {
            if (hasFileProviderMark(home(name))) {
                add(list, home(name));
            }
        }
    }
    return list;
}

QSet<QString> runningPrograms()
{
    QSet<QString> names;
    std::vector<pid_t> pids(4096);
    int bytes = proc_listpids(PROC_ALL_PIDS, 0, pids.data(), int(pids.size() * sizeof(pid_t)));
    if (bytes > int(pids.size() * sizeof(pid_t)) - int(sizeof(pid_t))) {
        pids.resize(pids.size() * 4);
        bytes = proc_listpids(PROC_ALL_PIDS, 0, pids.data(), int(pids.size() * sizeof(pid_t)));
    }
    static const QRegularExpression year(QStringLiteral(" (19|20)\\d\\d$"));
    char path[PROC_PIDPATHINFO_MAXSIZE];
    for (int i = 0; i < (bytes > 0 ? bytes / int(sizeof(pid_t)) : 0); ++i) {
        if (pids[size_t(i)] <= 0 || proc_pidpath(pids[size_t(i)], path, sizeof(path)) <= 0) {
            continue;
        }
        // El ejecutable ("Google Chrome", "Adobe Premiere Pro 2025"), sin el ano de la version tambien.
        const QString executable = QString::fromUtf8(path);
        const QString name = QFileInfo(executable).fileName().toLower();
        names.insert(name);
        QString plain = name;
        plain.remove(year);
        names.insert(plain);
        // Y el identificador de su app ("com.google.chrome"), con sus prefijos de 3 tramos o mas:
        // "com.adobe.premierepro.25" tambien es "com.adobe.premierepro". Un ayudante (Helper, Renderer)
        // vive dentro del .app de su app y da el identificador de la app de afuera... o el suyo propio,
        // que empieza con el de ella.
        const QString id = bundleIdOf(executable);
        if (!id.isEmpty()) {
            QStringList parts = id.split(QLatin1Char('.'));
            while (parts.size() >= 3) {
                names.insert(parts.join(QLatin1Char('.')));
                parts.removeLast();
            }
        }
    }
    return names;
}

QStringList scanExclusions()
{
    if (hasFullDiskAccess()) {
        return {};
    }
    // Lo que protege la privacidad de macOS (TCC) dentro de la carpeta del usuario: sin acceso total al
    // disco, entrar muestra un cartel ("quiere acceder a tu carpeta Escritorio") o falla.
    QStringList list;
    for (const char *name : {"Desktop", "Documents", "Downloads", "Pictures/Photos Library.photoslibrary", "Library/Photos", "Movies/TV",
                             "Music/Music", "Library/Mobile Documents", "Library/CloudStorage", "Library/Containers",
                             "Library/Group Containers", "Library/Mail", "Library/Messages", "Library/Safari",
                             "Library/Calendars", "Library/Reminders", "Library/HomeKit", "Library/Suggestions",
                             "Library/Cookies", "Library/IdentityServices", "Library/Metadata/CoreSpotlight",
                             "Library/Application Support/AddressBook", "Library/Application Support/CallHistoryDB",
                             "Library/Application Support/MobileSync", "Library/Application Support/com.apple.TCC",
                             "Library/Application Support/Knowledge", "Library/Application Support/FaceTime",
                             "Library/Accounts", "Library/Autosave Information", "Library/Biome", "Library/DoNotDisturb",
                             "Library/Daemon Containers", "Library/PersonalizationPortrait", "Library/Sharing", ".Trash"}) {
        list.append(QDir::cleanPath(home(name)));
    }
    return list;
}

bool hasFullDiskAccess()
{
    // La base de permisos del usuario solo se puede abrir con acceso total al disco; sin el, open falla
    // con EPERM sin mostrar ningun cartel.
    const QByteArray path = home("Library/Application Support/com.apple.TCC/TCC.db").toUtf8();
    const int fd = open(path.constData(), O_RDONLY);
    if (fd < 0) {
        return false;
    }
    close(fd);
    return true;
}

void openFullDiskAccessSettings()
{
    if (AutomatedRun::active()) {
        qInfo() << "[SystemPaths] (automatizada, sin abrir) ajustes de acceso total al disco";
        return;
    }
    QProcess::startDetached(QStringLiteral("/usr/bin/open"),
                            {QStringLiteral("x-apple.systempreferences:com.apple.preference.security?Privacy_AllFiles")});
}

void revealInFileManager(const QString &path)
{
    if (AutomatedRun::active()) {
        qInfo().noquote() << QStringLiteral("[SystemPaths] (automatizada, sin abrir) Finder en %1").arg(path);
        return;
    }
    QProcess::startDetached(QStringLiteral("/usr/bin/open"), {QStringLiteral("-R"), path});
}

void openSystemCleanup()
{
    if (AutomatedRun::active()) {
        qInfo() << "[SystemPaths] (automatizada, sin abrir) Almacenamiento";
        return;
    }
    QProcess::startDetached(QStringLiteral("/usr/bin/open"), {QStringLiteral("x-apple.systempreferences:com.apple.settings.Storage")});
}

} // namespace SystemPaths
