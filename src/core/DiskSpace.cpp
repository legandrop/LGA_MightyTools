#include "core/DiskSpace.h"

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

const QList<int> &intervalChoices()
{
    static const QList<int> choices = {1, 5, 15, 30, 60, 360};
    return choices;
}

bool isValidInterval(int minutes)
{
    return intervalChoices().contains(minutes);
}

QString intervalText(int minutes)
{
    if (minutes >= 60 && minutes % 60 == 0) {
        const int hours = minutes / 60;
        return hours == 1 ? QStringLiteral("1 hour") : QStringLiteral("%1 hours").arg(hours);
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

QString thresholdText(const DiskWatch &watch)
{
    const int value = clampValue(watch.value, watch.unit);
    return watch.unit == DiskWatch::Unit::Percent ? QStringLiteral("%1%").arg(value) : QStringLiteral("%1 GB").arg(value);
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

bool shouldNotify(bool lowNow, const AlertState &state, const QDateTime &now)
{
    if (!lowNow) {
        return false;
    }
    if (!state.wasLow || !state.lastNotified.isValid()) {
        return true;
    }
    return state.lastNotified.secsTo(now) >= kRepeatSeconds;
}

} // namespace DiskSpace
