#include "platform/WindowFrame.h"

#include <QGuiApplication>
#include <QTimer>
#include <QWidget>
#include <QWindow>

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

// La ventana sin marco de Qt es una NSWindow «borderless»: macOS le pone sombra pero la deja con las
// esquinas rectas. prepare() la vuelve translucida (Qt la crea sin opacidad y con fondo transparente)
// y apply() recorta la capa de la vista de Qt, que ocupa toda la ventana, con el radio de las ventanas
// del sistema; la sombra se recalcula con esa forma. La que se estira (`resizable`) suma
// NSWindowStyleMaskResizable: con eso macOS la deja estirar desde los bordes y las esquinas, con sus
// cursores, aunque no tenga barra de titulo nativa. Respeta el minimo que fija Qt.
//
// Este archivo se compila SIN ARC (CMakeLists.txt): retain/release a mano.
namespace {

bool isCocoa()
{
    return QGuiApplication::platformName() == QLatin1String("cocoa");
}

// La vista de Qt de una ventana ya creada, sin crearla si todavia no existe.
NSView *createdView(QWidget *window)
{
    QWindow *handle = window ? window->windowHandle() : nullptr;
    if (!handle || !handle->handle()) {
        return nil;
    }
    return reinterpret_cast<NSView *>(handle->winId());
}

// Radio de las esquinas de una ventana con barra de titulo: medido en macOS 26 con NSThemeFrame
// (`_cornerRadius` = 16); hasta macOS 15 las ventanas sin barra de herramientas usaban 10.
CGFloat systemCornerRadius()
{
    if (@available(macOS 26.0, *)) {
        return 16.0;
    }
    return 10.0;
}

void roundCorners(NSView *view)
{
    view.wantsLayer = YES;
    CALayer *layer = view.layer;
    layer.cornerRadius = systemCornerRadius();
    if (@available(macOS 10.15, *)) {
        layer.cornerCurve = kCACornerCurveContinuous;
    }
    layer.masksToBounds = YES;
}

// El ultimo mouseDown izquierdo que AppKit le entrego a la app, tal cual llego. Qt entrega el click
// DIFERIDO: cuando la barra lo recibe, NSApp.currentEvent (lo que usa startSystemMove) puede ser otro
// evento y la ventana no se mueve. Con este, el arrastre arranca siempre desde el mouseDown real.
NSEvent *g_lastMouseDown = nil;
id g_mouseDownMonitor = nil;
id g_resizeObserver = nil;

void installObservers()
{
    if (g_mouseDownMonitor || !isCocoa()) {
        return;
    }
    // Solo mira: devuelve el evento sin tocarlo, Qt lo recibe como siempre (menus, modales, filtros
    // de «click afuera»).
    g_mouseDownMonitor = [[NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskLeftMouseDown
                                                                handler:^NSEvent *(NSEvent *event) {
                                                                    [g_lastMouseDown release];
                                                                    g_lastMouseDown = [event retain];
                                                                    return event;
                                                                }] retain];
    // Al terminar de estirar una ventana con las esquinas recortadas, la sombra se recalcula con la
    // forma nueva.
    g_resizeObserver = [[[NSNotificationCenter defaultCenter]
        addObserverForName:NSWindowDidEndLiveResizeNotification
                    object:nil
                     queue:nil
                usingBlock:^(NSNotification *note) {
                    NSWindow *window = note.object;
                    if (!window.opaque && window.contentView.layer.cornerRadius > 0) {
                        [window invalidateShadow];
                    }
                }] retain];
}

} // namespace

namespace WindowFrame {

void prepare(QWidget *window)
{
    if (window && isCocoa()) {
        window->setAttribute(Qt::WA_TranslucentBackground, true);
        installObservers();
    }
}

bool startDrag(QWidget *window)
{
    if (!isCocoa()) {
        return false; // offscreen: winId no es una NSView
    }
    NSView *view = createdView(window);
    NSWindow *nsWindow = view.window;
    if (!nsWindow || !g_lastMouseDown || g_lastMouseDown.window != nsWindow) {
        return false;
    }
    // Si el boton ya se solto (un click corto que Qt entrego tarde), no hay nada que arrastrar.
    if (!([NSEvent pressedMouseButtons] & 1)) {
        return true;
    }
    [nsWindow performWindowDragWithEvent:g_lastMouseDown];
    return true;
}

bool apply(QWidget *window, bool resizable)
{
    if (!window || !isCocoa()) {
        return false;
    }
    NSView *view = reinterpret_cast<NSView *>(window->winId());
    NSWindow *nsWindow = view.window;
    if (!nsWindow) {
        return false;
    }
    roundCorners(view);
    if (resizable) {
        nsWindow.styleMask = nsWindow.styleMask | NSWindowStyleMaskResizable;
    }
    // La sombra de una ventana translucida sale de lo pintado: se recalcula cuando ya hay un cuadro en
    // pantalla (el primero llega despues de este showEvent).
    [nsWindow invalidateShadow];
    QTimer::singleShot(100, window, [window]() {
        if (NSView *shown = createdView(window)) {
            [shown.window invalidateShadow];
        }
    });
    return false; // no hay eventos nativos que atender
}

bool handleNativeEvent(void *, qintptr *)
{
    return false;
}

} // namespace WindowFrame
