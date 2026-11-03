#include "modules/diskspace/cleanup/CleanupFixture.h"

#include "core/I18n.h"
#include "modules/diskspace/cleanup/CleanupRules.h"

#include <QDateTime>
#include <QDir>

namespace {

constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;
constexpr qint64 kDay = 86400;

qint64 gb(double value)
{
    return qint64(value * kGiB);
}

// Una carpeta del disco de mentira: nivel, nombre, GiB del subarbol, archivos del subarbol y dias desde
// el ultimo cambio. En el orden en que se recorre un arbol (cada una despues de su madre).
struct Dir
{
    int depth;
    const char16_t *name;
    double gib;
    int files;
    int days;
};

#ifdef Q_OS_MACOS
// macOS: el disco de arranque visto desde "/", con el volumen de datos entrando por /Users, /Applications...
const Dir kDirs[] = {
    {0, u"Users", 612.0, 2990600, 0},
    {1, u"lega", 611.2, 2989700, 0},
    {2, u"Library", 318.4, 1500300, 0},
    {3, u"Application Support", 151.6, 610400, 0},
    {4, u"Studio", 14.5, 60210, 0},
    {4, u"Google", 9.8, 40042, 0},
    {4, u"BraveSoftware", 6.2, 30877, 0},
    {4, u"Blackmagic Design", 5.1, 292, 5},
    {3, u"Caches", 12.0, 90120, 0},
    {4, u"com.openai.codex", 1.6, 4332, 2},
    {4, u"BraveSoftware", 1.6, 30120, 0},
    {4, u"Google", 1.5, 9230, 0},
    {4, u"pip", 0.5, 40, 13},
    {3, u"Containers", 40.2, 201400, 0},
    {2, u"Desktop", 95.3, 160142, 1},
    {2, u"Movies", 60.1, 3004, 3},
    {2, u".nuke", 29.0, 91507, 0},
    {2, u"Downloads", 25.2, 3120, 1},
    {2, u".codex", 12.5, 17444, 0},
    {2, u"Documents", 6.3, 29250, 0},
    {2, u".cache", 3.6, 22736, 4},
    {0, u"Applications", 72.8, 594358, 0},
    {1, u"Nuke17.0v1", 8.9, 40210, 40},
    {1, u"DaVinci Resolve", 6.0, 20311, 30},
    {0, u"Library", 30.9, 137466, 0},
    {0, u"System", 28.9, 309019, 0},
    {0, u"private", 11.7, 12933, 0},
    {0, u"opt", 4.6, 93869, 0},
    {0, u"usr", 1.7, 23240, 0},
};
#else
const Dir kDirs[] = {
    {0, u"Users", 228.8, 1009800, 0},
    {1, u"lega", 221.2, 1003700, 0},
    {2, u"AppData", 125.9, 454700, 0},
    {3, u"Local", 90.2, 320077, 0},
    {4, u"Packages", 25.8, 70957, 0},
    {4, u"pip", 10.4, 1318, 7},
    {4, u"Microsoft", 7.0, 14455, 0},
    {4, u"Spark Desktop", 5.7, 14855, 0},
    {4, u"Google", 5.3, 24042, 0},
    {4, u"Temp", 5.0, 40378, 0},
    {3, u"Roaming", 32.9, 127600, 0},
    {4, u"Adobe", 8.4, 28650, 0},
    {4, u"Blackmagic Design", 5.1, 292, 5},
    {2, u".nuke", 29.0, 91507, 0},
    {2, u"Desktop", 25.2, 159142, 1},
    {2, u".codex", 12.5, 17444, 0},
    {2, u"Documents", 6.3, 29250, 0},
    {2, u".cache", 3.6, 22736, 4},
    {2, u"miniconda3", 3.3, 88696, 21},
    {1, u"Public", 7.5, 6068, 1},
    {0, u"Portable", 207.9, 667336, 0},
    {1, u"LGA_ShotPlayer", 36.0, 19231, 3},
    {1, u"LGA_MediaTools_v2", 35.7, 127524, 0},
    {1, u"LGA_SceneBuilder", 33.0, 67250, 6},
    {1, u"LGA_PipeSync_2", 18.5, 50762, 0},
    {0, u"Program Files", 179.4, 681604, 0},
    {1, u"Adobe", 39.2, 83115, 13},
    {1, u"Topaz Labs LLC", 22.8, 182411, 48},
    {0, u"Windows", 80.7, 205041, 0},
    {1, u"Installer", 34.0, 1819, 4},
    {1, u"WinSxS", 17.8, 132274, 2},
    {0, u"ProgramData", 48.4, 220136, 0},
    {0, u"Qt", 40.2, 536035, 7},
    {0, u"Program Files (x86)", 31.5, 124969, 0},
    {0, u"DevTools", 21.7, 1120198, 1},
    {1, u"cache", 21.6, 1116848, 1},
    {0, u"$Recycle.Bin", 9.1, 861, 0},
    {0, u"temp", 7.3, 4245, 15},
};
#endif
constexpr int kDirCount = int(sizeof(kDirs) / sizeof(kDirs[0]));

// Agrega las carpetas de `kDirs` que cuelgan de `parent` (las del nivel `depth` a partir de `index`).
// Devuelve el indice de la primera que ya no es suya.
int addLevel(ScanTree &tree, ScanTree::Index parent, int depth, int index, qint64 now, qint64 *bytesUsed, int *filesUsed)
{
    struct Child
    {
        ScanTree::Index node;
        int row;
        int next;
    };
    QList<Child> children;
    int i = index;
    while (i < kDirCount && kDirs[i].depth == depth) {
        const ScanTree::Index node = tree.addChild(parent, QStringView(kDirs[i].name));
        int end = i + 1;
        while (end < kDirCount && kDirs[end].depth > depth) {
            ++end;
        }
        children.append(Child{node, i, end});
        *bytesUsed += gb(kDirs[i].gib);
        *filesUsed += kDirs[i].files;
        i = end;
    }
    for (const Child &child : children) {
        qint64 inner = 0;
        int innerFiles = 0;
        addLevel(tree, child.node, depth + 1, child.row + 1, now, &inner, &innerFiles);
        const Dir &dir = kDirs[child.row];
        // Lo propio es lo que no esta en sus subcarpetas listadas.
        tree.setListed(child.node, quint64(qMax<qint64>(0, gb(dir.gib) - inner)), quint32(qMax(0, dir.files - innerFiles)),
                       now - dir.days * kDay, false, false);
    }
    return i;
}

Cleanup::Item item(const QString &name, const QString &path, double gib, int days, bool checked, qint64 now)
{
    Cleanup::Item it;
    it.name = name;
    it.path = path;
    it.targets.append(Cleanup::Target{path, Cleanup::Action::Contents, 0});
    it.measured = true;
    it.bytes = gb(gib);
    it.files = 1;
    it.newest = days < 0 ? 0 : now - days * kDay;
    it.checked = checked;
    return it;
}

// Una carpeta cuya app esta abierta: cuenta en el total pero no se puede tildar.
Cleanup::Item inUse(Cleanup::Item it, const QString &app)
{
    it.blocked = true;
    it.blockerLabel = app;
    return it;
}

// Un item de varias carpetas: se muestra lo que se borra, no la carpeta que las junta.
Cleanup::Item shown(Cleanup::Item it, const QString &detail)
{
    it.detail = QDir::toNativeSeparators(detail);
    return it;
}

Cleanup::Category category(const QString &id, Cleanup::Group group, const QString &title, const QString &caption,
                           const QList<Cleanup::Item> &items, bool single = false)
{
    Cleanup::Category c;
    c.id = id;
    c.group = group;
    c.title = title;
    c.caption = caption;
    c.single = single;
    c.items = items;
    return c;
}

// Las caches de los programas de video, audio y VFX (AppCacheRules), como las arma la ventana con las
// preferencias leidas: "cleanup-apps" con las de medios abiertas, "cleanup-apps-nuke" con las de Nuke y
// Redshift, y "cleanup-apps-yours" con las del usuario.
QList<Cleanup::Category> appCategories(const QString &state, qint64 now)
{
#ifdef Q_OS_MACOS
    const QString home = QStringLiteral("/Users/lega");
    const QString root = QString();
    const QString caches = home + QStringLiteral("/Library/Caches");
    const QString prefs = home + QStringLiteral("/Library/Preferences");
    const QString adobeCommon = home + QStringLiteral("/Library/Application Support/Adobe/Common");
    const QString aeCache = caches + QStringLiteral("/Adobe/After Effects/2025/Disk Cache - lega.noindex");
    const QString ableton = caches + QStringLiteral("/Ableton/Cache/Decoding");
    const QString packs = caches + QStringLiteral("/Ableton/PackDownloads");
#else
    const QString home = QStringLiteral("C:/Users/lega");
    const QString root = QStringLiteral("C:");
    const QString prefs = home + QStringLiteral("/AppData/Roaming");
    const QString adobeCommon = prefs + QStringLiteral("/Adobe/Common");
    const QString aeCache = home + QStringLiteral("/AppData/Local/Temp/Disk Cache - LEGA-PC");
    const QString ableton = home + QStringLiteral("/AppData/Local/Ableton/Cache/Cache/Decoding");
    const QString packs = home + QStringLiteral("/AppData/Local/Ableton/Cache/PackDownloads");
#endif
    const auto at = [](const QString &path) { return QDir::toNativeSeparators(path); };
    const QString nuke = root + QStringLiteral("/Nuke_Cache");
    const QString resolve = root + QStringLiteral("/Resolve");
    const QString redshift = prefs + QStringLiteral("/Maxon/Maxon Cinema 4D 2024_A1B2C3D4/Redshift/Cache");
    QList<Cleanup::Category> list;

    if (!state.endsWith(QLatin1String("-yours"))) {
        list.append(category(QStringLiteral("bin"), Cleanup::Group::Safe, I18n::tr("Recycle Bin"),
                             I18n::tr("%1 items already deleted once.").arg(QStringLiteral("861")),
                             {item(I18n::tr("Recycle Bin"), at(root + QStringLiteral("/")), 9.06, 0, true, now)}, true));
        QList<Cleanup::Item> media = {
            item(QStringLiteral("Adobe Media Cache"), at(adobeCommon), 1.33, 1, true, now),
            item(QStringLiteral("Adobe Media Cache"), at(root + QStringLiteral("/MediaCache")), 4.12, 2, true, now),
            item(I18n::tr("After Effects disk cache"), at(aeCache), 6.85, 3, true, now),
            item(I18n::tr("DaVinci Resolve render cache"), at(resolve + QStringLiteral("/CacheClip")), 18.4, 5, true, now),
            item(I18n::tr("Ableton Live decoding cache"), at(ableton), 1.21, 40, true, now),
            shown(item(I18n::tr("REAPER peak files"), at(root + QStringLiteral("/ReaperPeaks")), 0.31, 12, true, now), root + QStringLiteral("/ReaperPeaks/*/*.reapeaks")),
        };
#ifndef Q_OS_MACOS
        media.append(shown(item(I18n::tr("Topaz Video thumbnails"), at(home + QStringLiteral("/Documents/Topaz Video Projects")), 0.02, 1, true, now),
                           home + QStringLiteral("/Documents/Topaz Video Projects/*/thumbnails")));
#endif
        list.append(category(QStringLiteral("media"), Cleanup::Group::Safe, I18n::tr("Media caches"), I18n::tr("Their programs rebuild them when they need them."), media));
        QList<Cleanup::Item> nukeItems = {
            item(I18n::tr("Audio cache"), at(nuke + QStringLiteral("/audiocache")), 2.41, 0, true, now),
            item(I18n::tr("Viewer cache"), at(nuke + QStringLiteral("/ViewerCache")), 0.57, 0, true, now),
            item(I18n::tr("Thumbnails"), at(nuke + QStringLiteral("/thumbnailcache")), 0.07, 0, true, now),
            item(I18n::tr("Roto and Paint tiles"), at(nuke + QStringLiteral("/tilecache")), 0.006, 2, true, now),
            item(I18n::tr("Read blocks"), at(nuke + QStringLiteral("/jitCache-LEGA-PC/BlockCache")), 0.0004, 0, true, now),
        };
#ifndef Q_OS_MACOS
        nukeItems.append(item(I18n::tr("CUDA kernels"), at(prefs + QStringLiteral("/NVIDIA/NukeComputeCache")), 0.0001, 0, true, now));
#endif
        list.append(category(QStringLiteral("nuke"), Cleanup::Group::Safe, I18n::tr("Nuke caches"),
                             I18n::tr("Viewer, audio, thumbnail, paint, read-block and CUDA caches. Nuke fills them again as you work."), nukeItems));
        list.append(category(QStringLiteral("rendercache"), Cleanup::Group::Safe, I18n::tr("Render caches"),
                             I18n::tr("Converted textures and compiled shaders. The renderer makes them again on the next render."),
                             {inUse(item(I18n::tr("Redshift cache"), at(redshift), 3.62, 0, true, now), QStringLiteral("Cinema 4D"))}));
        list.append(category(QStringLiteral("pstemp"), Cleanup::Group::Safe, I18n::tr("Photoshop scratch files"),
                             I18n::tr("Left at the root of a scratch disk when Photoshop closes unexpectedly."),
                             {shown(item(I18n::tr("Photoshop scratch files"), at(root + QStringLiteral("/")), 2.0, 9, true, now), root + QStringLiteral("/Photoshop Temp*"))}, true));
    }

    list.append(category(QStringLiteral("nukework"), Cleanup::Group::Yours, I18n::tr("Nuke timeline cache and local copies"),
                         I18n::tr("Frames sent to the timeline cache and local copies of footage. Nuke makes them again, but it takes time."),
                         {shown(item(I18n::tr("Timeline cache"), at(nuke), 12.3, 0, false, now), nuke + QStringLiteral("/timeline_cache.db + 00–ff")),
                          item(I18n::tr("Localized files"), at(nuke + QStringLiteral("/jitCache-LEGA-PC/LocalisedFiles")), 4.4, 6, false, now)}));
    QList<Cleanup::Item> previews = {
        item(I18n::tr("DaVinci Resolve optimized media"), at(resolve + QStringLiteral("/OptimizedMedia")), 31.2, 5, false, now),
        item(I18n::tr("DaVinci Resolve proxies"), at(resolve + QStringLiteral("/ProxyMedia")), 9.7, 5, false, now),
        item(I18n::tr("Premiere Pro previews"), at(root + QStringLiteral("/Edits/Spot_2026/Adobe Premiere Pro Video Previews")), 5.3, 11, false, now),
        item(I18n::tr("Blender proxies"), at(root + QStringLiteral("/Footage/Plates/BL_proxy")), 0.9, 30, false, now),
    };
    QList<Cleanup::Item> downloads;
#ifndef Q_OS_MACOS
    previews.insert(3, shown(item(I18n::tr("Topaz Video previews"), at(home + QStringLiteral("/Documents/Topaz Video Projects")), 1.1, 1, false, now),
                             home + QStringLiteral("/Documents/Topaz Video Projects/*/previews")));
    downloads.append(item(I18n::tr("Topaz Video models"), at(QStringLiteral("C:/ProgramData/Topaz Labs LLC/Topaz Video/models/models")), 22.75, 1, false, now));
    downloads.append(shown(item(I18n::tr("Unreal Engine derived data"), at(home + QStringLiteral("/AppData/Local/UnrealEngine/Common/DerivedDataCache")), 14.2, 20, false, now),
                           home + QStringLiteral("/AppData/Local/UnrealEngine/Common/DerivedDataCache/0–9")));
#endif
    downloads.append(item(I18n::tr("Ableton Live pack downloads"), at(packs), 2.3, 60, false, now));
    list.append(category(QStringLiteral("previews"), Cleanup::Group::Yours, I18n::tr("Previews, proxies and optimized media"),
                         I18n::tr("Made by video apps from your footage. They can be made again, but it takes render time."), previews));
    list.append(category(QStringLiteral("appdownloads"), Cleanup::Group::Yours, I18n::tr("Downloads and derived data"),
                         I18n::tr("Each program downloads or builds them again when it needs them, which can take a long time."), downloads));
    return list;
}

#ifdef Q_OS_MACOS
QList<Cleanup::Category> categories(const QString &state, qint64 now)
{
    if (state.startsWith(QLatin1String("cleanup-apps"))) {
        return appCategories(state, now);
    }
    const QString home = QStringLiteral("/Users/lega");
    const QString codin = home + QStringLiteral("/Desktop/Codin");
    QList<Cleanup::Category> list;
    const bool pick = state == QLatin1String("cleanup-pick");
    const bool result = state == QLatin1String("cleanup-result");

    if (!pick && !result) {
        list.append(category(QStringLiteral("bin"), Cleanup::Group::Safe, I18n::tr("Trash"),
                             I18n::tr("%1 items already deleted once.").arg(QStringLiteral("25")),
                             {item(I18n::tr("Trash"), QStringLiteral("/"), 2.49, 0, true, now)}, true));
        list.append(category(QStringLiteral("python"), Cleanup::Group::Safe, I18n::tr("Python package caches"),
                             I18n::tr("Downloads kept by pip and uv. Installing a package fetches it again."),
                             {item(QStringLiteral("uv cache"), home + QStringLiteral("/.cache/uv"), 3.21, 4, true, now),
                              item(QStringLiteral("pip cache"), home + QStringLiteral("/Library/Caches/pip"), 0.47, 13, true, now)}));
        list.append(category(QStringLiteral("dev"), Cleanup::Group::Safe, I18n::tr("Developer caches"),
                             I18n::tr("Package downloads. Fetched again by the next install or build."),
                             {item(QStringLiteral("npm cache"), home + QStringLiteral("/.npm/_cacache"), 0.16, 0, true, now),
                              item(QStringLiteral("Homebrew downloads"), home + QStringLiteral("/Library/Caches/Homebrew"), 0.04, 0, true, now)}));
        list.append(category(QStringLiteral("browsers"), Cleanup::Group::Safe, I18n::tr("Browser caches"),
                             I18n::tr("Cache only: history, cookies and logins stay."),
                             {item(QStringLiteral("Brave"), home + QStringLiteral("/Library/Application Support/BraveSoftware/Brave-Browser"), 1.61, 0, true, now),
                              item(QStringLiteral("Chrome"), home + QStringLiteral("/Library/Application Support/Google/Chrome"), 1.36, 0, true, now),
                              item(QStringLiteral("Firefox"), home + QStringLiteral("/Library/Caches/Firefox/Profiles"), 0.10, 14, true, now)}));
        list.append(category(QStringLiteral("apps"), Cleanup::Group::Safe, I18n::tr("App caches"),
                             I18n::tr("Cache folders of desktop apps. Settings and logins stay."),
                             {inUse(item(QStringLiteral("Studio"), home + QStringLiteral("/Library/Application Support/Studio"), 1.52, 0, true, now), QStringLiteral("Studio")),
                              inUse(item(QStringLiteral("Akiflow"), home + QStringLiteral("/Library/Application Support/Akiflow"), 0.24, 0, true, now), QStringLiteral("Akiflow")),
                              item(QStringLiteral("Spark Desktop"), home + QStringLiteral("/Library/Application Support/Spark Desktop"), 0.23, 0, true, now),
                              item(QStringLiteral("Code"), home + QStringLiteral("/Library/Application Support/Code"), 0.09, 0, true, now)}));
        list.append(category(QStringLiteral("updates"), Cleanup::Group::Safe, I18n::tr("App update downloads"),
                             I18n::tr("Updates apps already downloaded. They download them again if they need them."),
                             {item(QStringLiteral("com.openai.codex"), home + QStringLiteral("/Library/Caches/com.openai.codex/org.sparkle-project.Sparkle"), 1.65, 2, true, now),
                              item(QStringLiteral("com.microsoft.VSCode"), home + QStringLiteral("/Library/Caches/com.microsoft.VSCode.ShipIt"), 0.87, 1, true, now),
                              item(QStringLiteral("com.brave.Browser"), home + QStringLiteral("/Library/Caches/com.brave.Browser/org.sparkle-project.Sparkle"), 0.79, 5, true, now),
                              item(QStringLiteral("Firefox"), home + QStringLiteral("/Library/Caches/Mozilla/updates"), 0.65, 14, true, now)}));
        list.append(category(QStringLiteral("temp"), Cleanup::Group::Safe, I18n::tr("Temporary files"),
                             I18n::tr("Older than 7 days and not in use.") + QLatin1Char(' ')
                                 + I18n::tr("%1 newer or in use are left alone.").arg(QStringLiteral("1.91 GB")),
                             {item(I18n::tr("Temporary files"), QStringLiteral("/private/var/folders/cx/qtyt758157x8qm7xl4fdjmqc0000gn/T"), 0.41, 9, true, now)},
                             true));
    }

    // ---- Yours to decide: las reglas de carpetas del usuario y lo demas.
    QList<Cleanup::Item> builds = {
        item(QStringLiteral("LGA_PipeSync_2"), codin + QStringLiteral("/LGA_PipeSync_2/build"), 5.46, 0, false, now),
        item(QStringLiteral("LGA_MightyTools"), codin + QStringLiteral("/LGA_MightyTools/build"), 2.10, 8, pick, now),
        item(QStringLiteral("LGA_FileManagerS3"), codin + QStringLiteral("/LGA_FileManagerS3/build"), 1.72, 0, false, now),
        item(QStringLiteral("LGA_MediaTools_v2"), codin + QStringLiteral("/LGA_MediaTools_v2/build"), 1.29, 8, pick, now),
        item(QStringLiteral("LGA_FrameRev"), codin + QStringLiteral("/LGA_FrameRev/build"), 0.75, 0, false, now),
    };
    list.append(category(QStringLiteral("rule:0"), Cleanup::Group::Yours, I18n::tr("Folders named %1").arg(QStringLiteral("build")),
                         I18n::tr("%1 folders named %2 in %3.").arg(5).arg(QStringLiteral("build"), codin), builds));
    if (!pick) {
        list.append(category(QStringLiteral("vm"), Cleanup::Group::Yours, I18n::tr("Virtual machine images"),
                             I18n::tr("Kept by desktop apps, one copy per profile. The app downloads the image again; what its sessions saved inside is lost."),
                             {item(QStringLiteral("Studio"), home + QStringLiteral("/Library/Application Support/Studio/vm_bundles"), 9.26, 1, false, now)}));
        list.append(category(QStringLiteral("codex"), Cleanup::Group::Yours, I18n::tr("Codex archived sessions"),
                             I18n::tr("Old conversations kept by Codex."),
                             {item(I18n::tr("Codex archived sessions"), home + QStringLiteral("/.codex/archived_sessions"), 0.03, 6, false, now)},
                             true));
        list.append(category(QStringLiteral("appcaches"), Cleanup::Group::Yours, I18n::tr("Other app caches"),
                             I18n::tr("Most rebuild themselves, but some apps keep their own data here too. Tick only what you know."),
                             {item(QStringLiteral("Adobe"), home + QStringLiteral("/Library/Caches/Adobe"), 0.47, 13, false, now),
                              item(QStringLiteral("Spark Desktop"), home + QStringLiteral("/Library/Caches/Spark Desktop"), 0.27, 0, false, now),
                              item(QStringLiteral("vscode-cpptools"), home + QStringLiteral("/Library/Caches/vscode-cpptools"), 0.24, 0, false, now)}));
    }
    return list;
}

#else
// Las copias huerfanas de %WINDIR%\Installer, armadas con la misma funcion que usan las reglas. Datos de
// mentira: paquetes de programas que ya NO estan instalados (los de un programa instalado, en cualquier
// version, nunca se listan). `skip`: las que ya se movieron.
Cleanup::Category orphanedInstallers(qint64 now, InstallerOrphans::Status status, int skip = 0)
{
    const QString dir = QStringLiteral("C:\\Windows\\Installer");
    struct Orphan
    {
        const char *name;
        const char *author;
        const char *subject;
        double gib;
        int days;
    };
    const Orphan orphans[] = {
        {"5be0d12.msi", "Maxon Computer GmbH", "Red Giant Universe 2023.2", 1.12, 412},
        {"77c13a8.msi", "Autodesk", "Autodesk Desktop Connector 16.4", 0.312, 290},
        {"a4f21c9.msi", "Blackmagic Design", "Blackmagic RAW 3.6", 0.208, 520},
        {"2e19b40.msp", "Microsoft Corporation", "Update for Office 2016 (KB4484103)", 0.0442, 830},
        {"14d9e02.msi", "Microsoft Corporation", "Visual C++ 2019 X64 Minimum Runtime", 0.000196, 905},
        {"14d9e21.msi", "Microsoft Corporation", "Visual C++ 2019 X64 Additional Runtime", 0.000196, 905},
    };
    InstallerOrphans::Verdict verdict;
    verdict.status = status;
    if (status == InstallerOrphans::Status::Ok) {
        int index = 0;
        for (const Orphan &orphan : orphans) {
            if (index++ < skip) {
                continue;
            }
            InstallerPackages::FileFacts file;
            file.name = QLatin1String(orphan.name);
            file.path = dir + QLatin1Char('\\') + file.name;
            file.author = QLatin1String(orphan.author);
            file.subject = QLatin1String(orphan.subject);
            file.bytes = gb(orphan.gib);
            file.created = now - orphan.days * kDay;
            file.modified = file.created;
            verdict.orphans.append(file);
            verdict.bytes += file.bytes;
        }
    }
    Cleanup::Category category = CleanupRules::installerCategory(verdict, dir);
    for (Cleanup::Item &item : category.items) {
        item.measured = true;
    }
    return category;
}

QList<Cleanup::Category> categories(const QString &state, qint64 now)
{
    if (state.startsWith(QLatin1String("cleanup-apps"))) {
        return appCategories(state, now);
    }
    const QString home = QStringLiteral("C:\\Users\\lega");
    const QString portable = QStringLiteral("C:\\Portable");
    QList<Cleanup::Category> list;
    const bool pick = state == QLatin1String("cleanup-pick");
    const bool result = state == QLatin1String("cleanup-result");

    // Los estados de los instaladores huerfanos: solo "Yours to decide", para que la lista se vea entera.
    if (state == QLatin1String("cleanup-installers") || state == QLatin1String("cleanup-installers-denied")) {
        Cleanup::Category orphans = orphanedInstallers(now, InstallerOrphans::Status::Ok);
        orphans.items[0].checked = true;
        orphans.items[1].checked = true;
        list.append(orphans);
        return list;
    }
    if (state == QLatin1String("cleanup-installers-result")) {
        list.append(orphanedInstallers(now, InstallerOrphans::Status::Ok, 2));
        return list;
    }
    if (state == QLatin1String("cleanup-installers-admin")) {
        list.append(category(QStringLiteral("rule:0"), Cleanup::Group::Yours, I18n::tr("Folders named %1").arg(QStringLiteral("build")),
                             I18n::tr("%1 folders named %2 in %3.").arg(2).arg(QStringLiteral("build"), portable),
                             {item(QStringLiteral("LGA_PipeSync_2"), portable + QStringLiteral("\\LGA_PipeSync_2\\build"), 5.46, 0, false, now),
                              item(QStringLiteral("LGA_FrameRev"), portable + QStringLiteral("\\LGA_FrameRev\\build"), 0.75, 0, false, now)}));
        // La de administrador y la de una lectura que fallo: las dos lineas sin casilla.
        list.append(orphanedInstallers(now, InstallerOrphans::Status::NeedsAdmin));
        return list;
    }
    if (state == QLatin1String("cleanup-installers-unverified") || state == QLatin1String("cleanup-installers-unfinished")) {
        list.append(category(QStringLiteral("rule:0"), Cleanup::Group::Yours, I18n::tr("Folders named %1").arg(QStringLiteral("build")),
                             I18n::tr("%1 folders named %2 in %3.").arg(2).arg(QStringLiteral("build"), portable),
                             {item(QStringLiteral("LGA_PipeSync_2"), portable + QStringLiteral("\\LGA_PipeSync_2\\build"), 5.46, 0, false, now),
                              item(QStringLiteral("LGA_FrameRev"), portable + QStringLiteral("\\LGA_FrameRev\\build"), 0.75, 0, false, now)}));
        list.append(orphanedInstallers(now, state.endsWith(QLatin1String("unfinished")) ? InstallerOrphans::Status::Unfinished
                                                                                         : InstallerOrphans::Status::Unverified));
        return list;
    }

    if (!pick && !result) {
        list.append(category(QStringLiteral("bin"), Cleanup::Group::Safe, I18n::tr("Recycle Bin"),
                             I18n::tr("%1 items already deleted once.").arg(QStringLiteral("861")),
                             {item(I18n::tr("Recycle Bin"), QStringLiteral("C:\\"), 9.06, 0, true, now)}, true));
        list.append(category(QStringLiteral("python"), Cleanup::Group::Safe, I18n::tr("Python package caches"),
                             I18n::tr("Downloads kept by pip and uv. Installing a package fetches it again."),
                             {item(QStringLiteral("uv cache"), QStringLiteral("C:\\DevTools\\cache"), 21.56, 1, true, now),
                              item(QStringLiteral("pip cache"), home + QStringLiteral("\\AppData\\Local\\pip\\cache"), 10.35, 7, true, now)}));
        list.append(category(QStringLiteral("media"), Cleanup::Group::Safe, I18n::tr("Media caches"), I18n::tr("Their programs rebuild them when they need them."),
                             {item(QStringLiteral("Adobe Media Cache"), home + QStringLiteral("\\AppData\\Roaming\\Adobe\\Common"), 1.33, 1, true, now)}));
        list.append(category(QStringLiteral("dev"), Cleanup::Group::Safe, I18n::tr("Developer caches"),
                             I18n::tr("Package downloads. Fetched again by the next install or build."),
                             {item(QStringLiteral("Cargo registry"), home + QStringLiteral("\\.cargo\\registry"), 2.18, 40, true, now),
                              item(QStringLiteral("npm cache"), home + QStringLiteral("\\AppData\\Local\\npm-cache\\_cacache"), 0.59, 2, true, now)}));
        list.append(category(QStringLiteral("browsers"), Cleanup::Group::Safe, I18n::tr("Browser caches"),
                             I18n::tr("Cache only: history, cookies and logins stay."),
                             {item(QStringLiteral("Brave"), home + QStringLiteral("\\AppData\\Local\\BraveSoftware\\Brave-Browser\\User Data"), 2.24, 0, true, now),
                              item(QStringLiteral("Chrome"), home + QStringLiteral("\\AppData\\Local\\Google\\Chrome\\User Data"), 1.48, 1, true, now),
                              item(QStringLiteral("Edge"), home + QStringLiteral("\\AppData\\Local\\Microsoft\\Edge\\User Data"), 0.18, 3, true, now)}));
        list.append(category(QStringLiteral("apps"), Cleanup::Group::Safe, I18n::tr("App caches"),
                             I18n::tr("Cache folders of desktop apps. Settings and logins stay."),
                             {item(QStringLiteral("ClickUp"), home + QStringLiteral("\\AppData\\Roaming\\ClickUp"), 0.54, 125, true, now),
                              item(QStringLiteral("Acrobat"), home + QStringLiteral("\\AppData\\Local\\Adobe\\Acrobat"), 0.35, 9, true, now),
                              inUse(item(QStringLiteral("Todoist"), home + QStringLiteral("\\AppData\\Roaming\\Todoist"), 0.30, 0, true, now), QStringLiteral("Todoist")),
                              item(QStringLiteral("Spark Desktop"), home + QStringLiteral("\\AppData\\Local\\Spark Desktop"), 0.21, 0, true, now),
                              item(QStringLiteral("discord"), home + QStringLiteral("\\AppData\\Roaming\\discord"), 0.18, 400, true, now)}));
        list.append(category(QStringLiteral("temp"), Cleanup::Group::Safe, I18n::tr("Temporary files"),
                             I18n::tr("Older than 7 days and not in use.") + QLatin1Char(' ')
                                 + I18n::tr("%1 newer or in use are left alone.").arg(QStringLiteral("3.90 GB")),
                             {item(I18n::tr("Temporary files"), home + QStringLiteral("\\AppData\\Local\\Temp"), 1.10, 13, true, now)}, true));

        Cleanup::Category dump = category(QStringLiteral("memdump"), Cleanup::Group::Admin, I18n::tr("System memory dump"),
                                          I18n::tr("Written by Windows when it crashed."),
                                          {item(I18n::tr("System memory dump"), QStringLiteral("C:\\Windows\\MEMORY.DMP"), 6.84, 4, false, now)}, true);
        dump.info = true;
        list.append(dump);
        Cleanup::Category update = category(QStringLiteral("winupdate"), Cleanup::Group::Admin, I18n::tr("Windows Update leftovers"),
                                            I18n::tr("Old versions of system components. Windows removes them itself."),
                                            {item(I18n::tr("Windows Update leftovers"), QStringLiteral("C:\\Windows"), 0, -1, false, now)}, true);
        update.info = true;
        update.measurable = false;
        list.append(update);
    }

    // ---- Yours to decide: las reglas de carpetas del usuario y lo demas.
    QList<Cleanup::Item> builds = {
        item(QStringLiteral("LGA_PipeSync_2"), portable + QStringLiteral("\\LGA_PipeSync_2\\build"), 5.46, 0, false, now),
        item(QStringLiteral("vendor\\LGA_PipeSync_2"), portable + QStringLiteral("\\vendor\\LGA_PipeSync_2\\build"), 3.46, 8, pick, now),
        item(QStringLiteral("LGA_FileManagerS3"), portable + QStringLiteral("\\LGA_FileManagerS3\\build"), 1.72, 0, false, now),
        item(QStringLiteral("vendor\\LGA_FileManagerS3"), portable + QStringLiteral("\\vendor\\LGA_FileManagerS3\\build"), 1.71, 8, pick, now),
        item(QStringLiteral("vendor\\LGA_MediaTools_v2"), portable + QStringLiteral("\\vendor\\LGA_MediaTools_v2\\build"), 1.29, 8, pick, now),
        item(QStringLiteral("LGA_MediaTools_v2"), portable + QStringLiteral("\\LGA_MediaTools_v2\\build"), 1.29, 0, false, now),
        item(QStringLiteral("LGA_FrameRev"), portable + QStringLiteral("\\LGA_FrameRev\\build"), 0.75, 0, false, now),
    };
    list.append(category(QStringLiteral("rule:0"), Cleanup::Group::Yours, I18n::tr("Folders named %1").arg(QStringLiteral("build")),
                         I18n::tr("%1 folders named %2 in %3.").arg(7).arg(QStringLiteral("build"), portable), builds));
    QList<Cleanup::Item> installers = {
        item(QStringLiteral("LGA_FileManagerS3"), portable + QStringLiteral("\\LGA_FileManagerS3\\installer"), 5.64, 9, false, now),
        item(QStringLiteral("LGA_PipeSync_2"), portable + QStringLiteral("\\LGA_PipeSync_2\\installer"), 3.82, 0, false, now),
        item(QStringLiteral("LGA_MediaTools_v2"), portable + QStringLiteral("\\LGA_MediaTools_v2\\installer"), 3.76, 6, false, now),
    };
    list.append(category(QStringLiteral("rule:1"), Cleanup::Group::Yours, I18n::tr("Folders named %1").arg(QStringLiteral("installer")),
                         I18n::tr("%1 folders named %2 in %3.").arg(3).arg(QStringLiteral("installer"), portable), installers));
    if (!pick) {
        list.append(category(QStringLiteral("vm"), Cleanup::Group::Yours, I18n::tr("Virtual machine images"),
                             I18n::tr("Kept by desktop apps, one copy per profile. The app downloads the image again; what its sessions saved inside is lost."),
                             {item(QStringLiteral("Studio"), home + QStringLiteral("\\AppData\\Roaming\\Studio\\vm_bundles"), 9.0, 1, false, now),
                              item(QStringLiteral("Studio-Site"), home + QStringLiteral("\\AppData\\Roaming\\Studio-Site\\vm_bundles"), 8.9, 13, false, now)}));
        list.append(category(QStringLiteral("codex"), Cleanup::Group::Yours, I18n::tr("Codex archived sessions"),
                             I18n::tr("Old conversations kept by Codex."),
                             {item(I18n::tr("Codex archived sessions"), home + QStringLiteral("\\.codex\\archived_sessions"), 7.42, 0, false, now)},
                             true));
        list.append(orphanedInstallers(now, InstallerOrphans::Status::Ok));
    }
    return list;
}

#endif

} // namespace

