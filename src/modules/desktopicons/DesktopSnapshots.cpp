#include "modules/desktopicons/DesktopSnapshots.h"

#include "core/I18n.h"

#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace {

constexpr int kFormatVersion = 1;

bool kindFromKey(const QString &key, SnapshotKind *kind)
{
    for (const SnapshotKind candidate : {SnapshotKind::Manual, SnapshotKind::Automatic, SnapshotKind::BeforeRestore}) {
        if (DesktopSnapshots::kindKey(candidate) == key) {
            *kind = candidate;
            return true;
        }
    }
    return false;
}

QJsonArray sizeArray(const QSize &size)
{
    return QJsonArray{size.width(), size.height()};
}

bool sizeFrom(const QJsonValue &value, QSize *size)
{
    const QJsonArray array = value.toArray();
    if (array.size() != 2 || !array.at(0).isDouble() || !array.at(1).isDouble()) return false;
    *size = QSize(array.at(0).toInt(), array.at(1).toInt());
    return true;
}

qint64 screenKey(const QSize &screen)
{
    return (qint64(screen.width()) << 32) | quint32(screen.height());
}

// Los de un tipo, del mas nuevo al mas viejo.
QList<const DesktopSnapshot *> newestFirst(const QList<DesktopSnapshot> &stored, SnapshotKind kind)
{
    QList<const DesktopSnapshot *> list;
    for (const DesktopSnapshot &snapshot : stored) {
        if (snapshot.kind == kind) list.append(&snapshot);
    }
    std::stable_sort(list.begin(), list.end(), [](const DesktopSnapshot *a, const DesktopSnapshot *b) {
        return a->when > b->when;
    });
    return list;
}

} // namespace

namespace DesktopSnapshots {

QString kindKey(SnapshotKind kind)
{
    switch (kind) {
    case SnapshotKind::Automatic: return QStringLiteral("automatic");
    case SnapshotKind::BeforeRestore: return QStringLiteral("beforeRestore");
    case SnapshotKind::Manual: break;
    }
    return QStringLiteral("manual");
}

QByteArray toJson(const DesktopSnapshot &snapshot)
{
    QJsonArray icons;
    for (const DesktopIconView::Icon &icon : snapshot.layout.icons) {
        icons.append(QJsonObject{{QStringLiteral("path"), icon.path},
                                 {QStringLiteral("name"), icon.name},
                                 {QStringLiteral("x"), icon.position.x()},
                                 {QStringLiteral("y"), icon.position.y()}});
    }
    const DesktopIconView::Layout &layout = snapshot.layout;
    const QJsonObject root{{QStringLiteral("version"), kFormatVersion},
                           {QStringLiteral("kind"), kindKey(snapshot.kind)},
                           {QStringLiteral("name"), snapshot.name},
                           {QStringLiteral("when"), snapshot.when.toString(Qt::ISODateWithMs)},
                           {QStringLiteral("screen"), sizeArray(layout.screen)},
                           {QStringLiteral("dpi"), layout.dpi},
                           {QStringLiteral("spacing"), sizeArray(layout.spacing)},
                           {QStringLiteral("autoArrange"), layout.autoArrange},
                           {QStringLiteral("snapToGrid"), layout.snapToGrid},
                           {QStringLiteral("icons"), icons}};
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool fromJson(const QByteArray &json, DesktopSnapshot *snapshot, QString *error)
{
    const auto fail = [error](const QString &why) {
        if (error) *error = why;
        return false;
    };
    QJsonParseError parse;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) {
        return fail(QStringLiteral("JSON invalido: %1").arg(parse.errorString()));
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("version")).toInt() != kFormatVersion) {
        return fail(QStringLiteral("version desconocida"));
    }
    DesktopSnapshot result;
    if (!kindFromKey(root.value(QStringLiteral("kind")).toString(), &result.kind)) {
        return fail(QStringLiteral("tipo desconocido"));
    }
    result.name = root.value(QStringLiteral("name")).toString();
    result.when = QDateTime::fromString(root.value(QStringLiteral("when")).toString(), Qt::ISODateWithMs);
    if (!result.when.isValid()) {
        return fail(QStringLiteral("fecha invalida"));
    }
    DesktopIconView::Layout &layout = result.layout;
    if (!sizeFrom(root.value(QStringLiteral("screen")), &layout.screen) || layout.screen.isEmpty()) {
        return fail(QStringLiteral("pantalla invalida"));
    }
    layout.dpi = root.value(QStringLiteral("dpi")).toInt(96);
    sizeFrom(root.value(QStringLiteral("spacing")), &layout.spacing);
    layout.autoArrange = root.value(QStringLiteral("autoArrange")).toBool();
    layout.snapToGrid = root.value(QStringLiteral("snapToGrid")).toBool();
    const QJsonValue icons = root.value(QStringLiteral("icons"));
    if (!icons.isArray()) {
        return fail(QStringLiteral("sin lista de iconos"));
    }
    for (const QJsonValue &value : icons.toArray()) {
        const QJsonObject object = value.toObject();
        DesktopIconView::Icon icon;
        icon.path = object.value(QStringLiteral("path")).toString();
        icon.name = object.value(QStringLiteral("name")).toString();
        if (icon.path.isEmpty() || !object.value(QStringLiteral("x")).isDouble() || !object.value(QStringLiteral("y")).isDouble()) {
            return fail(QStringLiteral("icono invalido"));
        }
        icon.position = QPoint(object.value(QStringLiteral("x")).toInt(), object.value(QStringLiteral("y")).toInt());
        layout.icons.append(icon);
    }
    result.id = snapshot->id;
    *snapshot = result;
    return true;
}

