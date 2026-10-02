#include "modules/folderswitch/mac/MacFileDialogs.h"

#include "modules/folderswitch/FolderSwitchLogic.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QPointer>
#include <QTimer>

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h>

#include <atomic>

// Con ARC (-fobjc-arc en CMakeLists). Ver el encabezado para lo medido en la Mac de Lega.

namespace {

// Una app que no responde no puede trabar la app entera: cada consulta de Accesibilidad espera como
// mucho esto (el default del sistema son 6 s).
constexpr float kAxTimeoutSeconds = 0.5f;
// Despues de Cmd+Shift+G, lo que tarda en aparecer "Ir a la carpeta" con el foco en su campo.
constexpr int kGoToFolderWaitMs = 400;
// Entre escribir la ruta y Return; y entre escribir la ruta y tipear el ultimo caracter (Qt).
constexpr int kBeforeReturnMs = 150;
constexpr int kBeforeLastCharMs = 100;

AXUIElementRef appElement(qint64 pid)
{
    AXUIElementRef app = AXUIElementCreateApplication(static_cast<pid_t>(pid));
    if (app) {
        AXUIElementSetMessagingTimeout(app, kAxTimeoutSeconds);
    }
    return app;
}

NSString *stringAttribute(AXUIElementRef element, CFStringRef attribute)
{
    CFTypeRef value = nullptr;
    if (AXUIElementCopyAttributeValue(element, attribute, &value) != kAXErrorSuccess || !value) {
        return nil;
    }
    NSString *result = CFGetTypeID(value) == CFStringGetTypeID() ? [(__bridge NSString *)value copy] : nil;
    CFRelease(value);
    return result;
}

NSArray *children(AXUIElementRef element)
{
    CFTypeRef value = nullptr;
    if (AXUIElementCopyAttributeValue(element, kAXChildrenAttribute, &value) != kAXErrorSuccess || !value) {
        return @[];
    }
    return (__bridge_transfer NSArray *)value;
}

bool isPanelIdentifier(NSString *identifier)
{
    return [identifier isEqualToString:@"open-panel"] || [identifier isEqualToString:@"save-panel"];
}

std::shared_ptr<const void> retained(AXUIElementRef element)
{
    CFRetain(element);
    return std::shared_ptr<const void>(element, [](const void *e) { CFRelease(static_cast<CFTypeRef>(e)); });
}

// La ventana del panel nativo, retenida: la propia ventana, o una hoja suya (Guardar como hoja de un documento).
std::shared_ptr<const void> nativePanelOf(AXUIElementRef window)
{
    if (isPanelIdentifier(stringAttribute(window, CFSTR("AXIdentifier")))) {
        return retained(window);
    }
    for (id child in children(window)) {
        auto element = (__bridge AXUIElementRef)child;
        if ([stringAttribute(element, kAXRoleAttribute) isEqualToString:@"AXSheet"]
            && isPanelIdentifier(stringAttribute(element, CFSTR("AXIdentifier")))) {
            return retained(element); // el arreglo de hijos se libera al salir
        }
    }
    return nullptr;
}

// Los campos de texto editables de la ventana (sin los de un combo, como el filtro "*.nk" de Nuke, ni los
// nombres de archivo de una lista) y los titulos de sus botones. No baja a listas ni arboles.
void collectQtDialogParts(AXUIElementRef element, NSString *parentRole, int depth, NSMutableArray *fields,
                          NSMutableArray *buttonTitles)
{
    if (depth > 6) {
        return;
    }
    NSString *role = stringAttribute(element, kAXRoleAttribute);
    if ([role isEqualToString:@"AXTextField"] && ![parentRole isEqualToString:@"AXComboBox"]
        && ![parentRole isEqualToString:@"AXRow"] && ![parentRole isEqualToString:@"AXCell"]) {
        [fields addObject:(__bridge id)element];
    }
    if ([role isEqualToString:@"AXButton"]) {
        NSString *title = stringAttribute(element, kAXTitleAttribute);
        if (title.length) {
            [buttonTitles addObject:title.lowercaseString];
        }
    }
    if ([role isEqualToString:@"AXOutline"] || [role isEqualToString:@"AXList"] || [role isEqualToString:@"AXTable"]) {
        return;
    }
    for (id child in children(element)) {
        collectQtDialogParts((__bridge AXUIElementRef)child, role, depth + 1, fields, buttonTitles);
    }
}

// El campo de la ruta de un dialogo de archivos Qt (Nuke), retenido, o nullptr si la ventana no es uno: un dialogo
// con UN solo campo editable y un boton de los que cierran un dialogo de archivos (la misma lista que
// WindowUtils en Windows).
std::shared_ptr<const void> qtPathField(AXUIElementRef window)
{
    if (![stringAttribute(window, kAXSubroleAttribute) isEqualToString:@"AXDialog"]) {
        return nullptr;
    }
    NSMutableArray *fields = [NSMutableArray array];
    NSMutableArray *buttons = [NSMutableArray array];
    collectQtDialogParts(window, nil, 0, fields, buttons);
    if (fields.count != 1) {
        return nullptr;
    }
    static NSArray *const kAccept = @[@"open", @"save", @"choose", @"select", @"abrir", @"guardar"];
    for (NSString *title in buttons) {
        for (NSString *accept in kAccept) {
            if ([title hasPrefix:accept]) {
                // Retenido: el arreglo que lo tiene se libera al salir.
                return retained((__bridge AXUIElementRef)fields.firstObject);
            }
        }
    }
    return nullptr;
}

// Teclas con los modificadores EXPLICITOS: el usuario puede seguir apretando los del atajo (Cmd+Alt+O) y
// sin esto la app recibiria Cmd+Alt+Shift+G.
void postKey(CGKeyCode code, CGEventFlags flags)
{
    CGEventRef down = CGEventCreateKeyboardEvent(nullptr, code, true);
    CGEventRef up = CGEventCreateKeyboardEvent(nullptr, code, false);
    CGEventSetFlags(down, flags);
    CGEventSetFlags(up, flags);
    CGEventPost(kCGHIDEventTap, down);
    CGEventPost(kCGHIDEventTap, up);
    CFRelease(down);
    CFRelease(up);
}

void postText(const QString &text)
{
    const std::u16string chars = text.toStdU16String();
    CGEventRef down = CGEventCreateKeyboardEvent(nullptr, 0, true);
    CGEventRef up = CGEventCreateKeyboardEvent(nullptr, 0, false);
    CGEventSetFlags(down, 0);
    CGEventSetFlags(up, 0);
    CGEventKeyboardSetUnicodeString(down, chars.size(), reinterpret_cast<const UniChar *>(chars.data()));
    CGEventKeyboardSetUnicodeString(up, chars.size(), reinterpret_cast<const UniChar *>(chars.data()));
    CGEventPost(kCGHIDEventTap, down);
    CGEventPost(kCGHIDEventTap, up);
    CFRelease(down);
    CFRelease(up);
}

bool setValue(AXUIElementRef element, const QString &value)
{
    return AXUIElementSetAttributeValue(element, kAXValueAttribute, (__bridge CFStringRef)value.toNSString())
        == kAXErrorSuccess;
}

// El elemento con el foco en la app `pid`, retenido (nullptr si no hay).
std::shared_ptr<const void> focusedElement(qint64 pid)
{
    AXUIElementRef app = appElement(pid);
    if (!app) {
        return nullptr;
    }
    CFTypeRef focused = nullptr;
    std::shared_ptr<const void> result;
    if (AXUIElementCopyAttributeValue(app, kAXFocusedUIElementAttribute, &focused) == kAXErrorSuccess && focused) {
        result = std::shared_ptr<const void>(focused, [](const void *e) { CFRelease(static_cast<CFTypeRef>(e)); });
    }
    CFRelease(app);
    return result;
}

AXUIElementRef focusedWindowOf(AXUIElementRef app)
{
    CFTypeRef window = nullptr;
    if (AXUIElementCopyAttributeValue(app, kAXFocusedWindowAttribute, &window) != kAXErrorSuccess || !window) {
        return nullptr;
    }
    return static_cast<AXUIElementRef>(window); // +1, lo suelta quien llama
}

} // namespace

