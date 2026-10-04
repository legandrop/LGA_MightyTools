#include "updates/UpdateManifest.h"
#include "updates/VersionCompare.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>

namespace {

const QString kRepoSlug = QStringLiteral("legandrop/LGA_MightyTools");

// Formato del manifiesto que esta app sabe leer.
constexpr int kManifestSchemaVersion = 1;

const QRegularExpression &assetPattern(UpdateManifest::Platform platform)
{
    static const QRegularExpression windows(QStringLiteral("^LGA_MightyTools_Setup_v(.+)\\.exe$"));
    static const QRegularExpression mac(QStringLiteral("^LGA_MightyTools_Mac_v[0-9]+(?:\\.[0-9]+)+\\.zip$"));
    return platform == UpdateManifest::Platform::Mac ? mac : windows;
}

QString normalizedVersion(QString version)
{
    version = version.trimmed();
    if (version.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) {
        version.remove(0, 1);
    }
    return version;
}

} // namespace

namespace UpdateManifest {

Platform currentPlatform()
{
#ifdef Q_OS_MACOS
    return Platform::Mac;
#else
    return Platform::Windows;
#endif
}

QUrl loopbackUrl(const QString &text)
{
    const QUrl url(text.trimmed(), QUrl::StrictMode);
    if (!url.isValid() || url.scheme() != QLatin1String("http")) {
        return {};
    }
    const QString host = url.host();
    if (host != QLatin1String("127.0.0.1") && host != QLatin1String("localhost")) {
        return {};
    }
    return url;
}

QString normalizedSha256Digest(QString digest)
{
    digest = digest.trimmed();
    if (digest.startsWith(QStringLiteral("sha256:"), Qt::CaseInsensitive)) {
        digest.remove(0, QStringLiteral("sha256:").size());
    }
    digest = digest.toLower();

    static const QRegularExpression digestPattern(QStringLiteral("^[0-9a-f]{64}$"));
    if (!digestPattern.match(digest).hasMatch()) {
        return {};
    }
    return digest;
}

ReleaseInfo parse(const QByteArray &payload, Platform platform)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return {};
    }

    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("schemaVersion")).toInt(0) != kManifestSchemaVersion) {
        return {};
    }

    const QJsonObject products = root.value(QStringLiteral("products")).toObject();
    const QJsonObject product = products.value(kRepoSlug).toObject();
    if (product.isEmpty()) {
        // Repo sin release todavia: no es un error, es "no hay update".
        return {};
    }

    const QRegularExpression &pattern = assetPattern(platform);

    // El ultimo paquete de esta plataforma, venga del release que venga. Si matchea mas de una
    // entrada (no deberia: hay una por familia), gana la version mas alta.
    const QJsonValue latestValue = product.value(QStringLiteral("assetLatest"));
    if (latestValue.isArray()) {
        ReleaseInfo best;
        const QJsonArray latest = latestValue.toArray();
        for (const QJsonValue &entryValue : latest) {
            const QJsonObject entry = entryValue.toObject();
            const QString name = entry.value(QStringLiteral("name")).toString();
            if (!pattern.match(name).hasMatch()) {
                continue;
            }
            const QString tag = entry.value(QStringLiteral("tag")).toString().trimmed();
            const QString version = normalizedVersion(tag);
            if (version.isEmpty()) {
                continue;
            }
            if (!best.version.isEmpty() && !VersionCompare::isNewer(version, best.version)) {
                continue;
            }
            best.version = version;
            best.tag = tag;
            best.assetName = name;
            best.assetDigest = normalizedSha256Digest(entry.value(QStringLiteral("digest")).toString());
        }
        // Con `assetLatest` presente y sin paquete de esta plataforma, no hay nada publicado para
        // ella: se devuelve vacio aunque el ultimo release exista para la otra.
        return best;
    }

    const QString tag = product.value(QStringLiteral("tag")).toString().trimmed();
    ReleaseInfo info;
    info.version = normalizedVersion(tag);
    if (info.version.isEmpty()) {
        return {};
    }
    info.tag = tag;

    const QJsonArray assets = product.value(QStringLiteral("assets")).toArray();
    for (const QJsonValue &assetValue : assets) {
        const QJsonObject asset = assetValue.toObject();
        const QString name = asset.value(QStringLiteral("name")).toString();
        if (!pattern.match(name).hasMatch()) {
            continue;
        }
        info.assetName = name;
        info.assetDigest = normalizedSha256Digest(asset.value(QStringLiteral("digest")).toString());
        break;
    }

    return info;
}

} // namespace UpdateManifest
