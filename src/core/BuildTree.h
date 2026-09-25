#ifndef MIGHTYTOOLS_BUILDTREE_H
#define MIGHTYTOOLS_BUILDTREE_H

#include <QString>

/**
 * ¿La app corre desde un ARBOL DE BUILD o desde una instalacion?
 *
 * De la respuesta depende donde se busca `config/` y donde se escribe `debug.log`: en el arbol de
 * build cuelgan de la raiz del repo; en una instalacion, de la carpeta del ejecutable (Windows) o
 * de Application Support (macOS, nunca adentro del `.app`).
 *
 * Es la UNICA funcion que decide esto. Ningun otro archivo repite la heuristica.
 *
 * 🔴 Nunca se decide por el NOMBRE de la ruta (`appDir.contains("build")`): una instalacion en
 * `D:\builds\App` se daria por arbol de build y resolveria todo una carpeta mas arriba.
 *
 * Criterio (copia de LGA_Base_QT_C_Py, que lo toma de LGA_MediaTools_v2):
 *  1. `LGA_BUILD_TREE` en el entorno fuerza la respuesta. Solo valores explicitos:
 *     `1`/`true`/`yes`/`on` y `0`/`false`/`no`/`off`. Cualquier otra cosa se ignora con un aviso.
 *  2. Si no hay override, hacen falta LAS DOS cosas:
 *     - un `CMakeCache.txt` en la carpeta del ejecutable o hasta DOS niveles arriba (TRES en
 *       macOS, por `App.app/Contents/MacOS/`), y
 *     - que el padre de esa carpeta tenga `CMakeLists.txt` y `src/`: la forma de la raiz del repo.
 *     `deploy/` cuelga de la raiz del repo, asi que NO puede tener un `CMakeCache.txt`.
 *  3. Si no, instalacion.
 *
 * Funcion MUDA salvo por `qWarning`: la llama la resolucion de la carpeta de logs.
 */
namespace LgaBuildTree {

/// `appDir` es la carpeta del ejecutable. El resultado se cachea por ruta.
bool isBuildTree(const QString &appDir);

/// Repite el aviso de un `LGA_BUILD_TREE` invalido (no cambia ninguna respuesta). main() la
/// llama despues de instalar su handler de mensajes, para que el aviso llegue al log.
void warnIfInvalidOverride();

/// Solo para tests: vacia el cache de respuestas.
void clearCache();

} // namespace LgaBuildTree

#endif // MIGHTYTOOLS_BUILDTREE_H
