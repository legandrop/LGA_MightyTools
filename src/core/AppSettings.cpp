#include "core/AppSettings.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace AppSettings {

QString filePath()
{
    // AppDataLocation ya incluye organizacion y app (LGA/LGA_MightyTools) en las dos plataformas,
    // siempre que main() haya fijado los dos nombres antes.
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("settings.ini"));
}

std::unique_ptr<QSettings> open()
{
    QDir().mkpath(QFileInfo(filePath()).absolutePath());
    return std::make_unique<QSettings>(filePath(), QSettings::IniFormat);
}

} // namespace AppSettings
