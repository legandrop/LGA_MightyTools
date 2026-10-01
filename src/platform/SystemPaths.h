#ifndef MIGHTYTOOLS_SYSTEMPATHS_H
#define MIGHTYTOOLS_SYSTEMPATHS_H

#include <QSet>
#include <QString>
#include <QStringList>

// Lo que la limpieza de Disk Space necesita saber del sistema: donde viven las carpetas del usuario
// (para las reglas), cuales no se tocan nunca, que programas corren, y como mostrar algo en el
// explorador de archivos.
//  - Windows (platform/win/SystemPathsWin.cpp): SHGetKnownFolderPath (las carpetas reales, aunque esten
//    movidas a otro disco o a OneDrive), toolhelp, SHOpenFolderAndSelectItems.
//  - macOS (platform/mac/SystemPathsMac.cpp): cleanupSupported() devuelve false; lo demas, vacio.
namespace SystemPaths {

// La limpieza esta disponible en esta plataforma. En macOS todavia no: el borrado no se probo.
bool cleanupSupported();

// Bases de las reglas de limpieza. Rutas con los separadores del sistema; vacia la que no existe.
struct CleanupBases
{
    QString profile;        ///< C:\Users\<usuario>
    QString localAppData;   ///< ...\AppData\Local
    QString roamingAppData; ///< ...\AppData\Roaming
    QString temp;           ///< la carpeta temporal del usuario
    QString systemRoot;     ///< C:\Windows
    // Carpetas de cache que el usuario movio con una variable de entorno (vacias si no).
    QString uvCacheDir;   ///< UV_CACHE_DIR
    QString pipCacheDir;  ///< PIP_CACHE_DIR
    QString cargoHome;    ///< CARGO_HOME
};
CleanupBases cleanupBases();

// Carpetas que nunca se borran ellas mismas ni se vacian enteras: la raiz de cada unidad, el perfil,
// sus carpetas conocidas (Escritorio, Documentos, Descargas, Imagenes, Musica, Videos), AppData y sus
// tres ramas, la carpeta de usuarios y la temporal.
QStringList protectedFolders();
// Arboles en los que no se borra NADA desde la ventana: Windows, Program Files, ProgramData, la carpeta
// desde la que corre la app y la de sus ajustes.
QStringList protectedTrees();
// Carpetas sincronizadas con la nube (OneDrive): borrar ahi borra en todos los dispositivos.
QStringList cloudFolders();

// Nombres de los programas que corren, en minusculas y sin ".exe".
QSet<QString> runningPrograms();

// Abre el explorador de archivos con ese item seleccionado. Inerte en una corrida automatizada.
void revealInFileManager(const QString &path);
// Abre la pantalla de limpieza del sistema (Windows: Configuracion > Almacenamiento). Inerte en una
// corrida automatizada.
void openSystemCleanup();

} // namespace SystemPaths

#endif // MIGHTYTOOLS_SYSTEMPATHS_H
