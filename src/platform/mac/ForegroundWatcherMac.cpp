#include "platform/ForegroundWatcher.h"

// Sin implementar todavia: Folder Switch (el unico que lo usa hasta ahora) es solo Windows. El
// stub existe para que el servicio compartido de ModuleContext (plan 4.4) compile en mac aunque
// ningun modulo de esta plataforma lo pida todavia; el dia que haga falta (Nuke Shortcuts en mac),
// esto se completa con NSWorkspace.notificationCenter y didActivateApplicationNotification.

struct ForegroundWatcher::Private
{
};

ForegroundWatcher::ForegroundWatcher(bool /*active*/, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
}

ForegroundWatcher::~ForegroundWatcher() = default;

void ForegroundWatcher::setForeground(quintptr, quint32, const QString &)
{
}
