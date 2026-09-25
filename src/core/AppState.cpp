#include "core/AppState.h"
#include "core/AppSettings.h"

#include <QDebug>
#include <QStringList>

#include <algorithm>
#include <QVariant>

namespace {

const QString kEnabled = QStringLiteral("enabled");
const QString kCheckUpdates = QStringLiteral("checkUpdatesAtStartup");
const QString kAddKeyframe = QStringLiteral("shortcuts/addKeyframe");
const QString kFrameDopeSheet = QStringLiteral("shortcuts/frameDopeSheet");
const QString kSpotX = QStringLiteral("dopeSheet/x");
const QString kSpotY = QStringLiteral("dopeSheet/y");
const QString kDiskMinutes = QStringLiteral("disks/checkMinutes");
const QString kDiskWatches = QStringLiteral("disks/watched");

bool validFraction(double value)
{
    return value >= 0.0 && value <= 1.0;
}

} // namespace

AppState::AppState(Persistence persistence, QObject *parent)
    : QObject(parent)
    , m_persistence(persistence)
{
    if (m_persistence != Persistence::Settings) {
        return;
    }
    const auto settings = AppSettings::open();
    m_enabled = settings->value(kEnabled, true).toBool();
    m_checkUpdatesAtStartup = settings->value(kCheckUpdates, true).toBool();

    // Un atajo ilegible en el .ini (editado a mano, o de una version futura) vuelve al de fabrica
    // en vez de dejar la accion sin atajo.
    const Shortcut addKeyframe = Shortcut::fromPortableString(settings->value(kAddKeyframe).toString());
    if (addKeyframe.isValid()) {
        m_addKeyframe = addKeyframe;
    }
    const Shortcut frame = Shortcut::fromPortableString(settings->value(kFrameDopeSheet).toString());
    if (frame.isValid()) {
        m_frameDopeSheet = frame;
    }

    bool okX = false;
    bool okY = false;
    const double x = settings->value(kSpotX).toDouble(&okX);
    const double y = settings->value(kSpotY).toDouble(&okY);
    if (okX && okY && validFraction(x) && validFraction(y)) {
        m_hasDopeSheetSpot = true;
        m_dopeSheetSpot = QPointF(x, y);
    }
    const int minutes = settings->value(kDiskMinutes, DiskSpace::kDefaultIntervalMinutes).toInt();
    m_diskCheckMinutes = DiskSpace::isValidInterval(minutes) ? minutes : DiskSpace::kDefaultIntervalMinutes;
    // Una entrada ilegible (editada a mano) se descarta sola; las demas se conservan.
    const int count = settings->beginReadArray(kDiskWatches);
    for (int i = 0; i < count; ++i) {
        settings->setArrayIndex(i);
        DiskWatch watch;
        watch.root = settings->value(QStringLiteral("root")).toString();
        bool okValue = false;
        watch.value = settings->value(QStringLiteral("value")).toInt(&okValue);
        const bool okUnit = DiskSpace::unitFromString(settings->value(QStringLiteral("unit")).toString(), &watch.unit);
        watch.name = settings->value(QStringLiteral("name")).toString();
        if (watch.root.isEmpty() || !okValue || !okUnit || isWatched(watch.root)) {
            qWarning() << "[AppState] Disco vigilado ilegible en el .ini, se descarta: indice" << i;
            continue;
        }
        watch.value = DiskSpace::clampValue(watch.value, watch.unit);
        m_diskWatches.append(watch);
    }
    settings->endArray();

    qInfo() << "[AppState] Cargado:" << (m_enabled ? "activo" : "en pausa")
            << "| Add keyframe:" << m_addKeyframe.toPortableString()
            << "| Frame Dope Sheet:" << m_frameDopeSheet.toPortableString()
            << "| punto del Dope Sheet:" << (m_hasDopeSheetSpot ? QStringLiteral("%1, %2").arg(x).arg(y)
                                                                  : QStringLiteral("sin calibrar"))
            << "| discos vigilados:" << m_diskWatches.size() << "cada" << m_diskCheckMinutes << "min";
}

Shortcut AppState::shortcut(ShortcutAction action) const
{
    return action == ShortcutAction::AddKeyframe ? m_addKeyframe : m_frameDopeSheet;
}

AppState::Registration AppState::registration(ShortcutAction action) const
{
    return action == ShortcutAction::AddKeyframe ? m_addKeyframeRegistration : m_frameRegistration;
}

QString AppState::actionTitle(ShortcutAction action)
{
    return action == ShortcutAction::AddKeyframe ? QStringLiteral("Add keyframe") : QStringLiteral("Frame Dope Sheet");
}

void AppState::writeValue(const QString &key, const QVariant &value)
{
    if (m_persistence != Persistence::Settings) {
        return;
    }
    const auto settings = AppSettings::open();
    settings->setValue(key, value);
    settings->sync();
    if (settings->status() != QSettings::NoError) {
        qWarning() << "[AppState] No se pudo guardar" << key << "en" << AppSettings::filePath();
    }
}

