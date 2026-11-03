#ifndef MIGHTYTOOLS_INSTALLERPACKAGES_H
#define MIGHTYTOOLS_INSTALLERPACKAGES_H

#include "platform/FileSystemOps.h"

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

// Windows Installer guarda en %WINDIR%\Installer una copia de cada .msi y .msp de lo instalado, para
// reparar y desinstalar. Esto lee, SIN tocar nada, lo que hace falta para saber cuales de esas copias
// ya no reclama ningun producto ni parche: la carpeta, lo que Windows Installer tiene en uso (su API y
// HKLM\...\Installer\UserData) y si hay una instalacion corriendo.
//
// Solo Windows (platform/win/InstallerPackagesWin.cpp). Los tipos son de datos puros: la decision
// (que es huerfano) la toma modules/diskspace/cleanup/InstallerOrphans, que el self-test prueba con
// datos armados en las dos plataformas.
namespace InstallerPackages {

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
    bool busy = true;         ///< hay una instalacion corriendo, o no se pudo saber
    bool allUsers = false;    ///< la API se consulto para todos los usuarios (pide administrador)
    bool enumerated = false;  ///< todas las enumeraciones terminaron en ERROR_NO_MORE_ITEMS
    QString enumerationError; ///< la primera que fallo, para el log
    bool userDataRead = false; ///< HKLM\...\Installer\UserData se leyo entero
    QString currentSid;
    QStringList userDataSids; ///< los SIDs de UserData con algun producto o parche
    int products = 0;         ///< los que dio la API
    int patches = 0;
    int withoutPackage = 0;   ///< productos o parches sin copia local registrada
    Claims claims;
};

enum class Scope {
    CurrentUser, ///< la maquina y el usuario actual: anda sin administrador
    AllUsers,    ///< todos los usuarios: solo con administrador
};

// Lee la carpeta, la API (en el alcance pedido), UserData y el mutex de instalacion. Solo lectura.
Survey survey(Scope scope);
// Hay una instalacion de Windows Installer corriendo (el mutex Global\_MSIExecute existe). Ante la
// duda, true.
bool installInProgress();
// Autor y asunto del resumen del paquete ("Boris FX, Inc.", el nombre del producto). Vacios si no se
// pudo leer.
void summary(const QString &path, QString *author, QString *subject);
// Los nombres de los .msi/.msp de primer nivel y su informacion, de una carpeta ya resuelta.
QList<FileFacts> listPackages(const QString &installerDir, bool *listed);

} // namespace InstallerPackages

#endif // MIGHTYTOOLS_INSTALLERPACKAGES_H
