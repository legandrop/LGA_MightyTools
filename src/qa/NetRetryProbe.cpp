#include "qa/NetRetryProbe.h"

#include "core/AutomatedRun.h"
#include "core/I18n.h"
#include "core/LgaNetRetry.h"
#include "platform/UpdateInstaller.h"
#include "updates/UpdateManifest.h"
#include "updates/UpdateService.h"
#ifdef Q_OS_WIN
#include "modules/dictation/DictationModels.h"
#endif

#include <QApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkProxyFactory>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>
#include <functional>
#include <memory>

// Modelo: las sondas de src/qa/UiShot.cpp (plataforma offscreen, una linea por chequeo). Todo corre en el
// hilo de la interfaz: el servidor de prueba, los pedidos y los carteles (que se cierran solos).

namespace {

// ---------------------------------------------------------------- Servidor de prueba

// Lo que contesta el servidor a un pedido.
enum class Answer {
    CutHeaders,    // 200 con Content-Length y se corta sin mandar el cuerpo
    CutHalf,       // 200 con la mitad del cuerpo y se corta
    Ok,            // 200 entero; con Range, 206 con el resto
    OkIgnoreRange, // 200 entero aunque pida Range
    Status404,
    Status503,
    Status416Empty, // 416 sin cuerpo: no emite readyRead
    Redirect,      // 302 a la URL de serveRedirect
};

// HTTP/1.1 minimo en 127.0.0.1 (o en ::1, el loopback de IPv6, para tener un segundo servidor de la misma
// maquina), un pedido por conexion (Connection: close), con un guion por ruta.
class TestServer : public QObject
{
public:
    explicit TestServer(const QHostAddress &address = QHostAddress(QHostAddress::LocalHost))
    {
        m_server.listen(address, 0);
        QObject::connect(&m_server, &QTcpServer::newConnection, this, [this]() { onConnection(); });
    }

    bool listening() const { return m_server.isListening(); }
    QUrl url(const QString &path) const
    {
        const QHostAddress address = m_server.serverAddress();
        const QString host = address.protocol() == QAbstractSocket::IPv6Protocol
                                 ? QStringLiteral("[%1]").arg(address.toString())
                                 : address.toString();
        return QUrl(QStringLiteral("http://%1:%2%3").arg(host).arg(m_server.serverPort()).arg(path));
    }
    // `plan` para los primeros pedidos, `then` para los que siguen.
    void serve(const QString &path, const QByteArray &body, const QList<Answer> &plan, Answer then = Answer::Ok)
    {
        m_resources.insert(path, Resource{body, plan, then, {}, {}});
    }
    // `plan` para los primeros pedidos y despues un 302 a `target` (como GitHub al servidor de los archivos).
    void serveRedirect(const QString &path, const QUrl &target, const QList<Answer> &plan)
    {
        m_resources.insert(path, Resource{{}, plan, Answer::Redirect, {}, target.toEncoded()});
    }
    int requests(const QString &path) const { return m_resources.value(path).ranges.size(); }
    // El header Range de cada pedido (vacio si no lo trajo).
    QList<QByteArray> ranges(const QString &path) const { return m_resources.value(path).ranges; }

private:
    struct Resource
    {
        QByteArray body;
        QList<Answer> plan;
        Answer then = Answer::Ok;
        QList<QByteArray> ranges;
        QByteArray location; // Redirect
    };

    void onConnection()
    {
        while (QTcpSocket *socket = m_server.nextPendingConnection()) {
            auto buffer = std::make_shared<QByteArray>();
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            QObject::connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer]() {
                buffer->append(socket->readAll());
                if (buffer->contains("\r\n\r\n")) {
                    QObject::disconnect(socket, &QTcpSocket::readyRead, this, nullptr);
                    answer(socket, *buffer);
                }
            });
        }
    }

    void answer(QTcpSocket *socket, const QByteArray &request)
    {
        const QList<QByteArray> lines = request.split('\n');
        const QList<QByteArray> first = lines.value(0).trimmed().split(' ');
        const QString path = QString::fromLatin1(first.value(1));
        QByteArray range;
        for (const QByteArray &line : lines) {
            if (line.toLower().startsWith("range:")) {
                range = line.mid(6).trimmed();
            }
        }
        const auto head = [](int code, const char *reason, qint64 length, const QByteArray &extra) {
            return "HTTP/1.1 " + QByteArray::number(code) + ' ' + reason + "\r\nContent-Length: "
                   + QByteArray::number(length) + "\r\nConnection: close\r\n" + extra + "\r\n";
        };
        QByteArray out;
        auto it = m_resources.find(path);
        if (it == m_resources.end()) {
            out = head(404, "Not Found", 0, QByteArray());
        } else {
            Resource &resource = it.value();
            resource.ranges << range;
            const Answer what = resource.plan.isEmpty() ? resource.then : resource.plan.takeFirst();
            const QByteArray &body = resource.body;
            const QByteArray error("error de prueba");
            qint64 start = 0;
            if (range.startsWith("bytes=")) {
                start = range.mid(6).split('-').value(0).toLongLong();
            }
            switch (what) {
            case Answer::CutHeaders:
                out = head(200, "OK", body.size(), QByteArray());
                break;
            case Answer::CutHalf:
                out = head(200, "OK", body.size(), QByteArray()) + body.left(body.size() / 2);
                break;
            case Answer::Ok:
                if (!range.isEmpty() && start > 0 && start < body.size()) {
                    out = head(206, "Partial Content", body.size() - start,
                               "Content-Range: bytes " + QByteArray::number(start) + '-' + QByteArray::number(body.size() - 1)
                                   + '/' + QByteArray::number(body.size()) + "\r\n")
                          + body.mid(int(start));
                } else {
                    out = head(200, "OK", body.size(), QByteArray()) + body;
                }
                break;
            case Answer::OkIgnoreRange:
                out = head(200, "OK", body.size(), QByteArray()) + body;
                break;
            case Answer::Status404:
                out = head(404, "Not Found", error.size(), QByteArray()) + error;
                break;
            case Answer::Status503:
                out = head(503, "Service Unavailable", error.size(), QByteArray()) + error;
                break;
            case Answer::Status416Empty:
                out = head(416, "Range Not Satisfiable", 0, "Content-Range: bytes */" + QByteArray::number(body.size()) + "\r\n");
                break;
            case Answer::Redirect:
                out = head(302, "Found", 0, "Location: " + resource.location + "\r\n");
                break;
            }
        }
        socket->write(out);
        // Cierre ordenado: lo escrito sale antes del FIN, asi el cliente ve el corte DESPUES de los headers.
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QHash<QString, Resource> m_resources;
};

