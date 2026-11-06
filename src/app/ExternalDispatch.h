#ifndef MIGHTYTOOLS_EXTERNALDISPATCH_H
#define MIGHTYTOOLS_EXTERNALDISPATCH_H

#include "app/Module.h"

#include <QList>
#include <QString>
#include <QUrl>

class SettingsStore;

// Entradas del sistema (un .nk, una URL) en el modo corto de Windows (plan 4.5): main lo resuelve
// ANTES de la instancia unica, sin ventana ni bandeja, con la funcion estatica del descriptor.
namespace ExternalDispatch {

// El primer argumento que no es un flag (--algo), o vacio.
QString firstPlainArgument(const QStringList &arguments);

// mac: el texto que se despacha para un QFileOpenEvent. Un archivo local va como ruta; una URL, con su
// codificacion intacta y sin un caracter que una URL no admita. Nunca QUrl::toString() a secas: decodifica
// %20 a un espacio, y con un solo caracter invalido macOS (open -a, NSURL desde macOS 14) vuelve a
// codificar TODOS los % de la URL (%3A -> %253A), lo que rompia los logins OAuth (redirect_uri=http%253A...).
QString fileOpenArgument(const QUrl &url);

// El descriptor que reclama `argument` (claimsExternal, sin efectos), o nullptr.
const ModuleDescriptor *claimant(const QList<ModuleDescriptor> &descriptors, const QString &argument);

struct Outcome
{
    bool handled = false; ///< algun descriptor la reclamo
    int exitCode = 0;
    bool timedOut = false;
};

// Atiende la entrada con runExternal. Si devuelve Pending, corre un QEventLoop hasta que el modulo
// llama a finished(code) o hasta `timeoutMs` (30 s en la app; sale con 1). `store` es de donde se
// leen [modules]/<id>/enabled y la seccion del modulo.
Outcome run(const QList<ModuleDescriptor> &descriptors, const QString &argument, SettingsStore *store, bool dryRun,
            int timeoutMs = 30000);

} // namespace ExternalDispatch

#endif // MIGHTYTOOLS_EXTERNALDISPATCH_H
