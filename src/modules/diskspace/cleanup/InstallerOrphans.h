#ifndef MIGHTYTOOLS_INSTALLERORPHANS_H
#define MIGHTYTOOLS_INSTALLERORPHANS_H

#include "platform/InstallerPackages.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

// Que copias de %WINDIR%\Installer son huerfanas: un .msi o .msp que ya no reclama ningun producto ni
// parche instalado. Si el programa sigue instalado, borrar su copia rompe su reparacion y su
// desinstalacion: por eso la regla es COMPLETA O NADA. Si no se pudo saber todo lo que Windows
// Installer tiene en uso, no se lista ningun archivo.
//
// Logica pura sobre lo que lee platform/InstallerPackages.h: la usan las reglas de limpieza (sin
// administrador) y el paso elevado que borra (con la consulta de todos los usuarios), y la prueba el
// self-test con datos armados.
namespace InstallerOrphans {

using InstallerPackages::FileFacts;
using InstallerPackages::Survey;

// Un paquete que cambio hace menos que esto puede ser una instalacion que todavia no se registro.
constexpr qint64 kMinAgeSecs = 24 * 3600;
// Tope de archivos de un pedido al paso elevado: corta un pedido armado para barrer la carpeta.
constexpr int kMaxRequest = 1000;
constexpr const char *kSystemSid = "S-1-5-18";

enum class Status {
    Ok,          ///< la consulta esta completa: la lista de huerfanos vale
    NeedsAdmin,  ///< no se pudo saber todo lo reclamado sin administrador (o fallo una lectura)
    Busy,        ///< hay una instalacion corriendo
    Unavailable, ///< no hay una carpeta de instaladores usable
};

// Que pasa con un archivo.
enum class Reason {
    Ok,         ///< huerfano: se puede ofrecer (y, en el paso elevado, mandar a la Papelera)
    NotAllowed, ///< no es un .msi/.msp suelto de primer nivel de la carpeta, es un enlace o tiene otro nombre
    Missing,    ///< ya no esta
    Claimed,    ///< lo usa un producto o un parche instalado
    TooNew,     ///< cambio hace menos de 24 h
    Busy,       ///< hay una instalacion corriendo
    Incomplete, ///< no se pudo verificar todo lo reclamado
};

struct Verdict
{
    Status status = Status::Unavailable;
    QList<FileFacts> orphans; ///< solo con Status::Ok
    qint64 bytes = 0;
};

// Solo el nombre de un .msi o .msp: sin carpetas, sin unidad, con algo antes de la extension.
bool acceptedName(const QString &name);
// `path` es un archivo directo de `dir` (no de una subcarpeta).
bool isTopLevel(const QString &path, const QString &dir);
// UserData tiene productos o parches de alguien que no es el sistema (S-1-5-18) ni `currentSid`.
// Sin `currentSid`, todo SID que no es el del sistema cuenta como ajeno.
bool hasForeignSids(const QStringList &sids, const QString &currentSid);

Status status(const Survey &survey);
// Un archivo de la lista, con la consulta ya completa (no mira status()).
Reason classify(const FileFacts &file, const Survey &survey, qint64 nowSecs);
// Los huerfanos, si la consulta esta completa; si no, ninguno.
Verdict evaluate(const Survey &survey, qint64 nowSecs);

// ---- El paso elevado

struct Check
{
    QString name;
    Reason reason = Reason::Incomplete;
    FileFacts file; ///< lo que se vio, con Reason::Ok
};
// Lo pedido contra una consulta de TODOS los usuarios: sin ella, nada pasa (Incomplete).
QList<Check> reverify(const Survey &survey, const QStringList &names, qint64 nowSecs);

enum class Result { Trashed, Claimed, TooNew, Busy, Incomplete, NotAllowed, Missing, TrashFailed };
struct FileResult
{
    QString name;
    Result result = Result::Incomplete;
    qint64 bytes = 0;
};
Result resultFor(Reason reason);

// Una linea JSON por mensaje. La app manda el pedido; el paso elevado contesta un resultado por archivo
// y al final "done".
QByteArray encodeRequest(const QStringList &names);
// false si la linea no es un pedido valido: un nombre que no es de un .msi/.msp, o demasiados.
bool decodeRequest(const QByteArray &line, QStringList *names);
QByteArray encodeResult(const FileResult &result);
QByteArray encodeDone();
// false si la linea no se entiende. `done`: es el cierre.
bool decodeReply(const QByteArray &line, FileResult *result, bool *done);

} // namespace InstallerOrphans

#endif // MIGHTYTOOLS_INSTALLERORPHANS_H
