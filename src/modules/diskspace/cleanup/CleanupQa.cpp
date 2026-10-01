#include "modules/diskspace/cleanup/CleanupQa.h"

#include "modules/diskspace/DiskSpace.h"
#include "modules/diskspace/cleanup/CleanupRules.h"
#include "modules/diskspace/cleanup/DeleteGuard.h"
#include "modules/diskspace/cleanup/ScanEngine.h"
#include "modules/diskspace/cleanup/ScanTree.h"
#include "platform/SystemPaths.h"

#include <QDateTime>
#include <QDir>
#include <QThread>

#include <cstdio>

namespace {

int simulateScan(const QStringList &args)
{
    const QString root = args.value(0);
    if (root.isEmpty() || !QDir(root).exists()) {
        std::fprintf(stderr, "usage: --simulate-action diskSpace:scan <root>\n");
        return 2;
    }
    ScanEngine engine;
    engine.start(root);
    while (engine.isRunning()) {
        QThread::msleep(50);
    }
    const ScanEngine::Progress progress = engine.progress();
    std::printf("scan root=%s complete=%d seconds=%.2f files=%llu dirs=%u bytes=%llu (%s) denied=%u\n",
                qPrintable(QDir::toNativeSeparators(root)), progress.complete, progress.seconds,
                static_cast<unsigned long long>(progress.files), progress.dirs,
                static_cast<unsigned long long>(progress.bytes), qPrintable(DiskSpace::formatSize(qint64(progress.bytes))),
                progress.denied);
    {
        const auto lock = engine.lock();
        const ScanTree &tree = engine.tree();
        const QList<ScanTree::Index> top = tree.children(tree.root());
        for (int i = 0; i < top.size() && i < 12; ++i) {
            const ScanTree::Node &node = tree.node(top.at(i));
            std::printf("  dir  %10s  %8u files  %s\n", qPrintable(DiskSpace::formatSize(qint64(node.bytes))), node.files,
                        qPrintable(tree.path(top.at(i))));
        }
    }
    const QList<ScanEngine::TopFile> files = engine.topFiles();
    for (int i = 0; i < files.size() && i < 8; ++i) {
        const auto lock = engine.lock();
        std::printf("  file %10s  %s\\%s\n", qPrintable(DiskSpace::formatSize(qint64(files.at(i).bytes))),
                    qPrintable(engine.tree().path(files.at(i).dir)), qPrintable(files.at(i).name));
    }
    std::printf("  cache-tagged dirs: %d, top files kept: %d\n", int(engine.cacheTaggedDirs().size()), int(files.size()));
    return progress.complete ? 0 : 1;
}

const char *groupName(Cleanup::Group group)
{
    switch (group) {
    case Cleanup::Group::Safe:
        return "safe";
    case Cleanup::Group::Admin:
        return "admin";
    case Cleanup::Group::Yours:
        break;
    }
    return "yours";
}

// Solo lectura: escanea, arma las reglas contra el disco real e imprime que se limpiaria. No borra.
int simulatePlan(const QStringList &args)
{
    const QString root = args.value(0);
    if (root.isEmpty() || !QDir(root).exists()) {
        std::fprintf(stderr, "usage: --simulate-action diskSpace:cleanup-plan <root> [folder-rule-base folder-rule-match]\n");
        return 2;
    }
    ScanEngine engine;
    engine.start(root);
    while (engine.isRunning()) {
        QThread::msleep(50);
    }
    const DeleteGuard guard = DeleteGuard::forVolume(root);
    CleanupRules::Context context;
    context.bases = SystemPaths::cleanupBases();
    context.volumeRoot = guard.volumeRoot;
    if (args.size() >= 2) {
        context.folderRules.append(Cleanup::FolderRule{QString(), args.at(1), args.value(2)});
    }
    std::printf("plan volume=%s profile=%s temp=%s\n", qPrintable(context.volumeRoot), qPrintable(context.bases.profile),
                qPrintable(context.bases.temp));
    std::printf("guard protected=%d trees=%d cloud=%d\n", int(guard.protectedFolders.size()), int(guard.protectedTrees.size()),
                int(guard.cloudFolders.size()));
    const QList<Cleanup::Category> categories = CleanupRules::refresh(context, engine, {});
    const QDateTime now = QDateTime::currentDateTime();
    for (const Cleanup::Category &category : categories) {
        qint64 total = 0;
        for (const Cleanup::Item &item : category.items) {
            total += item.bytes;
        }
        std::printf("[%s] %s (%s)%s%s  %s\n", groupName(category.group), qPrintable(category.id), qPrintable(category.title),
                    category.info ? " info" : "", category.single ? " single" : "", qPrintable(DiskSpace::formatSize(total)));
        int shown = 0;
        for (const Cleanup::Item &item : category.items) {
            if (++shown > 14) {
                std::printf("      ... %d more\n", int(category.items.size()) - 14);
                break;
            }
            std::printf("   %s %10s %7lld files %6s  %s  -> %s%s\n", item.checked ? "[x]" : (item.blocked ? "[#]" : "[ ]"),
                        qPrintable(DiskSpace::formatSize(item.bytes)), static_cast<long long>(item.files),
                        qPrintable(DiskSpace::ageText(item.newest, now)), qPrintable(item.name), qPrintable(item.path),
                        item.keptBytes > 0 ? qPrintable(QStringLiteral(" (kept %1)").arg(DiskSpace::formatSize(item.keptBytes))) : "");
            for (const Cleanup::Target &target : item.targets) {
                // Cada destino pasa por la guarda del borrado: el plan muestra lo que el trabajo aceptaria.
                DeleteGuard::Scope scope = DeleteGuard::Scope::Entire;
                if (target.action == Cleanup::Action::Contents) {
                    scope = DeleteGuard::Scope::Contents;
                } else if (target.action == Cleanup::Action::OldChildren) {
                    scope = DeleteGuard::Scope::Children;
                }
                const bool bin = target.action == Cleanup::Action::RecycleBin;
                const DeleteGuard::Verdict verdict = bin ? DeleteGuard::Verdict::Ok : guard.check(target.path, scope, false);
                if (item.targets.size() > 1 || verdict != DeleteGuard::Verdict::Ok) {
                    std::printf("        %s %s\n", verdict == DeleteGuard::Verdict::Ok ? "ok    " : "REFUSE", qPrintable(target.path));
                }
            }
        }
    }
    std::printf("selected=%s\n", qPrintable(DiskSpace::formatSize(CleanupRules::selectedBytes(categories))));
    return 0;
}

} // namespace

