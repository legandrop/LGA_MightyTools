#ifndef MIGHTYTOOLS_DELETEGUARD_H
#define MIGHTYTOOLS_DELETEGUARD_H

#include <QString>
#include <QStringList>

// Las guardas del borrado, en un solo lugar y sin interfaz: lo que decide si una ruta se puede borrar.
// Las usa el trabajo de borrado ANTES de tocar nada (la ventana las usa ademas para habilitar o no sus
// botones, pero la que manda es la del trabajo).
//
// Todas las comparaciones son sobre rutas REALES (FileSystemOps::canonicalPath): nombres largos, sin
// unidades `subst`, sin enlaces intermedios. Asi "C:\WINDOWS", "c:/windows" y "C:\PROGRA~1" caen donde
// tienen que caer, y una carpeta que en realidad es un junction a otro disco queda fuera del volumen.
struct DeleteGuard
{
    enum class Verdict {
        Ok,
        Missing,           ///< no existe (o no se pudo abrir)
        OutsideVolume,     ///< no esta dentro del volumen que la ventana tiene abierto
        Protected,         ///< es una carpeta que nunca se borra (raiz de la unidad, perfil, Documentos...)
        ContainsProtected, ///< adentro tiene una carpeta protegida
        ProtectedTree,     ///< esta dentro de Windows, Program Files o ProgramData
        CloudFolder,       ///< carpeta sincronizada con la nube: solo a la Papelera
        BadNameForTrash,   ///< nombre terminado en punto o espacio: la Papelera podria tomar otro
        Link,              ///< es un enlace y la accion pedia entrar en el
        AutomatedRun,      ///< corrida automatizada fuera de la carpeta de pruebas
    };

    // Que se le va a hacer a la ruta.
    enum class Scope {
        Entire,   ///< se borra ella misma (carpeta o archivo)
        Contents, ///< se borra todo lo que tiene adentro; ella queda
        Children, ///< solo algunos de sus hijos directos, y cada uno se vuelve a revisar como Entire
    };

    QString volumeRoot; ///< real, sin separador final salvo la raiz ("C:\")
    QStringList protectedFolders;
    QStringList protectedTrees;
    QStringList cloudFolders;

    // Arma las guardas del volumen con lo que dice el sistema (SystemPaths).
    static DeleteGuard forVolume(const QString &volumeRoot);

    // `path` tiene que ser la ruta real. `toTrash`: va a la Papelera en vez de borrarse.
    Verdict check(const QString &path, Scope scope, bool toTrash) const;
    // Para la ventana: esa ruta se ve pero no se borra desde aca, ni a la Papelera.
    bool isViewOnly(const QString &path) const;

    // Rutas: `path` esta ESTRICTAMENTE dentro de `ancestor` (con limite de separador).
    static bool isInside(const QString &path, const QString &ancestor);
    static bool samePath(const QString &a, const QString &b);
    // Sin separador final (salvo la raiz), con los separadores del sistema.
    static QString clean(const QString &path);
};

#endif // MIGHTYTOOLS_DELETEGUARD_H
