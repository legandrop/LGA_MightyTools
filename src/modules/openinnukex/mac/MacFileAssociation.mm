#include "modules/openinnukex/mac/MacFileAssociation.h"

#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <QTimer>

namespace {

/// El UTI de los `.nk` se resuelve por extension, no hardcodeado: si otra app declara su propio
/// UTI para `.nk`, el que manda para el doble click es el que resuelva el sistema, no el nuestro.
UTType *nkContentType()
{
    return [UTType typeWithFilenameExtension:@"nk"];
}

} // namespace

namespace MacFileAssociation {

bool isDefaultNkHandler()
{
    UTType *type = nkContentType();
    NSURL *bundleUrl = NSBundle.mainBundle.bundleURL;
    if (!type || !bundleUrl) {
        return false;
    }

    NSURL *current = [NSWorkspace.sharedWorkspace URLForApplicationToOpenContentType:type];
    if (!current) {
        return false;
    }

    // Comparacion por ruta canonica, no por isEqual: las dos URL pueden describir el mismo bundle
    // con distinto trailing slash o via un symlink.
    return [current.URLByResolvingSymlinksInPath.path
        isEqualToString:bundleUrl.URLByResolvingSymlinksInPath.path];
}

void setAsDefaultNkHandler(std::function<void(bool ok, const QString &error)> done)
{
    UTType *type = nkContentType();
    NSURL *bundleUrl = NSBundle.mainBundle.bundleURL;
    if (!type || !bundleUrl) {
        if (done) {
            done(false, QStringLiteral("no se pudo resolver el UTI de .nk o el bundle de la app"));
        }
        return;
    }

    // Si ya somos el handler no se llama a la API: el sistema muestra su cartel de permiso cada
    // vez que se le pide un CAMBIO, y pedirle que confirme algo que ya esta hecho es ruido.
    if (isDefaultNkHandler()) {
        if (done) {
            done(true, QString());
        }
        return;
    }

    [NSWorkspace.sharedWorkspace
        setDefaultApplicationAtURL:bundleUrl
                  toOpenContentType:type
                  completionHandler:^(NSError *_Nullable error) {
        const bool ok = (error == nil);
        const QString detail = ok ? QString() : QString::fromNSString(error.localizedDescription);
        if (!done) {
            return;
        }
        // El completion de NSWorkspace no garantiza el hilo principal: se vuelve al hilo de UI
        // por la cola de eventos de Qt antes de tocar cualquier widget.
        QTimer::singleShot(0, [done, ok, detail]() { done(ok, detail); });
    }];
}

} // namespace MacFileAssociation
