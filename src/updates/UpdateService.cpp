#include "updates/UpdateService.h"
#include "updates/UpdateManifest.h"
#include "updates/UpdateNotes.h"
#include "updates/VersionCompare.h"
#include "updates/UpdateDialog.h"

#include "app/SettingsStore.h"
#include "core/AutomatedRun.h"
#include "core/DebugFlags.h"
#include "core/I18n.h"
#include "core/LgaNetRetry.h"
#include "platform/UpdateInstaller.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressDialog>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScopedPointer>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

namespace {

// Manifiesto estatico de LGA, servido por GitHub Pages desde legandrop/LGA_Updates.
// No le pega a la API de GitHub (rate limit 60/h/IP): un workflow de ese repo la
// consulta una vez y publica este archivo, sin limite de lecturas.
const QString kOfficialManifestUrl = QStringLiteral("https://legandrop.github.io/LGA_Updates/versions.json");
const QString kRepoSlug = QStringLiteral("legandrop/LGA_MightyTools");
// Donde se baja a mano cuando esta copia no se puede actualizar sola.
const QString kReleasesPageUrl = QStringLiteral("https://github.com/legandrop/LGA_MightyTools/releases");
const QString kDisplayName = QStringLiteral("LGA Mighty Tools");

// Demora del chequeo automatico al arrancar: le da tiempo a la app a terminar de
// levantar el tray antes de meter trafico de red.
constexpr int kAutomaticCheckDelayMs = 15000;
constexpr int kCheckTimeoutMs = 15000;
constexpr int kDownloadTimeoutMs = 300000;

// Chequeo periodico mientras la app sigue abierta (vive en la bandeja dias enteros): cada 3 horas
// con un desfase al azar de +/-15 min, para que las maquinas de un estudio no consulten en el
// mismo segundo. El tick de 5 min solo mira el reloj; si un periodico falla por red, se reintenta
// a los 10 min (al despertar de una suspension la red suele tardar en levantar).
constexpr qint64 kPeriodicCheckIntervalMs = 3LL * 60 * 60 * 1000;
constexpr int kPeriodicCheckJitterMs = 15 * 60 * 1000;
constexpr qint64 kPeriodicCheckRetryMs = 10LL * 60 * 1000;
constexpr int kPeriodicTickMs = 5 * 60 * 1000;

// "Later" calla el chequeo automatico este tiempo, igual que "Remind me later" en las demas apps.
constexpr int kSnoozeDays = 1;
const QString kSnoozeKey = QStringLiteral("updates/snoozeUntil");

// Log del intento que sale: el primero todavia no tiene un servidor en el que haya fallado.
QString sendingLabel(const LgaNetRetry::Budget &budget)
{
    return budget.attempt() == 1 ? LgaNetRetry::attemptLabel(1) : budget.label();
}

// URL de un flag de debug, solo si apunta a ESTA maquina (UpdateManifest::loopbackUrl).
QUrl loopbackDebugUrl(const QString &flag)
{
    return UpdateManifest::loopbackUrl(DebugFlags::value(flag));
}

// El manifiesto oficial, salvo en una prueba del updater contra un servidor local
// (updateManifestUrl de config/debug_flags.txt).
QUrl configuredManifestUrl()
{
    const QUrl debugUrl = loopbackDebugUrl(QStringLiteral("updateManifestUrl"));
    return debugUrl.isValid() ? debugUrl : QUrl(kOfficialManifestUrl);
}

bool usingDebugManifest()
{
    return loopbackDebugUrl(QStringLiteral("updateManifestUrl")).isValid();
}

// URL de descarga del asset, DERIVADA del slug/tag/nombre en vez de venir en el
// manifiesto: es publica y estable, y guardarla seria un segundo lugar donde
// quedar vieja.
QUrl configuredAssetDownloadUrl(const QString &tag, const QString &name)
{
    if (usingDebugManifest()) {
        const QUrl base = loopbackDebugUrl(QStringLiteral("updateDownloadBase"));
        if (base.isValid()) {
            return base.resolved(QUrl(name));
        }
    }
    return QUrl(QStringLiteral("https://github.com/%1/releases/download/%2/%3")
                    .arg(kRepoSlug, tag, name));
}

// En una corrida automatizada el updater solo habla con ESTA maquina: nunca con la red de verdad.
bool refusedInAutomatedRun(const QUrl &url)
{
    if (!AutomatedRun::active() || UpdateManifest::loopbackUrl(url.toString()).isValid()) {
        return false;
    }
    qDebug() << "[UpdateService] Corrida automatizada: no se sale a la red (" << url.host() << ")";
    return true;
}

// Primer byte de "Content-Range: bytes <inicio>-<fin>/<total>"; -1 si no viene o no se entiende.
qint64 contentRangeStart(const QNetworkReply *reply)
{
    static const QRegularExpression pattern(QStringLiteral("^\\s*bytes\\s+(\\d+)-"));
    const QRegularExpressionMatch match = pattern.match(QString::fromLatin1(reply->rawHeader("Content-Range")));
    return match.hasMatch() ? match.captured(1).toLongLong() : -1;
}

} // namespace

UpdateService::UpdateService(QWidget *parentWindow, QObject *parent)
    : QObject(parent)
    , m_parentWindow(parentWindow)
    , m_network(new QNetworkAccessManager(this))
    , m_notes(new UpdateNotes(m_network, this))
{
}

