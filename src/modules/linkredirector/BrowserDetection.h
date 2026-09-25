#ifndef MIGHTYTOOLS_LINKREDIRECTOR_BROWSERDETECTION_H
#define MIGHTYTOOLS_LINKREDIRECTOR_BROWSERDETECTION_H

#include "modules/linkredirector/LinkRedirectorTypes.h"

#include <QList>
#include <QString>

// Navegadores instalados en el sistema, para los combos "Default browser" / "Alternative browser"
// (etapa 2) y para el paso directo del host con la herramienta apagada (plan 4.3, D-05).
//
// La lectura es propia de cada plataforma (por eso vive detras de una interfaz con win/ y mac/, al
// estilo de src/platform):
//  - Windows (win/BrowserDetectionWin.cpp): Software\Clients\StartMenuInternet en HKLM y HKCU, excluye
//    Internet Explorer y deduplica por ruta de ejecutable.
//  - macOS   (mac/BrowserDetectionMac.cpp): LaunchServices (LSCopyAllHandlersForURLScheme) mas una
//    lista fija de respaldo, filtrado por familias de navegadores conocidas.
//
// installedBrowsers() ademas EXCLUYE SIEMPRE esta misma app (BrowserRegistration::isOwnHandlerId() y
// su propio ejecutable): elegirse a si misma como destino de un link seria un bucle de ruteo.
namespace LinkRedirectorBrowsers {

// Deteccion cruda de la plataforma, sin filtrar. Puede incluir esta misma app si ya se registro
// como navegador.
QList<DetectedBrowser> installedBrowsersRaw();

// installedBrowsersRaw() sin esta misma app, deduplicado por ruta de ejecutable.
QList<DetectedBrowser> installedBrowsers();

// Nombre visible para un handler (ProgId en Windows, bundle id en mac), buscando en la lista CRUDA
// (incluye esta app, para poder mostrar "LGA Mighty Tools" cuando el default actual es ella misma).
QString nameForHandlerId(const QString &handlerId);

// Nombre visible para una ruta de ejecutable ya elegida (combos, avisos). Si no esta en la lista
// detectada, cae al nombre del archivo sin extension.
QString friendlyNameForExe(const QString &exePath);

} // namespace LinkRedirectorBrowsers

#endif // MIGHTYTOOLS_LINKREDIRECTOR_BROWSERDETECTION_H
