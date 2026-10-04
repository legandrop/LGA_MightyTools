#ifndef MIGHTYTOOLS_DEBUGFLAGS_H
#define MIGHTYTOOLS_DEBUGFLAGS_H

#include <QString>

// Lee config/debug_flags.txt (clave=valor, # comenta). Se lee una sola vez y se cachea.
// Claves que usa la app:
//  - log=true           escribe debug.log (ver AppPaths::logFile()).
//  - dryRunInput=true   las acciones NO mueven el mouse ni aprietan teclas: solo loguean los pasos.
//                       Sirve para probar el registro de los atajos sin tocar Nuke.
//  - updateManifestUrl=http://127.0.0.1:<puerto>/versions.json
//  - updateDownloadBase=http://127.0.0.1:<puerto>/
//                       Prueban el updater entero contra un servidor en ESTA maquina, sin publicar
//                       nada: de donde se lee el manifiesto y de donde se baja el paquete (base +
//                       nombre del asset). Solo valen para 127.0.0.1 y localhost.
//  - updateInstallWithoutAsking=true
//                       Con updateManifestUrl puesto, el chequeo de arranque instala la version
//                       que encuentre sin mostrar el cartel. Sin updateManifestUrl no hace nada.
// Funcion MUDA: la usa el handler de mensajes desde el primer log.
namespace DebugFlags {

bool isOn(const QString &name);
// El valor tal cual esta escrito; vacio si la clave no esta.
QString value(const QString &name);

} // namespace DebugFlags

#endif // MIGHTYTOOLS_DEBUGFLAGS_H