UpdateService::~UpdateService()
{
    // Teardown propio: no depender del orden de destruccion de TrayController.
    // Si un reply de red o el dialogo de progreso siguen vivos, cortarlos aca evita
    // que un callback dispare sobre un UpdateService a medio destruir.
    if (m_checkReply) {
        m_checkReply->disconnect(this);
        m_checkReply->abort();
        m_checkReply->deleteLater();
        m_checkReply = nullptr;
    }
    if (m_downloadReply) {
        m_downloadReply->disconnect(this);
        m_downloadReply->abort();
        m_downloadReply->deleteLater();
        m_downloadReply = nullptr;
    }
    if (m_progressDialog) {
        m_progressDialog->disconnect(this);
        m_progressDialog->close();
        m_progressDialog->deleteLater();
        m_progressDialog = nullptr;
    }

    // Si la app se cierra con una descarga en curso, el parcial NO se commitea:
    // un instalador a medio bajar en la carpeta de temp es peor que no tener ninguno.
    discardPartialDownload();
}

QWidget *UpdateService::parentWindow() const
{
    return m_parentWindow.data();
}

bool UpdateService::setEndpointsForTests(const QUrl &manifestUrl, const QUrl &downloadBase, const QString &downloadDir)
{
    if (!AutomatedRun::active()) {
        return false;
    }
    m_testManifestUrl = UpdateManifest::loopbackUrl(manifestUrl.toString());
    m_testDownloadBase = UpdateManifest::loopbackUrl(downloadBase.toString());
    m_testDownloadDir = downloadDir;
    return m_testManifestUrl.isValid() && m_testDownloadBase.isValid() && !m_testDownloadDir.isEmpty();
}

QUrl UpdateService::manifestUrl() const
{
    return m_testManifestUrl.isValid() ? m_testManifestUrl : configuredManifestUrl();
}

QUrl UpdateService::assetDownloadUrl(const QString &tag, const QString &name) const
{
    return m_testDownloadBase.isValid() ? m_testDownloadBase.resolved(QUrl(name)) : configuredAssetDownloadUrl(tag, name);
}

QWidget *UpdateService::dialogParent() const
{
#ifdef Q_OS_MACOS
    QWidget *window = parentWindow();
    return window && window->isVisible() ? window : nullptr;
#else
    return parentWindow();
#endif
}

bool UpdateService::refuseWhileBusyElsewhere()
{
    if (!m_busyElsewhere || !m_busyElsewhere()) {
        return false;
    }
    qDebug() << "[UpdateService] La app esta borrando archivos: no se actualiza ahora";
    QMessageBox::information(parentWindow(), I18n::tr("Updates"),
        I18n::tr("Disk Space is still deleting files. Update when it finishes."));
    return true;
}

void UpdateService::scheduleAutomaticCheck()
{
    if (m_automaticCheckScheduled) {
        return;
    }
    m_automaticCheckScheduled = true;

    QTimer::singleShot(kAutomaticCheckDelayMs, this, [this]() { checkForUpdates(false); });

    schedulePeriodicCheckIn(kPeriodicCheckIntervalMs, /*withJitter=*/true);
    m_periodicTimer = new QTimer(this);
    m_periodicTimer->setTimerType(Qt::VeryCoarseTimer);
    m_periodicTimer->setInterval(kPeriodicTickMs);
    connect(m_periodicTimer, &QTimer::timeout, this, &UpdateService::onPeriodicTick);
    m_periodicTimer->start();
}

void UpdateService::schedulePeriodicCheckIn(qint64 delayMs, bool withJitter)
{
    qint64 jitterMs = 0;
    if (withJitter) {
        jitterMs = QRandomGenerator::global()->bounded(2 * kPeriodicCheckJitterMs + 1) - kPeriodicCheckJitterMs;
    }
    m_nextPeriodicCheckUtc = QDateTime::currentDateTimeUtc().addMSecs(qMax<qint64>(0, delayMs + jitterMs));
}

void UpdateService::onPeriodicTick()
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    // Un reloj que se atraso (a mano o por zona) dejaria el proximo chequeo a dias de distancia.
    if (now.msecsTo(m_nextPeriodicCheckUtc) > kPeriodicCheckIntervalMs + kPeriodicCheckJitterMs) {
        schedulePeriodicCheckIn(kPeriodicCheckIntervalMs, /*withJitter=*/true);
        return;
    }
    if (now < m_nextPeriodicCheckUtc) {
        return;
    }
    // Con un chequeo, el cartel o una descarga en curso no se pisa nada: lo intenta el tick siguiente.
    if (m_busy) {
        return;
    }
    schedulePeriodicCheckIn(kPeriodicCheckIntervalMs, /*withJitter=*/true);
    if (m_automaticChecksEnabled && !m_automaticChecksEnabled()) {
        return;
    }
    m_periodicCheckActive = true;
    checkForUpdates(false);
    if (!m_checkReply) {
        m_periodicCheckActive = false;
    }
}

QDateTime UpdateService::snoozeUntil() const
{
    if (!m_store) {
        return m_memorySnoozeUntil;
    }
    return QDateTime::fromString(m_store->value(kSnoozeKey).toString(), Qt::ISODate);
}

void UpdateService::setSnoozeUntil(const QDateTime &until)
{
    if (!m_store) {
        m_memorySnoozeUntil = until;
        return;
    }
    if (until.isValid()) {
        m_store->setValue(kSnoozeKey, until.toString(Qt::ISODate));
    } else {
        m_store->remove(kSnoozeKey);
    }
}

void UpdateService::checkForUpdates(bool manual)
{
    if (!m_network) {
        return;
    }
    const Mode mode = manual ? Mode::Manual : Mode::Automatic;
    if (m_busy) {
        if (manual) {
            QMessageBox::information(parentWindow(), I18n::tr("Updates"),
                I18n::tr("An update operation is already in progress."));
        }
        return;
    }

    m_busy = true;
    startCheckRequest(mode);
}

