#ifndef MIGHTYTOOLS_WINDOWACTIVATION_H
#define MIGHTYTOOLS_WINDOWACTIVATION_H

#include <QtGlobal>

#include <functional>

// Pasar el frente a otro proceso. Windows solo deja que un proceso traiga su ventana al frente si el
// que tiene el foco se lo permite: la segunda copia (la que abrio el usuario) lo autoriza antes de
// pedirle la ventana a la residente. Una implementacion por plataforma:
//  - Windows (platform/win/WindowActivationWin.cpp): AllowSetForegroundWindow(ASFW_ANY).
//  - macOS (platform/mac/WindowActivationMac.mm): nada; la residente se activa sola al mostrarse.
namespace WindowActivation {

void allowAnyProcessToActivate();

// macOS: abrir la app desde Finder, el Launchpad o Spotlight con la copia ya corriendo NO lanza otra
// copia (no llega a la instancia unica): el sistema le manda a la residente el evento "reabrir". El
// handler se llama en el hilo de la interfaz. En Windows no hace nada: ahi la segunda copia arranca y
// le pide la ventana a la residente por la instancia unica.
void onReopenRequested(std::function<void()> handler);

// macOS: la ventana `winId` (ya mostrada) recibe el teclado SIN activar la app, como los paneles de
// Spotlight o Raycast. Lo usa el popup de recientes de Folder Switch: desde un atajo global macOS 14+ no
// deja que esta app pase al frente, y sin esto las teclas (1-9, Enter, Esc) le llegaban a la app del
// dialogo. En Windows no hace nada (ahi el popup usa SetForegroundWindow con el permiso de WM_HOTKEY).
void takeKeyboardWithoutActivating(quintptr winId);

} // namespace WindowActivation

#endif // MIGHTYTOOLS_WINDOWACTIVATION_H
