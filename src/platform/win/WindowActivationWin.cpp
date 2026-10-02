#include "platform/WindowActivation.h"

#include <windows.h>

namespace WindowActivation {

void allowAnyProcessToActivate()
{
    // Solo tiene efecto si este proceso puede pasar el frente (lo lanzo el usuario): la residente
    // hace activateWindow() al recibir el pedido y Windows se lo permite.
    AllowSetForegroundWindow(ASFW_ANY);
}

void onReopenRequested(std::function<void()>)
{
}

void takeKeyboardWithoutActivating(quintptr)
{
}

} // namespace WindowActivation
