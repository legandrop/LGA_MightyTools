#include "modules/diskspace/cleanup/AppCacheRules.h"

#include "core/I18n.h"
#include "modules/diskspace/cleanup/DeleteGuard.h"
#include "modules/diskspace/cleanup/ScanTree.h"
#include "platform/AppPreferences.h"
#include "platform/FileSystemOps.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QStringDecoder>
#include <QtEndian>

#ifdef Q_OS_MACOS
#include <unistd.h>
#endif

// Fuentes de cada regla (informe del 2026-11-03, +Building_Blocks/trabajo/caches_2026-11-03, y lo leido
// en una PC con Nuke 13 a 17, Ableton Live 11 y Topaz Video):
//  - Nuke / NukeX / Studio / Hiero: todos los `preferences*.nk` de `.nuke` (bajo HOME en Windows si esta
//    definida), claves `DiskCachePath` y `TimelineDiskCachePath`; sin la linea valen
//    `[getenv NUKE_TEMP_DIR]` y `[getenv NUKE_TEMP_DIR]/TimelineCache`. `FC_DISK_CACHE` y
//    `NUKE_DISK_CACHE` tambien cuentan. Dentro: `ViewerCache`, `audiocache`, `thumbnailcache`, `tilecache`
//    y `jitCache-<equipo>\BlockCache` (seguras), el timeline (`timeline_cache.db` + carpetas 00..ff) y
//    `jitCache-<equipo>\LocalisedFiles` (del usuario). `%APPDATA%\NVIDIA\NukeComputeCache` (CUDA de Nuke).
//  - Adobe Media Cache movida: `HKCU\Software\Adobe\Common <N>.0\Media Cache` (Windows) o
//    `com.Adobe.Common <N>.0.plist` (mac), `FolderPath` y `DatabasePath`; vale la carpeta madre o la
//    propia, solo por marca (`.cfa` / `.pek`).
//  - After Effects: `Adobe After Effects <ver> Prefs*.txt`, seccion "Disk Cache Controls", `"Folder N"`;
//    se ofrecen las `Disk Cache - *` de adentro.
//  - Ableton Live: `CacheDirectory` del bloque `FileCachePrefData` de `Preferences.cfg` y la de fabrica;
//    se ofrece `Decoding` con su `CacheContent.idx`. `PackDownloads`, del usuario.
//  - Topaz Video (Windows): `defaultProjectDir` y `veaiDataFolder` del registro; por proyecto con
//    `project.tvai`, `thumbnails` (segura) y `previews` (del usuario); los modelos en `models\models` con
//    sus `blob.*` (del usuario; al lado queda `auth.tpz`, que no se toca).
//  - REAPER: `altpeakspath` con `altpeaks` distinto de 0; carpetas de 2 hex con solo `.reapeaks`.
//  - Unreal Engine (Windows): `UE-LocalDataCachePath`, `GlobalLocalDDCPath` de `EditorSettings.ini` y
//    `Common\DerivedDataCache`; solo la forma clasica (carpetas 0..9 con `.udd`). Zen (5.4+) queda afuera.
//  - Redshift en Cinema 4D: `...\Maxon\<Cinema 4D>\Redshift\Cache` con `Textures` adentro; al lado
//    quedan `preferences.xml` y las licencias.
//  - Photoshop: los `Photoshop Temp<digitos>` de la raiz del volumen. CUDA: `%APPDATA%\NVIDIA\ComputeCache`.
//  - Por escaneo: Resolve (`CacheClip` con `.dvcc`; `OptimizedMedia` y `ProxyMedia` a su lado), las
//    previews de Premiere Pro y los `BL_proxy` de Blender.
// Afuera, y por que: la carpeta de `localCachePath` de Nuke (no tiene una marca conocida), Houdini (su
// carpeta temporal guarda los archivos de recuperacion de escenas), las preferencias de Resolve y de
// Redshift (formato sin confirmar), Topaz y Unreal en mac (rutas sin confirmar), Final Cut Pro, Mocha,
// Fusion, Maya y Avid.

namespace {

using CleanupRules::Context;

#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
constexpr Qt::CaseSensitivity kPathCase = Qt::CaseInsensitive;
#else
constexpr Qt::CaseSensitivity kPathCase = Qt::CaseSensitive;
#endif

// Un archivo de preferencias mas grande que esto no es el que se busca.
constexpr qint64 kMaxPrefsBytes = 8 * 1024 * 1024;
// Cuantas entradas se miran, como mucho, para reconocer una carpeta por lo que tiene adentro.
constexpr int kMarkerBudget = 20000;

QString native(const QString &path)
{
    return path.isEmpty() ? QString() : QDir::toNativeSeparators(QDir::cleanPath(path));
}

QString join(const QString &dir, const QString &name)
{
    return dir.isEmpty() ? QString() : native(dir + QLatin1Char('/') + name);
}

QString parentOf(const QString &path)
{
    return native(QFileInfo(path).absolutePath());
}

// ---------------------------------------------------------------- de donde se lee

QString envValue(const Context &context, const QString &name)
{
    if (context.sources.env) {
        return context.sources.env(name);
    }
    return qEnvironmentVariable(name.toLocal8Bit().constData());
}

[[maybe_unused]] QString registryValue(const Context &context, const QString &key, const QString &value)
{
    return context.sources.registry ? context.sources.registry(key, value) : AppPreferences::userRegistryString(key, value);
}

[[maybe_unused]] QStringList registryKeys(const Context &context, const QString &key)
{
    return context.sources.registryKeys ? context.sources.registryKeys(key) : AppPreferences::userRegistrySubKeys(key);
}

[[maybe_unused]] QVariantMap plistValue(const Context &context, const QString &path)
{
    return context.sources.plist ? context.sources.plist(path) : AppPreferences::readPlist(path);
}

QByteArray readBytes(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > kMaxPrefsBytes) {
        return QByteArray();
    }
    return file.readAll();
}

QString readText(const QString &path)
{
    const QByteArray bytes = readBytes(path);
    if (bytes.startsWith("\xFF\xFE")) {
        QStringDecoder decoder(QStringDecoder::Utf16LE);
        return decoder(bytes.mid(2));
    }
    if (bytes.startsWith("\xEF\xBB\xBF")) {
        return QString::fromUtf8(bytes.mid(3));
    }
    return QString::fromUtf8(bytes);
}

// ---------------------------------------------------------------- formas de carpetas

QStringList subDirs(const QString &dir)
{
    if (dir.isEmpty()) {
        return {};
    }
    return QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::NoSymLinks);
}

bool isHexName(const QString &name, int minLength, int maxLength)
{
    if (name.size() < minLength || name.size() > maxLength) {
        return false;
    }
    for (const QChar c : name) {
        const char16_t u = c.unicode();
        if (!((u >= u'0' && u <= u'9') || (u >= u'a' && u <= u'f') || (u >= u'A' && u <= u'F'))) {
            return false;
        }
    }
    return true;
}

bool isLink(const QFileInfo &info)
{
    return info.isSymLink() || info.isJunction();
}

