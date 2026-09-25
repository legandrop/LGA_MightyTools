#ifndef MIGHTYTOOLS_OPENINNUKEX_MESSAGES_H
#define MIGHTYTOOLS_OPENINNUKEX_MESSAGES_H

#include "modules/openinnukex/NukeBridge.h"

#include <QString>

// Todos los mensajes de Open in NukeX, con el texto EXACTO en ingles de la tabla "Mensajes de
// Open in NukeX" del canvas de diseno (seccion 5), que a su vez reemplaza los 20 mensajes
// castellano/mixtos/sin-i18n del cliente v1.83 (inventario, "Mensajes de error y avisos").
//
// Cada mensaje se arma aca como {titulo, texto, icono} pero NO se muestra: la presentacion
// (Dialogs::info/warn/error de la UI) es etapa 2. Por ahora todo mensaje pasa por
// OpenInNukeXMessages::report(), el UNICO punto de salida del modulo, que solo loguea
// (comentario en el .cpp marca donde se conecta el sistema de dialogos).
struct OpenInNukeXMessage
{
    enum class Icon { Information, Warning, Critical };

    QString title;
    QString text;
    Icon icon = Icon::Information;
};

namespace OpenInNukeXMessages {

// ---- File Association / ruta de NukeX (Save, Apply) ----------------------------------------
OpenInNukeXMessage chooseVersionFirst();                    // Save o Apply con el campo vacio
OpenInNukeXMessage fileNoLongerExists();                    // el ejecutable elegido ya no esta
OpenInNukeXMessage notANukeExecutable();                    // el nombre no contiene "nuke"
OpenInNukeXMessage nukeVersionSaved(const QString &path);   // Save exitoso
OpenInNukeXMessage associationCompleted();                  // Apply exitoso, Windows o mac
OpenInNukeXMessage oneMoreStepInWindows();                  // Apply que necesita confirmar en Windows
OpenInNukeXMessage associationFinishedWithWarnings(const QString &technicalDetails); // Apply con errores
OpenInNukeXMessage associationError(const QString &errorText);       // excepcion no controlada en Apply
OpenInNukeXMessage almostDoneMac();                         // mac rechazo el cambio de asociacion
OpenInNukeXMessage runningFromBuildFolder();                // Apply desde un arbol de build (Windows y mac)

// ---- Nuke Bridge (Install, Export) ----------------------------------------------------------
OpenInNukeXMessage bridgeInstalled(const QString &path);    // Install exitoso
OpenInNukeXMessage bridgeExported(const QString &path);     // Export exitoso
// Traduce el codigo de NukeBridge::Error al mensaje de la tabla. None no deberia llegar aca
// (no es un error); si llega, devuelve un mensaje generico.
OpenInNukeXMessage bridgeError(NukeBridge::Error error);

// ---- NukeOpener / doble click en un .nk -------------------------------------------------------
// Los seis mensajes originales de nukeopener.cpp (QMessageBox::information directo, en castellano
// o mezclados, sin pasar por i18n) se homologan a estos tres, en ingles, por el mismo sistema de
// mensajes que el resto de la ventana. Los dos casos "config vacia" y "config no encontrada" del
// origen quedan unificados en nukeNotConfigured(). Si el modulo esta APAGADO y llega un .nk, se
// lanza NukeX directo, sin ningun mensaje (D-05): estas tres solo corren con el modulo prendido.
OpenInNukeXMessage nukeNotConfigured();                     // no hay ruta guardada en nukeXpath.txt
OpenInNukeXMessage nukeXPathGone(const QString &path);      // la ruta guardada ya no existe
OpenInNukeXMessage nukeXFailedToStart(const QString &errorDetail); // CreateProcessW/startDetached fallo

// Unico punto de salida del modulo. Por ahora un qWarning/qInfo segun el icono; el comentario del
// .cpp marca donde etapa 2 lo conecta al sistema de dialogos de la UI (Dialogs::info/warn/error).
void report(const OpenInNukeXMessage &message);

} // namespace OpenInNukeXMessages

#endif // MIGHTYTOOLS_OPENINNUKEX_MESSAGES_H
