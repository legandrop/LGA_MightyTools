#include "updates/UpdateNotes.h"

#include "core/I18n.h"

#include "lgaupdate/LgaUpdateI18n.h"
#include "lgaupdate/LgaWhatsNew.h"

#include <QApplication>
#include <QDebug>
#include <QTimer>

namespace {

// El mismo repo de releases que el del paquete (UpdateService.cpp, kRepoSlug).
const QString kRepoSlug = QStringLiteral("legandrop/LGA_MightyTools");
// Por intento: con los reintentos de LGA (3 por servidor, 5 s entre uno y otro) el cartel puede
// tardar en salir si la red anda mal, pero con red normal son milisegundos.
constexpr int kNotesTimeoutMs = 8000;

} // namespace

UpdateNotes::UpdateNotes(QNetworkAccessManager *network, QObject *parent)
    : QObject(parent)
    , m_network(network)
{
}

UpdateNotes::~UpdateNotes()
{
    cancel();
}

QString UpdateNotes::html(const QByteArray &whatsNewJson, const QString &installedVersion,
                          const QString &offeredVersion)
{
    const LgaWhatsNew::Notes notes = LgaWhatsNew::parse(whatsNewJson);
    if (!notes.valid) {
        qDebug().noquote() << "[UpdateNotes] whats_new.json invalido:" << notes.error;
        return {};
    }
    // El idioma de la biblioteca sigue al de la interfaz: elige `text_en` o el castellano y traduce
    // los rotulos NEW / IMPROVED / FIXED. Se fija cada vez porque el usuario lo cambia en General.
    LgaUpdateI18n::setLanguage(I18n::code(I18n::language()));
    const QList<LgaWhatsNew::VersionNotes> range = LgaWhatsNew::range(
        notes, installedVersion, offeredVersion, LgaWhatsNew::currentPlatform());
    return range.isEmpty() ? QString() : LgaWhatsNew::toHtml(range);
}

void UpdateNotes::fetch(const QByteArray &manifestJson, const QString &installedVersion,
                        const QString &offeredVersion, std::function<void(const QString &html)> done)
{
    cancel();
    const LgaWhatsNew::AssetRef asset = LgaWhatsNew::assetFromManifest(manifestJson, kRepoSlug);
    if (!m_network || !asset.isValid()) {
        // Un release sin notas publicadas (o un manifiesto viejo) no es un error: el cartel sale igual.
        qDebug() << "[UpdateNotes] El manifiesto no trae whats_new.json para" << kRepoSlug;
        QTimer::singleShot(0, this, [done]() { done(QString()); });
        return;
    }

    m_fetcher = new LgaWhatsNewFetcher(m_network, this);
    connect(m_fetcher, &LgaWhatsNewFetcher::finished, this,
            [this, done, installedVersion, offeredVersion](const LgaWhatsNewFetchResult &result) {
                m_fetcher->deleteLater();
                m_fetcher = nullptr;
                if (result.status != LgaWhatsNewFetchResult::Status::Ok) {
                    qDebug().noquote() << "[UpdateNotes] Sin notas para el cartel:" << result.detail;
                    done(QString());
                    return;
                }
                const LgaWhatsNew::Notes &notes = result.notes;
                LgaUpdateI18n::setLanguage(I18n::code(I18n::language()));
                const QList<LgaWhatsNew::VersionNotes> range = LgaWhatsNew::range(
                    notes, installedVersion, offeredVersion, LgaWhatsNew::currentPlatform());
                done(range.isEmpty() ? QString() : LgaWhatsNew::toHtml(range));
            });

    LgaWhatsNewFetcher::Request request;
    request.repoSlug = kRepoSlug;
    request.asset = asset;
    request.timeoutMs = kNotesTimeoutMs;
    request.userAgent = QStringLiteral("LGA_MightyTools/%1").arg(QApplication::applicationVersion());
    request.log = [](const QString &message) { qDebug().noquote() << "[UpdateNotes]" << message; };
    m_fetcher->fetch(request);
}

void UpdateNotes::cancel()
{
    if (!m_fetcher) {
        return;
    }
    m_fetcher->disconnect(this);
    m_fetcher->cancel();
    m_fetcher->deleteLater();
    m_fetcher = nullptr;
}