// La carpeta tiene la forma de una cache: ninguna entrada es un enlace, todas cumplen `accepted` y al
// menos una cumple `required`. Con muchisimas entradas iguales alcanza con mirar las primeras.
bool looksLike(const QString &dir, const std::function<bool(const QFileInfo &)> &accepted,
               const std::function<bool(const QFileInfo &)> &required)
{
    if (dir.isEmpty() || FileSystemOps::kind(dir) != FileSystemOps::Kind::Dir) {
        return false;
    }
    QDirIterator it(dir, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    bool found = false;
    int seen = 0;
    while (it.hasNext() && seen < kMarkerBudget) {
        it.next();
        const QFileInfo info = it.fileInfo();
        if (isLink(info) || !accepted(info)) {
            return false;
        }
        found = found || required(info);
        ++seen;
    }
    return found;
}

// Hay, a `depth` niveles o menos, un archivo que cumple `match`. Mira `budget` entradas como mucho y no
// entra en enlaces.
bool findFile(const QString &dir, int depth, const std::function<bool(const QString &name)> &match, int *budget)
{
    QStringList inner;
    QDirIterator it(dir, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    while (it.hasNext()) {
        if (--*budget < 0) {
            return false;
        }
        it.next();
        const QFileInfo info = it.fileInfo();
        if (isLink(info)) {
            continue;
        }
        if (info.isDir()) {
            inner.append(info.filePath());
        } else if (match(info.fileName())) {
            return true;
        }
    }
    if (depth <= 1) {
        return false;
    }
    for (const QString &sub : inner) {
        if (findFile(sub, depth - 1, match, budget)) {
            return true;
        }
    }
    return false;
}

std::function<bool(const QString &)> suffixIn(const QStringList &suffixes)
{
    return [suffixes](const QString &name) {
        for (const QString &suffix : suffixes) {
            if (name.endsWith(QLatin1Char('.') + suffix, Qt::CaseInsensitive)) {
                return true;
            }
        }
        return false;
    };
}

bool hasFileWithin(const QString &dir, int depth, const QStringList &suffixes)
{
    if (FileSystemOps::kind(dir) != FileSystemOps::Kind::Dir) {
        return false;
    }
    int budget = kMarkerBudget;
    return findFile(dir, depth, suffixIn(suffixes), &budget);
}

// Al lado de la cache hay algo que parece una licencia: no se ofrece.
bool hasLicenseFile(const QString &dir)
{
    return !QDir(dir)
                .entryList({QStringLiteral("*.lic"), QStringLiteral("*licen*"), QStringLiteral("*.rlm"), QStringLiteral("*.hid"),
                            QStringLiteral("*.key")},
                           QDir::Files | QDir::Hidden)
                .isEmpty();
}

// ---------------------------------------------------------------- bloqueos

QStringList nukeBlockers()
{
    // Un exe por version ("Nuke17.0.exe"); NukeX, Studio y Hiero son el mismo con otra bandera.
    return {QStringLiteral("nuke"),  QStringLiteral("nuke1*"), QStringLiteral("nuke2*"), QStringLiteral("nuke9*"),
            QStringLiteral("nuke8*"), QStringLiteral("nuke7*"), QStringLiteral("nuke6*"), QStringLiteral("com.foundry.nuke*")};
}

QStringList adobeBlockers()
{
#ifdef Q_OS_MACOS
    return {QStringLiteral("com.adobe.premierepro"), QStringLiteral("com.adobe.aftereffects"), QStringLiteral("com.adobe.ame"),
            QStringLiteral("com.adobe.audition"),    QStringLiteral("adobe premiere pro"),    QStringLiteral("after effects"),
            QStringLiteral("adobe media encoder"),   QStringLiteral("adobe audition")};
#else
    return {QStringLiteral("adobe premiere pro"), QStringLiteral("afterfx"), QStringLiteral("adobe media encoder"),
            QStringLiteral("adobe audition")};
#endif
}

QStringList afterEffectsBlockers()
{
    return {QStringLiteral("afterfx"), QStringLiteral("aerender"), QStringLiteral("after effects"), QStringLiteral("com.adobe.aftereffects*")};
}

QStringList premiereBlockers()
{
    return {QStringLiteral("adobe premiere pro"), QStringLiteral("com.adobe.premierepro*")};
}

QStringList abletonBlockers()
{
    return {QStringLiteral("ableton live*"), QStringLiteral("com.ableton.live"), QStringLiteral("live")};
}

QStringList resolveBlockers()
{
    return {QStringLiteral("resolve"), QStringLiteral("davinci resolve"), QStringLiteral("com.blackmagic-design.davinciresolve*")};
}

[[maybe_unused]] QStringList topazBlockers()
{
    return {QStringLiteral("topaz video*")};
}

QStringList reaperBlockers()
{
    return {QStringLiteral("reaper"), QStringLiteral("com.cockos.reaper")};
}

[[maybe_unused]] QStringList unrealBlockers()
{
    return {QStringLiteral("unrealeditor*"), QStringLiteral("ue4editor*"), QStringLiteral("shadercompileworker")};
}

QStringList cinemaBlockers()
{
    return {QStringLiteral("cinema 4d*"), QStringLiteral("c4dpy"), QStringLiteral("commandline"), QStringLiteral("redshiftcmdline"),
            QStringLiteral("com.maxon.cinema4d*")};
}

QStringList blenderBlockers()
{
    return {QStringLiteral("blender"), QStringLiteral("org.blenderfoundation.blender")};
}

QStringList photoshopBlockers()
{
    return {QStringLiteral("photoshop"), QStringLiteral("com.adobe.photoshop*")};
}

// ---------------------------------------------------------------- bases que se rechazan

QString dotNukeDir(const Context &context)
{
#ifdef Q_OS_WIN
    // Foundry (Q100048): con HOME definida, `.nuke` va debajo de HOME.
    const QString home = envValue(context, QStringLiteral("HOME"));
    if (!home.isEmpty()) {
        return join(home, QStringLiteral(".nuke"));
    }
#endif
    return join(context.bases.profile, QStringLiteral(".nuke"));
}

struct Fences
{
    QStringList banned;    ///< ni ellas ni una carpeta que las contenga
    QStringList dataRoots; ///< ni ellas ni sus hijas directas (la carpeta de datos de un programa)
    QStringList allowed;   ///< hijas de una carpeta de datos que si sirven (la temporal)
    QStringList excluded;  ///< mac sin acceso total al disco: ni se mira
    QString dotNuke;
};

Fences fences(const Context &context)
{
    Fences f;
    const auto add = [](QStringList &list, const QString &path) {
        if (path.isEmpty()) {
            return;
        }
        const QString canonical = FileSystemOps::canonicalPath(path);
        const QString value = canonical.isEmpty() ? native(path) : canonical;
        if (!value.isEmpty() && !list.contains(value, kPathCase)) {
            list.append(value);
        }
    };
    for (const QStorageInfo &storage : QStorageInfo::mountedVolumes()) {
        add(f.banned, storage.rootPath());
    }
    add(f.banned, context.volumeRoot);
    const QString profile = context.bases.profile;
    if (!profile.isEmpty()) {
        add(f.banned, profile);
        add(f.banned, parentOf(profile));
        for (const char *name : {"Desktop", "Documents", "Downloads", "Pictures", "Music", "Videos", "Movies"}) {
            add(f.banned, join(profile, QLatin1String(name)));
        }
    }
    for (const QStandardPaths::StandardLocation location :
         {QStandardPaths::HomeLocation, QStandardPaths::DesktopLocation, QStandardPaths::DocumentsLocation,
          QStandardPaths::DownloadLocation, QStandardPaths::PicturesLocation, QStandardPaths::MusicLocation,
          QStandardPaths::MoviesLocation}) {
        add(f.banned, QStandardPaths::writableLocation(location));
    }
#ifdef Q_OS_WIN
    add(f.dataRoots, context.bases.roamingAppData);
    add(f.dataRoots, context.bases.localAppData);
    if (!context.bases.roamingAppData.isEmpty()) {
        add(f.dataRoots, parentOf(context.bases.roamingAppData));
    }
    if (!context.bases.localAppData.isEmpty()) {
        add(f.dataRoots, join(parentOf(context.bases.localAppData), QStringLiteral("LocalLow")));
    }
    for (const char *name : {"ProgramData", "ProgramFiles", "ProgramFiles(x86)", "ProgramW6432", "SystemRoot"}) {
        add(f.dataRoots, envValue(context, QLatin1String(name)));
    }
    add(f.dataRoots, context.bases.systemRoot);
#else
    add(f.dataRoots, context.bases.library);
    add(f.dataRoots, context.bases.appSupport);
    add(f.dataRoots, context.bases.caches);
    add(f.dataRoots, join(context.bases.library, QStringLiteral("Preferences")));
    for (const char *path : {"/Applications", "/Library", "/Library/Application Support"}) {
        add(f.dataRoots, QLatin1String(path));
    }
#endif
    add(f.allowed, context.bases.temp);
    f.excluded = SystemPaths::scanExclusions();
    const QString dotNuke = dotNukeDir(context);
    const QString realNuke = FileSystemOps::canonicalPath(dotNuke);
    f.dotNuke = realNuke.isEmpty() ? dotNuke : realNuke;
    return f;
}

QString acceptedBaseWith(const QString &path, const Fences &f)
{
    const QString cleaned = native(path.trimmed());
    if (cleaned.isEmpty() || QDir::isRelativePath(cleaned)) {
        return QString();
    }
    // Lo que macOS protege sin acceso total al disco ni se mira: mirarlo mostraria un cartel.
    for (const QString &excluded : f.excluded) {
        if (DeleteGuard::samePath(cleaned, excluded) || DeleteGuard::isInside(cleaned, excluded)) {
            return QString();
        }
    }
    // La ruta real; si la carpeta misma es un enlace, `kind` no es Dir y se rechaza.
    const QString real = FileSystemOps::canonicalPath(cleaned);
    if (real.isEmpty() || FileSystemOps::kind(real) != FileSystemOps::Kind::Dir) {
        return QString();
    }
    for (const QString &banned : f.banned) {
        if (DeleteGuard::samePath(real, banned) || DeleteGuard::isInside(banned, real)) {
            return QString();
        }
    }
    bool allowed = false;
    for (const QString &exception : f.allowed) {
        allowed = allowed || DeleteGuard::samePath(real, exception);
    }
    if (!allowed) {
        const QString parent = parentOf(real);
        for (const QString &root : f.dataRoots) {
            if (DeleteGuard::samePath(real, root) || DeleteGuard::isInside(root, real) || DeleteGuard::samePath(parent, root)) {
                return QString();
            }
        }
    }
    if (!f.dotNuke.isEmpty() && (DeleteGuard::samePath(real, f.dotNuke) || DeleteGuard::isInside(real, f.dotNuke))) {
        return QString();
    }
    if (AppCacheRules::hasProjectMarker(real)) {
        return QString();
    }
    return real;
}

void addUnique(QStringList &list, const QString &path)
{
    if (!path.isEmpty() && !list.contains(path, kPathCase)) {
        list.append(path);
    }
}

// ---------------------------------------------------------------- items

// Un archivo suelto como destino: existe, no es un enlace y esta en el volumen.
void addFile(Cleanup::Item &item, const QString &path, const Context &context)
{
    const QString real = FileSystemOps::canonicalPath(path);
    if (real.isEmpty() || FileSystemOps::kind(real) != FileSystemOps::Kind::File || !DeleteGuard::isInside(real, context.volumeRoot)) {
        return;
    }
    const QString parent = parentOf(real);
    if (!DeleteGuard::samePath(parent, context.volumeRoot) && CleanupRules::usableDir(parent, context).isEmpty()) {
        return;
    }
    item.targets.append(Cleanup::Target{real, Cleanup::Action::Entire, 0});
}

// Un item con esas carpetas (se vacian) y esos archivos (se borran): solo los que existen, no son enlaces,
// estan en el volumen y no tienen un proyecto adentro. Sin ninguno, no agrega nada.
void offer(Cleanup::Category &category, const QString &name, const QString &shown, const QStringList &dirs, const QStringList &files,
           const Context &context, const QStringList &blockers, const QString &label)
{
    QStringList clean;
    for (const QString &dir : dirs) {
        if (!dir.isEmpty() && !AppCacheRules::hasProjectMarker(dir)) {
            addUnique(clean, dir);
        }
    }
    Cleanup::Item item = CleanupRules::makeItem(name, shown, clean, Cleanup::Action::Contents, context);
    for (const QString &file : files) {
        addFile(item, file, context);
    }
    if (item.targets.isEmpty()) {
        return;
    }
    if (item.path.isEmpty()) {
        const QString usable = CleanupRules::usableDir(shown, context);
        item.path = usable.isEmpty() ? (DeleteGuard::samePath(shown, context.volumeRoot) ? context.volumeRoot : item.targets.first().path)
                                     : usable;
    }
    item.blockers = blockers;
    item.blockerLabel = label;
    category.items.append(item);
}

Cleanup::Category category(const char *id, Cleanup::Group group, const QString &title, const QString &caption, bool single = false)
{
    Cleanup::Category c;
    c.id = QLatin1String(id);
    c.group = group;
    c.title = title;
    c.caption = caption;
    c.single = single;
    return c;
}

// Lo que se junta antes de sumarlo a las categorias del sistema.
struct Pending
{
    Cleanup::Category media;     ///< a la categoria "media" de cada plataforma
    Cleanup::Category shaders;   ///< a "shaders" (Windows)
    Cleanup::Category nuke;      ///< Nuke, seguras
    Cleanup::Category render;    ///< caches de render (Redshift)
    Cleanup::Category scratch;   ///< temporales de Photoshop
    Cleanup::Category nukeWork;  ///< Nuke, del usuario
    Cleanup::Category previews;  ///< previews, proxies y medios optimizados
    Cleanup::Category downloads; ///< modelos, packs y datos derivados
};

Pending pending()
{
    Pending p;
    p.media = category("media", Cleanup::Group::Safe, I18n::tr("Media caches"), I18n::tr("Rebuilt when a project opens."));
    p.shaders = category("shaders", Cleanup::Group::Safe, I18n::tr("Shader caches and crash dumps"),
                         I18n::tr("Games and 3D apps compile their shaders again as they run."));
    p.nuke = category("nuke", Cleanup::Group::Safe, I18n::tr("Nuke caches"),
                      I18n::tr("Viewer, audio, thumbnail and paint caches. Nuke fills them again as you work."));
    p.render = category("rendercache", Cleanup::Group::Safe, I18n::tr("Render caches"),
                        I18n::tr("Converted textures and compiled shaders. The renderer makes them again on the next render."));
    p.scratch = category("pstemp", Cleanup::Group::Safe, I18n::tr("Photoshop scratch files"),
                         I18n::tr("Left at the root of a scratch disk when Photoshop closes unexpectedly."), true);
    p.nukeWork = category("nukework", Cleanup::Group::Yours, I18n::tr("Nuke timeline cache and local copies"),
                          I18n::tr("Frames sent to the timeline cache and local copies of footage. Nuke makes them again, but it takes time."));
    p.previews = category("previews", Cleanup::Group::Yours, I18n::tr("Previews, proxies and optimized media"),
                          I18n::tr("Made by video apps from your footage. They can be made again, but it takes render time."));
    p.downloads = category("appdownloads", Cleanup::Group::Yours, I18n::tr("Downloads and derived data"),
                           I18n::tr("The app downloads or builds them again when it needs them, which can take a long time."));
    return p;
}

// Suma `incoming` a la categoria del mismo id, o la inserta despues de `after` (al final si no esta).
void merge(QList<Cleanup::Category> &categories, const Cleanup::Category &incoming, const QString &after)
{
    if (incoming.items.isEmpty()) {
        return;
    }
    for (Cleanup::Category &category : categories) {
        if (category.id == incoming.id) {
            category.items.append(incoming.items);
            return;
        }
    }
    qsizetype at = categories.size();
    for (qsizetype i = 0; i < categories.size(); ++i) {
        if (categories.at(i).id == after) {
            at = i + 1;
            break;
        }
    }
    categories.insert(at, incoming);
}

// ---------------------------------------------------------------- Nuke

QStringList nukeTempDirs(const Context &context)
{
    const QString set = envValue(context, QStringLiteral("NUKE_TEMP_DIR"));
    if (!set.isEmpty()) {
        return {native(set)};
    }
#ifdef Q_OS_MACOS
    // Foundry actual: /var/tmp/nuke-u<uid>; guias viejas: /var/tmp/nuke.
    return {QStringLiteral("/var/tmp/nuke-u%1").arg(getuid()), QStringLiteral("/var/tmp/nuke")};
#else
    return {join(context.bases.temp, QStringLiteral("nuke"))};
#endif
}

bool isViewerCache(const QString &dir)
{
    return looksLike(
        dir,
        [](const QFileInfo &info) {
            return info.isDir() ? isHexName(info.fileName(), 2, 2) : info.suffix().compare(QLatin1String("nkc"), Qt::CaseInsensitive) == 0;
        },
        [](const QFileInfo &info) { return info.isDir(); });
}

bool isAudioCache(const QString &dir)
{
    const auto file = [](const QFileInfo &info) { return info.isFile() && isHexName(info.completeBaseName(), 32, 128); };
    return looksLike(dir, file, file);
}

bool isAllOfSuffix(const QString &dir, const char *suffix)
{
    const auto file = [suffix](const QFileInfo &info) {
        return info.isFile() && info.suffix().compare(QLatin1String(suffix), Qt::CaseInsensitive) == 0;
    };
    return looksLike(dir, file, file);
}

bool isTileCache(const QString &dir)
{
    return looksLike(
        dir,
        [](const QFileInfo &info) {
            if (info.isDir()) {
                return isHexName(info.fileName(), 2, 2);
            }
            const QString name = info.fileName();
            return name == QLatin1String("DiskSize") || name == QLatin1String("DiskSize.lock") || name.endsWith(QLatin1String(".cache"))
                   || name.endsWith(QLatin1String(".cache.metadata"));
        },
        [](const QFileInfo &info) { return info.isDir() || info.fileName() == QLatin1String("DiskSize"); });
}

// La cache de CUDA: un archivo `index` y carpetas de un caracter hexadecimal.
[[maybe_unused]] bool isComputeCache(const QString &dir)
{
    return looksLike(
        dir, [](const QFileInfo &info) { return info.isDir() ? isHexName(info.fileName(), 1, 1) : info.fileName() == QLatin1String("index"); },
        [](const QFileInfo &info) { return info.isFile(); });
}

void addNuke(Pending &p, const Context &context, const Fences &f)
{
    const QStringList temps = nukeTempDirs(context);
    QString temp = temps.value(0);
    for (const QString &candidate : temps) {
        if (QFileInfo(candidate).isDir()) {
            temp = candidate;
            break;
        }
    }
    const auto getenv = [&](const QString &name) {
        const QString value = envValue(context, name);
        return value.isEmpty() && name == QLatin1String("NUKE_TEMP_DIR") ? temp : value;
    };
    QStringList disk;
    QStringList timeline;
    const auto collect = [&](const QString &text, const char *key, const char *fallback, QStringList &out) {
        bool found = false;
        QString value = text.isEmpty() ? QString() : AppCacheRules::nukeValue(text, QLatin1String(key), &found);
        if (!found || value.trimmed().isEmpty()) {
            value = QLatin1String(fallback);
        }
        addUnique(out, AppCacheRules::resolveNukeExpression(value, getenv));
    };
    // Todos los `preferences<ver>.nk` (uno por version instalada): se unen sus rutas.
    const QString dot = dotNukeDir(context);
    const QStringList prefs = QDir(dot).entryList({QStringLiteral("preferences*.nk")}, QDir::Files);
    for (const QString &name : prefs) {
        const QString text = readText(join(dot, name));
        collect(text, "DiskCachePath", "[getenv NUKE_TEMP_DIR]", disk);
        collect(text, "TimelineDiskCachePath", "[getenv NUKE_TEMP_DIR]/TimelineCache", timeline);
    }
    if (prefs.isEmpty()) {
        collect(QString(), "DiskCachePath", "[getenv NUKE_TEMP_DIR]", disk);
        collect(QString(), "TimelineDiskCachePath", "[getenv NUKE_TEMP_DIR]/TimelineCache", timeline);
    }
    // Las variables que mandan sobre la preferencia (el flipbook de Nuke mira FC_DISK_CACHE y NUKE_DISK_CACHE).
    for (const char *name : {"FC_DISK_CACHE", "NUKE_DISK_CACHE"}) {
        addUnique(disk, native(envValue(context, QLatin1String(name))));
    }

    QStringList bases;
    for (const QString &path : disk) {
        addUnique(bases, acceptedBaseWith(path, f));
    }
    const QString label = QStringLiteral("Nuke");
    for (const QString &base : bases) {
        const QString viewer = join(base, QStringLiteral("ViewerCache"));
        if (isViewerCache(viewer)) {
            offer(p.nuke, I18n::tr("Viewer cache"), viewer, {viewer}, {}, context, nukeBlockers(), label);
        }
        const QString audio = join(base, QStringLiteral("audiocache"));
        if (isAudioCache(audio)) {
            offer(p.nuke, I18n::tr("Audio cache"), audio, {audio}, {}, context, nukeBlockers(), label);
        }
        const QString thumbnails = join(base, QStringLiteral("thumbnailcache"));
        if (isAllOfSuffix(thumbnails, "cch")) {
            offer(p.nuke, I18n::tr("Thumbnails"), thumbnails, {thumbnails}, {}, context, nukeBlockers(), label);
        }
        const QString tiles = join(base, QStringLiteral("tilecache"));
        if (isTileCache(tiles)) {
            offer(p.nuke, I18n::tr("Roto and Paint tiles"), tiles, {tiles}, {}, context, nukeBlockers(), label);
        }
        for (const QString &name : subDirs(base)) {
            if (!name.startsWith(QLatin1String("jitCache-"))) {
                continue;
            }
            const QString jit = join(base, name);
            const QString blocks = join(jit, QStringLiteral("BlockCache"));
            if (isAllOfSuffix(blocks, "cch")) {
                offer(p.nuke, I18n::tr("Read blocks"), blocks, {blocks}, {}, context, nukeBlockers(), label);
            }
            // Copias locales de media de red (Studio): sin red, el trabajo se cae. Del usuario.
            const QString local = join(jit, QStringLiteral("LocalisedFiles"));
            if (FileSystemOps::kind(local) == FileSystemOps::Kind::Dir) {
                offer(p.nukeWork, I18n::tr("Localized files"), local, {local}, {}, context, nukeBlockers(), label);
            }
        }
    }

    // El timeline de Studio y Hiero: `timeline_cache.db` y las carpetas 00..ff de su carpeta (que puede ser
    // la misma de DiskCachePath). Se van juntos: la base sin sus cuadros, o al reves, queda inconsistente.
    QStringList timelineBases;
    for (const QString &path : timeline) {
        addUnique(timelineBases, acceptedBaseWith(path, f));
    }
    for (const QString &base : timelineBases) {
        const QString database = join(base, QStringLiteral("timeline_cache.db"));
        if (FileSystemOps::kind(database) != FileSystemOps::Kind::File) {
            continue;
        }
        QStringList buckets;
        for (const QString &name : subDirs(base)) {
            const QString bucket = join(base, name);
            if (isHexName(name, 2, 2) && !QDir(bucket).isEmpty()) {
                buckets.append(bucket);
            }
        }
        offer(p.nukeWork, I18n::tr("Timeline cache"), base, buckets, {database}, context, nukeBlockers(), label);
    }

#ifdef Q_OS_WIN
    // La cache de CUDA propia de Nuke (Nuke le fija CUDA_CACHE_PATH).
    const QString compute = join(join(context.bases.roamingAppData, QStringLiteral("NVIDIA")), QStringLiteral("NukeComputeCache"));
    if (isComputeCache(compute)) {
        offer(p.nuke, I18n::tr("CUDA kernels"), compute, {compute}, {}, context, nukeBlockers(), label);
    }
#endif
}

// ---------------------------------------------------------------- Adobe

bool hasAdobeMedia(const QString &dir)
{
    return hasFileWithin(dir, 2, {QStringLiteral("cfa"), QStringLiteral("pek")});
}

void addAdobeMediaCache(Pending &p, const Context &context, const Fences &f)
{
    QStringList configured;
#ifdef Q_OS_WIN
    const QString root = QStringLiteral("Software\\Adobe");
    for (const QString &key : registryKeys(context, root)) {
        if (!key.startsWith(QLatin1String("Common "), Qt::CaseInsensitive)) {
            continue;
        }
        for (const char *value : {"FolderPath", "DatabasePath"}) {
            configured.append(registryValue(context, root + QLatin1Char('\\') + key + QStringLiteral("\\Media Cache"), QLatin1String(value)));
        }
    }
#else
    const QString prefs = join(context.bases.library, QStringLiteral("Preferences"));
    for (const QString &file : QDir(prefs).entryList({QStringLiteral("com.Adobe.Common *.plist")}, QDir::Files)) {
        const QVariantMap media = plistValue(context, join(prefs, file)).value(QStringLiteral("Media Cache")).toMap();
        for (const char *value : {"FolderPath", "DatabasePath"}) {
            configured.append(media.value(QLatin1String(value)).toString());
        }
    }
#endif
    static const QStringList names = {QStringLiteral("Media Cache Files"), QStringLiteral("Media Cache"), QStringLiteral("Peak Files")};
    // Por carpeta madre: lo que se vacia ahi.
    QStringList parents;
    QList<QStringList> dirsByParent;
    const auto put = [&](const QString &parent, const QString &dir) {
        const qsizetype at = parents.indexOf(parent);
        if (at < 0) {
            parents.append(parent);
            dirsByParent.append({dir});
        } else if (!dirsByParent[at].contains(dir, kPathCase)) {
            dirsByParent[at].append(dir);
        }
    };
    for (const QString &path : configured) {
        const QString real = acceptedBaseWith(path, f);
        if (real.isEmpty()) {
            continue;
        }
        const QString name = QFileInfo(real).fileName();
        if (names.contains(name, Qt::CaseInsensitive)) {
            // La preferencia nombra la carpeta de cache misma: va ella sola, con su marca. La de la base de
            // datos ("Media Cache") cuenta si al lado estan los archivos de la cache.
            const QString parent = parentOf(real);
            const bool marked = name.compare(QLatin1String("Media Cache"), Qt::CaseInsensitive) == 0
                                    ? hasAdobeMedia(join(parent, QStringLiteral("Media Cache Files")))
                                    : hasAdobeMedia(real);
            if (marked) {
                put(parent, real);
            }
            continue;
        }
        // La preferencia nombra la carpeta madre: Adobe crea adentro sus tres carpetas.
        const QString files = join(real, QStringLiteral("Media Cache Files"));
        const QString peaks = join(real, QStringLiteral("Peak Files"));
        const QString database = join(real, QStringLiteral("Media Cache"));
        if (hasAdobeMedia(files)) {
            put(real, files);
            if (FileSystemOps::kind(database) == FileSystemOps::Kind::Dir) {
                put(real, database);
            }
        }
        if (hasAdobeMedia(peaks)) {
            put(real, peaks);
        }
    }
    for (qsizetype i = 0; i < parents.size(); ++i) {
        offer(p.media, QStringLiteral("Adobe Media Cache"), parents.at(i), dirsByParent.at(i), {}, context, adobeBlockers(),
              QStringLiteral("Adobe"));
    }
}

void addAfterEffects(Pending &p, const Context &context, const Fences &f)
{
#ifdef Q_OS_WIN
    const QString root = join(context.bases.roamingAppData, QStringLiteral("Adobe/After Effects"));
    const QStringList filters = {QStringLiteral("Adobe After Effects * Prefs*.txt")};
#else
    const QString root = join(context.bases.library, QStringLiteral("Preferences/Adobe/After Effects"));
    const QStringList filters = {QStringLiteral("Adobe After Effects * Prefs*")};
#endif
    QStringList parents;
    for (const QString &version : subDirs(root)) {
        const QString dir = join(root, version);
        const QStringList files = QDir(dir).entryList(filters, QDir::Files);
        if (files.isEmpty()) {
            continue;
        }
        QStringList folders;
        for (const QString &file : files) {
            folders.append(AppCacheRules::afterEffectsCacheFolders(readText(join(dir, file))));
        }
        if (folders.isEmpty()) {
            // Sin la clave, la de fabrica (sin confirmar en Windows; la marca decide).
#ifdef Q_OS_WIN
            folders.append(context.bases.temp);
#else
            folders.append(join(join(context.bases.caches, QStringLiteral("Adobe/After Effects")), version));
#endif
        }
        for (const QString &folder : folders) {
            addUnique(parents, acceptedBaseWith(folder, f));
        }
    }
    for (const QString &parent : parents) {
        QStringList dirs;
        for (const QString &name : subDirs(parent)) {
            if (name.startsWith(QLatin1String("Disk Cache - "), Qt::CaseInsensitive)) {
                dirs.append(join(parent, name));
            }
        }
        offer(p.media, I18n::tr("After Effects disk cache"), dirs.size() == 1 ? dirs.first() : parent, dirs, {}, context,
              afterEffectsBlockers(), QStringLiteral("After Effects"));
    }
}

// ---------------------------------------------------------------- Ableton Live

void addAbleton(Pending &p, const Context &context, const Fences &f)
{
#ifdef Q_OS_WIN
    const QString prefsRoot = join(context.bases.roamingAppData, QStringLiteral("Ableton"));
    const auto cfgOf = [&](const QString &live) { return join(join(prefsRoot, live), QStringLiteral("Preferences/Preferences.cfg")); };
    const QString cacheRoot = join(context.bases.localAppData, QStringLiteral("Ableton"));
    QStringList candidates = {join(cacheRoot, QStringLiteral("Cache/Cache"))};
#else
    const QString prefsRoot = join(context.bases.library, QStringLiteral("Preferences/Ableton"));
    const auto cfgOf = [&](const QString &live) { return join(join(prefsRoot, live), QStringLiteral("Preferences.cfg")); };
    const QString cacheRoot = join(context.bases.caches, QStringLiteral("Ableton"));
    // En mac la de fabrica no esta confirmada: las dos formas, y decide la marca.
    QStringList candidates = {join(cacheRoot, QStringLiteral("Cache")), join(cacheRoot, QStringLiteral("Cache/Cache"))};
#endif
    for (const QString &live : subDirs(prefsRoot)) {
        if (!live.startsWith(QLatin1String("Live "), Qt::CaseInsensitive)) {
            continue;
        }
        const QString configured = AppCacheRules::abletonCacheDirectory(readBytes(cfgOf(live)));
        if (!configured.isEmpty()) {
            candidates.prepend(configured);
        }
    }
    QStringList decoding;
    for (const QString &candidate : candidates) {
        const QString real = acceptedBaseWith(candidate, f);
        if (real.isEmpty()) {
            continue;
        }
        const QString dir = join(real, QStringLiteral("Decoding"));
        if (FileSystemOps::kind(join(dir, QStringLiteral("CacheContent.idx"))) == FileSystemOps::Kind::File) {
            addUnique(decoding, dir);
        }
    }
    for (const QString &dir : decoding) {
        offer(p.media, I18n::tr("Ableton Live decoding cache"), dir, {dir}, {}, context, abletonBlockers(), QStringLiteral("Ableton Live"));
    }
    // Las descargas de Packs: carpeta fija de Ableton. Del usuario.
#ifdef Q_OS_WIN
    const QString packs = join(cacheRoot, QStringLiteral("Cache/PackDownloads"));
#else
    const QString packs = join(cacheRoot, QStringLiteral("PackDownloads"));
#endif
    if (FileSystemOps::kind(packs) == FileSystemOps::Kind::Dir && !QDir(packs).isEmpty()) {
        offer(p.downloads, I18n::tr("Ableton Live pack downloads"), packs, {packs}, {}, context, abletonBlockers(),
              QStringLiteral("Ableton Live"));
    }
}

// ---------------------------------------------------------------- Topaz Video (Windows)

[[maybe_unused]] bool hasTopazModels(const QString &dir)
{
    for (const QString &name : subDirs(dir)) {
        if (!QDir(join(dir, name)).entryList({QStringLiteral("blob.*")}, QDir::Files).isEmpty()) {
            return true;
        }
    }
    return false;
}

void addTopaz(Pending &p, const Context &context, const Fences &f)
{
#ifdef Q_OS_WIN
    QStringList projectDirs;
    QStringList dataDirs;
    const QString programData = envValue(context, QStringLiteral("ProgramData"));
    for (const char *app : {"Topaz Video", "Topaz Video AI"}) {
        const QString key = QStringLiteral("Software\\Topaz Labs LLC\\") + QLatin1String(app);
        projectDirs.append(registryValue(context, key, QStringLiteral("defaultProjectDir")));
        dataDirs.append(registryValue(context, key, QStringLiteral("veaiDataFolder")));
        if (!programData.isEmpty()) {
            dataDirs.append(join(join(join(programData, QStringLiteral("Topaz Labs LLC")), QLatin1String(app)), QStringLiteral("models")));
        }
    }
    projectDirs.append(join(context.bases.profile, QStringLiteral("Documents/Topaz Video Projects")));
    projectDirs.append(join(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), QStringLiteral("Topaz Video Projects")));
    const QString label = QStringLiteral("Topaz Video");

    // Proyectos: cada subcarpeta con `project.tvai` (que nunca se toca). La carpeta de proyectos misma no
    // puede tener un `project.tvai` (acceptedBase): seria un proyecto, no la carpeta que los junta.
    QStringList seen;
    for (const QString &dir : projectDirs) {
        const QString real = acceptedBaseWith(dir, f);
        if (real.isEmpty() || seen.contains(real, kPathCase)) {
            continue;
        }
        seen.append(real);
        QStringList thumbnails;
        QStringList previews;
        for (const QString &name : subDirs(real)) {
            const QString project = join(real, name);
            if (FileSystemOps::kind(join(project, QStringLiteral("project.tvai"))) != FileSystemOps::Kind::File) {
                continue;
            }
            const QString thumbs = join(project, QStringLiteral("thumbnails"));
            const QString renders = join(project, QStringLiteral("previews"));
            if (FileSystemOps::kind(thumbs) == FileSystemOps::Kind::Dir) {
                thumbnails.append(thumbs);
            }
            if (FileSystemOps::kind(renders) == FileSystemOps::Kind::Dir) {
                previews.append(renders);
            }
        }
        offer(p.media, I18n::tr("Topaz Video thumbnails"), real, thumbnails, {}, context, topazBlockers(), label);
        offer(p.previews, I18n::tr("Topaz Video previews"), real, previews, {}, context, topazBlockers(), label);
    }

    // Modelos: `models` dentro de la carpeta de datos, con sus `blob.*`. Al lado quedan las definiciones y
    // `auth.tpz`, que no se tocan.
    QStringList models;
    for (const QString &dir : dataDirs) {
        const QString real = acceptedBaseWith(dir, f);
        if (real.isEmpty()) {
            continue;
        }
        const QString inner = join(real, QStringLiteral("models"));
        if (hasTopazModels(inner)) {
            addUnique(models, inner);
        }
    }
    for (const QString &dir : models) {
        offer(p.downloads, I18n::tr("Topaz Video models"), dir, {dir}, {}, context, topazBlockers(), label);
    }
#else
    Q_UNUSED(p)
    Q_UNUSED(context)
    Q_UNUSED(f)
#endif
}

// ---------------------------------------------------------------- REAPER, Unreal, Redshift, Photoshop, CUDA

void addReaper(Pending &p, const Context &context, const Fences &f)
{
#ifdef Q_OS_WIN
    const QString ini = join(context.bases.roamingAppData, QStringLiteral("REAPER/reaper.ini"));
#else
    const QString ini = join(context.bases.appSupport, QStringLiteral("REAPER/reaper.ini"));
#endif
    if (FileSystemOps::kind(ini) != FileSystemOps::Kind::File) {
        return;
    }
    const QString base = acceptedBaseWith(AppCacheRules::reaperPeaksFolder(readText(ini)), f);
    if (base.isEmpty()) {
        return;
    }
    QStringList dirs;
    const auto peaks = [](const QFileInfo &info) {
        return info.isFile() && info.suffix().compare(QLatin1String("reapeaks"), Qt::CaseInsensitive) == 0;
    };
    for (const QString &name : subDirs(base)) {
        const QString dir = join(base, name);
        if (isHexName(name, 2, 2) && looksLike(dir, peaks, peaks)) {
            dirs.append(dir);
        }
    }
    offer(p.media, I18n::tr("REAPER peak files"), base, dirs, {}, context, reaperBlockers(), QStringLiteral("REAPER"));
}

void addUnreal(Pending &p, const Context &context, const Fences &f)
{
#ifdef Q_OS_WIN
    QStringList candidates;
    const QString variable = envValue(context, QStringLiteral("UE-LocalDataCachePath"));
    if (!variable.isEmpty() && variable.compare(QLatin1String("None"), Qt::CaseInsensitive) != 0) {
        candidates.append(variable);
    }
    const QString root = join(context.bases.localAppData, QStringLiteral("UnrealEngine"));
    for (const QString &version : subDirs(root)) {
        for (const char *platform : {"WindowsEditor", "Windows"}) {
            const QString ini = join(join(root, version), QStringLiteral("Saved/Config/%1/EditorSettings.ini").arg(QLatin1String(platform)));
            if (FileSystemOps::kind(ini) == FileSystemOps::Kind::File) {
                candidates.append(AppCacheRules::unrealLocalDdcPath(readText(ini)));
            }
        }
    }
    candidates.append(join(root, QStringLiteral("Common/DerivedDataCache")));
    QStringList seen;
    for (const QString &candidate : candidates) {
        const QString base = acceptedBaseWith(candidate, f);
        if (base.isEmpty() || seen.contains(base, kPathCase)) {
            continue;
        }
        seen.append(base);
        // La forma clasica: <base>\<digito>\<digito>\<digito>\<clave>.udd.
        QStringList buckets;
        for (const QString &name : subDirs(base)) {
            const QString dir = join(base, name);
            if (name.size() == 1 && name.at(0).isDigit() && hasFileWithin(dir, 3, {QStringLiteral("udd")})) {
                buckets.append(dir);
            }
        }
        offer(p.downloads, I18n::tr("Unreal Engine derived data"), base, buckets, {}, context, unrealBlockers(),
              QStringLiteral("Unreal Engine"));
    }
#else
    Q_UNUSED(p)
    Q_UNUSED(context)
    Q_UNUSED(f)
#endif
}

void addRedshift(Pending &p, const Context &context)
{
#ifdef Q_OS_WIN
    const QString maxon = join(context.bases.roamingAppData, QStringLiteral("Maxon"));
#else
    const QString maxon = join(context.bases.library, QStringLiteral("Preferences/Maxon"));
#endif
    for (const QString &name : subDirs(maxon)) {
        if (!name.contains(QLatin1String("Cinema 4D"), Qt::CaseInsensitive)) {
            continue;
        }
        // Solo `Cache`: en la carpeta `Redshift` de al lado estan `preferences.xml` y las licencias.
        const QString cache = join(join(maxon, name), QStringLiteral("Redshift/Cache"));
        if (FileSystemOps::kind(join(cache, QStringLiteral("Textures"))) != FileSystemOps::Kind::Dir || hasLicenseFile(cache)) {
            continue;
        }
        offer(p.render, I18n::tr("Redshift cache"), cache, {cache}, {}, context, cinemaBlockers(), QStringLiteral("Cinema 4D"));
    }
}

void addPhotoshop(Pending &p, const Context &context)
{
    static const QRegularExpression pattern(QStringLiteral("^Photoshop Temp\\d+$"));
    QStringList files;
    for (const QString &name : QDir(context.volumeRoot).entryList({QStringLiteral("Photoshop Temp*")}, QDir::Files | QDir::Hidden | QDir::System)) {
        if (pattern.match(name).hasMatch()) {
            files.append(join(context.volumeRoot, name));
        }
    }
    offer(p.scratch, I18n::tr("Photoshop scratch files"), context.volumeRoot, {}, files, context, photoshopBlockers(),
          QStringLiteral("Photoshop"));
}

void addCuda(Pending &p, const Context &context)
{
#ifdef Q_OS_WIN
    const QString compute = join(context.bases.roamingAppData, QStringLiteral("NVIDIA/ComputeCache"));
    if (isComputeCache(compute)) {
        offer(p.shaders, I18n::tr("CUDA compute cache"), compute, {compute}, {}, context, {}, QString());
    }
#else
    Q_UNUSED(p)
    Q_UNUSED(context)
#endif
}

// Lo que alguna de estas reglas ya ofrece no se repite en "Other app caches" (mac): una carpeta de
// ~/Library/Caches que contiene una de estas caches deja de ofrecerse entera.
void dropCovered(QList<Cleanup::Category> &categories, const Pending &p)
{
    QStringList mine;
    for (const Cleanup::Category *category : {&p.media, &p.shaders, &p.nuke, &p.render, &p.scratch, &p.nukeWork, &p.previews, &p.downloads}) {
        for (const Cleanup::Item &item : category->items) {
            for (const Cleanup::Target &target : item.targets) {
                mine.append(target.path);
            }
        }
    }
    if (mine.isEmpty()) {
        return;
    }
    for (Cleanup::Category &category : categories) {
        if (category.id != QLatin1String("appcaches")) {
            continue;
        }
        QList<Cleanup::Item> kept;
        for (const Cleanup::Item &item : category.items) {
            bool overlaps = false;
            for (const Cleanup::Target &target : item.targets) {
                for (const QString &path : mine) {
                    if (DeleteGuard::samePath(path, target.path) || DeleteGuard::isInside(path, target.path)
                        || DeleteGuard::isInside(target.path, path)) {
                        overlaps = true;
                    }
                }
            }
            if (!overlaps) {
                kept.append(item);
            }
        }
        category.items = kept;
    }
}

} // namespace

