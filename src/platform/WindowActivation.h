#ifndef MIGHTYTOOLS_WINDOWACTIVATION_H
#define MIGHTYTOOLS_WINDOWACTIVATION_H

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

} // namespace WindowActivation

#endif // MIGHTYTOOLS_WINDOWACTIVATION_H
