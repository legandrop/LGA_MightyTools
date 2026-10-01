#include "modules/diskspace/cleanup/CleanupRules.h"

#include "core/I18n.h"
#include "modules/diskspace/cleanup/DeleteGuard.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

// Las reglas de limpieza de Windows. Cada ruta de esta tabla es una carpeta de CACHE documentada por
// su programa; la carpeta raiz de un programa nunca aparece. Fuentes y lo que quedo afuera a
// proposito (auditoria del 2026-09-30):
//  - pip (`pip cache dir`), uv (`uv cache dir`), npm (`_cacache`), Cargo (`registry\cache`,
//    `registry\src`, `git\checkouts`: documentados como regenerables por Cargo).
//  - Navegadores Chromium: solo `Cache`, `Code Cache`, `GPUCache` de cada perfil y los shaders de
//    `User Data`. Nunca cookies, historial, `Service Worker`, `IndexedDB` ni `Local Storage`.
//  - Afuera: `.nuget\packages` (es el almacen instalado, no una cache), la cache de miniaturas de
//    Windows (la tiene abierta el Explorador), `C:\Windows\Installer`, WinSxS y Package Cache.
//  - Con costo, por eso en "Yours to decide": la cache de Adobe Bridge (purgarla puede perder
//    etiquetas y puntajes de archivos sin XMP) y las imagenes de maquina virtual de apps de escritorio.

namespace {

using CleanupRules::Context;

QString join(const QString &dir, const QString &name)
{
    return dir.isEmpty() ? QString() : (dir.endsWith(QDir::separator()) ? dir + name : dir + QDir::separator() + name);
}

QStringList subDirs(const QString &dir)
{
    if (dir.isEmpty()) {
        return {};
    }
    return QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::NoSymLinks);
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

// Las carpetas de cache de una app Chromium/Electron dentro de `dir` (un perfil de navegador o la
// carpeta de datos de una app de escritorio). `Cache` solo cuenta si adentro tiene `Cache_Data`, que es
// la forma de la cache de Chromium: una carpeta "Cache" cualquiera puede ser otra cosa.
QStringList chromiumCacheDirs(const QString &dir)
{
    QStringList dirs;
    const QString cache = join(dir, QStringLiteral("Cache"));
    if (QFileInfo(join(cache, QStringLiteral("Cache_Data"))).isDir()) {
        dirs.append(cache);
    }
    for (const char *name : {"Code Cache", "GPUCache", "DawnCache", "DawnGraphiteCache", "DawnWebGPUCache"}) {
        const QString path = join(dir, QLatin1String(name));
        if (QFileInfo(path).isDir()) {
            dirs.append(path);
        }
    }
    return dirs;
}

// Los nombres propios de Chromium: con uno de estos, la carpeta es de una app Chromium/Electron.
bool isChromiumDataDir(const QString &dir)
{
    return QFileInfo(join(dir, QStringLiteral("Code Cache"))).isDir() || QFileInfo(join(dir, QStringLiteral("GPUCache"))).isDir();
}

// Como se llamaria el proceso de una app cuya carpeta de datos es `name`: "Spark Desktop" -> "spark desktop"
// y "sparkdesktop".
// Tambien la primera palabra ("Studio-Site" -> "studio"): un segundo perfil de la misma app corre con
// el proceso de la app. Bloquear de mas es el lado seguro. De una app empaquetada
// (Packages\Editor.App_abc123\LocalCache\Roaming\<app>) se suma el nombre del paquete.
QStringList guessedProcesses(const QString &dir)
{
    const QString lower = QFileInfo(dir).fileName().toLower();
    QStringList names{lower};
    const auto add = [&names](const QString &name) {
        if (name.size() >= 3 && !names.contains(name)) {
            names.append(name);
        }
    };
    QString compact = lower;
    compact.remove(QLatin1Char(' '));
    add(compact);
    static const QRegularExpression separators(QStringLiteral("[ \\-_.]"));
    add(lower.section(separators, 0, 0));
    const QString native = QDir::toNativeSeparators(dir);
    const int packages = native.indexOf(QStringLiteral("\\Packages\\"), 0, Qt::CaseInsensitive);
    if (packages >= 0 && native.contains(QStringLiteral("\\LocalCache\\Roaming\\"), Qt::CaseInsensitive)) {
        const QString package = native.mid(packages + 10).section(QLatin1Char('\\'), 0, 0).section(QLatin1Char('_'), 0, 0).toLower();
        add(package);
        add(package.section(QLatin1Char('.'), -1));
    }
    return names;
}

void addBrowser(Cleanup::Category &category, const QString &name, const QString &userData, const QString &process,
                const Context &context)
{
    if (userData.isEmpty() || !QFileInfo(userData).isDir()) {
        return;
    }
    QStringList dirs;
    for (const QString &profile : subDirs(userData)) {
        const QString profileDir = join(userData, profile);
        // Un perfil tiene su archivo Preferences; las demas carpetas de User Data no son perfiles.
        if (QFileInfo::exists(join(profileDir, QStringLiteral("Preferences")))) {
            dirs.append(chromiumCacheDirs(profileDir));
        }
    }
    for (const char *shared : {"ShaderCache", "GrShaderCache", "GraphiteDawnCache"}) {
        dirs.append(join(userData, QLatin1String(shared)));
    }
    addItem(category, CleanupRules::makeItem(name, userData, dirs, Cleanup::Action::Contents, context), {process}, name);
}

// Carpetas de datos de apps de escritorio: los hijos directos de AppData\Roaming y AppData\Local, y los
// de las apps empaquetadas (Packages\<paquete>\LocalCache\Roaming\<app>).
QStringList appDataDirs(const Context &context)
{
    QStringList dirs;
    for (const QString &base : {context.bases.roamingAppData, context.bases.localAppData}) {
        for (const QString &name : subDirs(base)) {
            dirs.append(join(base, name));
        }
    }
    const QString packages = join(context.bases.localAppData, QStringLiteral("Packages"));
    for (const QString &package : subDirs(packages)) {
        const QString roaming = join(join(join(packages, package), QStringLiteral("LocalCache")), QStringLiteral("Roaming"));
        for (const QString &name : subDirs(roaming)) {
            dirs.append(join(roaming, name));
        }
    }
    return dirs;
}

} // namespace

