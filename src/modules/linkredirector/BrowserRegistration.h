#ifndef MIGHTYTOOLS_LINKREDIRECTOR_BROWSERREGISTRATION_H
#define MIGHTYTOOLS_LINKREDIRECTOR_BROWSERREGISTRATION_H

#include <QString>

// Registro de esta app como navegador candidato del sistema (plan 4.6) y consulta del navegador por
// defecto actual. Detras de una interfaz con win/ y mac/, al estilo de src/platform.
//
//  - Windows: ProgId nuevo "LGA.MightyTools.URL" para http, https, .htm y .html; Capabilities de
//    "LGA Mighty Tools"; clave "StartMenuInternet\LGA_MightyTools" en HKCU; entrada en
//    RegisteredApplications. No hay API para forzar el default: "Make Default" abre el panel de Apps
//    predeterminadas del sistema (openDefaultAppsSettings) y el usuario elige ahi.
//  - macOS: el bundle declara los schemes http/https en su Info.plist; registerAsBrowser() solo
//    refuerza el registro en LaunchServices (LSRegisterURL). "Make Default" pide el cambio por API
//    (NSWorkspace 12+) y el sistema muestra su propio prompt de confirmacion. No existe una API de
//    desregistro programatico (unregisterAsBrowser() es un no-op que devuelve true, como en el origen).
//
// 🔴 registerAsBrowser(), unregisterAsBrowser() y requestSetAsDefault() escriben el sistema o abren un
// panel del sistema: nunca se llaman desde --self-test, --simulate-action ni ninguna corrida
// automatizada. Solo start() del modulo (registerAsBrowser, si persistentRegistrationAllowed()),
// releaseSystem() del descriptor (unregisterAsBrowser) y el futuro panel de la etapa 2
// (requestSetAsDefault, boton "Make Default") los invocan.
namespace LinkRedirectorBrowserRegistration {

// Identificador propio asociado a http/https: el ProgId en Windows, el bundle id en mac.
QString ownHandlerId();
// True si `handlerId` es el de esta misma app (para excluirse de la lista de navegadores).
bool isOwnHandlerId(const QString &handlerId);

// Registra la app como candidata a navegador (no la vuelve default: eso lo pide el usuario en el
// panel del sistema). Idempotente. `error` opcional, con el motivo si devuelve false.
bool registerAsBrowser(QString *error = nullptr);
// Suelta el registro de navegador (D-09: "Remove as browser"). Simetrico en Windows; en mac no hay
// API equivalente, y es un no-op que no falla.
bool unregisterAsBrowser(QString *error = nullptr);
// True si esta app ya quedo registrada como candidata (independiente de ser el default).
bool isRegistered();

// Handler http actual del sistema (ProgId o bundle id).
QString currentDefaultHandlerId();
// True si el navegador por defecto del sistema es esta app.
bool isDefaultBrowser();
// Nombre visible del navegador por defecto actual ("Google Chrome", "LGA Mighty Tools", el ProgId
// crudo si no se pudo resolver, o vacio si no hay ninguno configurado).
QString currentDefaultBrowserName();

// Ruta al ejecutable de un handler ya detectado (para guardar "cual era el default" antes de pedir
// "Make Default", y poder sincronizarlo despues como Default browser de la herramienta). Vacio si no
// se encuentra entre los navegadores detectados (por ejemplo, si el handler es esta misma app).
QString exePathForHandlerId(const QString &handlerId);

// Abre el panel del sistema para elegir navegador por defecto (Windows: Apps predeterminadas, con
// deep link a esta app y fallback generico; mac: Ajustes del Sistema).
void openDefaultAppsSettings();

// Pide ser el navegador por defecto. Windows no tiene API: registra y abre el panel (devuelve
// false, el usuario elige a mano). mac 12+: lo pide por API y el sistema muestra su propio prompt de
// confirmacion (devuelve true si el pedido se disparo).
bool requestSetAsDefault();

} // namespace LinkRedirectorBrowserRegistration

#endif // MIGHTYTOOLS_LINKREDIRECTOR_BROWSERREGISTRATION_H
