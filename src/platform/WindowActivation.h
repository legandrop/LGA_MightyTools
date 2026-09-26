#ifndef MIGHTYTOOLS_WINDOWACTIVATION_H
#define MIGHTYTOOLS_WINDOWACTIVATION_H

// Pasar el frente a otro proceso. Windows solo deja que un proceso traiga su ventana al frente si el
// que tiene el foco se lo permite: la segunda copia (la que abrio el usuario) lo autoriza antes de
// pedirle la ventana a la residente. Una implementacion por plataforma:
//  - Windows (platform/win/WindowActivationWin.cpp): AllowSetForegroundWindow(ASFW_ANY).
//  - macOS (platform/mac/WindowActivationMac.cpp): nada; la residente se activa sola al mostrarse.
namespace WindowActivation {

void allowAnyProcessToActivate();

} // namespace WindowActivation

#endif // MIGHTYTOOLS_WINDOWACTIVATION_H
