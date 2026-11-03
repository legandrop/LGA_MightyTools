#include "modules/diskspace/cleanup/CleanupQa.h"

#include "core/AutomatedRun.h"
#include "modules/diskspace/cleanup/AppCacheRules.h"
#include "modules/diskspace/cleanup/CleanupJob.h"
#include "modules/diskspace/cleanup/CleanupRules.h"
#include "modules/diskspace/cleanup/DeleteGuard.h"
#include "modules/diskspace/cleanup/ScanEngine.h"
#include "platform/FileSystemOps.h"
#include "platform/SystemPaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QTemporaryDir>
#include <QThread>
#include <QtEndian>

// Self-test de las caches de programas de video, audio y VFX (AppCacheRules) sobre una carpeta de pruebas
// PROPIA en %TEMP%, con el mismo criterio que CleanupSelfTest.cpp: antes de nada prueba el aislamiento con
// un canario fuera de esa carpeta, y solo borra lo que el mismo creo adentro. Las preferencias, el
// registro y las variables de entorno salen de Context::sources: no se lee nada de esta maquina.

namespace {

using Check = std::function<void(bool ok, const QString &what)>;

bool writeFile(const QString &path, const QByteArray &bytes = QByteArray(4096, 'x'))
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(bytes);
    return true;
}

QByteArray utf16(const QString &text)
{
    QByteArray out;
    for (const QChar c : text) {
        out.append(char(c.unicode() & 0xFF));
        out.append(char(c.unicode() >> 8));
    }
    return out;
}

QByteArray le32(quint32 value)
{
    QByteArray bytes(4, '\0');
    qToLittleEndian(value, bytes.data());
    return bytes;
}

// Un Preferences.cfg de Ableton armado como el de Live 11: el esquema y despues los valores, con la ruta en
// UTF-16 detras de su largo. `gap` son los bytes entre el nombre del bloque y el largo (13 en Live 11).
QByteArray abletonCfg(const QString &path, int gap)
{
    QByteArray cfg("\x00\x11", 2);
    cfg += "FileCachePrefData";
    cfg += le32(4) + le32(18) + utf16(QStringLiteral("CacheMinLimitValue")) + QByteArray("\x00\x09UserFloat", 11);
    cfg += QByteArray(64, 'x');
    cfg += "FileCachePrefData";
    QByteArray middle = le32(6) + QByteArray("\x00\x00\x00\x3F", 4) + QByteArray(1, '\0') + QByteArray("\x00\x00\x20\x41", 4);
    middle = middle.left(gap);
    while (middle.size() < gap) {
        middle.append('\0');
    }
    cfg += middle + le32(quint32(path.size())) + utf16(path) + QByteArray("\x00\x0eRecent", 8);
    return cfg;
}

bool waitScan(ScanEngine &engine)
{
    for (int i = 0; i < 1000 && engine.isRunning(); ++i) {
        QThread::msleep(5);
    }
    return !engine.isRunning() && engine.progress().complete;
}

struct Offered
{
    QStringList targets;            ///< todos, de las categorias de estas reglas
    QHash<QString, QStringList> by; ///< por id de categoria
    QStringList shown;              ///< lo que se ve de cada item: su detalle o, si no tiene, su ruta
    bool yoursUnchecked = true;
    bool safeChecked = true;
};

const QStringList kOwnIds = {QStringLiteral("nuke"),     QStringLiteral("media"),    QStringLiteral("shaders"),
                             QStringLiteral("rendercache"), QStringLiteral("pstemp"), QStringLiteral("nukework"),
                             QStringLiteral("previews"), QStringLiteral("appdownloads")};

Offered offered(const QList<Cleanup::Category> &categories)
{
    Offered o;
    for (const Cleanup::Category &category : categories) {
        if (!kOwnIds.contains(category.id)) {
            continue;
        }
        for (const Cleanup::Item &item : category.items) {
            if (category.group == Cleanup::Group::Yours && item.checked) {
                o.yoursUnchecked = false;
            }
            if (category.group == Cleanup::Group::Safe && !item.checked && !item.blocked) {
                o.safeChecked = false;
            }
            o.shown.append(QDir::toNativeSeparators(item.detail.isEmpty() ? item.path : item.detail));
            for (const Cleanup::Target &target : item.targets) {
                o.targets.append(target.path);
                o.by[category.id].append(target.path);
            }
        }
    }
    return o;
}

bool hasPath(const QStringList &list, const QString &path)
{
    for (const QString &entry : list) {
        if (DeleteGuard::samePath(entry, path)) {
            return true;
        }
    }
    return false;
}

// Alguno empieza en esa carpeta (es ella o esta adentro).
bool touches(const QStringList &list, const QString &dir)
{
    for (const QString &entry : list) {
        if (DeleteGuard::samePath(entry, dir) || DeleteGuard::isInside(entry, dir)) {
            return true;
        }
    }
    return false;
}

