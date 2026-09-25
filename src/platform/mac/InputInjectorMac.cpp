#include "platform/InputInjector.h"

#include <ApplicationServices/ApplicationServices.h>
#include <Carbon/Carbon.h>

// Sin compilar todavia en un Mac: la primera compilacion en macOS la valida.
//
// Cada evento se crea con sus modificadores EXPLICITOS (CGEventSetFlags): el usuario puede tener
// todavia apretado Cmd+Shift del atajo, y sin fijar los flags el click derecho le llegaria a Nuke
// como Cmd+Shift+click.

namespace {

CGKeyCode keyCode(InputInjector::Key key)
{
    switch (key) {
    case InputInjector::Key::Down:
        return kVK_DownArrow;
    case InputInjector::Key::Return:
        return kVK_Return;
    case InputInjector::Key::A:
        return kVK_ANSI_A;
    case InputInjector::Key::F:
        return kVK_ANSI_F;
    }
    return 0;
}

void post(CGEventRef event, CGEventFlags flags)
{
    if (!event) {
        return;
    }
    CGEventSetFlags(event, flags);
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
}

} // namespace

QPoint InputInjector::cursorPos() const
{
    CGEventRef event = CGEventCreate(nullptr);
    const CGPoint point = CGEventGetLocation(event);
    CFRelease(event);
    return QPoint(qRound(point.x), qRound(point.y));
}

void InputInjector::moveCursor(const QPoint &nativePoint)
{
    note(QStringLiteral("move %1,%2").arg(nativePoint.x()).arg(nativePoint.y()));
    if (m_dryRun) {
        return;
    }
    const CGPoint point = CGPointMake(nativePoint.x(), nativePoint.y());
    // Un evento de movimiento (y no solo CGWarpMouseCursorPosition) para que Nuke actualice lo que
    // tiene debajo del puntero.
    post(CGEventCreateMouseEvent(nullptr, kCGEventMouseMoved, point, kCGMouseButtonLeft), 0);
}

void InputInjector::clickAt(Button button, const QPoint &nativePoint)
{
    moveCursor(nativePoint);
    note(QStringLiteral("click %1").arg(button == Button::Left ? QStringLiteral("left") : QStringLiteral("right")));
    if (m_dryRun) {
        return;
    }
    const CGPoint point = CGPointMake(nativePoint.x(), nativePoint.y());
    const bool left = button == Button::Left;
    post(CGEventCreateMouseEvent(nullptr, left ? kCGEventLeftMouseDown : kCGEventRightMouseDown, point,
                                 left ? kCGMouseButtonLeft : kCGMouseButtonRight),
         0);
    post(CGEventCreateMouseEvent(nullptr, left ? kCGEventLeftMouseUp : kCGEventRightMouseUp, point,
                                 left ? kCGMouseButtonLeft : kCGMouseButtonRight),
         0);
}

void InputInjector::releaseModifiers()
{
    note(QStringLiteral("release modifiers [not needed on macOS]"));
}

void InputInjector::tapKey(Key key, bool withPrimary)
{
    note(QStringLiteral("key %1%2").arg(withPrimary ? QStringLiteral("Cmd+") : QString(), keyName(key)));
    if (m_dryRun) {
        return;
    }
    const CGEventFlags flags = withPrimary ? kCGEventFlagMaskCommand : 0;
    const CGKeyCode code = keyCode(key);
    post(CGEventCreateKeyboardEvent(nullptr, code, true), flags);
    post(CGEventCreateKeyboardEvent(nullptr, code, false), flags);
}