namespace CleanupRules {

QList<Cleanup::Category> systemCategories(const Context &context)
{
    const SystemPaths::CleanupBases &b = context.bases;
    QList<Cleanup::Category> categories;

    // ---------------------------------------------------------------- Safe to delete
    {
        Cleanup::Category bin;
        bin.id = QStringLiteral("bin");
        bin.group = Cleanup::Group::Safe;
        bin.single = true;
        bin.title = I18n::tr("Recycle Bin");
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
        const QString pip = b.pipCacheDir.isEmpty() ? join(join(b.localAppData, QStringLiteral("pip")), QStringLiteral("cache")) : b.pipCacheDir;
        addItem(python, makeItem(QStringLiteral("pip cache"), pip, {pip}, Cleanup::Action::Contents, context),
                {QStringLiteral("pip"), QStringLiteral("pip3")}, QStringLiteral("pip"));
        const QString uv = b.uvCacheDir.isEmpty() ? join(join(b.localAppData, QStringLiteral("uv")), QStringLiteral("cache")) : b.uvCacheDir;
        addItem(python, makeItem(QStringLiteral("uv cache"), uv, {uv}, Cleanup::Action::Contents, context),
                {QStringLiteral("uv"), QStringLiteral("uvx")}, QStringLiteral("uv"));
        // Va siempre, aunque quede vacia: el escaneo puede sumarle una cache de uv movida de lugar.
        categories.append(python);
    }
    {
        Cleanup::Category media;
        media.id = QStringLiteral("media");
        media.group = Cleanup::Group::Safe;
        media.title = I18n::tr("Media caches");
        media.caption = I18n::tr("Rebuilt when a project opens.");
        const QString common = join(b.roamingAppData, QStringLiteral("Adobe\\Common"));
        addItem(media,
                makeItem(QStringLiteral("Adobe Media Cache"), common,
                         {join(common, QStringLiteral("Media Cache Files")), join(common, QStringLiteral("Media Cache")),
                          join(common, QStringLiteral("Peak Files"))},
                         Cleanup::Action::Contents, context),
                {QStringLiteral("adobe premiere pro"), QStringLiteral("afterfx"), QStringLiteral("adobe media encoder"),
                 QStringLiteral("adobe audition")},
                QStringLiteral("Adobe"));
        categories.append(media);
    }
    {
        Cleanup::Category dev;
        dev.id = QStringLiteral("dev");
        dev.group = Cleanup::Group::Safe;
        dev.title = I18n::tr("Developer caches");
        dev.caption = I18n::tr("Package downloads. Fetched again by the next install or build.");
        const QString npm = join(join(b.localAppData, QStringLiteral("npm-cache")), QStringLiteral("_cacache"));
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
        categories.append(dev);
    }
    {
        Cleanup::Category browsers;
        browsers.id = QStringLiteral("browsers");
        browsers.group = Cleanup::Group::Safe;
        browsers.title = I18n::tr("Browser caches");
        browsers.caption = I18n::tr("Cache only: history, cookies and logins stay.");
        const QString local = b.localAppData;
        addBrowser(browsers, QStringLiteral("Chrome"), join(local, QStringLiteral("Google\\Chrome\\User Data")), QStringLiteral("chrome"),
                   context);
        addBrowser(browsers, QStringLiteral("Edge"), join(local, QStringLiteral("Microsoft\\Edge\\User Data")), QStringLiteral("msedge"),
                   context);
        addBrowser(browsers, QStringLiteral("Brave"), join(local, QStringLiteral("BraveSoftware\\Brave-Browser\\User Data")),
                   QStringLiteral("brave"), context);
        // Firefox guarda la cache de disco de cada perfil en AppData\Local, separada del perfil.
        const QString firefox = join(local, QStringLiteral("Mozilla\\Firefox\\Profiles"));
        QStringList firefoxDirs;
        for (const QString &profile : subDirs(firefox)) {
            firefoxDirs.append(join(join(firefox, profile), QStringLiteral("cache2")));
        }
        addItem(browsers, makeItem(QStringLiteral("Firefox"), firefox, firefoxDirs, Cleanup::Action::Contents, context),
                {QStringLiteral("firefox")}, QStringLiteral("Firefox"));
        categories.append(browsers);
    }

    // Apps de escritorio Chromium/Electron: una pasada sirve para sus caches y para sus maquinas virtuales.
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
    for (const QString &dir : appDataDirs(context)) {
        if (!isChromiumDataDir(dir)) {
            continue;
        }
        const QString name = QFileInfo(dir).fileName();
        const QStringList processes = guessedProcesses(dir);
        addItem(apps, makeItem(name, dir, chromiumCacheDirs(dir), Cleanup::Action::Contents, context), processes, name);
        const QString bundles = join(dir, QStringLiteral("vm_bundles"));
        addItem(machines, makeItem(name, bundles, {bundles}, Cleanup::Action::Contents, context), processes, name);
    }
    categories.append(apps);

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
        Cleanup::Category shaders;
        shaders.id = QStringLiteral("shaders");
        shaders.group = Cleanup::Group::Safe;
        shaders.title = I18n::tr("Shader caches and crash dumps");
        shaders.caption = I18n::tr("Games and 3D apps compile their shaders again as they run.");
        const QString d3d = join(b.localAppData, QStringLiteral("D3DSCache"));
        addItem(shaders, makeItem(I18n::tr("DirectX shader cache"), d3d, {d3d}, Cleanup::Action::Contents, context), {}, QString());
        const QString nvidia = join(b.localAppData, QStringLiteral("NVIDIA"));
        addItem(shaders,
                makeItem(I18n::tr("NVIDIA shader cache"), nvidia,
                         {join(nvidia, QStringLiteral("DXCache")), join(nvidia, QStringLiteral("GLCache"))},
                         Cleanup::Action::Contents, context),
                {}, QString());
        // Un volcado no se regenera: solo los de hace 30 dias o mas.
        const QString dumps = join(b.localAppData, QStringLiteral("CrashDumps"));
        Cleanup::Item crash = makeItem(I18n::tr("Crash dumps older than 30 days"), dumps, {dumps}, Cleanup::Action::OldChildren, context);
        for (Cleanup::Target &target : crash.targets) {
            target.minAgeDays = 30;
        }
        addItem(shaders, crash, {}, QString());
        categories.append(shaders);
    }

