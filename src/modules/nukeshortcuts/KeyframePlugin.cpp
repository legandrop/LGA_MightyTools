#include "modules/nukeshortcuts/KeyframePlugin.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QVersionNumber>

namespace KeyframePlugin {

const NukePlugin::Spec &plugin()
{
    static const NukePlugin::Spec spec = {
        QStringLiteral("LGA_NukeShortcuts"),
        QStringLiteral(":/nukeshortcuts"),
        {QStringLiteral("menu.py"), QStringLiteral("LGA_NukeShortcuts.py"), QStringLiteral("LGA_KeyframeToggle.py"),
         QStringLiteral("LGA_FrameDopeSheet.py")},
        // Solo los dos que ya traia la 1.00: una 1.00 instalada se ve "Update available", no "Not installed".
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

QString frameMarkerDir()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("nuke_frame_dope"));
}

bool framesDopeSheet(const QString &installedVersion)
{
#ifdef Q_OS_MACOS
    // Sin probar en la Mac (roadmap): ahi el plugin no registra el atajo y sigue el macro calibrado.
    Q_UNUSED(installedVersion);
    return false;
#endif
    // La 1.00 solo traia el keyframe. Una version ilegible no cuenta: sin saber, queda la calibracion.
    // Mismo criterio que NukePlugin::chipState: "1.01" es 1.1 y "1.10" es 1.10.
    const QString trimmed = installedVersion.trimmed();
    qsizetype end = 0;
    const QVersionNumber version = QVersionNumber::fromString(trimmed, &end);
    if (trimmed.isEmpty() || version.isNull() || end != trimmed.size()) {
        return false;
    }
    return version >= QVersionNumber(1, 1);
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