namespace AppCacheRules {

QString nukeValue(const QString &text, const QString &key, bool *found)
{
    if (found) {
        *found = false;
    }
    if (key.isEmpty()) {
        return QString();
    }
    for (QString line : text.split(QLatin1Char('\n'))) {
        line.remove(QLatin1Char('\r'));
        qsizetype i = 0;
        while (i < line.size() && line.at(i).isSpace()) {
            ++i;
        }
        if (QStringView(line).mid(i).left(key.size()) != key) {
            continue;
        }
        qsizetype j = i + key.size();
        // "DiskCachePathX" es otra clave; sin valor no cuenta.
        if (j >= line.size() || !line.at(j).isSpace()) {
            continue;
        }
        while (j < line.size() && line.at(j).isSpace()) {
            ++j;
        }
        if (j >= line.size()) {
            continue;
        }
        if (found) {
            *found = true;
        }
        const QString rest = line.mid(j);
        QString out;
        if (rest.startsWith(QLatin1Char('"'))) {
            // Entre comillas: hasta la que cierra; `\x` es `x`.
            for (qsizetype k = 1; k < rest.size(); ++k) {
                const QChar c = rest.at(k);
                if (c == QLatin1Char('\\') && k + 1 < rest.size()) {
                    out += rest.at(++k);
                } else if (c == QLatin1Char('"')) {
                    break;
                } else {
                    out += c;
                }
            }
            return out;
        }
        if (rest.startsWith(QLatin1Char('{'))) {
            // Entre llaves: literal, hasta la que cierra.
            int depth = 0;
            for (qsizetype k = 0; k < rest.size(); ++k) {
                const QChar c = rest.at(k);
                if (c == QLatin1Char('{')) {
                    if (depth++ == 0) {
                        continue;
                    }
                } else if (c == QLatin1Char('}') && --depth == 0) {
                    break;
                }
                out += c;
            }
            return out;
        }
        // Sin comillas: hasta el primer espacio fuera de corchetes ("[getenv X]/y" es una sola palabra).
        int brackets = 0;
        for (qsizetype k = 0; k < rest.size(); ++k) {
            const QChar c = rest.at(k);
            if (c == QLatin1Char('\\') && k + 1 < rest.size()) {
                out += rest.at(++k);
                continue;
            }
            if (c == QLatin1Char('[')) {
                ++brackets;
            } else if (c == QLatin1Char(']') && brackets > 0) {
                --brackets;
            } else if (c.isSpace() && brackets == 0) {
                break;
            }
            out += c;
        }
        return out;
    }
    return QString();
}

QString resolveNukeExpression(const QString &value, const std::function<QString(const QString &)> &getenv)
{
    static const QRegularExpression call(QStringLiteral("\\[\\s*getenv\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*\\]"));
    QString out;
    qsizetype last = 0;
    QRegularExpressionMatchIterator it = call.globalMatch(value);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const QString resolved = getenv ? getenv(match.captured(1)) : QString();
        if (resolved.isEmpty()) {
            return QString();
        }
        out += value.mid(last, match.capturedStart() - last);
        out += resolved;
        last = match.capturedEnd();
    }
    out += value.mid(last);
    out = out.trimmed();
    // Otra expresion de TCL (corchetes, variables): no se adivina.
    if (out.isEmpty() || out.contains(QLatin1Char('[')) || out.contains(QLatin1Char(']')) || out.contains(QLatin1Char('$'))) {
        return QString();
    }
    return native(out);
}

