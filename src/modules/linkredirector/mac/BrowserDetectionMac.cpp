#include "modules/linkredirector/BrowserDetection.h"

#include <CoreServices/CoreServices.h>

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <limits.h>

// Port de LGA_LinkRedirector src/macos/BrowserRegistry.cpp (v0.173), sin cambios de logica salvo el
// bundle id propio a excluir (com.lga.mightytools en vez de com.lga.linkredirector).
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

QString cfUrlToPath(CFURLRef value)
{
    if (!value) {
        return QString();
    }
    char path[PATH_MAX] = {0};
    if (!CFURLGetFileSystemRepresentation(value, true, reinterpret_cast<UInt8 *>(path), sizeof(path))) {
        return QString();
    }
    return QString::fromUtf8(path);
}

QString bundleInfoValue(const QString &bundlePath, const QString &key)
{
    const QString plistPath = QDir(bundlePath).filePath(QStringLiteral("Contents/Info.plist"));
    if (!QFileInfo::exists(plistPath)) {
        return QString();
    }
    QSettings info(plistPath, QSettings::NativeFormat);
    return info.value(key).toString();
}

QString bundleExecutablePath(const QString &bundlePath)
{
    if (bundlePath.isEmpty()) {
        return QString();
    }

    QString executableName = bundleInfoValue(bundlePath, QStringLiteral("CFBundleExecutable"));
    if (executableName.isEmpty()) {
        executableName = QFileInfo(bundlePath).completeBaseName();
    }
    if (executableName.isEmpty()) {
        return QString();
    }

    const QString executablePath = QDir(bundlePath).filePath(QStringLiteral("Contents/MacOS/") + executableName);
    if (!QFileInfo::exists(executablePath)) {
        return QString();
    }
    return QDir::fromNativeSeparators(executablePath);
}

QString bundleDisplayName(const QString &bundlePath)
{
    QString name = bundleInfoValue(bundlePath, QStringLiteral("CFBundleDisplayName"));
    if (name.isEmpty()) {
        name = bundleInfoValue(bundlePath, QStringLiteral("CFBundleName"));
    }
    if (name.isEmpty()) {
        name = QFileInfo(bundlePath).completeBaseName();
    }
    return name;
}

// True solo si el bundle id corresponde a un navegador conocido. En macOS,
// LSCopyAllHandlersForURLScheme("http") lista cualquier app que declare manejar http (Cyberduck,
// mpv, etc.), no solo browsers; esto filtra a los conocidos. Tambien excluye la propia app (elegirla
// como destino seria un bucle de ruteo): filtro de refuerzo, ademas del que hace
// BrowserDetectionCommon.cpp contra BrowserRegistration::isOwnHandlerId().
bool isKnownBrowserBundleId(const QString &bundleId)
{
    const QString id = bundleId.toLower();

    if (id == QLatin1String("com.lga.mightytools")) {
        return false;
    }

    // IDs exactos de navegadores cuyo bundle id no contiene un token evidente.
    static const QSet<QString> exact = {
        QStringLiteral("com.apple.safari"),
        QStringLiteral("com.apple.safaritechnologypreview"),
        QStringLiteral("company.thebrowser.browser"),   // Arc
        QStringLiteral("company.thebrowser.dia"),        // Dia
        QStringLiteral("com.pushplaylabs.sidekick"),
        QStringLiteral("com.naver.whale"),
        QStringLiteral("com.kagi.kagimacos"),            // Orion
        QStringLiteral("org.qutebrowser.qutebrowser"),
        QStringLiteral("com.duckduckgo.macos.browser"),
        QStringLiteral("org.mozilla.nightly"),
    };
    if (exact.contains(id)) {
        return true;
    }

    // Familias por substring (cubre variantes beta/dev/canary/nightly).
    static const QStringList families = {
        QStringLiteral("chrome"),
        QStringLiteral("chromium"),
        QStringLiteral("firefox"),
        QStringLiteral("brave"),
        QStringLiteral("edgemac"),
        QStringLiteral("opera"),
        QStringLiteral("vivaldi"),
        QStringLiteral("floorp"),
        QStringLiteral("waterfox"),
        QStringLiteral("librewolf"),
        QStringLiteral("torbrowser"),
        QStringLiteral("zen-browser"),
        QStringLiteral("thebrowser"),
    };
    for (const QString &token : families) {
        if (id.contains(token)) {
            return true;
        }
    }
    return false;
}

