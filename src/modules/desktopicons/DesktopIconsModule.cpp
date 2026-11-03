#include "modules/desktopicons/DesktopIconsModule.h"

#include "app/ModuleContext.h"
#include "core/AppSettings.h"
#include "core/AutomatedRun.h"
#include "core/I18n.h"
#include "modules/desktopicons/DesktopIconsPanel.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QSaveFile>
#include <QSet>
#include <QTimer>

#include <algorithm>
#include <cstdio>

namespace {

constexpr int kTickMs = 15 * 60 * 1000;
constexpr qint64 kMaxFileBytes = 8 * 1024 * 1024;

const char *const kDailyKey = "daily";
const char *const kSkippedDayKey = "skippedDay";

// Monitor con su pie y cuatro iconos arriba a la izquierda, en la caja de 16 de los demas iconos.
void paintDesktopIcon(QPainter &painter, const QRectF &rect, const QColor &color)
{
    const qreal scale = qMin(rect.width(), rect.height()) / 16.0;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.translate(rect.center().x() - 8 * scale, rect.center().y() - 8 * scale);
    painter.scale(scale, scale);
    painter.setPen(QPen(color, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(1.7, 2.2, 12.6, 9.0), 1.6, 1.6);
    painter.drawLine(QPointF(8, 11.2), QPointF(8, 13.8));
    painter.drawLine(QPointF(5.4, 13.8), QPointF(10.6, 13.8));
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    for (const QPointF &corner : {QPointF(4.0, 4.4), QPointF(6.9, 4.4), QPointF(4.0, 7.2), QPointF(6.9, 7.2)}) {
        painter.drawRoundedRect(QRectF(corner, QSizeF(1.9, 1.9)), 0.4, 0.4);
    }
    painter.restore();
}

QString idPrefix(SnapshotKind kind)
{
    switch (kind) {
    case SnapshotKind::Automatic: return QStringLiteral("auto");
    case SnapshotKind::BeforeRestore: return QStringLiteral("before");
    case SnapshotKind::Manual: break;
    }
    return QStringLiteral("manual");
}

// ---- Datos de prueba (capturas y self-test): nunca un escritorio real.

DesktopIconView::Layout fixtureLayout(int count, const QSize &screen, int shift)
{
    DesktopIconView::Layout layout;
    layout.screen = screen;
    layout.spacing = QSize(76, 102);
    layout.snapToGrid = true;
    const int rows = qMax(1, (screen.height() - 60) / layout.spacing.height());
    for (int i = 0; i < count; ++i) {
        DesktopIconView::Icon icon;
        icon.name = QStringLiteral("App %1").arg(i + 1);
        icon.path = QStringLiteral("C:\\Users\\Public\\Desktop\\App %1.lnk").arg(i + 1);
        const int column = i / rows;
        const int row = i % rows;
        icon.position = QPoint(14 + column * layout.spacing.width() + (column >= 6 ? shift * layout.spacing.width() : 0),
                               6 + row * layout.spacing.height());
        layout.icons.append(icon);
    }
    return layout;
}

DesktopSnapshot fixture(const QString &id, SnapshotKind kind, const QDateTime &when, int icons, const QString &name = {},
                        const QSize &screen = QSize(1920, 1080), int shift = 0)
{
    DesktopSnapshot snapshot;
    snapshot.id = id;
    snapshot.kind = kind;
    snapshot.name = name;
    snapshot.when = when;
    snapshot.layout = fixtureLayout(icons, screen, shift);
    return snapshot;
}

DesktopIconView::Layout layoutOf(const QList<QPair<QString, QPoint>> &icons, const QSize &screen = QSize(1920, 1080))
{
    DesktopIconView::Layout layout;
    layout.screen = screen;
    for (const auto &entry : icons) {
        DesktopIconView::Icon icon;
        icon.path = entry.first;
        icon.name = QFileInfo(entry.first).completeBaseName();
        icon.position = entry.second;
        layout.icons.append(icon);
    }
    return layout;
}

bool sameLayout(const DesktopIconView::Layout &a, const DesktopIconView::Layout &b)
{
    if (a.screen != b.screen || a.dpi != b.dpi || a.spacing != b.spacing || a.autoArrange != b.autoArrange
        || a.snapToGrid != b.snapToGrid || a.icons.size() != b.icons.size()) {
        return false;
    }
    for (int i = 0; i < a.icons.size(); ++i) {
        const DesktopIconView::Icon &x = a.icons.at(i);
        const DesktopIconView::Icon &y = b.icons.at(i);
        if (x.path != y.path || x.name != y.name || x.position != y.position) return false;
    }
    return true;
}

QSet<QString> asSet(const QStringList &list)
{
    return QSet<QString>(list.cbegin(), list.cend());
}

void selfTest(const std::function<void(bool, const QString &)> &check)
{
    using namespace DesktopSnapshots;
    const QDateTime now(QDate(2026, 11, 2), QTime(15, 0));
    const SnapshotKind automatic = SnapshotKind::Automatic;
    const SnapshotKind manual = SnapshotKind::Manual;
    const SnapshotKind before = SnapshotKind::BeforeRestore;

    // Retencion.
    QList<DesktopSnapshot> stored;
    for (int i = 0; i < 7; ++i) stored << fixture(QStringLiteral("a%1").arg(i), automatic, now.addDays(-i), 3);
    stored << fixture(QStringLiteral("m1"), manual, now.addDays(-40), 3) << fixture(QStringLiteral("m2"), manual, now.addDays(-90), 3);
    stored << fixture(QStringLiteral("b0"), before, now.addDays(-2), 3) << fixture(QStringLiteral("b1"), before, now.addSecs(-60), 3);
    check(asSet(toPrune(stored)) == QSet<QString>({QStringLiteral("a5"), QStringLiteral("a6"), QStringLiteral("b0")}),
          QStringLiteral("retencion: quedan los 5 automaticos mas nuevos; los manuales, aunque viejos, quedan"));
    QList<DesktopSnapshot> shuffled = {stored.at(6), stored.at(1), stored.at(9), stored.at(4), stored.at(0), stored.at(10),
                                       stored.at(5), stored.at(2), stored.at(7), stored.at(3), stored.at(8)};
    check(asSet(toPrune(shuffled)) == asSet(toPrune(stored)), QStringLiteral("retencion: no depende del orden de la lista"));
    check(toPrune(stored.mid(0, 5) + stored.mid(7, 2)).isEmpty(),
          QStringLiteral("retencion: con 5 automaticos y manuales no se borra nada"));
    check(toPrune({stored.at(9), stored.at(10)}) == QStringList{QStringLiteral("b0")},
          QStringLiteral("antes de restaurar: queda solo el mas nuevo"));

    // Igual al anterior.
    const DesktopIconView::Layout a = layoutOf({{QStringLiteral("C:/D/a.lnk"), {10, 10}}, {QStringLiteral("C:/D/b.lnk"), {10, 110}}});
    const DesktopIconView::Layout reordered = layoutOf({{QStringLiteral("C:/D/b.lnk"), {10, 110}}, {QStringLiteral("C:/D/a.lnk"), {10, 10}}});
    const DesktopIconView::Layout moved = layoutOf({{QStringLiteral("C:/D/a.lnk"), {10, 10}}, {QStringLiteral("C:/D/b.lnk"), {86, 110}}});
    const DesktopIconView::Layout extra = layoutOf({{QStringLiteral("C:/D/a.lnk"), {10, 10}}, {QStringLiteral("C:/D/b.lnk"), {10, 110}},
                                                    {QStringLiteral("C:/D/c.lnk"), {10, 210}}});
    const DesktopIconView::Layout renamed = layoutOf({{QStringLiteral("C:/D/a.lnk"), {10, 10}}, {QStringLiteral("C:/D/z.lnk"), {10, 110}}});
    check(sameArrangement(a, reordered), QStringLiteral("igual: mismos iconos y posiciones en otro orden"));
    check(!sameArrangement(a, moved) && !sameArrangement(a, extra) && !sameArrangement(extra, a) && !sameArrangement(a, renamed),
          QStringLiteral("igual: un icono movido, uno de mas o uno distinto ya no es igual"));

    // Resolucion habitual.
    const QSize big(1920, 1080);
    const QSize small(1024, 768);
    check(isUsualScreen({}, small), QStringLiteral("habitual: sin snapshots cualquier resolucion es habitual"));
    const QList<DesktopSnapshot> mostlyBig = {fixture(QStringLiteral("x1"), automatic, now, 1),
                                              fixture(QStringLiteral("x2"), automatic, now, 1),
                                              fixture(QStringLiteral("x3"), manual, now, 1),
                                              fixture(QStringLiteral("x4"), automatic, now, 1, {}, small)};
    check(isUsualScreen(mostlyBig, big) && !isUsualScreen(mostlyBig, small),
          QStringLiteral("habitual: la que mas se repite gana; la otra no es habitual"));
    const QList<DesktopSnapshot> tie = {fixture(QStringLiteral("t1"), automatic, now, 1),
                                        fixture(QStringLiteral("t2"), automatic, now, 1, {}, small)};
    check(isUsualScreen(tie, big) && isUsualScreen(tie, small) && !isUsualScreen(tie, QSize(2560, 1440)),
          QStringLiteral("habitual: en un empate las empatadas cuentan como habituales; una tercera no"));
    const QList<DesktopSnapshot> undoNoise = {fixture(QStringLiteral("u1"), automatic, now, 1),
                                              fixture(QStringLiteral("u2"), before, now, 1, {}, small),
                                              fixture(QStringLiteral("u3"), before, now, 1, {}, small)};
    check(!isUsualScreen(undoNoise, small), QStringLiteral("habitual: los antes de restaurar no cuentan"));

    // Cuando toca el automatico.
    const QList<DesktopSnapshot> older = {fixture(QStringLiteral("y"), automatic, now.addDays(-1), 3)};
    check(!needsAutomatic(older, now, now.addSecs(-59 * 60), QDate()), QStringLiteral("despierta: 59 min no alcanza"));
    check(needsAutomatic(older, now, now.addSecs(-61 * 60), QDate()), QStringLiteral("despierta: 61 min alcanza"));
    check(!needsAutomatic(older + QList<DesktopSnapshot>{fixture(QStringLiteral("t"), automatic, now.addSecs(-3600), 3)}, now,
                          now.addDays(-1), QDate()),
          QStringLiteral("despierta: con un automatico de hoy no hace falta otro"));
    check(needsAutomatic(older + QList<DesktopSnapshot>{fixture(QStringLiteral("t"), manual, now.addSecs(-3600), 3)}, now,
                         now.addDays(-1), QDate()),
          QStringLiteral("despierta: un manual de hoy no reemplaza al automatico"));
    check(!needsAutomatic(older, now, now.addDays(-1), now.date()) && needsAutomatic(older, now, now.addDays(-1), now.date().addDays(-1)),
          QStringLiteral("despierta: el dia salteado no guarda; el dia siguiente si"));
    check(!needsAutomatic(older, now, QDateTime(), QDate()), QStringLiteral("despierta: sin hora de arranque no guarda"));

    // Suspension: el reloj despierto no cuenta el tiempo dormido.
    const qint64 wall0 = now.toMSecsSinceEpoch();
    const qint64 awake0 = 3600 * 1000;
    const qint64 quarter = 15 * 60 * 1000;
    check(!suspendedBetween(wall0, awake0, wall0 + quarter, awake0 + quarter),
          QStringLiteral("suspension: 15 min de pared y 15 despierta no es suspension"));
    check(suspendedBetween(wall0, awake0, wall0 + 8 * 3600 * 1000, awake0 + quarter),
          QStringLiteral("suspension: 8 h de pared y 15 min despierta es suspension"));
    check(!suspendedBetween(wall0, awake0, wall0 + quarter + 30 * 1000, awake0 + quarter),
          QStringLiteral("suspension: 30 s de deriva del reloj no cuentan"));
    check(!suspendedBetween(0, 0, wall0, awake0), QStringLiteral("suspension: el primer turno no tiene con que comparar"));

    // Que hace el automatico con el escritorio de ahora.
    QList<DesktopSnapshot> history = {fixture(QStringLiteral("old"), automatic, now.addDays(-3), 0),
                                      fixture(QStringLiteral("new"), automatic, now.addDays(-1), 0)};
    history[0].layout = moved;
    history[1].layout = a;
    QString sameId;
    check(automaticStep(history, reordered, &sameId) == AutomaticStep::Update && sameId == QLatin1String("new"),
          QStringLiteral("automatico: igual al anterior actualiza la fecha de ese"));
    check(automaticStep(history, moved, nullptr) == AutomaticStep::Save,
          QStringLiteral("automatico: igual a uno mas viejo pero no al anterior guarda uno nuevo"));
    check(automaticStep(history, layoutOf({{QStringLiteral("C:/D/a.lnk"), {5, 5}}}, small), nullptr) == AutomaticStep::SkipScreen,
          QStringLiteral("automatico: otra resolucion que la habitual saltea el dia"));
    check(automaticStep(history, DesktopIconView::Layout{}, nullptr) == AutomaticStep::Wait,
          QStringLiteral("automatico: un escritorio vacio espera al proximo turno"));

    // Plan de restauracion.
    DesktopIconView::Layout target = layoutOf({{QStringLiteral("C:/D/a.lnk"), {10, 10}},
                                               {QStringLiteral("C:/D/gone.lnk"), {10, 110}},
                                               {QStringLiteral("C:/D/b.lnk"), {10, 210}}});
    target.icons[1].name = QStringLiteral("Gone app");
    target.icons.append({QStringLiteral("C:/D/Unnamed tool.lnk"), QString(), QPoint(10, 310)});
    const DesktopIconView::Layout now2 = layoutOf({{QStringLiteral("C:/D/b.lnk"), {500, 500}},
                                                   {QStringLiteral("C:/D/new.lnk"), {600, 600}},
                                                   {QStringLiteral("C:/D/a.lnk"), {700, 700}}});
    const RestorePlan restore = plan(target, now2);
    check(restore.moves.size() == 2 && restore.moves.at(0).path == QLatin1String("C:/D/a.lnk")
              && restore.moves.at(0).position == QPoint(10, 10) && restore.moves.at(1).position == QPoint(10, 210),
          QStringLiteral("plan: mueve los que siguen, a la posicion del snapshot"));
    check(restore.missing == QStringList({QStringLiteral("Gone app"), QStringLiteral("Unnamed tool")}),
          QStringLiteral("plan: los que ya no estan se informan por nombre (o por el archivo) y se saltean"));
    bool touchesNew = false;
    for (const DesktopIconView::Icon &icon : restore.moves) touchesNew = touchesNew || icon.path.endsWith(QLatin1String("new.lnk"));
    check(!touchesNew, QStringLiteral("plan: un icono nuevo que no esta en el snapshot no se toca"));

    // JSON ida y vuelta y archivos corruptos.
    DesktopSnapshot original = fixture(QStringLiteral("auto-x"), manual, QDateTime(QDate(2026, 10, 30), QTime(9, 41, 12, 345)), 4,
                                       QStringLiteral("Antes de reinstalar · ñ"));
    original.layout.dpi = 144;
    original.layout.autoArrange = true;
    original.layout.icons[2].path = QStringLiteral("::{20D04FE0-3AEA-1069-A2D8-08002B30309D}");
    DesktopSnapshot loaded;
    loaded.id = QStringLiteral("auto-x");
    QString error;
    const QByteArray json = toJson(original);
    check(fromJson(json, &loaded, &error) && loaded.id == original.id && loaded.kind == original.kind
              && loaded.name == original.name && loaded.when == original.when && sameLayout(loaded.layout, original.layout),
          QStringLiteral("json: ida y vuelta con tipo, nombre, fecha, pantalla, DPI, espaciado, flags e iconos (%1)").arg(error));
    for (const SnapshotKind kind : {automatic, before}) {
        DesktopSnapshot copy = original;
        copy.kind = kind;
        DesktopSnapshot back;
        check(fromJson(toJson(copy), &back) && back.kind == kind, QStringLiteral("json: el tipo %1 vuelve igual").arg(int(kind)));
    }
    QByteArray badVersion = json;
    badVersion.replace("\"version\": 1", "\"version\": 2");
    QByteArray badKind = json;
    badKind.replace("\"kind\": \"manual\"", "\"kind\": \"weird\"");
    QByteArray noPath = json;
    noPath.replace("\"path\": \"::{20D04FE0", "\"pathx\": \"::{20D04FE0");
    DesktopSnapshot rejected;
    check(!fromJson(QByteArrayLiteral("{not json"), &rejected) && !fromJson(json.left(json.size() / 2), &rejected)
              && !fromJson(QByteArrayLiteral("[]"), &rejected) && !fromJson(badVersion, &rejected)
              && !fromJson(badKind, &rejected) && !fromJson(noPath, &rejected)
              && !fromJson(QByteArrayLiteral("{\"version\":1,\"kind\":\"manual\",\"when\":\"2026-10-30T09:41:12\",\"icons\":[]}"), &rejected),
          QStringLiteral("json: corrupto, cortado, otra version, otro tipo, icono sin ruta o sin pantalla se rechazan"));

    // Textos de fecha.
    const QString today = whenText(QDateTime(now.date(), QTime(10, 42)), now);
    const QString yesterday = whenText(QDateTime(now.date().addDays(-1), QTime(22, 15)), now);
    check(today.compare(I18n::tr("today %1").arg(QStringLiteral("10:42")), Qt::CaseInsensitive) == 0 && today.at(0).isUpper()
              && yesterday.compare(I18n::tr("yesterday %1").arg(QStringLiteral("22:15")), Qt::CaseInsensitive) == 0
              && whenText(QDateTime(QDate(2026, 10, 28), QTime(9, 5)), now) == QLatin1String("2026-10-28 09:05"),
          QStringLiteral("fecha: hoy y ayer en palabras y con mayuscula, el resto con la fecha (%1 / %2)").arg(today, yesterday));

    // Una corrida automatizada nunca mueve iconos ni cambia flags, aunque se pida de verdad.
    DesktopIconView::ApplyResult applied;
    check(AutomatedRun::active() && DesktopIconView::apply(true, true, a.icons, false, &applied, &error) && applied.dryRun
              && applied.moved == 2,
          QStringLiteral("restaurar: en corrida automatizada solo loguea, sin tocar el escritorio"));
}

int simulate(const QString &action, const QStringList &args)
{
    if (action == QLatin1String("read")) {
        DesktopIconView::Layout layout;
        QString error;
        if (!DesktopIconView::read(&layout, &error)) {
            std::printf("desktopIcons:read FALLO %s\n", qPrintable(error));
            return 1;
        }
        std::printf("desktopIcons:read icons=%d screen=%dx%d dpi=%d spacing=%dx%d autoArrange=%d snapToGrid=%d\n",
                    int(layout.icons.size()), layout.screen.width(), layout.screen.height(), layout.dpi,
                    layout.spacing.width(), layout.spacing.height(), int(layout.autoArrange), int(layout.snapToGrid));
        for (int i = 0; i < qMin(5, int(layout.icons.size())); ++i) {
            const DesktopIconView::Icon &icon = layout.icons.at(i);
            std::printf("  %s @ %d,%d  [%s]\n", qPrintable(icon.name), icon.position.x(), icon.position.y(),
                        qPrintable(icon.path));
        }
        // Con una ruta: deja el snapshot leido en ese archivo NUEVO (para probar plan sin tocar la app).
        const QString out = args.value(0);
        if (!out.isEmpty()) {
            if (QFileInfo::exists(out)) {
                std::printf("desktopIcons:read %s ya existe: no se pisa\n", qPrintable(out));
                return 1;
            }
            DesktopSnapshot snapshot;
            snapshot.kind = SnapshotKind::Manual;
            snapshot.when = QDateTime::currentDateTime();
            snapshot.layout = layout;
            QSaveFile file(out);
            if (!file.open(QIODevice::WriteOnly) || file.write(DesktopSnapshots::toJson(snapshot)) < 0 || !file.commit()) {
                std::printf("desktopIcons:read no se pudo escribir %s\n", qPrintable(out));
                return 1;
            }
            std::printf("desktopIcons:read guardado en %s\n", qPrintable(out));
        }
        return 0;
    }
    if (action == QLatin1String("plan")) {
        QFile file(args.value(0));
        DesktopSnapshot target;
        QString error;
        if (args.value(0).isEmpty() || !file.open(QIODevice::ReadOnly)
            || !DesktopSnapshots::fromJson(file.read(kMaxFileBytes), &target, &error)) {
            std::printf("desktopIcons:plan no se pudo leer el snapshot '%s' %s\n", qPrintable(args.value(0)), qPrintable(error));
            return 1;
        }
        DesktopIconView::Layout current;
        if (!DesktopIconView::read(&current, &error)) {
            std::printf("desktopIcons:plan FALLO al leer el escritorio: %s\n", qPrintable(error));
            return 1;
        }
        const DesktopSnapshots::RestorePlan restore = DesktopSnapshots::plan(target.layout, current);
        int unchanged = 0;
        QHash<QString, QPoint> now;
        for (const DesktopIconView::Icon &icon : current.icons) now.insert(icon.path, icon.position);
        for (const DesktopIconView::Icon &icon : restore.moves) unchanged += now.value(icon.path) == icon.position;
        std::printf("desktopIcons:plan moveria %d iconos (%d ya en su lugar), %d no encontrados; flags autoArrange=%d snapToGrid=%d; "
                    "pantalla del snapshot %dx%d, ahora %dx%d. No se movio nada.\n",
                    int(restore.moves.size()), unchanged, int(restore.missing.size()), int(target.layout.autoArrange),
                    int(target.layout.snapToGrid), target.layout.screen.width(), target.layout.screen.height(),
                    current.screen.width(), current.screen.height());
        for (int i = 0; i < qMin(10, int(restore.missing.size())); ++i) {
            std::printf("  no encontrado: %s\n", qPrintable(restore.missing.at(i)));
        }
        return 0;
    }
    return 2;
}

} // namespace

DesktopIconsModule::DesktopIconsModule(ModuleContext &context)
    : Module(context)
{
}

DesktopIconsModule::~DesktopIconsModule()
{
    stop();
}

QString DesktopIconsModule::storageFolder()
{
    return QFileInfo(AppSettings::filePath()).absoluteDir().filePath(QStringLiteral("desktop_icons"));
}

void DesktopIconsModule::start()
{
    if (m_started) {
        return;
    }
    m_started = true;
    // En una corrida automatizada (o captura) los snapshots viven en memoria: ni se leen ni se escriben.
    m_folder = context().automatedRun() || context().captureMode() ? QString() : storageFolder();
    loadFromDisk();
    startDailyTimer();
    emit statusChanged();
}

void DesktopIconsModule::stop()
{
    if (!m_started) {
        return;
    }
    m_started = false;
    delete m_timer;
    m_timer = nullptr;
}

void DesktopIconsModule::startDailyTimer()
{
    delete m_timer;
    m_timer = nullptr;
    if (!m_started || !dailyEnabled()) {
        return;
    }
    // "Despierta desde" arranca ahora: si la app se reinicio a mitad del dia, esperar una hora mas no
    // hace dano. Se reinicia en tick() cuando se detecta una suspension.
    m_awakeSince = QDateTime::currentDateTime();
    m_lastWallMs = QDateTime::currentMSecsSinceEpoch();
    m_lastAwakeMs = DesktopIconView::awakeClockMs();
    m_timer = new QTimer(this);
    m_timer->setInterval(kTickMs);
    connect(m_timer, &QTimer::timeout, this, &DesktopIconsModule::tick);
    m_timer->start();
}

void DesktopIconsModule::tick()
{
    const qint64 wall = QDateTime::currentMSecsSinceEpoch();
    const qint64 awake = DesktopIconView::awakeClockMs();
    if (DesktopSnapshots::suspendedBetween(m_lastWallMs, m_lastAwakeMs, wall, awake)) {
        m_awakeSince = QDateTime::currentDateTime();
        qInfo() << "[desktopIcons] la computadora estuvo suspendida: la hora despierta se cuenta desde ahora";
    }
    m_lastWallMs = wall;
    m_lastAwakeMs = awake;
    // Una corrida automatizada (la medicion de consumo) no lee el escritorio.
    if (context().automatedRun()) {
        return;
    }
    const QDateTime now = QDateTime::currentDateTime();
    const QDate skipped = QDate::fromString(context().value(QLatin1String(kSkippedDayKey)).toString(), Qt::ISODate);
    if (!DesktopSnapshots::needsAutomatic(m_snapshots, now, m_awakeSince, skipped)) {
        return;
    }
    DesktopIconView::Layout current;
    QString error;
    if (!DesktopIconView::read(&current, &error)) {
        return; // se reintenta en el proximo turno
    }
    QString sameId;
    switch (DesktopSnapshots::automaticStep(m_snapshots, current, &sameId)) {
    case DesktopSnapshots::AutomaticStep::Wait:
        return;
    case DesktopSnapshots::AutomaticStep::SkipScreen:
        qInfo() << "[desktopIcons] resolucion distinta de la habitual" << current.screen << ": hoy no hay automatico";
        context().setValue(QLatin1String(kSkippedDayKey), now.date().toString(Qt::ISODate));
        return;
    case DesktopSnapshots::AutomaticStep::Update:
        for (DesktopSnapshot &snapshot : m_snapshots) {
            if (snapshot.id == sameId) {
                snapshot.when = now;
                persist(snapshot);
                qInfo() << "[desktopIcons] igual al automatico anterior: solo se actualiza su fecha";
            }
        }
        break;
    case DesktopSnapshots::AutomaticStep::Save:
        m_snapshots.append(makeSnapshot(SnapshotKind::Automatic, QString(), current));
        persist(m_snapshots.last());
        prune();
        qInfo() << "[desktopIcons] automatico guardado:" << current.icons.size() << "iconos";
        break;
    }
    announce();
}

void DesktopIconsModule::loadFromDisk()
{
    m_snapshots.clear();
    if (m_folder.isEmpty()) {
        return;
    }
    const QFileInfoList files = QDir(m_folder).entryInfoList({QStringLiteral("*.json")}, QDir::Files);
    for (const QFileInfo &info : files) {
        QFile file(info.absoluteFilePath());
        DesktopSnapshot snapshot;
        snapshot.id = info.completeBaseName();
        QString error;
        // Un archivo roto queda donde esta (no se borra nada del usuario) y no se muestra.
        if (info.size() > kMaxFileBytes || !file.open(QIODevice::ReadOnly)
            || !DesktopSnapshots::fromJson(file.readAll(), &snapshot, &error)) {
            qWarning() << "[desktopIcons] snapshot ilegible, se ignora:" << info.fileName() << error;
            continue;
        }
        m_snapshots.append(snapshot);
    }
}

void DesktopIconsModule::persist(const DesktopSnapshot &snapshot)
{
    if (m_folder.isEmpty() || AutomatedRun::active()) {
        return;
    }
    QDir().mkpath(m_folder);
    QSaveFile file(QDir(m_folder).filePath(snapshot.id + QStringLiteral(".json")));
    if (!file.open(QIODevice::WriteOnly) || file.write(DesktopSnapshots::toJson(snapshot)) < 0 || !file.commit()) {
        qWarning() << "[desktopIcons] no se pudo guardar el snapshot" << snapshot.id << file.errorString();
    }
}

void DesktopIconsModule::erase(const QString &id)
{
    if (m_folder.isEmpty() || AutomatedRun::active() || id.isEmpty()) {
        return;
    }
    QFile::remove(QDir(m_folder).filePath(id + QStringLiteral(".json")));
}

void DesktopIconsModule::prune()
{
    const QStringList ids = DesktopSnapshots::toPrune(m_snapshots);
    for (const QString &id : ids) {
        erase(id);
        m_snapshots.erase(std::remove_if(m_snapshots.begin(), m_snapshots.end(),
                                         [&id](const DesktopSnapshot &s) { return s.id == id; }),
                          m_snapshots.end());
    }
}

DesktopSnapshot DesktopIconsModule::makeSnapshot(SnapshotKind kind, const QString &name, const DesktopIconView::Layout &layout) const
{
    DesktopSnapshot snapshot;
    snapshot.kind = kind;
    snapshot.name = name.trimmed();
    snapshot.when = QDateTime::currentDateTime();
    snapshot.layout = layout;
    const QString base = QStringLiteral("%1-%2").arg(snapshot.when.toString(QStringLiteral("yyyyMMdd-HHmmss")), idPrefix(kind));
    snapshot.id = base;
    for (int n = 2;; ++n) {
        const bool taken = std::any_of(m_snapshots.cbegin(), m_snapshots.cend(),
                                       [&snapshot](const DesktopSnapshot &s) { return s.id == snapshot.id; });
        if (!taken) break;
        snapshot.id = QStringLiteral("%1-%2").arg(base).arg(n);
    }
    return snapshot;
}

void DesktopIconsModule::announce()
{
    emit snapshotsChanged();
    emit statusChanged();
}

QList<DesktopSnapshot> DesktopIconsModule::snapshots() const
{
    QList<DesktopSnapshot> list = m_snapshots;
    std::stable_sort(list.begin(), list.end(), [](const DesktopSnapshot &a, const DesktopSnapshot &b) { return a.when > b.when; });
    return list;
}

QSize DesktopIconsModule::currentScreen() const
{
    return context().captureMode() ? m_captureScreen : DesktopIconView::screenSize();
}

bool DesktopIconsModule::dailyEnabled() const
{
    return context().value(QLatin1String(kDailyKey), true).toBool();
}

void DesktopIconsModule::setDailyEnabled(bool enabled)
{
    context().setValue(QLatin1String(kDailyKey), enabled);
    startDailyTimer();
    announce();
}

bool DesktopIconsModule::saveManual(const QString &name)
{
    DesktopIconView::Layout current;
    QString error;
    m_result = Result();
    if (!DesktopIconView::read(&current, &error) || current.icons.isEmpty()) {
        m_result.visible = true;
        m_result.error = error.isEmpty() ? I18n::tr("No desktop icons were found.") : I18n::tr("Could not read the desktop icons.");
        announce();
        return false;
    }
    m_snapshots.append(makeSnapshot(SnapshotKind::Manual, name, current));
    persist(m_snapshots.last());
    announce();
    return true;
}

bool DesktopIconsModule::restore(const QString &id)
{
    DesktopSnapshot target;
    for (const DesktopSnapshot &snapshot : std::as_const(m_snapshots)) {
        if (snapshot.id == id) target = snapshot;
    }
    if (target.id.isEmpty()) {
        return false;
    }
    m_result = Result();
    m_result.visible = true;
    DesktopIconView::Layout current;
    QString error;
    if (!DesktopIconView::read(&current, &error)) {
        m_result.error = I18n::tr("Could not read the desktop icons.");
        announce();
        return false;
    }
    // Primero el "antes de restaurar" (deshacer); el anterior, aunque sea el que se esta restaurando,
    // ya esta copiado en `target`.
    const DesktopSnapshot before = makeSnapshot(SnapshotKind::BeforeRestore, QString(), current);
    m_snapshots.append(before);
    persist(before);
    prune();
    const DesktopSnapshots::RestorePlan restorePlan = DesktopSnapshots::plan(target.layout, current);
    DesktopIconView::ApplyResult applied;
    if (!DesktopIconView::apply(target.layout.autoArrange, target.layout.snapToGrid, restorePlan.moves,
                                context().automatedRun(), &applied, &error)) {
        m_result.error = I18n::tr("Could not move the desktop icons.");
        m_result.undoId = before.id;
        announce();
        return false;
    }
    m_result.ok = true;
    m_result.restored = applied.moved;
    m_result.missing = restorePlan.missing;
    m_result.undoId = before.id;
    announce();
    return true;
}

bool DesktopIconsModule::remove(const QString &id)
{
    const auto it = std::find_if(m_snapshots.begin(), m_snapshots.end(), [&id](const DesktopSnapshot &s) { return s.id == id; });
    if (it == m_snapshots.end()) {
        return false;
    }
    m_snapshots.erase(it);
    erase(id);
    if (m_result.undoId == id) {
        m_result.undoId.clear();
    }
    announce();
    return true;
}

ModuleStatus DesktopIconsModule::status() const
{
    // Corto: la linea de estado de la barra lateral mide 125 px (sin la hora desde ayer).
    const QDate today = QDate::currentDate();
    for (const DesktopSnapshot &snapshot : snapshots()) {
        if (snapshot.kind == SnapshotKind::BeforeRestore) continue;
        const qint64 days = snapshot.when.date().daysTo(today);
        return {ModuleTone::Active,
                days == 0   ? I18n::tr("Saved today %1").arg(snapshot.when.toString(QStringLiteral("HH:mm")))
                : days == 1 ? I18n::tr("Saved yesterday")
                            : I18n::tr("Saved %1").arg(snapshot.when.toString(QStringLiteral("yyyy-MM-dd")))};
    }
    return {ModuleTone::Active, I18n::tr("No snapshots yet")};
}

QWidget *DesktopIconsModule::createPanel(QWidget *parent)
{
    auto *panel = new DesktopIconsPanel(this, !context().captureMode(), parent);
    connect(this, &DesktopIconsModule::snapshotsChanged, panel, &DesktopIconsPanel::refresh);
    if (m_captureState == QLatin1String("confirm-delete")) panel->armFirstDeleteForCapture();
    return panel;
}

QStringList DesktopIconsModule::captureStates() const
{
    return {QStringLiteral("list"), QStringLiteral("empty"), QStringLiteral("restored"), QStringLiteral("resolution"),
            QStringLiteral("long-name"), QStringLiteral("confirm-delete"), QStringLiteral("daily-off"), QStringLiteral("error")};
}

bool DesktopIconsModule::applyCaptureState(const QString &state)
{
    if (!context().captureMode() || !captureStates().contains(state)) {
        return false;
    }
    m_captureState = state;
    m_captureScreen = state == QLatin1String("resolution") ? QSize(1024, 768) : QSize(1920, 1080);
    m_result = Result();
    m_snapshots.clear();
    context().setValue(QLatin1String(kDailyKey), state != QLatin1String("daily-off"));
    if (state == QLatin1String("empty")) {
        return true;
    }
    const QDate today = QDate::currentDate();
    m_snapshots = {
        fixture(QStringLiteral("a1"), SnapshotKind::Automatic, QDateTime(today, QTime(10, 42)), 112),
        fixture(QStringLiteral("m1"), SnapshotKind::Manual, QDateTime(today.addDays(-1), QTime(18, 5)), 108,
                state == QLatin1String("long-name")
                    ? QStringLiteral("Clean desktop before reinstalling the graphics driver and the second monitor")
                    : QStringLiteral("Clean desktop")),
        fixture(QStringLiteral("a2"), SnapshotKind::Automatic, QDateTime(today.addDays(-1), QTime(9, 51)), 111, {}, QSize(1920, 1080), 1),
        fixture(QStringLiteral("b1"), SnapshotKind::BeforeRestore, QDateTime(today.addDays(-3), QTime(21, 14)), 97, {},
                QSize(1024, 768)),
        fixture(QStringLiteral("a3"), SnapshotKind::Automatic, QDateTime(today.addDays(-3), QTime(9, 37)), 110),
    };
    if (state == QLatin1String("restored")) {
        m_result.visible = true;
        m_result.ok = true;
        m_result.restored = 86;
        for (const char *name : {"Blender 3.6", "Photoshop 2023", "DaVinci Resolve 18", "Steam", "Unreal Engine 5.2"}) {
            m_result.missing.append(QString::fromLatin1(name));
        }
        for (int i = 0; i < 21; ++i) m_result.missing.append(QStringLiteral("Tool %1").arg(i + 1));
        m_result.undoId = QStringLiteral("b1");
    } else if (state == QLatin1String("error")) {
        m_result.visible = true;
        m_result.error = I18n::tr("Could not read the desktop icons.");
    }
    return true;
}

ModuleDescriptor desktopIconsDescriptor()
{
    ModuleDescriptor d;
    d.id = QStringLiteral("desktopIcons");
    d.title = QStringLiteral("Desktop Icons");
    d.description = I18n::tr("Save and restore where your desktop icons are.");
    d.offBullets = {I18n::tr("A snapshot a day after an hour awake, keeping the last 5"),
                    I18n::tr("Save your own snapshots, with an optional name"),
                    I18n::tr("Restore any snapshot, with undo")};
    d.platforms = PlatformWindows;
    d.paintIcon = &paintDesktopIcon;
    d.create = [](ModuleContext &context) -> std::unique_ptr<Module> {
        return std::make_unique<DesktopIconsModule>(context);
    };
    d.selfTest = &selfTest;
    d.simulateAction = &simulate;
    return d;
}

HelpSection desktopIconsHelp(const SettingsReader &)
{
    HelpSection help;
    help.title = QStringLiteral("Desktop Icons");
    help.steps = {I18n::tr("Keep %1 on to have the last 5 days, or save a snapshot yourself with a name.")
                      .arg(HelpSection::strong(I18n::tr("Daily snapshot"))),
                  I18n::tr("When Windows mixes up your icons, pick a snapshot and click %1.")
                      .arg(HelpSection::strong(I18n::tr("Restore"))),
                  I18n::tr("%1 puts the icons back where they were before restoring.").arg(HelpSection::strong(I18n::tr("Undo")))};
    help.note = I18n::tr("Icons are matched by their file, not their name; the ones that no longer exist are skipped. "
                         "The daily snapshot waits until the computer has been awake for an hour and skips days with an unusual screen resolution.");
    return help;
}
