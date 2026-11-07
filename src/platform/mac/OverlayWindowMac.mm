#include "platform/OverlayWindow.h"

#include <QGuiApplication>
#include <QRect>
#include <QScreen>
#include <QWidget>

#import <AppKit/AppKit.h>
#include <CoreGraphics/CoreGraphics.h>
#include <objc/message.h>

// La ventanita del dictado en macOS. Qt::Tool ya es un NSPanel; acá se le suma lo que Qt no hace:
//  - panel que no activa la app (NonactivatingPanel y _setPreventsActivation, como el marco de Meet Frame):
//    mostrarla no le saca el foco a la app donde se dicta;
//  - hidesOnDeactivate = NO: un Qt::Tool se esconde cuando la app no está activa, que es justo siempre que se
//    dicta en otra app;
//  - en todos los escritorios y también sobre una app a pantalla completa.

namespace OverlayWindow {

void makeNonActivating(QWidget *window)
{
    if (QGuiApplication::platformName() != QLatin1String("cocoa")) {
        return;
    }
    NSView *view = (__bridge NSView *)reinterpret_cast<void *>(window->winId());
    NSWindow *ns = view ? view.window : nil;
    if (!ns) {
        return;
    }
    if ([ns isKindOfClass:[NSPanel class]]) {
        ns.styleMask |= NSWindowStyleMaskNonactivatingPanel;
        static_cast<NSPanel *>(ns).becomesKeyOnlyIfNeeded = YES;
    }
    SEL prevents = NSSelectorFromString(@"_setPreventsActivation:");
    if ([ns respondsToSelector:prevents]) ((void (*)(id, SEL, BOOL))objc_msgSend)(ns, prevents, YES);
    ns.hidesOnDeactivate = NO;
    ns.collectionBehavior |= NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorFullScreenAuxiliary
        | NSWindowCollectionBehaviorIgnoresCycle;
}

QScreen *screenOf(quintptr nativeWindow)
{
    // En mac el destino (TextTyper::foregroundWindow) lleva el PID de la app del frente abajo y el número de su
    // ventana de más arriba arriba: esa ventana, o si no se sabe, la de más arriba de la app.
    const pid_t pid = pid_t(quint64(nativeWindow) & 0xffffffffu);
    const quint32 number = quint32(quint64(nativeWindow) >> 32);
    if (pid <= 0) {
        return nullptr;
    }
    CFArrayRef windows = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
                                                    kCGNullWindowID);
    if (!windows) {
        return nullptr;
    }
    QRect bounds;
    for (CFIndex i = 0; i < CFArrayGetCount(windows) && bounds.isEmpty(); ++i) {
        NSDictionary *info = (__bridge NSDictionary *)CFArrayGetValueAtIndex(windows, i);
        if ([info[(__bridge NSString *)kCGWindowOwnerPID] intValue] != pid
            || [info[(__bridge NSString *)kCGWindowLayer] intValue] != 0
            || (number != 0 && [info[(__bridge NSString *)kCGWindowNumber] unsignedIntValue] != number)) {
            continue;
        }
        CGRect rect = CGRectZero;
        if (CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)info[(__bridge NSString *)kCGWindowBounds], &rect)
            && rect.size.width > 1 && rect.size.height > 1) {
            bounds = QRect(int(rect.origin.x), int(rect.origin.y), int(rect.size.width), int(rect.size.height));
        }
    }
    CFRelease(windows);
    if (bounds.isEmpty()) {
        return nullptr;
    }
    // Las coordenadas de CGWindow son las globales de Qt en mac (origen arriba a la izquierda de la pantalla
    // principal, en puntos): la pantalla que tiene más de la ventana.
    QScreen *best = nullptr;
    qint64 bestArea = 0;
    for (QScreen *screen : QGuiApplication::screens()) {
        const QRect part = screen->geometry().intersected(bounds);
        const qint64 area = qint64(part.width()) * part.height();
        if (area > bestArea) {
            bestArea = area;
            best = screen;
        }
    }
    return best;
}

} // namespace OverlayWindow
