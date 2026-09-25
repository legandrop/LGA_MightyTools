#include "modules/linkredirector/BrowserRegistration.h"
#include "modules/linkredirector/BrowserDetection.h"

#import <AppKit/AppKit.h>
#import <CoreServices/CoreServices.h>

#include <QByteArray>
#include <QDesktopServices>
#include <QUrl>

// Port de LGA_LinkRedirector src/macos/DefaultBrowser.cpp + src/macos/MacIntegration.mm (v0.173),
// fusionados en un solo archivo (Mighty Tools no separa la activacion de ventana, propia de la app
// de menu bar vieja, de la parte de navegador).
//
// Se compila solo bajo APPLE (ver CMakeLists.txt); no se compilo ni se corrio en esta maquina.

namespace {

QString cfStringToQString(CFStringRef value)
{
    if (!value) {
        return QString();
    }
    const CFIndex length = CFStringGetLength(value);
    const CFIndex maxBytes = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
    QByteArray buffer(static_cast<int>(maxBytes), '\0');
    if (!CFStringGetCString(value, buffer.data(), maxBytes, kCFStringEncodingUTF8)) {
        return QString();
    }
    return QString::fromUtf8(buffer.constData());
}

// Bundle id declarado en CMakeLists.txt (MACOSX_BUNDLE_GUI_IDENTIFIER).
QString ownBundleId()
{
    return QStringLiteral("com.lga.mightytools");
}

// NSWorkspace.setDefaultApplication muestra el prompt de confirmacion del sistema (macOS 12+); en
// versiones viejas se pide por la API de LaunchServices, sin prompt.
bool requestSystemDefaultBrowser()
{
    NSURL *appUrl = NSBundle.mainBundle.bundleURL;
    if (!appUrl) {
        return false;
    }

    if (@available(macOS 12.0, *)) {
        [NSWorkspace.sharedWorkspace setDefaultApplicationAtURL:appUrl
                                          toOpenURLsWithScheme:@"http"
                                             completionHandler:^(NSError *_Nullable error) {
            if (error) {
                NSLog(@"[linkRedirector] setDefaultApplication error: %@", error);
            }
        }];
        return true; // el prompt de confirmacion del sistema maneja el resto (async)
    }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    NSString *bundleId = NSBundle.mainBundle.bundleIdentifier;
    OSStatus st = LSSetDefaultHandlerForURLScheme(CFSTR("http"), (__bridge CFStringRef)bundleId);
    OSStatus st2 = LSSetDefaultHandlerForURLScheme(CFSTR("https"), (__bridge CFStringRef)bundleId);
#pragma clang diagnostic pop
    return st == noErr && st2 == noErr;
}

} // namespace

namespace LinkRedirectorBrowserRegistration {

QString ownHandlerId()
{
    return ownBundleId();
}

bool isOwnHandlerId(const QString &handlerId)
{
    return !handlerId.isEmpty() && handlerId.compare(ownBundleId(), Qt::CaseInsensitive) == 0;
}

bool registerAsBrowser(QString *error)
{
    Q_UNUSED(error);
    // Registrar el propio bundle en LaunchServices para aparecer como candidato a navegador en la
    // lista del sistema. Sin esto, declarar los schemes en el Info.plist no alcanza (y un registro
    // viejo/roto puede tapar al de /Applications).
    CFURLRef bundleUrl = CFBundleCopyBundleURL(CFBundleGetMainBundle());
    if (!bundleUrl) {
        if (error) {
            *error = QStringLiteral("No se pudo obtener la URL del bundle.");
        }
        return false;
    }
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    const OSStatus status = LSRegisterURL(bundleUrl, /*inUpdate=*/true);
#pragma clang diagnostic pop
    CFRelease(bundleUrl);
    const bool ok = status == noErr;
    if (!ok && error) {
        *error = QStringLiteral("LSRegisterURL devolvio un error (%1).").arg(status);
    }
    return ok;
}

bool unregisterAsBrowser(QString *error)
{
    Q_UNUSED(error);
    // No hay desregistro programatico equivalente al de Windows: LaunchServices no ofrece "olvidar"
    // un bundle ya registrado (mismo comportamiento que el origen). "Remove as browser" en mac
    // queda pendiente hasta que exista una via del sistema; documentado como limite heredado.
    return true;
}

bool isRegistered()
{
    return !ownBundleId().isEmpty();
}

QString currentDefaultHandlerId()
{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    CFStringRef handler = LSCopyDefaultHandlerForURLScheme(CFSTR("http"));
#pragma clang diagnostic pop
    if (!handler) {
        return QString();
    }
    const QString value = cfStringToQString(handler);
    CFRelease(handler);
    return value;
}

bool isDefaultBrowser()
{
    return currentDefaultHandlerId().compare(ownBundleId(), Qt::CaseInsensitive) == 0;
}

QString currentDefaultBrowserName()
{
    const QString handlerId = currentDefaultHandlerId();
    if (handlerId.isEmpty()) {
        return QString();
    }
    if (isOwnHandlerId(handlerId)) {
        return QStringLiteral("LGA Mighty Tools");
    }
    const QString name = LinkRedirectorBrowsers::nameForHandlerId(handlerId);
    return name.isEmpty() ? handlerId : name;
}

QString exePathForHandlerId(const QString &handlerId)
{
    if (handlerId.isEmpty() || isOwnHandlerId(handlerId)) {
        return QString();
    }
    for (const DetectedBrowser &browser : LinkRedirectorBrowsers::installedBrowsersRaw()) {
        if (browser.handlerId.compare(handlerId, Qt::CaseInsensitive) == 0) {
            return browser.exePath;
        }
    }
    return QString();
}

void openDefaultAppsSettings()
{
    const QStringList urls = {
        QStringLiteral("x-apple.systempreferences:com.apple.settings.Desktop-Dock.extension?DefaultWebBrowser"),
        QStringLiteral("x-apple.systempreferences:com.apple.preference.general"),
        QStringLiteral("file:///System/Applications/System Settings.app"),
    };
    for (const QString &url : urls) {
        if (QDesktopServices::openUrl(QUrl(url))) {
            return;
        }
    }
}

bool requestSetAsDefault()
{
    registerAsBrowser(nullptr); // asegurar que somos candidato antes de pedir el default
    return requestSystemDefaultBrowser();
}

} // namespace LinkRedirectorBrowserRegistration