void AppState::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }
    m_enabled = enabled;
    writeValue(kEnabled, enabled);
    emit changed();
}

void AppState::setCheckUpdatesAtStartup(bool check)
{
    if (m_checkUpdatesAtStartup == check) {
        return;
    }
    m_checkUpdatesAtStartup = check;
    writeValue(kCheckUpdates, check);
    emit changed();
}

void AppState::setShortcut(ShortcutAction action, const Shortcut &shortcut)
{
    Shortcut &target = action == ShortcutAction::AddKeyframe ? m_addKeyframe : m_frameDopeSheet;
    if (!shortcut.isValid() || target == shortcut) {
        return;
    }
    target = shortcut;
    writeValue(action == ShortcutAction::AddKeyframe ? kAddKeyframe : kFrameDopeSheet, shortcut.toPortableString());
    emit changed();
}

void AppState::setDopeSheetSpot(const QPointF &fraction)
{
    if (!validFraction(fraction.x()) || !validFraction(fraction.y())) {
        return;
    }
    if (m_hasDopeSheetSpot && m_dopeSheetSpot == fraction) {
        return;
    }
    m_hasDopeSheetSpot = true;
    m_dopeSheetSpot = fraction;
    writeValue(kSpotX, fraction.x());
    writeValue(kSpotY, fraction.y());
    emit changed();
}

void AppState::setNukeInFront(bool inFront)
{
    if (m_nukeInFront == inFront) {
        return;
    }
    m_nukeInFront = inFront;
    emit changed();
}

void AppState::setRegistration(ShortcutAction action, Registration registration)
{
    Registration &target = action == ShortcutAction::AddKeyframe ? m_addKeyframeRegistration : m_frameRegistration;
    if (target == registration) {
        return;
    }
    target = registration;
    emit changed();
}

void AppState::setAccessibilityGranted(bool granted)
{
    if (m_accessibilityGranted == granted) {
        return;
    }
    m_accessibilityGranted = granted;
    emit changed();
}

// ---------------------------------------------------------------- Chequeo de espacio en disco

bool AppState::isWatched(const QString &root) const
{
    for (const DiskWatch &watch : m_diskWatches) {
        if (watch.root == root) {
            return true;
        }
    }
    return false;
}

bool AppState::driveReading(const QString &root, DriveInfo *drive) const
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

QList<DriveInfo> AppState::drives() const
{
    QList<DriveInfo> list = m_drives.values();
    std::sort(list.begin(), list.end(), [](const DriveInfo &a, const DriveInfo &b) { return a.root < b.root; });
    return list;
}

QList<DiskWatch> AppState::lowWatches() const
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

void AppState::writeDiskWatches()
{
    if (m_persistence != Persistence::Settings) {
        return;
    }
    const auto settings = AppSettings::open();
    // El array se reescribe entero: sin el remove, un disco quitado dejaria su indice viejo.
    settings->remove(kDiskWatches);
    settings->beginWriteArray(kDiskWatches, int(m_diskWatches.size()));
    for (int i = 0; i < m_diskWatches.size(); ++i) {
        const DiskWatch &watch = m_diskWatches.at(i);
        settings->setArrayIndex(i);
        settings->setValue(QStringLiteral("root"), watch.root);
        settings->setValue(QStringLiteral("value"), watch.value);
        settings->setValue(QStringLiteral("unit"), DiskSpace::unitToString(watch.unit));
        settings->setValue(QStringLiteral("name"), watch.name);
    }
    settings->endArray();
    settings->sync();
    if (settings->status() != QSettings::NoError) {
        qWarning() << "[AppState] No se pudieron guardar los discos vigilados en" << AppSettings::filePath();
    }
}

void AppState::setDiskCheckMinutes(int minutes)
{
    if (!DiskSpace::isValidInterval(minutes) || m_diskCheckMinutes == minutes) {
        return;
    }
    m_diskCheckMinutes = minutes;
    writeValue(kDiskMinutes, minutes);
    qInfo() << "[AppState] Chequeo de discos cada" << minutes << "min";
    emit changed();
}

void AppState::addDiskWatch(const QString &root, const QString &name)
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
    qInfo() << "[AppState] Disco vigilado:" << root << "umbral" << DiskSpace::thresholdText(watch);
    emit changed();
}

void AppState::removeDiskWatch(const QString &root)
{
    for (int i = 0; i < m_diskWatches.size(); ++i) {
        if (m_diskWatches.at(i).root == root) {
            m_diskWatches.removeAt(i);
            writeDiskWatches();
            qInfo() << "[AppState] Disco sin vigilar:" << root;
            emit changed();
            return;
        }
    }
}

void AppState::setDiskThreshold(const QString &root, int value, DiskWatch::Unit unit)
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
        qInfo() << "[AppState] Umbral de" << root << ":" << DiskSpace::thresholdText(watch);
        emit changed();
        return;
    }
}

void AppState::setDriveReadings(const QList<DriveInfo> &readings, const QStringList &queried, bool listedAll,
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
