#ifndef MIGHTYTOOLS_INSTALLERPACKAGES_H
#define MIGHTYTOOLS_INSTALLERPACKAGES_H

#include "platform/FileSystemOps.h"

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QXmlStreamReader>

// Windows Installer guarda en %WINDIR%\Installer una copia de cada .msi y .msp de lo instalado, para
// reparar y desinstalar. Esto lee, SIN tocar nada, lo que hace falta para saber cuales de esas copias
// ya no reclama ningun producto ni parche: la carpeta, lo que Windows Installer tiene en uso (su API,
// HKLM\...\Installer\UserData y los enlaces de origen), que producto es cada paquete suelto y si ese
// producto sigue instalado, y si hay una instalacion corriendo o a medio terminar.
//
// Solo Windows (platform/win/InstallerPackagesWin.cpp). Los tipos son de datos puros: la decision
// (que es huerfano) la toma modules/diskspace/cleanup/InstallerOrphans, que el self-test prueba con
// datos armados en las dos plataformas.
namespace InstallerPackages {

// Solo el nombre de un .msi o .msp: sin carpetas, sin unidad, con algo antes de la extension. Pura.
inline bool isPackageName(const QString &name)
{
    if (name.size() < 5 || name.size() > 255 || name.startsWith(QLatin1Char('.'))) {
        return false;
    }
    for (const QChar c : name) {
        if (c == QLatin1Char('\\') || c == QLatin1Char('/') || c == QLatin1Char(':') || c.unicode() < 0x20) {
            return false;
        }
    }
    return name.endsWith(QLatin1String(".msi"), Qt::CaseInsensitive) || name.endsWith(QLatin1String(".msp"), Qt::CaseInsensitive);
}

// Que producto es un paquete que nadie reclama, y si ese producto (en cualquier version) sigue
// instalado. Una copia de un programa instalado nunca se ofrece, aunque Windows Installer no la use:
// solo se lee para los paquetes que no estan reclamados.
struct PackageFacts
{
    bool read = false;         ///< se leyo lo que identifica al paquete (sin esto, no se ofrece)
    bool patch = false;        ///< .msp
    QString productCode;       ///< .msi: ProductCode de su tabla Property
    QStringList upgradeCodes;  ///< .msi: su UpgradeCode (puede no tener); .msp: los de sus productos destino
    QString productName;       ///< .msi: ProductName y ProductVersion, para el motivo
    QStringList targets;       ///< .msp: los productos a los que se aplica (PID_TEMPLATE y su XML)
    QStringList installed;     ///< productos instalados con ese ProductCode o UpgradeCode, o destino del parche
    QString installedName;     ///< nombre y version del primero ("Topaz Video 1.7.1")
    bool lookupFailed = false; ///< una consulta de lo instalado fallo
};

// Un archivo de primer nivel de la carpeta de instaladores, como lo vio el sistema.
struct FileFacts
{
    QString name;         ///< "a199bd5.msi"
    QString path;         ///< carpeta real + nombre
    bool isFile = false;  ///< archivo (no carpeta)
    bool isLink = false;  ///< punto de reanalisis (symlink, junction...)
    quint32 links = 0;    ///< enlaces duros; 0 si no se pudo leer
    FileSystemOps::Identity id;
    qint64 bytes = 0;
    qint64 created = 0;  ///< segundos desde 1970
    qint64 modified = 0;
    PackageFacts package; ///< solo de los que nadie reclama
    QString author;  ///< del resumen del paquete, solo para mostrar (lo llena quien lo pide)
    QString subject;
};

// Como se comparan las rutas de Windows: separadores '\', sin el prefijo \\?\ ni la barra final, sin
// distinguir mayusculas. Pura (el self-test la usa tambien en mac).
inline QString pathKey(const QString &path)
{
    QString text = path;
    text.replace(QLatin1Char('/'), QLatin1Char('\\'));
    if (text.startsWith(QLatin1String("\\\\?\\"))) {
        text.remove(0, 4);
    }
    while (text.size() > 3 && text.endsWith(QLatin1Char('\\'))) {
        text.chop(1);
    }
    return text.toCaseFolded();
}

// Las cadenas de un REG_MULTI_SZ leido entero (con sus '\0' adentro). Un renombre pendiente que es un
// borrado tiene el destino vacio: las cadenas vacias se saltean, sin cortar ahi. Pura.
inline QStringList splitMultiString(const QString &raw)
{
    QStringList lines;
    for (const QString &line : raw.split(QChar(0))) {
        if (!line.isEmpty()) {
            lines.append(line);
        }
    }
    return lines;
}

// Los productos destino de un parche, segun su XML de aplicabilidad (MsiExtractPatchXMLData). `complete`:
// el XML se entendio, tiene algun destino y cada destino trae su UpgradeCode. Pura.
struct PatchTargets
{
    QStringList products;
    QStringList upgradeCodes;
    bool complete = false;
};
inline PatchTargets parsePatchXml(const QString &xml)
{
    PatchTargets result;
    QXmlStreamReader reader(xml);
    int targets = 0;
    bool missing = false;
    bool inTarget = false;
    QString upgrade;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            if (reader.name() == QLatin1String("TargetProduct")) {
                inTarget = true;
                upgrade.clear();
                ++targets;
            } else if (inTarget && reader.name() == QLatin1String("TargetProductCode")) {
                const QString code = reader.readElementText().trimmed();
                if (!code.isEmpty() && !result.products.contains(code, Qt::CaseInsensitive)) {
                    result.products.append(code);
                }
            } else if (inTarget && reader.name() == QLatin1String("UpgradeCode")) {
                upgrade = reader.readElementText().trimmed();
            }
        } else if (reader.isEndElement() && reader.name() == QLatin1String("TargetProduct")) {
            inTarget = false;
            if (upgrade.isEmpty()) {
                missing = true;
            } else if (!result.upgradeCodes.contains(upgrade, Qt::CaseInsensitive)) {
                result.upgradeCodes.append(upgrade);
            }
        }
    }
    result.complete = !reader.hasError() && targets > 0 && !missing;
    return result;
}

