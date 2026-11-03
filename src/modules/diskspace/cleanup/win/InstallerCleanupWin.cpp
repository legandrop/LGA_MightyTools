#include "modules/diskspace/cleanup/InstallerCleanup.h"

#include "core/AutomatedRun.h"
#include "core/I18n.h"
#include "platform/FileSystemOps.h"
#include "platform/InstallerPackages.h"
#include "platform/RecycleBin.h"
#include "platform/WindowPlacement.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QTimer>
#include <QUuid>
#include <QVarLengthArray>
#include <QWinEventNotifier>

#include <windows.h>
#include <shellapi.h>

#include <string>
#include <thread>

namespace {

// Lo que puede tardar el paso elevado en conectarse despues de aceptado el permiso.
constexpr int kConnectTimeoutMs = 20000;
// Sin noticias del paso elevado por este tiempo (un archivo, o el pedido), se lo da por perdido.
constexpr int kQuietTimeoutMs = 120000;
constexpr int kHelperConnectMs = 5000;
constexpr int kHelperRequestMs = 10000;
constexpr int kMaxPending = 4;

QString imagePath(DWORD pid)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) {
        return {};
    }
    QVarLengthArray<wchar_t, 512> buffer(32768);
    DWORD length = DWORD(buffer.size());
    const bool ok = QueryFullProcessImageNameW(process, 0, buffer.data(), &length);
    CloseHandle(process);
    return ok ? QString::fromWCharArray(buffer.data(), int(length)) : QString();
}

bool writeLine(QLocalSocket &socket, const QByteArray &data)
{
    if (socket.state() != QLocalSocket::ConnectedState) {
        return false;
    }
    socket.write(data);
    socket.flush();
    return socket.waitForBytesWritten(5000) || socket.bytesToWrite() == 0;
}

} // namespace

InstallerCleanup::InstallerCleanup(QObject *parent)
    : QObject(parent)
{
    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, [this]() {
        qWarning() << "[InstallerCleanup] El paso elevado no respondio a tiempo";
        finish(QStringLiteral("timeout"));
    });
}

InstallerCleanup::~InstallerCleanup()
{
    release();
}

bool InstallerCleanup::mayLaunch()
{
    return !AutomatedRun::active();
}

bool InstallerCleanup::start(const QStringList &names, quintptr owner)
{
    if (m_running) {
        return false;
    }
    m_outcome = Outcome();
    m_names = names;
    // Una corrida automatizada nunca lanza un proceso elevado ni borra.
    if (!mayLaunch() || names.isEmpty() || names.size() > InstallerOrphans::kMaxRequest) {
        m_outcome.error = QStringLiteral("not started");
        return false;
    }
    for (const QString &name : names) {
        if (!InstallerOrphans::acceptedName(name)) {
            m_outcome.error = QStringLiteral("not started");
            return false;
        }
    }
    const QString pipe = QStringLiteral("LGA_MightyTools_installers_%1_%2")
                             .arg(GetCurrentProcessId())
                             .arg(QUuid::createUuid().toString(QUuid::Id128));
    m_server = new QLocalServer(this);
    m_server->setMaxPendingConnections(1);
    if (!m_server->listen(pipe)) {
        qWarning() << "[InstallerCleanup] No se pudo abrir el pipe:" << m_server->errorString();
        release();
        m_outcome.error = QStringLiteral("pipe");
        return false;
    }
    connect(m_server, &QLocalServer::newConnection, this, &InstallerCleanup::onNewConnection);

    const quint64 generation = ++m_generation;
    m_launching = true;
    m_running = true;
    const std::wstring exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath()).toStdWString();
    const std::wstring parameters = QStringLiteral("--installer-cleanup %1 %2").arg(pipe).arg(GetCurrentProcessId()).toStdWString();
    // ShellExecuteEx con "runas" no vuelve hasta que se contesta el cartel de UAC: en el hilo de la UI
    // congelaria la ventana mientras espera.
    const QPointer<InstallerCleanup> guard(this);
    try {
        std::thread([guard, generation, exe, parameters, owner]() {
            const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            SHELLEXECUTEINFOW info{};
            info.cbSize = sizeof(info);
            info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
            info.hwnd = reinterpret_cast<HWND>(owner);
            info.lpVerb = L"runas";
            info.lpFile = exe.c_str();
            info.lpParameters = parameters.c_str();
            info.nShow = SW_HIDE;
            const bool ok = ShellExecuteExW(&info) && info.hProcess;
            const DWORD code = ok ? 0 : GetLastError();
            if (SUCCEEDED(com)) CoUninitialize();
            void *process = info.hProcess;
            QCoreApplication *app = QCoreApplication::instance();
            if (!app || !QMetaObject::invokeMethod(app, [guard, generation, process, code]() {
                    if (guard) {
                        guard->onLaunched(generation, process, code);
                    } else if (process) {
                        CloseHandle(process);
                    }
                }, Qt::QueuedConnection)) {
                if (process) CloseHandle(process);
            }
        }).detach();
    } catch (...) {
        release();
        m_running = false;
        m_outcome.error = QStringLiteral("thread");
        return false;
    }
    return true;
}

