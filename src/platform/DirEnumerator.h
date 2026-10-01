#ifndef MIGHTYTOOLS_DIRENUMERATOR_H
#define MIGHTYTOOLS_DIRENUMERATOR_H

#include <QString>

#include <functional>
#include <string>
#include <vector>

// Listado rapido de UNA carpeta: nombre, tipo, tamano y fecha de cada entrada, tal como los guarda el
// indice del directorio (sin abrir ningun archivo). Es la pieza con la que el motor de Disk Space
// recorre un disco entero sin permiso de administrador.
//  - Windows (platform/win/DirEnumeratorWin.cpp): GetFileInformationByHandleEx con
//    FileFullDirectoryInfo y un buffer grande. Trae el tamano asignado en disco. FileIdBothDirectoryInfo
//    (con id de archivo) se midio 4 veces mas lento en un disco de 4,7 M de archivos: no se usa.
//  - macOS (platform/mac/DirEnumeratorMac.cpp): readdir + fstatat sin seguir enlaces.
//
// Las rutas viajan en la codificacion del sistema (UTF-16 en Windows, UTF-8 en mac) para no convertir
// millones de nombres; nativeDir() / toDisplay() pasan de y a QString en los bordes.
namespace DirEnumerator {

#ifdef Q_OS_WIN
using NativeChar = wchar_t;
#else
using NativeChar = char;
#endif
using NativeString = std::basic_string<NativeChar>;

struct Entry
{
    const NativeChar *name = nullptr; ///< valido solo durante la visita
    int nameLength = 0;
    bool isDir = false;
    // Junction, symlink, punto de montaje o cualquier otro enlace de nombre: no se recorre ni se cuenta
    // (lo que hay del otro lado no es de esta carpeta, y puede ser otro volumen).
    bool isLink = false;
    // Archivo o carpeta de nube que no esta bajado (OneDrive Files On-Demand): listar la carpeta la
    // traeria de la red, y borrarlo lo borra de la nube y de los demas dispositivos.
    bool isCloud = false;
    quint64 size = 0;      ///< tamano logico
    quint64 allocated = 0; ///< lo que ocupa en disco
    qint64 modifiedSecs = 0; ///< segundos desde 1970
    qint64 createdSecs = 0;  ///< 0 si el sistema no la da
};

using Visitor = std::function<void(const Entry &)>;

// Ruta nativa de una carpeta, terminada en separador: lista para enumerate() y para pegarle el nombre
// de un hijo. Acepta "C:/Users", "C:\\Users" o "/Volumes/Cache".
NativeString nativeDir(const QString &path);
// Ruta para mostrar y para las APIs de Qt: separadores del sistema, sin prefijos, sin separador final
// (salvo la raiz: "C:\\" o "/").
QString toDisplay(const NativeString &path);
QString nameToString(const NativeChar *name, int length);
NativeString nameFromString(const QString &name);
// Agrega el nombre a `out` en UTF-16 y devuelve cuantas unidades agrego (en Windows es una copia).
int appendUtf16(std::u16string &out, const NativeChar *name, int length);
NativeChar separator();

// Lista `dir` (ruta de nativeDir). `scratch` es un buffer que el llamador reutiliza entre llamadas (uno
// por hilo). Devuelve false si la carpeta no se pudo abrir (sin permiso, desaparecio); las entradas "."
// y ".." nunca llegan a `visit`.
bool enumerate(const NativeString &dir, std::vector<char> &scratch, const Visitor &visit);

} // namespace DirEnumerator

#endif // MIGHTYTOOLS_DIRENUMERATOR_H