namespace CleanupQa {

void selfTest(const std::function<void(bool ok, const QString &what)> &check)
{
    // ---- Aritmetica del arbol: totales hacia arriba, completar, borrar y volver a leer.
    ScanTree tree;
    tree.reset(QStringLiteral("C:/"));
    const ScanTree::Index users = tree.addChild(tree.root(), u"Users");
    const ScanTree::Index temp = tree.addChild(tree.root(), u"temp");
    tree.setListed(tree.root(), 100, 2, 50, false, false);
    check(!tree.isComplete(tree.root()) && tree.node(tree.root()).bytes == 100,
          QStringLiteral("arbol: la raiz leida con dos subcarpetas pendientes no esta completa"));
    const ScanTree::Index lega = tree.addChild(users, u"lega");
    tree.setListed(users, 10, 1, 60, false, false);
    tree.setListed(lega, 1000, 5, 70, false, true);
    check(tree.isComplete(users) && tree.node(users).bytes == 1010 && tree.node(tree.root()).bytes == 1110
              && tree.node(tree.root()).files == 8 && tree.node(tree.root()).newest == 70,
          QStringLiteral("arbol: los totales y la fecha suben hasta la raiz"));
    check(!tree.isComplete(tree.root()), QStringLiteral("arbol: falta temp, la raiz sigue sin completar"));
    tree.setListed(temp, 400, 3, 10, false, false);
    check(tree.isComplete(tree.root()) && tree.node(tree.root()).bytes == 1510 && tree.node(tree.root()).dirs == 3,
          QStringLiteral("arbol: con la ultima carpeta leida la raiz queda completa (3 subcarpetas)"));
    check(tree.children(tree.root()) == QList<ScanTree::Index>({users, temp}),
          QStringLiteral("arbol: los hijos salen de mayor a menor"));
    check(tree.path(lega) == QDir::toNativeSeparators(QStringLiteral("C:/Users/lega"))
              && tree.find(QStringLiteral("C:/Users/lega")) == lega && tree.find(QStringLiteral("C:/Users2")) == ScanTree::kNone,
          QStringLiteral("arbol: ruta de un nodo y busqueda por ruta (%1)").arg(tree.path(lega)));
#ifdef Q_OS_WIN
    check(tree.find(QStringLiteral("c:\\users\\LEGA")) == lega, QStringLiteral("arbol: la busqueda no distingue mayusculas en Windows"));
#endif
    tree.removeFile(lega, 300);
    check(tree.node(lega).bytes == 700 && tree.node(lega).files == 4 && tree.node(tree.root()).bytes == 1210,
          QStringLiteral("arbol: borrar un archivo resta en su carpeta y hacia arriba"));
    tree.resetSubtree(users);
    check(!tree.isComplete(tree.root()) && tree.node(tree.root()).bytes == 500 && tree.node(tree.root()).dirs == 2
              && !tree.isAlive(lega),
          QStringLiteral("arbol: volver a leer una carpeta la vacia, resta su peso y descompleta a la raiz"));
    tree.setListed(users, 10, 1, 60, false, false);
    check(tree.isComplete(tree.root()) && tree.node(tree.root()).bytes == 510,
          QStringLiteral("arbol: releida sin subcarpetas, la raiz vuelve a estar completa"));
    tree.removeSubtree(temp);
    check(tree.node(tree.root()).bytes == 110 && tree.node(tree.root()).dirs == 1 && tree.node(tree.root()).files == 3
              && tree.children(tree.root()) == QList<ScanTree::Index>({users}) && !tree.isAlive(temp),
          QStringLiteral("arbol: borrar una carpeta la saca y resta su peso, sus archivos y la carpeta misma"));
    tree.removeSubtree(tree.root());
    check(tree.node(tree.root()).bytes == 110, QStringLiteral("arbol: la raiz no se borra"));

    selfTestSandbox(check);
}

int simulate(const QString &action, const QStringList &args)
{
    if (action == QLatin1String("scan")) {
        return simulateScan(args);
    }
    if (action == QLatin1String("cleanup-plan")) {
        return simulatePlan(args);
    }
    return 2;
}

} // namespace CleanupQa
