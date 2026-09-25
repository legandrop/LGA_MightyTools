#include "modules/openinnukex/NukeBridge.h"

#include "core/LgaRegistry.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>
#include <QtGlobal>

namespace {

const QStringList kPayloadFiles = {
    QStringLiteral("init.py"),
    QStringLiteral("LGA_QtAdapter_OpenInNukeX.py"),
};

/// Recurso embebido (nuke_plugin/OpenInNukeXBridge.qrc, prefix "/bridge"). QFile lee ":/..." como
/// cualquier archivo, asi que el resto de esta unidad no necesita saber que es un recurso Qt.
QString payloadResourcePath(const QString &name)
{
    return QStringLiteral(":/bridge/%1").arg(name);
}

QString readTrimmedFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll()).trimmed();
}

/// Una carpeta con `QtClient/CMakeLists.txt` o `.git` es un REPO (el de origen o un clon de
/// Mighty Tools), no una `.nuke` instalada. Instalar ahi pisaria codigo fuente sin commitear.
/// D-04 agrega el chequeo de `.git` al que ya tenia el cliente v1.83 (solo CMakeLists.txt).
bool looksLikeSourceRepo(const QString &dirPath)
{
    return QFileInfo::exists(QDir(dirPath).filePath(QStringLiteral("QtClient/CMakeLists.txt")))
        || QFileInfo::exists(QDir(dirPath).filePath(QStringLiteral(".git")));
}

/// Si el `init.py` tiene una linea ACTIVA (no comentada) que agrega la carpeta del plugin al
/// plugin path. Tolerante a comillas simples/dobles y a "./" opcional (ver nota del origen:
/// matchear el archivo entero en vez de linea por linea daba falsos positivos con la linea
/// comentada, el gesto natural para desactivar el bridge un rato).
bool hasActivePluginPathLine(const QString &initPyContents)
{
    static const QRegularExpression lineRe(
        QStringLiteral(R"(nuke\s*\.\s*pluginAddPath\s*\(\s*['"][^'"]*LGA_OpenInNukeX['"])"));
    const QStringList lines = initPyContents.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line.trimmed().startsWith(QLatin1Char('#'))) {
            continue;
        }
        if (lineRe.match(line).hasMatch()) {
            return true;
        }
    }
    return false;
}

bool copyOver(const QString &from, const QString &to, QString *errorOut)
{
    if (QFile::exists(to) && !QFile::remove(to)) {
        *errorOut = QStringLiteral("No se pudo reemplazar %1").arg(to);
        return false;
    }
    if (!QFile::copy(from, to)) {
        *errorOut = QStringLiteral("No se pudo copiar %1").arg(QFileInfo(from).fileName());
        return false;
    }
    // Lo copiado desde el recurso Qt hereda permisos de solo lectura: se habilita escritura para
    // que el usuario pueda tocar o borrar el .py a mano en su propia carpeta .nuke.
    if (!QFile(to).setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther)) {
        qInfo("NukeBridge: no se pudieron ajustar los permisos de %s", qUtf8Printable(to));
    }
    return true;
}

bool payloadPresent()
{
    for (const QString &name : kPayloadFiles) {
        if (!QFileInfo::exists(payloadResourcePath(name))) {
            return false;
        }
    }
    return true;
}

bool writePayloadInto(const QString &targetDir, QString *errorOut)
{
    if (!payloadPresent()) {
        *errorOut = QStringLiteral("Este build no trae el payload del bridge embebido.");
        return false;
    }

    for (const QString &name : kPayloadFiles) {
        const QString from = payloadResourcePath(name);
        if (!copyOver(from, QDir(targetDir).filePath(name), errorOut)) {
            return false;
        }
    }

    // El VERSION se escribe con NukeBridge::bundledVersion() y no se copia del recurso: la fuente
    // de verdad es el archivo VERSION embebido, leido una sola vez por bundledVersion().
    QSaveFile versionFile(QDir(targetDir).filePath(QStringLiteral("VERSION")));
    if (!versionFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        *errorOut = QStringLiteral("No se pudo escribir el VERSION en %1").arg(targetDir);
        return false;
    }
    versionFile.write((NukeBridge::bundledVersion() + QLatin1Char('\n')).toUtf8());
    if (!versionFile.commit()) {
        *errorOut = QStringLiteral("No se pudo escribir el VERSION en %1").arg(targetDir);
        return false;
    }
    return true;
}

