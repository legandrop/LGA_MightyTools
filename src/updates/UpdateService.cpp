#include "updates/UpdateService.h"
#include "updates/VersionCompare.h"
#include "updates/UpdateDialog.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScopedPointer>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace {

// Manifiesto estatico de LGA, servido por GitHub Pages desde legandrop/LGA_Updates.
// No le pega a la API de GitHub (rate limit 60/h/IP): un workflow de ese repo la
// consulta una vez y publica este archivo, sin limite de lecturas.
const QString kManifestUrl = QStringLiteral("https://legandrop.github.io/LGA_Updates/versions.json");
const QString kRepoSlug = QStringLiteral("legandrop/LGA_MightyTools");
const QString kDisplayName = QStringLiteral("LGA Mighty Tools");

// Demora del chequeo automatico al arrancar: le da tiempo a la app a terminar de
// levantar el tray antes de meter trafico de red.
constexpr int kAutomaticCheckDelayMs = 15000;
constexpr int kCheckTimeoutMs = 15000;
constexpr int kDownloadTimeoutMs = 300000;

// Formato del manifiesto que esta app sabe leer.
constexpr int kManifestSchemaVersion = 1;

// El asset del release para esta app: "LGA_MightyTools_Setup_v<version>.exe".
const QRegularExpression &assetPattern()
{
    static const QRegularExpression pattern(
        QStringLiteral("^LGA_MightyTools_Setup_v(.+)\\.exe$"));
    return pattern;
}

QString normalizedVersion(QString version)
{
    version = version.trimmed();
    if (version.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) {
        version.remove(0, 1);
    }
    return version;
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

// Datos del asset del release que le toca a esta plataforma.
struct ReleaseInfo {
    QString version;
    QUrl downloadUrl;
    QString assetName;
    QString assetDigest;
};

// URL de descarga del asset, DERIVADA del slug/tag/nombre en vez de venir en el
// manifiesto: es publica y estable, y guardarla seria un segundo lugar donde
// quedar vieja.
QUrl assetDownloadUrl(const QString &tag, const QString &name)
{
    return QUrl(QStringLiteral("https://github.com/%1/releases/download/%2/%3")
                    .arg(kRepoSlug, tag, name));
}

// Saca del manifiesto el release de LGA_MightyTools: tag + primer asset cuyo
// nombre matchee assetPattern(). info.version queda vacio si el repo no tiene
// release todavia (entrada ausente) o el manifiesto no se pudo leer.
ReleaseInfo parseManifest(const QByteArray &payload)
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

    const QString tag = product.value(QStringLiteral("tag")).toString().trimmed();
    ReleaseInfo info;
    info.version = normalizedVersion(tag);
    if (info.version.isEmpty()) {
        return {};
    }

    const QJsonArray assets = product.value(QStringLiteral("assets")).toArray();
    for (const QJsonValue &assetValue : assets) {
        const QJsonObject asset = assetValue.toObject();
        const QString name = asset.value(QStringLiteral("name")).toString();
        if (!assetPattern().match(name).hasMatch()) {
            continue;
        }
        info.assetName = name;
        info.downloadUrl = assetDownloadUrl(tag, name);
        info.assetDigest = normalizedSha256Digest(asset.value(QStringLiteral("digest")).toString());
        break;
    }

    return info;
}

} // namespace

UpdateService::UpdateService(QWidget *parentWindow, QObject *parent)
    : QObject(parent)
    , m_parentWindow(parentWindow)
    , m_network(new QNetworkAccessManager(this))
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

void UpdateService::scheduleAutomaticCheck()
{
    if (m_automaticCheckScheduled) {
        return;
    }
    m_automaticCheckScheduled = true;

    QTimer::singleShot(kAutomaticCheckDelayMs, this, [this]() { checkForUpdates(false); });
}

