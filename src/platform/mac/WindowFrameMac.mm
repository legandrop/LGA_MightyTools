#include "platform/WindowFrame.h"

#include <QWidget>

#import <AppKit/AppKit.h>

// En macOS la ventana sin marco ya tiene sombra y esquinas del sistema. La que se estira (`resizable`)
// suma NSWindowStyleMaskResizable: con eso macOS la deja estirar desde los bordes y las esquinas, con
// sus cursores, aunque no tenga barra de titulo nativa. Respeta el minimo que fija Qt.
namespace WindowFrame {

bool apply(QWidget *window, bool resizable)
{
    if (!resizable || !window) {
        return false;
    }
    NSView *view = reinterpret_cast<NSView *>(window->winId());
    NSWindow *nsWindow = view.window;
    if (!nsWindow) {
        return false;
    }
    nsWindow.styleMask = nsWindow.styleMask | NSWindowStyleMaskResizable;
    return false; // no hay eventos nativos que atender
}

bool handleNativeEvent(void *, qintptr *)
{
    return false;
}

} // namespace WindowFrame
