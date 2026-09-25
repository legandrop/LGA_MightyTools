#include "platform/WindowFrame.h"

#include <QGuiApplication>
#include <QWidget>

#include <windows.h>
#include <dwmapi.h>

namespace WindowFrame {

bool apply(QWidget *window)
{
    if (QGuiApplication::platformName() != QLatin1String("windows")) {
        return false;
    }
    // Los estilos de una ventana con titulo (WS_CAPTION, WS_SYSMENU, WS_MINIMIZEBOX): Windows la
    // sigue tratando como ventana normal aunque el titulo nativo no se dibuje.
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    SetWindowLongPtrW(hwnd, GWL_STYLE, style | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX);
    // Un pixel de "marco" metido en el cliente es lo que hace que DWM pinte la sombra.
    const MARGINS margins{0, 0, 1, 0};
    DwmExtendFrameIntoClientArea(hwnd, &margins);
    // Esquinas redondeadas de Windows 11 (DWMWA_WINDOW_CORNER_PREFERENCE = 33, DWMWCP_ROUND = 2).
    const DWORD corners = 2;
    DwmSetWindowAttribute(hwnd, 33, &corners, sizeof(corners));
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    return true;
}

bool handleNativeEvent(void *message, qintptr *result)
{
    const MSG *msg = static_cast<const MSG *>(message);
    if (msg->message == WM_NCCALCSIZE && msg->wParam == TRUE) {
        // Todo el rectangulo de la ventana es cliente: la barra de titulo es TitleBar.
        *result = 0;
        return true;
    }
    return false;
}

} // namespace WindowFrame
