#include "platform/ForegroundWatcher.h"

#include <QDebug>
#include <QString>

#import <AppKit/AppKit.h>

// La app activa por NSWorkspace (lo que antes observaba NukeWatcherMac por su cuenta). Sin compilar
// todavia en un Mac: la primera compilacion en macOS la valida. Con ARC (-fobjc-arc en CMakeLists).
// hwnd queda en 0: mac no expone la ventana del frente; pid y exeName son los de la app activa.

namespace {

// Observadores instalados en el proceso: un contador, no un puntero a una instancia.
int g_installed = 0;

QString exeNameOf(NSRunningApplication *app)
{
    // Nuke15.1v4.app, NukeX y Nuke Studio lanzan el mismo ejecutable ("Nuke15.1").
    NSString *executable = app ? app.executableURL.lastPathComponent : nil;
    return executable ? QString::fromNSString(executable) : QString();
}

} // namespace

struct ForegroundWatcher::Private
{
    id observer = nil;
};

ForegroundWatcher::ForegroundWatcher(bool active, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    if (!active) {
        // Corrida automatizada: ni observador ni consulta al sistema.
        return;
    }
    ForegroundWatcher *watcher = this;
    NSNotificationCenter *center = [[NSWorkspace sharedWorkspace] notificationCenter];
    d->observer = [center addObserverForName:NSWorkspaceDidActivateApplicationNotification
                                      object:nil
                                       queue:[NSOperationQueue mainQueue]
                                  usingBlock:^(NSNotification *note) {
                                      NSRunningApplication *app = note.userInfo[NSWorkspaceApplicationKey];
                                      watcher->setForeground(0, app ? quint32(app.processIdentifier) : 0,
                                                             exeNameOf(app));
                                  }];
    if (d->observer) {
        ++g_installed;
    }
    NSRunningApplication *front = [[NSWorkspace sharedWorkspace] frontmostApplication];
    m_pid = front ? quint32(front.processIdentifier) : 0;
    m_exeName = exeNameOf(front);
}

ForegroundWatcher::~ForegroundWatcher()
{
    if (d->observer) {
        [[[NSWorkspace sharedWorkspace] notificationCenter] removeObserver:d->observer];
        d->observer = nil;
        --g_installed;
    }
}

int ForegroundWatcher::installedHooks()
{
    return g_installed;
}

void ForegroundWatcher::setForeground(quintptr hwnd, quint32 pid, const QString &exeName)
{
    // En mac no hay ventana (hwnd 0): el cambio se ve en la app activa.
    if (m_pid == pid && m_exeName == exeName) {
        return;
    }
    m_hwnd = hwnd;
    m_pid = pid;
    m_exeName = exeName;
    emit foregroundChanged(hwnd, pid, exeName);
}
