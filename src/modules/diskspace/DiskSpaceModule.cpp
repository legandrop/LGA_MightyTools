#include "modules/diskspace/DiskSpaceModule.h"

#include "app/ModuleContext.h"
#include "app/ModuleContextImpl.h"
#include "app/ModuleHost.h"
#include "app/SettingsStore.h"
#include "modules/diskspace/DiskCard.h"
#include "modules/diskspace/DiskMonitor.h"
#include "modules/diskspace/DiskState.h"
#include "platform/LocalDrives.h"
#include "ui/Theme.h"

#include <QAction>
#include <QDebug>
#include <QEvent>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

namespace {

const QString kId = QStringLiteral("diskSpace");

// Primer chequeo que puede avisar: un rato despues de prender, para no sumar una notificacion al
// inicio de la sesion.
constexpr int kFirstDiskCheckMs = 20000;
constexpr int kDiskNotificationMs = 10000;

// El panel: la tarjeta de discos. Cada vez que se ve (se elige la herramienta o se abre la ventana
// con ella elegida) pide numeros al dia, como la ventana de Nuke Shortcuts al abrirse.
class DiskPanel : public QWidget
{
public:
    DiskPanel(DiskState *state, bool interactive, std::function<void()> onShown, QWidget *parent)
        : QWidget(parent)
        , m_onShown(std::move(onShown))
    {
        setObjectName(QStringLiteral("diskSpacePanel"));
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        card = new DiskCard(state, interactive, this);
        layout->addWidget(card);
    }

    DiskCard *card = nullptr;

protected:
    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::Show && m_onShown) {
            m_onShown();
        }
        return QWidget::event(event);
    }

private:
    std::function<void()> m_onShown;
};

// Icono de la herramienta (canvas `I.disk`): el disco con la ranura y la luz.
void paintDiskIcon(QPainter &painter, const QRectF &rect, const QColor &color)
{
    const qreal scale = qMin(rect.width(), rect.height()) / 16.0;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.translate(rect.center().x() - 8 * scale, rect.center().y() - 8 * scale);
    painter.scale(scale, scale);
    painter.setPen(QPen(color, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(1.8, 4, 12.4, 8), 1.6, 1.6);
    painter.drawLine(QPointF(4.5, 9.5), QPointF(8.5, 9.5));
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawEllipse(QPointF(11.3, 9.5), 0.9, 0.9);
    painter.restore();
}

// Discos de prueba en GiB: los mismos numeros que el diseno aprobado.
DriveInfo fixtureDrive(const char *root, const char *label, const char *name, double totalGb, double freeGb)
{
    constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;
    DriveInfo drive;
    drive.root = QString::fromLatin1(root);
    drive.label = QString::fromLatin1(label);
    drive.name = QString::fromLatin1(name);
    drive.totalBytes = qint64(totalGb * kGiB);
    drive.freeBytes = qint64(freeGb * kGiB);
    return drive;
}

// Carga en el estado los discos del estado de captura pedido. No lee ningun disco real.
void applyFixture(DiskState &state, const QString &name)
{
    const bool low = name == QLatin1String("low") || name == QLatin1String("low2");
    QList<DriveInfo> drives = {
        fixtureDrive("C:/", "C:", "Windows", 931, 182),
        fixtureDrive("D:/", "D:", "Cache", 1863, low ? 42 : 640),
        fixtureDrive("E:/", "E:", "Renders", 3726, name == QLatin1String("low2") ? 298 : 1208),
        fixtureDrive("F:/", "F:", "Backup", 7452, 3103),
    };
    if (name == QLatin1String("missing")) {
        drives.removeAt(2); // E: desenchufado
    }
    if (name == QLatin1String("empty")) {
        return;
    }
    state.addDiskWatch(QStringLiteral("C:/"), QStringLiteral("Windows"));
    state.setDiskThreshold(QStringLiteral("C:/"), 50, DiskWatch::Unit::GB);
    state.addDiskWatch(QStringLiteral("D:/"), QStringLiteral("Cache"));
    state.setDiskThreshold(QStringLiteral("D:/"), 100, DiskWatch::Unit::GB);
    if (name != QLatin1String("good") && name != QLatin1String("add-menu") && name != QLatin1String("interval-menu")) {
        state.addDiskWatch(QStringLiteral("E:/"), QStringLiteral("Renders"));
        state.setDiskThreshold(QStringLiteral("E:/"), 15, DiskWatch::Unit::Percent);
    }
    state.setDriveReadings(drives, QStringList(), true, QDateTime(QDate(2026, 9, 24), QTime(12, 41)));
}

QIcon warnDotIcon()
{
    // Punto ambar: el QSS no puede tenir el texto de UNA accion.
    QPixmap dot(16, 16);
    dot.fill(Qt::transparent);
    QPainter painter(&dot);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Theme::color(Theme::kWarn));
    painter.drawEllipse(QRectF(4, 4, 8, 8));
    painter.end();
    return QIcon(dot);
}

