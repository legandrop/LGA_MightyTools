#include "modules/diskspace/DiskSpace.h"
#include "core/I18n.h"

#include <QtGlobal>

namespace {

constexpr qint64 kGiB = qint64(1024) * 1024 * 1024;

// Numero con hasta dos decimales, sin ceros de cola: 1.80 -> "1.8", 2.00 -> "2".
QString trimmed(double value)
{
    QString text = QString::number(value, 'f', 2);
    while (text.contains(QLatin1Char('.')) && (text.endsWith(QLatin1Char('0')) || text.endsWith(QLatin1Char('.')))) {
        text.chop(1);
    }
    return text;
}

} // namespace

namespace DiskSpace {

const QList<int> &remindChoices()
{
    static const QList<int> choices = {15, 30, 60, 120, 360};
    return choices;
}

bool isValidRemind(int minutes)
{
    return remindChoices().contains(minutes);
}

QString intervalText(int minutes)
{
    if (minutes >= 60 && minutes % 60 == 0) {
        const int hours = minutes / 60;
        return hours == 1 ? I18n::tr("1 hour") : I18n::tr("%1 hours").arg(hours);
    }
    return QStringLiteral("%1 min").arg(minutes);
}

int clampValue(int value, DiskWatch::Unit unit)
{
    return qBound(1, value, unit == DiskWatch::Unit::Percent ? kMaxPercent : kMaxGb);
}

QString unitToString(DiskWatch::Unit unit)
{
    return unit == DiskWatch::Unit::Percent ? QStringLiteral("%") : QStringLiteral("GB");
}

bool unitFromString(const QString &text, DiskWatch::Unit *unit)
{
    if (text == QLatin1String("GB")) {
        *unit = DiskWatch::Unit::GB;
        return true;
    }
    if (text == QLatin1String("%")) {
        *unit = DiskWatch::Unit::Percent;
        return true;
    }
    return false;
}

qint64 thresholdBytes(const DiskWatch &watch, qint64 totalBytes)
{
    const int value = clampValue(watch.value, watch.unit);
    if (watch.unit == DiskWatch::Unit::Percent) {
        return totalBytes <= 0 ? 0 : qint64(double(totalBytes) * value / 100.0);
    }
    return qint64(value) * kGiB;
}

bool isLow(const DiskWatch &watch, const DriveInfo &drive)
{
    if (drive.totalBytes <= 0) {
        return false; // sin lectura no hay nada que comparar
    }
    return drive.freeBytes < thresholdBytes(watch, drive.totalBytes);
}

QString formatBytes(qint64 bytes)
{
    const double gib = double(qMax<qint64>(0, bytes)) / double(kGiB);
    if (gib >= 1000.0) {
        return trimmed(gib / 1024.0) + QStringLiteral(" TB");
    }
    if (gib >= 10.0) {
        return QString::number(qRound(gib)) + QStringLiteral(" GB");
    }
    if (gib >= 1.0) {
        return trimmed(gib) + QStringLiteral(" GB");
    }
    return QString::number(qRound(gib * 1024.0)) + QStringLiteral(" MB");
}

QString formatSize(qint64 bytes)
{
    const double value = double(qMax<qint64>(0, bytes));
    const double gib = value / double(kGiB);
    if (gib >= 1000.0) {
        return QString::number(gib / 1024.0, 'f', 2) + QStringLiteral(" TB");
    }
    if (gib >= 100.0) {
        return QString::number(qRound(gib)) + QStringLiteral(" GB");
    }
    if (gib >= 10.0) {
        return QString::number(gib, 'f', 1) + QStringLiteral(" GB");
    }
    if (gib >= 1.0) {
        return QString::number(gib, 'f', 2) + QStringLiteral(" GB");
    }
    const double mib = value / (1024.0 * 1024.0);
    if (mib >= 1.0) {
        return QString::number(qRound(mib)) + QStringLiteral(" MB");
    }
    // Un archivo de pocos bytes igual ocupa algo: nunca "0 KB" salvo que este vacio.
    return QString::number(bytes <= 0 ? 0 : qMax(1, qRound(value / 1024.0))) + QStringLiteral(" KB");
}

QString ageText(qint64 modifiedSecs, const QDateTime &now)
{
    if (modifiedSecs <= 0) {
        return QString();
    }
    const qint64 days = qMax<qint64>(0, (now.toSecsSinceEpoch() - modifiedSecs) / 86400);
    if (days < 1) {
        return I18n::trc("age", "today");
    }
    if (days < 365) {
        return I18n::trc("age", "%1 d").arg(days);
    }
    return I18n::trc("age", "%1 y").arg(QString::number(double(days) / 365.0, 'f', 1));
}

QString thresholdText(const DiskWatch &watch)
{
    const int value = clampValue(watch.value, watch.unit);
    return watch.unit == DiskWatch::Unit::Percent ? QStringLiteral("%1%").arg(value) : QStringLiteral("%1 GB").arg(value);
}

QString displayName(const QString &storedName)
{
    return storedName == QLatin1String("Local Disk") ? I18n::tr("Local Disk") : storedName;
}

QString labelForRoot(const QString &root, const QString &storedName)
{
#ifdef Q_OS_WIN
    Q_UNUSED(storedName)
    QString label = root;
    while (label.endsWith(QLatin1Char('/')) || label.endsWith(QLatin1Char('\\'))) {
        label.chop(1);
    }
    return label;
#else
    if (!storedName.isEmpty()) {
        return storedName;
    }
    const int slash = root.lastIndexOf(QLatin1Char('/'), root.endsWith(QLatin1Char('/')) ? -2 : -1);
    const QString last = root.mid(slash + 1).remove(QLatin1Char('/'));
    return last.isEmpty() ? root : last;
#endif
}

bool shouldNotify(bool lowNow, const AlertState &state, const QDateTime &now, int remindMinutes)
{
    if (!lowNow) {
        return false;
    }
    if (!state.wasLow || !state.lastNotified.isValid()) {
        return true;
    }
    const QDateTime due =
        state.snoozedUntil.isValid() ? state.snoozedUntil : state.lastNotified.addSecs(qint64(remindMinutes) * 60);
    return now.secsTo(due) <= kDueToleranceSeconds;
}

} // namespace DiskSpace
