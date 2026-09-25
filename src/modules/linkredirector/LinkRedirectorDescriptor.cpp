#include "modules/linkredirector/LinkRedirectorDescriptor.h"
#include "modules/linkredirector/LinkRedirectorModule.h"
#include "modules/linkredirector/LinkRedirectorExternal.h"
#include "modules/linkredirector/LinkRedirectorRouting.h"
#include "modules/linkredirector/BrowserDetection.h"
#include "modules/linkredirector/BrowserRegistration.h"
#include "core/AppSettings.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <QPainterPath>

#include <cstdio>

namespace {

// Icono vectorial simple: una flecha que se bifurca en dos, para "abre cada link en el navegador
// correcto" (canvas, seccion 2 "Link Redirector"). Mismo estilo que Icons::paint (viewBox + stroke,
// escalado al rect).
void paintForkIcon(QPainter &painter, const QRectF &rect, const QColor &color)
{
    constexpr qreal kViewBox = 16.0;
    constexpr qreal kStroke = 1.4;

    QPainterPath path;
    path.moveTo(2.2, 8.0);
    path.lineTo(7.0, 8.0);
    path.moveTo(7.0, 8.0);
    path.lineTo(12.6, 3.2);
    path.moveTo(7.0, 8.0);
    path.lineTo(12.6, 12.8);
    // Puntas de flecha en las dos ramas.
    path.moveTo(9.8, 3.5);
    path.lineTo(12.6, 3.2);
    path.lineTo(12.3, 6.0);
    path.moveTo(9.8, 12.5);
    path.lineTo(12.6, 12.8);
    path.lineTo(12.3, 10.0);

    const qreal scale = qMin(rect.width(), rect.height()) / kViewBox;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.translate(rect.center().x() - kViewBox * scale / 2.0, rect.center().y() - kViewBox * scale / 2.0);
    painter.scale(scale, scale);
    painter.setPen(QPen(color, kStroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
    painter.restore();
}

// Adaptador para leer [linkRedirector] de settings.ini directamente, para las funciones del
// descriptor que no reciben un SettingsReader (offNotice: Module.h no se lo da, a diferencia de
// ExternalRequest::value). Nunca escribe.
SettingsReader appSettingsReader()
{
    return [](const QString &key, const QVariant &defaultValue) -> QVariant {
        const auto settings = AppSettings::open();
        return settings->value(QStringLiteral("linkRedirector/%1").arg(key), defaultValue);
    };
}

// Navegador al que iria un link con la herramienta apagada (paso directo, D-05), para el aviso de la
// seccion de apagado. Comparte la regla con LinkRedirectorExternal::handle (decideDisabled).
QString resolveOffModeBrowserExe(const SettingsReader &value)
{
    const LinkRedirectorRouting::Rules rules = LinkRedirectorRouting::rulesFromReader(value);
    QList<QString> fallbackExePaths;
    for (const DetectedBrowser &browser : LinkRedirectorBrowsers::installedBrowsers()) {
        fallbackExePaths << browser.exePath;
    }
    return LinkRedirectorRouting::decideDisabled(rules, fallbackExePaths).chosenExe;
}

ModuleOffNotice linkRedirectorOffNotice(const QString &captureState)
{
    ModuleOffNotice notice;
    bool stillSendingLinksHere = false;
    QString browserName;

    if (!captureState.isEmpty()) {
        // Estado de prueba de la captura (--ui-shot, --ui-probe): sin leer nada del sistema.
        if (captureState == QLatin1String("still-default")) {
            stillSendingLinksHere = true;
            browserName = QStringLiteral("Google Chrome"); // mismo ejemplo que el tablero de diseno
        }
    } else {
        // Windows solo le "sigue mandando links" a esta app si sigue siendo el default REAL del
        // sistema (no alcanza con estar registrada como candidata: ver BrowserRegistration.h).
        stillSendingLinksHere = LinkRedirectorBrowserRegistration::isDefaultBrowser();
        if (stillSendingLinksHere) {
            const QString exe = resolveOffModeBrowserExe(appSettingsReader());
            browserName = exe.isEmpty() ? QString() : LinkRedirectorBrowsers::friendlyNameForExe(exe);
        }
    }

    notice.visible = stillSendingLinksHere;
    if (!stillSendingLinksHere) {
        return notice;
    }
    notice.title = QStringLiteral("Windows still sends links here");
    notice.caption = QStringLiteral("They open in %1, your default browser in this tool.")
        .arg(browserName.isEmpty() ? QStringLiteral("your default browser") : browserName);
    notice.actionText = QStringLiteral("Remove as browser");
    return notice;
}

bool linkRedirectorReleaseSystem(QString *error)
{
    return LinkRedirectorBrowserRegistration::unregisterAsBrowser(error);
}

void linkRedirectorSelfTest(const std::function<void(bool ok, const QString &what)> &check)
{
    using namespace LinkRedirectorRouting;

    // ---- claimsExternal: positivos y los casos negativos pedidos (--settings, ruta a .txt, mailto:).
    for (const char *yes : {"http://example.com", "https://example.com/x", "C:/x/y.html",
                            "C:/x/y.HTML", "page.xhtml", "https://Example.com/Frame.IO"}) {
        check(LinkRedirectorExternal::claims(QString::fromLatin1(yes)),
              QStringLiteral("claimsExternal acepta: %1").arg(QLatin1String(yes)));
    }
    for (const char *no : {"--settings", "C:/x/notes.txt", "mailto:lega@example.com", "--self-test", ""}) {
        check(!LinkRedirectorExternal::claims(QString::fromLatin1(no)),
              QStringLiteral("claimsExternal rechaza: '%1'").arg(QLatin1String(no)));
    }

    // ---- Ruteo por palabras: mayusculas, substring, vacias.
    check(matchesAnyWord(QStringLiteral("https://NetflixStudios.com/x"), {QStringLiteral("netflixstudios")}),
          QStringLiteral("match: sin importar mayusculas"));
    check(matchesAnyWord(QStringLiteral("https://sub.frame.io/abc"), {QStringLiteral("frame.io")}),
          QStringLiteral("match: substring en el medio de la URL"));
    check(!matchesAnyWord(QStringLiteral("https://example.com"), {QStringLiteral("netflixstudios")}),
          QStringLiteral("match: no matchea si no esta la palabra"));
    check(!matchesAnyWord(QStringLiteral("https://example.com"), {QString(), QStringLiteral("   ")}),
          QStringLiteral("match: las palabras vacias (o solo espacios) no matchean todo"));
    check(matchesAnyWord(QStringLiteral("https://frame.io"), {QString(), QStringLiteral("frame.io")}),
          QStringLiteral("match: una vacia en la lista no rompe el resto"));

    // ---- decideEnabled: fallback al otro navegador y ninguno valido.
    const QString existingExe = QDir::fromNativeSeparators(QCoreApplication::applicationFilePath());
    const QString missingExe = QStringLiteral("C:/no/existe/browser.exe");

    {
        // Default vacio -> AlternativeNotSet no aplica (matched=false), reason=DefaultNotSet,
        // fallback al alternativo si sirve.
        Rules rules;
        rules.alternativeBrowser = existingExe;
        const Decision d = decideEnabled(QStringLiteral("https://example.com"), rules);
        check(!d.matched, QStringLiteral("decideEnabled: URL sin palabras clave no matchea"));
        check(d.usedFallback && d.chosenExe == existingExe && d.reason == UnavailableReason::DefaultNotSet,
              QStringLiteral("decideEnabled: default sin configurar cae al alternativo"));
    }
    {
        // Default configurado pero con ruta que no existe -> DefaultPathMissing, fallback al alternativo.
        Rules rules;
        rules.defaultBrowser = missingExe;
        rules.alternativeBrowser = existingExe;
        const Decision d = decideEnabled(QStringLiteral("https://example.com"), rules);
        check(d.usedFallback && d.chosenExe == existingExe && d.reason == UnavailableReason::DefaultPathMissing,
              QStringLiteral("decideEnabled: default con ruta borrada cae al alternativo"));
    }
    {
        // Match -> alternativo elegido; si su ruta no existe y el default tampoco, ninguno sirve.
        Rules rules;
        rules.defaultBrowser = missingExe;
        rules.alternativeBrowser = missingExe;
        rules.matchWords = {QStringLiteral("frame.io")};
        const Decision d = decideEnabled(QStringLiteral("https://frame.io/x"), rules);
        check(d.matched, QStringLiteral("decideEnabled: matchea la palabra clave"));
        check(d.chosenExe.isEmpty(), QStringLiteral("decideEnabled: ningun navegador valido, no se abre nada"));
    }
    {
        // Los dos configurados y validos: se usa el elegido, sin fallback.
        Rules rules;
        rules.defaultBrowser = existingExe;
        rules.alternativeBrowser = existingExe;
        const Decision d = decideEnabled(QStringLiteral("https://example.com"), rules);
        check(!d.usedFallback && d.chosenExe == existingExe,
              QStringLiteral("decideEnabled: navegador default valido, sin fallback"));
    }

    // ---- decideDisabled (paso directo D-05): ignora match words.
    {
        Rules rules;
        rules.defaultBrowser = existingExe;
        rules.matchWords = {QStringLiteral("frame.io")}; // no deberia importar apagada
        const OffDecision d = decideDisabled(rules, {});
        check(!d.usedDetectedFallback && d.chosenExe == existingExe,
              QStringLiteral("decideDisabled: usa el default configurado, ignora matchWords"));
    }
    {
        Rules rules; // sin defaultBrowser configurado
        const OffDecision d = decideDisabled(rules, {existingExe});
        check(d.usedDetectedFallback && d.chosenExe == existingExe,
              QStringLiteral("decideDisabled: sin default configurado, cae al primero detectado"));
    }
    {
        Rules rules;
        const OffDecision d = decideDisabled(rules, {});
        check(d.chosenExe.isEmpty(), QStringLiteral("decideDisabled: sin default y sin detectados, no abre nada"));
    }

    // ---- Avisos: texto exacto (canvas, seccion 5 "Browser settings").
    check(warningTitle() == QLatin1String("Browser settings"), QStringLiteral("aviso: titulo exacto"));
    check(reasonText(UnavailableReason::DefaultNotSet) == QLatin1String("The default browser isn't set."),
          QStringLiteral("aviso: motivo 'default no configurado'"));
    check(reasonText(UnavailableReason::AlternativeNotSet) == QLatin1String("The alternative browser isn't set."),
          QStringLiteral("aviso: motivo 'alternativo no configurado'"));
    check(reasonText(UnavailableReason::DefaultPathMissing) == QLatin1String("The default browser's path no longer exists."),
          QStringLiteral("aviso: motivo 'ruta del default borrada'"));
    check(reasonText(UnavailableReason::AlternativePathMissing) == QLatin1String("The alternative browser's path no longer exists."),
          QStringLiteral("aviso: motivo 'ruta del alternativo borrada'"));
    check(warningCaption(UnavailableReason::AlternativePathMissing, QStringLiteral("C:/x/chrome.exe"))
              == QLatin1String("The alternative browser's path no longer exists. This link opens with chrome.exe for now."),
          QStringLiteral("aviso: cuerpo compuesto con el nombre del exe, sin la ruta"));

    // ---- Sincronizacion "el que era default pasa a Default browser" (etapa 2, mainwindow.cpp:1562-1601 del origen).
    check(browserToSyncAsDefault(true, existingExe) == existingExe,
          QStringLiteral("sync: si ahora es default y habia uno guardado, lo sincroniza"));
    check(browserToSyncAsDefault(false, existingExe).isEmpty(),
          QStringLiteral("sync: si todavia no es default, no sincroniza"));
    check(browserToSyncAsDefault(true, QString()).isEmpty(),
          QStringLiteral("sync: si no habia ninguno guardado, no sincroniza"));

    // ---- Privacidad: el log nunca lleva la URL completa (solo el host, o el nombre de archivo).
    const QString sensitiveUrl = QStringLiteral("https://example.com/private/report?token=abc123");
    const QString logged = logTarget(sensitiveUrl);
    check(logged == QLatin1String("example.com"), QStringLiteral("log: solo el host de la URL"));
    check(!logged.contains(QLatin1String("private")) && !logged.contains(QLatin1String("token")),
          QStringLiteral("log: la URL completa no queda en el log"));
    const QString loggedFile = logTarget(QStringLiteral("C:/Users/lega/Desktop/secret_report.html"));
    check(loggedFile == QLatin1String("secret_report.html") && !loggedFile.contains(QLatin1String("Desktop")),
          QStringLiteral("log: un archivo local loguea solo su nombre, sin la ruta"));

    // ---- Exclusion de esta misma app de la lista de navegadores (BrowserRegistration.h).
    check(LinkRedirectorBrowserRegistration::isOwnHandlerId(LinkRedirectorBrowserRegistration::ownHandlerId()),
          QStringLiteral("exclusion: el propio handler id se reconoce como propio"));
    check(!LinkRedirectorBrowserRegistration::isOwnHandlerId(QStringLiteral("BraveHTML")),
          QStringLiteral("exclusion: un handler ajeno no se confunde con el propio"));
    check(!LinkRedirectorBrowserRegistration::isOwnHandlerId(QString()),
          QStringLiteral("exclusion: un handler vacio no se confunde con el propio"));
}

int linkRedirectorSimulateAction(const QString &action, const QStringList &args)
{
    if (action != QLatin1String("route") || args.isEmpty()) {
        std::fprintf(stderr, "usage: --simulate-action route <url>\n");
        return 2;
    }

    ExternalRequest request;
    request.argument = args.first();
    request.moduleEnabled = true; // aplica la regla completa (match words), para mostrar el ruteo
    request.dryRun = true;        // 🔴 nunca abre nada de verdad
    request.resident = false;
    request.value = appSettingsReader();

    const ExternalResult result = LinkRedirectorExternal::handle(request);
    return result == ExternalResult::Done ? 0 : 1;
}

} // namespace

ModuleDescriptor linkRedirectorDescriptor()
{
    ModuleDescriptor d;
    d.id = QStringLiteral("linkRedirector");
    d.title = QStringLiteral("Link Redirector");
    d.description = QStringLiteral(
        "Opens each link in the right browser: links with your keywords go to the alternative browser.");
    d.offBullets = {
        QStringLiteral("Becomes the system default browser"),
        QStringLiteral("Routes each link by keyword"),
        QStringLiteral("Everything else opens in your default browser"),
    };
    d.platforms = PlatformWindows | PlatformMac;
    d.paintIcon = paintForkIcon;
    d.create = [](ModuleContext &context) -> std::unique_ptr<Module> {
        return std::make_unique<LinkRedirectorModule>(context);
    };

    d.offNotice = linkRedirectorOffNotice;
    d.releaseSystem = linkRedirectorReleaseSystem;

    d.claimsExternal = LinkRedirectorExternal::claims;
    d.runExternal = LinkRedirectorExternal::handle;

    d.selfTest = linkRedirectorSelfTest;
    d.simulateAction = linkRedirectorSimulateAction;

    return d;
}
