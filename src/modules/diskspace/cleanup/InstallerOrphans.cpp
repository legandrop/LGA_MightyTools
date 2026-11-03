#include "modules/diskspace/cleanup/InstallerOrphans.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace {

using InstallerPackages::pathKey;

struct ResultName
{
    InstallerOrphans::Result result;
    const char *name;
};

const ResultName kResultNames[] = {
    {InstallerOrphans::Result::Trashed, "trashed"},       {InstallerOrphans::Result::Claimed, "claimed"},
    {InstallerOrphans::Result::Installed, "installed"},   {InstallerOrphans::Result::Unverifiable, "unverifiable"},
    {InstallerOrphans::Result::TooNew, "too-new"},        {InstallerOrphans::Result::Busy, "busy"},
    {InstallerOrphans::Result::Unfinished, "unfinished"},
    {InstallerOrphans::Result::Incomplete, "incomplete"}, {InstallerOrphans::Result::NotAllowed, "not-allowed"},
    {InstallerOrphans::Result::Missing, "missing"},       {InstallerOrphans::Result::TrashFailed, "trash-failed"},
};

QByteArray line(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
}

} // namespace

namespace InstallerOrphans {

bool acceptedName(const QString &name)
{
    return InstallerPackages::isPackageName(name);
}

bool isTopLevel(const QString &path, const QString &dir)
{
    const QString file = pathKey(path);
    const QString folder = pathKey(dir);
    if (folder.isEmpty() || file.size() <= folder.size() + 1 || !file.startsWith(folder) || file.at(folder.size()) != QLatin1Char('\\')) {
        return false;
    }
    const QString rest = file.mid(folder.size() + 1);
    return !rest.isEmpty() && !rest.contains(QLatin1Char('\\'));
}

bool hasForeignSids(const QStringList &sids, const QString &currentSid)
{
    for (const QString &sid : sids) {
        if (sid.compare(QLatin1String(kSystemSid), Qt::CaseInsensitive) == 0) {
            continue;
        }
        if (!currentSid.isEmpty() && sid.compare(currentSid, Qt::CaseInsensitive) == 0) {
            continue;
        }
        return true;
    }
    return false;
}

Status status(const Survey &survey)
{
    if (survey.installerDir.isEmpty() || !survey.listed) {
        return Status::Unavailable;
    }
    if (survey.busy) {
        // Corriendo (o sin saber) pesa mas que sin terminar: la espera es otra.
        return survey.busyRunning || !survey.busyUnfinished ? Status::Busy : Status::Unfinished;
    }
    // Una enumeracion que fallo, o UserData sin leer: lo reclamado esta incompleto. Nada en uso con una
    // carpeta llena es mas probable una lectura rota que una maquina sin ningun programa de Windows
    // Installer: tampoco se le cree. Ser administrador no lo arregla necesariamente.
    if (!survey.enumerated || !survey.userDataRead || survey.claims.isEmpty()) {
        return Status::Unverified;
    }
    // Otro usuario tiene instalaciones propias y la consulta fue solo la de este: no se las ve. Esto si
    // lo resuelve la consulta de todos los usuarios, que pide administrador.
    if (!survey.allUsers && hasForeignSids(survey.userDataSids, survey.currentSid)) {
        return Status::NeedsAdmin;
    }
    return Status::Ok;
}

Reason packageReason(const FileFacts &file, const Survey &survey)
{
    const InstallerPackages::PackageFacts &package = file.package;
    // Sin saber que producto es, no se ofrece.
    if (!package.read || package.lookupFailed) {
        return Reason::Unverifiable;
    }
    if (!package.installed.isEmpty()) {
        return Reason::Installed;
    }
    // Lo que dio la enumeracion (con administrador, tambien lo de los demas usuarios).
    const QStringList own = package.patch ? package.targets : QStringList{package.productCode};
    for (const QString &code : own) {
        if (survey.installedProducts.contains(InstallerPackages::codeKey(code))) {
            return Reason::Installed;
        }
    }
    if (own.isEmpty() || (!package.patch && package.productCode.trimmed().isEmpty())) {
        return Reason::Unverifiable;
    }
    // Un parche sin el UpgradeCode de sus destinos: no se puede saber si su programa esta instalado en
    // otra version.
    if (package.patch && package.upgradeCodes.isEmpty()) {
        return Reason::Unverifiable;
    }
    // Con la consulta de todos los usuarios, MsiEnumRelatedProducts sigue sin ver las instalaciones por
    // usuario de los demas: si otro usuario tiene programas instalados, otra version del mismo programa
    // puede ser suya. Sin poder descartarlo, no se ofrece. (Sin administrador ese caso ya es NeedsAdmin.)
    if (!package.upgradeCodes.isEmpty() && hasForeignSids(survey.userDataSids, survey.currentSid)) {
        return Reason::Unverifiable;
    }
    return Reason::Ok;
}

Reason classify(const FileFacts &file, const Survey &survey, qint64 nowSecs)
{
    // Solo un archivo comun, de un solo nombre, suelto en la carpeta y con nombre de paquete.
    if (!acceptedName(file.name) || !file.isFile || file.isLink || file.links != 1 || !file.id.valid
        || !isTopLevel(file.path, survey.installerDir)
        || pathKey(file.path) != pathKey(survey.installerDir + QLatin1Char('\\') + file.name)) {
        return Reason::NotAllowed;
    }
    // En uso por la ruta o por la identidad (la misma copia con otro nombre).
    if (survey.claims.paths.contains(pathKey(file.path)) || survey.claims.ids.contains(file.id)) {
        return Reason::Claimed;
    }
    // Nadie la usa, pero es de un programa que sigue instalado (en cualquier version): no se ofrece.
    const Reason package = packageReason(file, survey);
    if (package != Reason::Ok) {
        return package;
    }
    // Lo mas nuevo entre creacion y modificacion; una fecha del futuro tambien cuenta como nueva.
    if (nowSecs - qMax(file.created, file.modified) < kMinAgeSecs) {
        return Reason::TooNew;
    }
    return Reason::Ok;
}

Verdict evaluate(const Survey &survey, qint64 nowSecs)
{
    Verdict verdict;
    verdict.status = status(survey);
    if (verdict.status != Status::Ok) {
        return verdict;
    }
    for (const FileFacts &file : survey.files) {
        if (classify(file, survey, nowSecs) == Reason::Ok) {
            verdict.orphans.append(file);
            verdict.bytes += file.bytes;
        }
    }
    return verdict;
}

QList<Check> reverify(const Survey &survey, const QStringList &names, qint64 nowSecs)
{
    const Status state = status(survey);
    QList<Check> checks;
    QSet<QString> seen;
    for (const QString &name : names) {
        // El mismo nombre dos veces es una sola cosa.
        if (seen.contains(pathKey(name))) {
            continue;
        }
        seen.insert(pathKey(name));
        Check check;
        check.name = name;
        if (!acceptedName(name)) {
            check.reason = Reason::NotAllowed;
        } else if (state == Status::Busy) {
            check.reason = Reason::Busy;
        } else if (state == Status::Unfinished) {
            check.reason = Reason::Unfinished;
        } else if (state != Status::Ok || !survey.allUsers) {
            // El paso elevado solo borra con la consulta de todos los usuarios.
            check.reason = Reason::Incomplete;
        } else {
            check.reason = Reason::Missing;
            for (const FileFacts &file : survey.files) {
                if (pathKey(file.name) == pathKey(name)) {
                    check.reason = classify(file, survey, nowSecs);
                    if (check.reason == Reason::Ok) {
                        check.file = file;
                    }
                    break;
                }
            }
        }
        checks.append(check);
    }
    return checks;
}

Result resultFor(Reason reason)
{
    switch (reason) {
    case Reason::Ok:
        return Result::Trashed;
    case Reason::NotAllowed:
        return Result::NotAllowed;
    case Reason::Missing:
        return Result::Missing;
    case Reason::Claimed:
        return Result::Claimed;
    case Reason::Installed:
        return Result::Installed;
    case Reason::Unverifiable:
        return Result::Unverifiable;
    case Reason::TooNew:
        return Result::TooNew;
    case Reason::Busy:
        return Result::Busy;
    case Reason::Unfinished:
        return Result::Unfinished;
    case Reason::Incomplete:
        break;
    }
    return Result::Incomplete;
}

QByteArray encodeRequest(const QStringList &names)
{
    return line(QJsonObject{{QStringLiteral("files"), QJsonArray::fromStringList(names)}});
}

bool decodeRequest(const QByteArray &data, QStringList *names)
{
    const QJsonDocument document = QJsonDocument::fromJson(data.trimmed());
    const QJsonValue files = document.object().value(QStringLiteral("files"));
    if (!document.isObject() || !files.isArray() || files.toArray().size() > kMaxRequest) {
        return false;
    }
    QStringList decoded;
    for (const QJsonValue &value : files.toArray()) {
        if (!value.isString() || !acceptedName(value.toString())) {
            return false;
        }
        decoded.append(value.toString());
    }
    if (names) *names = decoded;
    return true;
}

QByteArray encodeResult(const FileResult &result)
{
    QString name = QStringLiteral("incomplete");
    for (const ResultName &entry : kResultNames) {
        if (entry.result == result.result) {
            name = QLatin1String(entry.name);
        }
    }
    return line(QJsonObject{{QStringLiteral("file"), result.name},
                            {QStringLiteral("result"), name},
                            {QStringLiteral("bytes"), double(result.bytes)}});
}

QByteArray encodeDone()
{
    return line(QJsonObject{{QStringLiteral("done"), true}});
}

bool decodeReply(const QByteArray &data, FileResult *result, bool *done)
{
    if (done) *done = false;
    const QJsonDocument document = QJsonDocument::fromJson(data.trimmed());
    if (!document.isObject()) {
        return false;
    }
    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("done")).toBool()) {
        if (done) *done = true;
        return true;
    }
    const QString name = object.value(QStringLiteral("file")).toString();
    const QString text = object.value(QStringLiteral("result")).toString();
    if (!acceptedName(name)) {
        return false;
    }
    for (const ResultName &entry : kResultNames) {
        if (text == QLatin1String(entry.name)) {
            if (result) {
                result->name = name;
                result->result = entry.result;
                result->bytes = qint64(object.value(QStringLiteral("bytes")).toDouble());
            }
            return true;
        }
    }
    return false;
}

} // namespace InstallerOrphans
