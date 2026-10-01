#ifndef MIGHTYTOOLS_CLEANUPMODEL_H
#define MIGHTYTOOLS_CLEANUPMODEL_H

#include <QList>
#include <QString>
#include <QStringList>

// Lo que muestra la pestana "Clean up": categorias que se despliegan en renglones (items), cada uno
// con su casilla. Tipos de datos, sin logica: los arma CleanupRules, los pinta CleanupPane y los
// ejecuta CleanupJob.
namespace Cleanup {

enum class Group {
    Safe,  ///< "Safe to delete": caches que se regeneran; arranca tildado
    Admin, ///< "Needs administrator": en esta etapa solo se informa
    Yours, ///< "Yours to decide": cosas del usuario; arranca destildado
};

enum class Action {
    Contents,    ///< lo que hay adentro de la carpeta; la carpeta queda
    Entire,      ///< la carpeta (o el archivo) entera
    OldChildren, ///< los hijos directos con `minAgeDays` o mas sin cambios y sin nada en uso
    RecycleBin,  ///< la Papelera del volumen, por su API
};

// Un lugar concreto del disco que un item limpia.
struct Target
{
    QString path; ///< ruta real, separadores del sistema
    Action action = Action::Contents;
    int minAgeDays = 0;
};

struct Item
{
    QString name; ///< "uv cache", "Brave"
    QString path; ///< la ruta que se muestra
    QList<Target> targets;
    // Programas (en minusculas, sin ".exe") que no pueden estar corriendo mientras se limpia.
    QStringList blockers;
    QString blockerLabel; ///< como se nombra en "Brave is open"
    // Arranca destildado aunque su grupo sea el seguro: una cache reconocida por su forma en un lugar
    // que no es el de fabrica (puede ser la cache privada de otra herramienta).
    bool optional = false;

    // ---- Lo medido
    bool measured = false;
    qint64 bytes = 0;
    qint64 files = 0;
    qint64 newest = 0;       ///< modificacion mas nueva (segundos desde 1970)
    qint64 keptBytes = 0;    ///< OldChildren: lo que queda afuera por nuevo
    // ---- Estado
    bool checked = false;
    bool blocked = false; ///< un programa de `blockers` esta corriendo: no se puede tildar
};

struct Category
{
    QString id; ///< estable: "python", "browsers", "rule:3"
    Group group = Group::Safe;
    QString title;
    QString caption;
    bool single = false;     ///< un solo item, sin desplegar (Papelera, temporales)
    bool info = false;       ///< sin casilla: solo informa
    bool measurable = true;  ///< false: se muestra sin peso ("Windows Update leftovers")
    QString reviewPath;      ///< info con "Review": la carpeta que se abre en Folders
    QList<Item> items;
};

// Carpetas que el usuario declaro descartables ("Add a folder rule...").
struct FolderRule
{
    QString name;  ///< titulo de la categoria; vacio: el nombre de la carpeta
    QString base;  ///< carpeta
    QString match; ///< vacio: `base` misma; si no, toda carpeta con ese nombre dentro de `base`
};

} // namespace Cleanup

#endif // MIGHTYTOOLS_CLEANUPMODEL_H
