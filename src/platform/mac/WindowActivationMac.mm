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

} // namespace WindowActivation
