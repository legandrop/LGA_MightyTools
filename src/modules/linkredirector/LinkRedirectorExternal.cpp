#include "modules/linkredirector/LinkRedirectorExternal.h"
#include "modules/linkredirector/LinkRedirectorRouting.h"
#include "modules/linkredirector/BrowserDetection.h"
#include "app/ModuleContext.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>

namespace {

// Unico punto de salida del aviso "navegador no disponible" (tablero de diseno, seccion 5). Siempre
// loguea (privacidad + diagnostico); en corrida automatizada (request.dryRun) nunca muestra nada mas.
// Si no, dos caminos segun quien haya recibido el link:
//  - Modo corto de Windows (context == nullptr): QMessageBox::warning con el Theme de la app, el
//    mismo sistema de dialogos que el updater (Theme::apply ya corrio en main.cpp antes de este
//    camino: ver ExternalDispatch::run, llamado despues de applyAppStyle(app)).
//  - App residente de mac (context != nullptr, Module::handleExternal con la herramienta prendida):
//    context->notify(...), la notificacion de bandeja de la app (el click abre el panel).
void showBrowserWarning(const QString &title, const QString &caption, bool dryRun, ModuleContext *context)
{
    qWarning() << "[linkRedirector]" << title << "-" << caption;
    if (dryRun) {
        return;
    }
    if (context) {
        context->notify(title, caption, ModuleContext::NoticeIcon::Warning, 8000);
        return;
    }
    QMessageBox::warning(nullptr, title, caption);
}

// Abre `target` (URL o archivo local) con el ejecutable `exePath`. Nunca se llama en dryRun.
bool launchBrowser(const QString &exePath, const QString &target)
{
#if defined(Q_OS_MACOS)
    // En mac se lanza via LaunchServices ("open -a <bundle>") y no el binario interno: asi funciona
    // con cualquier navegador, incluido Safari (que ignora la URL pasada por argv). Derivamos el
    // .app desde el ejecutable interno guardado (.../Xxx.app/Contents/MacOS/Xxx -> .../Xxx.app).
    QString appBundle = exePath;
    if (!appBundle.endsWith(QLatin1String(".app"), Qt::CaseInsensitive)) {
        QDir bundleDir(QFileInfo(exePath).absolutePath()); // .../Contents/MacOS
        if (bundleDir.cdUp() && bundleDir.cdUp() && bundleDir.path().endsWith(QLatin1String(".app"), Qt::CaseInsensitive)) {
            appBundle = bundleDir.path();
        }
    }
    const bool ok = QProcess::startDetached(QStringLiteral("/usr/bin/open"), {QStringLiteral("-a"), appBundle, target});
    if (!ok) {
        qWarning() << "[linkRedirector] Fallo al lanzar via open:" << appBundle;
    }
    return ok;
#else
    // Convertir a separadores nativos (CreateProcess es mas confiable con backslashes).
    const QString nativeExe = QDir::toNativeSeparators(exePath);
    const bool ok = QProcess::startDetached(nativeExe, {target});
    if (!ok) {
        qWarning() << "[linkRedirector] Fallo al lanzar:" << nativeExe;
    }
    return ok;
#endif
}

} // namespace

namespace LinkRedirectorExternal {

bool claims(const QString &argument)
{
    if (argument.startsWith(QLatin1String("--"))) {
        return false;
    }
    if (argument.startsWith(QLatin1String("http://"), Qt::CaseInsensitive) ||
        argument.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)) {
        return true;
    }
    const QString lower = argument.toLower();
    return lower.endsWith(QLatin1String(".htm")) || lower.endsWith(QLatin1String(".html")) ||
           lower.endsWith(QLatin1String(".xhtml"));
}

ExternalResult handle(const ExternalRequest &request, ModuleContext *context)
{
    using namespace LinkRedirectorRouting;

    if (!claims(request.argument)) {
        return ExternalResult::NotMine;
    }

    const Rules rules = rulesFromReader(request.value);
    const QString logHost = LinkRedirectorRouting::logTarget(request.argument);

    QString chosenExe;
    bool usedFallback = false;
    UnavailableReason reason = UnavailableReason::None;

    if (request.moduleEnabled) {
        // Prendida: match words -> alternativo; si no -> default; fallback al otro si el elegido no
        // sirve, con aviso.
        const Decision d = decideEnabled(request.argument, rules);
        chosenExe = d.chosenExe;
        usedFallback = d.usedFallback;
        reason = d.reason;
    } else {
        // Apagada (D-05, paso directo): sin match words. Default browser configurado o, si no hay,
        // el primer navegador detectado (nunca esta misma app: BrowserDetection ya la excluye).
        QList<QString> fallbackExePaths;
        for (const DetectedBrowser &browser : LinkRedirectorBrowsers::installedBrowsers()) {
            fallbackExePaths << browser.exePath;
        }
        const OffDecision d = decideDisabled(rules, fallbackExePaths);
        chosenExe = d.chosenExe;
        usedFallback = d.usedDetectedFallback;
    }

    if (chosenExe.isEmpty()) {
        qWarning() << "[linkRedirector] Sin navegador valido para" << logHost << "- no se abre nada.";
        return ExternalResult::Done;
    }

    if (usedFallback && request.moduleEnabled && reason != UnavailableReason::None) {
        showBrowserWarning(warningTitle(), warningCaption(reason, chosenExe), request.dryRun, context);
    }

    if (request.dryRun) {
        qInfo() << "[linkRedirector] (dry-run)" << logHost << "->" << QFileInfo(chosenExe).fileName();
        return ExternalResult::Done;
    }

    qInfo() << "[linkRedirector]" << logHost << "->" << QFileInfo(chosenExe).fileName();
    launchBrowser(chosenExe, request.argument);
    return ExternalResult::Done;
}

} // namespace LinkRedirectorExternal
