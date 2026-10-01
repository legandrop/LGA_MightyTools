#include "platform/ScreenInfo.h"

#import <AppKit/AppKit.h>

namespace ScreenInfo {

QSize primaryAvailableSize()
{
    @autoreleasepool {
        // La primera pantalla es la que tiene la barra de menu. visibleFrame ya descuenta menu y Dock, en
        // puntos (los pixeles logicos de Qt).
        NSScreen *screen = [[NSScreen screens] firstObject];
        if (!screen) {
            return QSize();
        }
        const NSRect frame = [screen visibleFrame];
        return QSize(int(frame.size.width), int(frame.size.height));
    }
}

} // namespace ScreenInfo