// Un puerto que nadie escucha: la conexion se rechaza.
QUrl closedPortUrl(const QString &path)
{
    QTcpServer probe;
    probe.listen(QHostAddress::LocalHost, 0);
    const quint16 port = probe.serverPort();
    probe.close();
    return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(port).arg(path));
}

// ---------------------------------------------------------------- Espera, carteles y log

// Cuatro intentos contra un puerto cerrado: en Windows cada rechazo tarda unos 2 s.
constexpr int kClosedPortMs = 20000;

bool waitFor(const std::function<bool()> &done, int timeoutMs)
{
    QElapsedTimer clock;
    clock.start();
    while (!done()) {
        if (clock.elapsed() > timeoutMs) {
            return false;
        }
        QEventLoop loop;
        QTimer::singleShot(5, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return true;
}

void pump(int ms)
{
    waitFor([]() { return false; }, ms);
}

// Cierra los carteles modales (QMessageBox::warning de UpdateService) apenas aparecen y guarda su texto, y
// anota cada texto que muestra el dialogo de la descarga. Un QTimer corre tambien dentro del exec() del cartel.
class Watcher
{
public:
    Watcher()
    {
        m_timer.setInterval(10);
        QObject::connect(&m_timer, &QTimer::timeout, [this]() { tick(); });
        m_timer.start();
    }
    void reset()
    {
        boxes.clear();
        progressTexts.clear();
    }
    QPointer<QProgressDialog> progress() const
    {
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (auto *dialog = qobject_cast<QProgressDialog *>(widget)) {
                if (dialog->isVisible()) return dialog;
            }
        }
        return nullptr;
    }

    QStringList boxes;
    QSet<QString> progressTexts;

private:
    void tick()
    {
        if (QPointer<QProgressDialog> dialog = progress()) {
            progressTexts.insert(dialog->labelText());
        }
        if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
            auto *box = qobject_cast<QMessageBox *>(dialog);
            boxes << (box ? box->text() : dialog->windowTitle());
            dialog->done(QDialog::Rejected);
        }
    }

    QTimer m_timer;
};

QStringList g_log;

void captureLog(QtMsgType, const QMessageLogContext &, const QString &message)
{
    g_log << message;
}

int logCount(const QString &needle, const QString &also = QString())
{
    int count = 0;
    for (const QString &line : g_log) {
        if (line.contains(needle) && (also.isEmpty() || line.contains(also))) ++count;
    }
    return count;
}

QByteArray sha256(const QByteArray &data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
}

QByteArray fileSha256(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return sha256(file.readAll());
}

// Lo que se "baja": bytes armados, que no empiezan como un ejecutable.
QByteArray payload(int size)
{
    QByteArray data;
    data.reserve(size);
    for (int i = 0; i < size; ++i) {
        data.append(char('a' + (i * 7 + i / 977) % 26));
    }
    return data;
}

QStringList filesIn(const QString &dir)
{
    return QDir(dir).entryList(QDir::Files | QDir::Hidden | QDir::System);
}

// ---------------------------------------------------------------- Chequeos

struct Probe
{
    int checks = 0;
    int failures = 0;
    void check(bool ok, const QString &what)
    {
        ++checks;
        if (!ok) ++failures;
        fprintf(stdout, "%s %s\n", ok ? "ok  " : "FAIL", qPrintable(what));
        if (!ok) {
            // Lo ultimo del log, para ver que paso sin volver a correr.
            for (int i = qMax(0, int(g_log.size()) - 8); i < g_log.size(); ++i) {
                fprintf(stdout, "       log: %s\n", qPrintable(g_log.at(i)));
            }
        }
        fflush(stdout);
    }
};

QString retryText(int attempt)
{
    return I18n::tr("Retrying (%1 of %2)…").arg(attempt).arg(LgaNetRetry::kRetries + 1);
}

// ---------------------------------------------------------------- Updater

