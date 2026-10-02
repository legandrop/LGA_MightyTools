#include "modules/diskspace/cleanup/CleanupQa.h"

#include "core/AutomatedRun.h"
#include "modules/diskspace/cleanup/CleanupJob.h"
#include "modules/diskspace/cleanup/CleanupRules.h"
#include "modules/diskspace/cleanup/DeleteGuard.h"
#include "modules/diskspace/cleanup/ScanEngine.h"
#include "modules/diskspace/cleanup/ScanSnapshot.h"
#include "platform/FileSystemOps.h"
#include "platform/RecycleBin.h"
#include "platform/SystemPaths.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThread>

#ifdef Q_OS_MACOS
#include <sys/attr.h>
#include <unistd.h>
#endif

// Self-test de la limpieza sobre una carpeta de pruebas PROPIA en %TEMP%.
//
// Es la unica excepcion a "en una corrida automatizada nunca se ejecuta una accion real" de este
// modulo (auditada el 2026-09-30, mismo criterio que el self-test del registro sobre un hive privado):
// borra archivos, pero solo los que el mismo creo, dentro de una carpeta que registra con
// AutomatedRun::setSandbox(). Antes de borrar nada prueba el aislamiento: intenta borrar un archivo
// "canario" FUERA de la carpeta de pruebas y, si el borrado no es rechazado, corta todo.

namespace {

using Check = std::function<void(bool ok, const QString &what)>;

constexpr int kFileBytes = 8192; // dos clusters de 4 KB: ocupa lugar de verdad (no vive dentro de la MFT)

bool writeFile(const QString &path, int bytes = kFileBytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QByteArray(bytes, 'x'));
    return true;
}

// Deja las fechas de creacion y de modificacion `days` dias atras.
bool ageFile(QFile &file, int days)
{
    const QDateTime when = QDateTime::currentDateTime().addDays(-days);
#ifdef Q_OS_MACOS
    // Qt no cambia la fecha de creacion fuera de Windows: en mac va por setattrlist (APFS la acepta).
    attrlist request {};
    request.bitmapcount = ATTR_BIT_MAP_COUNT;
    request.commonattr = ATTR_CMN_CRTIME;
    timespec created {};
    created.tv_sec = time_t(when.toSecsSinceEpoch());
    const bool birth = setattrlist(QFile::encodeName(file.fileName()).constData(), &request, &created, sizeof(created), 0) == 0;
#else
    const bool birth = file.setFileTime(when, QFileDevice::FileBirthTime);
#endif
    return birth && file.setFileTime(when, QFileDevice::FileModificationTime);
}

bool runJob(const QList<CleanupJob::Request> &requests, const DeleteGuard &guard, QList<CleanupJob::Outcome> *outcomes)
{
    CleanupJob job;
    if (!job.start(requests, guard)) {
        return false;
    }
    for (int i = 0; i < 400 && job.isRunning(); ++i) {
        QThread::msleep(10);
    }
    *outcomes = job.outcomes();
    return !job.isRunning();
}

CleanupJob::Request request(int id, const QString &path, Cleanup::Action action)
{
    CleanupJob::Request r;
    r.id = id;
    r.label = path;
    r.path = path;
    r.action = action;
    return r;
}

// Espera a que no quede ningun hilo de la limpieza y anota cuantos quedaron.
void checkNoThreads(const Check &check, const QString &what)
{
    const bool none = CleanupThreads::waitForNone(5000);
    check(none, QStringLiteral("%1 (quedan %2)").arg(what).arg(CleanupThreads::alive()));
}

bool waitScan(ScanEngine &engine)
{
    for (int i = 0; i < 1000 && engine.isRunning(); ++i) {
        QThread::msleep(5);
    }
    return !engine.isRunning() && engine.progress().complete;
}

} // namespace