namespace MacFileDialogs {

Dialog focusedDialog(qint64 pid)
{
    Dialog dialog;
    if (pid <= 0 || pid == NSProcessInfo.processInfo.processIdentifier) {
        return dialog;
    }
    QElapsedTimer timer;
    timer.start();
    AXUIElementRef app = appElement(pid);
    if (!app) {
        return dialog;
    }
    if (AXUIElementRef window = focusedWindowOf(app)) {
        if (std::shared_ptr<const void> panel = nativePanelOf(window)) {
            dialog.window = panel;
            dialog.kind = Kind::Native;
        } else if (qtPathField(window)) {
            dialog.window = retained(window);
            dialog.kind = Kind::Qt;
        }
        CFRelease(window);
    }
    CFRelease(app);
    dialog.pid = dialog.isValid() ? pid : 0;
    FolderSwitchLogic::logCallTiming("focusedDialog", timer.elapsed());
    return dialog;
}

qint64 frontPid()
{
    NSRunningApplication *front = NSWorkspace.sharedWorkspace.frontmostApplication;
    return front ? front.processIdentifier : 0;
}

bool same(const Dialog &a, const Dialog &b)
{
    return a.isValid() && b.isValid() && a.pid == b.pid && CFEqual(a.window.get(), b.window.get());
}

bool isOpen(const Dialog &dialog)
{
    if (!dialog.isValid()) {
        return false;
    }
    auto window = static_cast<AXUIElementRef>(const_cast<void *>(dialog.window.get()));
    AXUIElementSetMessagingTimeout(window, kAxTimeoutSeconds);
    return stringAttribute(window, kAXRoleAttribute) != nil;
}

void activate(const Dialog &dialog)
{
    NSRunningApplication *app = [NSRunningApplication runningApplicationWithProcessIdentifier:static_cast<pid_t>(dialog.pid)];
    [app activateWithOptions:NSApplicationActivateAllWindows];
}

void switchTo(const Dialog &dialog, const QString &path, const std::function<void(bool ok)> &done)
{
    if (!dialog.isValid() || path.isEmpty()) {
        done(false);
        return;
    }
    QString folder = path;
    if (!folder.endsWith(QLatin1Char('/'))) {
        folder += QLatin1Char('/');
    }

    // Las teclas van a la app del frente: si el usuario cambio de app en el medio, no se manda nada.
    const qint64 pid = dialog.pid;
    const auto stillInFront = [pid]() { return frontPid() == pid; };
    if (!stillInFront()) {
        qInfo() << "[folderSwitch] La app del dialogo ya no esta al frente: no se mandan teclas";
        done(false);
        return;
    }

    if (dialog.kind == Kind::Native) {
        // "Ir a la carpeta" del panel. El campo que queda con el foco es el de la ruta: tiene que ser OTRO
        // que el enfocado antes (si "Ir a la carpeta" no se abrio, el foco seguiria en, por ejemplo, el
        // nombre de un Guardar, y Return guardaria).
        std::shared_ptr<const void> before = focusedElement(pid);
        postKey(kVK_ANSI_G, kCGEventFlagMaskCommand | kCGEventFlagMaskShift);
        QTimer::singleShot(kGoToFolderWaitMs, [pid, before, folder, done, stillInFront]() {
            const std::shared_ptr<const void> focused = focusedElement(pid);
            bool ok = false;
            if (focused && !(before && CFEqual(before.get(), focused.get())) && stillInFront()) {
                auto field = static_cast<AXUIElementRef>(const_cast<void *>(focused.get()));
                NSString *role = stringAttribute(field, kAXRoleAttribute);
                ok = ([role isEqualToString:@"AXTextField"] || [role isEqualToString:@"AXComboBox"]) && setValue(field, folder);
            }
            if (!ok) {
                qWarning() << "[folderSwitch] No aparecio el campo de \"Ir a la carpeta\"";
                if (stillInFront() && focused && !(before && CFEqual(before.get(), focused.get()))) {
                    postKey(kVK_Escape, 0); // no dejar "Ir a la carpeta" abierto a medio camino
                }
                done(false);
                return;
            }
            QTimer::singleShot(kBeforeReturnMs, [done, stillInFront]() {
                if (!stillInFront()) {
                    done(false);
                    return;
                }
                postKey(kVK_Return, 0);
                done(true);
            });
        });
        return;
    }

    // Qt (Nuke): la ruta menos el ultimo caracter por Accesibilidad, con el foco en el campo, y el ultimo
    // caracter tipeado: asi el dialogo ve una edicion del usuario y navega. Sin Return (abriria el
    // archivo seleccionado).
    auto window = static_cast<AXUIElementRef>(const_cast<void *>(dialog.window.get()));
    AXUIElementSetMessagingTimeout(window, kAxTimeoutSeconds);
    const std::shared_ptr<const void> keptField = qtPathField(window);
    if (!keptField) {
        done(false);
        return;
    }
    auto field = static_cast<AXUIElementRef>(const_cast<void *>(keptField.get()));
    AXUIElementSetAttributeValue(field, kAXFocusedAttribute, kCFBooleanTrue);
    if (!setValue(field, folder.left(folder.size() - 1))) {
        done(false);
        return;
    }
    QTimer::singleShot(kBeforeLastCharMs, [keptField, folder, done, stillInFront]() {
        if (!stillInFront()) {
            done(false);
            return;
        }
        postText(folder.right(1));
        QTimer::singleShot(kBeforeReturnMs, [keptField, folder, done]() {
            auto f = static_cast<AXUIElementRef>(const_cast<void *>(keptField.get()));
            NSString *value = stringAttribute(f, kAXValueAttribute);
            done(value && QString::fromNSString(value) == folder);
        });
    });
}

Automation finderAutomation(bool mayAsk)
{
    static std::atomic<bool> asking{false};
    if (asking) {
        return Automation::Asking;
    }
    static const char kFinder[] = "com.apple.finder";
    AEAddressDesc target;
    if (AECreateDesc(typeApplicationBundleID, kFinder, sizeof(kFinder) - 1, &target) != noErr) {
        return Automation::Granted; // sin descriptor no se puede preguntar: que decida el propio Apple Event
    }
    const OSStatus status = AEDeterminePermissionToAutomateTarget(&target, typeWildCard, typeWildCard, false);
    AEDisposeDesc(&target);
    if (status == errAEEventWouldRequireUserConsent) {
        if (!mayAsk) {
            return Automation::Asking;
        }
        asking = true;
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
            AEAddressDesc askTarget;
            if (AECreateDesc(typeApplicationBundleID, kFinder, sizeof(kFinder) - 1, &askTarget) == noErr) {
                const OSStatus answer = AEDeterminePermissionToAutomateTarget(&askTarget, typeWildCard, typeWildCard, true);
                AEDisposeDesc(&askTarget);
                NSLog(@"[folderSwitch] Permiso para controlar el Finder: %d", static_cast<int>(answer));
            }
            asking = false;
        });
        return Automation::Asking;
    }
    // errAEEventNotPermitted: el usuario lo nego. procNotFound: el Finder no corre (el Apple Event lo
    // lanza y el permiso se decide entonces).
    return status == errAEEventNotPermitted ? Automation::Denied : Automation::Granted;
}