void selfTestParsers(const Check &check)
{
    const QString prefs = QStringLiteral(
        "Preferences {\n"
        " inputs 0\n"
        " localCachePath \"\\[getenv NUKE_TEMP_DIR]/localize\"\n"
        " DiskCachePath \"E:/z cache/\"\n"
        "TimelineDiskCachePath E:/t/\n"
        " contactSheetDefaultResolution \"1,\\[ 0, 0, 1920, 1080],1\"\n"
        " PaintCachePath [getenv NUKE_TEMP_DIR]/paint extra\n"
        " tileCachePath {E:/my tiles}\n"
        "}\n");
    bool found = false;
    check(AppCacheRules::nukeValue(prefs, QStringLiteral("DiskCachePath"), &found) == QStringLiteral("E:/z cache/") && found
              && AppCacheRules::nukeValue(prefs, QStringLiteral("TimelineDiskCachePath"), nullptr) == QStringLiteral("E:/t/"),
          QStringLiteral("nuke: una preferencia entre comillas (con espacios) y otra sin comillas ni sangria"));
    check(AppCacheRules::nukeValue(prefs, QStringLiteral("localCachePath"), nullptr) == QStringLiteral("[getenv NUKE_TEMP_DIR]/localize")
              && AppCacheRules::nukeValue(prefs, QStringLiteral("contactSheetDefaultResolution"), nullptr)
                     == QStringLiteral("1,[ 0, 0, 1920, 1080],1"),
          QStringLiteral("nuke: los corchetes escapados (\\[) quedan como corchetes"));
    check(AppCacheRules::nukeValue(prefs, QStringLiteral("PaintCachePath"), nullptr) == QStringLiteral("[getenv NUKE_TEMP_DIR]/paint")
              && AppCacheRules::nukeValue(prefs, QStringLiteral("tileCachePath"), nullptr) == QStringLiteral("E:/my tiles"),
          QStringLiteral("nuke: sin comillas, el espacio dentro de [getenv X] no corta la palabra; entre llaves va literal"));
    AppCacheRules::nukeValue(prefs, QStringLiteral("CachePath"), &found);
    check(!found, QStringLiteral("nuke: una clave que es el final de otra (CachePath) no cuenta"));
    const auto getenv = [](const QString &name) {
        return name == QLatin1String("NUKE_TEMP_DIR") ? QStringLiteral("C:/T/nuke/") : QString();
    };
    check(AppCacheRules::resolveNukeExpression(QStringLiteral("[getenv NUKE_TEMP_DIR]/localize"), getenv)
                  == QDir::toNativeSeparators(QStringLiteral("C:/T/nuke/localize"))
              && AppCacheRules::resolveNukeExpression(QStringLiteral("[getenv MISSING]/x"), getenv).isEmpty()
              && AppCacheRules::resolveNukeExpression(QStringLiteral("[file dirname x]/y"), getenv).isEmpty()
              && AppCacheRules::resolveNukeExpression(QStringLiteral("$env(HOME)/y"), getenv).isEmpty(),
          QStringLiteral("nuke: [getenv X] se resuelve; una variable vacia u otra expresion no da ruta"));

    const QString ae = QStringLiteral(
        "[\"Disk Cache Controls\"]\n"
        "\t\"Enabled 2\" = \"1\"\n"
        "\t\"Folder 7\" = \"C:\\Users\\u\\AE Cache\"\n"
        "\t\"Max Size 3\" = \"100\"\n"
        "[\"Other Section\"]\n"
        "\t\"Folder 7\" = \"X:\\no\"\n");
    const QString aeLong = QStringLiteral(
        "[\"Disk Cache Controls\"]\n"
        "\t\"Folder 9\" = \n"
        "\t\t\"D:\\\\Cache\\\\\"\n"
        "\t\t\"AE\"\n"
        "\t\"Size\" = \"1\"\n");
    const QStringList aeFolders = AppCacheRules::afterEffectsCacheFolders(ae);
    const QStringList aeLongFolders = AppCacheRules::afterEffectsCacheFolders(aeLong);
    check(aeFolders.size() == 1 && aeFolders.first() == QDir::toNativeSeparators(QDir::cleanPath(QStringLiteral("C:\\Users\\u\\AE Cache")))
              && aeLongFolders.size() == 1
              && aeLongFolders.first() == QDir::toNativeSeparators(QDir::cleanPath(QStringLiteral("D:\\Cache\\AE"))),
          QStringLiteral("after effects: \"Folder N\" solo de \"Disk Cache Controls\", tambien partido en varias lineas (%1 | %2)")
              .arg(aeFolders.join(QLatin1Char(',')), aeLongFolders.join(QLatin1Char(','))));

    const QString path = QStringLiteral("D:/Audio/Ableton Cache");
    check(AppCacheRules::abletonCacheDirectory(abletonCfg(path, 13)) == QDir::toNativeSeparators(path)
              && AppCacheRules::abletonCacheDirectory(abletonCfg(path, 9)) == QDir::toNativeSeparators(path),
          QStringLiteral("ableton: CacheDirectory del ultimo FileCachePrefData, en su lugar de Live 11 o unos bytes mas aca"));
    check(AppCacheRules::abletonCacheDirectory(QByteArray("FileCachePrefData") + le32(6)).isEmpty()
              && AppCacheRules::abletonCacheDirectory(QByteArray(200, 'z')).isEmpty()
              && AppCacheRules::abletonCacheDirectory(QByteArray("\x11" "FileCachePrefData", 18) + le32(4) + le32(18)
                                                      + utf16(QStringLiteral("CacheMinLimitValue")))
                     .isEmpty(),
          QStringLiteral("ableton: un archivo cortado, sin el bloque o con solo el esquema no da ruta"));

    check(AppCacheRules::reaperPeaksFolder(QStringLiteral("[audio]\naltpeakspath=X:\\no\n[REAPER]\naltpeaks=1\r\naltpeakspath=E:\\Peaks\r\n"))
                  == QDir::toNativeSeparators(QDir::cleanPath(QStringLiteral("E:\\Peaks")))
              && AppCacheRules::reaperPeaksFolder(QStringLiteral("[REAPER]\naltpeaks=0\naltpeakspath=E:\\Peaks\n")).isEmpty()
              && AppCacheRules::reaperPeaksFolder(QStringLiteral("[REAPER]\naltpeaks=4\naltpeakspath=\n")).isEmpty(),
          QStringLiteral("reaper: altpeakspath solo con altpeaks activado y una ruta"));
    check(AppCacheRules::unrealLocalDdcPath(QStringLiteral("[/Script/UnrealEd.EditorSettings]\nbX=1\nGlobalLocalDDCPath=(Path=\"D:/DDC\")\n"))
                  == QDir::toNativeSeparators(QStringLiteral("D:/DDC"))
              && AppCacheRules::unrealLocalDdcPath(QStringLiteral("[x]\nGlobalSharedDDCPath=(Path=\"\\\\server\\ddc\")\n")).isEmpty(),
          QStringLiteral("unreal: GlobalLocalDDCPath, nunca la DDC compartida"));

    // Lo que ya se lleva la regla de temporales (una hija sin tocar en 7 dias) no se cuenta dos veces.
    {
        // Un arbol armado a mano sobre una carpeta que no existe (en la temporal, para no tocar otra unidad):
        // dropInsideSwept mira las fechas de la hija en el disco y no las encuentra.
        const QString root = QDir::toNativeSeparators(QDir(QDir::tempPath()).filePath(QStringLiteral("lga_mt_no_such_dir_5c1e")));
        const qint64 now = 2000000000;
        const qint64 old = now - 30 * 86400;
        ScanTree tree;
        tree.reset(root);
        const ScanTree::Index temp = tree.addChild(tree.root(), u"Temp");
        const ScanTree::Index nuke = tree.addChild(temp, u"nuke");
        const ScanTree::Index viewer = tree.addChild(nuke, u"ViewerCache");
        tree.setListed(viewer, 100, 1, old, false, false);
        tree.setListed(nuke, 0, 0, old, false, false);
        const ScanTree::Index young = tree.addChild(temp, u"Disk Cache - HOST");
        tree.setListed(young, 100, 1, now - 3600, false, false);
        tree.setListed(temp, 0, 0, old, false, false);
        const ScanTree::Index other = tree.addChild(tree.root(), u"Cache");
        tree.setListed(other, 100, 1, old, false, false);
        tree.setListed(tree.root(), 0, 0, old, false, false);
        const QString tempPath = tree.path(temp);
        const QString viewerPath = tree.path(viewer);
        const QString youngPath = tree.path(young);
        const QString otherPath = tree.path(other);
        const auto make = [](const QString &name, const QStringList &paths) {
            Cleanup::Item item;
            item.name = name;
            for (const QString &path : paths) {
                item.targets.append(Cleanup::Target{path, Cleanup::Action::Contents, 0});
            }
            return item;
        };
        Cleanup::Category sweep;
        sweep.id = QStringLiteral("temp");
        sweep.items = {make(QStringLiteral("temp"), {})};
        sweep.items.first().targets = {Cleanup::Target{tempPath, Cleanup::Action::OldChildren, 7}};
        Cleanup::Category caches;
        caches.id = QStringLiteral("nuke");
        caches.items = {make(QStringLiteral("old"), {viewerPath}), make(QStringLiteral("young"), {youngPath}),
                        make(QStringLiteral("outside"), {otherPath}), make(QStringLiteral("mixed"), {viewerPath, otherPath})};
        QList<Cleanup::Category> categories = {sweep, caches};
        CleanupRules::dropInsideSwept(categories, tree, now);
        QStringList left;
        for (const Cleanup::Item &item : categories.at(1).items) {
            left.append(item.name);
        }
        check(categories.at(0).items.size() == 1 && left == QStringList({QStringLiteral("young"), QStringLiteral("outside"), QStringLiteral("mixed")}),
              QStringLiteral("temporales: una cache dentro de una hija de %TEMP% sin tocar en 7 dias va con esa regla, no aparte (%1)")
                  .arg(left.join(QLatin1Char(','))));
    }

    const QSet<QString> running = {QStringLiteral("nuke17.0"), QStringLiteral("lga_nukeshortcuts"), QStringLiteral("ableton live 11 suite")};
    check(Cleanup::blockerRunning(running, QStringLiteral("nuke1*")) && Cleanup::blockerRunning(running, QStringLiteral("ableton live*"))
              && !Cleanup::blockerRunning(running, QStringLiteral("nuke2*")) && !Cleanup::blockerRunning(running, QStringLiteral("*"))
              && !Cleanup::blockerRunning(running, QStringLiteral("nuke")) && Cleanup::blockerRunning(running, QStringLiteral("nuke17.0")),
          QStringLiteral("bloqueos: un bloqueo con * es un prefijo; sin *, el nombre exacto"));
}

} // namespace

