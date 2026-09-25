#ifndef MIGHTYTOOLS_LINKREDIRECTOR_ROUTING_H
#define MIGHTYTOOLS_LINKREDIRECTOR_ROUTING_H

#include "app/Module.h" // SettingsReader

#include <QString>
#include <QStringList>

// Logica de ruteo de LinkRedirector (plan: "Nuke Shortcuts" 2, "LGA Link Redirector"), PURA: nada de
// aca abre un navegador ni toca el sistema. Se puede probar entera con --self-test.
//
// Regla con la herramienta PRENDIDA: si la URL contiene (sin mayusculas, substring) alguna palabra de
// matchWords, va al navegador alternativo; si no, al navegador por defecto. Si el elegido no sirve
// (vacio o su ruta ya no existe), se prueba el otro navegador configurado, con un aviso; si ninguno
// sirve, no se abre nada (UrlRouter.cpp:23-83 del origen).
//
// Regla con la herramienta APAGADA (D-05, paso directo del host): SIN reglas de palabras clave, todo
// va al "Default browser" configurado; si no hay, al primer navegador detectado (que nunca es esta
// misma app: BrowserDetection ya la excluye).
namespace LinkRedirectorRouting {

struct Rules
{
    QString defaultBrowser;
    QString alternativeBrowser;
    QStringList matchWords;
};

// Lee la seccion [linkRedirector] via el SettingsReader que da el host (con el modulo prendido o
// apagado: ver ExternalRequest::value y ModuleContext::value).
Rules rulesFromReader(const SettingsReader &value);

// True si `url` contiene alguna palabra de `words` (sin mayusculas, substring, ignorando vacias).
bool matchesAnyWord(const QString &url, const QStringList &words, QString *matchedWord = nullptr);

// Por que el navegador elegido en primer lugar no sirvio.
enum class UnavailableReason {
    None,
    DefaultNotSet,
    AlternativeNotSet,
    DefaultPathMissing,
    AlternativePathMissing,
};

struct Decision
{
    bool matched = false;              // matcheo alguna palabra clave (regla "prendida")
    QString chosenExe;                 // navegador final; vacio si ninguno sirvio
    bool usedFallback = false;         // true si se uso el OTRO navegador configurado
    UnavailableReason reason = UnavailableReason::None; // motivo si usedFallback, o si nada sirvio
};

// Regla "prendida": aplica matchWords y el fallback al otro navegador configurado.
Decision decideEnabled(const QString &url, const Rules &rules);

// Regla "apagada" (paso directo D-05): ignora matchWords; usa defaultBrowser o, si no sirve, el
// primero de `detectedFallback` (ya sin esta misma app).
struct OffDecision
{
    QString chosenExe;          // vacio si no hay ningun navegador disponible
    bool usedDetectedFallback = false;
};
OffDecision decideDisabled(const Rules &rules, const QList<QString> &detectedFallbackExePaths);

// Texto de la razon (idioma UI: ingles), tal cual el tablero de diseno, seccion 5.
QString reasonText(UnavailableReason reason);
// Titulo y cuerpo del aviso "navegador no disponible" (tablero de diseno, seccion 5: "Browser
// settings" + "<motivo> This link opens with <exe> for now."). Vacio si reason es None.
QString warningTitle();
QString warningCaption(UnavailableReason reason, const QString &usedExe);

// Etapa 2: al volverse el navegador por defecto del sistema, el que era default pasa a ser el
// "Default browser" de la herramienta (mainwindow.cpp:1562-1601 del origen). Pura: el panel es quien
// sabe si `isNowSystemDefault` y cual era `previousSystemDefaultExePath` (BrowserRegistration los da).
QString browserToSyncAsDefault(bool isNowSystemDefault, const QString &previousSystemDefaultExePath);

// Privacidad: el modo corto de Windows loguea solo el host (o el nombre del archivo local, sin la
// ruta), nunca la URL completa (salvo el flag de debug "linkRedirectorLogFullUrl").
QString safeLogTarget(const QString &argument);
QString logTarget(const QString &argument);

} // namespace LinkRedirectorRouting

#endif // MIGHTYTOOLS_LINKREDIRECTOR_ROUTING_H