QStringList afterEffectsCacheFolders(const QString &prefsText)
{
    static const QRegularExpression section(QStringLiteral("^\\s*\\[\\s*\"([^\"]*)\"\\s*\\]\\s*$"));
    static const QRegularExpression folder(QStringLiteral("^\\s*\"Folder\\s*\\d*\"\\s*=\\s*(.*)$"));
    static const QRegularExpression keyLine(QStringLiteral("^\\s*\"[^\"]*\"\\s*="));
    // Los textos entre comillas de una linea, unidos ("a" "b" -> "ab"). `\\` y `\"` son escapes; las demas
    // barras quedan (AE escribe rutas de Windows con barras simples).
    const auto quoted = [](const QString &line, bool *any) {
        QString out;
        *any = false;
        qsizetype k = 0;
        while (k < line.size()) {
            if (line.at(k) != QLatin1Char('"')) {
                ++k;
                continue;
            }
            *any = true;
            ++k;
            while (k < line.size() && line.at(k) != QLatin1Char('"')) {
                if (line.at(k) == QLatin1Char('\\') && k + 1 < line.size()
                    && (line.at(k + 1) == QLatin1Char('\\') || line.at(k + 1) == QLatin1Char('"'))) {
                    ++k;
                }
                out += line.at(k);
                ++k;
            }
            ++k;
        }
        return out;
    };
    QStringList folders;
    const QStringList lines = QString(prefsText).remove(QLatin1Char('\r')).split(QLatin1Char('\n'));
    bool inSection = false;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const QRegularExpressionMatch header = section.match(lines.at(i));
        if (header.hasMatch()) {
            inSection = header.captured(1).compare(QLatin1String("Disk Cache Controls"), Qt::CaseInsensitive) == 0;
            continue;
        }
        if (!inSection) {
            continue;
        }
        const QRegularExpressionMatch entry = folder.match(lines.at(i));
        if (!entry.hasMatch()) {
            continue;
        }
        bool any = false;
        QString value = quoted(entry.captured(1), &any);
        // Un valor largo puede seguir en las lineas de abajo: lineas con solo texto entre comillas (una
        // linea con `"clave" =` ya es otra entrada).
        while (i + 1 < lines.size()) {
            const QString &next = lines.at(i + 1);
            if (!next.trimmed().startsWith(QLatin1Char('"')) || keyLine.match(next).hasMatch()) {
                break;
            }
            bool more = false;
            value += quoted(next, &more);
            any = any || more;
            ++i;
        }
        if (any && !value.trimmed().isEmpty()) {
            folders.append(native(value.trimmed()));
        }
    }
    return folders;
}