/// Solo APPENDEA: el init.py del usuario puede tener toda su configuracion de Nuke y reescribirlo
/// seria imperdonable.
bool ensurePluginPathLine(const QString &nukeDir, QString *errorOut)
{
    const QString initPath = QDir(nukeDir).filePath(QStringLiteral("init.py"));

    QString existing;
    if (QFileInfo::exists(initPath)) {
        QFile file(initPath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            *errorOut = QStringLiteral("No se pudo leer %1").arg(initPath);
            return false;
        }
        existing = QString::fromUtf8(file.readAll());
    }

    if (hasActivePluginPathLine(existing)) {
        return true;
    }

    QFile file(initPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        *errorOut = QStringLiteral("No se pudo escribir en %1").arg(initPath);
        return false;
    }
    QTextStream out(&file);
    if (!existing.isEmpty() && !existing.endsWith(QLatin1Char('\n'))) {
        out << "\n";
    }
    out << "\n# LGA OpenInNukeX\n" << NukeBridge::pluginAddPathLine() << "\n";
    return true;
}

} // namespace

namespace NukeBridge {

QString pluginFolderName()
{
    return QStringLiteral("LGA_OpenInNukeX");
}

QString pluginAddPathLine()
{
    return QStringLiteral("nuke.pluginAddPath('./%1')").arg(pluginFolderName());
}

QString bundledVersion()
{
    return readTrimmedFile(payloadResourcePath(QStringLiteral("VERSION")));
}

ChipState chipState(const Status &status)
{
    if (!status.filesPresent || !status.pathRegistered) {
        return ChipState::NotInstalled;
    }
    if (status.installedVersion.isEmpty()) {
        return ChipState::InstalledUnknownVersion;
    }
    const QString bundled = bundledVersion();
    if (!bundled.isEmpty() && status.installedVersion != bundled) {
        return ChipState::UpdateAvailable;
    }
    return ChipState::Installed;
}

QString detectNukeDirectory()
{
    const QString candidate = QDir(QDir::homePath()).filePath(QStringLiteral(".nuke"));
    return QFileInfo(candidate).isDir() ? QDir::cleanPath(candidate) : QString();
}

QString currentNukeDirectory()
{
    const QString registered = LgaRegistry::readNukeDirectory();
    if (!registered.isEmpty() && QFileInfo(registered).isDir()) {
        return QDir::cleanPath(QDir::fromNativeSeparators(registered));
    }
    return detectNukeDirectory();
}

Status inspect(const QString &nukeDir)
{
    Status status;
    if (nukeDir.isEmpty() || !QFileInfo(nukeDir).isDir()) {
        return status;
    }

    const QString pluginDir = QDir(nukeDir).filePath(pluginFolderName());
    status.folderPresent = QFileInfo(pluginDir).isDir();
    if (status.folderPresent) {
        status.filesPresent = true;
        for (const QString &name : kPayloadFiles) {
            if (!QFileInfo::exists(QDir(pluginDir).filePath(name))) {
                status.filesPresent = false;
                break;
            }
        }
        status.installedVersion = readTrimmedFile(QDir(pluginDir).filePath(QStringLiteral("VERSION")));
    }

    const QString initPath = QDir(nukeDir).filePath(QStringLiteral("init.py"));
    if (QFileInfo::exists(initPath)) {
        status.pathRegistered = hasActivePluginPathLine(readTrimmedFile(initPath));
    }

    return status;
}

bool publishToLgaRegistry(const QString &nukeDir)
{
    return LgaRegistry::saveNukeDirectory(nukeDir);
}

Error install(const QString &nukeDir, QString *detailForLog, bool automatedRun, RegistryPublisher publisher)
{
    QString ignored;
    QString &detail = detailForLog ? *detailForLog : ignored;
    detail.clear();

    const QString clean = QDir::cleanPath(nukeDir.trimmed());
    if (clean.isEmpty() || !QFileInfo(clean).isDir()) {
        detail = QStringLiteral("La carpeta no existe: '%1'").arg(nukeDir);
        return Error::DirMissing;
    }

    const QString pluginDir = QDir(clean).filePath(pluginFolderName());
    if (looksLikeSourceRepo(pluginDir) || looksLikeSourceRepo(clean)) {
        detail = QStringLiteral("Es un repositorio (QtClient/CMakeLists.txt o .git): %1").arg(pluginDir);
        return Error::SourceRepo;
    }

    // El chequeo del payload va ANTES del mkpath: con un build incompleto no queremos dejar una
    // carpeta <.nuke>/LGA_OpenInNukeX/ vacia que el proximo inspect() vea como "a medio instalar".
    if (!payloadPresent()) {
        detail = QStringLiteral("Este build no trae el payload del bridge embebido.");
        return Error::PayloadMissing;
    }
    if (!QDir().mkpath(pluginDir)) {
        detail = QStringLiteral("No se pudo crear %1").arg(pluginDir);
        return Error::WriteFailed;
    }
    if (!writePayloadInto(pluginDir, &detail)) {
        return Error::WriteFailed;
    }
    if (!ensurePluginPathLine(clean, &detail)) {
        return Error::WriteFailed;
    }

    // Recien cuando la instalacion salio bien se publica la carpeta: registrar una `.nuke` en la
    // que el bridge no quedo instalado le daria a las otras apps LGA una ruta inutil. En
    // automatedRun (self-test, --ui-shot, --ui-probe) NUNCA se llama a `publisher`, aunque la
    // carpeta de instalacion sea una temporal legitima: el registro compartido no tiene forma de
    // distinguir una ruta de prueba de una real, asi que la guarda es sobre automatedRun, no sobre
    // la carpeta.
    if (automatedRun) {
        qInfo("NukeBridge: automatedRun=true, NO se publica %s en el registro LGA compartido", qUtf8Printable(clean));
    } else if (publisher) {
        publisher(clean);
    }

    qInfo("NukeBridge: instalado v%s en %s", qUtf8Printable(bundledVersion()), qUtf8Printable(pluginDir));
    return Error::None;
}

Error exportPayload(const QString &destDir, QString *detailForLog)
{
    QString ignored;
    QString &detail = detailForLog ? *detailForLog : ignored;
    detail.clear();

    const QString clean = QDir::cleanPath(destDir.trimmed());
    if (clean.isEmpty() || !QFileInfo(clean).isDir()) {
        detail = QStringLiteral("La carpeta no existe: '%1'").arg(destDir);
        return Error::DirMissing;
    }

    const QString targetDir = QDir(clean).filePath(pluginFolderName());
    if (looksLikeSourceRepo(targetDir) || looksLikeSourceRepo(clean)) {
        detail = QStringLiteral("Es un repositorio (QtClient/CMakeLists.txt o .git): %1").arg(targetDir);
        return Error::SourceRepo;
    }
    if (!payloadPresent()) {
        detail = QStringLiteral("Este build no trae el payload del bridge embebido.");
        return Error::PayloadMissing;
    }
    if (!QDir().mkpath(targetDir)) {
        detail = QStringLiteral("No se pudo crear %1").arg(targetDir);
        return Error::WriteFailed;
    }
    if (!writePayloadInto(targetDir, &detail)) {
        return Error::WriteFailed;
    }

    qInfo("NukeBridge: exportado a %s", qUtf8Printable(targetDir));
    return Error::None;
}

} // namespace NukeBridge
