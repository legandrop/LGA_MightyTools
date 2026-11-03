#include "platform/AppToggleSystem.h"

#include "core/I18n.h"

#include <QDebug>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QProcess>

#import <AppKit/AppKit.h>

// macOS actua sobre la app entera, sin el permiso de Accesibilidad:
//  - no corre o corre sin el foco: openApplicationAtURL con activates = YES, lo mismo que un click en
//    el Dock (activa, muestra lo oculto y saca del Dock una ventana minimizada);
//  - esta al frente: hide, como Cmd+H (vuelve con todas sus ventanas).

namespace {

// La ruta real (sin enlaces) para comparar; la de la entrada si no existe.
QString realPath(const QString &path)
{
    const QString canonical = QFileInfo(path).canonicalFilePath();
    return canonical.isEmpty() ? path : canonical;
}

QString realPath(NSURL *url)
{
    return url ? QString::fromNSString(url.URLByResolvingSymlinksInPath.path) : QString();
}

NSRunningApplication *runningApp(const QString &appPath)
{
    const QString wanted = realPath(appPath);
    for (NSRunningApplication *app in NSWorkspace.sharedWorkspace.runningApplications) {
        if (!app.terminated && AppToggleSystem::samePath(realPath(app.bundleURL), wanted)) {
            return app;
        }
    }
    return nil;
}

void fail(QString *error, const QString &message)
{
    if (error) *error = message;
}

} // namespace

namespace AppToggleSystem {

Snapshot snapshot(const QString &appPath)
{
    Snapshot snapshot;
    snapshot.wholeApp = true;
    @autoreleasepool {
        // Coinciden por la ruta real; a la decision se le pasa la de la entrada, que es con la que compara.
        if (runningApp(appPath)) {
            snapshot.processPaths << appPath;
        }
        NSRunningApplication *front = NSWorkspace.sharedWorkspace.frontmostApplication;
        if (front && samePath(realPath(front.bundleURL), realPath(appPath))) {
            snapshot.foregroundPath = appPath;
        } else if (front) {
            snapshot.foregroundPath = realPath(front.bundleURL);
        }
    }
    return snapshot;
}

bool run(const Decision &decision, const Target &target, QString *error)
{
    if (error) error->clear();
    if (decision.action == Action::Nothing) {
        return true;
    }
    @autoreleasepool {
        if (decision.action == Action::Minimize) {
            NSRunningApplication *app = runningApp(target.path);
            if (!app) {
                fail(error, I18n::tr("The app closed before it could be hidden."));
                return false;
            }
            if (![app hide]) {
                fail(error, I18n::tr("macOS did not let the app be hidden."));
                return false;
            }
            return true;
        }
        if (!QFileInfo(target.path).isDir()) {
            fail(error, I18n::tr("The app was not found: %1").arg(target.path));
            return false;
        }
        NSURL *url = [NSURL fileURLWithPath:target.path.toNSString() isDirectory:YES];
        NSWorkspaceOpenConfiguration *configuration = [NSWorkspaceOpenConfiguration configuration];
        configuration.activates = YES;
        NSMutableArray<NSString *> *arguments = [NSMutableArray array];
        for (const QString &argument : QProcess::splitCommand(target.arguments)) {
            [arguments addObject:argument.toNSString()];
        }
        configuration.arguments = arguments;
        const QString name = QFileInfo(target.path).fileName();
        // Asincronico: el resultado llega despues y solo se loguea.
        [NSWorkspace.sharedWorkspace openApplicationAtURL:url
                                            configuration:configuration
                                        completionHandler:^(NSRunningApplication *opened, NSError *openError) {
            (void)opened;
            if (openError) {
                qWarning() << "[appToggle] macOS no pudo abrir" << name << ":"
                           << QString::fromNSString(openError.localizedDescription);
            }
        }];
    }
    return true;
}

QIcon icon(const QString &appPath)
{
    // La plataforma de Qt en mac ya lo pide a NSWorkspace.
    return QFileInfo(appPath).isDir() ? QFileIconProvider().icon(QFileInfo(appPath)) : QIcon();
}

QString productName(const QString &appPath)
{
    @autoreleasepool {
        NSBundle *bundle = [NSBundle bundleWithPath:appPath.toNSString()];
        if (!bundle) {
            return {};
        }
        for (NSString *key in @[@"CFBundleDisplayName", @"CFBundleName"]) {
            id value = [bundle objectForInfoDictionaryKey:key];
            if ([value isKindOfClass:[NSString class]] && [(NSString *)value length] > 0) {
                return QString::fromNSString((NSString *)value).trimmed();
            }
        }
    }
    return {};
}

} // namespace AppToggleSystem
