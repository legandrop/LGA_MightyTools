#include "platform/WindowPlacement.h"
#include "core/I18n.h"

#include <QGuiApplication>
#include <QScreen>

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>

#include <limits>
#include <unistd.h>

namespace {

struct CFHolder
{
    CFTypeRef value = nullptr;
    ~CFHolder() { if (value) CFRelease(value); }
};

void fail(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

qint64 number(CFDictionaryRef dictionary, const void *key, qint64 fallback = -1)
{
    auto *value = static_cast<CFNumberRef>(CFDictionaryGetValue(dictionary, key));
    qint64 result = fallback;
    if (value && CFGetTypeID(value) == CFNumberGetTypeID()) {
        CFNumberGetValue(value, kCFNumberSInt64Type, &result);
    }
    return result;
}

QRect windowBounds(CFDictionaryRef dictionary)
{
    auto *value = static_cast<CFDictionaryRef>(CFDictionaryGetValue(dictionary, kCGWindowBounds));
    CGRect bounds{};
    if (!value || CFGetTypeID(value) != CFDictionaryGetTypeID()
        || !CGRectMakeWithDictionaryRepresentation(value, &bounds)) {
        return {};
    }
    return QRect(qRound(bounds.origin.x), qRound(bounds.origin.y),
                 qRound(bounds.size.width), qRound(bounds.size.height));
}

QString string(CFDictionaryRef dictionary, const void *key)
{
    auto *value = static_cast<CFStringRef>(CFDictionaryGetValue(dictionary, key));
    return value && CFGetTypeID(value) == CFStringGetTypeID()
        ? QString::fromNSString((__bridge NSString *)value).trimmed() : QString();
}

QRect screenRect(NSScreen *screen, CGFloat mainTop, bool available)
{
    NSRect frame = available ? screen.visibleFrame : screen.frame;
    if (available) {
        CFHolder hidden, orientation;
        if (CFPreferencesAppSynchronize(CFSTR("com.apple.dock"))) {
            hidden.value = CFPreferencesCopyAppValue(CFSTR("autohide"), CFSTR("com.apple.dock"));
            orientation.value = CFPreferencesCopyAppValue(CFSTR("orientation"), CFSTR("com.apple.dock"));
        }
        if (hidden.value && CFGetTypeID(hidden.value) == CFBooleanGetTypeID()
            && CFBooleanGetValue(static_cast<CFBooleanRef>(hidden.value))
            && orientation.value && CFGetTypeID(orientation.value) == CFStringGetTypeID()) {
            const NSRect full = screen.frame;
            auto *edge = static_cast<CFStringRef>(orientation.value);
            // visibleFrame puede conservar el borde de activacion del Dock autooculto.
            // Si falla la sincronizacion o falta una preferencia, se conserva
            // visibleFrame por seguridad.
            if (CFStringCompare(edge, CFSTR("left"), 0) == kCFCompareEqualTo) {
                const CGFloat right = NSMaxX(frame);
                frame.origin.x = NSMinX(full);
                frame.size.width = right - frame.origin.x;
            } else if (CFStringCompare(edge, CFSTR("right"), 0) == kCFCompareEqualTo) {
                frame.size.width = NSMaxX(full) - NSMinX(frame);
            } else if (CFStringCompare(edge, CFSTR("bottom"), 0) == kCFCompareEqualTo) {
                const CGFloat top = NSMaxY(frame);
                frame.origin.y = NSMinY(full);
                frame.size.height = top - frame.origin.y;
            }
        }
    }
    // AppKit usa origen abajo; AX y CoreGraphics, arriba de la pantalla principal.
    return QRect(qRound(NSMinX(frame)), qRound(mainTop - NSMaxY(frame)),
                 qRound(NSWidth(frame)), qRound(NSHeight(frame)));
}

bool axGeometry(AXUIElementRef window, QRect *out)
{
    constexpr AXValueType pointType = static_cast<AXValueType>(kAXValueCGPointType);
    constexpr AXValueType sizeType = static_cast<AXValueType>(kAXValueCGSizeType);
    CFHolder position, size;
    if (AXUIElementCopyAttributeValue(window, kAXPositionAttribute, &position.value) != kAXErrorSuccess
        || AXUIElementCopyAttributeValue(window, kAXSizeAttribute, &size.value) != kAXErrorSuccess
        || !position.value || !size.value
        || CFGetTypeID(position.value) != AXValueGetTypeID() || CFGetTypeID(size.value) != AXValueGetTypeID()) {
        return false;
    }
    const auto pointValue = static_cast<AXValueRef>(position.value);
    const auto sizeValue = static_cast<AXValueRef>(size.value);
    if (AXValueGetType(pointValue) != pointType || AXValueGetType(sizeValue) != sizeType) {
        return false;
    }
    CGPoint point{};
    CGSize dimensions{};
    if (!AXValueGetValue(pointValue, pointType, &point)
        || !AXValueGetValue(sizeValue, sizeType, &dimensions)) {
        return false;
    }
    *out = QRect(qRound(point.x), qRound(point.y), qRound(dimensions.width), qRound(dimensions.height));
    return !out->isEmpty();
}

bool setAxGeometry(AXUIElementRef window, const QRect &rect)
{
    constexpr AXValueType pointType = static_cast<AXValueType>(kAXValueCGPointType);
    constexpr AXValueType sizeType = static_cast<AXValueType>(kAXValueCGSizeType);
    CGPoint point = CGPointMake(rect.x(), rect.y());
    CGSize size = CGSizeMake(rect.width(), rect.height());
    CFHolder pointValue, sizeValue;
    pointValue.value = AXValueCreate(pointType, &point);
    sizeValue.value = AXValueCreate(sizeType, &size);
    // Algunas apps limitan el tamaño segun la posicion actual; reubicar antes
    // de redimensionar y repetir la posicion por si la app la ajusto.
    return pointValue.value && sizeValue.value
        && AXUIElementSetAttributeValue(window, kAXPositionAttribute, pointValue.value) == kAXErrorSuccess
        && AXUIElementSetAttributeValue(window, kAXSizeAttribute, sizeValue.value) == kAXErrorSuccess
        && AXUIElementSetAttributeValue(window, kAXPositionAttribute, pointValue.value) == kAXErrorSuccess;
}

bool near(const QRect &a, const QRect &b)
{
    return qAbs(a.x() - b.x()) <= 2 && qAbs(a.y() - b.y()) <= 2
        && qAbs(a.width() - b.width()) <= 2 && qAbs(a.height() - b.height()) <= 2;
}

} // namespace

namespace WindowPlacement {

QList<WindowInfo> selectableWindows(QString *error)
{
    if (error) error->clear();
    QList<WindowInfo> result;
    CFHolder windows;
    windows.value = CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
    if (!windows.value) {
        fail(error, I18n::tr("Could not list open windows."));
        return result;
    }
    QRect desktop;
    for (const QScreen *screen : QGuiApplication::screens()) {
        desktop = desktop.united(screen->geometry());
    }
    const auto list = static_cast<CFArrayRef>(windows.value);
    for (CFIndex i = 0; i < CFArrayGetCount(list); ++i) {
        auto *dictionary = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(list, i));
        if (!dictionary || number(dictionary, kCGWindowLayer) != 0
            || number(dictionary, kCGWindowOwnerPID) == getpid()) {
            continue;
        }
        const QRect rect = windowBounds(dictionary);
        if (rect.width() <= 10 || rect.height() <= 10 || !desktop.intersects(rect)) {
            continue;
        }
        QString title = string(dictionary, kCGWindowName);
        const QString app = string(dictionary, kCGWindowOwnerName);
        if (title.isEmpty()) {
            title = app;
        } else if (!app.isEmpty() && title != app) {
            title = app + QStringLiteral(" — ") + title;
        }
        if (!title.isEmpty()) {
            result.append({title, rect});
        }
    }
    if (result.isEmpty()) {
        fail(error, I18n::tr("No selectable windows were found. Check Screen Recording permission in System Settings."));
    }
    return result;
}

QRect pickerWorkAreaAt(const QPoint &globalLogical)
{
    @autoreleasepool {
        NSArray<NSScreen *> *screens = NSScreen.screens;
        NSScreen *primary = screens.firstObject;
        if (!primary) {
            return {};
        }
        const CGFloat mainTop = NSMaxY(primary.frame);
        for (NSScreen *screen in screens) {
            if (screenRect(screen, mainTop, false).contains(globalLogical)) {
                return screenRect(screen, mainTop, true);
            }
        }
        return {};
    }
}

bool moveForeground(const QRectF &normalizedArea, QString *error, bool dryRun)
{
    if (error) error->clear();
    if (resolve(QRect(0, 0, 10000, 10000), normalizedArea).isEmpty()) {
        fail(error, I18n::tr("Choose a valid area."));
        return false;
    }
    if (dryRun) {
        return true;
    }
    @autoreleasepool {
        if (!AXIsProcessTrustedWithOptions(nullptr)) {
            fail(error, I18n::tr("Allow LGA Mighty Tools in Privacy & Security > Accessibility to move windows."));
            return false;
        }
        NSRunningApplication *front = NSWorkspace.sharedWorkspace.frontmostApplication;
        if (!front || front.processIdentifier == getpid()) {
            fail(error, I18n::tr("Switch to a movable app window and try again."));
            return false;
        }
        CFHolder app, window;
        app.value = AXUIElementCreateApplication(front.processIdentifier);
        if (!app.value) {
            fail(error, I18n::tr("Could not access the active app."));
            return false;
        }
        AXUIElementSetMessagingTimeout(static_cast<AXUIElementRef>(app.value), 1.0f);
        if (AXUIElementCopyAttributeValue(static_cast<AXUIElementRef>(app.value), kAXFocusedWindowAttribute,
                                          &window.value) != kAXErrorSuccess || !window.value) {
            fail(error, I18n::tr("The active app has no accessible window."));
            return false;
        }
        auto *axWindow = static_cast<AXUIElementRef>(window.value);
        AXUIElementSetMessagingTimeout(axWindow, 1.0f);
        Boolean positionSettable = false, sizeSettable = false;
        if (AXUIElementIsAttributeSettable(axWindow, kAXPositionAttribute, &positionSettable) != kAXErrorSuccess
            || AXUIElementIsAttributeSettable(axWindow, kAXSizeAttribute, &sizeSettable) != kAXErrorSuccess
            || !positionSettable || !sizeSettable) {
            fail(error, I18n::tr("This window does not allow moving or resizing."));
            return false;
        }
        QRect current;
        if (!axGeometry(axWindow, &current)) {
            fail(error, I18n::tr("Could not read the active window."));
            return false;
        }
        NSArray<NSScreen *> *screens = NSScreen.screens;
        NSScreen *primary = screens.firstObject;
        if (!primary) {
            fail(error, I18n::tr("Could not read the window's monitor."));
            return false;
        }
        const CGFloat mainTop = NSMaxY(primary.frame);
        NSScreen *chosen = primary;
        qint64 bestArea = 0;
        qint64 nearestDistance = std::numeric_limits<qint64>::max();
        for (NSScreen *screen in screens) {
            const QRect frame = screenRect(screen, mainTop, false);
            const QRect overlap = frame.intersected(current);
            const qint64 area = overlap.isEmpty() ? 0 : qint64(overlap.width()) * overlap.height();
            if (area > bestArea) {
                bestArea = area;
                chosen = screen;
            }
            const QPoint center = current.center();
            const qint64 dx = qint64(center.x()) - qBound(frame.left(), center.x(), frame.right());
            const qint64 dy = qint64(center.y()) - qBound(frame.top(), center.y(), frame.bottom());
            const qint64 distance = dx * dx + dy * dy;
            if (bestArea == 0 && distance < nearestDistance) {
                nearestDistance = distance;
                chosen = screen;
            }
        }
        const QRect desired = resolve(screenRect(chosen, mainTop, true), normalizedArea);
        if (desired.isEmpty()) {
            fail(error, I18n::tr("The selected area is too small for this monitor."));
            return false;
        }
        const auto restore = [&]() {
            QRect restored;
            return setAxGeometry(axWindow, current) && axGeometry(axWindow, &restored) && near(restored, current);
        };
        if (!setAxGeometry(axWindow, desired)) {
            fail(error, restore() ? I18n::tr("This app refused to move or resize its window.")
                                  : I18n::tr("This app refused to move and its previous position could not be restored."));
            return false;
        }
        QRect actual;
        if (!axGeometry(axWindow, &actual) || !near(actual, desired)) {
            fail(error, restore() ? I18n::tr("This app limits the window's position or minimum size.")
                                  : I18n::tr("This app changed the requested area and its previous position could not be restored."));
            return false;
        }
        return true;
    }
}

bool minimizeForeground(QString *error, bool dryRun)
{
    if (error) error->clear();
    if (dryRun) return true;
    @autoreleasepool {
        if (!AXIsProcessTrustedWithOptions(nullptr)) {
            fail(error, I18n::tr("Allow LGA Mighty Tools in Privacy & Security > Accessibility to control windows."));
            return false;
        }
        NSRunningApplication *front = NSWorkspace.sharedWorkspace.frontmostApplication;
        if (!front || front.processIdentifier == getpid()) {
            fail(error, I18n::tr("Switch to a movable app window and try again."));
            return false;
        }
        CFHolder app, window, minimized;
        app.value = AXUIElementCreateApplication(front.processIdentifier);
        if (!app.value) {
            fail(error, I18n::tr("Could not access the active app."));
            return false;
        }
        AXUIElementSetMessagingTimeout(static_cast<AXUIElementRef>(app.value), 1.0f);
        if (AXUIElementCopyAttributeValue(static_cast<AXUIElementRef>(app.value), kAXFocusedWindowAttribute,
                                          &window.value) != kAXErrorSuccess || !window.value) {
            fail(error, I18n::tr("The active app has no accessible window."));
            return false;
        }
        auto *axWindow = static_cast<AXUIElementRef>(window.value);
        AXUIElementSetMessagingTimeout(axWindow, 1.0f);
        Boolean settable = false;
        if (AXUIElementIsAttributeSettable(axWindow, kAXMinimizedAttribute, &settable) != kAXErrorSuccess
            || !settable
            || AXUIElementSetAttributeValue(axWindow, kAXMinimizedAttribute, kCFBooleanTrue) != kAXErrorSuccess
            || AXUIElementCopyAttributeValue(axWindow, kAXMinimizedAttribute, &minimized.value) != kAXErrorSuccess
            || !minimized.value || CFGetTypeID(minimized.value) != CFBooleanGetTypeID()
            || !CFBooleanGetValue(static_cast<CFBooleanRef>(minimized.value))) {
            fail(error, I18n::tr("This window could not be minimized."));
            return false;
        }
        return true;
    }
}

} // namespace WindowPlacement