QString abletonCacheDirectory(const QByteArray &cfg)
{
    static const QByteArray tag("FileCachePrefData");
    const qsizetype at = cfg.lastIndexOf(tag);
    if (at < 0) {
        return QString();
    }
    const qsizetype start = at + tag.size();
    const auto pathAt = [&cfg](qsizetype offset) -> QString {
        if (offset < 0 || offset + 4 > cfg.size()) {
            return QString();
        }
        const quint32 length = qFromLittleEndian<quint32>(cfg.constData() + offset);
        if (length < 3 || length > 1024 || offset + 4 + qsizetype(length) * 2 > cfg.size()) {
            return QString();
        }
        QStringDecoder decoder(QStringDecoder::Utf16LE);
        const QString text = decoder(cfg.mid(offset + 4, qsizetype(length) * 2));
        if (text.size() != qsizetype(length)) {
            return QString();
        }
        for (const QChar c : text) {
            if (c.unicode() < 0x20) {
                return QString();
            }
        }
        const bool drive = text.size() >= 3 && text.at(0).isLetter() && text.at(1) == QLatin1Char(':')
                           && (text.at(2) == QLatin1Char('/') || text.at(2) == QLatin1Char('\\'));
        if (!drive && !text.startsWith(QLatin1Char('/'))) {
            return QString();
        }
        return native(text);
    };
    // Live 11: tras el nombre van 13 bytes (version, minimo, si hay maximo, maximo) y despues el largo y
    // el texto. Si no cierra ahi (otra version), se busca el primer largo+ruta de los bytes que siguen.
    const QString expected = pathAt(start + 13);
    if (!expected.isEmpty()) {
        return expected;
    }
    for (qsizetype offset = start; offset <= start + 48; ++offset) {
        const QString found = pathAt(offset);
        if (!found.isEmpty()) {
            return found;
        }
    }
    return QString();
}