namespace CleanupFixture {

QStringList states()
{
    QStringList list = {QStringLiteral("cleanup"),      QStringLiteral("cleanup-pick"),      QStringLiteral("cleanup-result"),
                        QStringLiteral("cleanup-apps"), QStringLiteral("cleanup-apps-nuke"), QStringLiteral("cleanup-apps-yours"),
                        QStringLiteral("folders"),      QStringLiteral("folders-selected"),  QStringLiteral("files"),
                        QStringLiteral("changes"),      QStringLiteral("scanning")};
#ifndef Q_OS_MACOS
    // Instaladores huerfanos (solo Windows): la lista, la consulta que pide administrador, el resultado de
    // una limpieza y el pedido de administrador cancelado.
    list << QStringLiteral("cleanup-installers") << QStringLiteral("cleanup-installers-admin") << QStringLiteral("cleanup-installers-unverified")
         << QStringLiteral("cleanup-installers-unfinished") << QStringLiteral("cleanup-installers-result") << QStringLiteral("cleanup-installers-denied");
#endif
    return list;
}

Data build(const QString &state, ScanEngine &engine)
{
    Data data;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    data.scanning = state == QLatin1String("scanning");
#ifdef Q_OS_MACOS
    data.drive.root = QStringLiteral("/");
    data.drive.label = QStringLiteral("Macintosh HD");
    data.drive.name = QString();
    data.drive.totalBytes = gb(926.4);
    data.drive.freeBytes = state == QLatin1String("cleanup-result") ? gb(146.2) : gb(31.8);

    data.guard.volumeRoot = QStringLiteral("/");
    data.guard.protectedTrees = {QStringLiteral("/System"), QStringLiteral("/Applications"), QStringLiteral("/Library"),
                                 QStringLiteral("/usr"), QStringLiteral("/private/var/db"), QStringLiteral("/opt")};
    data.guard.protectedFolders = {QStringLiteral("/Users"), QStringLiteral("/Users/lega"), QStringLiteral("/Users/lega/Library"),
                                   QStringLiteral("/Users/lega/Desktop"), QStringLiteral("/Users/lega/Documents")};
#else
    data.drive.root = QStringLiteral("C:/");
    data.drive.label = QStringLiteral("C:");
    data.drive.name = QStringLiteral("Windows");
    data.drive.totalBytes = gb(930.5);
    // Lo que va a la Papelera no libera nada: despues de mover instaladores, el disco queda igual.
    data.drive.freeBytes = state == QLatin1String("cleanup-result") ? gb(91.6) : gb(32.3);

    data.guard.volumeRoot = QStringLiteral("C:\\");
    data.guard.protectedTrees = {QStringLiteral("C:\\Windows"), QStringLiteral("C:\\Program Files"), QStringLiteral("C:\\Program Files (x86)"),
                                 QStringLiteral("C:\\ProgramData"), QStringLiteral("C:\\pagefile.sys"), QStringLiteral("C:\\$Recycle.Bin")};
    data.guard.protectedFolders = {QStringLiteral("C:\\Users"), QStringLiteral("C:\\Users\\lega"), QStringLiteral("C:\\Users\\lega\\AppData"),
                                   QStringLiteral("C:\\Users\\lega\\Desktop"), QStringLiteral("C:\\Users\\lega\\Documents")};
#endif

    ScanTree::Index users = ScanTree::kNone;
    ScanTree::Index lega = ScanTree::kNone;
    ScanTree::Index desktop = ScanTree::kNone;
    {
        const auto lock = engine.lock();
        ScanTree &tree = engine.tree();
#ifdef Q_OS_MACOS
        tree.reset(QStringLiteral("/"));
        qint64 bytes = 0;
        int files = 0;
        addLevel(tree, tree.root(), 0, 0, now, &bytes, &files);
        tree.setListed(tree.root(), 0, 0, now - kDay, false, false);
        users = tree.find(QStringLiteral("/Users"));
        lega = tree.find(QStringLiteral("/Users/lega"));
        desktop = tree.find(QStringLiteral("/Users/lega/Desktop"));
#else
        tree.reset(QStringLiteral("C:\\"));
        qint64 bytes = 0;
        int files = 0;
        addLevel(tree, tree.root(), 0, 0, now, &bytes, &files);
        // En la raiz, la memoria virtual: un archivo suelto de 32 GB.
        tree.setListed(tree.root(), quint64(gb(32.0)), 1, now - kDay, false, false);
        users = tree.find(QStringLiteral("C:\\Users"));
        lega = tree.find(QStringLiteral("C:\\Users\\lega"));
        desktop = tree.find(QStringLiteral("C:\\Users\\lega\\Desktop"));
#endif
    }
#ifdef Q_OS_MACOS
    data.listings.insert(0, ScanEngine::FileListing());
#else
    ScanEngine::FileListing rootFiles;
    rootFiles.largest.append(ScanEngine::FileEntry{QStringLiteral("pagefile.sys"), quint64(gb(32.0)), now - kDay});
    data.listings.insert(0, rootFiles);
#endif

    QList<ScanEngine::TopFile> top;
    const auto addTop = [&](double gib, ScanTree::Index dir, const QString &name, int days) {
        ScanEngine::TopFile file;
        file.bytes = quint64(gb(gib));
        file.dir = dir;
        file.name = name;
        file.modified = now - days * kDay;
        top.append(file);
    };
    {
        const auto lock = engine.lock();
        const ScanTree &tree = engine.tree();
#ifdef Q_OS_MACOS
        addTop(13.0, desktop, QStringLiteral("Allemaal Familie - Ventana Ciudad Dia 01.mov"), 21);
        addTop(9.12, tree.find(QStringLiteral("/Users/lega/Library/Application Support/Studio")), QStringLiteral("rootfs.img"), 1);
        addTop(8.58, tree.find(QStringLiteral("/Users/lega/Movies")), QStringLiteral("IMG_0020.mov"), 30);
        addTop(6.51, tree.find(QStringLiteral("/Users/lega/Downloads")), QStringLiteral("DaVinci_Resolve_Studio_20.2.2_Mac.dmg"), 60);
        addTop(4.37, tree.find(QStringLiteral("/Users/lega/Movies")), QStringLiteral("berlin-symphony-of-a-great-city_1928.mp4"), 90);
        addTop(3.98, tree.find(QStringLiteral("/Users/lega/Library/Application Support/Google")), QStringLiteral("weights.bin"), 40);
        addTop(3.30, tree.find(QStringLiteral("/Users/lega/.nuke")), QStringLiteral("pack-d6bf6beee809.pack"), 136);
        addTop(3.07, desktop, QStringLiteral("LS83_Prensa.mp4"), 8);
        addTop(2.93, tree.find(QStringLiteral("/Applications/Nuke17.0v1")), QStringLiteral("libtorch_cpu.dylib"), 40);
#else
        addTop(32.0, tree.root(), QStringLiteral("pagefile.sys"), 1);
        addTop(8.74, desktop, QStringLiteral("InviziGrain Demo Vintage Stock.mov"), 13);
        addTop(8.24, tree.find(QStringLiteral("C:\\Users\\lega\\AppData\\Local\\Packages")), QStringLiteral("rootfs.vhdx"), 1);
        addTop(7.37, desktop, QStringLiteral("InviziGrain Demo 2026.mov"), 13);
        addTop(6.84, tree.find(QStringLiteral("C:\\Windows")), QStringLiteral("MEMORY.DMP"), 4);
        addTop(4.31, tree.find(QStringLiteral("C:\\Portable\\LGA_SceneBuilder")), QStringLiteral("lingbot-map-long.pt"), 28);
        addTop(4.24, desktop, QStringLiteral("2026-09-22 14-38-56.mp4"), 8);
        addTop(3.65, tree.find(QStringLiteral("C:\\Qt")), QStringLiteral("Qt6WebEngineCored.pdb"), 1095);
        addTop(3.30, tree.find(QStringLiteral("C:\\Users\\lega\\.nuke")), QStringLiteral("pack-d6bf6beee809.pack"), 136);
        addTop(3.22, tree.find(QStringLiteral("C:\\Users\\lega\\AppData\\Local\\pip")), QStringLiteral("bd17b62e998533a6.body"), 28);
        addTop(3.09, tree.find(QStringLiteral("C:\\Users\\lega\\AppData\\Roaming\\Blackmagic Design")), QStringLiteral("Project.db"), 766);
        addTop(3.04, tree.find(QStringLiteral("C:\\ProgramData")), QStringLiteral("nlstats.db"), 0);
        addTop(2.93, tree.find(QStringLiteral("C:\\Portable\\LGA_ShotPlayer")), QStringLiteral("pack-9c30c762fe5e.pack"), 3);
#endif
    }
    engine.setTopFilesForCapture(top);

    data.progress.complete = !data.scanning;
    data.progress.running = data.scanning;
    data.progress.files = data.scanning ? 2840000 : 4690000;
    data.progress.dirs = 718000;
    data.progress.seconds = data.scanning ? 4.0 : 7.0;

    data.categories = categories(state, now);
    data.openCategories = {QStringLiteral("python")};
    data.tab = 0;
    if (state.startsWith(QLatin1String("cleanup-installers"))) {
        data.openCategories = {QStringLiteral("installers")};
        const QString title = I18n::tr("Orphaned installer files");
        if (state == QLatin1String("cleanup-installers-result")) {
            // Se eligieron los dos primeros y Blackmagic RAW; ese ultimo se salteo en la re-verificacion.
            data.banner = I18n::tr("%1 installer files went to the Recycle Bin (%2).").arg(2).arg(QStringLiteral("1.43 GB"));
            data.skippedSummary = I18n::tr("1 item");
            data.skipped = {{title + QStringLiteral(" · Blackmagic RAW 3.6"), I18n::tr("An installation is running"), false}};
        } else if (state == QLatin1String("cleanup-installers-denied")) {
            data.skippedSummary = I18n::tr("1 item");
            data.skipped = {{title, I18n::tr("Administrator permission was not given"), false}};
        }
    } else if (state == QLatin1String("cleanup-pick")) {
        data.openCategories = {QStringLiteral("rule:0")};
    } else if (state == QLatin1String("cleanup-apps")) {
        data.openCategories = {QStringLiteral("media")};
    } else if (state == QLatin1String("cleanup-apps-nuke")) {
        data.openCategories = {QStringLiteral("nuke"), QStringLiteral("rendercache")};
    } else if (state == QLatin1String("cleanup-apps-yours")) {
        data.openCategories = {QStringLiteral("nukework"), QStringLiteral("previews"), QStringLiteral("appdownloads")};
    } else if (state == QLatin1String("cleanup-result")) {
        data.openCategories.clear();
#ifdef Q_OS_MACOS
        data.banner = I18n::tr("Freed %1. %2 has %3 free.").arg(QStringLiteral("114.4 GB"), QStringLiteral("Macintosh HD"), QStringLiteral("146.2 GB"));
#else
        data.banner = I18n::tr("Freed %1. %2 has %3 free.").arg(QStringLiteral("59.3 GB"), QStringLiteral("C:"), QStringLiteral("91.6 GB"));
#endif
        data.skippedSummary = I18n::tr("%1 files · %2").arg(QStringLiteral("212"), QStringLiteral("922 MB"));
        data.skipped = {{I18n::tr("Browser caches") + QStringLiteral(" · Brave"), I18n::tr("%1 is open").arg(QStringLiteral("Brave"))},
                        {I18n::tr("Temporary files"), I18n::tr("%1 files in use").arg(64)}};
    } else if (data.scanning) {
        // Como en el canvas: solo el primer nivel, con varias carpetas todavia contandose.
        data.tab = 1;
        const auto lock = engine.lock();
#ifdef Q_OS_MACOS
        for (const char16_t *name : {u"/Users", u"/Applications", u"/Library", u"/System"}) {
#else
        for (const char16_t *name : {u"C:\\Users", u"C:\\Program Files", u"C:\\Portable", u"C:\\ProgramData", u"C:\\Qt"}) {
#endif
            data.counting.insert(engine.tree().find(QString::fromUtf16(name)));
        }
    } else if (state == QLatin1String("folders") || state == QLatin1String("folders-selected")) {
        data.tab = 1;
        data.expanded = {users, lega};
        if (state == QLatin1String("folders-selected")) {
#ifdef Q_OS_MACOS
            data.selectedPaths = {QStringLiteral("/Users/lega/.cache"), QStringLiteral("/Users/lega/Downloads")};
#else
            data.selectedPaths = {QStringLiteral("C:\\Users\\lega\\.cache"), QStringLiteral("C:\\Users\\lega\\miniconda3")};
#endif
        }
    } else if (state == QLatin1String("files")) {
        data.tab = 2;
    } else if (state == QLatin1String("changes")) {
        data.tab = 3;
        // Anidado como un escaneo real: una zona con carpetas intermedias (que no se listan), hojas y resto,
        // temporales del sistema, una zona que bajo y crecimiento que ninguna zona explica.
#ifdef Q_OS_MACOS
        const QString home = QDir::homePath();
        const auto h = [&](const char *relative) { return home + QLatin1Char('/') + QLatin1String(relative); };
        const QString temp = QStringLiteral("/private/var/folders/cx/qtyt758157x8qm7xl4fdjmqc0000gn");
        data.baseline.root = QStringLiteral("/");
        data.baseline.takenAt = QDateTime(QDate::currentDate().addDays(-1), QTime(18, 10));
        data.usedDelta = gb(62.0);
        data.changes = {{h("Desktop/Codin"), gb(39.6), gb(88.1)},
                        {h("Desktop/Codin/LGA_PipeSync_2"), gb(24.7), gb(37.3)},
                        {h("Desktop/Codin/LGA_PipeSync_2/build-qa/Gantt Mac 2026-10-11"), gb(4.64), gb(4.64)},
                        {h("Desktop/Codin/LGA_PipeSync_2/build_client-mcp-pack-20261013"), gb(4.42), gb(4.42)},
                        {h("Desktop/Codin/LGA_PipeSync_2/build-mcp-pack-20261013"), gb(4.42), gb(4.42)},
                        {h("Desktop/Codin/LGA_PipeSync_2/build-mtmac"), gb(3.65), gb(3.65)},
                        {h("Desktop/Codin/LGA_PipeSync_2/build_client-release"), gb(2.18), gb(2.18)},
                        {h("Desktop/Codin/LGA_PipeSync_2/build_client"), gb(1.85), gb(4.57)},
                        {h("Desktop/Codin/LGA_PipeSync_2/build"), gb(1.64), gb(4.65)},
                        {h("Desktop/Codin/LGA_App_El_Music"), gb(9.27), gb(9.27)},
                        {h("Desktop/Codin/LGA_App_El_Music/.qa"), gb(6.77), gb(6.77)},
                        {h("Desktop/Codin/LGA_RepoTools"), gb(2.12), gb(2.52)},
                        {h("Desktop/Codin/LGA_RepoTools/RepoRules_Shared/repos"), gb(1.52), gb(1.82)},
                        {h("Desktop/Codin/LGA_MediaTools"), gb(1.90), gb(11.9)},
                        {h("Library"), gb(6.24), gb(414)},
                        {h("Library/Caches"), gb(3.34), gb(12.9)},
                        {h("Library/Caches/com.spotify.client/Data"), gb(1.63), gb(1.63)},
                        {temp, gb(4.88), gb(9.66)},
                        {temp + QStringLiteral("/T"), gb(2.84), gb(4.80)},
                        {temp + QStringLiteral("/T/com.apple.fileproviderd/TemporaryItems"), gb(1.90), gb(1.90)},
                        {temp + QStringLiteral("/X/com.google.Chrome.code_sign_clone"), gb(2.09), gb(2.09)},
                        {h(".Trash"), -gb(0.4), gb(2.5)}};
#else
        data.baseline.root = QStringLiteral("C:\\");
        data.baseline.takenAt = QDateTime(QDate::currentDate().addDays(-1), QTime(18, 10));
        data.usedDelta = gb(18.0);
        data.changes = {{QStringLiteral("C:\\Portable"), gb(12.0), gb(88.1)},
                        {QStringLiteral("C:\\Portable\\LGA_PipeSync_2"), gb(8.0), gb(37.3)},
                        {QStringLiteral("C:\\Portable\\LGA_PipeSync_2\\build-qa"), gb(4.6), gb(4.6)},
                        {QStringLiteral("C:\\Portable\\LGA_PipeSync_2\\build"), gb(1.6), gb(4.65)},
                        {QStringLiteral("C:\\Portable\\LGA_MediaTools"), gb(1.9), gb(11.9)},
                        {QStringLiteral("C:\\Users\\lega\\AppData\\Local\\Temp"), gb(3.1), gb(5.0)},
                        {QStringLiteral("C:\\Users\\lega\\.gradle"), gb(1.9), gb(12.5)},
                        {QStringLiteral("C:\\$Recycle.Bin"), -gb(0.4), gb(9.1)}};
#endif
    }
    return data;
}

} // namespace CleanupFixture