QString lowLine(const DiskState &state, const DiskWatch &watch)
{
    DriveInfo drive;
    state.driveReading(watch.root, &drive);
    return QStringLiteral("%1 is low · %2 free").arg(drive.label, DiskSpace::formatBytes(drive.freeBytes));
}

// ---------------------------------------------------------------- self-test

void selfTest(const std::function<void(bool, const QString &)> &check)
{
    // ---- Reglas puras: umbral, formato y cuando avisar.
    constexpr qint64 kGiB = qint64(1024) * 1024 * 1024;
    DriveInfo cache;
    cache.root = QStringLiteral("D:/");
    cache.label = QStringLiteral("D:");
    cache.totalBytes = 2000 * kGiB;
    cache.freeBytes = 99 * kGiB;
    DiskWatch gb{QStringLiteral("D:/"), 100, DiskWatch::Unit::GB, QString()};
    check(DiskSpace::isLow(gb, cache), QStringLiteral("99 GB libres con umbral 100 GB: bajo"));
    cache.freeBytes = 100 * kGiB;
    check(!DiskSpace::isLow(gb, cache), QStringLiteral("100 GB libres con umbral 100 GB: justo en el borde no esta bajo"));
    DiskWatch pct{QStringLiteral("D:/"), 5, DiskWatch::Unit::Percent, QString()};
    check(!DiskSpace::isLow(pct, cache), QStringLiteral("100 GB de 2000 (5%) con umbral 5%: justo en el borde no esta bajo"));
    cache.freeBytes = 99 * kGiB;
    check(DiskSpace::isLow(pct, cache), QStringLiteral("99 GB de 2000 con umbral 5%: bajo"));
    pct.value = 4;
    check(!DiskSpace::isLow(pct, cache), QStringLiteral("99 GB de 2000 con umbral 4%: no esta bajo"));
    DriveInfo unread;
    check(!DiskSpace::isLow(gb, unread), QStringLiteral("disco sin lectura: nunca bajo"));
    check(DiskSpace::clampValue(0, DiskWatch::Unit::GB) == 1 && DiskSpace::clampValue(150, DiskWatch::Unit::Percent) == 99,
          QStringLiteral("umbral acotado: 0 GB -> 1, 150% -> 99"));
    check(DiskSpace::formatBytes(182 * kGiB) == QLatin1String("182 GB"), QStringLiteral("formato: 182 GB"));
    check(DiskSpace::formatBytes(1863 * kGiB) == QLatin1String("1.82 TB"),
          QStringLiteral("formato: 1863 GiB -> '%1'").arg(DiskSpace::formatBytes(1863 * kGiB)));
    check(DiskSpace::formatBytes(2048 * kGiB) == QLatin1String("2 TB"), QStringLiteral("formato: 2 TB sin decimales de mas"));
    check(DiskSpace::formatBytes(kGiB / 2) == QLatin1String("512 MB"), QStringLiteral("formato: 512 MB"));
    check(DiskSpace::thresholdText(pct) == QLatin1String("4%"), QStringLiteral("texto del umbral en %"));
    DiskWatch::Unit unit = DiskWatch::Unit::GB;
    check(DiskSpace::unitFromString(QStringLiteral("%"), &unit) && unit == DiskWatch::Unit::Percent,
          QStringLiteral("unidad leida del .ini: %"));
    check(!DiskSpace::unitFromString(QStringLiteral("gb"), &unit), QStringLiteral("unidad invalida rechazada: gb en minuscula"));
    check(DiskSpace::isValidInterval(15) && !DiskSpace::isValidInterval(7), QStringLiteral("intervalo: 15 vale, 7 no"));
    check(DiskSpace::intervalText(360) == QLatin1String("6 hours") && DiskSpace::intervalText(60) == QLatin1String("1 hour"),
          QStringLiteral("texto del intervalo en horas"));

    const QDateTime t0(QDate(2026, 9, 24), QTime(12, 0));
    DiskSpace::AlertState alert;
    check(DiskSpace::shouldNotify(true, alert, t0), QStringLiteral("aviso: cruza el umbral"));
    check(!DiskSpace::shouldNotify(false, alert, t0), QStringLiteral("aviso: no bajo, no avisa"));
    alert.wasLow = true;
    alert.lastNotified = t0;
    check(!DiskSpace::shouldNotify(true, alert, t0.addSecs(5 * 3600)), QStringLiteral("aviso: sigue bajo a las 5 h, no repite"));
    check(DiskSpace::shouldNotify(true, alert, t0.addSecs(6 * 3600)), QStringLiteral("aviso: sigue bajo a las 6 h, repite"));
    alert.wasLow = false;
    check(DiskSpace::shouldNotify(true, alert, t0.addSecs(60)), QStringLiteral("aviso: subio y volvio a bajar, avisa al cruzar"));

    // ---- DiskMonitor entero con discos falsos y un reloj que se adelanta a mano.
    {
        DiskState state(nullptr);
        state.addDiskWatch(QStringLiteral("C:/"), QStringLiteral("Windows"));
        state.setDiskThreshold(QStringLiteral("C:/"), 10, DiskWatch::Unit::Percent);
        state.addDiskWatch(QStringLiteral("D:/"), QStringLiteral("Cache"));
        const QList<DiskWatch> watches = state.diskWatches();
        check(watches.size() == 2 && watches.at(1).unit == DiskWatch::Unit::Percent && watches.at(1).value == 10,
              QStringLiteral("un disco nuevo toma el umbral del ultimo (10%)"));
        state.addDiskWatch(QStringLiteral("D:/"), QStringLiteral("Cache"));
        check(state.diskWatches().size() == 2, QStringLiteral("el mismo disco no se agrega dos veces"));

        QHash<QString, DriveInfo> disks;
        DriveInfo c;
        c.root = QStringLiteral("C:/");
        c.label = QStringLiteral("C:");
        c.totalBytes = 1000 * kGiB;
        c.freeBytes = 500 * kGiB;
        disks.insert(c.root, c);
        DriveInfo d = c;
        d.root = QStringLiteral("D:/");
        d.label = QStringLiteral("D:");
        d.freeBytes = 50 * kGiB; // 5%: bajo con el 10%
        disks.insert(d.root, d);
        QDateTime clock = t0;
        DiskMonitor::Sources sources;
        sources.listAll = [&disks]() { return disks.values(); };
        sources.query = [&disks](const QString &root, DriveInfo *drive) {
            if (!disks.contains(root)) {
                return false;
            }
            *drive = disks.value(root);
            return true;
        };
        sources.now = [&clock]() { return clock; };
        DiskMonitor monitor(&state, sources);
        QStringList notified;
        QObject::connect(&monitor, &DiskMonitor::lowSpace,
                         [&notified](const DriveInfo &drive, const DiskWatch &) { notified.append(drive.root); });

        monitor.checkNow(false);
        check(notified.isEmpty() && state.lowWatches().size() == 1,
              QStringLiteral("monitor: la lectura sin aviso marca D: bajo y no notifica"));
        monitor.checkNow(true);
        check(notified == QStringList{QStringLiteral("D:/")}, QStringLiteral("monitor: primer chequeo avisa D: y no C:"));
        clock = clock.addSecs(15 * 60);
        monitor.checkNow(true);
        check(notified.size() == 1, QStringLiteral("monitor: 15 min despues, sigue bajo y no repite"));
        clock = clock.addSecs(6 * 3600);
        monitor.checkNow(true);
        check(notified.size() == 2, QStringLiteral("monitor: 6 h despues, repite"));
        disks.remove(QStringLiteral("D:/"));
        clock = clock.addSecs(6 * 3600);
        monitor.checkNow(true);
        check(notified.size() == 2 && state.lowWatches().isEmpty(),
              QStringLiteral("monitor: D: desenchufado no avisa ni cuenta como bajo"));
        d.freeBytes = 400 * kGiB;
        disks.insert(d.root, d);
        clock = clock.addSecs(60);
        monitor.checkNow(true);
        check(notified.size() == 2, QStringLiteral("monitor: D: vuelve con espacio, no avisa"));
        d.freeBytes = 20 * kGiB;
        disks.insert(d.root, d);
        clock = clock.addSecs(60);
        monitor.checkNow(true);
        check(notified.size() == 3, QStringLiteral("monitor: D: vuelve a bajar, avisa de nuevo al cruzar"));
        state.setDiskThreshold(QStringLiteral("D:/"), 1, DiskWatch::Unit::Percent);
        clock = clock.addSecs(7 * 3600);
        monitor.checkNow(true);
        check(notified.size() == 3, QStringLiteral("monitor: con el umbral en 1% (2% libre), D: ya no esta bajo"));
        state.removeDiskWatch(QStringLiteral("C:/"));
        check(state.diskWatches().size() == 1 && !state.isWatched(QStringLiteral("C:/")),
              QStringLiteral("dejar de vigilar C:"));
        monitor.refreshAll();
        check(state.drives().size() == 2, QStringLiteral("el listado completo trae tambien los discos sin vigilar"));
    }

    // ---- Persistencia en SU seccion [diskSpace], ida y vuelta, con entradas ilegibles descartadas.
    {
        MemorySettingsStore store;
        HostOptions options;
        options.automatedRun = true;
        ModuleHost host({}, &store, options);
        ModuleContextImpl context(&host, kId, QStringLiteral("Disk Space"), 1);
        {
            DiskState state(&context);
            state.addDiskWatch(QStringLiteral("C:/"), QStringLiteral("Windows"));
            state.addDiskWatch(QStringLiteral("D:/"), QStringLiteral("Cache"));
            state.setDiskThreshold(QStringLiteral("D:/"), 12, DiskWatch::Unit::Percent);
            state.setDiskCheckMinutes(60);
            state.removeDiskWatch(QStringLiteral("C:/"));
        }
        check(store.value(QStringLiteral("diskSpace/watched/size")).toInt() == 1
                  && store.value(QStringLiteral("diskSpace/watched/1/root")).toString() == QLatin1String("D:/")
                  && !store.value(QStringLiteral("diskSpace/watched/2/root")).isValid(),
              QStringLiteral("settings: la lista se reescribe entera en [diskSpace] (sin indices viejos)"));
        DiskState reloaded(&context);
        check(reloaded.diskWatches().size() == 1 && reloaded.diskWatches().first().unit == DiskWatch::Unit::Percent
                  && reloaded.diskWatches().first().value == 12 && reloaded.diskCheckMinutes() == 60,
              QStringLiteral("settings: ida y vuelta de discos, umbral y intervalo"));
        store.setValue(QStringLiteral("diskSpace/watched/size"), 2);
        store.setValue(QStringLiteral("diskSpace/watched/2/root"), QStringLiteral("E:/"));
        store.setValue(QStringLiteral("diskSpace/watched/2/unit"), QStringLiteral("gb"));
        store.setValue(QStringLiteral("diskSpace/checkMinutes"), 7);
        DiskState damaged(&context);
        check(damaged.diskWatches().size() == 1 && damaged.diskCheckMinutes() == DiskSpace::kDefaultIntervalMinutes,
              QStringLiteral("settings: una entrada ilegible y un intervalo invalido se descartan"));
    }

    // ---- El historial de avisos sobrevive a apagar y prender (se guarda con cada disco).
    {
        MemorySettingsStore store;
        HostOptions options;
        options.automatedRun = true;
        ModuleHost host({}, &store, options);
        ModuleContextImpl context(&host, kId, QStringLiteral("Disk Space"), 1);
        QHash<QString, DriveInfo> disks;
        DriveInfo d;
        d.root = QStringLiteral("D:/");
        d.label = QStringLiteral("D:");
        d.totalBytes = 1000 * kGiB;
        d.freeBytes = 10 * kGiB;
        disks.insert(d.root, d);
        QDateTime clock = t0;
        DiskMonitor::Sources sources;
        sources.query = [&disks](const QString &root, DriveInfo *drive) {
            if (!disks.contains(root)) {
                return false;
            }
            *drive = disks.value(root);
            return true;
        };
        sources.now = [&clock]() { return clock; };
        int notified = 0;
        {
            DiskState state(&context);
            state.addDiskWatch(QStringLiteral("D:/"), QStringLiteral("Cache"));
            DiskMonitor monitor(&state, sources);
            QObject::connect(&monitor, &DiskMonitor::lowSpace, [&notified](const DriveInfo &, const DiskWatch &) { ++notified; });
            monitor.checkNow(true);
        }
        check(notified == 1 && store.value(QStringLiteral("diskSpace/watched/1/lastNotified")).toString() == t0.toString(Qt::ISODate),
              QStringLiteral("historial: el aviso queda guardado con su disco"));
        clock = t0.addSecs(3600);
        {
            DiskState state(&context); // "prender de nuevo": estado nuevo desde la seccion
            DiskMonitor monitor(&state, sources);
            QObject::connect(&monitor, &DiskMonitor::lowSpace, [&notified](const DriveInfo &, const DiskWatch &) { ++notified; });
            monitor.checkNow(true);
            check(notified == 1, QStringLiteral("historial: apagar y prender no repite el aviso antes de las 6 h"));
            clock = t0.addSecs(6 * 3600);
            monitor.checkNow(true);
            check(notified == 2, QStringLiteral("historial: a las 6 h repite, como siempre"));
            state.removeDiskWatch(QStringLiteral("D:/"));
            state.addDiskWatch(QStringLiteral("D:/"), QStringLiteral("Cache"));
            clock = clock.addSecs(60);
            monitor.checkNow(true);
            check(notified == 3, QStringLiteral("historial: dejar de vigilar y volver a agregar avisa de nuevo"));
        }
    }
}

} // namespace