bool sameArrangement(const DesktopIconView::Layout &a, const DesktopIconView::Layout &b)
{
    if (a.icons.size() != b.icons.size()) return false;
    QHash<QString, QPoint> positions;
    for (const DesktopIconView::Icon &icon : a.icons) positions.insert(icon.path, icon.position);
    for (const DesktopIconView::Icon &icon : b.icons) {
        const auto found = positions.constFind(icon.path);
        if (found == positions.constEnd() || found.value() != icon.position) return false;
    }
    return positions.size() == b.icons.size();
}

bool isUsualScreen(const QList<DesktopSnapshot> &stored, const QSize &screen)
{
    QHash<qint64, int> counts;
    int most = 0;
    for (const DesktopSnapshot &snapshot : stored) {
        if (snapshot.kind == SnapshotKind::BeforeRestore) continue;
        most = qMax(most, ++counts[screenKey(snapshot.layout.screen)]);
    }
    return most == 0 || counts.value(screenKey(screen)) == most;
}

QSize usualScreen(const QList<DesktopSnapshot> &stored)
{
    QSize best;
    QDateTime bestWhen;
    for (const DesktopSnapshot &snapshot : stored) {
        if (snapshot.kind == SnapshotKind::BeforeRestore || !isUsualScreen(stored, snapshot.layout.screen)) continue;
        if (!best.isValid() || snapshot.when > bestWhen) {
            best = snapshot.layout.screen;
            bestWhen = snapshot.when;
        }
    }
    return best;
}

QStringList toPrune(const QList<DesktopSnapshot> &stored, const QString &keep)
{
    QStringList ids;
    for (const auto &[kind, limit] : {std::pair{SnapshotKind::Automatic, kKeepAutomatic},
                                      std::pair{SnapshotKind::BeforeRestore, kKeepBeforeRestore}}) {
        const QList<const DesktopSnapshot *> list = newestFirst(stored, kind);
        for (int i = limit; i < list.size(); ++i) {
            if (list.at(i)->id != keep) ids.append(list.at(i)->id);
        }
    }
    return ids;
}

bool needsAutomatic(const QList<DesktopSnapshot> &stored, const QDateTime &now, const QDateTime &awakeSince,
                    const QDate &skippedDay, qint64 idleMs)
{
    if (skippedDay == now.date() || idleMs > qint64(kIdleMinutes) * 60 * 1000) return false;
    for (const DesktopSnapshot &snapshot : stored) {
        if (snapshot.kind == SnapshotKind::Automatic && snapshot.when.date() == now.date()) return false;
    }
    return awakeSince.isValid() && awakeSince.secsTo(now) >= qint64(kAwakeMinutes) * 60;
}

AutomaticStep automaticStep(const QList<DesktopSnapshot> &stored, const DesktopIconView::Layout &current,
                            ScreenStreak *streak, QString *sameId)
{
    if (current.icons.isEmpty()) return AutomaticStep::Wait;
    // Sin manuales ni automaticos guardados (todos borrados) no hay habitual: la fijada por una racha se olvida.
    if (std::all_of(stored.cbegin(), stored.cend(), [](const DesktopSnapshot &s) { return s.kind == SnapshotKind::BeforeRestore; })) {
        streak->usual = QSize();
    }
    const bool usual = streak->usual.isValid() ? streak->usual == current.screen : isUsualScreen(stored, current.screen);
    if (!usual) {
        streak->days = streak->screen == current.screen ? streak->days + 1 : 1;
        streak->screen = current.screen;
        if (streak->days < kStreakDays) return AutomaticStep::SkipScreen;
        // Tantos dias seguidos con la misma: es la pantalla nueva, no un arranque fallido.
        streak->usual = current.screen;
    }
    streak->screen = QSize();
    streak->days = 0;
    const QList<const DesktopSnapshot *> automatic = newestFirst(stored, SnapshotKind::Automatic);
    if (!automatic.isEmpty() && sameArrangement(automatic.first()->layout, current)) {
        if (sameId) *sameId = automatic.first()->id;
        return AutomaticStep::Update;
    }
    return AutomaticStep::Save;
}

