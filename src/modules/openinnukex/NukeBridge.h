#ifndef MIGHTYTOOLS_OPENINNUKEX_NUKEBRIDGE_H
#define MIGHTYTOOLS_OPENINNUKEX_NUKEBRIDGE_H

#include "core/NukePlugin.h"

#include <QString>
#include <QStringList>

#include <functional>

// Nuke Bridge: el componente Python que corre DENTRO de NukeX (puerto 54325), portado de
// `~/.nuke/LGA_OpenInNukeX` (QtClient/src/nukebridge.{h,cpp}, v1.83). Diferencia con el origen:
// el payload (`init.py`, `LGA_QtAdapter_OpenInNukeX.py`, `VERSION`) ya no vive en una carpeta
// `bridge/` al lado del exe, sino embebido en el binario via `nuke_plugin/OpenInNukeXBridge.qrc`
// (D-04): se lee de `:/bridge/<archivo>` con QFile, que Qt trata igual que un archivo de disco.
//
// Las dos cosas que hace install():
//   1. Copiar los `.py` + un `VERSION` (con la version DEL PLUGIN, no la de la app) a
//      `<nukeDir>/LGA_OpenInNukeX/`.
//   2. Appendear una linea al `init.py` de `<nukeDir>` que agregue esa carpeta al plugin path
//      de Nuke (nunca reescribe el archivo del usuario).
//
// La carpeta `.nuke` elegida se publica en el registro compartido LGA (core/LgaRegistry.h) para
// que otras apps (PipeSync) la vean.
namespace NukeBridge {

/// La descripcion del plugin para core/NukePlugin: carpeta `LGA_OpenInNukeX` (contrato con Nuke,
/// Hiero y PipeSync), payload `:/bridge/*`, y los restos de la 1.84 (el keyframe de Nuke Shortcuts,
/// que desde la 1.85 tiene su carpeta) que se borran al actualizar.
const NukePlugin::Spec &plugin();

/// Nombre de la carpeta del plugin adentro de `.nuke`. No es configurable: el propio `init.py`
/// asume que se llama asi.
QString pluginFolderName();

/// La linea que tiene que estar en el `init.py` de `.nuke`.
QString pluginAddPathLine();

/// Version del plugin que trae ESTE build (contenido de `:/bridge/VERSION`), no la version de la
/// app. Vacio si el recurso no esta (build incompleto: ver Error::PayloadMissing).
QString bundledVersion();

using Status = NukePlugin::Status;
using ChipState = NukePlugin::ChipState;
using Error = NukePlugin::Error;

/// El estado del chip del panel. Funcion pura sobre `status` y la version embebida.
ChipState chipState(const Status &status);

/// `~/.nuke` si existe; QString() si no.
QString detectNukeDirectory();

/// La carpeta `.nuke` con la que trabaja el modulo: la del registro compartido LGA si hay una
/// registrada y sigue existiendo, y si no la detectada. Vacio si no hay ninguna.
QString currentNukeDirectory();

/// Estado del bridge en esa carpeta. Con `nukeDir` vacio devuelve todo en false.
Status inspect(const QString &nukeDir);

/// Publica `nukeDir` en el registro LGA compartido de verdad (`LgaRegistry::saveNukeDirectory`).
/// `install()` la llama por default; el self-test la reemplaza por una de prueba para poder
/// verificar CUANDO se llama sin tocar jamas `%APPDATA%\LGA\nuke.json`.
using RegistryPublisher = std::function<bool(const QString &nukeDir)>;
bool publishToLgaRegistry(const QString &nukeDir);

/// Copia los `.py` y el `VERSION` a `<nukeDir>/LGA_OpenInNukeX/`, agrega la linea al `init.py`
/// de `<nukeDir>` si falta, y publica la carpeta en el registro LGA llamando a `publisher` SALVO
/// que `automatedRun` sea true. `detailForLog` (puede ser nullptr) recibe el detalle tecnico en
/// castellano, para el log; la UI traduce el codigo de `Error` a un mensaje en ingles (ver
/// OpenInNukeXMessages.h).
///
/// `automatedRun` es OBLIGATORIO y sin default a proposito (ver ModuleContext::automatedRun()):
/// un incidente real (2026-09-25) fue que el self-test, instalando en una carpeta TEMPORAL para
/// probar la guarda de repo, terminaba igual escribiendo `%APPDATA%\LGA\nuke.json` — el registro
/// COMPARTIDO que leen otras apps LGA de verdad (PipeSync) — porque el guard anterior
/// (`publishToRegistry`) era un parametro con default `true` que alguien tenia que acordarse de
/// desactivar. Sin default, cada llamador (panel real, self-test) tiene que decidir explicitamente
/// y a proposito. El panel pasa `context().automatedRun()`; el self-test, `true` siempre. Instalar
/// en una carpeta TEMPORAL sigue escribiendo esos archivos de verdad (es lo que prueba la guarda de
/// repo): lo unico que `automatedRun` bloquea es la publicacion en el registro COMPARTIDO.
///
/// `publisher` inyecta QUIEN publica (default: el registro LGA real). El self-test inyecta una
/// funcion de prueba propia para demostrar, con `automatedRun` en true y en false, que la
/// publicacion se llama o no se llama, SIN escribir ni leer jamas el `nuke.json` real.
Error install(const QString &nukeDir, QString *detailForLog, bool automatedRun,
              RegistryPublisher publisher = publishToLgaRegistry);

/// Deja una copia de `LGA_OpenInNukeX/` en `destDir` para instalar a mano (panel manual del
/// inventario). No toca ningun `init.py` ni el registro LGA: no necesita `automatedRun`.
Error exportPayload(const QString &destDir, QString *detailForLog);

} // namespace NukeBridge

#endif // MIGHTYTOOLS_OPENINNUKEX_NUKEBRIDGE_H
