#ifndef MIGHTYTOOLS_LINKREDIRECTOR_EXTERNAL_H
#define MIGHTYTOOLS_LINKREDIRECTOR_EXTERNAL_H

#include "app/Module.h"

// Entrada del sistema (.htm/.html/http/https), plan 4.5. Una sola implementacion para los DOS puntos
// de entrada del contrato (ModuleDescriptor::claimsExternal/runExternal para el modo corto de Windows
// y la herramienta apagada, y Module::handleExternal para la app residente de mac con la herramienta
// prendida): asi la regla de ruteo es una sola, no dos copias que se puedan desincronizar.
namespace LinkRedirectorExternal {

// Decide, SIN efectos, si `argument` es un link o un .htm/.html/.xhtml de esta herramienta. Ignora
// flags que empiecen con "--" (--settings, --self-test...).
bool claims(const QString &argument);

// Rutea `argument` segun las reglas (plan 4.5, D-05) y lo abre (salvo dryRun). Nunca abre nada si
// request.dryRun es true: solo loguea el navegador elegido y el host (privacidad).
ExternalResult handle(const ExternalRequest &request);

} // namespace LinkRedirectorExternal

#endif // MIGHTYTOOLS_LINKREDIRECTOR_EXTERNAL_H
