#include "platform/AutoStart.h"

#include <QDebug>

#import <Foundation/Foundation.h>
#import <ServiceManagement/ServiceManagement.h>

// "Open at login" con SMAppService.mainApp (macOS 13 o posterior): registra el propio .app como
// item de inicio, sin plist ni helper. En versiones anteriores no se ofrece.
//
// Sin compilar todavia en un Mac: la primera compilacion en macOS la valida. Se compila con ARC.

namespace AutoStart {

QString storedCommand()
{
    return QString();
}

bool disabledByTaskManager()
{
    return false;
}

bool isEnabled()
{
    if (@available(macOS 13.0, *)) {
        return SMAppService.mainAppService.status == SMAppServiceStatusEnabled;
    }
    return false;
}

bool setEnabled(bool enabled)
{
    if (@available(macOS 13.0, *)) {
        NSError *error = nil;
        const BOOL ok = enabled ? [SMAppService.mainAppService registerAndReturnError:&error]
                                : [SMAppService.mainAppService unregisterAndReturnError:&error];
        if (!ok) {
            qWarning() << "[AutoStart] SMAppService fallo:"
                       << QString::fromNSString(error.localizedDescription ?: @"(sin detalle)");
        }
        return ok;
    }
    return false;
}

bool removeIfOwned(QString *detail)
{
    // El item de SMAppService.mainApp es del propio .app: se va con el bundle al tirarlo a la
    // papelera. No hay desinstalador en mac.
    if (detail) {
        *detail = QStringLiteral("Login item: nada que limpiar en macOS");
    }
    return true;
}

Availability availability()
{
    if (@available(macOS 13.0, *)) {
        if (runsFromDevelopmentTree()) {
            return {false, Unavailability::DevelopmentTree, QStringLiteral("Not available from a development build")};
        }
        return {true, Unavailability::None, QString()};
    }
    return {false, Unavailability::Unsupported, QStringLiteral("Needs macOS 13 or later")};
}

} // namespace AutoStart