// ---------------------------------------------------------------- DiskSpaceModule

DiskSpaceModule::DiskSpaceModule(ModuleContext &context)
    : Module(context)
{
    m_state = new DiskState(context.captureMode() ? nullptr : &context, this);
    connect(m_state, &DiskState::changed, this, [this]() {
        if (m_card) {
            m_card->refresh();
        }
        emit statusChanged();
    });
}

DiskSpaceModule::~DiskSpaceModule()
{
    stop();
}

void DiskSpaceModule::start()
{
    if (m_monitor) {
        return;
    }
    DiskMonitor::Sources sources;
    sources.listAll = &LocalDrives::list;
    sources.query = &LocalDrives::query;
    m_monitor = new DiskMonitor(m_state, sources, this);
    connect(m_monitor, &DiskMonitor::lowSpace, this, &DiskSpaceModule::notifyLowSpace);
    m_monitor->start(kFirstDiskCheckMs);
}

void DiskSpaceModule::stop()
{
    // El timer y la lectura de discos mueren con el monitor.
    delete m_monitor;
    m_monitor = nullptr;
}

ModuleStatus DiskSpaceModule::status() const
{
    const QList<DiskWatch> watches = m_state->diskWatches();
    if (watches.isEmpty()) {
        return {ModuleTone::Paused, QStringLiteral("Nothing watched yet")};
    }
    const QList<DiskWatch> low = m_state->lowWatches();
    if (low.size() == 1) {
        DriveInfo drive;
        m_state->driveReading(low.first().root, &drive);
        return {ModuleTone::Attention, QStringLiteral("%1 is low").arg(drive.label)};
    }
    if (low.size() > 1) {
        return {ModuleTone::Attention, QStringLiteral("%1 drives are low").arg(low.size())};
    }
    return {ModuleTone::Active, QStringLiteral("All good")};
}

