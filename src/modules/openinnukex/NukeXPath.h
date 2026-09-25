#ifndef MIGHTYTOOLS_OPENINNUKEX_NUKEXPATH_H
#define MIGHTYTOOLS_OPENINNUKEX_NUKEXPATH_H

#include "modules/openinnukex/NukeScanner.h"

#include <QList>
#include <QString>

// La ruta del ejecutable de NukeX preferido: UN CONTRATO con PipeSync (plan 4.7, seccion 2).
//
// Vive SIEMPRE en `nukeXpath.txt`, nunca en settings.ini:
//   Windows  %APPDATA%\LGA\OpenInNukeX\nukeXpath.txt
//   macOS    ~/Library/Application Support/LGA/OpenInNukeX/nukeXpath.txt
//
// La carpeta es `OpenInNukeX`, NO `LGA_MightyTools`: es la misma que ya lee PipeSync
// (UserAccountPanel.cpp, py_scr/open_nuke_latest_version.py) y la que escribia el cliente viejo.
// Primera linea del archivo = ruta al ejecutable, sin mas formato.
namespace NukeXPath {

// %APPDATA%/LGA/OpenInNukeX/nukeXpath.txt (o el equivalente de mac), a partir de
// LgaRegistry::directory() (el mismo ".../LGA" que usan las demas apps). Vacio si no se pudo
// resolver el directorio de datos del usuario.
QString defaultFilePath();

// Primera linea de `filePath`, sin espacios en las puntas. QString() si el archivo no existe,
// no se pudo leer o esta vacio.
QString read(const QString &filePath);

// Escribe `nukePath` como primera (y unica) linea de `filePath`, creando la carpeta si hace
// falta. No valida que el ejecutable exista: eso lo decide quien llama.
bool write(const QString &filePath, const QString &nukePath);

// Auto-reparacion de ruta muerta + autodeteccion (inventario: healStalePath, configwindow.cpp:1549-1574).
// Si `currentPath` esta vacio o ya no existe en disco, y `versions` no esta vacio, reemplaza por
// la version MAS NUEVA de `versions` (NukeScanner::isNewer), la escribe en `filePath` y la
// devuelve. Si `currentPath` sigue siendo un archivo valido, no toca nada y lo devuelve tal cual.
// Sin versiones para reponer y sin ruta valida, devuelve QString() sin escribir.
QString healStalePath(const QString &filePath, const QString &currentPath,
                       const QList<NukeVersion> &versions);

} // namespace NukeXPath

#endif // MIGHTYTOOLS_OPENINNUKEX_NUKEXPATH_H