namespace CleanupQa {

void selfTestSandbox(const Check &check)
{
    // Donde la limpieza no esta habilitada el borrado no hace nada: no hay nada que probar.
    if (!SystemPaths::cleanupSupported()) {
        return;
    }
    QTemporaryDir sandboxDir(QDir(QDir::tempPath()).filePath(QStringLiteral("lga_mt_cleanup_XXXXXX")));
    QTemporaryDir outsideDir(QDir(QDir::tempPath()).filePath(QStringLiteral("lga_mt_canary_XXXXXX")));
    check(sandboxDir.isValid() && outsideDir.isValid(), QStringLiteral("limpieza: se crean la carpeta de pruebas y la del canario"));
    if (!sandboxDir.isValid() || !outsideDir.isValid()) {
        return;
    }
    const QString sandbox = FileSystemOps::canonicalPath(sandboxDir.path());
    const QString outside = FileSystemOps::canonicalPath(outsideDir.path());
    const QChar sep = QDir::separator();
    const QString canary = outside + sep + QStringLiteral("canary.txt");
    writeFile(canary);
    const QString vol = sandbox + sep + QStringLiteral("vol");
    const QString linkTarget = sandbox + sep + QStringLiteral("linktarget");
    // Pase lo que pase, al salir se sacan los enlaces (QTemporaryDir borra con Qt, que entraria en un
    // junction como si fuera una carpeta comun) y se suelta la carpeta de pruebas.
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
    closer.links = {vol + sep + QStringLiteral("linkdir"), vol + sep + QStringLiteral("linkdir3"),
                    vol + sep + QStringLiteral("withlink") + sep + QStringLiteral("j")};

    // ---- Aislamiento, antes que nada. Sin carpeta de pruebas registrada no se borra NADA; con ella,
    // nada fuera de ella. Si alguna de las dos falla, el resto no corre.
    check(AutomatedRun::active(), QStringLiteral("limpieza: el self-test corre como corrida automatizada"));
    const bool refusedUnregistered = FileSystemOps::removeFile(FileSystemOps::nativePath(canary)) == FileSystemOps::Result::Denied;
    AutomatedRun::setSandbox(sandbox);
    const bool refusedOutside = FileSystemOps::removeFile(FileSystemOps::nativePath(canary)) == FileSystemOps::Result::Denied;
    const bool refusedSandboxItself = FileSystemOps::removeDir(FileSystemOps::nativePath(sandbox)) == FileSystemOps::Result::Denied;
    const bool isolated = AutomatedRun::active() && refusedUnregistered && refusedOutside && refusedSandboxItself && QFileInfo::exists(canary)
                          && !AutomatedRun::mayModify(sandbox) && !AutomatedRun::mayModify(sandbox + QStringLiteral("2") + sep + QStringLiteral("x"))
                          && AutomatedRun::mayModify(sandbox + sep + QStringLiteral("x"));
    check(isolated, QStringLiteral("limpieza: AISLAMIENTO - fuera de la carpeta de pruebas el borrado se rechaza y el canario sigue"));
    if (!isolated) {
        return;
    }
    check(!RecycleBin::empty(QDir::rootPath()) && !RecycleBin::moveToTrash(canary, nullptr) && QFileInfo::exists(canary),
          QStringLiteral("limpieza: vaciar la Papelera y mandar a la Papelera son inertes en una corrida automatizada"));

    // ---- El disco de mentira: sandbox\vol es "el volumen"; sandbox\linktarget, a donde apunta el enlace.
    writeFile(linkTarget + sep + QStringLiteral("precious.bin"));
    writeFile(vol + sep + QStringLiteral("keep") + sep + QStringLiteral("data.bin"));
    writeFile(vol + sep + QStringLiteral("cache") + sep + QStringLiteral("a.bin"));
    writeFile(vol + sep + QStringLiteral("cache") + sep + QStringLiteral("sub") + sep + QStringLiteral("b.bin"));
    writeFile(vol + sep + QStringLiteral("withlink") + sep + QStringLiteral("f.bin"));
    const bool linkA = FileSystemOps::createDirLink(vol + sep + QStringLiteral("linkdir"), linkTarget);
    const bool linkB = FileSystemOps::createDirLink(vol + sep + QStringLiteral("withlink") + sep + QStringLiteral("j"), linkTarget);
    check(linkA && linkB && FileSystemOps::kind(vol + sep + QStringLiteral("linkdir")) == FileSystemOps::Kind::Link,
          QStringLiteral("limpieza: los enlaces de carpeta de prueba se crean y se reconocen como enlaces"));
    check(QFileInfo::exists(vol + sep + QStringLiteral("linkdir") + sep + QStringLiteral("precious.bin")),
          QStringLiteral("limpieza: el enlace lleva de verdad a su destino"));

    // ---- Rutas reales: mayusculas y barras distintas dan la misma ruta.
    check(DeleteGuard::samePath(FileSystemOps::canonicalPath(vol.toUpper().replace(QLatin1Char('\\'), QLatin1Char('/'))), vol),
          QStringLiteral("limpieza: la ruta real no depende de mayusculas ni de las barras (%1)")
              .arg(FileSystemOps::canonicalPath(vol.toUpper())));

    // ---- Motor: cuenta lo del volumen y no sigue el enlace.
    DeleteGuard guard;
    guard.volumeRoot = DeleteGuard::clean(vol);
    {
        ScanEngine engine;
        engine.start(vol);
        const bool finished = waitScan(engine);
        const ScanEngine::Progress progress = engine.progress();
        check(finished && progress.files == 4 && progress.dirs == 4 && progress.bytes >= 4ULL * kFileBytes,
              QStringLiteral("motor: 4 archivos y 4 carpetas, sin entrar en los enlaces (archivos=%1 carpetas=%2 bytes=%3)")
                  .arg(progress.files)
                  .arg(progress.dirs)
                  .arg(progress.bytes));
        const auto lock = engine.lock();
        const ScanTree::Index cache = engine.tree().find(vol + sep + QStringLiteral("cache"));
        check(cache != ScanTree::kNone && engine.tree().node(cache).files == 2 && engine.tree().find(vol + sep + QStringLiteral("linkdir")) == ScanTree::kNone,
              QStringLiteral("motor: la carpeta cache tiene sus 2 archivos y el enlace no es un nodo del arbol"));
    }
    checkNoThreads(check, QStringLiteral("motor: al terminar no queda ningun hilo vivo"));
    {
        // Destruir el motor en medio de una pasada no espera a nadie, y los hilos igual se van.
        ScanEngine engine;
        engine.start(vol);
    }
    checkNoThreads(check, QStringLiteral("motor: cancelado al nacer, los hilos terminan solos"));

    // ---- Guardas: casos negativos (ninguno toca nada).
    check(guard.check(vol, DeleteGuard::Scope::Entire, false) == DeleteGuard::Verdict::Protected,
          QStringLiteral("guarda: la raiz del volumen no se borra"));
    check(guard.check(canary, DeleteGuard::Scope::Entire, false) == DeleteGuard::Verdict::OutsideVolume
              && guard.check(vol + QStringLiteral("2") + sep + QStringLiteral("x"), DeleteGuard::Scope::Entire, false) == DeleteGuard::Verdict::OutsideVolume,
          QStringLiteral("guarda: fuera del volumen se rechaza, tambien una carpeta hermana de nombre parecido"));
    check(guard.check(vol + sep + QStringLiteral("cache"), DeleteGuard::Scope::Contents, false) == DeleteGuard::Verdict::Ok,
          QStringLiteral("guarda: una carpeta comun del volumen se puede vaciar"));
    guard.protectedTrees = {vol + sep + QStringLiteral("keep")};
    guard.protectedFolders = {vol + sep + QStringLiteral("cache") + sep + QStringLiteral("sub")};
    check(guard.check((vol + sep + QStringLiteral("KEEP") + sep + QStringLiteral("data.bin")).replace(QLatin1Char('\\'), QLatin1Char('/')),
                      DeleteGuard::Scope::Entire, false)
              == DeleteGuard::Verdict::ProtectedTree,
          QStringLiteral("guarda: dentro de un arbol protegido se rechaza, con otras mayusculas y otras barras"));
    check(guard.check(vol + sep + QStringLiteral("cache"), DeleteGuard::Scope::Contents, false) == DeleteGuard::Verdict::ContainsProtected
              && guard.check(vol + sep + QStringLiteral("cache") + sep + QStringLiteral("sub"), DeleteGuard::Scope::Entire, true) == DeleteGuard::Verdict::Protected
              && guard.check(vol + sep + QStringLiteral("cache") + sep + QStringLiteral("sub"), DeleteGuard::Scope::Children, false) == DeleteGuard::Verdict::Ok,
          QStringLiteral("guarda: lo que contiene una carpeta protegida se rechaza; la protegida, tambien, salvo para sacarle hijos"));
    check(guard.check(vol + sep + QStringLiteral("cache"), DeleteGuard::Scope::Children, false) == DeleteGuard::Verdict::ContainsProtected,
          QStringLiteral("guarda: tampoco se le sacan hijos a una carpeta que contiene algo protegido"));
    guard.cloudFolders = {vol + sep + QStringLiteral("withlink")};
    check(guard.check(vol + sep + QStringLiteral("withlink") + sep + QStringLiteral("f.bin"), DeleteGuard::Scope::Entire, false) == DeleteGuard::Verdict::CloudFolder
              && guard.check(vol + sep + QStringLiteral("withlink") + sep + QStringLiteral("f.bin"), DeleteGuard::Scope::Entire, true) == DeleteGuard::Verdict::Ok,
          QStringLiteral("guarda: dentro de una carpeta de nube, solo a la Papelera"));
#ifdef Q_OS_WIN
    check(guard.check(vol + sep + QStringLiteral("cache") + sep + QStringLiteral("informe."), DeleteGuard::Scope::Entire, true) == DeleteGuard::Verdict::BadNameForTrash,
          QStringLiteral("guarda: un nombre terminado en punto no va a la Papelera"));
#else
    check(guard.check(vol + sep + QStringLiteral("cache") + sep + QStringLiteral("informe."), DeleteGuard::Scope::Entire, true) == DeleteGuard::Verdict::Ok,
          QStringLiteral("guarda: en mac un nombre terminado en punto es un nombre comun"));
#endif
    guard.protectedTrees.clear();
    guard.protectedFolders.clear();
    guard.cloudFolders.clear();
#ifdef Q_OS_WIN
    {
        // Con las raices del sistema de verdad (solo se pregunta, no se borra nada).
        const SystemPaths::CleanupBases bases = SystemPaths::cleanupBases();
        const QString systemVolume = bases.systemRoot.left(3); // "C:\\"
        const DeleteGuard real = DeleteGuard::forVolume(systemVolume);
        const QString system32 = FileSystemOps::canonicalPath(bases.systemRoot.toUpper().replace(QLatin1Char('\\'), QLatin1Char('/'))
                                                              + QStringLiteral("/system32"));
        check(!system32.isEmpty() && real.check(system32, DeleteGuard::Scope::Entire, true) == DeleteGuard::Verdict::ProtectedTree,
              QStringLiteral("guarda: WINDOWS/system32, escrito en mayusculas y con otras barras, cae dentro de Windows (%1)").arg(system32));
        check(real.check(bases.profile, DeleteGuard::Scope::Entire, true) != DeleteGuard::Verdict::Ok
                  && real.check(QFileInfo(bases.profile).absolutePath(), DeleteGuard::Scope::Entire, true) != DeleteGuard::Verdict::Ok
                  && real.check(bases.localAppData, DeleteGuard::Scope::Contents, false) == DeleteGuard::Verdict::Protected
                  && real.check(systemVolume, DeleteGuard::Scope::Contents, false) == DeleteGuard::Verdict::Protected,
              QStringLiteral("guarda: la unidad, el perfil, la carpeta de usuarios y AppData\\Local no se borran ni se vacian"));
        check(real.check(bases.temp, DeleteGuard::Scope::Children, false) == DeleteGuard::Verdict::Ok
                  && real.check(bases.temp, DeleteGuard::Scope::Contents, false) == DeleteGuard::Verdict::Protected,
              QStringLiteral("guarda: de la carpeta temporal se sacan hijos, pero no se vacia entera"));
        check(real.check(systemVolume + QStringLiteral("pagefile.sys"), DeleteGuard::Scope::Entire, false) == DeleteGuard::Verdict::ProtectedTree
                  && real.check(QCoreApplication::applicationDirPath(), DeleteGuard::Scope::Entire, false) != DeleteGuard::Verdict::Ok,
              QStringLiteral("guarda: la memoria virtual y la carpeta de la app estan protegidas"));
    }
#else
    {
        // Con las raices del sistema de verdad (solo se pregunta, no se borra nada).
        const SystemPaths::CleanupBases bases = SystemPaths::cleanupBases();
        const DeleteGuard real = DeleteGuard::forVolume(QStringLiteral("/"));
        const QString finder = FileSystemOps::canonicalPath(QStringLiteral("/SYSTEM/library/CoreServices/Finder.app"));
        check(!finder.isEmpty() && real.check(finder, DeleteGuard::Scope::Entire, true) == DeleteGuard::Verdict::ProtectedTree,
              QStringLiteral("guarda: el Finder, escrito con otras mayusculas, cae dentro de /System (%1)").arg(finder));
        check(real.check(QStringLiteral("/Applications/Safari.app"), DeleteGuard::Scope::Entire, true) == DeleteGuard::Verdict::ProtectedTree
                  && real.check(QStringLiteral("/Library/Preferences"), DeleteGuard::Scope::Contents, false) == DeleteGuard::Verdict::ProtectedTree,
              QStringLiteral("guarda: las apps instaladas y /Library estan protegidas"));
        check(real.check(bases.profile, DeleteGuard::Scope::Entire, true) != DeleteGuard::Verdict::Ok
                  && real.check(QStringLiteral("/Users"), DeleteGuard::Scope::Entire, true) != DeleteGuard::Verdict::Ok
                  && real.check(bases.library, DeleteGuard::Scope::Contents, false) != DeleteGuard::Verdict::Ok
                  && real.check(bases.caches, DeleteGuard::Scope::Contents, false) == DeleteGuard::Verdict::Protected
                  && real.check(QStringLiteral("/"), DeleteGuard::Scope::Contents, false) == DeleteGuard::Verdict::Protected,
              QStringLiteral("guarda: el disco, la carpeta del usuario, /Users, ~/Library y ~/Library/Caches no se borran ni se vacian"));
        check(real.check(bases.temp, DeleteGuard::Scope::Children, false) == DeleteGuard::Verdict::Ok
                  && real.check(bases.temp, DeleteGuard::Scope::Contents, false) == DeleteGuard::Verdict::Protected,
              QStringLiteral("guarda: de la carpeta temporal se sacan hijos, pero no se vacia entera (%1)").arg(bases.temp));
        check(real.check(QDir::homePath() + QStringLiteral("/.Trash"), DeleteGuard::Scope::Contents, false) == DeleteGuard::Verdict::ProtectedTree
                  && real.check(QStringLiteral("/.Spotlight-V100"), DeleteGuard::Scope::Entire, false) == DeleteGuard::Verdict::ProtectedTree
                  && real.check(QCoreApplication::applicationDirPath(), DeleteGuard::Scope::Entire, false) != DeleteGuard::Verdict::Ok,
              QStringLiteral("guarda: la Papelera, el indice de Spotlight y la app estan protegidos"));
        const QString keychains = QDir::homePath() + QStringLiteral("/Library/Keychains");
        check(real.check(keychains, DeleteGuard::Scope::Entire, true) == DeleteGuard::Verdict::ProtectedTree,
              QStringLiteral("guarda: los llaveros del usuario estan protegidos"));
        check(FileSystemOps::kind(QStringLiteral("/System/Volumes/Data")) == FileSystemOps::Kind::Link,
              QStringLiteral("guarda: el volumen de datos montado en /System/Volumes/Data cuenta como enlace"));
    }
#endif

    // ---- El trabajo de borrado.
    const QString temp = vol + sep + QStringLiteral("temp");
    writeFile(temp + sep + QStringLiteral("old.tmp"));
    writeFile(temp + sep + QStringLiteral("locked.tmp"));
    writeFile(temp + sep + QStringLiteral("new.tmp"));
    writeFile(temp + sep + QStringLiteral("newdir") + sep + QStringLiteral("y.tmp"));
    {
        QFile old(temp + sep + QStringLiteral("old.tmp"));
        old.open(QIODevice::ReadWrite);
        ageFile(old, 10);
    }
    {
        QFile aging(temp + sep + QStringLiteral("locked.tmp"));
        aging.open(QIODevice::ReadWrite);
        ageFile(aging, 10);
    }
    // Viejo, pero abierto por este proceso: "en uso". (Se envejece con otro handle y se cierra antes: el
    // listado de la carpeta toma las fechas de un archivo cuando se cierra su handle.)
    QFile locked(temp + sep + QStringLiteral("locked.tmp"));
    locked.open(QIODevice::ReadOnly);

    const CleanupJob::Measure aged = CleanupJob::measureOldChildren(temp, 7, QDateTime::currentSecsSinceEpoch());
    check(aged.files == 2 && aged.keptBytes > 0, QStringLiteral("medicion: 2 temporales de mas de 7 dias, y lo nuevo cuenta aparte (%1 archivos)").arg(aged.files));

    QList<CleanupJob::Outcome> outcomes;
    CleanupJob::Request trash = request(6, vol + sep + QStringLiteral("keep") + sep + QStringLiteral("data.bin"), Cleanup::Action::Entire);
    trash.toTrash = true;
    CleanupJob::Request blocked = request(7, vol + sep + QStringLiteral("keep"), Cleanup::Action::Contents);
    blocked.blockers = {QFileInfo(QCoreApplication::applicationFilePath()).completeBaseName().toLower()};
    blocked.blockerLabel = QStringLiteral("Self");
    const bool ran = runJob({request(0, vol + sep + QStringLiteral("cache"), Cleanup::Action::Contents),
                             request(1, vol + sep + QStringLiteral("linkdir"), Cleanup::Action::Entire),
                             request(2, vol + sep + QStringLiteral("withlink"), Cleanup::Action::Contents),
                             [&]() {
                                 CleanupJob::Request r = request(3, temp, Cleanup::Action::OldChildren);
                                 r.minAgeDays = 7;
                                 return r;
                             }(),
                             request(4, canary, Cleanup::Action::Entire), request(5, vol, Cleanup::Action::Entire), trash, blocked,
                             request(8, QDir::rootPath(), Cleanup::Action::RecycleBin),
                             request(9, vol + sep + QStringLiteral("linkdir2"), Cleanup::Action::Contents)},
                            guard, &outcomes);
    check(ran && outcomes.size() == 10, QStringLiteral("borrado: el trabajo termina y deja un resultado por pedido (%1)").arg(outcomes.size()));
    if (outcomes.size() != 10) {
        return;
    }
    check(outcomes.at(0).deletedFiles == 2 && QFileInfo(vol + sep + QStringLiteral("cache")).isDir()
              && QDir(vol + sep + QStringLiteral("cache")).isEmpty(),
          QStringLiteral("borrado: vaciar una carpeta borra sus 2 archivos y su subcarpeta, y la carpeta queda"));
    check(outcomes.at(1).removedEntirely && !QFileInfo::exists(vol + sep + QStringLiteral("linkdir"))
              && QFileInfo::exists(linkTarget + sep + QStringLiteral("precious.bin")),
          QStringLiteral("borrado: borrar un ENLACE borra el enlace y deja intacto su destino"));
    check(outcomes.at(2).deletedFiles == 1 && !QFileInfo::exists(vol + sep + QStringLiteral("withlink") + sep + QStringLiteral("j"))
              && QFileInfo::exists(linkTarget + sep + QStringLiteral("precious.bin")),
          QStringLiteral("borrado: un enlace dentro de la carpeta que se vacia se borra como enlace; su destino sigue"));
    check(!QFileInfo::exists(temp + sep + QStringLiteral("old.tmp")) && QFileInfo::exists(temp + sep + QStringLiteral("locked.tmp"))
              && QFileInfo::exists(temp + sep + QStringLiteral("new.tmp"))
              && QFileInfo::exists(temp + sep + QStringLiteral("newdir") + sep + QStringLiteral("y.tmp")) && outcomes.at(3).deletedFiles == 1
              && outcomes.at(3).skippedFiles == 1,
          QStringLiteral("borrado: de los temporales se va solo el viejo; el que esta en uso se saltea y lo nuevo queda (borrados=%1 salteados=%2)")
              .arg(outcomes.at(3).deletedFiles)
              .arg(outcomes.at(3).skippedFiles));
    check(outcomes.at(4).refused == DeleteGuard::Verdict::OutsideVolume && QFileInfo::exists(canary),
          QStringLiteral("borrado: un pedido fuera del volumen se rechaza y el canario sigue"));
    check(outcomes.at(5).refused == DeleteGuard::Verdict::Protected && QFileInfo(vol).isDir(),
          QStringLiteral("borrado: la raiz del volumen se rechaza"));
    check(!outcomes.at(6).trashed && !outcomes.at(6).error.isEmpty() && QFileInfo::exists(vol + sep + QStringLiteral("keep") + sep + QStringLiteral("data.bin")),
          QStringLiteral("borrado: si la Papelera no acepta, el archivo NO se borra definitivo"));
    check(outcomes.at(7).blockedBy == QLatin1String("Self") && QFileInfo::exists(vol + sep + QStringLiteral("keep") + sep + QStringLiteral("data.bin")),
          QStringLiteral("borrado: con el programa dueno corriendo, el item no se toca"));
    check(outcomes.at(8).refused == DeleteGuard::Verdict::AutomatedRun,
          QStringLiteral("borrado: vaciar la Papelera se rechaza en una corrida automatizada"));
    check(outcomes.at(9).refused == DeleteGuard::Verdict::Missing, QStringLiteral("borrado: una ruta que no existe no hace nada"));
    locked.close();
    {
        // "Vaciar" una carpeta que en realidad es un enlace se rechaza: seria borrar en otro lado.
        FileSystemOps::createDirLink(vol + sep + QStringLiteral("linkdir3"), linkTarget);
        QList<CleanupJob::Outcome> linkOutcomes;
        runJob({request(0, vol + sep + QStringLiteral("linkdir3"), Cleanup::Action::Contents)}, guard, &linkOutcomes);
        // La ruta real de lo que hay detras del enlace queda fuera del volumen, o el enlace se reconoce: de
        // las dos formas el pedido se rechaza y el destino queda.
        check(linkOutcomes.size() == 1 && linkOutcomes.first().refused != DeleteGuard::Verdict::Ok
                  && QFileInfo::exists(linkTarget + sep + QStringLiteral("precious.bin")),
              QStringLiteral("borrado: vaciar a traves de un enlace se rechaza y el destino queda"));
    }
#ifdef Q_OS_MACOS
    {
        // En mac borrar un archivo abierto no falla: la carpeta con algo abierto adentro se queda entera.
        const QString busy = vol + sep + QStringLiteral("busy");
        writeFile(busy + sep + QStringLiteral("open.bin"));
        writeFile(busy + sep + QStringLiteral("sub") + sep + QStringLiteral("closed.bin"));
        QFile open(busy + sep + QStringLiteral("open.bin"));
        open.open(QIODevice::ReadOnly);
        QThread::msleep(2100); // la foto de archivos abiertos dura 2 s
        QList<CleanupJob::Outcome> busyOutcomes;
        runJob({request(0, busy, Cleanup::Action::Contents)}, guard, &busyOutcomes);
        open.close();
        check(busyOutcomes.size() == 1 && busyOutcomes.first().deletedFiles == 0 && busyOutcomes.first().skippedFiles >= 1
                  && QFileInfo::exists(busy + sep + QStringLiteral("open.bin"))
                  && QFileInfo::exists(busy + sep + QStringLiteral("sub") + sep + QStringLiteral("closed.bin")),
              QStringLiteral("borrado: una carpeta con un archivo abierto adentro no se vacia (ni lo que no esta abierto)"));
        // Una Papelera de volumen que es un enlace no se lee ni se vacia: podria llevar a otro lado.
        const QString trashes = vol + sep + QStringLiteral(".Trashes");
        QDir().mkpath(trashes);
        FileSystemOps::createDirLink(trashes + sep + QString::number(getuid()), linkTarget);
        closer.links.append(trashes + sep + QString::number(getuid()));
        check(!RecycleBin::query(vol).ok && QFileInfo::exists(linkTarget + sep + QStringLiteral("precious.bin")),
              QStringLiteral("papelera: una Papelera que es un enlace no se lee"));
    }
#endif
    checkNoThreads(check, QStringLiteral("borrado: al terminar no queda ningun hilo vivo"));

#ifdef Q_OS_WIN
    // ---- Reglas contra un perfil de mentira: solo las carpetas hijas de la lista blanca.
    const QString profile = vol + sep + QStringLiteral("Users") + sep + QStringLiteral("u");
    const QString local = profile + sep + QStringLiteral("AppData") + sep + QStringLiteral("Local");
    const QString roaming = profile + sep + QStringLiteral("AppData") + sep + QStringLiteral("Roaming");
    const QString chrome = local + sep + QStringLiteral("Google") + sep + QStringLiteral("Chrome") + sep + QStringLiteral("User Data");
    const QString chromeProfile = chrome + sep + QStringLiteral("Default");
    writeFile(chromeProfile + sep + QStringLiteral("Preferences"), 100);
    writeFile(chromeProfile + sep + QStringLiteral("Cookies"));
    writeFile(chromeProfile + sep + QStringLiteral("Local Storage") + sep + QStringLiteral("x.ldb"));
    writeFile(chromeProfile + sep + QStringLiteral("Cache") + sep + QStringLiteral("Cache_Data") + sep + QStringLiteral("f_0001"));
    writeFile(chromeProfile + sep + QStringLiteral("Code Cache") + sep + QStringLiteral("js") + sep + QStringLiteral("c_0001"));
    const QString app = roaming + sep + QStringLiteral("SomeApp");
    writeFile(app + sep + QStringLiteral("Preferences"), 100);
    writeFile(app + sep + QStringLiteral("Local Storage") + sep + QStringLiteral("keep.ldb"));
    writeFile(app + sep + QStringLiteral("Code Cache") + sep + QStringLiteral("c1"));
    writeFile(app + sep + QStringLiteral("Cache") + sep + QStringLiteral("Cache_Data") + sep + QStringLiteral("d1"));
    writeFile(app + sep + QStringLiteral("vm_bundles") + sep + QStringLiteral("image.vhdx"));
    // Una app con una carpeta "Cache" que NO es de Chromium: no se toca.
    writeFile(roaming + sep + QStringLiteral("OtherApp") + sep + QStringLiteral("Cache") + sep + QStringLiteral("mine.db"));
    writeFile(local + sep + QStringLiteral("pip") + sep + QStringLiteral("cache") + sep + QStringLiteral("http") + sep + QStringLiteral("p1"));
    writeFile(local + sep + QStringLiteral("Temp") + sep + QStringLiteral("fresh.tmp"));
#else
    // ---- Reglas contra un perfil de mentira con la forma de mac: el perfil del navegador en Application
    // Support y su cache en Caches; ~/Library/Caches con cosas que NO son cache (bases de PipeSync, CloudKit).
    const QString profile = vol + sep + QStringLiteral("Users") + sep + QStringLiteral("u");
    const QString library = profile + sep + QStringLiteral("Library");
    const QString support = library + sep + QStringLiteral("Application Support");
    const QString caches = library + sep + QStringLiteral("Caches");
    const QString local = caches; // donde vive la cache de pip
    const QString roaming = support; // donde viven las apps de escritorio
    const QString chromeProfile = support + sep + QStringLiteral("Google") + sep + QStringLiteral("Chrome") + sep + QStringLiteral("Default");
    const QString chromeCache = caches + sep + QStringLiteral("Google") + sep + QStringLiteral("Chrome") + sep + QStringLiteral("Default");
    writeFile(chromeProfile + sep + QStringLiteral("Preferences"), 100);
    writeFile(chromeProfile + sep + QStringLiteral("Cookies"));
    writeFile(chromeProfile + sep + QStringLiteral("Local Storage") + sep + QStringLiteral("x.ldb"));
    writeFile(chromeProfile + sep + QStringLiteral("GPUCache") + sep + QStringLiteral("g_0001"));
    writeFile(chromeCache + sep + QStringLiteral("Cache") + sep + QStringLiteral("Cache_Data") + sep + QStringLiteral("f_0001"));
    writeFile(chromeCache + sep + QStringLiteral("Code Cache") + sep + QStringLiteral("js") + sep + QStringLiteral("c_0001"));
    const QString app = support + sep + QStringLiteral("SomeApp");
    writeFile(app + sep + QStringLiteral("Preferences"), 100);
    writeFile(app + sep + QStringLiteral("Local Storage") + sep + QStringLiteral("keep.ldb"));
    writeFile(app + sep + QStringLiteral("Code Cache") + sep + QStringLiteral("c1"));
    writeFile(app + sep + QStringLiteral("Cache") + sep + QStringLiteral("Cache_Data") + sep + QStringLiteral("d1"));
    writeFile(app + sep + QStringLiteral("vm_bundles") + sep + QStringLiteral("image.vhdx"));
    // Una app con una carpeta "Cache" que NO es de Chromium: no se toca.
    writeFile(support + sep + QStringLiteral("OtherApp") + sep + QStringLiteral("Cache") + sep + QStringLiteral("mine.db"));
    writeFile(caches + sep + QStringLiteral("pip") + sep + QStringLiteral("cache") + sep + QStringLiteral("http") + sep + QStringLiteral("p1"));
    writeFile(caches + sep + QStringLiteral("com.example.App.ShipIt") + sep + QStringLiteral("update.zip"));
    writeFile(caches + sep + QStringLiteral("SomeTool") + sep + QStringLiteral("data.db"));
    writeFile(caches + sep + QStringLiteral("LGA") + sep + QStringLiteral("PipeSync") + sep + QStringLiteral("pipesync.db"));
    writeFile(caches + sep + QStringLiteral("CloudKit") + sep + QStringLiteral("cloudd_db"));
    writeFile(caches + sep + QStringLiteral("com.apple.Something") + sep + QStringLiteral("x.db"));
    writeFile(profile + sep + QStringLiteral("tmp") + sep + QStringLiteral("fresh.tmp"));
#endif
    // Carpetas marcadas como cache: una comun, y un entorno virtual que tambien lleva la marca.
    const QString tagged = vol + sep + QStringLiteral("proj") + sep + QStringLiteral("target");
    const QString venv = vol + sep + QStringLiteral("proj") + sep + QStringLiteral(".venv");
    for (const QString &dir : {tagged, venv}) {
        QDir().mkpath(dir);
        QFile tag(dir + sep + QStringLiteral("CACHEDIR.TAG"));
        tag.open(QIODevice::WriteOnly);
        tag.write("Signature: 8a477f597d28d172789f06886806bc55\n");
        tag.close();
        QFile big(dir + sep + QStringLiteral("big.bin"));
        big.open(QIODevice::WriteOnly);
        big.write(QByteArray(12 * 1024 * 1024, 'y'));
    }
    writeFile(venv + sep + QStringLiteral("pyvenv.cfg"), 50);
    // Una cache de uv fuera de su lugar de fabrica: se reconoce por su forma y arranca SIN tildar.
    const QString uvCache = vol + sep + QStringLiteral("tool") + sep + QStringLiteral("uv-cache");
    {
        QFile tag(uvCache + sep + QStringLiteral("CACHEDIR.TAG"));
        QDir().mkpath(uvCache + sep + QStringLiteral("archive-v0"));
        QDir().mkpath(uvCache + sep + QStringLiteral("wheels-v5"));
        tag.open(QIODevice::WriteOnly);
        tag.write("Signature: 8a477f597d28d172789f06886806bc55\n");
        tag.close();
        QFile big(uvCache + sep + QStringLiteral("wheels-v5") + sep + QStringLiteral("w.whl"));
        big.open(QIODevice::WriteOnly);
        big.write(QByteArray(12 * 1024 * 1024, 'z'));
    }
    writeFile(vol + sep + QStringLiteral("work") + sep + QStringLiteral("p1") + sep + QStringLiteral("build") + sep + QStringLiteral("o.obj"));
    writeFile(vol + sep + QStringLiteral("work") + sep + QStringLiteral("p2") + sep + QStringLiteral("build") + sep + QStringLiteral("o.obj"));

    CleanupRules::Context context;
    context.volumeRoot = guard.volumeRoot;
    context.bases.profile = profile;
#ifdef Q_OS_WIN
    context.bases.localAppData = local;
    context.bases.roamingAppData = roaming;
    context.bases.temp = local + sep + QStringLiteral("Temp");
#else
    context.bases.library = library;
    context.bases.caches = caches;
    context.bases.appSupport = support;
    context.bases.temp = profile + sep + QStringLiteral("tmp");
#endif
    context.folderRules = {Cleanup::FolderRule{QString(), vol + sep + QStringLiteral("work"), QStringLiteral("build")}};
    ScanEngine engine;
    engine.start(vol);
    check(waitScan(engine), QStringLiteral("reglas: el volumen de prueba se escanea entero"));
    const QList<Cleanup::Category> categories = CleanupRules::refresh(context, engine, {});

    QStringList targets;
    QStringList ids;
    QList<CleanupJob::Request> cleanup;
    bool yoursUnchecked = true;
    bool safeChecked = true;
    bool uvOptionalUnchecked = false;
    for (const Cleanup::Category &category : categories) {
        ids.append(category.id);
        for (const Cleanup::Item &item : category.items) {
            if (category.group == Cleanup::Group::Yours && item.checked) {
                yoursUnchecked = false;
            }
            if (item.optional) {
                uvOptionalUnchecked = category.id == QLatin1String("python") && !item.checked && item.path.endsWith(QStringLiteral("uv-cache"));
                continue; // no entra en "lo seguro que se limpia solo"
            }
            if (category.group == Cleanup::Group::Safe && !item.checked && !item.blocked) {
                safeChecked = false;
            }
            for (const Cleanup::Target &target : item.targets) {
                targets.append(target.path);
                // Lo que haria "Clean up" con lo seguro. No se miran los programas abiertos: el perfil es
                // de mentira y el navegador de verdad de esta maquina no tiene nada que ver con el.
                if (category.group == Cleanup::Group::Safe && target.action != Cleanup::Action::RecycleBin) {
                    CleanupJob::Request r = request(int(cleanup.size()), target.path, target.action);
                    r.minAgeDays = target.minAgeDays;
                    cleanup.append(r);
                }
            }
        }
    }
    const auto touches = [&targets](const QString &needle) {
        for (const QString &target : targets) {
            if (target.contains(needle, Qt::CaseInsensitive)) {
                return true;
            }
        }
        return false;
    };
#ifdef Q_OS_WIN
    check(ids.contains(QStringLiteral("browsers")) && ids.contains(QStringLiteral("apps")) && ids.contains(QStringLiteral("python"))
              && ids.contains(QStringLiteral("vm")) && ids.contains(QStringLiteral("rule:0")) && ids.contains(QStringLiteral("tagged")),
          QStringLiteral("reglas: navegadores, apps, Python, maquinas virtuales, la regla del usuario y lo marcado (%1)").arg(ids.join(QLatin1Char(' '))));
    check(touches(QStringLiteral("Cache_Data")) == false && touches(QStringLiteral("Default\\Cache")) && touches(QStringLiteral("Default\\Code Cache"))
              && touches(QStringLiteral("SomeApp\\Code Cache")) && touches(QStringLiteral("pip\\cache")),
          QStringLiteral("reglas: los destinos son las carpetas de cache conocidas"));
    check(!touches(QStringLiteral("Cookies")) && !touches(QStringLiteral("Local Storage")) && !touches(QStringLiteral("Preferences"))
              && !touches(QStringLiteral("OtherApp")) && !touches(QStringLiteral(".venv")) && !targets.contains(app) && !targets.contains(chromeProfile),
          QStringLiteral("reglas: nunca cookies, Local Storage, la carpeta raiz de una app, una Cache ajena ni un entorno virtual"));
#else
    check(ids.contains(QStringLiteral("browsers")) && ids.contains(QStringLiteral("apps")) && ids.contains(QStringLiteral("python"))
              && ids.contains(QStringLiteral("vm")) && ids.contains(QStringLiteral("rule:0")) && ids.contains(QStringLiteral("tagged"))
              && ids.contains(QStringLiteral("updates")) && ids.contains(QStringLiteral("appcaches")),
          QStringLiteral("reglas: navegadores, apps, Python, actualizaciones, otras caches, maquinas virtuales, la regla del usuario y lo marcado (%1)")
              .arg(ids.join(QLatin1Char(' '))));
    check(touches(QStringLiteral("Cache_Data")) == false && touches(QStringLiteral("Caches/Google/Chrome/Default/Cache"))
              && touches(QStringLiteral("Caches/Google/Chrome/Default/Code Cache")) && touches(QStringLiteral("Chrome/Default/GPUCache"))
              && touches(QStringLiteral("SomeApp/Code Cache")) && touches(QStringLiteral("Caches/pip"))
              && touches(QStringLiteral("com.example.App.ShipIt")),
          QStringLiteral("reglas: los destinos son las carpetas de cache conocidas, de los dos lados del navegador"));
    check(!touches(QStringLiteral("Cookies")) && !touches(QStringLiteral("Local Storage")) && !touches(QStringLiteral("Preferences"))
              && !touches(QStringLiteral("OtherApp")) && !touches(QStringLiteral(".venv")) && !targets.contains(app) && !targets.contains(chromeProfile)
              && !touches(QStringLiteral("Caches/LGA")) && !touches(QStringLiteral("CloudKit")) && !touches(QStringLiteral("com.apple.")),
          QStringLiteral("reglas: nunca cookies, Local Storage, la carpeta raiz de una app, una Cache ajena, un entorno virtual, LGA ni lo de Apple"));
    {
        bool otherOffered = false;
        bool otherUnchecked = true;
        for (const Cleanup::Category &category : categories) {
            if (category.id != QLatin1String("appcaches")) {
                continue;
            }
            for (const Cleanup::Item &item : category.items) {
                otherOffered = otherOffered || item.path.endsWith(QStringLiteral("SomeTool"));
                otherUnchecked = otherUnchecked && !item.checked;
                // Lo que ya cubre otra regla no se ofrece de nuevo.
                otherUnchecked = otherUnchecked && !item.path.endsWith(QStringLiteral("pip")) && !item.path.endsWith(QStringLiteral("Google"));
            }
        }
        check(otherOffered && otherUnchecked,
              QStringLiteral("reglas: las otras caches de ~/Library/Caches van en Yours, destildadas, sin repetir las de otras reglas"));
    }
#endif
    check(yoursUnchecked && safeChecked, QStringLiteral("reglas: lo seguro arranca tildado y lo del usuario, destildado"));
    check(uvOptionalUnchecked, QStringLiteral("reglas: una cache de uv fuera de su lugar de fabrica va con las de Python, sin tildar"));
    for (const Cleanup::Category &category : categories) {
        if (category.id == QLatin1String("rule:0")) {
            check(category.items.size() == 2 && category.items.first().targets.first().action == Cleanup::Action::Entire,
                  QStringLiteral("reglas: la regla del usuario encuentra las 2 carpetas build (%1)").arg(category.items.size()));
        }
        if (category.id == QLatin1String("tagged")) {
            check(category.items.size() == 1 && category.items.first().path.endsWith(QStringLiteral("target")),
                  QStringLiteral("reglas: de lo marcado como cache queda la carpeta comun, no el entorno virtual"));
        }
    }
    QList<CleanupJob::Outcome> cleaned;
    check(runJob(cleanup, guard, &cleaned) && !cleaned.isEmpty(), QStringLiteral("reglas: la limpieza de lo seguro corre entera (%1 pedidos)").arg(cleanup.size()));
    check(QFileInfo::exists(chromeProfile + sep + QStringLiteral("Cookies")) && QFileInfo::exists(chromeProfile + sep + QStringLiteral("Preferences"))
              && QFileInfo::exists(chromeProfile + sep + QStringLiteral("Local Storage") + sep + QStringLiteral("x.ldb"))
              && QFileInfo::exists(app + sep + QStringLiteral("Local Storage") + sep + QStringLiteral("keep.ldb"))
              && QFileInfo::exists(app + sep + QStringLiteral("vm_bundles") + sep + QStringLiteral("image.vhdx"))
              && QFileInfo::exists(roaming + sep + QStringLiteral("OtherApp") + sep + QStringLiteral("Cache") + sep + QStringLiteral("mine.db"))
#ifdef Q_OS_WIN
              && QFileInfo::exists(local + sep + QStringLiteral("Temp") + sep + QStringLiteral("fresh.tmp"))
#else
              && QFileInfo::exists(profile + sep + QStringLiteral("tmp") + sep + QStringLiteral("fresh.tmp"))
              && QFileInfo::exists(caches + sep + QStringLiteral("SomeTool") + sep + QStringLiteral("data.db"))
              && QFileInfo::exists(caches + sep + QStringLiteral("LGA") + sep + QStringLiteral("PipeSync") + sep + QStringLiteral("pipesync.db"))
              && QFileInfo::exists(caches + sep + QStringLiteral("CloudKit") + sep + QStringLiteral("cloudd_db"))
              && QFileInfo::exists(chromeProfile + sep + QStringLiteral("Cookies"))
#endif
              && QFileInfo::exists(venv + sep + QStringLiteral("big.bin")) && QFileInfo::exists(tagged + sep + QStringLiteral("big.bin"))
              && QFileInfo::exists(uvCache + sep + QStringLiteral("wheels-v5") + sep + QStringLiteral("w.whl")),
          QStringLiteral("reglas: despues de limpiar lo seguro SOBREVIVE todo lo que no es cache (cookies, ajustes, lo del usuario, lo nuevo)"));
#ifdef Q_OS_WIN
    const QString chromeCache = chromeProfile;
#endif
    check(!QFileInfo::exists(chromeCache + sep + QStringLiteral("Cache") + sep + QStringLiteral("Cache_Data") + sep + QStringLiteral("f_0001"))
              && !QFileInfo::exists(app + sep + QStringLiteral("Code Cache") + sep + QStringLiteral("c1"))
              && !QFileInfo::exists(local + sep + QStringLiteral("pip") + sep + QStringLiteral("cache") + sep + QStringLiteral("http") + sep + QStringLiteral("p1"))
              && QFileInfo(chromeCache + sep + QStringLiteral("Cache")).isDir(),
          QStringLiteral("reglas: las caches quedan vacias y sus carpetas siguen estando"));
    engine.cancel();

    // ---- Resumenes para "What changed".
    const qint64 mb = 1024 * 1024;
    ScanSnapshot before;
    before.root = vol;
    before.takenAt = QDateTime::currentDateTime().addDays(-1);
    // Rutas con la forma de esta plataforma: el resumen mira "adentro de" con su separador.
    const auto at = [](const char *path) { return QDir::toNativeSeparators(QDir::rootPath() + QLatin1String(path)); };
    before.dirs = {{at("A"), 1000 * mb}, {at("A/B"), 900 * mb}, {at("C"), 500 * mb}};
    ScanSnapshot after = before;
    after.takenAt = QDateTime::currentDateTime();
    after.dirs = {{at("A"), 1500 * mb}, {at("A/B"), 1400 * mb}, {at("C"), 200 * mb}, {at("D"), 150 * mb}};
    const QList<ScanSnapshot::Change> changes = ScanSnapshot::diff(before, after, 100 * mb);
    check(changes.size() == 3 && changes.at(0).path == at("A/B") && changes.at(0).delta == 500 * mb
              && changes.at(1).path == at("D") && changes.at(2).path == at("C") && changes.at(2).delta == -300 * mb,
          QStringLiteral("resumen: lo que cambio, con la carpeta mas profunda que lo explica (%1 cambios)").arg(changes.size()));
    const QString store = sandbox + sep + QStringLiteral("scans");
    check(!ScanSnapshot::store(before) && ScanSnapshot::store(before, true, store) && ScanSnapshot::store(after, true, store),
          QStringLiteral("resumen: en una corrida automatizada no se escribe en los ajustes del usuario; en la carpeta de prueba, si"));
    const ScanSnapshot current = ScanSnapshot::loadCurrent(vol, store);
    const ScanSnapshot previous = ScanSnapshot::loadPrevious(vol, store);
    check(current.dirs.value(at("D")) == 150 * mb && previous.dirs.size() == 3 && !previous.dirs.contains(at("D")),
          QStringLiteral("resumen: el nuevo queda como actual y el que habia pasa a anterior"));

    check(QFileInfo::exists(canary) && QFileInfo::exists(linkTarget + sep + QStringLiteral("precious.bin")),
          QStringLiteral("limpieza: al final el canario y el destino de los enlaces siguen intactos"));
    checkNoThreads(check, QStringLiteral("limpieza: al final no queda ningun hilo vivo"));
    // La carpeta de pruebas la borra Qt (QTemporaryDir) al salir, despues de que `closer` saco los enlaces.
}

} // namespace CleanupQa