void UpdateService::checkForUpdates(bool manual)
{
    if (!m_network) {
        return;
    }
    const Mode mode = manual ? Mode::Manual : Mode::Automatic;
    if (m_busy) {
        if (manual) {
            QMessageBox::information(parentWindow(), tr("Updates"),
                                     tr("An update operation is already in progress."));
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
        QMessageBox::information(parentWindow(), tr("Updates"), tr("An update operation is already in progress."));
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
        QMessageBox::information(parentWindow(), tr("Updates"), tr("An update operation is already in progress."));
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

    QNetworkRequest request{QUrl(kManifestUrl)};
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("LGA_MightyTools/%1").arg(QApplication::applicationVersion()));
    request.setRawHeader("Accept", "application/json");
    // Pages ya sirve el manifiesto por CDN con su propio cache; el de Qt encima solo
    // agregaria una segunda capa que hace mas dificil saber que version se esta leyendo.
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    request.setTransferTimeout(kCheckTimeoutMs);

    m_checkReply = m_network->get(request);
    connect(m_checkReply, &QNetworkReply::finished, this, [this, mode]() { onCheckFinished(mode); });
    emit checking();

    qDebug() << "[UpdateService] Chequeando updates, modo=" << int(mode);
}

void UpdateService::onCheckFinished(Mode mode)
{
    // Manual (menu) e Inline ("Check now" de General) muestran los errores en cartel; el automatico
    // se calla en todo lo que no sea "hay version nueva".
    const bool manual = mode != Mode::Automatic;
    QNetworkReply *reply = m_checkReply;
    m_checkReply = nullptr;

    if (!reply) {
        // Defensivo: no deberia poder pasar (ver comentario del guard en
        // startCheckRequest), pero si pasa no hay nada mas que vaya a liberar
        // m_busy, asi que se libera aca para no dejar el servicio trabado.
        m_busy = false;
        emit checkFailed();
        return;
    }

    const QNetworkReply::NetworkError error = reply->error();
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray payload = reply->readAll();
    reply->deleteLater();

    if (error != QNetworkReply::NoError) {
        qDebug() << "[UpdateService] Chequeo fallo, networkError=" << error
                 << "httpStatus=" << httpStatus;
        m_busy = false;
        emit checkFailed();
        if (manual) {
            QMessageBox::warning(parentWindow(), tr("Update Check Failed"),
                                 tr("Could not check for updates. Please try again later.\n\n"
                                    "httpStatus=%1\nurl=%2\nnetworkError=%3\nbody=%4")
                                     .arg(QString::number(httpStatus), kManifestUrl,
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
        m_busy = false;
        emit checkFailed();
        if (manual) {
            QMessageBox::warning(parentWindow(), tr("Update Check Failed"),
                                 tr("Could not check for updates. Please try again later.\n\n"
                                    "httpStatus=%1\nurl=%2\nbody=%3")
                                     .arg(QString::number(httpStatus), kManifestUrl,
                                          QString::fromUtf8(payload.left(2048))));
        }
        return;
    }

    const ReleaseInfo info = parseManifest(payload);
    if (info.version.isEmpty()) {
        qDebug() << "[UpdateService] Sin release instalable en el manifiesto.";
        m_busy = false;
        emit checkFailed();
        if (manual) {
            QMessageBox::information(parentWindow(), tr("Updates"),
                                     tr("No installable update was found."));
        }
        return;
    }

    if (!info.downloadUrl.isValid()) {
        qDebug() << "[UpdateService] Release" << info.version << "sin asset que matchee el patron.";
        m_busy = false;
        emit checkFailed();
        if (manual) {
            QMessageBox::information(parentWindow(), tr("Updates"),
                                     tr("Release %1 does not contain an installable asset.")
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
            QMessageBox::information(parentWindow(), tr("Updates"),
                                     tr("You are running the latest version."));
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
            QMessageBox::warning(parentWindow(), tr("Update Check Failed"),
                                 tr("Release %1 does not include a valid integrity digest. "
                                    "Refusing to download an unverifiable installer.")
                                     .arg(info.version));
        }
        return;
    }

    qDebug() << "[UpdateService] Hay update disponible:" << info.version;
    m_availableVersion = info.version;
    m_availableUrl = info.downloadUrl;
    m_availableAsset = info.assetName;
    m_availableDigest = info.assetDigest;
    emit updateAvailable(info.version);
    if (mode == Mode::Inline) {
        // La fila muestra "vX is available" con "Update": no hace falta el cartel.
        m_busy = false;
        return;
    }
    // m_busy queda en true a proposito: sigue representando la operacion en curso
    // durante el dialogo y, si el usuario acepta, durante el arranque de la
    // descarga. Se libera en promptForUpdate (si elige "Later") o en
    // onDownloadFinished / los retornos tempranos de downloadAndRunUpdate.
    promptForUpdate(info.version, info.downloadUrl, info.assetName, info.assetDigest);
}

void UpdateService::promptForUpdate(const QString &version, const QUrl &downloadUrl,
                                    const QString &assetName, const QString &sha256Digest)
{
    // El armado vive en UpdateDialog.cpp para que --ui-shot lo pueda dibujar sin este servicio.
    QScopedPointer<QDialog> dialog(
        createUpdateAvailableDialog(parentWindow(), kDisplayName, version, QApplication::applicationVersion()));

    // Later = no hacer nada: sin snooze ni skip persistente, por diseño. La proxima
    // oportunidad de actualizar es el proximo arranque o un chequeo manual.
    if (dialog->exec() == QDialog::Accepted) {
        downloadAndRunUpdate(downloadUrl, assetName, sha256Digest, version);
    } else {
        // Salida real del flujo chequeo->prompt: recien aca se libera m_busy.
        m_busy = false;
    }
}

void UpdateService::downloadAndRunUpdate(const QUrl &downloadUrl, const QString &assetName,
                                         const QString &sha256Digest, const QString &version)
{
    if (!m_network || m_downloadReply) {
        return;
    }

    m_pendingSha256Digest = normalizedSha256Digest(sha256Digest);

    // Carpeta temporal, no el cache: el instalador descargado es de un solo uso y no
    // hay motivo para que sobreviva a un reinicio del sistema.
    const QString updateDir =
        QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
            .filePath(QStringLiteral("LGA_MightyTools_updates"));
    if (!QDir().mkpath(updateDir)) {
        QMessageBox::warning(parentWindow(), tr("Update Failed"),
                             tr("The update cache directory could not be created."));
        // Salida real del flujo: no hay descarga que vaya a liberar m_busy despues.
        m_busy = false;
        return;
    }

    discardPartialDownload();
    m_downloadTargetPath = QDir(updateDir).filePath(assetName);
    m_downloadUserCancelled = false;
    m_downloadWriteFailed = false;
    m_downloadResponseChecked = false;

    m_downloadFile = new QSaveFile(m_downloadTargetPath);
    if (!m_downloadFile->open(QIODevice::WriteOnly)) {
        const QString detail = m_downloadFile->errorString();
        discardPartialDownload();
        QMessageBox::warning(parentWindow(), tr("Update Failed"),
                             tr("The update installer could not be saved.\n%1").arg(detail));
        // Idem: salida real, sin descarga en vuelo que libere m_busy mas adelante.
        m_busy = false;
        return;
    }
    m_downloadHash = new QCryptographicHash(QCryptographicHash::Sha256);

    m_progressDialog = new QProgressDialog(
        tr("Downloading %1 %2...").arg(kDisplayName, version), tr("Cancel"), 0, 0, parentWindow());
    m_progressDialog->setWindowTitle(tr("Downloading Update"));
    m_progressDialog->setWindowModality(Qt::WindowModal);
    m_progressDialog->setMinimumDuration(0);
    m_progressDialog->setAutoClose(false);
    m_progressDialog->setAutoReset(false);
    m_progressDialog->show();

    QNetworkRequest request{downloadUrl};
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("LGA_MightyTools/%1").arg(QApplication::applicationVersion()));
    // Timeout de INACTIVIDAD, no total: una descarga lenta no se corta mientras
    // sigan llegando bytes.
    request.setTransferTimeout(kDownloadTimeoutMs);

    m_busy = true;
    m_downloadReply = m_network->get(request);
    connect(m_downloadReply, &QNetworkReply::readyRead, this, &UpdateService::onDownloadReadyRead);
    connect(m_downloadReply, &QNetworkReply::downloadProgress, this,
            [this](qint64 bytesReceived, qint64 bytesTotal) {
                if (!m_progressDialog) {
                    return;
                }
                if (bytesTotal > 0) {
                    m_progressDialog->setMaximum(static_cast<int>(bytesTotal / 1024));
                    m_progressDialog->setValue(static_cast<int>(bytesReceived / 1024));
                }
            });
    connect(m_downloadReply, &QNetworkReply::finished, this, &UpdateService::onDownloadFinished);
    connect(m_progressDialog, &QProgressDialog::canceled, this, [this]() {
        m_downloadUserCancelled = true;
        if (m_downloadReply) {
            m_downloadReply->abort();
        }
    });
}

void UpdateService::onDownloadReadyRead()
{
    if (!m_downloadReply || !m_downloadFile) {
        return;
    }

    // Se valida el status UNA vez, al primer dato: escribir el cuerpo de una
    // respuesta de error al disco lo haria pasar por instalador valido.
    if (!m_downloadResponseChecked) {
        m_downloadResponseChecked = true;
        const int status =
            m_downloadReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200) {
            // Diferido: abort() emite finished() sincronicamente, y llamarlo desde
            // aca anidaria onDownloadFinished (con su cartel) dentro de este handler.
            QTimer::singleShot(0, this, [this]() {
                if (m_downloadReply) {
                    m_downloadReply->abort();
                }
            });
            return;
        }
    }

    // Se drena el reply en cada senal: sin esto Qt acumula todo el cuerpo en su
    // buffer interno y la memoria crece con el tamano del asset.
    const QByteArray chunk = m_downloadReply->readAll();
    if (chunk.isEmpty()) {
        return;
    }

    const qint64 written = m_downloadFile->write(chunk);
    if (written != chunk.size()) {
        // Escritura corta o fallida (disco lleno, permisos, etc.): cortar ya, no
        // seguir bajando bytes que no se van a poder guardar completos. Diferido
        // por el mismo motivo que el chequeo de status: abort() emite finished()
        // sincronicamente y anidaria onDownloadFinished dentro de este handler.
        m_downloadWriteFailed = true;
        QTimer::singleShot(0, this, [this]() {
            if (m_downloadReply) {
                m_downloadReply->abort();
            }
        });
        return;
    }
    m_downloadHash->addData(chunk);
}

void UpdateService::onDownloadFinished()
{
    QNetworkReply *reply = m_downloadReply;
    m_downloadReply = nullptr;
    m_busy = false;

    if (!reply) {
        return;
    }

    // finished() puede llegar con bytes sin drenar en el buffer.
    if (m_downloadFile && reply->bytesAvailable() > 0) {
        const QByteArray tail = reply->readAll();
        if (!tail.isEmpty()) {
            const qint64 written = m_downloadFile->write(tail);
            if (written != tail.size()) {
                m_downloadWriteFailed = true;
            } else {
                m_downloadHash->addData(tail);
            }
        }
    }

    const QNetworkReply::NetworkError error = reply->error();
    const QString errorString = reply->errorString();
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();

    if (m_progressDialog) {
        // QProgressDialog::closeEvent() emite canceled(); desconectar antes de cerrar
        // evita que una descarga que termino bien se marque como cancelada por el
        // usuario.
        m_progressDialog->disconnect(this);
        m_progressDialog->close();
        m_progressDialog->deleteLater();
        m_progressDialog = nullptr;
    }

    // Escritura a disco fallida: se chequea ANTES que la cancelacion del usuario y
    // que el error de red generico, porque el abort() diferido que dispara esto
    // deja error() en OperationCanceledError y se confundiria con cualquiera de
    // los otros dos casos, ocultando la causa real (disco lleno, permisos, etc.).
    if (m_downloadWriteFailed) {
        qDebug() << "[UpdateService] Escritura del instalador a disco fallo (write() corto).";
        discardPartialDownload();
        QMessageBox::warning(parentWindow(), tr("Update Failed"),
                             tr("The update installer could not be written to disk."));
        return;
    }

    // Cancelar es una decision del usuario, no un fallo: no lleva cartel.
    if (m_downloadUserCancelled) {
        qDebug() << "[UpdateService] Descarga cancelada por el usuario.";
        discardPartialDownload();
        return;
    }

    if (error != QNetworkReply::NoError) {
        const QString detail = QStringLiteral("httpStatus=%1\nnetworkError=%2 (%3)")
                                   .arg(QString::number(httpStatus),
                                        QString::number(static_cast<int>(error)), errorString);
        discardPartialDownload();
        QMessageBox::warning(parentWindow(), tr("Update Failed"),
                             tr("The update installer could not be downloaded.\n\n%1").arg(detail));
        return;
    }

    // Fail-closed: a esta altura el digest SIEMPRE existe, porque onCheckFinished
    // ya rechazo el update antes de descargar si el manifiesto no traia uno valido.
    // No hay rama "sin digest = sin verificar": si esto dispara es un bug de ese
    // guard, no un caso legitimo.
    Q_ASSERT(!m_pendingSha256Digest.isEmpty());
    if (m_pendingSha256Digest.isEmpty()) {
        discardPartialDownload();
        QMessageBox::warning(parentWindow(), tr("Update Failed"),
                             tr("Internal error: missing integrity digest for the downloaded update."));
        return;
    }

    const QString downloadedDigest = QString::fromLatin1(m_downloadHash->result().toHex());
    if (downloadedDigest.compare(m_pendingSha256Digest, Qt::CaseInsensitive) != 0) {
        const QString detail = QStringLiteral("esperado=%1\nobtenido=%2")
                                   .arg(m_pendingSha256Digest, downloadedDigest);
        discardPartialDownload();
        QMessageBox::warning(parentWindow(), tr("Update Failed"),
                             tr("The downloaded update failed integrity verification.\n\n%1")
                                 .arg(detail));
        return;
    }

    const QString installerPath = m_downloadTargetPath;
    if (!m_downloadFile->commit()) {
        const QString detail = m_downloadFile->errorString();
        discardPartialDownload();
        QMessageBox::warning(parentWindow(), tr("Update Failed"),
                             tr("The update installer could not be saved.\n\n%1").arg(detail));
        return;
    }

    delete m_downloadFile;
    m_downloadFile = nullptr;
    delete m_downloadHash;
    m_downloadHash = nullptr;

    qDebug() << "[UpdateService] Descarga OK, verificada. Lanzando instalador:" << installerPath;

    // El .iss ya mata la instancia en curso igual, pero salir primero es mas limpio.
    QProcess::startDetached(installerPath, {});
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
}