void UpdateService::checkInline()
{
    if (!m_network) {
        return;
    }
    if (m_busy) {
        QMessageBox::information(parentWindow(), I18n::tr("Updates"),
            I18n::tr("An update operation is already in progress."));
        return;
    }
    m_busy = true;
    startCheckRequest(Mode::Inline);
}

void UpdateService::installAvailable()
{
    if (m_availableVersion.isEmpty() || !m_availableUrl.isValid()) {
        return;
    }
    if (m_busy) {
        QMessageBox::information(parentWindow(), I18n::tr("Updates"),
            I18n::tr("An update operation is already in progress."));
        return;
    }
    if (blockedHere() || refuseWhileBusyElsewhere()) {
        return;
    }
    m_busy = true;
    downloadAndRunUpdate(m_availableUrl, m_availableAsset, m_availableDigest, m_availableVersion);
}

void UpdateService::startCheckRequest(Mode mode)
{
    // Guard de re-entrancia, simetrico al de m_downloadReply en downloadAndRunUpdate:
    // si ya hay un chequeo en vuelo, no se pisa con uno nuevo.
    if (m_checkReply) {
        return;
    }
    if (refusedInAutomatedRun(manifestUrl())) {
        m_periodicCheckActive = false;
        m_busy = false;
        emit checkFailed();
        return;
    }

    m_checkMode = mode;
    m_checkBudget.reset();
    ++m_checkGeneration;
    emit checking();
    sendCheckRequest();
}

void UpdateService::sendCheckRequest()
{
    QNetworkRequest request{manifestUrl()};
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("LGA_MightyTools/%1").arg(QApplication::applicationVersion()));
    request.setRawHeader("Accept", "application/json");
    // Pages ya sirve el manifiesto por CDN con su propio cache; el de Qt encima solo
    // agregaria una segunda capa que hace mas dificil saber que version se esta leyendo.
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    request.setTransferTimeout(kCheckTimeoutMs);

    m_checkReply = m_network->get(request);
    connect(m_checkReply, &QNetworkReply::finished, this, &UpdateService::onCheckFinished);

    qDebug().noquote() << QStringLiteral("[UpdateService] Chequeando updates, modo=%1, %2 (%3)")
                              .arg(int(m_checkMode))
                              .arg(sendingLabel(m_checkBudget), manifestUrl().host());
}