void appendBrowserFromBundleId(const QString &bundleId,
                               QList<DetectedBrowser> &result,
                               QSet<QString> &seenExe,
                               QSet<QString> &seenBundleIds)
{
    if (bundleId.isEmpty()) {
        return;
    }
    if (!isKnownBrowserBundleId(bundleId)) {
        return; // Descartar apps que manejan http pero no son navegadores.
    }
    const QString bundleKey = bundleId.toLower();
    if (seenBundleIds.contains(bundleKey)) {
        return;
    }

    const QByteArray bundleUtf8 = bundleId.toUtf8();
    CFStringRef cfBundleId = CFStringCreateWithCString(kCFAllocatorDefault, bundleUtf8.constData(), kCFStringEncodingUTF8);
    if (!cfBundleId) {
        return;
    }

    CFArrayRef appUrls = LSCopyApplicationURLsForBundleIdentifier(cfBundleId, nullptr);
    CFRelease(cfBundleId);
    if (!appUrls) {
        return;
    }

    const CFIndex count = CFArrayGetCount(appUrls);
    for (CFIndex i = 0; i < count; ++i) {
        const CFTypeRef item = CFArrayGetValueAtIndex(appUrls, i);
        if (!item || CFGetTypeID(item) != CFURLGetTypeID()) {
            continue;
        }

        const QString bundlePath = cfUrlToPath(reinterpret_cast<CFURLRef>(item));
        const QString executablePath = bundleExecutablePath(bundlePath);
        if (executablePath.isEmpty()) {
            continue;
        }

        const QString dedupeKey = executablePath.toLower();
        if (seenExe.contains(dedupeKey)) {
            continue;
        }

        DetectedBrowser info;
        info.name = bundleDisplayName(bundlePath);
        info.exePath = executablePath;
        info.handlerId = bundleId;
        result << info;
        seenExe.insert(dedupeKey);
        seenBundleIds.insert(bundleKey);
        break;
    }

    CFRelease(appUrls);
}

QStringList fallbackBrowserBundleIds()
{
    return {
        QStringLiteral("com.apple.Safari"),
        QStringLiteral("com.google.Chrome"),
        QStringLiteral("com.google.Chrome.canary"),
        QStringLiteral("com.brave.Browser"),
        QStringLiteral("org.mozilla.firefox"),
        QStringLiteral("org.mozilla.firefoxdeveloperedition"),
        QStringLiteral("com.operasoftware.Opera"),
        QStringLiteral("com.vivaldi.Vivaldi"),
        QStringLiteral("com.microsoft.edgemac"),
        QStringLiteral("company.thebrowser.Browser"),
        QStringLiteral("com.apple.SafariTechnologyPreview"),
    };
}

} // namespace

namespace LinkRedirectorBrowsers {

QList<DetectedBrowser> installedBrowsersRaw()
{
    QList<DetectedBrowser> result;
    QSet<QString> seenExe;
    QSet<QString> seenBundleIds;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    CFArrayRef handlers = LSCopyAllHandlersForURLScheme(CFSTR("http"));
#pragma clang diagnostic pop
    if (handlers) {
        const CFIndex count = CFArrayGetCount(handlers);
        for (CFIndex i = 0; i < count; ++i) {
            const CFTypeRef value = CFArrayGetValueAtIndex(handlers, i);
            if (!value || CFGetTypeID(value) != CFStringGetTypeID()) {
                continue;
            }
            appendBrowserFromBundleId(
                cfStringToQString(reinterpret_cast<CFStringRef>(value)),
                result,
                seenExe,
                seenBundleIds);
        }
        CFRelease(handlers);
    }

    // Fallback para cubrir browsers instalados pero no listados aun en la DB de handlers.
    for (const QString &bundleId : fallbackBrowserBundleIds()) {
        appendBrowserFromBundleId(bundleId, result, seenExe, seenBundleIds);
    }

    std::sort(result.begin(), result.end(), [](const DetectedBrowser &a, const DetectedBrowser &b) {
        return a.name.toLower() < b.name.toLower();
    });

    return result;
}

} // namespace LinkRedirectorBrowsers
