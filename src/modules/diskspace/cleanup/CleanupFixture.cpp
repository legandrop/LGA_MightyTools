#include "modules/diskspace/cleanup/CleanupFixture.h"

#include "core/I18n.h"

#include <QDateTime>

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

QList<Cleanup::Category> categories(const QString &state, qint64 now)
{
    const QString home = QStringLiteral("C:\\Users\\lega");
    const QString portable = QStringLiteral("C:\\Portable");
    QList<Cleanup::Category> list;
    const bool pick = state == QLatin1String("cleanup-pick");
    const bool result = state == QLatin1String("cleanup-result");

    if (!pick && !result) {
        list.append(category(QStringLiteral("bin"), Cleanup::Group::Safe, I18n::tr("Recycle Bin"),
                             I18n::tr("%1 items already deleted once.").arg(QStringLiteral("861")),
                             {item(I18n::tr("Recycle Bin"), QStringLiteral("C:\\"), 9.06, 0, true, now)}, true));
        list.append(category(QStringLiteral("python"), Cleanup::Group::Safe, I18n::tr("Python package caches"),
                             I18n::tr("Downloads kept by pip and uv. Installing a package fetches it again."),
                             {item(QStringLiteral("uv cache"), QStringLiteral("C:\\DevTools\\cache"), 21.56, 1, true, now),
                              item(QStringLiteral("pip cache"), home + QStringLiteral("\\AppData\\Local\\pip\\cache"), 10.35, 7, true, now)}));
        list.append(category(QStringLiteral("media"), Cleanup::Group::Safe, I18n::tr("Media caches"), I18n::tr("Rebuilt when a project opens."),
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
                              item(QStringLiteral("Todoist"), home + QStringLiteral("\\AppData\\Roaming\\Todoist"), 0.30, 0, true, now),
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
        Cleanup::Category msi = category(QStringLiteral("msi"), Cleanup::Group::Yours, I18n::tr("Windows Installer patches"),
                                         I18n::tr("Programs need most of them to update or uninstall. Not cleaned from here."),
                                         {item(I18n::tr("Windows Installer patches"), QStringLiteral("C:\\Windows\\Installer"), 34.0, 4, false, now)},
                                         true);
        msi.info = true;
        msi.reviewPath = QStringLiteral("C:\\Windows\\Installer");
        list.append(msi);
    }
    return list;
}

} // namespace

namespace CleanupFixture {

QStringList states()
{
    return {QStringLiteral("cleanup"), QStringLiteral("cleanup-pick"), QStringLiteral("cleanup-result"), QStringLiteral("folders"),
            QStringLiteral("folders-selected"), QStringLiteral("files"), QStringLiteral("changes"), QStringLiteral("scanning")};
}

Data build(const QString &state, ScanEngine &engine)
{
    Data data;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    data.scanning = state == QLatin1String("scanning");
    data.drive.root = QStringLiteral("C:/");
    data.drive.label = QStringLiteral("C:");
    data.drive.name = QStringLiteral("Windows");
    data.drive.totalBytes = gb(930.5);
    data.drive.freeBytes = state == QLatin1String("cleanup-result") ? gb(91.6) : gb(32.3);

    data.guard.volumeRoot = QStringLiteral("C:\\");
    data.guard.protectedTrees = {QStringLiteral("C:\\Windows"), QStringLiteral("C:\\Program Files"), QStringLiteral("C:\\Program Files (x86)"),
                                 QStringLiteral("C:\\ProgramData"), QStringLiteral("C:\\pagefile.sys"), QStringLiteral("C:\\$Recycle.Bin")};
    data.guard.protectedFolders = {QStringLiteral("C:\\Users"), QStringLiteral("C:\\Users\\lega"), QStringLiteral("C:\\Users\\lega\\AppData"),
                                   QStringLiteral("C:\\Users\\lega\\Desktop"), QStringLiteral("C:\\Users\\lega\\Documents")};

    ScanTree::Index users = ScanTree::kNone;
    ScanTree::Index lega = ScanTree::kNone;
    ScanTree::Index desktop = ScanTree::kNone;
    {
        const auto lock = engine.lock();
        ScanTree &tree = engine.tree();
        tree.reset(QStringLiteral("C:\\"));
        qint64 bytes = 0;
        int files = 0;
        addLevel(tree, tree.root(), 0, 0, now, &bytes, &files);
        // En la raiz, la memoria virtual: un archivo suelto de 32 GB.
        tree.setListed(tree.root(), quint64(gb(32.0)), 1, now - kDay, false, false);
        users = tree.find(QStringLiteral("C:\\Users"));
        lega = tree.find(QStringLiteral("C:\\Users\\lega"));
        desktop = tree.find(QStringLiteral("C:\\Users\\lega\\Desktop"));
    }
    ScanEngine::FileListing rootFiles;
    rootFiles.largest.append(ScanEngine::FileEntry{QStringLiteral("pagefile.sys"), quint64(gb(32.0)), now - kDay});
    data.listings.insert(0, rootFiles);

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
    if (state == QLatin1String("cleanup-pick")) {
        data.openCategories = {QStringLiteral("rule:0")};
    } else if (state == QLatin1String("cleanup-result")) {
        data.openCategories.clear();
        data.banner = I18n::tr("Freed %1. %2 has %3 free.").arg(QStringLiteral("59.3 GB"), QStringLiteral("C:"), QStringLiteral("91.6 GB"));
        data.skippedSummary = I18n::tr("%1 files · %2").arg(QStringLiteral("212"), QStringLiteral("922 MB"));
        data.skipped = {{I18n::tr("Browser caches") + QStringLiteral(" · Brave"), I18n::tr("%1 is open").arg(QStringLiteral("Brave"))},
                        {I18n::tr("Temporary files"), I18n::tr("%1 files in use").arg(64)}};
    } else if (data.scanning) {
        // Como en el canvas: solo el primer nivel, con varias carpetas todavia contandose.
        data.tab = 1;
        const auto lock = engine.lock();
        for (const char16_t *name : {u"C:\\Users", u"C:\\Program Files", u"C:\\Portable", u"C:\\ProgramData", u"C:\\Qt"}) {
            data.counting.insert(engine.tree().find(QString::fromUtf16(name)));
        }
    } else if (state == QLatin1String("folders") || state == QLatin1String("folders-selected")) {
        data.tab = 1;
        data.expanded = {users, lega};
        if (state == QLatin1String("folders-selected")) {
            data.selectedPaths = {QStringLiteral("C:\\Users\\lega\\.cache"), QStringLiteral("C:\\Users\\lega\\miniconda3")};
        }
    } else if (state == QLatin1String("files")) {
        data.tab = 2;
    } else if (state == QLatin1String("changes")) {
        data.tab = 3;
        data.baseline.root = QStringLiteral("C:\\");
        data.baseline.takenAt = QDateTime(QDate::currentDate().addDays(-1), QTime(18, 10));
        data.usedDelta = gb(6.4);
        data.changes = {{QStringLiteral("C:\\Users\\lega\\AppData\\Local\\Temp"), gb(3.1), gb(5.0)},
                        {QStringLiteral("C:\\Users\\lega\\.codex"), gb(1.9), gb(12.5)},
                        {QStringLiteral("C:\\Portable\\LGA_PipeSync_2\\build"), gb(1.2), gb(5.5)},
                        {QStringLiteral("C:\\Users\\lega\\AppData\\Local\\Packages"), gb(0.6), gb(25.8)},
                        {QStringLiteral("C:\\Users\\lega\\AppData\\Local\\pip"), gb(0.5), gb(10.4)},
                        {QStringLiteral("C:\\$Recycle.Bin"), -gb(0.4), gb(9.1)},
                        {QStringLiteral("C:\\Users\\lega\\Desktop"), -gb(0.5), gb(25.2)}};
    }
    return data;
}

} // namespace CleanupFixture
