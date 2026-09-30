#include "modules/diskspace/DiskState.h"

#include "app/ModuleContext.h"

#include <QDebug>
#include <QStringList>

#include <algorithm>

namespace {

// Claves de la seccion [diskSpace]. La lista va con el formato de array de QSettings
// (watched/size, watched/1/root...), la misma forma que escribia Nuke Shortcuts.
const QString kRemindMinutes = QStringLiteral("remindMinutes");
// El intervalo del chequeo era configurable hasta v0.08; hoy es fijo y la clave se borra al cargar.
const QString kObsoleteCheckMinutes = QStringLiteral("checkMinutes");
const QString kDiskWatches = QStringLiteral("watched");

QString watchKey(int index, const char *field)
{
    return QStringLiteral("%1/%2/%3").arg(kDiskWatches).arg(index + 1).arg(QLatin1String(field));
}

} // namespace

DiskState::DiskState(ModuleContext *context, QObject *parent)
    : QObject(parent)
    , m_context(context)
{
    if (!m_context) {
        return;
    }
    const int minutes = m_context->value(kRemindMinutes, DiskSpace::kDefaultRemindMinutes).toInt();
    m_remindMinutes = DiskSpace::isValidRemind(minutes) ? minutes : DiskSpace::kDefaultRemindMinutes;
    if (m_context->value(kObsoleteCheckMinutes).isValid()) {
        m_context->removeValue(kObsoleteCheckMinutes);
    }
    // Una entrada ilegible (editada a mano) se descarta sola; las demas se conservan.
    const int count = m_context->value(kDiskWatches + QStringLiteral("/size"), 0).toInt();
    for (int i = 0; i < count; ++i) {
        DiskWatch watch;
        watch.root = m_context->value(watchKey(i, "root")).toString();
        bool okValue = false;
        watch.value = m_context->value(watchKey(i, "value")).toInt(&okValue);
        const bool okUnit = DiskSpace::unitFromString(m_context->value(watchKey(i, "unit")).toString(), &watch.unit);
        watch.name = m_context->value(watchKey(i, "name")).toString();
        if (watch.root.isEmpty() || !okValue || !okUnit || isWatched(watch.root)) {
            qWarning() << "[DiskState] Disco vigilado ilegible en el .ini, se descarta: indice" << i;
            continue;
        }
        watch.value = DiskSpace::clampValue(watch.value, watch.unit);
        m_diskWatches.append(watch);
        DiskSpace::AlertState alert;
        alert.wasLow = m_context->value(watchKey(i, "wasLow"), false).toBool();
        alert.lastNotified = QDateTime::fromString(m_context->value(watchKey(i, "lastNotified")).toString(), Qt::ISODate);
        alert.snoozedUntil = QDateTime::fromString(m_context->value(watchKey(i, "snoozedUntil")).toString(), Qt::ISODate);
        if (alert.wasLow || alert.lastNotified.isValid() || alert.snoozedUntil.isValid()) {
            m_alerts.insert(watch.root, alert);
        }
    }
    qInfo() << "[DiskState] Cargado:" << m_diskWatches.size() << "discos vigilados, recordatorio cada" << m_remindMinutes
            << "min";
}

bool DiskState::isWatched(const QString &root) const
{
    for (const DiskWatch &watch : m_diskWatches) {
        if (watch.root == root) {
            return true;
        }
    }
    return false;
}

DiskSpace::AlertState DiskState::alertState(const QString &root) const
{
    return m_alerts.value(root);
}

void DiskState::setAlertState(const QString &root, const DiskSpace::AlertState &alert)
{
    if (!isWatched(root)) {
        return;
    }
    const DiskSpace::AlertState previous = m_alerts.value(root);
    if (previous.wasLow == alert.wasLow && previous.lastNotified == alert.lastNotified
        && previous.snoozedUntil == alert.snoozedUntil) {
        return;
    }
    m_alerts.insert(root, alert);
    // Se escribe solo cuando cambia (al cruzar el umbral, al avisar o al posponer), no en cada chequeo.
    writeDiskWatches();
}

void DiskState::snooze(const QString &root, int minutes, const QDateTime &now)
{
    if (!isWatched(root) || !DiskSpace::isValidRemind(minutes)) {
        qWarning() << "[DiskState] Posponer ignorado:" << root << minutes << "min";
        return;
    }
    DiskSpace::AlertState alert = m_alerts.value(root);
    alert.snoozedUntil = now.addSecs(qint64(minutes) * 60);
    setAlertState(root, alert);
    qInfo() << "[DiskState] Proximo recordatorio de" << root << "en" << minutes << "min";
}

bool DiskState::driveReading(const QString &root, DriveInfo *drive) const
{
    const auto it = m_drives.constFind(root);
    if (it == m_drives.constEnd()) {
        return false;
    }
    if (drive) {
        *drive = it.value();
    }
    return true;
}

