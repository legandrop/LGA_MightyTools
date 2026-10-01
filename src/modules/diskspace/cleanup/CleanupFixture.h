#ifndef MIGHTYTOOLS_CLEANUPFIXTURE_H
#define MIGHTYTOOLS_CLEANUPFIXTURE_H

#include "modules/diskspace/DiskSpace.h"
#include "modules/diskspace/cleanup/CleanupModel.h"
#include "modules/diskspace/cleanup/CleanupPane.h"
#include "modules/diskspace/cleanup/DeleteGuard.h"
#include "modules/diskspace/cleanup/ScanEngine.h"
#include "modules/diskspace/cleanup/ScanSnapshot.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QStringList>

// Datos fijos de la ventana de limpieza para las capturas de QA (--ui-shot): un disco de mentira con
// los numeros del diseno. No se lee ningun disco ni se construye ningun hilo.
namespace CleanupFixture {

struct Data
{
    DriveInfo drive;
    DeleteGuard guard;
    bool scanning = false;
    ScanEngine::Progress progress;
    QList<Cleanup::Category> categories;
    QSet<ScanTree::Index> expanded;
    QSet<ScanTree::Index> counting; ///< escaneando: las que todavia se estan contando
    QHash<ScanTree::Index, ScanEngine::FileListing> listings;
    ScanSnapshot baseline;
    QList<ScanSnapshot::Change> changes;
    qint64 usedDelta = 0;
    QStringList openCategories;
    QString banner;
    QString skippedSummary;
    QList<CleanupPane::SkippedLine> skipped;
    int tab = 0;
    QStringList selectedPaths;
};

// cleanup, cleanup-pick, cleanup-result, folders, folders-selected, files, changes, scanning.
QStringList states();
// Arma el arbol de mentira adentro de `engine` y devuelve el resto.
Data build(const QString &state, ScanEngine &engine);

} // namespace CleanupFixture

#endif // MIGHTYTOOLS_CLEANUPFIXTURE_H
