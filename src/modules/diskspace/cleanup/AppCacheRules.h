#ifndef MIGHTYTOOLS_APPCACHERULES_H
#define MIGHTYTOOLS_APPCACHERULES_H

#include "modules/diskspace/cleanup/CleanupRules.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

class ScanTree;

// Las caches de los programas de video, audio y VFX: Nuke, Adobe (Media Cache y Disk Cache de After
// Effects), DaVinci Resolve, Ableton Live, Topaz Video, REAPER, Unreal Engine, Redshift, Premiere Pro,
// Blender, Photoshop y CUDA. El usuario suele moverlas a otro disco: las rutas salen de las preferencias
// de cada programa (archivos de texto, el registro o un plist, todo de solo lectura), y las de los
// programas cuyas preferencias no se pueden leer se reconocen por su forma en el arbol del escaneo.
//
// Reglas propias, ademas de las de CleanupRules.h (encargo del 2026-11-03, a partir del caso de Topaz
// Video 1.6.0, que con la carpeta de proyectos movida vacio media unidad):
//  - Nunca se ofrece la carpeta que dice una preferencia: solo sus subcarpetas (o archivos) conocidas,
//    cada una con su marca. Sin marca, no hay item.
//  - Una carpeta leida de una preferencia se descarta (acceptedBase) si es la raiz de un volumen, la
//    carpeta del usuario o una de sus carpetas conocidas (o las contiene), una carpeta de datos raiz
//    (AppData, Application Support...) o la de un programa (su hija directa), esta en `.nuke`, o tiene
//    adentro la marca de un proyecto.
//  - Al lado de algunas caches hay licencias (Redshift, el `auth.tpz` de Topaz, los `.hid` de Foundry):
//    nunca se ofrecen.
//  - Cada item lleva los procesos (Windows) o identificadores de app (mac) que lo bloquean.
namespace AppCacheRules {

// Suma a las categorias del sistema lo que no depende del escaneo. Lo que va con categorias que ya existen
// ("media", "shaders") se agrega a ellas; lo demas va en categorias propias.
void add(QList<Cleanup::Category> &categories, const CleanupRules::Context &context);
// Con el arbol del escaneo (con su candado tomado): Resolve, previews de Premiere Pro y proxies de Blender.
void addScanned(QList<Cleanup::Category> &categories, const ScanTree &tree, const CleanupRules::Context &context);

// ---- Piezas, expuestas para el self-test.

// El valor de una preferencia de Nuke (`preferences<ver>.nk`, sintaxis TCL), sin comillas ni llaves y con
// los escapes resueltos (`\[` queda `[`). `found` dice si la linea estaba.
QString nukeValue(const QString &text, const QString &key, bool *found);
// Resuelve `[getenv X]`. Vacio si la variable no tiene valor o queda algo que no se sabe resolver.
QString resolveNukeExpression(const QString &value, const std::function<QString(const QString &)> &getenv);
// Las carpetas de la seccion "Disk Cache Controls" de un `Adobe After Effects <ver> Prefs.txt`.
QStringList afterEffectsCacheFolders(const QString &prefsText);
// `CacheDirectory` del bloque `FileCachePrefData` de un `Preferences.cfg` de Ableton Live (binario).
// Vacio si los bytes no cierran con una ruta absoluta.
QString abletonCacheDirectory(const QByteArray &cfg);
// `altpeakspath` de un `reaper.ini`, solo con `altpeaks` activado.
QString reaperPeaksFolder(const QString &ini);
// `GlobalLocalDDCPath` de un `EditorSettings.ini` de Unreal Engine.
QString unrealLocalDdcPath(const QString &ini);
// La carpeta que dice una preferencia, si sirve como base: su ruta real. Vacia si se rechaza.
QString acceptedBase(const QString &path, const CleanupRules::Context &context);
// En su primer nivel tiene la marca de un proyecto (`.nk`, `.prproj`, `.aep`, `.drp`, `.als`,
// `project.tvai`...).
bool hasProjectMarker(const QString &dir);

} // namespace AppCacheRules

#endif // MIGHTYTOOLS_APPCACHERULES_H
