#include "platform/WhatsAppSystem.h"

#include "core/AutomatedRun.h"
#include "core/I18n.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QUrl>

#import <AppKit/AppKit.h>

// macOS:
//  - la app: la que el sistema usa para `whatsapp://` (la de WhatsApp registra el protocolo);
//  - las apps web de Chromium: los .app de `~/Applications/<Navegador> Apps.localized` (y sueltos en
//    `~/Applications`) cuyo Info.plist dice `CrAppModeShortcutURL` en web.whatsapp.com; las de Safari
//    («Agregar al Dock»): bundle `com.apple.Safari.WebApp.*` con WhatsApp en el nombre.
//  - abierta = el proceso del .app corre (la app web de Chromium tiene su propio proceso mientras esta abierta).

namespace {

bool isRunning(NSString *bundleId)
{
    if (bundleId.length == 0) return false;
    for (NSRunningApplication *app in [NSRunningApplication runningApplicationsWithBundleIdentifier:bundleId]) {
        if (!app.terminated) return true;
    }
    return false;
}

QString browserName(NSString *bundleId)
{
    const QString id = QString::fromNSString(bundleId);
    if (id == QLatin1String("com.brave.Browser")) return QStringLiteral("Brave");
    if (id == QLatin1String("com.google.Chrome")) return QStringLiteral("Chrome");
    if (id == QLatin1String("com.microsoft.edgemac")) return QStringLiteral("Edge");
    if (id == QLatin1String("com.vivaldi.Vivaldi")) return QStringLiteral("Vivaldi");
    if (id == QLatin1String("company.thebrowser.Browser")) return QStringLiteral("Arc");
    NSURL *url = [NSWorkspace.sharedWorkspace URLForApplicationWithBundleIdentifier:bundleId];
    return url ? QFileInfo(QString::fromNSString(url.path)).completeBaseName() : id;
}

void addWebApp(const QString &bundlePath, QList<WhatsAppSystem::Target> *targets)
{
    NSBundle *bundle = [NSBundle bundleWithPath:bundlePath.toNSString()];
    if (!bundle) return;
    NSDictionary *info = bundle.infoDictionary;
    NSString *bundleId = bundle.bundleIdentifier ?: @"";
    NSString *name = info[@"CFBundleName"] ?: @"";
    NSString *startUrl = info[@"CrAppModeShortcutURL"];
    WhatsAppSystem::Target target;
    target.kind = WhatsAppSystem::Kind::WebApp;
    target.path = bundlePath;
    target.name = QString::fromNSString(name);
    if ([startUrl isKindOfClass:NSString.class]) {
        if (!QUrl(QString::fromNSString(startUrl)).host().endsWith(QLatin1String("web.whatsapp.com"), Qt::CaseInsensitive)) return;
        NSString *browserId = info[@"CrBundleIdentifier"] ?: @"";
        target.browser = browserName(browserId);
        target.appId = QString::fromNSString(info[@"CrAppModeShortcutID"] ?: @"");
        // El perfil donde esta instalada (versiones que lo escriben); sin el, el navegador usa el ultimo usado.
        NSString *profile = info[@"CrAppModeProfileDir"];
        if ([profile isKindOfClass:NSString.class] && profile.length > 0) {
            target.profile = QFileInfo(QString::fromNSString(profile)).fileName();
        }
        NSURL *browserUrl = [NSWorkspace.sharedWorkspace URLForApplicationWithBundleIdentifier:browserId];
        NSBundle *browserBundle = browserUrl ? [NSBundle bundleWithURL:browserUrl] : nil;
        if (browserBundle.executablePath) target.launcher = QString::fromNSString(browserBundle.executablePath);
    } else if ([bundleId hasPrefix:@"com.apple.Safari.WebApp"]) {
        if (!target.name.contains(QLatin1String("WhatsApp"), Qt::CaseInsensitive)) return;
        target.browser = QStringLiteral("Safari");
    } else {
        return;
    }
    if (target.name.isEmpty()) target.name = QStringLiteral("WhatsApp Web");
    target.running = isRunning(bundleId);
    targets->append(target);
}

} // namespace

namespace WhatsAppSystem {

QList<Target> detect()
{
    QList<Target> targets;
    @autoreleasepool {
        NSURL *handler = [NSWorkspace.sharedWorkspace URLForApplicationToOpenURL:[NSURL URLWithString:@"whatsapp://send"]];
        if (handler) {
            NSBundle *bundle = [NSBundle bundleWithURL:handler];
            Target app;
            app.kind = Kind::App;
            app.path = QString::fromNSString(handler.path);
            app.name = QFileInfo(app.path).completeBaseName();
            app.running = isRunning(bundle.bundleIdentifier);
            targets.append(app);
        }
        const QString home = QDir::homePath() + QStringLiteral("/Applications");
        QStringList folders{home};
        const QDir homeApps(home);
        for (const QString &sub : homeApps.entryList({QStringLiteral("*.localized")}, QDir::Dirs | QDir::NoDotAndDotDot)) {
            folders.append(homeApps.filePath(sub));
        }
        for (const QString &folder : folders) {
            const QDir dir(folder);
            for (const QString &entry : dir.entryList({QStringLiteral("*.app")}, QDir::Dirs | QDir::NoDotAndDotDot)) {
                addWebApp(dir.filePath(entry), &targets);
            }
        }
    }
    return targets;
}

bool open(const Target &target, const QString &digits, QString *error)
{
    if (error) error->clear();
    // Segunda barrera (la primera es el modulo): una corrida automatizada nunca abre nada.
    if (AutomatedRun::active()) {
        qInfo().noquote() << "[whatsApp] Corrida automatizada, no se abre:" << describe(target);
        return true;
    }
    @autoreleasepool {
        NSWorkspace *workspace = NSWorkspace.sharedWorkspace;
        if (target.kind == Kind::App) {
            if (![workspace openURL:[NSURL URLWithString:appUrl(digits).toNSString()]]) {
                if (error) *error = I18n::tr("WhatsApp could not be opened.");
                return false;
            }
            return true;
        }
        NSURL *web = [NSURL URLWithString:webUrl(digits).toNSString()];
        if (target.kind == Kind::WebApp) {
            // Chromium: el ejecutable del navegador con el id de la app. Si el navegador ya corre, el proceso
            // nuevo le pasa el pedido al que corre y sale.
            if (!target.launcher.isEmpty() && !target.appId.isEmpty()) {
                QStringList args{QStringLiteral("--app-id=%1").arg(target.appId),
                                 QStringLiteral("--app-launch-url-for-shortcuts-menu-item=%1").arg(webUrl(digits))};
                if (!target.profile.isEmpty()) args.prepend(QStringLiteral("--profile-directory=%1").arg(target.profile));
                if (QProcess::startDetached(target.launcher, args)) return true;
            }
            // Safari, o sin el navegador: la URL al .app de la app web.
            NSURL *app = [NSURL fileURLWithPath:target.path.toNSString()];
            NSWorkspaceOpenConfiguration *configuration = [NSWorkspaceOpenConfiguration configuration];
            configuration.activates = YES;
            [workspace openURLs:@[web] withApplicationAtURL:app configuration:configuration completionHandler:nil];
            return true;
        }
        if (![workspace openURL:web]) {
            if (error) *error = I18n::tr("The browser could not be opened.");
            return false;
        }
    }
    return true;
}

} // namespace WhatsAppSystem
