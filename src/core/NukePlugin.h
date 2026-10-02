#ifndef MIGHTYTOOLS_CORE_NUKEPLUGIN_H
#define MIGHTYTOOLS_CORE_NUKEPLUGIN_H

#include <QList>
#include <QString>
#include <QStringList>

// Instala una carpeta de plugin en la `.nuke` del usuario, como hacen los toolpacks: una carpeta propia
// `<.nuke>/<carpeta>/` con los `.py` y un `VERSION` (la version DEL PLUGIN, no la de la app), y una linea
// `nuke.pluginAddPath('./<carpeta>')` al final del `init.py` de la `.nuke` (solo se agrega; el archivo
// del usuario nunca se reescribe). Cada herramienta que pone algo dentro de Nuke tiene su carpeta:
//   - Open in NukeX: `LGA_OpenInNukeX` (el servidor de .nk; modules/openinnukex/NukeBridge.h).
//   - Nuke Shortcuts: `LGA_NukeShortcuts` (el keyframe; modules/nukeshortcuts/KeyframePlugin.h).
// El payload viaja embebido en el exe (un .qrc por plugin en nuke_plugin/).
namespace NukePlugin {

/// Un archivo que una version vieja dejaba en la carpeta y la actual ya no trae. Se borra al
/// instalar SOLO si su contenido incluye `marker`: lo que no reconocemos como nuestro no se toca.
struct RetiredFile
{
    QString name;
    QString marker;
};

struct Spec
{
    QString folderName;              ///< carpeta dentro de `.nuke`
    QString resourcePrefix;          ///< ":/bridge", ":/nukeshortcuts"
    QStringList payloadFiles;        ///< lo que se copia (sin VERSION, que se escribe aparte)
    QStringList requiredFiles;       ///< lo que tiene que estar para darlo por instalado
    QList<RetiredFile> retiredFiles; ///< restos de versiones anteriores
    QString initComment;             ///< comentario que precede a la linea en el init.py
};

QString pluginAddPathLine(const Spec &spec);

/// Version que trae ESTE build (`<prefijo>/VERSION`). Vacio si el recurso no esta.
QString bundledVersion(const Spec &spec);

struct Status
{
    bool folderPresent = false;  ///< existe `<.nuke>/<carpeta>/`
    bool filesPresent = false;   ///< y adentro estan los archivos requeridos
    bool pathRegistered = false; ///< el `init.py` de `.nuke` ya tiene la linea activa
    QString installedVersion;    ///< contenido de `<.nuke>/<carpeta>/VERSION` (vacio si no hay)

    /// Instalado de verdad: los archivos estan Y Nuke los va a cargar.
    bool installed() const { return filesPresent && pathRegistered; }
};

enum class ChipState {
    NotInstalled,           ///< "Not installed"
    Installed,              ///< "Installed · v%1" (misma version o mas nueva)
    UpdateAvailable,        ///< "Update available · v%1" (la instalada es MENOR que la embebida)
    InstalledUnknownVersion ///< "Installed · unknown version" (VERSION ilegible, ausente o no numerica)
};

/// Funcion pura sobre `status` y la version embebida: el self-test la prueba sin instalar nada.
ChipState chipState(const Spec &spec, const Status &status);

/// Estado en esa carpeta. Con `nukeDir` vacio devuelve todo en false.
Status inspect(const Spec &spec, const QString &nukeDir);

enum class Error {
    None,
    DirMissing,     ///< la carpeta elegida no existe
    SourceRepo,     ///< la carpeta del plugin es un repo, o la .nuke es codigo fuente: instalar ahi lo pisaria
    PayloadMissing, ///< este build no trae el payload embebido
    WriteFailed,    ///< no se pudo copiar, crear la carpeta o tocar el init.py
};

/// Copia el payload y el VERSION a `<nukeDir>/<carpeta>/`, borra los restos reconocidos de versiones
/// anteriores y agrega la linea al `init.py` si falta. NO publica nada en el registro LGA: eso lo
/// decide quien llama (ver NukeBridge::install y su `automatedRun`). `detailForLog` (puede ser nullptr)
/// recibe el detalle tecnico en castellano.
Error install(const Spec &spec, const QString &nukeDir, QString *detailForLog);

/// Deja una copia de `<carpeta>/` en `destDir` para instalar a mano. No toca ningun `init.py`.
Error exportPayload(const Spec &spec, const QString &destDir, QString *detailForLog);

/// `~/.nuke` si existe; QString() si no.
QString detectNukeDirectory();

/// La `.nuke` con la que se trabaja: la del registro compartido LGA si hay una registrada y sigue
/// existiendo, y si no la detectada. Vacio si no hay ninguna.
QString currentNukeDirectory();

} // namespace NukePlugin

#endif // MIGHTYTOOLS_CORE_NUKEPLUGIN_H
