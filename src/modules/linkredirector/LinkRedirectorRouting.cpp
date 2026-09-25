#include "modules/linkredirector/LinkRedirectorRouting.h"
#include "core/DebugFlags.h"

#include <QFileInfo>
#include <QUrl>

namespace LinkRedirectorRouting {

Rules rulesFromReader(const SettingsReader &value)
{
    Rules r;
    if (!value) {
        return r;
    }
    r.defaultBrowser = value(QStringLiteral("defaultBrowser"), QString()).toString();
    r.alternativeBrowser = value(QStringLiteral("alternativeBrowser"), QString()).toString();
    r.matchWords = value(QStringLiteral("matchWords"), QStringList()).toStringList();
    return r;
}

bool matchesAnyWord(const QString &url, const QStringList &words, QString *matchedWord)
{
    for (const QString &word : words) {
        const QString trimmed = word.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        if (url.contains(trimmed, Qt::CaseInsensitive)) {
            if (matchedWord) {
                *matchedWord = trimmed;
            }
            return true;
        }
    }
    return false;
}

Decision decideEnabled(const QString &url, const Rules &rules)
{
    Decision d;
    d.matched = matchesAnyWord(url, rules.matchWords);
    const QString primary = d.matched ? rules.alternativeBrowser : rules.defaultBrowser;
    const QString other = d.matched ? rules.defaultBrowser : rules.alternativeBrowser;

    if (!primary.isEmpty() && QFileInfo::exists(primary)) {
        d.chosenExe = primary;
        return d;
    }

    // El elegido no sirve: por que (para el aviso), y probar el otro como fallback.
    d.reason = primary.isEmpty()
        ? (d.matched ? UnavailableReason::AlternativeNotSet : UnavailableReason::DefaultNotSet)
        : (d.matched ? UnavailableReason::AlternativePathMissing : UnavailableReason::DefaultPathMissing);

    if (!other.isEmpty() && QFileInfo::exists(other)) {
        d.chosenExe = other;
        d.usedFallback = true;
        return d;
    }

    // Ninguno sirve: no se abre nada (queda en el log).
    d.chosenExe.clear();
    return d;
}

OffDecision decideDisabled(const Rules &rules, const QList<QString> &detectedFallbackExePaths)
{
    OffDecision d;
    if (!rules.defaultBrowser.isEmpty() && QFileInfo::exists(rules.defaultBrowser)) {
        d.chosenExe = rules.defaultBrowser;
        return d;
    }
    if (!detectedFallbackExePaths.isEmpty()) {
        d.chosenExe = detectedFallbackExePaths.first();
        d.usedDetectedFallback = true;
    }
    return d;
}

QString reasonText(UnavailableReason reason)
{
    switch (reason) {
    case UnavailableReason::DefaultNotSet:
        return QStringLiteral("The default browser isn't set.");
    case UnavailableReason::AlternativeNotSet:
        return QStringLiteral("The alternative browser isn't set.");
    case UnavailableReason::DefaultPathMissing:
        return QStringLiteral("The default browser's path no longer exists.");
    case UnavailableReason::AlternativePathMissing:
        return QStringLiteral("The alternative browser's path no longer exists.");
    case UnavailableReason::None:
        break;
    }
    return QString();
}

QString warningTitle()
{
    return QStringLiteral("Browser settings");
}

QString warningCaption(UnavailableReason reason, const QString &usedExe)
{
    const QString reasonPart = reasonText(reason);
    if (reasonPart.isEmpty()) {
        return QString();
    }
    return reasonPart + QStringLiteral(" This link opens with %1 for now.").arg(QFileInfo(usedExe).fileName());
}

QString browserToSyncAsDefault(bool isNowSystemDefault, const QString &previousSystemDefaultExePath)
{
    if (!isNowSystemDefault || previousSystemDefaultExePath.isEmpty()) {
        return QString();
    }
    return previousSystemDefaultExePath;
}

QString safeLogTarget(const QString &argument)
{
    const QUrl url(argument);
    if (url.scheme().startsWith(QLatin1String("http"), Qt::CaseInsensitive) && !url.host().isEmpty()) {
        return url.host();
    }
    // Archivo local .htm/.html/.xhtml: sin host; no exponer la ruta completa en el log.
    const QString fileName = QFileInfo(argument).fileName();
    return fileName.isEmpty() ? QStringLiteral("(sin host)") : fileName;
}

QString logTarget(const QString &argument)
{
    if (DebugFlags::isOn(QStringLiteral("linkRedirectorLogFullUrl"))) {
        return argument;
    }
    return safeLogTarget(argument);
}

} // namespace LinkRedirectorRouting