bool isFinder(qint64 pid)
{
    NSRunningApplication *app = [NSRunningApplication runningApplicationWithProcessIdentifier:static_cast<pid_t>(pid)];
    return [app.bundleIdentifier isEqualToString:@"com.apple.finder"];
}

QString finderFolder(bool *denied, bool mayAsk)
{
    if (denied) {
        *denied = false;
    }
    if (!mayAsk && finderAutomation(false) != Automation::Granted) {
        return QString();
    }
    // El permiso de Automatizacion, sin trabar la app: el primer Apple Event al Finder muestra el cartel
    // del sistema y ESPERA la respuesta en el hilo que lo manda. En la prueba de Lega, mandado desde el
    // hilo de la interfaz, la app quedo congelada 84 s con el cartel abierto. Se pregunta primero sin
    // pedir; si falta decidir, el cartel se pide en otro hilo y esta vez no se cambia la carpeta.
    switch (finderAutomation(true)) {
    case Automation::Granted:
        break;
    case Automation::Asking:
        qInfo() << "[folderSwitch] Esperando el permiso para controlar el Finder";
        return QString();
    case Automation::Denied:
        if (denied) {
            *denied = true;
        }
        return QString();
    }
    QElapsedTimer timer;
    timer.start();
    static NSAppleScript *const script = [[NSAppleScript alloc] initWithSource:
        // Tope de 2 s: un Finder colgado (un volumen de red caido) no puede trabar la app entera.
        @"with timeout of 2 seconds\n"
         "  tell application \"Finder\"\n"
         "    if (count of Finder windows) is 0 then return \"\"\n"
         "    try\n"
         "      return POSIX path of (target of front Finder window as alias)\n"
         "    on error\n"
         "      return \"\"\n"
         "    end try\n"
         "  end tell\n"
         "end timeout"];
    NSDictionary *error = nil;
    NSAppleEventDescriptor *result = [script executeAndReturnError:&error];
    FolderSwitchLogic::logCallTiming("finderFolder", timer.elapsed());
    if (!result) {
        const NSInteger code = [error[NSAppleScriptErrorNumber] integerValue];
        // errAEEventNotPermitted (-1743) y errAEEventWouldRequireUserConsent (-1744): sin permiso.
        if (denied && (code == -1743 || code == -1744)) {
            *denied = true;
        }
        qWarning() << "[folderSwitch] Finder no respondio la carpeta, error" << code;
        return QString();
    }
    QString folder = QString::fromNSString(result.stringValue ?: @"");
    if (!folder.isEmpty() && !folder.endsWith(QLatin1Char('/'))) {
        folder += QLatin1Char('/');
    }
    return folder;
}

} // namespace MacFileDialogs
