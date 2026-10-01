#include "modules/diskspace/cleanup/ScanSnapshot.h"

#include "core/AppSettings.h"
#include "core/AutomatedRun.h"
#include "modules/diskspace/cleanup/DeleteGuard.h"
#include "modules/diskspace/cleanup/ScanTree.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>

namespace {

// Nombre de archivo de un volumen: su raiz en minusculas, hasheada (una raiz de mac lleva barras).
QString fileBase(const QString &root)
{
    const QByteArray hash = QCryptographicHash::hash(DeleteGuard::clean(root).toLower().toUtf8(), QCryptographicHash::Sha1);
    return QString::fromLatin1(hash.toHex().left(12));
}

QString resolveDir(const QString &dir)
{
    return dir.isEmpty() ? ScanSnapshot::storageDir() : dir;
}

ScanSnapshot load(const QString &path)
{
    ScanSnapshot snapshot;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return snapshot;
    }
    const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    snapshot.root = object.value(QStringLiteral("root")).toString();
    snapshot.takenAt = QDateTime::fromString(object.value(QStringLiteral("takenAt")).toString(), Qt::ISODate);
    snapshot.usedBytes = qint64(object.value(QStringLiteral("usedBytes")).toDouble());
    const QJsonArray dirs = object.value(QStringLiteral("dirs")).toArray();
    for (const QJsonValue &entry : dirs) {
        const QJsonArray pair = entry.toArray();
        if (pair.size() == 2) {
            snapshot.dirs.insert(pair.at(0).toString(), qint64(pair.at(1).toDouble()));
        }
    }
    return snapshot;
}

void collect(const ScanTree &tree, quint32 index, ScanSnapshot &snapshot)
{
    for (const quint32 child : tree.children(index)) {
        // Los hijos vienen de mayor a menor: debajo del minimo no queda ninguno que entre.
        if (qint64(tree.node(child).bytes) < ScanSnapshot::kMinBytes) {
            break;
        }
        snapshot.dirs.insert(tree.path(child), qint64(tree.node(child).bytes));
        collect(tree, child, snapshot);
    }
}

} // namespace

ScanSnapshot ScanSnapshot::fromTree(const ScanTree &tree, qint64 usedBytes, const QDateTime &now)
{
    ScanSnapshot snapshot;
    if (tree.isEmpty()) {
        return snapshot;
    }
    snapshot.root = tree.rootPath();
    snapshot.takenAt = now;
    snapshot.usedBytes = usedBytes;
    collect(tree, tree.root(), snapshot);
    return snapshot;
}

QList<ScanSnapshot::Change> ScanSnapshot::diff(const ScanSnapshot &before, const ScanSnapshot &after, qint64 minDelta)
{
    QList<Change> candidates;
    QHash<QString, qint64> deltas;
    const auto consider = [&](const QString &path) {
        if (deltas.contains(path)) {
            return;
        }
        // Una carpeta que no figura pesaba (o pesa) menos que el minimo del resumen: se toma como cero.
        const qint64 was = before.dirs.value(path, 0);
        const qint64 now = after.dirs.value(path, 0);
        deltas.insert(path, now - was);
        if (qAbs(now - was) >= minDelta) {
            candidates.append(Change{path, now - was, now});
        }
    };
    for (auto it = after.dirs.constBegin(); it != after.dirs.constEnd(); ++it) {
        consider(it.key());
    }
    for (auto it = before.dirs.constBegin(); it != before.dirs.constEnd(); ++it) {
        consider(it.key());
    }
    // Una madre se saca si una hija suya, en la lista, explica casi todo su cambio.
    QList<Change> result;
    for (const Change &change : candidates) {
        bool explained = false;
        for (const Change &other : candidates) {
            if (other.path.size() > change.path.size() && DeleteGuard::isInside(other.path, change.path)
                && (other.delta > 0) == (change.delta > 0) && double(qAbs(other.delta)) >= 0.8 * double(qAbs(change.delta))) {
                explained = true;
                break;
            }
        }
        if (!explained) {
            result.append(change);
        }
    }
    std::sort(result.begin(), result.end(), [](const Change &a, const Change &b) {
        if (a.delta != b.delta) {
            return a.delta > b.delta;
        }
        return a.path < b.path;
    });
    return result;
}

QString ScanSnapshot::storageDir()
{
    return QFileInfo(AppSettings::filePath()).absoluteDir().filePath(QStringLiteral("scans"));
}

bool ScanSnapshot::store(const ScanSnapshot &snapshot, bool rotate, const QString &dir)
{
    // Una corrida automatizada no escribe en la carpeta de ajustes del usuario (si en una de prueba).
    if (!snapshot.isValid() || (AutomatedRun::active() && dir.isEmpty())) {
        return false;
    }
    const QString folder = resolveDir(dir);
    if (!QDir().mkpath(folder)) {
        return false;
    }
    const QString base = QDir(folder).filePath(fileBase(snapshot.root));
    const QString current = base + QStringLiteral(".json");
    const QString previous = base + QStringLiteral(".prev.json");
    if (rotate && QFile::exists(current)) {
        QFile::remove(previous);
        QFile::rename(current, previous);
    }
    QJsonArray dirs;
    for (auto it = snapshot.dirs.constBegin(); it != snapshot.dirs.constEnd(); ++it) {
        dirs.append(QJsonArray{it.key(), double(it.value())});
    }
    QJsonObject object;
    object.insert(QStringLiteral("root"), snapshot.root);
    object.insert(QStringLiteral("takenAt"), snapshot.takenAt.toString(Qt::ISODate));
    object.insert(QStringLiteral("usedBytes"), double(snapshot.usedBytes));
    object.insert(QStringLiteral("dirs"), dirs);
    QSaveFile file(current);
    return file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(object).toJson(QJsonDocument::Compact)) >= 0 && file.commit();
}

ScanSnapshot ScanSnapshot::loadPrevious(const QString &root, const QString &dir)
{
    return load(QDir(resolveDir(dir)).filePath(fileBase(root) + QStringLiteral(".prev.json")));
}

ScanSnapshot ScanSnapshot::loadCurrent(const QString &root, const QString &dir)
{
    return load(QDir(resolveDir(dir)).filePath(fileBase(root) + QStringLiteral(".json")));
}
