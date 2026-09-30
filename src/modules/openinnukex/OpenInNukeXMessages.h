#ifndef MIGHTYTOOLS_OPENINNUKEX_MESSAGES_H
#define MIGHTYTOOLS_OPENINNUKEX_MESSAGES_H

#include "modules/openinnukex/NukeBridge.h"

#include <QString>

#include <functional>

class QDialog;
class QWidget;

// Todos los mensajes de Open in NukeX, con el texto EXACTO en ingles de la tabla "Mensajes de
// Open in NukeX" del canvas de diseno (seccion 5), que a su vez reemplaza los 20 mensajes
// castellano/mixtos/sin-i18n del cliente v1.83 (inventario, "Mensajes de error y avisos").
//
// Cada mensaje se arma aca como {titulo, texto, icono} y se presenta con
// OpenInNukeXMessages::report(), el UNICO punto de salida del modulo: un QMessageBox con el
// Theme de la app (el mismo sistema que usa el updater — Theme::apply() estiliza QMessageBox
// globalmente, ver ui/Theme.cpp), un solo boton OK. En corrida automatizada nunca se muestra
// nada: solo el log.
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
OpenInNukeXMessage runningFromBuildFolder();                // Apply desde un arbol de build (solo mac)

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

// Unico punto de salida del modulo: siempre loguea (qInfo/qWarning), y si `automatedRun` es
// false ADEMAS muestra un QMessageBox con el Theme de la app, con `parent` como padre (nullptr
// vale: el modo corto de Windows y el .nk de mac no siempre tienen una ventana a mano). Con
// `automatedRun` true (self-test, --ui-shot, --ui-probe, ExternalRequest::dryRun,
// ModuleContext::automatedRun()) NUNCA se muestra nada, solo el log.
void report(const OpenInNukeXMessage &message, QWidget *parent, bool automatedRun);

// Cartel "NukeX Launcher" (inventario, seccion homonima): no modal, cuenta regresiva 3-2-1 en el
// texto de un boton deshabilitado, se cierra solo. Solo se llama si el setting `showLaunchNotice`
// esta prendido Y `automatedRun` es false. `onClosed` corre cuando el cartel se cierra solo (para
// que quien lo llamo pueda esperarlo antes de terminar, como hacia el cliente v1.83 antes de
// salir del modo corto).
void showLaunchNotice(QWidget *parent, bool automatedRun, const std::function<void()> &onClosed);

// Solo construye el cartel (sin arrancar el timer ni mostrarlo): lo usa
// OpenInNukeXModule::createCaptureWidget() para el estado de captura "launcher-notice" (Module.h:
// "sin exec() ni show()"). El objeto es hijo de `parent` y no toma ninguna accion por si solo.
QDialog *buildLaunchNoticeWidget(QWidget *parent);

} // namespace OpenInNukeXMessages

#endif // MIGHTYTOOLS_OPENINNUKEX_MESSAGES_H
