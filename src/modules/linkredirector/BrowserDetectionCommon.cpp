#include "modules/linkredirector/BrowserDetection.h"
#include "modules/linkredirector/BrowserRegistration.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace LinkRedirectorBrowsers {

QList<DetectedBrowser> installedBrowsers()
{
    const QList<DetectedBrowser> raw = installedBrowsersRaw();
    const QString ownExe = QDir::fromNativeSeparators(QCoreApplication::applicationFilePath());

    QList<DetectedBrowser> result;
    for (const DetectedBrowser &browser : raw) {
        // Excluir siempre esta misma app: elegirla como destino de un link seria un bucle de ruteo.
        if (LinkRedirectorBrowserRegistration::isOwnHandlerId(browser.handlerId)) {
            continue;
        }
        if (!ownExe.isEmpty() && browser.exePath.compare(ownExe, Qt::CaseInsensitive) == 0) {
            continue;
        }
        result << browser;
    }
    return result;
}

QString nameForHandlerId(const QString &handlerId)
{
    if (handlerId.isEmpty()) {
        return QString();
    }
    for (const DetectedBrowser &browser : installedBrowsersRaw()) {
        if (browser.handlerId.compare(handlerId, Qt::CaseInsensitive) == 0) {
            return browser.name;
        }
    }
    return QString();
}

QString friendlyNameForExe(const QString &exePath)
{
    if (exePath.isEmpty()) {
        return QString();
    }
    for (const DetectedBrowser &browser : installedBrowsers()) {
        if (browser.exePath.compare(exePath, Qt::CaseInsensitive) == 0) {
            return browser.name;
        }
    }
    return QFileInfo(exePath).completeBaseName();
}

} // namespace LinkRedirectorBrowsers