QString reaperPeaksFolder(const QString &ini)
{
    bool inSection = false;
    QString enabled;
    QString path;
    for (QString line : QString(ini).remove(QLatin1Char('\r')).split(QLatin1Char('\n'))) {
        line = line.trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            inSection = line.compare(QLatin1String("[REAPER]"), Qt::CaseInsensitive) == 0;
            continue;
        }
        if (!inSection) {
            continue;
        }
        const qsizetype equals = line.indexOf(QLatin1Char('='));
        if (equals < 0) {
            continue;
        }
        const QString key = line.left(equals).trimmed();
        const QString value = line.mid(equals + 1).trimmed();
        if (key.compare(QLatin1String("altpeaks"), Qt::CaseInsensitive) == 0) {
            enabled = value;
        } else if (key.compare(QLatin1String("altpeakspath"), Qt::CaseInsensitive) == 0) {
            path = value;
        }
    }
    // `altpeaks` es un campo de bits sin documentar: distinto de 0, la opcion esta activada.
    bool ok = false;
    const int flags = enabled.toInt(&ok);
    if (!ok || flags == 0 || path.isEmpty()) {
        return QString();
    }
    if (path.size() >= 2 && path.startsWith(QLatin1Char('"')) && path.endsWith(QLatin1Char('"'))) {
        path = path.mid(1, path.size() - 2);
    }
    return native(path);
}