QWidget *DiskSpaceModule::createPanel(QWidget *parent)
{
    const bool interactive = !context().captureMode();
    auto *panel = new DiskPanel(
        m_state, interactive,
        [this]() {
            if (m_monitor) {
                m_monitor->checkNow(false);
            }
        },
        parent);
    m_card = panel->card;
    if (interactive) {
        connect(m_card, &DiskCard::drivesRefreshRequested, this, [this]() {
            if (m_monitor) {
                m_monitor->refreshAll();
            }
        });
    }
    m_panel = panel;
    return panel;
}

void DiskSpaceModule::fillTrayMenu(QMenu *menu)
{
    // Una linea por disco bajo; un click abre la ventana en Disk Space.
    const QIcon dot = warnDotIcon();
    for (const DiskWatch &watch : m_state->lowWatches()) {
        QAction *action = menu->addAction(dot, lowLine(*m_state, watch));
        connect(action, &QAction::triggered, this, [this]() { context().showPanel(); });
    }
}

QStringList DiskSpaceModule::trayTooltipLines() const
{
    QStringList lines;
    for (const DiskWatch &watch : m_state->lowWatches()) {
        DriveInfo drive;
        m_state->driveReading(watch.root, &drive);
        lines.append(QStringLiteral("%1 %2 free").arg(drive.label, DiskSpace::formatBytes(drive.freeBytes)));
    }
    return lines;
}

