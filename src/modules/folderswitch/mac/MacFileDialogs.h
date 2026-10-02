#ifndef MIGHTYTOOLS_MACFILEDIALOGS_H
#define MIGHTYTOOLS_MACFILEDIALOGS_H

#include <QString>
#include <QtGlobal>

#include <functional>
#include <memory>

// Folder Switch en macOS (D-40): los dialogos de archivos por Accesibilidad y la carpeta del Finder por
// Apple Events. Solo mac (MacFileDialogs.mm). Nada de esto se llama en una corrida automatizada: el
// modulo corta antes.
//
// Probado en la Mac de Lega (2026-10-02) con una sonda sobre las apps reales:
//  - Nativo (NSOpenPanel/NSSavePanel de cualquier app): la ventana, o una hoja de ella, trae el
//    AXIdentifier "open-panel" o "save-panel". Se le lleva la carpeta con Cmd+Shift+G ("Ir a la
//    carpeta"), la ruta en el campo que queda con el foco, y Return.
//  - Qt (el navegador de Nuke, "Script to open"): ventana de subrole AXDialog con un solo campo de texto
//    editable y un boton Open/Save/... Se le escribe la ruta menos el ultimo caracter por Accesibilidad
//    y ese caracter se tipea, para que el dialogo registre la edicion y navegue (igual que UiaSwitcher
//    en Windows).
//  - El Finder no expone su carpeta por Accesibilidad (AXDocument vacio): se pregunta por Apple Events,
//    que pide el permiso de Automatizacion la primera vez (NSAppleEventsUsageDescription).
namespace MacFileDialogs {

enum class Kind { None, Native, Qt };

// Un dialogo de archivos visto en una app: el elemento de Accesibilidad de su ventana, retenido.
struct Dialog
{
    std::shared_ptr<const void> window; // AXUIElementRef
    qint64 pid = 0;
    Kind kind = Kind::None;
    bool isValid() const { return kind != Kind::None && window != nullptr; }
};

// El dialogo de archivos enfocado en la app `pid`; Kind::None si la ventana enfocada no es uno.
Dialog focusedDialog(qint64 pid);
// La app del frente (0 si no hay).
qint64 frontPid();
// Los dos son la misma ventana.
bool same(const Dialog &a, const Dialog &b);
// Sigue abierto: su ventana todavia responde.
bool isOpen(const Dialog &dialog);
// Trae al frente la app del dialogo (despues del popup de recientes, que es una ventana de esta app).
void activate(const Dialog &dialog);

// Lleva el dialogo a `path` (una carpeta). Asincrono: espera a que aparezca "Ir a la carpeta". `done`
// recibe si se pudo escribir la ruta.
void switchTo(const Dialog &dialog, const QString &path, const std::function<void(bool ok)> &done);

// El Finder.
bool isFinder(qint64 pid);
// El permiso de Automatizacion para preguntarle al Finder. Asking = el cartel del sistema esta abierto
// (pedido desde otro hilo para no trabar la app; la primera llamada sin decidir lo abre).
enum class Automation { Granted, Asking, Denied };
// `mayAsk` = false: si falta decidir, no abre el cartel (devuelve Asking).
Automation finderAutomation(bool mayAsk);
// La carpeta de la ventana del frente del Finder, con barra final; vacia si no hay ventana o no es una
// carpeta (Recientes, AirDrop). `denied` = true si macOS no dio el permiso de Automatizacion.
// `mayAsk` = false: si el permiso falta decidir, no abre el cartel (para el historial al salir del Finder,
// que no tiene que ver con ningun dialogo).
QString finderFolder(bool *denied = nullptr, bool mayAsk = true);

} // namespace MacFileDialogs

#endif // MIGHTYTOOLS_MACFILEDIALOGS_H