const QString kManifestPath = QStringLiteral("/versions.json");

QString assetName()
{
    return UpdateManifest::currentPlatform() == UpdateManifest::Platform::Mac ? QStringLiteral("LGA_MightyTools_Mac_v99.0.zip")
                                                                              : QStringLiteral("LGA_MightyTools_Setup_v99.0.exe");
}

QByteArray manifestFor(const QByteArray &digest)
{
    const QByteArray asset = "{\"tag\":\"v99.0\",\"name\":\"" + assetName().toLatin1() + "\",\"digest\":\"sha256:" + digest
                             + "\",\"size\":1}";
    return "{\"schemaVersion\":1,\"products\":{\"legandrop/LGA_MightyTools\":{\"tag\":\"v99.0\",\"assets\":[" + asset
           + "],\"assetLatest\":[" + asset + "]}}}";
}

// Un UpdateService de prueba con sus senales contadas.
struct UpdateRun
{
    std::unique_ptr<UpdateService> service;
    QList<int> retries;
    int checking = 0;
    int failed = 0;
    int available = 0;
    QStringList ready;     // instaladores verificados
    QStringList readyHash; // su SHA-256, leido en el momento (despues se borran)
    bool endpoints = false;

    UpdateRun(const QUrl &manifest, const QUrl &downloadBase, const QString &dir)
        : service(std::make_unique<UpdateService>(nullptr))
    {
        endpoints = service->setEndpointsForTests(manifest, downloadBase, dir);
        UpdateService *s = service.get();
        QObject::connect(s, &UpdateService::checking, s, [this]() { ++checking; });
        QObject::connect(s, &UpdateService::retrying, s, [this](int attempt, int) { retries << attempt; });
        QObject::connect(s, &UpdateService::checkFailed, s, [this]() { ++failed; });
        QObject::connect(s, &UpdateService::updateAvailable, s, [this](const QString &) { ++available; });
        QObject::connect(s, &UpdateService::downloadReady, s, [this](const QString &path) {
            ready << path;
            readyHash << QString::fromLatin1(fileSha256(path));
        });
    }
    ~UpdateRun()
    {
        service.reset();
        pump(20); // los deleteLater de los pedidos
    }
};

void testUpdateCheck(Probe &probe, Watcher &watcher, const QString &dir)
{
    const QByteArray manifest = manifestFor(QByteArray(64, 'a'));
    const int delay = LgaNetRetry::delayMs();
    const QString attempt = QStringLiteral("Chequeando updates");

    {
        // (1) Corta las dos primeras respuestas despues de los headers: exito en el tercer intento.
        TestServer server;
        server.serve(kManifestPath, manifest, {Answer::CutHeaders, Answer::CutHeaders});
        UpdateRun run(server.url(kManifestPath), server.url(QStringLiteral("/dl/")), dir);
        probe.check(server.listening() && run.endpoints, QStringLiteral("updater: servidor de prueba y endpoints en 127.0.0.1"));
        if (!run.endpoints) return; // sin endpoints de prueba iria al manifiesto real: no se sigue
        g_log.clear();
        watcher.reset();
        run.service->checkInline();
        waitFor([&]() { return run.available + run.failed > 0; }, 5000);
        probe.check(run.available == 1 && run.failed == 0 && server.requests(kManifestPath) == 3
                        && run.retries == QList<int>{2, 3}
                        && logCount(attempt, QStringLiteral("intento 3 (3 de 4 en 127.0.0.1)")) == 1
                        && logCount(attempt) == 3 && watcher.boxes.isEmpty(),
                    QStringLiteral("updater chequeo (1): dos cortes y exito al 3.er intento (pedidos=%1, reintentos=%2)")
                        .arg(server.requests(kManifestPath))
                        .arg(run.retries.size()));
    }
    {
        // (2) Puerto cerrado: exactamente cuatro intentos y el cartel de error. Windows tarda ~2 s en rechazar
        // cada conexion a un puerto cerrado (reintenta el SYN): de ahi la espera larga.
        UpdateRun run(closedPortUrl(kManifestPath), closedPortUrl(QStringLiteral("/dl/")), dir);
        g_log.clear();
        watcher.reset();
        run.service->checkInline();
        waitFor([&]() { return run.failed > 0; }, kClosedPortMs);
        pump(4 * delay + 100);
        probe.check(run.failed == 1 && run.available == 0 && run.retries == QList<int>{2, 3, 4} && logCount(attempt) == 4
                        && watcher.boxes.size() == 1
                        && watcher.boxes.value(0).startsWith(I18n::tr("Could not check for updates. Please try again later.\n\n"
                                                                      "httpStatus=%1\nurl=%2\nnetworkError=%3\nbody=%4")
                                                                 .section(QLatin1Char('\n'), 0, 0)),
                    QStringLiteral("updater chequeo (2): puerto cerrado, 4 intentos y error visible (intentos=%1, carteles=%2)")
                        .arg(logCount(attempt))
                        .arg(watcher.boxes.size()));
    }
    {
        // (3) 404: un solo intento.
        TestServer server;
        server.serve(kManifestPath, manifest, {}, Answer::Status404);
        UpdateRun run(server.url(kManifestPath), server.url(QStringLiteral("/dl/")), dir);
        g_log.clear();
        watcher.reset();
        run.service->checkInline();
        waitFor([&]() { return run.failed > 0; }, 5000);
        pump(2 * delay + 50);
        probe.check(run.failed == 1 && server.requests(kManifestPath) == 1 && run.retries.isEmpty() && watcher.boxes.size() == 1,
                    QStringLiteral("updater chequeo (3): 404, un solo intento y error visible (pedidos=%1)")
                        .arg(server.requests(kManifestPath)));
    }
    {
        // (4) 503 dos veces y despues 200.
        TestServer server;
        server.serve(kManifestPath, manifest, {Answer::Status503, Answer::Status503});
        UpdateRun run(server.url(kManifestPath), server.url(QStringLiteral("/dl/")), dir);
        g_log.clear();
        watcher.reset();
        run.service->checkInline();
        waitFor([&]() { return run.available + run.failed > 0; }, 5000);
        probe.check(run.available == 1 && server.requests(kManifestPath) == 3 && run.retries == QList<int>{2, 3}
                        && watcher.boxes.isEmpty(),
                    QStringLiteral("updater chequeo (4): 503, 503 y 200 -> exito (pedidos=%1)").arg(server.requests(kManifestPath)));
    }
    {
        // (5) Cerrar la app durante la espera: el servicio se destruye y no sale ningun pedido mas.
        TestServer server;
        server.serve(kManifestPath, manifest, {}, Answer::CutHeaders);
        auto run = std::make_unique<UpdateRun>(server.url(kManifestPath), server.url(QStringLiteral("/dl/")), dir);
        g_log.clear();
        watcher.reset();
        run->service->checkInline();
        const bool waiting = waitFor([&]() { return !run->retries.isEmpty(); }, 5000);
        run.reset();
        pump(4 * delay + 100);
        probe.check(waiting && server.requests(kManifestPath) == 1 && watcher.boxes.isEmpty(),
                    QStringLiteral("updater chequeo (5): destruir el servicio en la espera corta el reintento (pedidos=%1)")
                        .arg(server.requests(kManifestPath)));
    }
}