void DiskSpaceModule::notifyLowSpace(const DriveInfo &drive, const DiskWatch &watch)
{
    context().notify(QStringLiteral("%1 is running low").arg(drive.label),
                     QStringLiteral("%1 free of %2. You asked to be warned under %3.")
                         .arg(DiskSpace::formatBytes(drive.freeBytes), DiskSpace::formatBytes(drive.totalBytes),
                              DiskSpace::thresholdText(watch)),
                     ModuleContext::NoticeIcon::Warning, kDiskNotificationMs);
}

QStringList DiskSpaceModule::captureStates() const
{
    // Canvas, secciones 2, 3 y 5: estado normal, uno y dos discos bajos, uno desenchufado, sin
    // discos, y los dos menus de la tarjeta.
    return {QStringLiteral("good"),  QStringLiteral("low"),      QStringLiteral("low2"),         QStringLiteral("missing"),
            QStringLiteral("empty"), QStringLiteral("add-menu"), QStringLiteral("interval-menu")};
}

bool DiskSpaceModule::applyCaptureState(const QString &state)
{
    if (!context().captureMode() || !captureStates().contains(state)) {
        return false;
    }
    // Estado limpio: un estado anterior no se mezcla con el nuevo.
    for (const DiskWatch &watch : m_state->diskWatches()) {
        m_state->removeDiskWatch(watch.root);
    }
    applyFixture(*m_state, state);
    return true;
}

