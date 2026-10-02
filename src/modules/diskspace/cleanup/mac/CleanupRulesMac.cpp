#include "modules/diskspace/cleanup/CleanupRules.h"

#include "core/I18n.h"
#include "modules/diskspace/cleanup/DeleteGuard.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

// Las reglas de limpieza de macOS. Mismos principios que las de Windows (CleanupRules.h): lista blanca de
// carpetas hijas de cache, nunca la carpeta raiz de un programa. Fuentes y lo que quedo afuera a
// proposito (propuesta auditada el 2026-10-02, +Building_Blocks/trabajo/disk_cleaner_mac):
//  - pip (`~/Library/Caches/pip`), uv (`~/.cache/uv`), npm (`~/.npm/_cacache`), Cargo, Homebrew
//    (`~/Library/Caches/Homebrew`, las descargas que `brew cleanup` tambien borra), Xcode DerivedData.
//  - Navegadores Chromium: el perfil vive en Application Support y su cache de disco en Caches; de los
//    dos lados, solo `Cache` (con `Cache_Data`), `Code Cache`, `GPUCache` y los `Dawn*`. Nunca cookies,
//    historial, `Service Worker`, `IndexedDB` ni `Local Storage`.
//  - Actualizaciones ya bajadas: `*.ShipIt` (Squirrel), `<id>/org.sparkle-project.Sparkle` (Sparkle),
//    `*-updater/pending` (electron-updater), `Mozilla/updates`.
//  - `~/Library/Caches` NO es todo cache: PipeSync guarda ahi sus bases y respaldos, CloudKit sus bases
//    abiertas, Spark su correo, JetBrains su historial local, Poetry sus entornos. Por eso cada hijo va en
//    "Yours to decide", destildado, y nunca los de Apple ni los de LGA.
//  - Afuera: los contenedores de otras apps (`~/Library/Containers`), `/Library/Caches`, las caches del
//    sistema (`/private/var/folders/*/C`), Safari, Mail, Fotos, las copias locales de Time Machine.
//  - Sin acceso total al disco (D-43) no se mira nada de lo que protege la privacidad de macOS: mirarlo
//    mostraria un cartel.

namespace {

using CleanupRules::Context;

QString join(const QString &dir, const QString &name)
{
    return dir.isEmpty() ? QString() : (dir.endsWith(QLatin1Char('/')) ? dir + name : dir + QLatin1Char('/') + name);
}

// Lo que el escaneo no mira sin acceso total al disco (SystemPaths::scanExclusions). Se vuelve a pedir en
// cada armado de las reglas: el permiso puede llegar con la app abierta.
QStringList g_exclusions;

bool excluded(const QString &path)
{
    for (const QString &exclusion : g_exclusions) {
        if (DeleteGuard::samePath(path, exclusion) || DeleteGuard::isInside(path, exclusion)) {
            return true;
        }
    }
    return false;
}

QStringList subDirs(const QString &dir)
{
    if (dir.isEmpty() || excluded(dir)) {
        return {};
    }
    QStringList names;
    for (const QString &name : QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::NoSymLinks)) {
        if (!excluded(join(dir, name))) {
            names.append(name);
        }
    }
    return names;
}

bool isDir(const QString &path)
{
    return !path.isEmpty() && !excluded(path) && QFileInfo(path).isDir();
}

void addItem(Cleanup::Category &category, Cleanup::Item item, const QStringList &blockers, const QString &blockerLabel)
{
    if (item.targets.isEmpty()) {
        return;
    }
    item.blockers = blockers;
    item.blockerLabel = blockerLabel;
    category.items.append(item);
}

// Las carpetas de cache de Chromium/Electron dentro de `dir`. `Cache` solo cuenta con `Cache_Data` adentro.
QStringList chromiumCacheDirs(const QString &dir)
{
    QStringList dirs;
    const QString cache = join(dir, QStringLiteral("Cache"));
    if (isDir(join(cache, QStringLiteral("Cache_Data")))) {
        dirs.append(cache);
    }
    for (const char *name : {"Code Cache", "GPUCache", "DawnCache", "DawnGraphiteCache", "DawnWebGPUCache"}) {
        const QString path = join(dir, QLatin1String(name));
        if (isDir(path)) {
            dirs.append(path);
        }
    }
    return dirs;
}

bool isChromiumDataDir(const QString &dir)
{
    return isDir(join(dir, QStringLiteral("Code Cache"))) || isDir(join(dir, QStringLiteral("GPUCache")));
}