// Chequeo + "Update": la descarga contra `downloadPlan` (o contra un puerto cerrado). Devuelve la corrida
// terminada: con el instalador verificado en `ready`, o con el cartel de error en el Watcher.
struct DownloadCase
{
    QList<Answer> plan;
    Answer then = Answer::Ok;
    bool closedPort = false;
};

void testUpdateDownload(Probe &probe, Watcher &watcher, const QString &baseDir)
{
    if (UpdateInstaller::blocker() != UpdateInstaller::Blocker::None) {
        // macOS desde build/: la copia de desarrollo no se actualiza sola (el cartel lo dice). Solo Windows.
        fprintf(stdout, "-    updater descarga: omitida (esta copia no se actualiza sola)\n");
        return;
    }
    const QByteArray body = payload(300000);
    const QString digest = QString::fromLatin1(sha256(body));
    const QByteArray manifest = manifestFor(sha256(body));
    const QString dlPath = QStringLiteral("/dl/") + assetName();
    const int delay = LgaNetRetry::delayMs();
    const QString attempt = QStringLiteral("Bajando el instalador");
    const QString started = I18n::tr("The update could not be started.\n\n%1").arg(QString());
    int index = 0;

    // Arma la corrida y deja hecho el chequeo (Inline: sin el cartel de "Update Available").
    const auto prepare = [&](TestServer &server, const DownloadCase &c, QString *dir) {
        *dir = QDir(baseDir).filePath(QStringLiteral("update-%1").arg(++index));
        QDir().mkpath(*dir);
        server.serve(kManifestPath, manifest, {});
        server.serve(dlPath, body, c.plan, c.then);
        const QUrl base = c.closedPort ? closedPortUrl(QStringLiteral("/dl/")) : server.url(QStringLiteral("/dl/"));
        auto run = std::make_unique<UpdateRun>(server.url(kManifestPath), base, *dir);
        if (!run->endpoints) return std::unique_ptr<UpdateRun>();
        run->service->checkInline();
        waitFor([&]() { return run->available + run->failed > 0; }, 5000);
        g_log.clear();
        watcher.reset();
        return run;
    };
    const auto finish = [&](UpdateRun &run, int boxes) {
        waitFor([&]() { return watcher.boxes.size() >= boxes; }, 8000);
        pump(20);
        Q_UNUSED(run);
    };

    {
        // (1) Corta dos veces despues de los headers y despues baja entero.
        TestServer server;
        QString dir;
        auto run = prepare(server, {{Answer::CutHeaders, Answer::CutHeaders}}, &dir);
        probe.check(run && run->available == 1, QStringLiteral("updater descarga: el chequeo encuentra la 99.0"));
        if (!run) return;
        run->service->installAvailable();
        finish(*run, 1);
        probe.check(run->ready.size() == 1 && run->readyHash.value(0) == digest && server.requests(dlPath) == 3
                        && logCount(attempt) == 3 && watcher.progressTexts.contains(
                               QStringLiteral("%1\n%2").arg(I18n::tr("Downloading %1 %2...").arg(QStringLiteral("LGA Mighty Tools"),
                                                                                                 QStringLiteral("99.0")),
                                                            retryText(2)))
                        && watcher.boxes.value(0) == started,
                    QStringLiteral("updater descarga (1): dos cortes, exito al 3.er intento, el dialogo dice que reintenta "
                                   "y el SHA-256 coincide (pedidos=%1)")
                        .arg(server.requests(dlPath)));
        probe.check(filesIn(dir).isEmpty(), QStringLiteral("updater descarga (1): no queda nada en la carpeta (el instalador "
                                                            "verificado no se ejecuta en una corrida automatizada y se borra)"));
    }
    {
        // (2) La descarga contra un puerto cerrado: cuatro intentos y el cartel.
        TestServer server;
        QString dir;
        DownloadCase c;
        c.closedPort = true;
        auto run = prepare(server, c, &dir);
        if (!run) return;
        run->service->installAvailable();
        waitFor([&]() { return !watcher.boxes.isEmpty(); }, kClosedPortMs);
        pump(3 * delay);
        probe.check(run->ready.isEmpty() && logCount(attempt) == 4 && watcher.boxes.size() == 1
                        && watcher.boxes.value(0).contains(QStringLiteral("attempts=4")) && filesIn(dir).isEmpty(),
                    QStringLiteral("updater descarga (2): puerto cerrado, 4 intentos, error visible y sin parcial (intentos=%1)")
                        .arg(logCount(attempt)));
    }
    {
        // (3) 404: un intento.
        TestServer server;
        QString dir;
        auto run = prepare(server, {{}, Answer::Status404}, &dir);
        if (!run) return;
        run->service->installAvailable();
        finish(*run, 1);
        pump(2 * delay);
        probe.check(run->ready.isEmpty() && server.requests(dlPath) == 1 && watcher.boxes.size() == 1
                        && watcher.boxes.value(0).contains(QStringLiteral("attempts=1")) && filesIn(dir).isEmpty(),
                    QStringLiteral("updater descarga (3): 404, un solo intento y error visible (pedidos=%1)").arg(server.requests(dlPath)));
    }
    {
        // (4) 503 dos veces y despues 200.
        TestServer server;
        QString dir;
        auto run = prepare(server, {{Answer::Status503, Answer::Status503}}, &dir);
        if (!run) return;
        run->service->installAvailable();
        finish(*run, 1);
        probe.check(run->ready.size() == 1 && run->readyHash.value(0) == digest && server.requests(dlPath) == 3
                        && watcher.boxes.value(0) == started,
                    QStringLiteral("updater descarga (4): 503, 503 y 200 -> exito con el SHA-256 correcto (pedidos=%1)")
                        .arg(server.requests(dlPath)));
    }
    {
        // (5) Cancel durante la espera: no sale ningun pedido mas, no queda parcial y el servicio queda libre.
        TestServer server;
        QString dir;
        auto run = prepare(server, {{}, Answer::CutHeaders}, &dir);
        if (!run) return;
        run->service->installAvailable();
        const bool waiting = waitFor([&]() {
            const QPointer<QProgressDialog> dialog = watcher.progress();
            return dialog && dialog->labelText().contains(retryText(2));
        }, 5000);
        // El boton Cancel, como el usuario (cancel() a secas no emite canceled()).
        if (const QPointer<QProgressDialog> dialog = watcher.progress()) {
            if (auto *button = dialog->findChild<QPushButton *>()) button->click();
        }
        pump(4 * delay + 100);
        const int checkingBefore = run->checking;
        run->service->checkInline(); // con el servicio trabado saldria "already in progress" en un cartel
        waitFor([&]() { return run->available + run->failed > 1; }, 5000);
        probe.check(waiting && server.requests(dlPath) == 1 && run->ready.isEmpty() && watcher.boxes.isEmpty()
                        && filesIn(dir).isEmpty() && run->checking == checkingBefore + 1 && !watcher.progress(),
                    QStringLiteral("updater descarga (5): Cancel en la espera corta el reintento, sin parcial y sin quedar "
                                   "trabado (pedidos=%1, carteles=%2)")
                        .arg(server.requests(dlPath))
                        .arg(watcher.boxes.size()));
    }
    {
        // (6) Cortada a la mitad: sigue con Range y termina con el archivo correcto.
        TestServer server;
        QString dir;
        auto run = prepare(server, {{Answer::CutHalf}, Answer::Ok}, &dir);
        if (!run) return;
        run->service->installAvailable();
        finish(*run, 1);
        const QList<QByteArray> ranges = server.ranges(dlPath);
        probe.check(run->ready.size() == 1 && run->readyHash.value(0) == digest && ranges.size() == 2 && ranges.at(0).isEmpty()
                        && ranges.at(1) == "bytes=" + QByteArray::number(body.size() / 2) + '-',
                    QStringLiteral("updater descarga (6): cortada a la mitad, sigue con Range (%1) y el SHA-256 coincide")
                        .arg(QString::fromLatin1(ranges.value(1))));
    }
    {
        // (6b) Cortada a la mitad contra un servidor sin Range: empieza de cero, sin mezclar.
        TestServer server;
        QString dir;
        auto run = prepare(server, {{Answer::CutHalf}, Answer::OkIgnoreRange}, &dir);
        if (!run) return;
        run->service->installAvailable();
        finish(*run, 1);
        const QList<QByteArray> ranges = server.ranges(dlPath);
        probe.check(run->ready.size() == 1 && run->readyHash.value(0) == digest && ranges.size() == 2 && !ranges.at(1).isEmpty()
                        && logCount(QStringLiteral("no siguio desde el byte")) == 1,
                    QStringLiteral("updater descarga (6b): servidor sin Range, vuelve a cero y el SHA-256 coincide"));
    }
    {
        // (6c) Cortada a la mitad y el servidor contesta al Range con un 416 SIN cuerpo (no hay readyRead):
        // igual se valida el status al terminar, vuelve a cero y el tercer pedido baja todo entero.
        TestServer server;
        QString dir;
        auto run = prepare(server, {{Answer::CutHalf, Answer::Status416Empty}, Answer::Ok}, &dir);
        if (!run) return;
        run->service->installAvailable();
        finish(*run, 1);
        const QList<QByteArray> ranges = server.ranges(dlPath);
        probe.check(run->ready.size() == 1 && run->readyHash.value(0) == digest && ranges.size() == 3 && !ranges.at(1).isEmpty()
                        && ranges.at(2).isEmpty() && watcher.boxes.value(0) == started,
                    QStringLiteral("updater descarga (6c): 416 vacio al pedir el resto, vuelve a cero y el SHA-256 coincide "
                                   "(pedidos=%1, primer cartel=%2)")
                        .arg(ranges.size())
                        .arg(watcher.boxes.value(0).left(40)));
    }
    {
        // (7) Como GitHub: el primer servidor redirige al de los archivos (aca, ::1). Falla dos veces en el
        // primero (503) y tres en el segundo (cortes): cada servidor tiene sus 3 reintentos (LgaNetRetry::Budget),
        // asi que baja al 6.o intento. Con una sola cuenta para todo el pedido fallaria al 4.o.
        TestServer server;
        TestServer files{QHostAddress(QHostAddress::LocalHostIPv6)};
        QString dir;
        auto run = prepare(server, {}, &dir);
        probe.check(files.listening(), QStringLiteral("updater descarga (7): segundo servidor de prueba en ::1"));
        if (!run || !files.listening()) return;
        server.serveRedirect(dlPath, files.url(dlPath), {Answer::Status503, Answer::Status503});
        files.serve(dlPath, body, {Answer::CutHeaders, Answer::CutHeaders, Answer::CutHeaders});
        run->service->installAvailable();
        finish(*run, 1);
        probe.check(run->ready.size() == 1 && run->readyHash.value(0) == digest && server.requests(dlPath) == 6
                        && files.requests(dlPath) == 4 && logCount(attempt, QStringLiteral("intento 6 (4 de 4 en ::1)")) == 1
                        && watcher.progressTexts.contains(QStringLiteral("%1\n%2").arg(
                               I18n::tr("Downloading %1 %2...").arg(QStringLiteral("LGA Mighty Tools"), QStringLiteral("99.0")),
                               retryText(4)))
                        && watcher.boxes.value(0) == started,
                    QStringLiteral("updater descarga (7): redirect a otro servidor, cada uno con su cuenta y exito al "
                                   "6.o intento (pedidos=%1+%2)")
                        .arg(server.requests(dlPath))
                        .arg(files.requests(dlPath)));
    }
}