QWidget *DiskSpaceModule::createCaptureWidget(const QString &state, QWidget *parent)
{
    if (state != QLatin1String("add-menu") && state != QLatin1String("interval-menu")) {
        return nullptr;
    }
    // Los menus de la tarjeta, armados por la misma tarjeta que los abre en la app.
    auto *card = new DiskCard(m_state, false, parent);
    card->hide();
    auto *menu = new QMenu(parent);
    if (state == QLatin1String("add-menu")) {
        card->fillAddMenu(menu);
        if (menu->actions().size() > 1) {
            menu->setActiveAction(menu->actions().at(1));
        }
    } else {
        card->fillIntervalMenu(menu);
    }
    return menu;
}

ModuleDescriptor diskSpaceDescriptor()
{
    ModuleDescriptor d;
    d.id = kId;
    d.title = QStringLiteral("Disk Space");
    d.description = QStringLiteral("Watches your local drives and warns you when one runs low.");
    d.offBullets = {QStringLiteral("Checks only the drives you pick, every 15 min by default"),
                    QStringLiteral("Warns with a notification and a line in the tray menu"),
                    QStringLiteral("Each drive has its own limit, in GB or %")};
    d.platforms = PlatformWindows | PlatformMac;
    d.paintIcon = &paintDiskIcon;
    d.create = [](ModuleContext &context) -> std::unique_ptr<Module> { return std::make_unique<DiskSpaceModule>(context); };
    d.selfTest = &selfTest;
    return d;
}

HelpSection diskSpaceHelp(const SettingsReader &)
{
    HelpSection section;
    section.title = QStringLiteral("Disk Space");
    section.steps = {QStringLiteral("Add the drives to watch and set when each one should warn you, in %1 or %2.")
                         .arg(HelpSection::strong(QStringLiteral("GB")), HelpSection::strong(QStringLiteral("%")))};
    section.note = QStringLiteral("You get a notification when a drive goes under its limit, again every 6 hours while it stays low.");
    return section;
}
