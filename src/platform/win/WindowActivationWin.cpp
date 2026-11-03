#include "platform/WindowActivation.h"
#include "platform/win/InputOrigin.h"

#include <windows.h>

namespace {

bool inFront(HWND window)
{
    HWND front = GetForegroundWindow();
    return front && (front == window || GetAncestor(front, GA_ROOTOWNER) == window);
}

} // namespace

namespace WindowActivation {

void allowAnyProcessToActivate()
{
    // Solo tiene efecto si este proceso puede pasar el frente (lo lanzo el usuario): la residente
    // hace activateWindow() al recibir el pedido y Windows se lo permite.
    AllowSetForegroundWindow(ASFW_ANY);
}

void claimForegroundRight()
{
    // Sin MOUSEEVENTF_MOVE ni botones: no mueve nada. Lleva la marca propia, como toda entrada de la app.
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwExtraInfo = kMightyToolsInputTag;
    SendInput(1, &input, sizeof(INPUT));
}

bool bringToFront(quintptr id)
{
    HWND window = reinterpret_cast<HWND>(id);
    if (!IsWindow(window)) {
        return false;
    }
    claimForegroundRight();
    if (IsIconic(window)) {
        ShowWindow(window, SW_RESTORE);
    }
    SetForegroundWindow(window);
    if (inFront(window)) {
        return true;
    }
    // Ultimo recurso: compartir por un instante la cola de entrada del hilo que tiene el frente.
    const DWORD own = GetCurrentThreadId();
    const DWORD front = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    const bool attached = front && front != own && AttachThreadInput(own, front, TRUE);
    BringWindowToTop(window);
    SetForegroundWindow(window);
    if (attached) {
        AttachThreadInput(own, front, FALSE);
    }
    return inFront(window);
}

void onReopenRequested(std::function<void()>)
{
}

void takeKeyboardWithoutActivating(quintptr)
{
}

void setDockIconVisible(bool visible)
{
    Q_UNUSED(visible);
}

} // namespace WindowActivation
