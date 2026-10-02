#include "core/NukePlugin.h"

#include "core/LgaRegistry.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>
#include <QVersionNumber>
#include <QtGlobal>

namespace {

/// Recurso embebido. QFile lee ":/..." como cualquier archivo, asi que el resto de esta unidad no
/// necesita saber que es un recurso Qt.
QString resourcePath(const NukePlugin::Spec &spec, const QString &name)
{
    return spec.resourcePrefix + QLatin1Char('/') + name;
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
/// Mighty Tools), no una carpeta de plugin instalada. Instalar ahi pisaria codigo fuente sin
/// commitear. D-04 agrega el chequeo de `.git` al que ya tenia el cliente v1.83 (solo CMakeLists.txt).
bool looksLikeSourceRepo(const QString &dirPath)
{
    return QFileInfo::exists(QDir(dirPath).filePath(QStringLiteral("QtClient/CMakeLists.txt")))
        || QFileInfo::exists(QDir(dirPath).filePath(QStringLiteral(".git")));
}

/// La carpeta elegida como `.nuke` es el codigo fuente del plugin (el repo de origen) o un clon de
/// Mighty Tools. Un `.git` solo NO cuenta: hay usuarios que versionan su `.nuke` entera con git (la de
/// Lega es el repo LGA_Nuke_Win) y ahi el plugin se instala como en cualquier otra.
bool looksLikeSourceTreeRoot(const QString &dirPath)
{
    return QFileInfo::exists(QDir(dirPath).filePath(QStringLiteral("QtClient/CMakeLists.txt")))
        || QFileInfo::exists(QDir(dirPath).filePath(QStringLiteral("nuke_plugin/OpenInNukeXBridge.qrc")));
}

/// Si el `init.py` tiene una linea ACTIVA (no comentada) que agrega la carpeta del plugin al
/// plugin path. Tolerante a comillas simples/dobles y a "./" opcional (matchear el archivo entero
/// en vez de linea por linea daba falsos positivos con la linea comentada, el gesto natural para
/// desactivar un plugin un rato).
bool hasActivePluginPathLine(const QString &initPyContents, const QString &folderName)
{
    const QRegularExpression lineRe(QStringLiteral(R"(nuke\s*\.\s*pluginAddPath\s*\(\s*['"][^'"]*%1['"])")
                                        .arg(QRegularExpression::escape(folderName)));
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
        qInfo("NukePlugin: no se pudieron ajustar los permisos de %s", qUtf8Printable(to));
    }
    return true;
}

bool payloadPresent(const NukePlugin::Spec &spec)
{
    for (const QString &name : spec.payloadFiles) {
        if (!QFileInfo::exists(resourcePath(spec, name))) {
            return false;
        }
    }
    return !NukePlugin::bundledVersion(spec).isEmpty();
}

bool writePayloadInto(const NukePlugin::Spec &spec, const QString &targetDir, QString *errorOut)
{
    for (const QString &name : spec.payloadFiles) {
        if (!copyOver(resourcePath(spec, name), QDir(targetDir).filePath(name), errorOut)) {
            return false;
        }
    }

    // El VERSION se escribe con bundledVersion() y no se copia del recurso: la fuente de verdad es
    // el archivo VERSION embebido, leido por una sola funcion.
    QSaveFile versionFile(QDir(targetDir).filePath(QStringLiteral("VERSION")));
    if (!versionFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        *errorOut = QStringLiteral("No se pudo escribir el VERSION en %1").arg(targetDir);
        return false;
    }
    versionFile.write((NukePlugin::bundledVersion(spec) + QLatin1Char('\n')).toUtf8());
    if (!versionFile.commit()) {
        *errorOut = QStringLiteral("No se pudo escribir el VERSION en %1").arg(targetDir);
        return false;
    }
    return true;
}

/// Borra los restos de versiones anteriores que reconocemos como nuestros por contenido, y su .pyc.
/// Un fallo aca no frena la instalacion: el resto queda y se reintenta en la proxima.
void removeRetiredFiles(const NukePlugin::Spec &spec, const QString &targetDir)
{
    for (const NukePlugin::RetiredFile &retired : spec.retiredFiles) {
        const QString path = QDir(targetDir).filePath(retired.name);
        if (!QFileInfo(path).isFile()) {
            continue;
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        const bool ours = QString::fromUtf8(file.readAll()).contains(retired.marker);
        file.close();
        if (!ours) {
            qInfo("NukePlugin: %s no es nuestro, no se toca", qUtf8Printable(path));
            continue;
        }
        if (QFile::remove(path)) {
            qInfo("NukePlugin: resto de una version anterior borrado: %s", qUtf8Printable(path));
        }
        // El .pyc que Python dejo de ese modulo (`__pycache__/<nombre>.cpython-311.pyc`).
        const QString stem = QFileInfo(retired.name).completeBaseName();
        QDir cache(QDir(targetDir).filePath(QStringLiteral("__pycache__")));
        const QStringList pycs = cache.entryList({stem + QStringLiteral(".*.pyc")}, QDir::Files);
        for (const QString &pyc : pycs) {
            QFile::remove(cache.filePath(pyc));
        }
    }
}

/// Solo APPENDEA: el init.py del usuario puede tener toda su configuracion de Nuke y reescribirlo
/// seria imperdonable.
bool ensurePluginPathLine(const NukePlugin::Spec &spec, const QString &nukeDir, QString *errorOut)
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

    if (hasActivePluginPathLine(existing, spec.folderName)) {
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
    out << "\n# " << spec.initComment << "\n" << NukePlugin::pluginAddPathLine(spec) << "\n";
    return true;
}

/// Lo comun a install() y exportPayload(): validar la carpeta y escribir el payload.
NukePlugin::Error writeFolder(const NukePlugin::Spec &spec, const QString &baseDir, QString &detail,
                              QString *cleanOut)
{
    using NukePlugin::Error;
    const QString clean = QDir::cleanPath(baseDir.trimmed());
    if (clean.isEmpty() || clean == QLatin1String(".") || !QFileInfo(clean).isDir()) {
        detail = QStringLiteral("La carpeta no existe: '%1'").arg(baseDir);
        return Error::DirMissing;
    }
    const QString pluginDir = QDir(clean).filePath(spec.folderName);
    if (looksLikeSourceRepo(pluginDir) || looksLikeSourceTreeRoot(clean)) {
        detail = QStringLiteral("Es un repositorio o codigo fuente: %1").arg(pluginDir);
        return Error::SourceRepo;
    }
    // El chequeo del payload va ANTES del mkpath: con un build incompleto no queremos dejar una
    // carpeta vacia que el proximo inspect() vea como "a medio instalar".
    if (!payloadPresent(spec)) {
        detail = QStringLiteral("Este build no trae el payload de %1 embebido.").arg(spec.folderName);
        return Error::PayloadMissing;
    }
    if (!QDir().mkpath(pluginDir)) {
        detail = QStringLiteral("No se pudo crear %1").arg(pluginDir);
        return Error::WriteFailed;
    }
    if (!writePayloadInto(spec, pluginDir, &detail)) {
        return Error::WriteFailed;
    }
    *cleanOut = clean;
    return Error::None;
}

} // namespace

namespace NukePlugin {

QString pluginAddPathLine(const Spec &spec)
{
    return QStringLiteral("nuke.pluginAddPath('./%1')").arg(spec.folderName);
}

QString bundledVersion(const Spec &spec)
{
    return readTrimmedFile(resourcePath(spec, QStringLiteral("VERSION")));
}

ChipState chipState(const Spec &spec, const Status &status)
{
    if (!status.filesPresent || !status.pathRegistered) {
        return ChipState::NotInstalled;
    }
    if (status.installedVersion.isEmpty()) {
        return ChipState::InstalledUnknownVersion;
    }
    // Se ofrece actualizar SOLO si la instalada es MENOR que la embebida, comparando por segmento
    // (1.9 < 1.10). Una instalada MAS NUEVA (otra copia de la app mas reciente la puso) no se pisa
    // con una vieja. Una version que no es una lista de enteros ("1.83-beta") cuenta como ilegible.
    const auto parse = [](const QString &text, bool *ok) {
        const QString trimmed = text.trimmed();
        qsizetype end = -1;
        const QVersionNumber version = QVersionNumber::fromString(trimmed, &end);
        *ok = !trimmed.isEmpty() && !version.isNull() && end == trimmed.size();
        return version;
    };
    bool installedOk = false;
    const QVersionNumber installed = parse(status.installedVersion, &installedOk);
    if (!installedOk) {
        return ChipState::InstalledUnknownVersion;
    }
    bool bundledOk = false;
    const QVersionNumber bundled = parse(bundledVersion(spec), &bundledOk);
    if (bundledOk && QVersionNumber::compare(installed, bundled) < 0) {
        return ChipState::UpdateAvailable;
    }
    return ChipState::Installed;
}

Status inspect(const Spec &spec, const QString &nukeDir)
{
    Status status;
    if (nukeDir.isEmpty() || !QFileInfo(nukeDir).isDir()) {
        return status;
    }

    const QString pluginDir = QDir(nukeDir).filePath(spec.folderName);
    status.folderPresent = QFileInfo(pluginDir).isDir();
    if (status.folderPresent) {
        status.filesPresent = true;
        for (const QString &name : spec.requiredFiles) {
            if (!QFileInfo::exists(QDir(pluginDir).filePath(name))) {
                status.filesPresent = false;
                break;
            }
        }
        status.installedVersion = readTrimmedFile(QDir(pluginDir).filePath(QStringLiteral("VERSION")));
    }

    const QString initPath = QDir(nukeDir).filePath(QStringLiteral("init.py"));
    if (QFileInfo::exists(initPath)) {
        status.pathRegistered = hasActivePluginPathLine(readTrimmedFile(initPath), spec.folderName);
    }
    return status;
}

Error install(const Spec &spec, const QString &nukeDir, QString *detailForLog)
{
    QString ignored;
    QString &detail = detailForLog ? *detailForLog : ignored;
    detail.clear();

    QString clean;
    const Error err = writeFolder(spec, nukeDir, detail, &clean);
    if (err != Error::None) {
        return err;
    }
    if (!ensurePluginPathLine(spec, clean, &detail)) {
        return Error::WriteFailed;
    }
    // Recien con la instalacion completa: si algo fallo antes, lo viejo sigue andando. Exportar no
    // limpia nada (la carpeta de destino no es una .nuke en uso).
    removeRetiredFiles(spec, QDir(clean).filePath(spec.folderName));
    qInfo("NukePlugin: %s v%s instalado en %s", qUtf8Printable(spec.folderName),
          qUtf8Printable(bundledVersion(spec)), qUtf8Printable(clean));
    return Error::None;
}

Error exportPayload(const Spec &spec, const QString &destDir, QString *detailForLog)
{
    QString ignored;
    QString &detail = detailForLog ? *detailForLog : ignored;
    detail.clear();

    QString clean;
    const Error err = writeFolder(spec, destDir, detail, &clean);
    if (err == Error::None) {
        qInfo("NukePlugin: %s exportado a %s", qUtf8Printable(spec.folderName), qUtf8Printable(clean));
    }
    return err;
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

} // namespace NukePlugin