namespace CleanupQa {

void selfTestAppCaches(const Check &check)
{
    selfTestParsers(check);
    if (!SystemPaths::cleanupSupported()) {
        return;
    }
    QTemporaryDir sandboxDir(QDir(QDir::tempPath()).filePath(QStringLiteral("lga_mt_appcache_XXXXXX")));
    QTemporaryDir outsideDir(QDir(QDir::tempPath()).filePath(QStringLiteral("lga_mt_appcanary_XXXXXX")));
    check(sandboxDir.isValid() && outsideDir.isValid(), QStringLiteral("caches de programas: se crean la carpeta de pruebas y la del canario"));
    if (!sandboxDir.isValid() || !outsideDir.isValid()) {
        return;
    }
    const QString sandbox = FileSystemOps::canonicalPath(sandboxDir.path());
    const QString outside = FileSystemOps::canonicalPath(outsideDir.path());
    const QChar sep = QDir::separator();
    const auto at = [sep](const QString &dir, const QString &relative) {
        return dir + sep + QDir::toNativeSeparators(relative);
    };
    const QString canary = at(outside, QStringLiteral("canary.txt"));
    writeFile(canary);
    const QString vol = at(sandbox, QStringLiteral("vol"));
    const QString otherVol = at(sandbox, QStringLiteral("othervol"));
    // Al salir: los enlaces se sacan como enlaces (QTemporaryDir entraria en ellos) y se suelta la carpeta.
    struct Closer
    {
        QStringList links;
        ~Closer()
        {
            for (const QString &link : links) {
                if (FileSystemOps::kind(link) == FileSystemOps::Kind::Link) {
                    FileSystemOps::removeDir(FileSystemOps::nativePath(link));
                }
            }
            AutomatedRun::setSandbox(QString());
        }
    } closer;

    // ---- Aislamiento, antes que nada.
    const bool refusedUnregistered = FileSystemOps::removeFile(FileSystemOps::nativePath(canary)) == FileSystemOps::Result::Denied;
    AutomatedRun::setSandbox(sandbox);
    const bool refusedOutside = FileSystemOps::removeFile(FileSystemOps::nativePath(canary)) == FileSystemOps::Result::Denied;
    const bool isolated = AutomatedRun::active() && refusedUnregistered && refusedOutside && QFileInfo::exists(canary)
                          && !AutomatedRun::mayModify(sandbox) && AutomatedRun::mayModify(at(sandbox, QStringLiteral("x")));
    check(isolated, QStringLiteral("caches de programas: AISLAMIENTO - fuera de la carpeta de pruebas el borrado se rechaza y el canario sigue"));
    if (!isolated) {
        return;
    }

    // ---- La excepcion de la guarda (modelos de Topaz en ProgramData): solo adentro, nunca la carpeta ni arriba.
    {
        DeleteGuard guard;
        guard.volumeRoot = DeleteGuard::clean(vol);
        const QString programData = at(vol, QStringLiteral("PD"));
        const QString models = at(programData, QStringLiteral("Topaz Labs LLC/Topaz Video/models/models"));
        guard.protectedTrees = {programData};
        guard.allowedTrees = {models};
        check(guard.check(models, DeleteGuard::Scope::Contents, false) == DeleteGuard::Verdict::Ok
                  && guard.check(at(models, QStringLiteral("astra")), DeleteGuard::Scope::Entire, false) == DeleteGuard::Verdict::Ok
                  && guard.check(models, DeleteGuard::Scope::Entire, false) == DeleteGuard::Verdict::ProtectedTree
                  && guard.check(QFileInfo(models).absolutePath(), DeleteGuard::Scope::Contents, false) == DeleteGuard::Verdict::ProtectedTree
                  && guard.check(at(programData, QStringLiteral("Topaz Labs LLC/Topaz Video/models/auth.tpz")), DeleteGuard::Scope::Entire, false)
                         == DeleteGuard::Verdict::ProtectedTree
                  && guard.check(programData, DeleteGuard::Scope::Contents, false) == DeleteGuard::Verdict::ProtectedTree,
              QStringLiteral("guarda: dentro de ProgramData solo se vacia la carpeta de modelos exceptuada; ella, lo de al lado y lo de arriba no"));
        // Un arbol protegido que NO contiene la excepcion (esta dentro de ella) sigue protegido.
        const QString locked = at(models, QStringLiteral("locked"));
        guard.protectedTrees = {programData, locked};
        check(guard.check(at(locked, QStringLiteral("x.bin")), DeleteGuard::Scope::Entire, false) == DeleteGuard::Verdict::ProtectedTree
                  && guard.check(at(models, QStringLiteral("astra")), DeleteGuard::Scope::Entire, false) == DeleteGuard::Verdict::Ok
                  && guard.check(models, DeleteGuard::Scope::Contents, false) == DeleteGuard::Verdict::ContainsProtected,
              QStringLiteral("guarda: la excepcion vale solo frente a los arboles que la contienen; uno de adentro sigue protegido"));
    }

    // ---- El perfil de mentira.
    const QString profile = at(vol, QStringLiteral("Users/u"));
#ifdef Q_OS_WIN
    const QString local = at(profile, QStringLiteral("AppData/Local"));
    const QString roaming = at(profile, QStringLiteral("AppData/Roaming"));
    const QString temp = at(local, QStringLiteral("Temp"));
    const QString aePrefs = at(roaming, QStringLiteral("Adobe/After Effects/24.0/Adobe After Effects 24.0 Prefs.txt"));
    const QString abletonPrefs = at(roaming, QStringLiteral("Ableton"));
    const QString abletonCache = at(local, QStringLiteral("Ableton/Cache"));
    const QString reaperIni = at(roaming, QStringLiteral("REAPER/reaper.ini"));
    const QString maxon = at(roaming, QStringLiteral("Maxon"));
#else
    const QString library = at(profile, QStringLiteral("Library"));
    const QString caches = at(library, QStringLiteral("Caches"));
    const QString support = at(library, QStringLiteral("Application Support"));
    const QString temp = at(profile, QStringLiteral("tmp"));
    const QString aePrefs = at(library, QStringLiteral("Preferences/Adobe/After Effects/24.0/Adobe After Effects 24.0 Prefs.txt"));
    const QString abletonPrefs = at(library, QStringLiteral("Preferences/Ableton"));
    const QString abletonCache = at(caches, QStringLiteral("Ableton/Cache"));
    const QString reaperIni = at(support, QStringLiteral("REAPER/reaper.ini"));
    const QString maxon = at(library, QStringLiteral("Preferences/Maxon"));
#endif
    QDir().mkpath(temp);
    const QString slashVol = QDir::fromNativeSeparators(vol);

    // ---- Nuke: preferencias de varias versiones, con comillas, \[ y [getenv]; una vacia (va el defecto); y
    // las que apuntan a la raiz, a la carpeta del usuario, a una carpeta con un .nk, a una sin marcas y a un enlace.
    const QString dotNuke = at(profile, QStringLiteral(".nuke"));
    const QString nukeCache = at(vol, QStringLiteral("NukeCache"));
    const auto prefsWith = [](const QString &lines) {
        return QStringLiteral("Preferences {\n inputs 0\n name Preferences\n%1}\n").arg(lines).toUtf8();
    };
    writeFile(at(dotNuke, QStringLiteral("preferences17.0.nk")),
              prefsWith(QStringLiteral(" localCachePath \"%1/NukeLocalize/\"\n DiskCachePath \"%1/NukeCache/\"\n"
                                       " TimelineDiskCachePath \"%1/NukeCache/\"\n contactSheetDefaultResolution \"1,\\[ 0, 0, 1920, 1080],1\"\n")
                            .arg(slashVol)));
    writeFile(at(dotNuke, QStringLiteral("preferences16.0.nk")), prefsWith(QStringLiteral("DiskCachePath \"\\[getenv LGA_TEST_CACHE]/FromEnv\"\n")));
    writeFile(at(dotNuke, QStringLiteral("preferences15.0.nk")), prefsWith(QString()));
    writeFile(at(dotNuke, QStringLiteral("preferences14.0.nk")), prefsWith(QStringLiteral(" DiskCachePath \"%1\"\n").arg(slashVol)));
    writeFile(at(dotNuke, QStringLiteral("preferences13.1.nk")),
              prefsWith(QStringLiteral(" DiskCachePath \"%1\"\n").arg(QDir::fromNativeSeparators(profile))));
    writeFile(at(dotNuke, QStringLiteral("preferences13.0.nk")), prefsWith(QStringLiteral(" DiskCachePath %1/Shots\n").arg(slashVol)));
    writeFile(at(dotNuke, QStringLiteral("preferences12.0.nk")), prefsWith(QStringLiteral(" DiskCachePath %1/NoMarks\n").arg(slashVol)));
    writeFile(at(dotNuke, QStringLiteral("preferences11.0.nk")), prefsWith(QStringLiteral(" DiskCachePath %1/LinkCache\n").arg(slashVol)));
    writeFile(at(dotNuke, QStringLiteral("preferences10.0.nk")), prefsWith(QStringLiteral(" DiskCachePath %1/NukeCache2\n").arg(slashVol)));
    writeFile(at(dotNuke, QStringLiteral("preferences9.0.nk")),
              prefsWith(QStringLiteral(" DiskCachePath %1/NukeCache\n").arg(QDir::fromNativeSeparators(otherVol))));
    const QString hex64 = QStringLiteral("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    writeFile(at(nukeCache, QStringLiteral("ViewerCache/00/a.nkc")));
    QDir().mkpath(at(nukeCache, QStringLiteral("ViewerCache/ff")));
    writeFile(at(nukeCache, QStringLiteral("audiocache/") + hex64));
    writeFile(at(nukeCache, QStringLiteral("thumbnailcache/") + hex64 + QStringLiteral(".cch")));
    writeFile(at(nukeCache, QStringLiteral("tilecache/00/t.cache")));
    writeFile(at(nukeCache, QStringLiteral("tilecache/DiskSize")), QByteArray("1"));
    writeFile(at(nukeCache, QStringLiteral("jitCache-host/BlockCache/b.cch")));
    writeFile(at(nukeCache, QStringLiteral("jitCache-host/LocalisedFiles/plate.exr")));
    // El timeline con su forma entera: las 256 carpetas 00..ff, una con un cuadro.
    const auto makeBuckets = [&](const QString &dir) {
        for (int i = 0; i < 256; ++i) {
            QDir().mkpath(at(dir, QStringLiteral("%1").arg(i, 2, 16, QLatin1Char('0'))));
        }
    };
    // Un cuadro del timeline: nombre hexadecimal, sin extension (como los de audiocache).
    const QString frame = QStringLiteral("0123456789abcdef0123456789abcdef");
    makeBuckets(nukeCache);
    writeFile(at(nukeCache, QStringLiteral("00/") + frame));
    writeFile(at(nukeCache, QStringLiteral("timeline_cache.db")));
    // Carpetas de trabajo a las que apunta un TimelineDiskCachePath, con un timeline_cache.db: una con solo
    // "01" y "10" (con subcarpetas y media), otra con las 256 pero una con subcarpeta, otra con las 256 pero
    // una con un .exr, y otra con las 256 pero un .txt y un .pdf. De ninguna se ofrece nada. Y una con solo
    // "01" con un cuadro: tiene la forma, pero sin el juego completo queda solo su timeline_cache.db.
    const QString work1 = at(vol, QStringLiteral("Work1"));
    const QString work2 = at(vol, QStringLiteral("Work2"));
    const QString work3 = at(vol, QStringLiteral("Work3"));
    const QString work4 = at(vol, QStringLiteral("Work4"));
    const QString work5 = at(vol, QStringLiteral("Work5"));
    writeFile(at(work1, QStringLiteral("01/plates/plate.0001.exr")));
    writeFile(at(work1, QStringLiteral("10/render_v001.mov")));
    writeFile(at(work1, QStringLiteral("timeline_cache.db")));
    makeBuckets(work2);
    writeFile(at(work2, QStringLiteral("05/") + frame));
    writeFile(at(work2, QStringLiteral("3a/sub/x.bin")));
    writeFile(at(work2, QStringLiteral("timeline_cache.db")));
    makeBuckets(work3);
    writeFile(at(work3, QStringLiteral("05/") + frame));
    writeFile(at(work3, QStringLiteral("7f/shot.exr")));
    writeFile(at(work3, QStringLiteral("timeline_cache.db")));
    makeBuckets(work4);
    writeFile(at(work4, QStringLiteral("05/") + frame));
    writeFile(at(work4, QStringLiteral("01/notes.txt")));
    writeFile(at(work4, QStringLiteral("10/brief.pdf")));
    writeFile(at(work4, QStringLiteral("timeline_cache.db")));
    writeFile(at(work5, QStringLiteral("01/") + frame));
    writeFile(at(work5, QStringLiteral("timeline_cache.db")));
    writeFile(at(dotNuke, QStringLiteral("preferences8.0.nk")), prefsWith(QStringLiteral(" TimelineDiskCachePath %1/Work1\n").arg(slashVol)));
    writeFile(at(dotNuke, QStringLiteral("preferences7.0.nk")), prefsWith(QStringLiteral(" TimelineDiskCachePath %1/Work2\n").arg(slashVol)));
    writeFile(at(dotNuke, QStringLiteral("preferences6.0.nk")), prefsWith(QStringLiteral(" TimelineDiskCachePath %1/Work3\n").arg(slashVol)));
    writeFile(at(dotNuke, QStringLiteral("preferences5.0.nk")), prefsWith(QStringLiteral(" TimelineDiskCachePath %1/Work4\n").arg(slashVol)));
    writeFile(at(dotNuke, QStringLiteral("preferences4.0.nk")), prefsWith(QStringLiteral(" TimelineDiskCachePath %1/Work5\n").arg(slashVol)));
    writeFile(at(nukeCache, QStringLiteral("keep.txt")));
    writeFile(at(nukeCache, QStringLiteral("notes/n.txt")));
    writeFile(at(vol, QStringLiteral("NukeLocalize/x.exr")));
    writeFile(at(vol, QStringLiteral("FromEnv/ViewerCache/01/b.nkc")));
    writeFile(at(vol, QStringLiteral("nuketemp/ViewerCache/02/c.nkc")));
    writeFile(at(vol, QStringLiteral("ViewerCache/00/root.nkc")));
    writeFile(at(profile, QStringLiteral("ViewerCache/00/home.nkc")));
    writeFile(at(vol, QStringLiteral("Shots/shot010.nk")));
    writeFile(at(vol, QStringLiteral("Shots/ViewerCache/00/s.nkc")));
    writeFile(at(vol, QStringLiteral("NoMarks/ViewerCache/notes.txt")));
    writeFile(at(vol, QStringLiteral("NoMarks/audiocache/readme.txt")));
    writeFile(at(vol, QStringLiteral("NoMarks/thumbnailcache/x.cch")));
    writeFile(at(vol, QStringLiteral("NoMarks/thumbnailcache/x.txt")));
    writeFile(at(sandbox, QStringLiteral("linktarget/ViewerCache/00/l.nkc")));
    writeFile(at(vol, QStringLiteral("elsewhere/00/e.nkc")));
    QDir().mkpath(at(vol, QStringLiteral("NukeCache2")));
    writeFile(at(otherVol, QStringLiteral("NukeCache/ViewerCache/00/o.nkc")));
    const QString linkCache = at(vol, QStringLiteral("LinkCache"));
    const QString linkViewer = at(vol, QStringLiteral("NukeCache2/ViewerCache"));
    closer.links = {linkCache, linkViewer};
    const bool linked = FileSystemOps::createDirLink(linkCache, at(sandbox, QStringLiteral("linktarget")))
                        && FileSystemOps::createDirLink(linkViewer, at(vol, QStringLiteral("elsewhere")));
    check(linked && FileSystemOps::kind(linkCache) == FileSystemOps::Kind::Link,
          QStringLiteral("caches de programas: los enlaces de prueba se crean"));

    // ---- After Effects: la carpeta de "Folder 7" (con barras simples, como la escribe AE) y adentro la suya.
    writeFile(aePrefs, QStringLiteral("[\"Disk Cache Controls\"]\n\t\"Enabled 2\" = \"1\"\n\t\"Folder 7\" = \"%1\"\n")
                           .arg(QDir::toNativeSeparators(at(vol, QStringLiteral("AECache"))))
                           .toUtf8());
    writeFile(at(vol, QStringLiteral("AECache/Disk Cache - HOST/x.bin")));
    writeFile(at(vol, QStringLiteral("AECache/keep.txt")));

    // ---- Ableton: la carpeta de Preferences.cfg, la de fabrica, y un Preferences.cfg cortado.
    const QString abletonConfigured = at(vol, QStringLiteral("AbletonCache"));
#ifdef Q_OS_WIN
    writeFile(at(abletonPrefs, QStringLiteral("Live 12.0.5/Preferences/Preferences.cfg")),
              abletonCfg(QDir::fromNativeSeparators(abletonConfigured), 13));
    writeFile(at(abletonPrefs, QStringLiteral("Live 11.0.0/Preferences/Preferences.cfg")), QByteArray("FileCachePrefData") + le32(6));
    const QString abletonDefault = at(abletonCache, QStringLiteral("Cache"));
    const QString packs = at(abletonCache, QStringLiteral("PackDownloads"));
#else
    writeFile(at(abletonPrefs, QStringLiteral("Live 12.0.5/Preferences.cfg")), abletonCfg(abletonConfigured, 13));
    writeFile(at(abletonPrefs, QStringLiteral("Live 11.0.0/Preferences.cfg")), QByteArray("FileCachePrefData") + le32(6));
    const QString abletonDefault = abletonCache;
    const QString packs = at(caches, QStringLiteral("Ableton/PackDownloads"));
#endif
    writeFile(at(abletonConfigured, QStringLiteral("Decoding/CacheContent.idx")), QByteArray("idx"));
    writeFile(at(abletonConfigured, QStringLiteral("Decoding/x.wav")));
    writeFile(at(abletonDefault, QStringLiteral("Decoding/CacheContent.idx")), QByteArray("idx"));
    writeFile(at(abletonDefault, QStringLiteral("Decoding/y.wav")));
    writeFile(at(packs, QStringLiteral("pack.alp")));

    // ---- Resolve, Premiere y Blender: por la forma de sus carpetas.
    writeFile(at(vol, QStringLiteral("media/CacheClip/abc/x.dvcc")));
    writeFile(at(vol, QStringLiteral("media/OptimizedMedia/o.mov")));
    writeFile(at(vol, QStringLiteral("media/ProxyMedia/p.mov")));
    writeFile(at(vol, QStringLiteral("media/.gallery/still.dpx")));
    writeFile(at(vol, QStringLiteral("other/CacheClip/notes.txt")));
    writeFile(at(vol, QStringLiteral("other/OptimizedMedia/w.mov")));
    writeFile(at(vol, QStringLiteral("edit/Adobe Premiere Pro Video Previews/v.mpeg")));
    writeFile(at(vol, QStringLiteral("edit2/Adobe Premiere Pro Audio Previews/a.cfa")));
    writeFile(at(vol, QStringLiteral("edit2/Adobe Premiere Pro Audio Previews/edit.prproj")));
    writeFile(at(vol, QStringLiteral("footage/BL_proxy/clip/proxy_50.avi")));

    // ---- Adobe Media Cache movida: la carpeta madre, la propia, y una sin marcas.
    writeFile(at(vol, QStringLiteral("AdobeMC/Media Cache Files/a.cfa")));
    writeFile(at(vol, QStringLiteral("AdobeMC/Peak Files/b.pek")));
    writeFile(at(vol, QStringLiteral("AdobeMC/Media Cache/db.mcdb")));
    writeFile(at(vol, QStringLiteral("AdobeMC/keep.txt")));
    writeFile(at(vol, QStringLiteral("AdobeMC2/Media Cache Files/c.pek")));
    writeFile(at(vol, QStringLiteral("AdobeMC2/other.txt")));
    writeFile(at(vol, QStringLiteral("AdobeEmpty/Media Cache Files/readme.txt")));
    // Una carpeta del usuario llamada "Media Cache" con la cache adentro (y cosas suyas al lado), y una
    // "Media Cache Files" con un archivo ajeno.
    const QString userMc = at(vol, QStringLiteral("UserMC/Media Cache"));
    writeFile(at(userMc, QStringLiteral("Media Cache Files/d.cfa")));
    writeFile(at(userMc, QStringLiteral("notes.txt")));
    writeFile(at(userMc, QStringLiteral("Cuts/cut.txt")));
    const QString foreignMc = at(vol, QStringLiteral("AdobeForeign/Media Cache Files"));
    writeFile(at(foreignMc, QStringLiteral("e.cfa")));
    writeFile(at(foreignMc, QStringLiteral("report.docx")));

    // ---- REAPER, Redshift y Photoshop.
    writeFile(reaperIni, QStringLiteral("[REAPER]\naltpeaks=1\naltpeakspath=%1\n").arg(at(vol, QStringLiteral("Peaks"))).toUtf8());
    writeFile(at(vol, QStringLiteral("Peaks/ab/x.reapeaks")));
    writeFile(at(vol, QStringLiteral("Peaks/cd/y.wav")));
    writeFile(at(vol, QStringLiteral("Peaks/keep.txt")));
    const QString redshift = at(maxon, QStringLiteral("Maxon Cinema 4D 2024_ABC/Redshift"));
    writeFile(at(redshift, QStringLiteral("Cache/Textures/t.rstexbin")));
    writeFile(at(redshift, QStringLiteral("preferences.xml")));
    writeFile(at(redshift, QStringLiteral("redshift.lic")));
    writeFile(at(vol, QStringLiteral("Photoshop Temp123456789")));
    writeFile(at(vol, QStringLiteral("Photoshop Template.psd")));

#ifdef Q_OS_WIN
    // ---- Solo Windows: Topaz (registro), Unreal y las caches de CUDA.
    writeFile(at(vol, QStringLiteral("TopazProjects/P1/project.tvai")));
    writeFile(at(vol, QStringLiteral("TopazProjects/P1/thumbnails/t.jpg")));
    writeFile(at(vol, QStringLiteral("TopazProjects/P1/previews/p.mp4")));
    writeFile(at(vol, QStringLiteral("TopazProjects/P2/thumbnails/t2.jpg")));
    writeFile(at(vol, QStringLiteral("TopazProjects/P3/project.tvai")));
    writeFile(at(vol, QStringLiteral("TopazProjects/P3/thumbnails/t3.jpg")));
    writeFile(at(vol, QStringLiteral("TopazData/models/auth.tpz")));
    writeFile(at(vol, QStringLiteral("TopazData/models/aaa-10.json")));
    writeFile(at(vol, QStringLiteral("TopazData/models/models/astra/blob.0123abcd")));
    writeFile(at(local, QStringLiteral("UnrealEngine/5.3/Saved/Config/WindowsEditor/EditorSettings.ini")),
              QStringLiteral("[/Script/UnrealEd.EditorSettings]\nGlobalLocalDDCPath=(Path=\"%1/DDC\")\n").arg(slashVol).toUtf8());
    writeFile(at(vol, QStringLiteral("DDC/0/1/2/k.udd")));
    writeFile(at(vol, QStringLiteral("DDC/readme.txt")));
    writeFile(at(roaming, QStringLiteral("NVIDIA/ComputeCache/index")), QByteArray("i"));
    writeFile(at(roaming, QStringLiteral("NVIDIA/ComputeCache/a/k")));
    writeFile(at(roaming, QStringLiteral("NVIDIA/NukeComputeCache/index")), QByteArray("i"));
    writeFile(at(roaming, QStringLiteral("NVIDIA/NukeComputeCache/3/k")));
#else
    // ---- Solo mac: los plist de Adobe (los lee la fuente de mentira, por nombre) y una cache de Ableton que
    // "Other app caches" ya no tiene que ofrecer entera.
    for (const char *name : {"com.Adobe.Common 24.0.plist", "com.Adobe.Common 25.0.plist", "com.Adobe.Common 26.0.plist",
                             "com.Adobe.Common 27.0.plist", "com.Adobe.Common 28.0.plist"}) {
        writeFile(at(library, QStringLiteral("Preferences/") + QLatin1String(name)), QByteArray("plist"));
    }
#endif

    // ---- Las fuentes de mentira: nada de esta maquina.
    const QHash<QString, QString> env = {{QStringLiteral("NUKE_TEMP_DIR"), at(vol, QStringLiteral("nuketemp"))},
                                         {QStringLiteral("LGA_TEST_CACHE"), slashVol},
                                         {QStringLiteral("ProgramData"), at(vol, QStringLiteral("PD"))},
                                         {QStringLiteral("ProgramFiles"), at(vol, QStringLiteral("PF"))}};
    const QString adobeKey = QStringLiteral("Software\\Adobe\\Common %1\\Media Cache");
    QHash<QString, QString> registry = {
        {adobeKey.arg(QStringLiteral("24.0")) + QStringLiteral("|FolderPath"), at(vol, QStringLiteral("AdobeMC"))},
        {adobeKey.arg(QStringLiteral("24.0")) + QStringLiteral("|DatabasePath"), at(vol, QStringLiteral("AdobeMC/Media Cache"))},
        {adobeKey.arg(QStringLiteral("25.0")) + QStringLiteral("|FolderPath"), at(vol, QStringLiteral("AdobeMC2/Media Cache Files"))},
        {adobeKey.arg(QStringLiteral("26.0")) + QStringLiteral("|FolderPath"), at(vol, QStringLiteral("AdobeEmpty"))},
        {adobeKey.arg(QStringLiteral("27.0")) + QStringLiteral("|FolderPath"), userMc},
        {adobeKey.arg(QStringLiteral("28.0")) + QStringLiteral("|FolderPath"), foreignMc},
        // Con barras mezcladas, como las escribe Topaz.
        {QStringLiteral("Software\\Topaz Labs LLC\\Topaz Video|defaultProjectDir"), vol + QStringLiteral("/TopazProjects")},
        {QStringLiteral("Software\\Topaz Labs LLC\\Topaz Video|veaiDataFolder"), at(vol, QStringLiteral("TopazData/models")) + sep},
        {QStringLiteral("Software\\Topaz Labs LLC\\Topaz Video AI|defaultProjectDir"), vol}};
    CleanupRules::Sources sources;
    sources.env = [env](const QString &name) { return env.value(name); };
    sources.registry = [registry](const QString &key, const QString &value) { return registry.value(key + QLatin1Char('|') + value); };
    sources.registryKeys = [](const QString &key) {
        return key == QLatin1String("Software\\Adobe")
                   ? QStringList{QStringLiteral("Common 24.0"), QStringLiteral("Common 25.0"), QStringLiteral("Common 26.0"),
                                 QStringLiteral("Common 27.0"), QStringLiteral("Common 28.0"), QStringLiteral("Bridge")}
                   : QStringList();
    };
    const QString adobeMc = at(vol, QStringLiteral("AdobeMC"));
    const QString adobeMc2 = at(vol, QStringLiteral("AdobeMC2/Media Cache Files"));
    const QString adobeEmpty = at(vol, QStringLiteral("AdobeEmpty"));
    sources.plist = [adobeMc, adobeMc2, adobeEmpty, userMc, foreignMc](const QString &path) {
        QVariantMap media;
        if (path.endsWith(QLatin1String("24.0.plist"))) {
            media.insert(QStringLiteral("FolderPath"), adobeMc + QLatin1Char('/'));
        } else if (path.endsWith(QLatin1String("25.0.plist"))) {
            media.insert(QStringLiteral("FolderPath"), adobeMc2);
        } else if (path.endsWith(QLatin1String("26.0.plist"))) {
            media.insert(QStringLiteral("FolderPath"), adobeEmpty);
        } else if (path.endsWith(QLatin1String("27.0.plist"))) {
            media.insert(QStringLiteral("FolderPath"), userMc);
        } else if (path.endsWith(QLatin1String("28.0.plist"))) {
            media.insert(QStringLiteral("FolderPath"), foreignMc);
        }
        return QVariantMap{{QStringLiteral("Media Cache"), media}};
    };

    CleanupRules::Context context;
    context.volumeRoot = DeleteGuard::clean(vol);
    context.bases.profile = profile;
    // Documentos de las bases, dentro del perfil de mentira: Topaz no lee la carpeta real.
    context.bases.documents = at(profile, QStringLiteral("Documents"));
    context.bases.temp = temp;
#ifdef Q_OS_WIN
    context.bases.localAppData = local;
    context.bases.roamingAppData = roaming;
#else
    context.bases.library = library;
    context.bases.caches = caches;
    context.bases.appSupport = support;
#endif
    context.sources = sources;

    // ---- acceptedBase: lo que se rechaza como carpeta de una preferencia (todas existen: se rechazan por
    // lo que son, no por faltar) y una que sirve.
    {
        struct Case
        {
            QString path;
            bool accept;
            const char *what;
        };
#ifdef Q_OS_WIN
        const QString dataChild = at(roaming, QStringLiteral("SomeApp"));
        const QString programChild = at(vol, QStringLiteral("PF/App"));
        QDir().mkpath(programChild);
#else
        const QString dataChild = at(support, QStringLiteral("SomeApp"));
        const QString programChild;
#endif
        QDir().mkpath(dataChild);
        QDir().mkpath(at(profile, QStringLiteral("Desktop")));
        QDir().mkpath(at(profile, QStringLiteral("Documents")));
        QList<Case> cases = {{dataChild, false, "hija directa de la carpeta de datos (AppData\\Roaming, Application Support)"},
                             {at(vol, QStringLiteral("Users")), false, "la carpeta de usuarios"},
                             {profile, false, "la carpeta del usuario"},
                             {at(profile, QStringLiteral("Desktop")), false, "Escritorio"},
                             {at(profile, QStringLiteral("Documents")), false, "Documentos"},
                             {QStringLiteral(".."), false, ".."},
                             {at(profile, QStringLiteral("AppData/Local/..")), false, "AppData por .."},
                             {QStringLiteral("E:foo"), false, "E:foo (relativa a la unidad)"},
                             {QStringLiteral("\\\\server\\share\\cache"), false, "UNC con \\\\"},
                             {QStringLiteral("//server/share/cache"), false, "UNC con //"},
                             {vol, false, "la raiz del volumen"},
                             {at(otherVol, QStringLiteral("NukeCache")), false, "otro volumen"},
                             {nukeCache, true, "una carpeta de cache comun"}};
        if (!programChild.isEmpty()) {
            cases.append({programChild, false, "hija directa de Program Files"});
        }
        QStringList wrong;
        for (const Case &c : cases) {
            const QString result = AppCacheRules::acceptedBase(c.path, context);
            const bool accepted = !result.isEmpty();
            if (accepted != c.accept || (accepted && !DeleteGuard::samePath(result, c.path))) {
                wrong.append(QString::fromLatin1(c.what));
            }
        }
        check(wrong.isEmpty(), QStringLiteral("acceptedBase: %1 casos, fallan: %2").arg(cases.size()).arg(wrong.join(QLatin1String("; "))));
    }

    ScanEngine engine;
    engine.start(vol);
    check(waitScan(engine), QStringLiteral("caches de programas: el volumen de prueba se escanea entero"));
    const QList<Cleanup::Category> categories = CleanupRules::refresh(context, engine, {});
    engine.cancel();
    const Offered o = offered(categories);
    const QStringList nuke = o.by.value(QStringLiteral("nuke"));
    const QStringList nukeWork = o.by.value(QStringLiteral("nukework"));
    const QStringList media = o.by.value(QStringLiteral("media"));
    const QStringList previews = o.by.value(QStringLiteral("previews"));
    const QStringList downloads = o.by.value(QStringLiteral("appdownloads"));

    check(hasPath(nuke, at(nukeCache, QStringLiteral("ViewerCache"))) && hasPath(nuke, at(nukeCache, QStringLiteral("audiocache")))
              && hasPath(nuke, at(nukeCache, QStringLiteral("thumbnailcache"))) && hasPath(nuke, at(nukeCache, QStringLiteral("tilecache")))
              && hasPath(nuke, at(nukeCache, QStringLiteral("jitCache-host/BlockCache"))),
          QStringLiteral("nuke: de DiskCachePath (entre comillas) salen sus cinco caches seguras (%1)").arg(nuke.join(QLatin1String(" | "))));
    check(hasPath(nuke, at(vol, QStringLiteral("FromEnv/ViewerCache"))) && hasPath(nuke, at(vol, QStringLiteral("nuketemp/ViewerCache"))),
          QStringLiteral("nuke: la ruta con \\[getenv] y la de fabrica de una preferencia sin la linea (NUKE_TEMP_DIR)"));
    check(!touches(o.targets, at(vol, QStringLiteral("ViewerCache"))) && !touches(o.targets, at(profile, QStringLiteral("ViewerCache")))
              && !touches(o.targets, at(vol, QStringLiteral("Shots"))),
          QStringLiteral("nuke: una preferencia que apunta a la raiz, a la carpeta del usuario o a una carpeta con un .nk se rechaza"));
    check(!touches(o.targets, at(vol, QStringLiteral("NoMarks"))) && !touches(o.targets, linkCache) && !touches(o.targets, linkViewer)
              && !touches(o.targets, at(vol, QStringLiteral("elsewhere"))) && !touches(o.targets, at(sandbox, QStringLiteral("linktarget"))),
          QStringLiteral("nuke: sin marca no hay item; una carpeta configurada o una cache que es un enlace se rechaza"));
    check(hasPath(nukeWork, at(nukeCache, QStringLiteral("00"))) && hasPath(nukeWork, at(nukeCache, QStringLiteral("timeline_cache.db")))
              && hasPath(nukeWork, at(nukeCache, QStringLiteral("jitCache-host/LocalisedFiles")))
              && !hasPath(nukeWork, at(nukeCache, QStringLiteral("0a"))),
          QStringLiteral("nuke: el timeline con sus 256 carpetas (las que tienen algo y su base) y las copias locales van en Yours"));
    check(o.shown.contains(at(nukeCache, QStringLiteral("timeline_cache.db")) + QStringLiteral(" + 00–ff"), Qt::CaseInsensitive),
          QStringLiteral("nuke: el timeline se muestra como lo que se borra (timeline_cache.db + 00–ff), no como su carpeta"));
    {
        // De las carpetas de trabajo no se ofrece nada: ni sus 00..ff ni su timeline_cache.db.
        QStringList wrong;
        for (const QString &work : {work1, work2, work3, work4}) {
            for (const QString &target : o.targets) {
                if (DeleteGuard::isInside(target, work) || DeleteGuard::samePath(target, work)) {
                    wrong.append(target);
                }
            }
        }
        check(wrong.isEmpty(),
              QStringLiteral("nuke: una carpeta con 01 y 10, o con 00..ff y una subcarpeta, un .exr, un .txt o un .pdf, no ofrece nada (%1)")
                  .arg(wrong.join(QLatin1String(" | "))));
        QStringList five;
        for (const QString &target : o.targets) {
            if (DeleteGuard::isInside(target, work5)) {
                five.append(target);
            }
        }
        check(five.size() == 1 && hasPath(five, at(work5, QStringLiteral("timeline_cache.db"))),
              QStringLiteral("nuke: con cuadros hex sin extension pero sin las 256 carpetas, solo timeline_cache.db (%1)")
                  .arg(five.join(QLatin1String(" | "))));
    }
    check(!touches(o.targets, at(vol, QStringLiteral("NukeLocalize"))) && !hasPath(o.targets, nukeCache)
              && !touches(o.targets, at(nukeCache, QStringLiteral("notes"))) && !hasPath(o.targets, at(nukeCache, QStringLiteral("keep.txt"))),
          QStringLiteral("nuke: nunca la carpeta de la preferencia, ni lo que no es suyo, ni la de localCachePath (sin marca conocida)"));
    check(!touches(o.targets, otherVol), QStringLiteral("volumen: la cache de otro volumen no se ofrece en este"));

    check(hasPath(media, at(vol, QStringLiteral("AECache/Disk Cache - HOST"))) && !hasPath(o.targets, at(vol, QStringLiteral("AECache")))
              && !hasPath(o.targets, at(vol, QStringLiteral("AECache/keep.txt"))),
          QStringLiteral("after effects: de Prefs.txt sale solo su \"Disk Cache - *\""));
    check(hasPath(media, at(abletonConfigured, QStringLiteral("Decoding"))) && hasPath(media, at(abletonDefault, QStringLiteral("Decoding")))
              && hasPath(downloads, packs) && !hasPath(o.targets, abletonConfigured),
          QStringLiteral("ableton: Decoding de Preferences.cfg y el de fabrica (seguros), los packs en Yours"));
    check(hasPath(media, at(vol, QStringLiteral("media/CacheClip"))) && hasPath(previews, at(vol, QStringLiteral("media/OptimizedMedia")))
              && hasPath(previews, at(vol, QStringLiteral("media/ProxyMedia"))) && !touches(o.targets, at(vol, QStringLiteral("media/.gallery")))
              && !touches(o.targets, at(vol, QStringLiteral("other"))),
          QStringLiteral("resolve: CacheClip con .dvcc segura, sus hermanas en Yours; nunca .gallery ni un CacheClip sin .dvcc"));
    check(hasPath(previews, at(vol, QStringLiteral("edit/Adobe Premiere Pro Video Previews")))
              && !touches(o.targets, at(vol, QStringLiteral("edit2"))) && hasPath(previews, at(vol, QStringLiteral("footage/BL_proxy"))),
          QStringLiteral("premiere y blender: previews y BL_proxy en Yours; una carpeta con un .prproj adentro no"));
    check(hasPath(media, at(adobeMc, QStringLiteral("Media Cache Files"))) && hasPath(media, at(adobeMc, QStringLiteral("Media Cache")))
              && hasPath(media, at(adobeMc, QStringLiteral("Peak Files"))) && hasPath(media, adobeMc2) && !touches(o.targets, adobeEmpty)
              && !hasPath(o.targets, adobeMc),
          QStringLiteral("adobe: la Media Cache movida, por la carpeta madre o la propia, solo con .cfa/.pek"));
    check(o.shown.contains(at(adobeMc, QStringLiteral("Media Cache Files + Media Cache + Peak Files")), Qt::CaseInsensitive)
              && o.shown.contains(adobeMc2, Qt::CaseInsensitive) && !o.shown.contains(adobeMc, Qt::CaseInsensitive),
          QStringLiteral("adobe: la Media Cache movida se muestra como lo que se borra, no como su carpeta madre"));
    check(hasPath(media, at(userMc, QStringLiteral("Media Cache Files"))) && !hasPath(o.targets, userMc)
              && !touches(o.targets, at(userMc, QStringLiteral("notes.txt"))) && !touches(o.targets, at(userMc, QStringLiteral("Cuts")))
              && !touches(o.targets, at(vol, QStringLiteral("AdobeForeign"))),
          QStringLiteral("adobe: una carpeta del usuario llamada Media Cache es la madre (solo su Media Cache Files); una cache con un archivo ajeno, nada"));
    check(hasPath(media, at(vol, QStringLiteral("Peaks/ab"))) && !touches(o.targets, at(vol, QStringLiteral("Peaks/cd")))
              && !hasPath(o.targets, at(vol, QStringLiteral("Peaks"))),
          QStringLiteral("reaper: solo las carpetas de picos con .reapeaks"));
    check(hasPath(o.by.value(QStringLiteral("rendercache")), at(redshift, QStringLiteral("Cache")))
              && !hasPath(o.targets, redshift) && !touches(o.targets, at(redshift, QStringLiteral("redshift.lic"))),
          QStringLiteral("redshift: su Cache con Textures; la carpeta de al lado (preferencias y licencias) no"));
    check(hasPath(o.by.value(QStringLiteral("pstemp")), at(vol, QStringLiteral("Photoshop Temp123456789")))
              && !hasPath(o.targets, at(vol, QStringLiteral("Photoshop Template.psd"))),
          QStringLiteral("photoshop: los Photoshop Temp<digitos> de la raiz y nada mas"));
#ifdef Q_OS_WIN
    check(hasPath(media, at(vol, QStringLiteral("TopazProjects/P1/thumbnails"))) && hasPath(previews, at(vol, QStringLiteral("TopazProjects/P1/previews")))
              && !touches(o.targets, at(vol, QStringLiteral("TopazProjects/P2"))) && !hasPath(o.targets, at(vol, QStringLiteral("TopazProjects/P1/project.tvai"))),
          QStringLiteral("topaz: con project.tvai, thumbnails segura y previews en Yours; sin project.tvai, nada"));
    check(hasPath(media, at(vol, QStringLiteral("TopazProjects/P3/thumbnails")))
              && o.shown.contains(at(vol, QStringLiteral("TopazProjects/*/thumbnails")), Qt::CaseInsensitive)
              && o.shown.contains(at(vol, QStringLiteral("TopazProjects/P1/previews")), Qt::CaseInsensitive)
              && !o.shown.contains(at(vol, QStringLiteral("TopazProjects")), Qt::CaseInsensitive),
          QStringLiteral("topaz: se muestra lo que se borra (...\\*\\thumbnails, o la carpeta de previews), nunca la de proyectos sola"));
    check(hasPath(downloads, at(vol, QStringLiteral("TopazData/models/models"))) && !hasPath(o.targets, at(vol, QStringLiteral("TopazData/models")))
              && !touches(o.targets, at(vol, QStringLiteral("TopazData/models/auth.tpz"))),
          QStringLiteral("topaz: los modelos (models\\models con blob.*) en Yours; auth.tpz y sus definiciones no"));
    check(hasPath(downloads, at(vol, QStringLiteral("DDC/0"))) && !hasPath(o.targets, at(vol, QStringLiteral("DDC"))),
          QStringLiteral("unreal: las carpetas 0..9 con .udd de GlobalLocalDDCPath, en Yours"));
    check(hasPath(o.by.value(QStringLiteral("shaders")), at(roaming, QStringLiteral("NVIDIA/ComputeCache")))
              && hasPath(nuke, at(roaming, QStringLiteral("NVIDIA/NukeComputeCache"))),
          QStringLiteral("cuda: la cache comun en las de shaders y la de Nuke con las de Nuke"));
#else
    bool ablePartOfOther = false;
    for (const Cleanup::Category &category : categories) {
        if (category.id == QLatin1String("appcaches")) {
            for (const Cleanup::Item &item : category.items) {
                ablePartOfOther = ablePartOfOther || item.path.endsWith(QStringLiteral("Ableton"));
            }
        }
    }
    check(!ablePartOfOther, QStringLiteral("mac: lo que ya ofrece una cache de programa no se repite en Other app caches"));
#endif
    check(o.yoursUnchecked && o.safeChecked, QStringLiteral("caches de programas: lo seguro arranca tildado y lo del usuario destildado"));

    // ---- Las mismas preferencias, con la ventana sobre el otro volumen: ahi si aparece su cache.
    {
        CleanupRules::Context second = context;
        second.volumeRoot = DeleteGuard::clean(otherVol);
        const Offered there = offered(CleanupRules::build(second));
        check(hasPath(there.by.value(QStringLiteral("nuke")), at(otherVol, QStringLiteral("NukeCache/ViewerCache")))
                  && !touches(there.targets, vol),
              QStringLiteral("volumen: las preferencias de un disco apuntan a otro y la ventana de ese otro las encuentra"));
    }

    // ---- Lo que haria Clean up con lo seguro (sin mirar los programas abiertos: el perfil es de mentira).
    DeleteGuard guard;
    guard.volumeRoot = DeleteGuard::clean(vol);
    QList<CleanupJob::Request> requests;
    for (const Cleanup::Category &category : categories) {
        if (!kOwnIds.contains(category.id) || category.group != Cleanup::Group::Safe) {
            continue;
        }
        for (const Cleanup::Item &item : category.items) {
            for (const Cleanup::Target &target : item.targets) {
                CleanupJob::Request request;
                request.id = int(requests.size());
                request.label = target.path;
                request.path = target.path;
                request.action = target.action;
                requests.append(request);
            }
        }
    }
    CleanupJob job;
    const bool started = job.start(requests, guard);
    for (int i = 0; i < 400 && job.isRunning(); ++i) {
        QThread::msleep(10);
    }
    check(started && !job.isRunning() && job.outcomes().size() == requests.size(),
          QStringLiteral("caches de programas: la limpieza de lo seguro corre entera (%1 pedidos)").arg(requests.size()));
    check(!QFileInfo::exists(at(nukeCache, QStringLiteral("ViewerCache/00/a.nkc"))) && QFileInfo(at(nukeCache, QStringLiteral("ViewerCache"))).isDir()
              && !QFileInfo::exists(at(nukeCache, QStringLiteral("audiocache/") + hex64))
              && !QFileInfo::exists(at(vol, QStringLiteral("AECache/Disk Cache - HOST/x.bin")))
              && !QFileInfo::exists(at(abletonConfigured, QStringLiteral("Decoding/x.wav")))
              && !QFileInfo::exists(at(vol, QStringLiteral("media/CacheClip/abc/x.dvcc")))
              && !QFileInfo::exists(at(vol, QStringLiteral("Photoshop Temp123456789"))),
          QStringLiteral("caches de programas: las caches seguras quedan vacias y sus carpetas siguen"));
    check(QFileInfo::exists(at(nukeCache, QStringLiteral("keep.txt"))) && QFileInfo::exists(at(nukeCache, QStringLiteral("notes/n.txt")))
              && QFileInfo::exists(at(nukeCache, QStringLiteral("timeline_cache.db"))) && QFileInfo::exists(at(nukeCache, QStringLiteral("00/") + frame))
              && QFileInfo::exists(at(nukeCache, QStringLiteral("jitCache-host/LocalisedFiles/plate.exr")))
              && QFileInfo::exists(at(vol, QStringLiteral("NukeLocalize/x.exr"))) && QFileInfo::exists(at(vol, QStringLiteral("ViewerCache/00/root.nkc")))
              && QFileInfo::exists(at(profile, QStringLiteral("ViewerCache/00/home.nkc"))) && QFileInfo::exists(at(vol, QStringLiteral("Shots/shot010.nk")))
              && QFileInfo::exists(at(vol, QStringLiteral("Shots/ViewerCache/00/s.nkc"))) && QFileInfo::exists(at(vol, QStringLiteral("elsewhere/00/e.nkc")))
              && QFileInfo::exists(at(sandbox, QStringLiteral("linktarget/ViewerCache/00/l.nkc")))
              && QFileInfo::exists(at(otherVol, QStringLiteral("NukeCache/ViewerCache/00/o.nkc")))
              && QFileInfo::exists(at(vol, QStringLiteral("AECache/keep.txt"))) && QFileInfo::exists(at(adobeMc, QStringLiteral("keep.txt")))
              && QFileInfo::exists(at(vol, QStringLiteral("media/OptimizedMedia/o.mov"))) && QFileInfo::exists(at(vol, QStringLiteral("media/.gallery/still.dpx")))
              && QFileInfo::exists(at(vol, QStringLiteral("edit2/Adobe Premiere Pro Audio Previews/edit.prproj")))
              && QFileInfo::exists(at(vol, QStringLiteral("Peaks/keep.txt"))) && QFileInfo::exists(at(vol, QStringLiteral("Peaks/cd/y.wav")))
              && QFileInfo::exists(at(redshift, QStringLiteral("preferences.xml"))) && QFileInfo::exists(at(redshift, QStringLiteral("redshift.lic")))
              && QFileInfo::exists(at(vol, QStringLiteral("Photoshop Template.psd"))) && QFileInfo::exists(at(packs, QStringLiteral("pack.alp")))
              && QFileInfo::exists(at(work1, QStringLiteral("01/plates/plate.0001.exr"))) && QFileInfo::exists(at(work1, QStringLiteral("10/render_v001.mov")))
              && QFileInfo::exists(at(work2, QStringLiteral("3a/sub/x.bin"))) && QFileInfo::exists(at(work3, QStringLiteral("7f/shot.exr")))
              && QFileInfo::exists(at(userMc, QStringLiteral("notes.txt"))) && QFileInfo::exists(at(foreignMc, QStringLiteral("report.docx")))
              && QFileInfo::exists(at(foreignMc, QStringLiteral("e.cfa")))
#ifdef Q_OS_WIN
              && QFileInfo::exists(at(vol, QStringLiteral("TopazProjects/P1/project.tvai")))
              && QFileInfo::exists(at(vol, QStringLiteral("TopazProjects/P1/previews/p.mp4")))
              && QFileInfo::exists(at(vol, QStringLiteral("TopazProjects/P2/thumbnails/t2.jpg")))
              && QFileInfo::exists(at(vol, QStringLiteral("TopazData/models/auth.tpz")))
              && QFileInfo::exists(at(vol, QStringLiteral("TopazData/models/models/astra/blob.0123abcd")))
              && QFileInfo::exists(at(vol, QStringLiteral("DDC/0/1/2/k.udd")))
#endif
              && QFileInfo::exists(canary),
          QStringLiteral("caches de programas: despues de limpiar lo seguro SOBREVIVE todo lo demas (proyectos, licencias, lo del usuario, el canario)"));
    // La carpeta de pruebas la borra Qt al salir, despues de que `closer` saco los enlaces.
}

} // namespace CleanupQa