QString unrealLocalDdcPath(const QString &ini)
{
    static const QRegularExpression line(QStringLiteral("^\\s*GlobalLocalDDCPath\\s*=\\s*(.*)$"), QRegularExpression::MultilineOption);
    static const QRegularExpression inner(QStringLiteral("Path\\s*=\\s*\"([^\"]*)\""));
    const QRegularExpressionMatch match = line.match(QString(ini).remove(QLatin1Char('\r')));
    if (!match.hasMatch()) {
        return QString();
    }
    QString value = match.captured(1).trimmed();
    if (value.startsWith(QLatin1Char('('))) {
        const QRegularExpressionMatch path = inner.match(value);
        value = path.hasMatch() ? path.captured(1) : QString();
    } else if (value.size() >= 2 && value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"'))) {
        value = value.mid(1, value.size() - 2);
    }
    return native(value.trimmed());
}

QString acceptedBase(const QString &path, const CleanupRules::Context &context)
{
    return acceptedBaseWith(path, fences(context));
}

bool hasProjectMarker(const QString &dir)
{
    if (dir.isEmpty()) {
        return false;
    }
    static const QStringList projects = {QStringLiteral("*.nk"),     QStringLiteral("*.nknc"),  QStringLiteral("*.hrox"),
                                         QStringLiteral("*.prproj"), QStringLiteral("*.aep"),   QStringLiteral("*.aepx"),
                                         QStringLiteral("*.drp"),    QStringLiteral("*.als"),   QStringLiteral("project.tvai"),
                                         QStringLiteral("*.hip"),    QStringLiteral("*.hipnc"), QStringLiteral("*.hiplc"),
                                         QStringLiteral("*.blend"),  QStringLiteral("*.c4d"),   QStringLiteral("*.uproject"),
                                         QStringLiteral("*.rpp"),    QStringLiteral("*.ma"),    QStringLiteral("*.mb")};
    return !QDir(dir).entryList(projects, QDir::Files | QDir::Hidden).isEmpty();
}

