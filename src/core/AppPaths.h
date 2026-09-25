#ifndef MIGHTYTOOLS_APPPATHS_H
#define MIGHTYTOOLS_APPPATHS_H

#include <QString>

// Donde lee `config/` y donde escribe `debug.log`. Se decide en UN solo lugar, con
// LgaBuildTree::isBuildTree() (por estructura en disco, nunca por el nombre de la ruta):
//  - Arbol de build: la raiz del repo (la carpeta que tiene CMakeLists.txt y src/).
//  - Instalacion en Windows: la carpeta del exe ({app}), que el desinstalador se lleva.
//  - Instalacion en macOS: ~/Library/Application Support/LGA/LGA_MightyTools (nunca el .app).
//
// Funciona ANTES de construir la QApplication (el handler de log la usa desde el primer mensaje):
// la carpeta del exe sale de GetModuleFileNameW en Windows y de argv[0] en macOS, y no loguea nada.
namespace AppPaths {

// Guarda argv[0]. main() la llama primero de todo (macOS la necesita antes de la QApplication).
void init(const char *argv0);

QString exeDir();
bool isBuildTree();
QString rootDir();
QString configFile(const QString &name);
QString logFile();

} // namespace AppPaths

#endif // MIGHTYTOOLS_APPPATHS_H
