#include "modules/openinnukex/NukeXPath.h"

#include "core/LgaRegistry.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>
#include <QtGlobal>

namespace NukeXPath {

QString defaultFilePath()
{
    // LgaRegistry::directory() ya resuelve ".../LGA" (Windows: %APPDATA%\LGA; mac: ~/Library/
    // Application Support/LGA), el mismo nivel que usaba el cliente viejo con su propio nombre
    // de app ("OpenInNukeX"). La subcarpeta NO cambia (contrato con PipeSync, plan seccion 2 y 4.7).
    const QString lgaDir = LgaRegistry::directory();
    if (lgaDir.isEmpty()) {
        return QString();
    }
    return QDir(lgaDir).filePath(QStringLiteral("OpenInNukeX/nukeXpath.txt"));
}

QString read(const QString &filePath)
{
    if (filePath.isEmpty()) {
        return QString();
    }
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    QTextStream in(&file);
    return in.readLine().trimmed();
}

bool write(const QString &filePath, const QString &nukePath)
{
    if (filePath.isEmpty()) {
        return false;
    }
    const QString dir = QFileInfo(filePath).absolutePath();
    if (!QDir().mkpath(dir)) {
        qWarning("NukeXPath: no se pudo crear %s", qUtf8Printable(dir));
        return false;
    }
    // QSaveFile: escritura atomica. PipeSync puede estar leyendo este mismo archivo del otro lado.
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning("NukeXPath: no se pudo abrir para escribir %s", qUtf8Printable(filePath));
        return false;
    }
    file.write((nukePath + QLatin1Char('\n')).toUtf8());
    if (!file.commit()) {
        qWarning("NukeXPath: no se pudo commitear %s", qUtf8Printable(filePath));
        return false;
    }
    return true;
}

QString healStalePath(const QString &filePath, const QString &currentPath, const QList<NukeVersion> &versions)
{
    if (!currentPath.isEmpty() && QFile::exists(currentPath)) {
        return currentPath;
    }
    if (versions.isEmpty()) {
        return QString();
    }

    const NukeVersion best = NukeScanner::newest(versions);
    if (currentPath.isEmpty()) {
        qInfo("NukeXPath: sin ruta configurada, se toma la version mas nueva encontrada (%s)",
              qUtf8Printable(best.displayName));
    } else {
        qInfo("NukeXPath: la ruta guardada ya no existe (%s), se repone con %s",
              qUtf8Printable(currentPath), qUtf8Printable(best.displayName));
    }
    write(filePath, best.path);
    return best.path;
}

} // namespace NukeXPath