void add(QList<Cleanup::Category> &categories, const CleanupRules::Context &context)
{
    const Fences f = fences(context);
    Pending p = pending();
    addNuke(p, context, f);
    addAdobeMediaCache(p, context, f);
    addAfterEffects(p, context, f);
    addAbleton(p, context, f);
    addTopaz(p, context, f);
    addReaper(p, context, f);
    addUnreal(p, context, f);
    addRedshift(p, context);
    addPhotoshop(p, context);
    addCuda(p, context);
    dropCovered(categories, p);
    merge(categories, p.media, QString());
    merge(categories, p.nuke, QStringLiteral("media"));
    merge(categories, p.shaders, QString());
    merge(categories, p.render, QStringLiteral("nuke"));
    merge(categories, p.scratch, QStringLiteral("temp"));
    merge(categories, p.nukeWork, QString());
    merge(categories, p.previews, QString());
    merge(categories, p.downloads, QString());
}

void addScanned(QList<Cleanup::Category> &categories, const ScanTree &tree, const CleanupRules::Context &context)
{
    static const QString clip = QStringLiteral("CacheClip");
    static const QString optimized = QStringLiteral("OptimizedMedia");
    static const QString proxies = QStringLiteral("ProxyMedia");
    static const QString video = QStringLiteral("Adobe Premiere Pro Video Previews");
    static const QString audio = QStringLiteral("Adobe Premiere Pro Audio Previews");
    static const QString blender = QStringLiteral("BL_proxy");
    Pending p = pending();
    const ScanTree::Index count = ScanTree::Index(qMax(0, tree.nodeCount()));
    for (ScanTree::Index i = 1; i < count; ++i) {
        const ScanTree::Node &node = tree.node(i);
        // Primero el largo del nombre: casi ninguna carpeta pasa y no se arma su texto.
        const int length = node.nameLength;
        if (length != clip.size() && length != video.size() && length != blender.size()) {
            continue;
        }
        if ((node.flags & ScanTree::Removed) != 0) {
            continue;
        }
        const QString name = tree.name(i);
        const bool isClip = name.compare(clip, kPathCase) == 0;
        const bool isPreview = name.compare(video, kPathCase) == 0 || name.compare(audio, kPathCase) == 0;
        const bool isProxy = name.compare(blender, kPathCase) == 0;
        if ((!isClip && !isPreview && !isProxy) || !tree.isAlive(i)) {
            continue;
        }
        const QString path = tree.path(i);
        if (isClip) {
            // La cache de render de Resolve: `CacheClip` con sus `.dvcc`. Sus hermanas `OptimizedMedia` y
            // `ProxyMedia` son del usuario; `.gallery` y la base de proyectos nunca se ofrecen.
            if (!hasFileWithin(path, 4, {QStringLiteral("dvcc")})) {
                continue;
            }
            offer(p.media, I18n::tr("DaVinci Resolve render cache"), path, {path}, {}, context, resolveBlockers(),
                  QStringLiteral("DaVinci Resolve"));
            for (const ScanTree::Index sibling : tree.children(node.parent)) {
                const QString siblingName = tree.name(sibling);
                if (siblingName.compare(optimized, kPathCase) == 0) {
                    const QString dir = tree.path(sibling);
                    offer(p.previews, I18n::tr("DaVinci Resolve optimized media"), dir, {dir}, {}, context, resolveBlockers(),
                          QStringLiteral("DaVinci Resolve"));
                } else if (siblingName.compare(proxies, kPathCase) == 0) {
                    const QString dir = tree.path(sibling);
                    offer(p.previews, I18n::tr("DaVinci Resolve proxies"), dir, {dir}, {}, context, resolveBlockers(),
                          QStringLiteral("DaVinci Resolve"));
                }
            }
        } else if (isPreview) {
            offer(p.previews, I18n::tr("Premiere Pro previews"), path, {path}, {}, context, premiereBlockers(), QStringLiteral("Premiere Pro"));
        } else {
            offer(p.previews, I18n::tr("Blender proxies"), path, {path}, {}, context, blenderBlockers(), QStringLiteral("Blender"));
        }
    }
    merge(categories, p.media, QString());
    merge(categories, p.previews, QString());
}

} // namespace AppCacheRules
