#ifndef MIGHTYTOOLS_DEBUGFLAGS_H
#define MIGHTYTOOLS_DEBUGFLAGS_H

#include <QString>

// Lee config/debug_flags.txt (clave=valor, # comenta). Se lee una sola vez y se cachea.
// Claves que usa la app:
//  - log=true           escribe debug.log (ver AppPaths::logFile()).
//  - dryRunInput=true   las acciones NO mueven el mouse ni aprietan teclas: solo loguean los pasos.
//                       Sirve para probar el registro de los atajos sin tocar Nuke.
// Funcion MUDA: la usa el handler de mensajes desde el primer log.
namespace DebugFlags {

bool isOn(const QString &name);

} // namespace DebugFlags

#endif // MIGHTYTOOLS_DEBUGFLAGS_H
