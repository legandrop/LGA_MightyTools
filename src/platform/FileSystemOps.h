#ifndef MIGHTYTOOLS_FILESYSTEMOPS_H
#define MIGHTYTOOLS_FILESYSTEMOPS_H

#include "platform/DirEnumerator.h"

#include <QString>

// Lo minimo del sistema de archivos que la limpieza de Disk Space necesita y Qt no da con la garantia
// que hace falta: saber si una ruta es un enlace SIN seguirlo, borrar un archivo o una carpeta vacia (o
// el enlace mismo, nunca su destino), y la ruta e identidad reales de algo que existe.
//  - Windows: platform/win/FileSystemOpsWin.cpp (rutas \\?\, DeleteFileW, RemoveDirectoryW).
//  - macOS:   platform/mac/FileSystemOpsMac.cpp (lstat, unlink, rmdir).
namespace FileSystemOps {

enum class Kind {
    Missing,
    File,
    Dir,
    Link, ///< junction, symlink o punto de montaje (de carpeta o de archivo)
};
// Que hay en esa ruta, sin seguir un enlace final.
Kind kind(const QString &path);

enum class Result {
    Removed,
    Missing,  ///< ya no estaba
    InUse,    ///< otro programa lo tiene abierto
    Denied,   ///< sin permiso
    NotEmpty, ///< la carpeta todavia tiene algo
    Failed,
};
// Ruta nativa de un archivo o de un enlace (sin separador final), para removeFile / removeDir.
DirEnumerator::NativeString nativePath(const QString &path);
// Borra un archivo o un enlace de archivo. Le saca el solo-lectura si hace falta.
// En una corrida automatizada solo actua dentro de la carpeta de pruebas (core/AutomatedRun.h): fuera
// de ella devuelve Denied sin tocar nada. En macOS el borrado esta deshabilitado (sin probar): Failed.
Result removeFile(const DirEnumerator::NativeString &path);
// Borra una carpeta VACIA, o un enlace de carpeta (el enlace: lo que hay del otro lado no se toca).
// Misma guarda de corrida automatizada que removeFile.
Result removeDir(const DirEnumerator::NativeString &path);
// Otro programa tiene abierto ese archivo (o no se puede abrir para borrarlo). No lo modifica.
bool isInUse(const DirEnumerator::NativeString &path);
// Es un archivo de la nube sin bajar (OneDrive, Dropbox, Drive): borrarlo lo borra en la nube.
bool isCloudPlaceholder(const DirEnumerator::NativeString &path);

// La ruta real de algo que existe: nombres largos (no 8.3), mayusculas como en el disco, sin unidades
// `subst` ni enlaces en los tramos intermedios. Si el ultimo tramo es un enlace, es la ruta del enlace.
// Vacia si no existe o no se pudo abrir.
QString canonicalPath(const QString &path);

// Identidad de un archivo o carpeta dentro de su volumen: dos rutas con la misma identidad son lo mismo
// (una app empaquetada puede mostrar una carpeta por dos rutas).
struct Identity
{
    quint64 volume = 0;
    quint64 file = 0;
    bool valid = false;
    bool operator==(const Identity &other) const { return valid && other.valid && volume == other.volume && file == other.file; }
};
Identity identity(const QString &path);

// Crea un enlace de carpeta `link` que apunta a `target` (junction en Windows). Lo usa el self-test
// para comprobar, con un enlace de verdad, que ni el escaneo ni el borrado lo siguen. Misma guarda de
// corrida automatizada que removeFile.
bool createDirLink(const QString &link, const QString &target);

} // namespace FileSystemOps

#endif // MIGHTYTOOLS_FILESYSTEMOPS_H