// Una entrada de PendingFileRenameOperations ("\??\C:\...", a veces con "!" adelante) cae dentro de
// `installerDir` (o es esa carpeta). Pura.
inline bool renameTouches(const QString &entry, const QString &installerDir)
{
    QString path = entry.trimmed();
    if (path.startsWith(QLatin1Char('!'))) {
        path.remove(0, 1);
    }
    if (path.startsWith(QLatin1String("\\??\\"))) {
        path.remove(0, 4);
    }
    const QString key = pathKey(path);
    const QString folder = pathKey(installerDir);
    return !key.isEmpty() && !folder.isEmpty() && (key == folder || key.startsWith(folder + QLatin1Char('\\')));
}

// Un codigo de producto como clave: mayusculas, sin espacios. Pura.
inline QString codeKey(const QString &code)
{
    return code.trimmed().toUpper();
}

// Lo que Windows Installer tiene en uso: rutas normalizadas con pathKey() e identidades de archivo (la
// misma copia por otro nombre tambien cuenta).
struct Claims
{
    QSet<QString> paths;
    QList<FileSystemOps::Identity> ids;
    bool isEmpty() const { return paths.isEmpty() && ids.isEmpty(); }
};

// Todo lo leido en una pasada.
struct Survey
{
    QString installerDir;     ///< ruta real; vacia si no existe, es un enlace o no se pudo resolver
    bool listed = false;      ///< la carpeta se listo entera
    QList<FileFacts> files;   ///< solo los .msi y .msp de primer nivel (lo demas no se mira)
    bool busy = true;         ///< hay una instalacion corriendo o a medio terminar, o no se pudo saber
    bool busyRunning = true;  ///< corriendo (el mutex), o no se pudo saber
    bool busyUnfinished = false; ///< suspendida (InProgress) o esperando un reinicio (renombres pendientes)
    QString busyReason;       ///< cual de las senales, para el log
    bool allUsers = false;    ///< la API se consulto para todos los usuarios (pide administrador)
    bool enumerated = false;  ///< todas las enumeraciones terminaron en ERROR_NO_MORE_ITEMS
    QString enumerationError; ///< la primera que fallo, para el log
    bool userDataRead = false; ///< HKLM\...\Installer\UserData se leyo entero
    QString currentSid;
    QStringList userDataSids; ///< los SIDs de UserData con algun producto o parche
    int products = 0;         ///< los que dio la API
    int patches = 0;
    int withoutPackage = 0;   ///< productos o parches sin copia local registrada
    int sourceLinks = 0;      ///< enlaces de DerandomizedSymbolicLinksForSourceLists resueltos
    QSet<QString> installedProducts; ///< los ProductCode que dio la API, con codeKey()
    Claims claims;
};

enum class Scope {
    CurrentUser, ///< la maquina y el usuario actual: anda sin administrador
    AllUsers,    ///< todos los usuarios: solo con administrador
};

// Lee la carpeta, la API (en el alcance pedido), UserData, los enlaces de origen, el producto de cada
// paquete sin reclamar y si hay una instalacion en curso. Solo lectura.
Survey survey(Scope scope);
// Si hay una instalacion de Windows Installer corriendo (el mutex Global\_MSIExecute) o sin terminar:
// suspendida (Installer\InProgress) o con un reemplazo de archivos pendiente dentro de `installerDir`
// (PendingFileRenameOperations, espera un reinicio). Ante la duda, ocupado.
struct BusyState
{
    bool running = false;    ///< corriendo, o no se pudo saber
    bool unfinished = false; ///< suspendida o esperando un reinicio, o no se pudo saber
    QString reason;          ///< cual de las senales, para el log
    bool any() const { return running || unfinished; }
};
BusyState installState(const QString &installerDir);
// Autor y asunto del resumen del paquete ("Boris FX, Inc.", el nombre del producto). Vacios si no se
// pudo leer.
void summary(const QString &path, QString *author, QString *subject);

} // namespace InstallerPackages

#endif // MIGHTYTOOLS_INSTALLERPACKAGES_H