void UpdateService::onCheckFinished()
{
    // Manual (menu) e Inline ("Check now" de General) muestran los errores en cartel; el automatico
    // se calla en todo lo que no sea "hay version nueva".
    const Mode mode = m_checkMode;
    const bool manual = mode != Mode::Automatic;
    QNetworkReply *reply = m_checkReply;
    m_checkReply = nullptr;

    if (!reply) {
        // Defensivo: no deberia poder pasar (ver comentario del guard en
        // startCheckRequest), pero si pasa no hay nada mas que vaya a liberar
        // m_busy, asi que se libera aca para no dejar el servicio trabado.
        m_periodicCheckActive = false;
        m_busy = false;
        emit checkFailed();
        return;
    }

    const QNetworkReply::NetworkError error = reply->error();
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray payload = reply->readAll();
    const QString failedHost = reply->url().host();
    // Nadie aborta el chequeo a proposito (solo el destructor, ya desconectado): un OperationCanceled es
    // el timeout de setTransferTimeout y se reintenta. allowRetry() va ultimo porque ya cuenta el intento,
    // en el servidor donde fallo (reply->url() es la URL final, despues de los redirects).
    const bool failed = error != QNetworkReply::NoError || httpStatus != 200;
    const bool retry = failed && LgaNetRetry::shouldRetry(reply, /*cancelledOnPurpose=*/false)
                       && m_checkBudget.allowRetry(reply);
    reply->deleteLater();

    if (retry) {
        qDebug().noquote() << QStringLiteral("[UpdateService] Chequeo fallo en %1, networkError=%2 httpStatus=%3; "
                                             "%4 en %5 ms")
                                  .arg(failedHost)
                                  .arg(int(error))
                                  .arg(httpStatus)
                                  .arg(m_checkBudget.label())
                                  .arg(LgaNetRetry::delayMs());
        const int generation = ++m_checkGeneration;
        emit retrying(m_checkBudget.attemptOnHost(), LgaNetRetry::kRetries + 1);
        // m_busy sigue en true durante la espera: nada pisa este chequeo. Si el servicio se destruye,
        // el timer muere con el (objeto de contexto).
        QTimer::singleShot(LgaNetRetry::delayMs(), this, [this, generation]() {
            if (generation != m_checkGeneration || m_checkReply) {
                return;
            }
            sendCheckRequest();
        });
        return;
    }

    // Recien con el resultado final: el reintento largo del periodico va despues de los cuatro intentos.
    const bool periodic = m_periodicCheckActive;
    m_periodicCheckActive = false;

    if (error != QNetworkReply::NoError) {
        qDebug() << "[UpdateService] Chequeo fallo, networkError=" << error
                 << "httpStatus=" << httpStatus << "intentos=" << m_checkBudget.attempt()
                 << "servidor=" << failedHost;
        if (periodic) {
            schedulePeriodicCheckIn(kPeriodicCheckRetryMs, /*withJitter=*/false);
        }
        m_busy = false;
        emit checkFailed();
        if (manual) {
            QMessageBox::warning(parentWindow(), I18n::tr("Update Check Failed"),
                I18n::tr("Could not check for updates. Please try again later.\n\n"
                   "httpStatus=%1\nurl=%2\nnetworkError=%3\nbody=%4")
                    .arg(QString::number(httpStatus), manifestUrl().toString(),
                         QString::number(static_cast<int>(error)),
                         QString::fromUtf8(payload.left(2048))));
        }
        return;
    }

    // Chequeo explicito del status HTTP, simetrico al que ya existe en el path de
    // descarga: sin esto, una pagina de error servida con NetworkError::NoError
    // (404, 500, etc.) se intentaria parsear como manifiesto y terminaria
    // reportada como "no hay update", ocultando el problema real.
    if (httpStatus != 200) {
        qDebug() << "[UpdateService] Chequeo con status HTTP invalido, httpStatus=" << httpStatus;
        if (periodic) {
            schedulePeriodicCheckIn(kPeriodicCheckRetryMs, /*withJitter=*/false);
        }
        m_busy = false;
        emit checkFailed();
        if (manual) {
            QMessageBox::warning(parentWindow(), I18n::tr("Update Check Failed"),
                I18n::tr("Could not check for updates. Please try again later.\n\n"
                   "httpStatus=%1\nurl=%2\nbody=%3")
                    .arg(QString::number(httpStatus), manifestUrl().toString(),
                         QString::fromUtf8(payload.left(2048))));
        }
        return;
    }

    const UpdateManifest::ReleaseInfo info = UpdateManifest::parse(payload, UpdateManifest::currentPlatform());
    const QUrl downloadUrl = info.assetName.isEmpty() ? QUrl() : assetDownloadUrl(info.tag, info.assetName);
    if (info.version.isEmpty()) {
        qDebug() << "[UpdateService] Sin release instalable en el manifiesto.";
        m_busy = false;
        emit checkFailed();
        if (manual) {
            QMessageBox::information(parentWindow(), I18n::tr("Updates"),
                I18n::tr("No installable update was found."));
        }
        return;
    }

    if (!downloadUrl.isValid()) {
        qDebug() << "[UpdateService] Release" << info.version << "sin asset que matchee el patron.";
        m_busy = false;
        emit checkFailed();
        if (manual) {
            QMessageBox::information(parentWindow(), I18n::tr("Updates"),
                I18n::tr("Release %1 does not contain an installable asset.")
                    .arg(info.version));
        }
        return;
    }

    if (!VersionCompare::isNewer(info.version, QApplication::applicationVersion())) {
        qDebug() << "[UpdateService] Ya esta al dia (remoto=" << info.version
                 << ", local=" << QApplication::applicationVersion() << ")";
        m_busy = false;
        // La fila de General lo dice sin cartel (D-13); el menu de la bandeja sigue con su cartel.
        emit upToDate(QApplication::applicationVersion());
        if (mode == Mode::Manual) {
            QMessageBox::information(parentWindow(), I18n::tr("Updates"),
                I18n::tr("You are running the latest version."));
        }
        return;
    }

    // Fail-closed: el manifiesto de LGA_Updates siempre publica digest, asi que no
    // hay caso legitimo sin el. Si el asset elegido no trae uno valido, se trata
    // como error duro ANTES de descargar nada; nunca se baja ni se ejecuta un
    // instalador sin forma de verificarlo.
    if (info.assetDigest.isEmpty()) {
        qDebug() << "[UpdateService] Release" << info.version
                 << "sin digest SHA-256 valido en el manifiesto; no se descarga.";
        m_busy = false;
        emit checkFailed();
        if (manual) {
            QMessageBox::warning(parentWindow(), I18n::tr("Update Check Failed"),
                I18n::tr("Release %1 does not include a valid integrity digest. "
                   "Refusing to download an unverifiable installer.")
                    .arg(info.version));
        }
        return;
    }

    qDebug() << "[UpdateService] Hay update disponible:" << info.version;
    m_availableVersion = info.version;
    m_availableUrl = downloadUrl;
    m_availableAsset = info.assetName;
    m_availableDigest = info.assetDigest;
    emit updateAvailable(info.version);
    if (mode == Mode::Inline) {
        // La fila muestra "vX is available" con "Update": no hace falta el cartel.
        m_busy = false;
        return;
    }
    // Con un borrado de Disk Space en curso no se interrumpe con el cartel: se vuelve a mirar en
    // un rato (la fila de General ya muestra la version disponible).
    if (mode == Mode::Automatic && m_busyElsewhere && m_busyElsewhere()) {
        qDebug() << "[UpdateService] La app esta borrando archivos: el cartel se deja para despues";
        if (m_periodicTimer) {
            schedulePeriodicCheckIn(kPeriodicCheckRetryMs, /*withJitter=*/false);
        }
        m_busy = false;
        return;
    }
    // "Later" calla solo el automatico: la fila de General ya muestra la version disponible.
    if (mode == Mode::Automatic) {
        const QDateTime until = snoozeUntil();
        if (until.isValid() && QDateTime::currentDateTime() < until) {
            qDebug() << "[UpdateService] Pospuesto hasta" << until.toString(Qt::ISODate) << "; no se ofrece";
            m_busy = false;
            return;
        }
    }
    // m_busy queda en true a proposito: sigue representando la operacion en curso
    // mientras se bajan las novedades, durante el dialogo y, si el usuario acepta, durante el
    // arranque de la descarga. Se libera en promptForUpdate (si elige "Later") o en
    // onDownloadFinished / los retornos tempranos de downloadAndRunUpdate.
    if (!notesAllowed()) {
        promptForUpdate(info.version, downloadUrl, info.assetName, info.assetDigest, QString());
        return;
    }
    // Las novedades primero (un fallo sale sin ellas, nunca frena el cartel). Si el servicio se
    // destruye antes, m_notes se va con el y no llama.
    m_notes->fetch(payload, QApplication::applicationVersion(), info.version,
                   [this, info, downloadUrl](const QString &notesHtml) {
                       promptForUpdate(info.version, downloadUrl, info.assetName, info.assetDigest, notesHtml);
                   });
}