// ---------------------------------------------------------------- Dictado (solo Windows)

#ifdef Q_OS_WIN
using State = DictationModels::State;

struct ModelRun
{
    DictationModels::Piece piece;
    std::unique_ptr<DictationModels> models;
    QList<int> retries; // los retryAttempt() que se vieron, sin repetir seguidos

    ModelRun(const QString &root, const QUrl &url, const QByteArray &body)
    {
        piece.id = QStringLiteral("model");
        piece.title = QStringLiteral("modelo de prueba");
        piece.url = url;
        piece.size = body.size();
        piece.sha256 = sha256(body);
        piece.license = QStringLiteral("MIT");
        piece.relativePath = QStringLiteral("whisper/net-retry.gguf");
        models = std::make_unique<DictationModels>(root, QList<DictationModels::Piece>{piece});
        DictationModels *m = models.get();
        QObject::connect(m, &DictationModels::changed, m, [this, m](const QString &id) {
            const int attempt = m->retryAttempt(id);
            if (attempt > 0 && (retries.isEmpty() || retries.last() != attempt)) retries << attempt;
        });
    }
    ~ModelRun()
    {
        models.reset();
        pump(20);
    }
    QString target() const { return models->path(piece.id); }
    QString part() const { return target() + QStringLiteral(".part"); }
    bool settled() const
    {
        const State s = models->state(piece.id);
        return s != State::Downloading && s != State::Verifying;
    }
    QByteArray onDisk() const
    {
        QFile file(target());
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
};

void testDictation(Probe &probe, const QString &baseDir)
{
    const QByteArray body = payload(400000);
    const QString path = QStringLiteral("/model.bin");
    const int delay = LgaNetRetry::delayMs();
    const QString attempt = QStringLiteral("[Dictado] Bajando");
    int index = 0;
    const auto root = [&]() { return QDir(baseDir).filePath(QStringLiteral("dictation-%1/models").arg(++index)); };

    {
        // (1) Corta dos veces y despues baja entero.
        TestServer server;
        server.serve(path, body, {Answer::CutHeaders, Answer::CutHeaders});
        ModelRun run(root(), server.url(path), body);
        g_log.clear();
        run.models->download(run.piece.id);
        waitFor([&]() { return run.settled(); }, 8000);
        probe.check(run.models->state(run.piece.id) == State::Ready && run.onDisk() == body && server.requests(path) == 3
                        && run.retries == QList<int>{2, 3} && logCount(attempt) == 3 && !QFileInfo::exists(run.part())
                        && run.models->retryAttempt(run.piece.id) == 0,
                    QStringLiteral("dictado (1): dos cortes, lista al 3.er intento con el archivo correcto (pedidos=%1, %2)")
                        .arg(server.requests(path))
                        .arg(run.models->errorText(run.piece.id)));
    }
    {
        // (2) Puerto cerrado: cuatro intentos y el error en la pieza.
        ModelRun run(root(), closedPortUrl(path), body);
        g_log.clear();
        run.models->download(run.piece.id);
        waitFor([&]() { return run.settled(); }, kClosedPortMs);
        pump(3 * delay);
        probe.check(run.models->state(run.piece.id) == State::Error
                        && run.models->errorText(run.piece.id).startsWith(I18n::tr("The download failed: %1").arg(QString()))
                        && run.models->errorText(run.piece.id).size() > I18n::tr("The download failed: %1").arg(QString()).size()
                        && run.retries == QList<int>{2, 3, 4} && logCount(attempt) == 4 && !QFileInfo::exists(run.part()),
                    QStringLiteral("dictado (2): puerto cerrado, 4 intentos y error visible: %1").arg(run.models->errorText(run.piece.id)));
    }
    {
        // (3) 404: un intento.
        TestServer server;
        server.serve(path, body, {}, Answer::Status404);
        ModelRun run(root(), server.url(path), body);
        g_log.clear();
        run.models->download(run.piece.id);
        waitFor([&]() { return run.settled(); }, 8000);
        pump(2 * delay);
        probe.check(run.models->state(run.piece.id) == State::Error && server.requests(path) == 1 && run.retries.isEmpty()
                        && run.models->errorText(run.piece.id) == I18n::tr("The server answered %1.").arg(404)
                        && !QFileInfo::exists(run.part()),
                    QStringLiteral("dictado (3): 404, un solo intento: %1").arg(run.models->errorText(run.piece.id)));
    }
    {
        // (4) 503 dos veces y despues 200.
        TestServer server;
        server.serve(path, body, {Answer::Status503, Answer::Status503});
        ModelRun run(root(), server.url(path), body);
        run.models->download(run.piece.id);
        waitFor([&]() { return run.settled(); }, 8000);
        probe.check(run.models->state(run.piece.id) == State::Ready && run.onDisk() == body && server.requests(path) == 3,
                    QStringLiteral("dictado (4): 503, 503 y 200 -> lista (pedidos=%1)").arg(server.requests(path)));
    }
    {
        // (5) Cancel durante la espera.
        TestServer server;
        server.serve(path, body, {}, Answer::CutHeaders);
        ModelRun run(root(), server.url(path), body);
        run.models->download(run.piece.id);
        const bool waiting = waitFor([&]() { return run.models->retryAttempt(run.piece.id) == 2; }, 5000);
        run.models->cancel(run.piece.id);
        pump(4 * delay + 100);
        probe.check(waiting && run.models->state(run.piece.id) == State::Missing && server.requests(path) == 1
                        && run.models->retryAttempt(run.piece.id) == 0 && !QFileInfo::exists(run.part()),
                    QStringLiteral("dictado (5): Cancel en la espera corta el reintento y borra el .part (pedidos=%1)")
                        .arg(server.requests(path)));
    }
    {
        // (5b) Apagar la herramienta durante la espera (se destruye DictationModels).
        TestServer server;
        server.serve(path, body, {}, Answer::CutHeaders);
        auto run = std::make_unique<ModelRun>(root(), server.url(path), body);
        const QString part = run->part();
        run->models->download(run->piece.id);
        const bool waiting = waitFor([&]() { return run->models->retryAttempt(run->piece.id) == 2; }, 5000);
        run.reset();
        pump(4 * delay + 100);
        probe.check(waiting && server.requests(path) == 1 && !QFileInfo::exists(part),
                    QStringLiteral("dictado (5b): apagar la herramienta en la espera no deja pedidos ni .part (pedidos=%1)")
                        .arg(server.requests(path)));
    }
    {
        // (6) Cortada a la mitad: sigue con Range y termina con el archivo correcto.
        TestServer server;
        server.serve(path, body, {Answer::CutHalf}, Answer::Ok);
        ModelRun run(root(), server.url(path), body);
        run.models->download(run.piece.id);
        waitFor([&]() { return run.settled(); }, 8000);
        const QList<QByteArray> ranges = server.ranges(path);
        probe.check(run.models->state(run.piece.id) == State::Ready && run.onDisk() == body && ranges.size() == 2
                        && ranges.at(0).isEmpty() && ranges.at(1) == "bytes=" + QByteArray::number(body.size() / 2) + '-',
                    QStringLiteral("dictado (6): cortada a la mitad, sigue con Range (%1) y queda el archivo correcto")
                        .arg(QString::fromLatin1(ranges.value(1))));
    }
    {
        // (6b) Sin Range: vuelve a cero, sin mezclar.
        TestServer server;
        server.serve(path, body, {Answer::CutHalf}, Answer::OkIgnoreRange);
        ModelRun run(root(), server.url(path), body);
        g_log.clear();
        run.models->download(run.piece.id);
        waitFor([&]() { return run.settled(); }, 8000);
        probe.check(run.models->state(run.piece.id) == State::Ready && run.onDisk() == body && server.requests(path) == 2
                        && logCount(QStringLiteral("no siguio desde el byte")) == 1,
                    QStringLiteral("dictado (6b): servidor sin Range, vuelve a cero y queda el archivo correcto"));
    }
    {
        // (7) Como Hugging Face y GitHub: redirect al servidor de los archivos (::1). Dos 503 en el primero y
        // tres cortes en el segundo: cada servidor tiene su cuenta, la fila vuelve a contar desde 2 y baja al 6.o.
        TestServer server;
        TestServer files{QHostAddress(QHostAddress::LocalHostIPv6)};
        probe.check(files.listening(), QStringLiteral("dictado (7): segundo servidor de prueba en ::1"));
        if (files.listening()) {
            server.serveRedirect(path, files.url(path), {Answer::Status503, Answer::Status503});
            files.serve(path, body, {Answer::CutHeaders, Answer::CutHeaders, Answer::CutHeaders});
            ModelRun run(root(), server.url(path), body);
            g_log.clear();
            run.models->download(run.piece.id);
            waitFor([&]() { return run.settled(); }, 8000);
            probe.check(run.models->state(run.piece.id) == State::Ready && run.onDisk() == body
                            && server.requests(path) == 6 && files.requests(path) == 4
                            && run.retries == QList<int>{2, 3, 2, 3, 4}
                            && logCount(attempt, QStringLiteral("intento 6 (4 de 4 en ::1)")) == 1,
                        QStringLiteral("dictado (7): redirect a otro servidor, cada uno con su cuenta y lista al 6.o "
                                       "intento (pedidos=%1+%2, %3)")
                            .arg(server.requests(path))
                            .arg(files.requests(path))
                            .arg(run.models->errorText(run.piece.id)));
        }
    }
    {
        // Afuera de 127.0.0.1 sigue sin bajar nada en una corrida automatizada.
        ModelRun run(root(), QUrl(QStringLiteral("http://example.invalid/model.bin")), body);
        run.models->download(run.piece.id);
        probe.check(run.models->state(run.piece.id) == State::Error && run.models->findChildren<QNetworkAccessManager *>().isEmpty(),
                    QStringLiteral("dictado: en una corrida automatizada, un servidor que no es 127.0.0.1 no se toca"));
    }
}
#endif

} // namespace