    // ---------------------------------------------------------------- Needs administrator (solo informa)
    if (!b.systemRoot.isEmpty() && DeleteGuard::isInside(b.systemRoot, context.volumeRoot)) {
        const QString dump = join(b.systemRoot, QStringLiteral("MEMORY.DMP"));
        if (QFileInfo(dump).isFile()) {
            Cleanup::Category memory;
            memory.id = QStringLiteral("memdump");
            memory.group = Cleanup::Group::Admin;
            memory.single = true;
            memory.info = true;
            memory.title = I18n::tr("System memory dump");
            memory.caption = I18n::tr("Written by Windows when it crashed.");
            Cleanup::Item item;
            item.name = memory.title;
            item.path = dump;
            item.targets.append(Cleanup::Target{dump, Cleanup::Action::Entire, 0});
            memory.items.append(item);
            categories.append(memory);
        }
        Cleanup::Category update;
        update.id = QStringLiteral("winupdate");
        update.group = Cleanup::Group::Admin;
        update.single = true;
        update.info = true;
        update.measurable = false;
        update.title = I18n::tr("Windows Update leftovers");
        update.caption = I18n::tr("Old versions of system components. Windows removes them itself.");
        Cleanup::Item item;
        item.name = update.title;
        item.path = b.systemRoot;
        update.items.append(item);
        categories.append(update);
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
        const QString adobe = join(b.roamingAppData, QStringLiteral("Adobe"));
        QStringList dirs;
        for (const QString &name : subDirs(adobe)) {
            if (name.startsWith(QLatin1String("Bridge"))) {
                dirs.append(join(join(adobe, name), QStringLiteral("Cache")));
            }
        }
        addItem(bridge, makeItem(bridge.title, adobe, dirs, Cleanup::Action::Contents, context), {QStringLiteral("adobe bridge")},
                QStringLiteral("Adobe Bridge"));
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
    if (!b.systemRoot.isEmpty() && DeleteGuard::isInside(b.systemRoot, context.volumeRoot)) {
        const QString installer = join(b.systemRoot, QStringLiteral("Installer"));
        if (QFileInfo(installer).isDir()) {
            Cleanup::Category msi;
            msi.id = QStringLiteral("msi");
            msi.group = Cleanup::Group::Yours;
            msi.single = true;
            msi.info = true;
            msi.title = I18n::tr("Windows Installer patches");
            msi.caption = I18n::tr("Programs need most of them to update or uninstall. Not cleaned from here.");
            msi.reviewPath = installer;
            Cleanup::Item item;
            item.name = msi.title;
            item.path = installer;
            item.targets.append(Cleanup::Target{installer, Cleanup::Action::Entire, 0});
            msi.items.append(item);
            categories.append(msi);
        }
    }
    return categories;
}

} // namespace CleanupRules
