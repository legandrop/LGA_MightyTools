#include "platform/SystemInput.h"

#include <windows.h>

namespace SystemInput {

bool primaryButtonDown()
{
    // GetAsyncKeyState mira el boton FISICO: con los botones invertidos en Windows, el principal es
    // el derecho.
    const int vk = GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON;
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

bool escapeDown()
{
    return (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
}

bool accessibilityTrusted(bool /*prompt*/)
{
    return true;
}

bool needsAccessibilityPermission()
{
    return false;
}

} // namespace SystemInput
