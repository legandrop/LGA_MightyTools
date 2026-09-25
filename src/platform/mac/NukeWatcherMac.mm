#include "platform/NukeWatcher.h"

#include <QDebug>
#include <QString>

#import <AppKit/AppKit.h>
#include <ApplicationServices/ApplicationServices.h>

// Sin compilar todavia en un Mac: la primera compilacion en macOS la valida. Se compila con ARC
// (-fobjc-arc en el CMakeLists), por eso no hay retain/release.

namespace {

bool isNukeApp(NSRunningApplication *app)
{
    if (!app) {
        return false;
    }
    // Nuke15.1v4.app, NukeX15.1v4.app y Nuke Studio lanzan el mismo ejecutable ("Nuke15.1").
    NSString *executable = app.executableURL.lastPathComponent;
    return executable && NukeWatcher::isNukeExecutable(QString::fromNSString(executable));
}

// Marco de la ventana principal de una app por el API de Accesibilidad: posicion y tamano en
// puntos, con origen arriba a la izquierda de la pantalla principal (las mismas coordenadas que
// CGEvent). Necesita el permiso de Accesibilidad; sin el, devuelve vacio.
QRect mainWindowFrame(pid_t pid)
{
    AXUIElementRef appElement = AXUIElementCreateApplication(pid);
    if (!appElement) {
        return QRect();
    }
    QRect frame;
    CFTypeRef window = nullptr;
    if (AXUIElementCopyAttributeValue(appElement, kAXMainWindowAttribute, &window) == kAXErrorSuccess && window) {
        CFTypeRef positionValue = nullptr;
        CFTypeRef sizeValue = nullptr;
        CGPoint position = CGPointZero;
        CGSize size = CGSizeZero;
        const auto element = static_cast<AXUIElementRef>(window);
        if (AXUIElementCopyAttributeValue(element, kAXPositionAttribute, &positionValue) == kAXErrorSuccess
            && AXUIElementCopyAttributeValue(element, kAXSizeAttribute, &sizeValue) == kAXErrorSuccess
            && AXValueGetValue(static_cast<AXValueRef>(positionValue), static_cast<AXValueType>(kAXValueCGPointType),
                               &position)
            && AXValueGetValue(static_cast<AXValueRef>(sizeValue), static_cast<AXValueType>(kAXValueCGSizeType),
                               &size)) {
            frame = QRect(qRound(position.x), qRound(position.y), qRound(size.width), qRound(size.height));
        }
        if (positionValue) CFRelease(positionValue);
        if (sizeValue) CFRelease(sizeValue);
        CFRelease(window);
    }
    CFRelease(appElement);
    return frame;
}

} // namespace

struct NukeWatcher::Private
{
    id observer = nil;
};

NukeWatcher::NukeWatcher(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    NukeWatcher *watcher = this;
    NSNotificationCenter *center = [[NSWorkspace sharedWorkspace] notificationCenter];
    d->observer = [center addObserverForName:NSWorkspaceDidActivateApplicationNotification
                                      object:nil
                                       queue:[NSOperationQueue mainQueue]
                                  usingBlock:^(NSNotification *note) {
                                      NSRunningApplication *app = note.userInfo[NSWorkspaceApplicationKey];
                                      watcher->setNukeInFront(isNukeApp(app));
                                  }];
    m_nukeInFront = isNukeApp([[NSWorkspace sharedWorkspace] frontmostApplication]);
    qInfo() << "[NukeWatcher] Nuke al frente al arrancar:" << m_nukeInFront;
}

NukeWatcher::~NukeWatcher()
{
    if (d->observer) {
        [[[NSWorkspace sharedWorkspace] notificationCenter] removeObserver:d->observer];
        d->observer = nil;
    }
}

bool NukeWatcher::isNukeInFrontNow() const
{
    return isNukeApp([[NSWorkspace sharedWorkspace] frontmostApplication]);
}

QRect NukeWatcher::frontNukeFrame() const
{
    NSRunningApplication *front = [[NSWorkspace sharedWorkspace] frontmostApplication];
    if (!isNukeApp(front)) {
        return QRect();
    }
    return mainWindowFrame(front.processIdentifier);
}

QRect NukeWatcher::nukeFrameAt(const QPoint &nativePoint) const
{
    // El click del calibrador trae a Nuke al frente: se mide contra su ventana principal y se
    // confirma que el punto cae adentro.
    const QRect frame = frontNukeFrame();
    return frame.contains(nativePoint) ? frame : QRect();
}
