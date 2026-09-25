#include "modules/diskspace/DiskMonitor.h"
#include "modules/diskspace/DiskState.h"

#include <QDebug>
#include <QSet>
#include <QStringList>
#include <QTimer>

DiskMonitor::DiskMonitor(DiskState *state, Sources sources, QObject *parent)
    : QObject(parent)
    , m_state(state)
    , m_sources(std::move(sources))
{
    m_timer = new QTimer(this);
    // Preciso y no grueso (el tipo por defecto): en Qt 6.5 sobre Windows, un timer grueso de
    // intervalo largo (el de 15 min) deja un objeto USER del sistema cada vez que se registra y se
    // suelta. Medido con 20 ciclos de prender y apagar Disk Space: +20 USER con el grueso, +0 con
    // este. El costo de un timer preciso cada 15 min es nulo.
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, [this]() { checkNow(true); });
    connect(m_state, &DiskState::changed, this, &DiskMonitor::onStateChanged);
}

QDateTime DiskMonitor::now() const
{
    return m_sources.now ? m_sources.now() : QDateTime::currentDateTime();
}

void DiskMonitor::start(int firstCheckDelayMs)
{
    m_timerMinutes = m_state->diskCheckMinutes();
    m_timer->start(m_timerMinutes * 60 * 1000);
    checkNow(false);
    // Un QTimer hijo del monitor y no QTimer::singleShot: muere con el monitor al apagar Disk Space,
    // aunque todavia no haya vencido. Preciso por lo mismo que m_timer.
    auto *first = new QTimer(this);
    first->setSingleShot(true);
    first->setTimerType(Qt::PreciseTimer);
    connect(first, &QTimer::timeout, this, [this, first]() {
        first->deleteLater();
        checkNow(true);
    });
    first->start(firstCheckDelayMs);
}

void DiskMonitor::onStateChanged()
{
    // Otro intervalo: el timer arranca de cero con el nuevo (setInterval lo reinicia si esta activo).
    if (m_timer->isActive() && m_state->diskCheckMinutes() != m_timerMinutes) {
        m_timerMinutes = m_state->diskCheckMinutes();
        m_timer->setInterval(m_timerMinutes * 60 * 1000);
    }
    // Un disco que se dejo de vigilar olvida su historial: si se vuelve a agregar, avisa de nuevo.
    QSet<QString> watched;
    for (const DiskWatch &watch : m_state->diskWatches()) {
        watched.insert(watch.root);
    }
    for (auto it = m_alerts.begin(); it != m_alerts.end();) {
        it = watched.contains(it.key()) ? std::next(it) : m_alerts.erase(it);
    }
}

void DiskMonitor::refreshAll()
{
    if (!m_sources.listAll) {
        return;
    }
    m_state->setDriveReadings(m_sources.listAll(), QStringList(), true, now());
}

void DiskMonitor::checkNow(bool mayNotify)
{
    const QList<DiskWatch> watches = m_state->diskWatches();
    const QDateTime current = now();
    QList<DriveInfo> readings;
    QStringList queried;
    for (const DiskWatch &watch : watches) {
        queried.append(watch.root);
        DriveInfo drive;
        if (m_sources.query && m_sources.query(watch.root, &drive)) {
            readings.append(drive);
        }
    }
    // Primero la lectura (lo que ve la tarjeta), despues el aviso: el click en la notificacion abre
    // Settings y tiene que mostrar los mismos numeros.
    m_state->setDriveReadings(readings, queried, false, current);
    if (!mayNotify) {
        return;
    }
    for (const DiskWatch &watch : watches) {
        DriveInfo drive;
        if (!m_state->driveReading(watch.root, &drive)) {
            continue; // desenchufado: no cuenta como bajo y conserva su historial
        }
        const bool low = DiskSpace::isLow(watch, drive);
        DiskSpace::AlertState &alert = m_alerts[watch.root];
        if (DiskSpace::shouldNotify(low, alert, current)) {
            qInfo() << "[DiskMonitor] Aviso:" << watch.root << DiskSpace::formatBytes(drive.freeBytes) << "libres, umbral"
                    << DiskSpace::thresholdText(watch);
            alert.lastNotified = current;
            emit lowSpace(drive, watch);
        }
        alert.wasLow = low;
    }
}