// Nombres con los que puede correr la app de una carpeta: el nombre del ejecutable suele ser el de la
// carpeta ("Studio", "Code", "Spark Desktop"); si la carpeta es un identificador ("com.microsoft.VSCode"),
// el identificador y sus prefijos de 3 tramos o mas (SystemPaths::runningPrograms da los mismos).
QStringList guessedProcesses(const QString &name)
{
    const QString lower = name.toLower();
    QStringList names{lower};
    const auto add = [&names](const QString &value) {
        if (value.size() >= 3 && !names.contains(value)) {
            names.append(value);
        }
    };
    QString compact = lower;
    compact.remove(QLatin1Char(' '));
    add(compact);
    static const QRegularExpression separators(QStringLiteral("[ \\-_]"));
    add(lower.section(separators, 0, 0));
    QStringList parts = lower.split(QLatin1Char('.'));
    if (parts.size() >= 3) {
        while (parts.size() >= 3) {
            add(parts.join(QLatin1Char('.')));
            parts.removeLast();
        }
    }
    return names;
}

// Un navegador Chromium: los perfiles (carpetas con `Preferences`) en Application Support; su cache, del
// mismo nombre, en los dos lados.
void addBrowser(Cleanup::Category &category, const QString &name, const QString &supportDir, const QString &cacheDir,
                const QStringList &processes, const Context &context)
{
    if (!isDir(supportDir)) {
        return;
    }
    QStringList dirs;
    for (const QString &profile : subDirs(supportDir)) {
        if (!QFileInfo::exists(join(join(supportDir, profile), QStringLiteral("Preferences")))) {
            continue;
        }
        dirs.append(chromiumCacheDirs(join(supportDir, profile)));
        dirs.append(chromiumCacheDirs(join(cacheDir, profile)));
    }
    for (const char *shared : {"ShaderCache", "GrShaderCache", "GraphiteDawnCache", "GPUPersistentCache"}) {
        if (isDir(join(supportDir, QLatin1String(shared)))) {
            dirs.append(join(supportDir, QLatin1String(shared)));
        }
    }
    addItem(category, CleanupRules::makeItem(name, supportDir, dirs, Cleanup::Action::Contents, context), processes, name);
}

// Los hijos de ~/Library/Caches que nunca se ofrecen: los de Apple (con o sin el prefijo) y los de LGA.
bool isSystemOrOwnCache(const QString &name)
{
    const QString lower = name.toLower();
    static const QStringList apple = {QStringLiteral("cloudkit"), QStringLiteral("geoservices"), QStringLiteral("familycircle"),
                                      QStringLiteral("familycircled"), QStringLiteral("passkit"), QStringLiteral("ssu"),
                                      QStringLiteral("lga")};
    return lower.startsWith(QLatin1String("com.apple.")) || apple.contains(lower);
}

} // namespace