bool UpdateService::notesAllowed() const
{
    return !usingDebugManifest() && !m_testManifestUrl.isValid()
           && !refusedInAutomatedRun(QUrl(QStringLiteral("https://github.com/%1/releases").arg(kRepoSlug)));
}

void UpdateService::promptForUpdate(const QString &version, const QUrl &downloadUrl,
                                    const QString &assetName, const QString &sha256Digest,
                                    const QString &notesHtml)
{
    // Prueba del updater contra un servidor local: instala sin preguntar (ver DebugFlags.h).
    const bool withoutAsking = usingDebugManifest() && DebugFlags::isOn(QStringLiteral("updateInstallWithoutAsking"));

    // El armado vive en UpdateDialog.cpp para que --ui-shot lo pueda dibujar sin este servicio.
    QScopedPointer<QDialog> dialog(
        createUpdateAvailableDialog(parentWindow(), kDisplayName, version, QApplication::applicationVersion(),
                                    notesHtml));

    // Later (o cerrar el cartel) pospone el chequeo automatico 1 dia; sin eso el chequeo
    // periodico lo volvia a mostrar cada 3 horas. El manual y "Check now" lo ignoran.
    if (withoutAsking || dialog->exec() == QDialog::Accepted) {
        if (blockedHere() || refuseWhileBusyElsewhere()) {
            m_busy = false;
            return;
        }
        setSnoozeUntil(QDateTime());
        downloadAndRunUpdate(downloadUrl, assetName, sha256Digest, version);
    } else {
        setSnoozeUntil(QDateTime::currentDateTime().addDays(kSnoozeDays));
        // Salida real del flujo chequeo->prompt: recien aca se libera m_busy.
        m_busy = false;
    }
}

bool UpdateService::blockedHere()
{
    const UpdateInstaller::Blocker blocker = UpdateInstaller::blocker();
    if (blocker == UpdateInstaller::Blocker::None) {
        return false;
    }
    qDebug() << "[UpdateService] Esta copia no se puede actualizar sola, motivo=" << int(blocker);
    // Vale como "Later": sin esto el chequeo periodico volveria a ofrecer cada 3 horas una
    // actualizacion que aca no se puede instalar.
    setSnoozeUntil(QDateTime::currentDateTime().addDays(kSnoozeDays));

    QString text;
    bool offerDownload = true;
    switch (blocker) {
    case UpdateInstaller::Blocker::DevelopmentCopy:
        text = I18n::tr("This is a development copy, so it does not update itself.");
        offerDownload = false;
        break;
    case UpdateInstaller::Blocker::MoveToApplications:
        text = I18n::tr("%1 is running from the disk image or from a temporary copy, so it cannot update "
                        "itself.\n\nMove it to the Applications folder, open it from there and try again.")
                   .arg(kDisplayName);
        break;
    case UpdateInstaller::Blocker::FolderNotWritable:
        text = I18n::tr("You do not have permission to change the folder where %1 is installed, so it "
                        "cannot update itself.\n\nDownload the new version and install it by hand.")
                   .arg(kDisplayName);
        break;
    case UpdateInstaller::Blocker::None:
        break;
    }

    QMessageBox box(QMessageBox::Information, I18n::tr("Updates"), text, QMessageBox::NoButton, parentWindow());
    QPushButton *download = offerDownload ? box.addButton(I18n::tr("Open download page"), QMessageBox::AcceptRole)
                                          : nullptr;
    box.addButton(I18n::tr("Close"), QMessageBox::RejectRole);
    box.exec();
    if (download && box.clickedButton() == download && !AutomatedRun::active()) {
        QDesktopServices::openUrl(QUrl(kReleasesPageUrl));
    }
    return true;
}

void UpdateService::downloadAndRunUpdate(const QUrl &downloadUrl, const QString &assetName,
                                         const QString &sha256Digest, const QString &version)
{
    if (!m_network || m_downloadReply || m_downloadRetryPending) {
        return;
    }
    if (refusedInAutomatedRun(downloadUrl)) {
        m_busy = false;
        return;
    }

    // Carpeta temporal, no el cache: el instalador descargado es de un solo uso y no
    // hay motivo para que sobreviva a un reinicio del sistema.
    const QString updateDir = !m_testDownloadDir.isEmpty()
        ? m_testDownloadDir
        : QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
              .filePath(QStringLiteral("LGA_MightyTools_updates"));
    if (!QDir().mkpath(updateDir)) {
        QMessageBox::warning(parentWindow(), I18n::tr("Update Failed"),
            I18n::tr("The update cache directory could not be created."));
        // Salida real del flujo: no hay descarga que vaya a liberar m_busy despues.
        m_busy = false;
        return;
    }

    discardPartialDownload();
    // DESPUES de discardPartialDownload(), que limpia el digest pendiente: asignado antes, la descarga
    // llegaba al final sin digest contra el cual verificar y nunca se instalaba.
    m_pendingSha256Digest = UpdateManifest::normalizedSha256Digest(sha256Digest);
    m_downloadVersion = version;
    m_downloadTargetPath = QDir(updateDir).filePath(assetName);
    m_downloadUserCancelled = false;
    m_downloadWriteFailed = false;
    m_downloadResponseChecked = false;

    m_downloadFile = new QSaveFile(m_downloadTargetPath);
    if (!m_downloadFile->open(QIODevice::WriteOnly)) {
        const QString detail = m_downloadFile->errorString();
        discardPartialDownload();
        QMessageBox::warning(parentWindow(), I18n::tr("Update Failed"),
            I18n::tr("The update installer could not be saved.\n%1").arg(detail));
        // Idem: salida real, sin descarga en vuelo que libere m_busy mas adelante.
        m_busy = false;
        return;
    }
    m_downloadHash = new QCryptographicHash(QCryptographicHash::Sha256);
    m_downloadUrl = downloadUrl;
    m_downloadBudget.reset();
    m_downloadReceived = 0;
    m_downloadShowingRetry = false;

    m_progressDialog = createUpdateProgressDialog(dialogParent(), kDisplayName, version);
    m_progressDialog->show();
    connect(m_progressDialog, &QProgressDialog::canceled, this, [this]() {
        m_downloadUserCancelled = true;
        if (m_downloadReply) {
            m_downloadReply->abort(); // finished() en el acto: onDownloadFinished limpia
        } else if (m_downloadRetryPending) {
            finishCancelledDownload();
        }
    });

    m_busy = true;
    sendDownloadRequest();
}