QList<DriveInfo> DiskState::drives() const
{
    QList<DriveInfo> list = m_drives.values();
    std::sort(list.begin(), list.end(), [](const DriveInfo &a, const DriveInfo &b) { return a.root < b.root; });
    return list;
}

QList<DiskWatch> DiskState::lowWatches() const
{
    QList<DiskWatch> low;
    for (const DiskWatch &watch : m_diskWatches) {
        DriveInfo drive;
        if (driveReading(watch.root, &drive) && DiskSpace::isLow(watch, drive)) {
            low.append(watch);
        }
    }
    return low;
}

void DiskState::writeDiskWatches()
{
    if (!m_context) {
        return;
    }
    // El array se reescribe entero: sin el remove, un disco quitado dejaria su indice viejo.
    m_context->removeValue(kDiskWatches);
    for (int i = 0; i < m_diskWatches.size(); ++i) {
        const DiskWatch &watch = m_diskWatches.at(i);
        m_context->setValue(watchKey(i, "root"), watch.root);
        m_context->setValue(watchKey(i, "value"), watch.value);
        m_context->setValue(watchKey(i, "unit"), DiskSpace::unitToString(watch.unit));
        m_context->setValue(watchKey(i, "name"), watch.name);
        const auto alert = m_alerts.constFind(watch.root);
        if (alert != m_alerts.constEnd()) {
            m_context->setValue(watchKey(i, "wasLow"), alert->wasLow);
            if (alert->lastNotified.isValid()) {
                m_context->setValue(watchKey(i, "lastNotified"), alert->lastNotified.toString(Qt::ISODate));
            }
            if (alert->snoozedUntil.isValid()) {
                m_context->setValue(watchKey(i, "snoozedUntil"), alert->snoozedUntil.toString(Qt::ISODate));
            }
        }
    }
    m_context->setValue(kDiskWatches + QStringLiteral("/size"), int(m_diskWatches.size()));
}

void DiskState::setRemindMinutes(int minutes)
{
    if (!DiskSpace::isValidRemind(minutes) || m_remindMinutes == minutes) {
        return;
    }
    m_remindMinutes = minutes;
    if (m_context) {
        m_context->setValue(kRemindMinutes, minutes);
    }
    qInfo() << "[DiskState] Recordatorio de discos cada" << minutes << "min";
    emit changed();
}

void DiskState::addDiskWatch(const QString &root, const QString &name)
{
    if (root.isEmpty() || isWatched(root)) {
        return;
    }
    DiskWatch watch;
    watch.root = root;
    watch.name = name;
    if (!m_diskWatches.isEmpty()) {
        watch.unit = m_diskWatches.constLast().unit;
        watch.value = m_diskWatches.constLast().value;
    } else {
        watch.unit = DiskWatch::Unit::GB;
        watch.value = DiskSpace::kDefaultGb;
    }
    m_diskWatches.append(watch);
    writeDiskWatches();
    qInfo() << "[DiskState] Disco vigilado:" << root << "umbral" << DiskSpace::thresholdText(watch);
    emit changed();
}

void DiskState::removeDiskWatch(const QString &root)
{
    for (int i = 0; i < m_diskWatches.size(); ++i) {
        if (m_diskWatches.at(i).root == root) {
            m_diskWatches.removeAt(i);
            // Un disco que se deja de vigilar olvida su historial: si se vuelve a agregar, avisa de nuevo.
            m_alerts.remove(root);
            writeDiskWatches();
            qInfo() << "[DiskState] Disco sin vigilar:" << root;
            emit changed();
            return;
        }
    }
}

void DiskState::setDiskThreshold(const QString &root, int value, DiskWatch::Unit unit)
{
    for (DiskWatch &watch : m_diskWatches) {
        if (watch.root != root) {
            continue;
        }
        const int clamped = DiskSpace::clampValue(value, unit);
        if (watch.value == clamped && watch.unit == unit) {
            return;
        }
        watch.value = clamped;
        watch.unit = unit;
        writeDiskWatches();
        qInfo() << "[DiskState] Umbral de" << root << ":" << DiskSpace::thresholdText(watch);
        emit changed();
        return;
    }
}

void DiskState::setDriveReadings(const QList<DriveInfo> &readings, const QStringList &queried, bool listedAll,
                                 const QDateTime &checkedAt)
{
    if (listedAll) {
        m_drives.clear();
    } else {
        for (const QString &root : queried) {
            m_drives.remove(root);
        }
    }
    for (const DriveInfo &drive : readings) {
        m_drives.insert(drive.root, drive);
    }
    // El nombre guardado es el que se muestra con el disco desenchufado: se mantiene al dia.
    bool namesChanged = false;
    for (DiskWatch &watch : m_diskWatches) {
        const auto it = m_drives.constFind(watch.root);
        if (it != m_drives.constEnd() && !it->name.isEmpty() && it->name != watch.name) {
            watch.name = it->name;
            namesChanged = true;
        }
    }
    if (namesChanged) {
        writeDiskWatches();
    }
    m_lastDiskCheck = checkedAt;
    emit changed();
}