namespace CleanupRules {

QList<Cleanup::Category> systemCategories(const Context &context)
{
    const SystemPaths::CleanupBases &b = context.bases;
    g_exclusions = SystemPaths::scanExclusions();
    QList<Cleanup::Category> categories;
    // Todo destino que alguna regla ya cubre: la de "otras caches" no los vuelve a ofrecer.
    QStringList covered;
    const auto remember = [&covered](const Cleanup::Category &category) {
        for (const Cleanup::Item &item : category.items) {
            for (const Cleanup::Target &target : item.targets) {
                covered.append(target.path);
            }
        }
    };

    // ---------------------------------------------------------------- Safe to delete
    {
        Cleanup::Category bin;
        bin.id = QStringLiteral("bin");
        bin.group = Cleanup::Group::Safe;
        bin.single = true;
        bin.title = I18n::tr("Trash");
        bin.caption = I18n::tr("What you already deleted from this drive.");
        Cleanup::Item item;
        item.name = bin.title;
        item.path = context.volumeRoot;
        item.targets.append(Cleanup::Target{context.volumeRoot, Cleanup::Action::RecycleBin, 0});
        bin.items.append(item);
        categories.append(bin);
    }
    {
        Cleanup::Category python;
        python.id = QStringLiteral("python");
        python.group = Cleanup::Group::Safe;
        python.title = I18n::tr("Python package caches");
        python.caption = I18n::tr("Downloads kept by pip and uv. Installing a package fetches it again.");
        const QString pip = b.pipCacheDir.isEmpty() ? join(b.caches, QStringLiteral("pip")) : b.pipCacheDir;
        addItem(python, makeItem(QStringLiteral("pip cache"), pip, {pip}, Cleanup::Action::Contents, context),
                {QStringLiteral("pip"), QStringLiteral("pip3")}, QStringLiteral("pip"));
        const QString uv = b.uvCacheDir.isEmpty() ? join(join(b.profile, QStringLiteral(".cache")), QStringLiteral("uv")) : b.uvCacheDir;
        addItem(python, makeItem(QStringLiteral("uv cache"), uv, {uv}, Cleanup::Action::Contents, context),
                {QStringLiteral("uv"), QStringLiteral("uvx")}, QStringLiteral("uv"));
        // Va siempre, aunque quede vacia: el escaneo puede sumarle una cache de uv movida de lugar.
        remember(python);
        categories.append(python);
    }
    {
        Cleanup::Category media;
        media.id = QStringLiteral("media");
        media.group = Cleanup::Group::Safe;
        media.title = I18n::tr("Media caches");
        media.caption = I18n::tr("Rebuilt when a project opens.");
        const QString common = join(b.appSupport, QStringLiteral("Adobe/Common"));
        addItem(media,
                makeItem(QStringLiteral("Adobe Media Cache"), common,
                         {join(common, QStringLiteral("Media Cache Files")), join(common, QStringLiteral("Media Cache")),
                          join(common, QStringLiteral("Peak Files"))},
                         Cleanup::Action::Contents, context),
                {QStringLiteral("com.adobe.premierepro"), QStringLiteral("com.adobe.aftereffects"), QStringLiteral("com.adobe.ame"),
                 QStringLiteral("com.adobe.audition"), QStringLiteral("adobe premiere pro"), QStringLiteral("after effects"),
                 QStringLiteral("adobe media encoder"), QStringLiteral("adobe audition")},
                QStringLiteral("Adobe"));
        remember(media);
        categories.append(media);
    }
    {
        Cleanup::Category dev;
        dev.id = QStringLiteral("dev");
        dev.group = Cleanup::Group::Safe;
        dev.title = I18n::tr("Developer caches");
        dev.caption = I18n::tr("Package downloads. Fetched again by the next install or build.");
        const QString npm = join(join(b.profile, QStringLiteral(".npm")), QStringLiteral("_cacache"));
        addItem(dev, makeItem(QStringLiteral("npm cache"), npm, {npm}, Cleanup::Action::Contents, context),
                {QStringLiteral("node"), QStringLiteral("npm")}, QStringLiteral("Node"));
        const QString cargo = b.cargoHome.isEmpty() ? join(b.profile, QStringLiteral(".cargo")) : b.cargoHome;
        const QString registry = join(cargo, QStringLiteral("registry"));
        addItem(dev,
                makeItem(QStringLiteral("Cargo registry"), registry,
                         {join(registry, QStringLiteral("cache")), join(registry, QStringLiteral("src")),
                          join(join(cargo, QStringLiteral("git")), QStringLiteral("checkouts"))},
                         Cleanup::Action::Contents, context),
                {QStringLiteral("cargo"), QStringLiteral("rustc"), QStringLiteral("rust-analyzer")}, QStringLiteral("Cargo"));
        const QString brew = join(b.caches, QStringLiteral("Homebrew"));
        addItem(dev, makeItem(QStringLiteral("Homebrew downloads"), brew, {brew}, Cleanup::Action::Contents, context), {},
                QString());
        const QString derived = join(b.library, QStringLiteral("Developer/Xcode/DerivedData"));
        addItem(dev, makeItem(QStringLiteral("Xcode DerivedData"), derived, {derived}, Cleanup::Action::Contents, context),
                {QStringLiteral("com.apple.dt.xcode"), QStringLiteral("xcode"), QStringLiteral("xcodebuild")}, QStringLiteral("Xcode"));
        remember(dev);
        categories.append(dev);
    }
    {
        Cleanup::Category browsers;
        browsers.id = QStringLiteral("browsers");
        browsers.group = Cleanup::Group::Safe;
        browsers.title = I18n::tr("Browser caches");
        browsers.caption = I18n::tr("Cache only: history, cookies and logins stay.");
        addBrowser(browsers, QStringLiteral("Chrome"), join(b.appSupport, QStringLiteral("Google/Chrome")),
                   join(b.caches, QStringLiteral("Google/Chrome")), {QStringLiteral("com.google.chrome"), QStringLiteral("google chrome")},
                   context);
        addBrowser(browsers, QStringLiteral("Edge"), join(b.appSupport, QStringLiteral("Microsoft Edge")),
                   join(b.caches, QStringLiteral("Microsoft Edge")), {QStringLiteral("com.microsoft.edgemac"), QStringLiteral("microsoft edge")},
                   context);
        addBrowser(browsers, QStringLiteral("Brave"), join(b.appSupport, QStringLiteral("BraveSoftware/Brave-Browser")),
                   join(b.caches, QStringLiteral("BraveSoftware/Brave-Browser")),
                   {QStringLiteral("com.brave.browser"), QStringLiteral("brave browser")}, context);
        // Firefox guarda la cache de disco de cada perfil en Caches, separada del perfil.
        const QString firefox = join(b.caches, QStringLiteral("Firefox/Profiles"));
        QStringList firefoxDirs;
        for (const QString &profile : subDirs(firefox)) {
            firefoxDirs.append(join(join(firefox, profile), QStringLiteral("cache2")));
        }
        addItem(browsers, makeItem(QStringLiteral("Firefox"), firefox, firefoxDirs, Cleanup::Action::Contents, context),
                {QStringLiteral("org.mozilla.firefox"), QStringLiteral("firefox")}, QStringLiteral("Firefox"));
        remember(browsers);
        categories.append(browsers);
    }

    // Apps de escritorio Chromium/Electron (su carpeta en Application Support): una pasada sirve para sus
    // caches y para sus maquinas virtuales.
    Cleanup::Category apps;
    apps.id = QStringLiteral("apps");
    apps.group = Cleanup::Group::Safe;
    apps.title = I18n::tr("App caches");
    apps.caption = I18n::tr("Cache folders of desktop apps. Settings and logins stay.");
    Cleanup::Category machines;
    machines.id = QStringLiteral("vm");
    machines.group = Cleanup::Group::Yours;
    machines.title = I18n::tr("Virtual machine images");
    machines.caption = I18n::tr("Kept by desktop apps, one copy per profile. The app downloads the image again; what its sessions saved inside is lost.");
    for (const QString &name : subDirs(b.appSupport)) {
        const QString dir = join(b.appSupport, name);
        if (!isChromiumDataDir(dir)) {
            continue;
        }
        const QStringList processes = guessedProcesses(name);
        addItem(apps, makeItem(name, dir, chromiumCacheDirs(dir), Cleanup::Action::Contents, context), processes, name);
        const QString bundles = join(dir, QStringLiteral("vm_bundles"));
        addItem(machines, makeItem(name, bundles, {bundles}, Cleanup::Action::Contents, context), processes, name);
    }
    remember(apps);
    categories.append(apps);

    {
        // Actualizaciones que la app ya bajo (y en general ya instalo): si le falta, la vuelve a bajar.
        Cleanup::Category updates;
        updates.id = QStringLiteral("updates");
        updates.group = Cleanup::Group::Safe;
        updates.title = I18n::tr("App update downloads");
        updates.caption = I18n::tr("Updates apps already downloaded. They download them again if they need them.");
        for (const QString &name : subDirs(b.caches)) {
            const QString dir = join(b.caches, name);
            if (isSystemOrOwnCache(name)) {
                continue;
            }
            if (name.endsWith(QLatin1String(".ShipIt"))) {
                // "com.microsoft.VSCode.ShipIt": la app es el identificador sin ".ShipIt".
                const QString owner = name.chopped(7);
                addItem(updates, makeItem(owner, dir, {dir}, Cleanup::Action::Contents, context), guessedProcesses(owner), owner);
                continue;
            }
            const QString sparkle = join(dir, QStringLiteral("org.sparkle-project.Sparkle"));
            if (isDir(sparkle)) {
                addItem(updates, makeItem(name, sparkle, {sparkle}, Cleanup::Action::Contents, context), guessedProcesses(name), name);
                continue;
            }
            const QString pending = join(dir, QStringLiteral("pending"));
            if (name.endsWith(QLatin1String("-updater")) && isDir(pending)) {
                const QString owner = name.chopped(8);
                addItem(updates, makeItem(owner, pending, {pending}, Cleanup::Action::Contents, context), guessedProcesses(owner), owner);
            }
        }
        const QString mozilla = join(b.caches, QStringLiteral("Mozilla/updates"));
        addItem(updates, makeItem(QStringLiteral("Firefox"), mozilla, {mozilla}, Cleanup::Action::Contents, context),
                {QStringLiteral("org.mozilla.firefox"), QStringLiteral("firefox")}, QStringLiteral("Firefox"));
        remember(updates);
        categories.append(updates);
    }
    {
        Cleanup::Category temp;
        temp.id = QStringLiteral("temp");
        temp.group = Cleanup::Group::Safe;
        temp.single = true;
        temp.title = I18n::tr("Temporary files");
        temp.caption = I18n::tr("Older than 7 days and not in use.");
        Cleanup::Item item = makeItem(temp.title, b.temp, {b.temp}, Cleanup::Action::OldChildren, context);
        for (Cleanup::Target &target : item.targets) {
            target.minAgeDays = 7;
        }
        addItem(temp, item, {}, QString());
        categories.append(temp);
    }
    {
        Cleanup::Category reports;
        reports.id = QStringLiteral("crashreports");
        reports.group = Cleanup::Group::Safe;
        reports.single = true;
        reports.title = I18n::tr("Crash reports older than 30 days");
        reports.caption = I18n::tr("Written by macOS when an app crashes.");
        const QString dir = join(b.library, QStringLiteral("Logs/DiagnosticReports"));
        Cleanup::Item item = makeItem(reports.title, dir, {dir}, Cleanup::Action::OldChildren, context);
        for (Cleanup::Target &target : item.targets) {
            target.minAgeDays = 30;
        }
        addItem(reports, item, {}, QString());
        categories.append(reports);
    }

    // ---------------------------------------------------------------- Yours to decide
    categories.append(machines);
    {
        Cleanup::Category bridge;
        bridge.id = QStringLiteral("bridge");
        bridge.group = Cleanup::Group::Yours;
        bridge.single = true;
        bridge.title = I18n::tr("Adobe Bridge previews");
        bridge.caption = I18n::tr("Thumbnails and previews. Labels and ratings of files without XMP can be lost.");
        QStringList dirs;
        for (const QString &base : {join(b.caches, QStringLiteral("Adobe")), join(b.appSupport, QStringLiteral("Adobe"))}) {
            for (const QString &name : subDirs(base)) {
                if (name.startsWith(QLatin1String("Bridge"))) {
                    dirs.append(join(join(base, name), QStringLiteral("Cache")));
                }
            }
        }
        addItem(bridge, makeItem(bridge.title, join(b.appSupport, QStringLiteral("Adobe")), dirs, Cleanup::Action::Contents, context),
                {QStringLiteral("com.adobe.bridge"), QStringLiteral("adobe bridge")}, QStringLiteral("Adobe Bridge"));
        remember(bridge);
        categories.append(bridge);
    }
    {
        Cleanup::Category sessions;
        sessions.id = QStringLiteral("codex");
        sessions.group = Cleanup::Group::Yours;
        sessions.single = true;
        sessions.title = I18n::tr("Codex archived sessions");
        sessions.caption = I18n::tr("Old conversations kept by Codex.");
        const QString dir = join(join(b.profile, QStringLiteral(".codex")), QStringLiteral("archived_sessions"));
        addItem(sessions, makeItem(sessions.title, dir, {dir}, Cleanup::Action::Contents, context), {QStringLiteral("codex")},
                QStringLiteral("Codex"));
        categories.append(sessions);
    }
    {
        // Cada hijo de ~/Library/Caches que ninguna regla de arriba cubre. Casi todo se regenera, pero
        // algunas apps guardan ahi datos propios: va destildado y cada uno se elige a mano.
        Cleanup::Category other;
        other.id = QStringLiteral("appcaches");
        other.group = Cleanup::Group::Yours;
        other.title = I18n::tr("Other app caches");
        other.caption = I18n::tr("Most rebuild themselves, but some apps keep their own data here too. Tick only what you know.");
        for (const QString &name : subDirs(b.caches)) {
            const QString dir = join(b.caches, name);
            if (isSystemOrOwnCache(name)) {
                continue;
            }
            const QString real = usableDir(dir, context);
            bool overlaps = real.isEmpty();
            for (const QString &target : covered) {
                if (overlaps || DeleteGuard::samePath(target, real) || DeleteGuard::isInside(target, real)
                    || DeleteGuard::isInside(real, target)) {
                    overlaps = true;
                    break;
                }
            }
            if (overlaps) {
                continue;
            }
            addItem(other, makeItem(name, dir, {dir}, Cleanup::Action::Contents, context), guessedProcesses(name), name);
        }
        categories.append(other);
    }
    {
        const QString backups = join(b.appSupport, QStringLiteral("MobileSync/Backup"));
        if (isDir(backups)) {
            Cleanup::Category ios;
            ios.id = QStringLiteral("iosbackup");
            ios.group = Cleanup::Group::Yours;
            ios.single = true;
            ios.info = true;
            ios.title = I18n::tr("iPhone and iPad backups");
            ios.caption = I18n::tr("Made by the Finder. Not cleaned from here: manage them in the Finder, with the device connected.");
            ios.reviewPath = backups;
            Cleanup::Item item;
            item.name = ios.title;
            item.path = backups;
            item.targets.append(Cleanup::Target{backups, Cleanup::Action::Entire, 0});
            ios.items.append(item);
            categories.append(ios);
        }
    }
    return categories;
}

} // namespace CleanupRules
