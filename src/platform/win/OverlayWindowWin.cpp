#include "platform/OverlayWindow.h"

#include <QGuiApplication>
#include <QScreen>
#include <QWidget>

#include <windows.h>

namespace OverlayWindow {

void makeNonActivating(QWidget *window)
{
    if (QGuiApplication::platformName() != QLatin1String("windows")) {
        return;
    }
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, GetWindowLongPtrW(hwnd, GWL_EXSTYLE) | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW);
}

QScreen *screenOf(quintptr nativeWindow)
{
    const HWND hwnd = reinterpret_cast<HWND>(nativeWindow);
    if (!hwnd || !IsWindow(hwnd)) {
        return nullptr;
    }
    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &info)) {
        return nullptr;
    }
    // Qt conserva el origen nativo de cada pantalla (solo escala su tamaño): el monitor se reconoce por su esquina.
    const QPoint origin(info.rcMonitor.left, info.rcMonitor.top);
    for (QScreen *screen : QGuiApplication::screens()) {
        if (screen->geometry().topLeft() == origin) {
            return screen;
        }
    }
    return nullptr;
}

} // namespace OverlayWindow
