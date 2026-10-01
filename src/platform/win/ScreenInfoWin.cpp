#include "platform/ScreenInfo.h"

#include <windows.h>

namespace ScreenInfo {

QSize primaryAvailableSize()
{
    RECT work{};
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
        return QSize();
    }
    HDC screen = GetDC(nullptr);
    if (!screen) {
        return QSize();
    }
    const int dpi = GetDeviceCaps(screen, LOGPIXELSX);
    ReleaseDC(nullptr, screen);
    if (dpi <= 0) {
        return QSize();
    }
    // Sin DPI declarado los dos valores vienen virtualizados (96); con DPI declarado, en pixeles fisicos y
    // con el DPI de sistema. Hoy el exe no trae manifiesto de DPI y se llama antes de la app (primer caso).
    // Si algun dia se declara DPI por manifiesto, usar GetDpiForMonitor sobre el monitor principal: el DPI
    // de sistema no cambia si el usuario cambia la escala sin cerrar sesion.
    return QSize(MulDiv(work.right - work.left, 96, dpi), MulDiv(work.bottom - work.top, 96, dpi));
}

} // namespace ScreenInfo