void InstallerCleanup::onLaunched(quint64 generation, void *process, quint32 code)
{
    // Un lanzamiento viejo: se cancelo mientras esperaba el permiso.
    if (generation != m_generation || !m_launching) {
        if (process) CloseHandle(static_cast<HANDLE>(process));
        return;
    }
    m_launching = false;
    if (!process) {
        qWarning() << "[InstallerCleanup] No se lanzo el paso elevado, error" << code;
        m_outcome.denied = code == ERROR_CANCELLED;
        finish(m_outcome.denied ? QString() : QStringLiteral("launch %1").arg(code));
        return;
    }
    m_process = process;
    m_processId = GetProcessId(static_cast<HANDLE>(process));
    qInfo() << "[InstallerCleanup] Paso elevado lanzado, PID" << m_processId;
    // Si sale antes de terminar (rechazo una verificacion, se cayo) se sabe enseguida.
    m_exitNotifier = new QWinEventNotifier(static_cast<HANDLE>(process), this);
    connect(m_exitNotifier, &QWinEventNotifier::activated, this, [this]() {
        // Un proceso que salio queda senalado para siempre: una sola vez alcanza.
        m_exitNotifier->setEnabled(false);
        DWORD exitCode = 0;
        GetExitCodeProcess(static_cast<HANDLE>(m_process), &exitCode);
        // Lo que ya escribio sigue en el pipe aunque el proceso se haya ido: el pipe lo entrega y recien
        // despues avisa que se corto (disconnected). Por si ese aviso no llega, un plazo corto.
        if (m_socket) {
            m_timeout->start(3000);
            return;
        }
        if (m_running) {
            qWarning() << "[InstallerCleanup] El paso elevado salio con codigo" << exitCode;
            finish(m_outcome.finished ? QString() : QStringLiteral("exit %1").arg(exitCode));
        }
    });
    m_timeout->start(kConnectTimeoutMs);
    acceptPending();
}

void InstallerCleanup::onNewConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QLocalSocket *socket = m_server->nextPendingConnection();
        socket->setParent(this);
        if (m_socket || m_pending.size() >= kMaxPending) {
            socket->abort();
            socket->deleteLater();
            continue;
        }
        m_pending.append(socket);
    }
    acceptPending();
}

void InstallerCleanup::acceptPending()
{
    // Hasta que vuelve el cartel de UAC no se sabe el PID del paso elevado: las conexiones esperan.
    if (!m_processId) {
        return;
    }
    while (!m_pending.isEmpty()) {
        QLocalSocket *socket = m_pending.takeFirst();
        ULONG client = 0;
        // Solo el proceso que lanzamos: cualquier otro que adivine el nombre queda afuera.
        if (m_socket || !GetNamedPipeClientProcessId(reinterpret_cast<HANDLE>(socket->socketDescriptor()), &client)
            || client != m_processId) {
            qWarning() << "[InstallerCleanup] Conexion rechazada, PID" << client;
            socket->abort();
            socket->deleteLater();
            continue;
        }
        m_socket = socket;
        if (m_server) m_server->close();
        connect(m_socket, &QLocalSocket::readyRead, this, &InstallerCleanup::onReadyRead);
        connect(m_socket, &QLocalSocket::disconnected, this, [this]() {
            onReadyRead();
            if (m_running) {
                finish(m_outcome.finished ? QString() : QStringLiteral("disconnected"));
            }
        });
        m_socket->write(InstallerOrphans::encodeRequest(m_names));
        m_socket->flush();
        m_timeout->start(kQuietTimeoutMs);
        onReadyRead();
    }
}

void InstallerCleanup::onReadyRead()
{
    while (m_socket && m_socket->canReadLine()) {
        InstallerOrphans::FileResult result;
        bool done = false;
        if (!InstallerOrphans::decodeReply(m_socket->readLine(), &result, &done)) {
            continue;
        }
        if (done) {
            m_outcome.finished = true;
            finish(QString());
            return;
        }
        // Solo lo que se pidio.
        if (m_names.contains(result.name, Qt::CaseInsensitive)) {
            m_outcome.files.append(result);
        }
        m_timeout->start(kQuietTimeoutMs);
    }
}

void InstallerCleanup::cancel()
{
    if (!m_running) {
        return;
    }
    finish(QStringLiteral("cancelled"));
}

void InstallerCleanup::finish(const QString &error)
{
    if (!m_running) {
        return;
    }
    if (!error.isEmpty() && m_outcome.error.isEmpty()) {
        m_outcome.error = error;
    }
    m_running = false;
    release();
    emit finished();
}

