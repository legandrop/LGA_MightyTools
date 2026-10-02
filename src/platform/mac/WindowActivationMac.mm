#include "platform/WindowActivation.h"

#import <AppKit/AppKit.h>

// "Reabrir" (kAEReopenApplication): lo manda el sistema a la copia residente cuando el usuario abre la
// app desde Finder, el Launchpad o Spotlight. Con LSUIElement no hay Dock ni ventana visible, asi que
// sin este handler la app no hace nada visible y parece rota.

@interface LgaReopenHandler : NSObject
@property(nonatomic) std::function<void()> handler;
- (void)handleReopen:(NSAppleEventDescriptor *)event withReplyEvent:(NSAppleEventDescriptor *)reply;
@end

@implementation LgaReopenHandler
- (void)handleReopen:(NSAppleEventDescriptor *)event withReplyEvent:(NSAppleEventDescriptor *)reply
{
    (void)event;
    (void)reply;
    if (self.handler) {
        self.handler();
    }
}
@end

namespace WindowActivation {

void allowAnyProcessToActivate()
{
    // En macOS no hace falta autorizar: la app residente se activa sola al mostrar su ventana.
}

void onReopenRequested(std::function<void()> handler)
{
    static LgaReopenHandler *target = [[LgaReopenHandler alloc] init];
    target.handler = std::move(handler);
    // Reemplaza al de NSApplication (que solo consulta al delegate de Qt, que no abre nada). Se instala
    // con el event loop ya corriendo: NSApplication registra los suyos al terminar de arrancar.
    [[NSAppleEventManager sharedAppleEventManager] setEventHandler:target
                                                       andSelector:@selector(handleReopen:withReplyEvent:)
                                                     forEventClass:kCoreEventClass
                                                        andEventID:kAEReopenApplication];
}

void takeKeyboardWithoutActivating(quintptr winId)
{
    NSView *view = (__bridge NSView *)reinterpret_cast<void *>(winId);
    NSWindow *window = view.window;
    if (!window) {
        return;
    }
    // Solo un NSPanel puede ser "nonactivating" (Qt usa NSPanel para Qt::Popup).
    if ([window isKindOfClass:[NSPanel class]]) {
        window.styleMask |= NSWindowStyleMaskNonactivatingPanel;
        static_cast<NSPanel *>(window).becomesKeyOnlyIfNeeded = NO;
        // Qt::Tool se esconde cuando la app no esta activa; aca la app nunca se activa.
        window.hidesOnDeactivate = NO;
    }
    [window makeKeyAndOrderFront:nil];
}

void setDockIconVisible(bool visible)
{
    const NSApplicationActivationPolicy wanted =
        visible ? NSApplicationActivationPolicyRegular : NSApplicationActivationPolicyAccessory;
    if (NSApp.activationPolicy == wanted) {
        return;
    }
    [NSApp setActivationPolicy:wanted];
    if (visible) {
        // Al pasar a app normal, macOS no la trae al frente sola: la ventana recien mostrada quedaria
        // detras de la app que estaba activa. Y el selector de Cmd+Tab no se entera: la app ya era la
        // activa (como app de barra de menu), activarla de nuevo no es un cambio y la deja ULTIMA en vez
        // de segunda al pasar a otra app. El arreglo conocido: un instante de frente al Dock y de vuelta
        // a la app, asi el selector ve una activacion de verdad (y aparece el menu de la app).
        NSRunningApplication *dock =
            [NSRunningApplication runningApplicationsWithBundleIdentifier:@"com.apple.dock"].firstObject;
        [dock activateWithOptions:0];
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, int64_t(100 * NSEC_PER_MSEC)), dispatch_get_main_queue(), ^{
            if (NSApp.activationPolicy == NSApplicationActivationPolicyRegular) {
                [NSApp activateIgnoringOtherApps:YES];
            }
        });
    }
}

} // namespace WindowActivation