bool suspendedBetween(qint64 wallBeforeMs, qint64 awakeBeforeMs, qint64 wallNowMs, qint64 awakeNowMs)
{
    // shortcut: la suspension se deduce comparando los dos relojes en cada turno; un cambio manual del
    // reloj de mas de 2 min hacia adelante tambien cuenta (solo atrasa el automatico una hora). Si
    // molestara, escuchar WM_POWERBROADCAST con una ventana propia.
    constexpr qint64 kGapMs = 2 * 60 * 1000;
    if (wallBeforeMs <= 0 || awakeBeforeMs <= 0) return false;
    return (wallNowMs - wallBeforeMs) - (awakeNowMs - awakeBeforeMs) > kGapMs;
}

RestorePlan plan(const DesktopIconView::Layout &target, const DesktopIconView::Layout &current)
{
    QSet<QString> present;
    for (const DesktopIconView::Icon &icon : current.icons) present.insert(icon.path);
    RestorePlan result;
    for (const DesktopIconView::Icon &icon : target.icons) {
        if (present.contains(icon.path)) {
            result.moves.append(icon);
        } else {
            result.missing.append(icon.name.isEmpty() ? QFileInfo(icon.path).completeBaseName() : icon.name);
        }
    }
    return result;
}

RestoreRun restoreInto(QList<DesktopSnapshot> *stored, const QString &targetId, const DesktopIconView::Layout &current,
                       const QDateTime &now, const QString &previousUndo, const std::function<QString()> &newId,
                       const std::function<bool(const DesktopSnapshot &target, const RestorePlan &plan)> &apply)
{
    RestoreRun run;
    const auto exists = [stored](const QString &id) {
        return !id.isEmpty() && std::any_of(stored->cbegin(), stored->cend(), [&id](const DesktopSnapshot &s) { return s.id == id; });
    };
    const auto target = std::find_if(stored->cbegin(), stored->cend(), [&targetId](const DesktopSnapshot &s) {
        return s.id == targetId;
    });
    if (target == stored->cend()) return run;
    run.found = true;
    const DesktopSnapshot wanted = *target; // copia: la lista cambia abajo
    run.plan = plan(wanted.layout, current);
    // Ya esta restaurado (doble click en Restore): todos los iconos del plan en su lugar y los mismos flags.
    QHash<QString, QPoint> at;
    for (const DesktopIconView::Icon &icon : current.icons) at.insert(icon.path, icon.position);
    const bool alreadyThere = current.autoArrange == wanted.layout.autoArrange && current.snapToGrid == wanted.layout.snapToGrid
        && std::all_of(run.plan.moves.cbegin(), run.plan.moves.cend(),
                       [&at](const DesktopIconView::Icon &icon) { return at.value(icon.path) == icon.position; });
    // El escritorio de ahora ya esta guardado: no hace falta otro "antes" (y no se empuja afuera el original).
    // Con los mismos flags y nunca el destino: un cambio que es solo de flags tiene que poder deshacerse.
    QString match;
    QDateTime matchWhen;
    for (const DesktopSnapshot &snapshot : std::as_const(*stored)) {
        if (snapshot.id != targetId && (match.isEmpty() || snapshot.when > matchWhen)
            && snapshot.layout.autoArrange == current.autoArrange && snapshot.layout.snapToGrid == current.snapToGrid
            && sameArrangement(snapshot.layout, current)) {
            match = snapshot.id;
            matchWhen = snapshot.when;
        }
    }
    if (alreadyThere) {
        run.undoId = exists(previousUndo) ? previousUndo : QString();
    } else if (match.isEmpty()) {
        DesktopSnapshot before;
        before.id = newId();
        before.kind = SnapshotKind::BeforeRestore;
        before.when = now;
        before.layout = current;
        stored->append(before);
        run.created << before.id;
        run.undoId = before.id;
    } else {
        run.undoId = match;
    }
    run.ok = apply(wanted, run.plan);
    if (run.ok) {
        run.pruned = toPrune(*stored, run.undoId);
        stored->erase(std::remove_if(stored->begin(), stored->end(),
                                     [&run](const DesktopSnapshot &s) { return run.pruned.contains(s.id); }),
                      stored->end());
    }
    return run;
}

QString whenText(const QDateTime &when, const QDateTime &now)
{
    // Las mismas claves que la ventana de limpieza ("today %1"), con mayuscula: aca empiezan la linea.
    const QString time = when.toString(QStringLiteral("HH:mm"));
    const qint64 days = when.date().daysTo(now.date());
    QString text = days == 0   ? I18n::tr("today %1").arg(time)
                   : days == 1 ? I18n::tr("yesterday %1").arg(time)
                               : when.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
    text[0] = text.at(0).toUpper();
    return text;
}

QString screenText(const QSize &size)
{
    return QStringLiteral("%1 × %2").arg(size.width()).arg(size.height());
}

} // namespace DesktopSnapshots
