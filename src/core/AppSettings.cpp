#include "core/AppSettings.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace {

enum class Source { User, Memory, File };
Source g_source = Source::User;
QString g_file;

// Formato "en memoria": leer no trae nada y escribir no guarda nada. Cada QSettings abierto con el
// arranca vacio y lo que se le escribe vive solo en ese objeto.
bool readNothing(QIODevice &, QSettings::SettingsMap &)
{
    return true;
}

bool writeNothing(QIODevice &, const QSettings::SettingsMap &)
{
    return true;
}

QSettings::Format memoryFormat()
{
    static const QSettings::Format format = QSettings::registerFormat(QStringLiteral("memoryini"), readNothing, writeNothing);
    return format;
}

std::unique_ptr<QSettings> make()
{
    if (g_source == Source::Memory) {
        // La ruta no se abre nunca para escribir (writeNothing) y la carpeta no se crea.
        return std::make_unique<QSettings>(QDir(QDir::tempPath()).filePath(QStringLiteral("lga_mightytools_memory.memoryini")),
                                           memoryFormat());
    }
    return std::make_unique<QSettings>(AppSettings::filePath(), QSettings::IniFormat);
}

} // namespace

namespace AppSettings {

QString filePath()
{
    if (g_source == Source::File) {
        return g_file;
    }
    // AppDataLocation ya incluye organizacion y app (LGA/LGA_MightyTools) en las dos plataformas,
    // siempre que main() haya fijado los dos nombres antes.
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("settings.ini"));
}

std::unique_ptr<QSettings> open()
{
    return make();
}

std::unique_ptr<QSettings> openForWrite()
{
    if (g_source != Source::Memory) {
        QDir().mkpath(QFileInfo(filePath()).absolutePath());
    }
    return make();
}

void useMemoryOnly()
{
    g_source = Source::Memory;
}

void useFile(const QString &path)
{
    g_source = Source::File;
    g_file = QFileInfo(path).absoluteFilePath();
}

bool memoryOnly()
{
    return g_source == Source::Memory;
}

} // namespace AppSettings