void UpdateService::sendDownloadRequest()
{
    QNetworkRequest request{m_downloadUrl};
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("LGA_MightyTools/%1").arg(QApplication::applicationVersion()));
    // Timeout de INACTIVIDAD, no total: una descarga lenta no se corta mientras
    // sigan llegando bytes.
    request.setTransferTimeout(kDownloadTimeoutMs);
    // Con bytes ya escritos se pide el resto. Si el servidor no lo acepta (contesta 200), se empieza de cero.
    if (m_downloadReceived > 0) {
        request.setRawHeader("Range", "bytes=" + QByteArray::number(m_downloadReceived) + '-');
    }
    m_downloadResumeOffset = m_downloadReceived;
    m_downloadResponseChecked = false;
    m_downloadAccepted = false;
    m_downloadAbortedOnPurpose = false;
    m_downloadRestartFromZero = false;

    m_downloadReply = m_network->get(request);
    connect(m_downloadReply, &QNetworkReply::readyRead, this, &UpdateService::onDownloadReadyRead);
    connect(m_downloadReply, &QNetworkReply::downloadProgress, this,
            [this](qint64 bytesReceived, qint64 bytesTotal) {
                if (!m_progressDialog || !m_downloadAccepted) {
                    return;
                }
                if (bytesTotal > 0) {
                    // Con Range, Qt cuenta solo lo de este pedido: se le suma lo que ya estaba.
                    m_progressDialog->setMaximum(static_cast<int>((m_downloadResumeOffset + bytesTotal) / 1024));
                    m_progressDialog->setValue(static_cast<int>((m_downloadResumeOffset + bytesReceived) / 1024));
                }
            });
    connect(m_downloadReply, &QNetworkReply::finished, this, &UpdateService::onDownloadFinished);

    qDebug().noquote() << QStringLiteral("[UpdateService] Bajando el instalador, %1 (%2, desde el byte %3)")
                              .arg(sendingLabel(m_downloadBudget), m_downloadUrl.host())
                              .arg(m_downloadReceived);
}

void UpdateService::abortDownloadLater(QNetworkReply *reply)
{
    // Diferido: abort() emite finished() sincronicamente, y llamarlo desde aca anidaria
    // onDownloadFinished (con su cartel) dentro de este handler. Solo si sigue siendo el mismo pedido:
    // nunca el de un reintento posterior.
    const QPointer<QNetworkReply> target(reply);
    QTimer::singleShot(0, this, [this, target]() {
        if (target && target == m_downloadReply) {
            target->abort();
        }
    });
}

bool UpdateService::restartPartialDownload()
{
    // QSaveFile no se trunca: se descarta el temporal y se abre otro.
    if (m_downloadFile) {
        m_downloadFile->cancelWriting();
        delete m_downloadFile;
    }
    m_downloadFile = new QSaveFile(m_downloadTargetPath);
    if (m_downloadHash) {
        m_downloadHash->reset();
    }
    m_downloadReceived = 0;
    m_downloadResumeOffset = 0;
    return m_downloadFile->open(QIODevice::WriteOnly);
}

void UpdateService::onDownloadReadyRead()
{
    if (!m_downloadReply || !m_downloadFile || m_downloadAbortedOnPurpose || m_downloadWriteFailed) {
        return;
    }

    // Se valida el status UNA vez por intento, al primer dato: escribir el cuerpo de una
    // respuesta de error al disco lo haria pasar por instalador valido.
    if (!m_downloadResponseChecked) {
        m_downloadResponseChecked = true;
        const int status =
            m_downloadReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool askedRange = m_downloadResumeOffset > 0;
        if (askedRange && status == 206 && contentRangeStart(m_downloadReply) == m_downloadReceived) {
            qDebug() << "[UpdateService] El servidor sigue desde el byte" << m_downloadReceived;
        } else if (status == 200) {
            if (m_downloadReceived > 0) {
                // No acepto el Range: manda todo de nuevo. Se empieza de cero, sin mezclar.
                qDebug() << "[UpdateService] El servidor no siguio desde el byte" << m_downloadReceived
                         << ": se baja de cero";
                if (!restartPartialDownload()) {
                    m_downloadWriteFailed = true;
                    abortDownloadLater(m_downloadReply);
                    return;
                }
            }
        } else if (askedRange && (status == 206 || status == 416)) {
            // Otro rango, o no reconoce el pedido: lo ya bajado no sirve para seguir.
            m_downloadRestartFromZero = true;
            m_downloadAbortedOnPurpose = true;
            abortDownloadLater(m_downloadReply);
            return;
        } else {
            m_downloadAbortedOnPurpose = true;
            abortDownloadLater(m_downloadReply);
            return;
        }
        m_downloadAccepted = true;
    }

    // Se drena el reply en cada senal: sin esto Qt acumula todo el cuerpo en su
    // buffer interno y la memoria crece con el tamano del asset.
    const QByteArray chunk = m_downloadReply->readAll();
    if (chunk.isEmpty()) {
        return;
    }
    // Volvieron a llegar datos: el dialogo deja de decir que esta reintentando.
    if (m_downloadShowingRetry && m_progressDialog) {
        m_downloadShowingRetry = false;
        m_progressDialog->setLabelText(updateProgressText(kDisplayName, m_downloadVersion, 0, 0));
    }

    const qint64 written = m_downloadFile->write(chunk);
    if (written != chunk.size()) {
        // Escritura corta o fallida (disco lleno, permisos, etc.): cortar ya, no
        // seguir bajando bytes que no se van a poder guardar completos. Diferido
        // por el mismo motivo que el chequeo de status.
        m_downloadWriteFailed = true;
        abortDownloadLater(m_downloadReply);
        return;
    }
    m_downloadHash->addData(chunk);
    m_downloadReceived += chunk.size();
}

