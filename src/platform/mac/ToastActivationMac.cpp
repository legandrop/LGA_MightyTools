#include "platform/ToastActivation.h"

// macOS: los avisos salen por osascript, sin acciones ni click que vuelva a la app. Nada que anotar
// ni atender.

namespace ToastActivation {

QString appUserModelId()
{
    return QString();
}

bool supported()
{
    return false;
}

bool ensureRegistered(const QString &, QString *detail)
{
    if (detail) {
        *detail = QStringLiteral("sin activaciones en esta plataforma");
    }
    return false;
}

bool registeredForThisExe()
{
    return false;
}

void listen()
{
}

void setHandler(Handler)
{
}

void stopListening()
{
}

bool removeIfOwned(QString *detail)
{
    *detail = QStringLiteral("nada que borrar en esta plataforma");
    return true;
}

} // namespace ToastActivation
