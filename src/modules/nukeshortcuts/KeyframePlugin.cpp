#include "modules/nukeshortcuts/KeyframePlugin.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace KeyframePlugin {

const NukePlugin::Spec &plugin()
{
    static const NukePlugin::Spec spec = {
        QStringLiteral("LGA_NukeShortcuts"),
        QStringLiteral(":/nukeshortcuts"),
        {QStringLiteral("menu.py"), QStringLiteral("LGA_KeyframeToggle.py")},
        {QStringLiteral("menu.py"), QStringLiteral("LGA_KeyframeToggle.py")},
        {},
        QStringLiteral("LGA Mighty Tools - Nuke Shortcuts"),
    };
    return spec;
}

QString markerDir()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("nuke_set_key"));
}

bool handles(const QString &markerDir, qint64 pid, const QDateTime &processStarted)
{
    if (pid <= 0 || markerDir.isEmpty() || !processStarted.isValid()) {
        return false;
    }
    const QFileInfo marker(QDir(markerDir).filePath(QString::number(pid)));
    if (!marker.isFile()) {
        return false;
    }
    // Dos segundos de margen: la hora del archivo y la del proceso salen de relojes distintos.
    return marker.lastModified().toUTC() >= processStarted.toUTC().addSecs(-2);
}

} // namespace KeyframePlugin