void UpdateService::closeProgressDialog()
{
    if (m_progressDialog) {
        // QProgressDialog::closeEvent() emite canceled(); desconectar antes de cerrar
        // evita que una descarga que termino bien se marque como cancelada por el
        // usuario.
        m_progressDialog->disconnect(this);
        m_progressDialog->close();
        m_progressDialog->deleteLater();
        m_progressDialog = nullptr;
    }
}

void UpdateService::finishCancelledDownload()
{
    // La espera queda sin efecto: el timer pendiente ve otra generacion y no manda nada.
    m_downloadRetryPending = false;
    ++m_downloadGeneration;
    qDebug() << "[UpdateService] Descarga cancelada por el usuario mientras esperaba otro intento.";
    closeProgressDialog();
    discardPartialDownload();
    m_busy = false;
}

void UpdateService::onDownloadFinished()
{
    QNetworkReply *reply = m_downloadReply;
    m_downloadReply = nullptr;

    if (!reply) {
        m_busy = false;
        return;
    }

    // finished() puede llegar con bytes sin drenar en el buffer (o con el primer dato del intento): pasan
    // por la misma validacion que los de readyRead, que necesita el pedido como el de la descarga. Una
    // respuesta SIN cuerpo (un 416 al pedir el resto) no emite readyRead: se valida igual si trajo status,
    // para que vuelva a cero. Sin status (no conecto, timeout antes de los headers) no se valida: quedaria
    // marcada como abort propio y dejaria de reintentarse.
    const bool unvalidatedStatus = !m_downloadResponseChecked
                                   && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).isValid();
    if (m_downloadFile && !m_downloadUserCancelled && (reply->bytesAvailable() > 0 || unvalidatedStatus)) {
        m_downloadReply = reply;
        onDownloadReadyRead();
        m_downloadReply = nullptr;
    }

    const QNetworkReply::NetworkError error = reply->error();
    const QString errorString = reply->errorString();
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString failedHost = reply->url().host();
    const bool failed = error != QNetworkReply::NoError || m_downloadRestartFromZero;
    // La cancelacion del usuario y la escritura fallida se miran ANTES de shouldRetry(), que pone el status
    // primero (un abort propio sobre un 5xx se reintenta). allowRetry() va ultimo porque ya cuenta el
    // intento, en el servidor donde fallo: reply->url() es la URL final, despues del redirect de GitHub.
    const bool retry = failed && !m_downloadWriteFailed && !m_downloadUserCancelled
                       && (m_downloadRestartFromZero || LgaNetRetry::shouldRetry(reply, m_downloadAbortedOnPurpose))
                       && m_downloadBudget.allowRetry(reply);
    reply->deleteLater();

    if (retry) {
        qDebug().noquote() << QStringLiteral("[UpdateService] La descarga fallo en %1, networkError=%2 httpStatus=%3, "
                                             "%4 bytes guardados; %5 en %6 ms")
                                  .arg(failedHost)
                                  .arg(int(error))
                                  .arg(httpStatus)
                                  .arg(m_downloadReceived)
                                  .arg(m_downloadBudget.label())
                                  .arg(LgaNetRetry::delayMs());
        if (m_downloadRestartFromZero && !restartPartialDownload()) {
            m_downloadWriteFailed = true;
        } else {
            m_downloadRetryPending = true;
            const int generation = ++m_downloadGeneration;
            if (m_progressDialog) {
                m_downloadShowingRetry = true;
                m_progressDialog->setLabelText(updateProgressText(kDisplayName, m_downloadVersion,
                                                                  m_downloadBudget.attemptOnHost(),
                                                                  LgaNetRetry::kRetries + 1));
            }
            // m_busy sigue en true y el dialogo abierto. Cancel durante la espera la corta
            // (finishCancelledDownload); si el servicio se destruye, el timer muere con el.
            QTimer::singleShot(LgaNetRetry::delayMs(), this, [this, generation]() {
                if (generation != m_downloadGeneration || !m_downloadRetryPending) {
                    return;
                }
                m_downloadRetryPending = false;
                sendDownloadRequest();
            });
            return;
        }
    }

    m_busy = false;
    closeProgressDialog();

    // Escritura a disco fallida: se chequea ANTES que la cancelacion del usuario y
    // que el error de red generico, porque el abort() diferido que dispara esto
    // deja error() en OperationCanceledError y se confundiria con cualquiera de
    // los otros dos casos, ocultando la causa real (disco lleno, permisos, etc.).
    if (m_downloadWriteFailed) {
        qDebug() << "[UpdateService] Escritura del instalador a disco fallo (write() corto).";
        discardPartialDownload();
        QMessageBox::warning(parentWindow(), I18n::tr("Update Failed"),
            I18n::tr("The update installer could not be written to disk."));
        return;
    }

    // Cancelar es una decision del usuario, no un fallo: no lleva cartel.
    if (m_downloadUserCancelled) {
        qDebug() << "[UpdateService] Descarga cancelada por el usuario.";
        discardPartialDownload();
        return;
    }

    if (error != QNetworkReply::NoError) {
        const QString detail = QStringLiteral("httpStatus=%1\nnetworkError=%2 (%3)\nattempts=%4\nhost=%5")
                                   .arg(QString::number(httpStatus),
                                        QString::number(static_cast<int>(error)), errorString,
                                        QString::number(m_downloadBudget.attempt()), failedHost);
        qDebug() << "[UpdateService] La descarga fallo:" << detail;
        discardPartialDownload();
        QMessageBox::warning(parentWindow(), I18n::tr("Update Failed"),
            I18n::tr("The update installer could not be downloaded.\n\n%1").arg(detail));
        return;
    }

    // Fail-closed: a esta altura el digest SIEMPRE existe, porque onCheckFinished
    // ya rechazo el update antes de descargar si el manifiesto no traia uno valido.
    // No hay rama "sin digest = sin verificar": si esto dispara es un bug de ese
    // guard, no un caso legitimo.
    Q_ASSERT(!m_pendingSha256Digest.isEmpty());
    if (m_pendingSha256Digest.isEmpty()) {
        qDebug() << "[UpdateService] Descarga sin digest pendiente: no se instala.";
        discardPartialDownload();
        QMessageBox::warning(parentWindow(), I18n::tr("Update Failed"),
            I18n::tr("Internal error: missing integrity digest for the downloaded update."));
        return;
    }

    const QString downloadedDigest = QString::fromLatin1(m_downloadHash->result().toHex());
    if (downloadedDigest.compare(m_pendingSha256Digest, Qt::CaseInsensitive) != 0) {
        const QString detail = QStringLiteral("expected=%1\ngot=%2")
                                   .arg(m_pendingSha256Digest, downloadedDigest);
        qDebug() << "[UpdateService] El SHA-256 de lo descargado no coincide:" << detail;
        discardPartialDownload();
        QMessageBox::warning(parentWindow(), I18n::tr("Update Failed"),
            I18n::tr("The downloaded update failed integrity verification.\n\n%1")
                .arg(detail));
        return;
    }

    const QString installerPath = m_downloadTargetPath;
    if (!m_downloadFile->commit()) {
        const QString detail = m_downloadFile->errorString();
        qDebug() << "[UpdateService] No se pudo guardar lo descargado:" << detail;
        discardPartialDownload();
        QMessageBox::warning(parentWindow(), I18n::tr("Update Failed"),
            I18n::tr("The update installer could not be saved.\n\n%1").arg(detail));
        return;
    }

    delete m_downloadFile;
    m_downloadFile = nullptr;
    delete m_downloadHash;
    m_downloadHash = nullptr;
    emit downloadReady(installerPath);

    // Un borrado de Disk Space pudo empezar mientras bajaba: instalar cierra la app.
    if (refuseWhileBusyElsewhere()) {
        QFile::remove(installerPath);
        return;
    }

    qDebug() << "[UpdateService] Descarga OK, verificada. Instalando:" << installerPath;

    const UpdateInstaller::Result launched = UpdateInstaller::launch(
        installerPath, m_downloadVersion, kDisplayName,
        I18n::tr("The update could not be installed. %1 was left as it was.").arg(kDisplayName));
    if (!launched.started) {
        qDebug() << "[UpdateService] No se pudo lanzar la instalacion, motivo=" << int(launched.failure);
        QFile::remove(installerPath);
        m_busy = false;
        QString reason;
        switch (launched.failure) {
        case UpdateInstaller::Failure::WriteScript:
            reason = I18n::tr("The update script could not be written.");
            break;
        case UpdateInstaller::Failure::StartScript:
            reason = I18n::tr("The update script could not be started.");
            break;
        case UpdateInstaller::Failure::StartInstaller:
            reason = I18n::tr("The installer could not be started.");
            break;
        case UpdateInstaller::Failure::Blocked:
            reason = I18n::tr("This copy cannot update itself.");
            break;
        case UpdateInstaller::Failure::AutomatedRun:
        case UpdateInstaller::Failure::None:
            break;
        }
        QMessageBox::warning(parentWindow(), I18n::tr("Update Failed"),
            I18n::tr("The update could not be started.\n\n%1").arg(reason));
        return;
    }
    // Windows: el .iss ya cierra la instancia en curso igual, pero salir primero es mas limpio.
    // macOS: el script espera a que esta copia termine para reemplazarla.
    qApp->quit();
}

void UpdateService::discardPartialDownload()
{
    if (m_downloadFile) {
        // cancelWriting() borra el temporal: un parcial no se deja en disco
        // haciendose pasar por un instalador valido.
        m_downloadFile->cancelWriting();
        delete m_downloadFile;
        m_downloadFile = nullptr;
    }
    delete m_downloadHash;
    m_downloadHash = nullptr;
    m_downloadTargetPath.clear();
    m_pendingSha256Digest.clear();
    m_downloadUserCancelled = false;
    m_downloadWriteFailed = false;
    m_downloadResponseChecked = false;
    m_downloadAccepted = false;
    m_downloadAbortedOnPurpose = false;
    m_downloadRestartFromZero = false;
    m_downloadRetryPending = false;
    m_downloadShowingRetry = false;
    m_downloadReceived = 0;
    m_downloadResumeOffset = 0;
}