void InstallerCleanup::release()
{
    m_timeout->stop();
    m_launching = false;
    // Un lanzamiento que todavia espera el permiso queda viejo: su resultado se descarta.
    ++m_generation;
    if (m_exitNotifier) {
        // deleteLater: release() tambien corre desde la senal del propio notifier.
        m_exitNotifier->setEnabled(false);
        m_exitNotifier->deleteLater();
        m_exitNotifier = nullptr;
    }
    for (QLocalSocket *socket : std::as_const(m_pending)) {
        socket->abort();
        socket->deleteLater();
    }
    m_pending.clear();
    if (m_socket) {
        disconnect(m_socket, nullptr, this, nullptr);
        // Cortar el pipe es la orden de parar para el paso elevado.
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    if (m_server) {
        disconnect(m_server, nullptr, this, nullptr);
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
    if (m_process) {
        CloseHandle(static_cast<HANDLE>(m_process));
        m_process = nullptr;
    }
    m_processId = 0;
}

int InstallerCleanup::runHelper(int argc, char *argv[])
{
    // Proceso elevado: sin plugins de Qt ni rutas de plugins que vengan del entorno del usuario.
    for (const char *name : {"QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QT_QPA_PLATFORM", "QT_QPA_GENERIC_PLUGINS"}) {
        qunsetenv(name);
    }
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    const int index = args.indexOf(QStringLiteral("--installer-cleanup"));
    bool parentOk = false;
    const DWORD parent = index >= 0 && index + 2 < args.size() ? args.at(index + 2).toULong(&parentOk) : 0;
    if (!parentOk || !parent) {
        return 2;
    }
    // Sin elevar no sirve de nada; y solo obedece a una copia de este mismo exe.
    const QString own = imagePath(GetCurrentProcessId());
    if (!WindowPlacement::currentProcessElevated() || own.isEmpty() || own.compare(imagePath(parent), Qt::CaseInsensitive) != 0) {
        return 3;
    }
    QLocalSocket socket;
    socket.connectToServer(args.at(index + 1));
    ULONG server = 0;
    if (!socket.waitForConnected(kHelperConnectMs)
        || !GetNamedPipeServerProcessId(reinterpret_cast<HANDLE>(socket.socketDescriptor()), &server) || server != parent) {
        return 4;
    }
    // El pedido: una linea con los nombres.
    while (!socket.canReadLine()) {
        if (socket.state() != QLocalSocket::ConnectedState || !socket.waitForReadyRead(kHelperRequestMs)) {
            return 5;
        }
    }
    QStringList names;
    if (!InstallerOrphans::decodeRequest(socket.readLine(), &names)) {
        return 5;
    }

    // Todo se vuelve a verificar aca, con la consulta de todos los usuarios.
    const InstallerPackages::Survey survey = InstallerPackages::survey(InstallerPackages::Scope::AllUsers);
    const QList<InstallerOrphans::Check> checks = InstallerOrphans::reverify(survey, names, QDateTime::currentSecsSinceEpoch());
    for (const InstallerOrphans::Check &check : checks) {
        // La app se fue o pidio parar (Stop corta el pipe): lo que falta no se toca. El pipe se mira
        // de nuevo antes de cada archivo, para no mover uno mas que la app ya no va a contar.
        socket.waitForReadyRead(0);
        if (socket.state() != QLocalSocket::ConnectedState) {
            return 6;
        }
        InstallerOrphans::FileResult result;
        result.name = check.name;
        result.result = InstallerOrphans::resultFor(check.reason);
        if (check.reason == InstallerOrphans::Reason::Ok) {
            const QString path = check.file.path;
            // Lo ultimo antes de moverlo: ninguna instalacion empezo ni quedo a medias, y el archivo es
            // el mismo que se verifico (un archivo comun, no un enlace, con la misma identidad).
            if (InstallerPackages::installBusy(survey.installerDir)) {
                result.result = InstallerOrphans::Result::Busy;
            } else if (FileSystemOps::kind(path) != FileSystemOps::Kind::File || !(FileSystemOps::identity(path) == check.file.id)) {
                result.result = InstallerOrphans::Result::NotAllowed;
            } else {
                QString error;
                // Si la Papelera no lo acepta, se queda: nunca se pasa a un borrado definitivo. Y si dice
                // que si pero el archivo sigue ahi (la operacion se aborto), tampoco cuenta como movido.
                if (RecycleBin::moveToTrash(path, &error) && FileSystemOps::kind(path) == FileSystemOps::Kind::Missing) {
                    result.result = InstallerOrphans::Result::Trashed;
                    result.bytes = check.file.bytes;
                } else {
                    result.result = InstallerOrphans::Result::TrashFailed;
                }
            }
        }
        if (!writeLine(socket, InstallerOrphans::encodeResult(result))) {
            return 6;
        }
    }
    writeLine(socket, InstallerOrphans::encodeDone());
    socket.disconnectFromServer();
    return 0;
}