int runNetRetryProbe(const QStringList &args)
{
    if (!AutomatedRun::active()) {
        fprintf(stderr, "net-retry: solo en una corrida automatizada\n");
        return 2;
    }
    // La carpeta de la corrida: una subcarpeta nueva de --work (nunca se reusa ni se borra), o una temporal.
    QTemporaryDir temp;
    QString base;
    const int workIndex = args.indexOf(QStringLiteral("--work"));
    if (workIndex >= 0) {
        const QString work = args.value(workIndex + 1);
        if (work.isEmpty() || !QFileInfo(work).isAbsolute()) {
            fprintf(stderr, "net-retry: --work necesita una ruta completa\n");
            return 2;
        }
        base = QDir(work).filePath(QStringLiteral("net-retry-%1")
                                       .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"))));
    } else {
        base = temp.path();
    }
    if (!QDir().mkpath(base)) {
        fprintf(stderr, "net-retry: no se pudo crear %s\n", qPrintable(base));
        return 2;
    }
    base = QFileInfo(base).canonicalFilePath();
    fprintf(stdout, "net-retry: carpeta %s\n", qPrintable(QDir::toNativeSeparators(base)));

    // Ni el proxy del sistema: todo va directo a 127.0.0.1.
    QNetworkProxyFactory::setUseSystemConfiguration(false);
    const QString previousSandbox = AutomatedRun::sandbox();
    AutomatedRun::setSandbox(base);
    const QtMessageHandler previousHandler = qInstallMessageHandler(captureLog);

    Probe probe;
    Watcher watcher;
    // Esperas cortas: los intentos y su orden son los mismos que con 5 s.
    LgaNetRetry::delayMsOverrideForTests() = 60;
    testUpdateCheck(probe, watcher, base);
    testUpdateDownload(probe, watcher, base);
#ifdef Q_OS_WIN
    testDictation(probe, base);
#else
    fprintf(stdout, "-    dictado: omitido (solo Windows)\n");
#endif
    LgaNetRetry::delayMsOverrideForTests() = -1;
    probe.check(LgaNetRetry::delayMs() == 5000 && LgaNetRetry::kRetries == 3,
                QStringLiteral("fuera de la prueba la espera vuelve a 5 s y son 3 reintentos"));

    qInstallMessageHandler(previousHandler);
    AutomatedRun::setSandbox(previousSandbox);
    fprintf(stdout, "net-retry: %d/%d comprobaciones, %d fallas\n%s\n", probe.checks - probe.failures, probe.checks,
            probe.failures, probe.failures ? "net-retry FALLO" : "net-retry ok");
    return probe.failures ? 1 : 0;
}
