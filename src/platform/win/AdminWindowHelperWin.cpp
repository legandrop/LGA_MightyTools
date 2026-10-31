#include "platform/AdminWindowHelper.h"

#include "core/AutomatedRun.h"
#include "core/I18n.h"
#include "platform/HotkeyService.h"
#include "platform/WindowPlacement.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QUuid>
#include <QVarLengthArray>
#include <QWinEventNotifier>

#include <windows.h>
#include <shellapi.h>

#include <cmath>
#include <string>
#include <thread>

namespace {

// Lo que puede tardar el ayudante en conectarse despues de aceptado el permiso.
constexpr int kConnectTimeoutMs = 15000;
constexpr int kHelperConnectMs = 5000;
// Mas que cualquier lista real de disposiciones; corta una lista armada para barrer el teclado.
constexpr int kMaxEntries = 64;
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

bool validArea(const QRectF &area)
{
    return std::isfinite(area.x()) && std::isfinite(area.y()) && std::isfinite(area.width())
        && std::isfinite(area.height()) && area.width() > 0 && area.height() > 0 && area.x() >= 0
        && area.y() >= 0 && area.x() + area.width() <= 1.0 + 1e-9 && area.y() + area.height() <= 1.0 + 1e-9;
}

QByteArray line(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
}

// La app la declara con QGuiApplication; el ayudante no la crea, asi que la pide a mano para que
// SetWindowPos y los monitores usen los mismos pixeles fisicos que la app.
void usePerMonitorDpi()
{
    using SetContext = BOOL(WINAPI *)(HANDLE);
    const auto set = reinterpret_cast<SetContext>(
        reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext")));
    if (set) {
        set(reinterpret_cast<HANDLE>(static_cast<LONG_PTR>(-4))); // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
    }
}

} // namespace

AdminWindowHelper::AdminWindowHelper(QObject *parent)
    : QObject(parent)
{
    m_connectTimeout = new QTimer(this);
    m_connectTimeout->setSingleShot(true);
    m_connectTimeout->setInterval(kConnectTimeoutMs);
    connect(m_connectTimeout, &QTimer::timeout, this, [this]() {
        release();
        fail(I18n::tr("The helper for administrator windows did not respond."));
    });
}

AdminWindowHelper::~AdminWindowHelper()
{
    release();
}

bool AdminWindowHelper::acceptsShortcut(const Shortcut &shortcut)
{
    return shortcut.isValid()
        && (shortcut.modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
}

bool AdminWindowHelper::start(QString *error, quintptr owner)
{
    if (error) error->clear();
    if (m_server) {
        return true;
    }
    // Segunda guarda, ademas de la del modulo: una corrida automatizada nunca lanza un proceso elevado.
    if (AutomatedRun::active()) {
        if (error) *error = I18n::tr("Could not start the helper for administrator windows.");
        return false;
    }
    const QString name = QStringLiteral("LGA_MightyTools_admin_%1_%2")
                             .arg(GetCurrentProcessId())
                             .arg(QUuid::createUuid().toString(QUuid::Id128));
    m_server = new QLocalServer(this);
    m_server->setMaxPendingConnections(1);
    if (!m_server->listen(name)) {
        qWarning() << "[AdminWindowHelper] No se pudo abrir el pipe:" << m_server->errorString();
        release();
        if (error) *error = I18n::tr("Could not start the helper for administrator windows.");
        return false;
    }
    connect(m_server, &QLocalServer::newConnection, this, &AdminWindowHelper::onNewConnection);

    const quint64 generation = ++m_generation;
    m_launching = true;
    const std::wstring exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath()).toStdWString();
    const std::wstring parameters = QStringLiteral("--admin-window-helper %1 %2")
                                        .arg(name)
                                        .arg(GetCurrentProcessId())
                                        .toStdWString();
    // ShellExecuteEx con "runas" no vuelve hasta que se contesta el cartel de permiso: en el hilo de
    // la UI congelaria la bandeja, los atajos y la instancia unica mientras el cartel espera.
    const QPointer<AdminWindowHelper> guard(this);
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
    return true;
}

void AdminWindowHelper::onLaunched(quint64 generation, void *process, quint32 code)
{
    // Un lanzamiento viejo: la opcion se apago (o se volvio a prender) mientras esperaba el permiso.
    if (generation != m_generation || !m_launching) {
        if (process) CloseHandle(static_cast<HANDLE>(process));
        return;
    }
    m_launching = false;
    if (!process) {
        qWarning() << "[AdminWindowHelper] No se lanzo el ayudante elevado, error" << code;
        release();
        fail(code == ERROR_CANCELLED
                 ? I18n::tr("Windows did not grant administrator permission, so administrator windows can't be moved.")
                 : I18n::tr("Could not start the helper for administrator windows."));
        return;
    }
    m_process = process;
    m_processId = GetProcessId(static_cast<HANDLE>(process));
    qInfo() << "[AdminWindowHelper] Ayudante elevado lanzado, PID" << m_processId;
    // Si el ayudante sale por su cuenta (rechazo una verificacion, se cayo) se sabe enseguida.
    m_exitNotifier = new QWinEventNotifier(static_cast<HANDLE>(process), this);
    connect(m_exitNotifier, &QWinEventNotifier::activated, this, [this]() {
        DWORD exitCode = 0;
        GetExitCodeProcess(static_cast<HANDLE>(m_process), &exitCode);
        qWarning() << "[AdminWindowHelper] El ayudante salio con codigo" << exitCode;
        release();
        fail(I18n::tr("The helper for administrator windows stopped."));
    });
    m_connectTimeout->start();
    acceptPending();
}

void AdminWindowHelper::stop()
{
    release();
}

void AdminWindowHelper::fail(const QString &error)
{
    emit failed(error);
}

void AdminWindowHelper::release()
{
    m_connectTimeout->stop();
    m_launching = false;
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
        // Cortar el pipe es la orden de salir para el ayudante.
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

void AdminWindowHelper::onNewConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QLocalSocket *socket = m_server->nextPendingConnection();
        // Que cerrar o borrar el servidor no se lleve la conexion ya aceptada.
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

void AdminWindowHelper::acceptPending()
{
    // Hasta que vuelve el cartel de permiso no se sabe el PID del ayudante: las conexiones esperan.
    if (!m_processId) {
        return;
    }
    while (!m_pending.isEmpty()) {
        QLocalSocket *socket = m_pending.takeFirst();
        ULONG client = 0;
        // Solo el proceso que lanzamos: cualquier otro que adivine el nombre queda afuera.
        if (m_socket || !GetNamedPipeClientProcessId(reinterpret_cast<HANDLE>(socket->socketDescriptor()), &client)
            || client != m_processId) {
            qWarning() << "[AdminWindowHelper] Conexion rechazada, PID" << client;
            socket->abort();
            socket->deleteLater();
            continue;
        }
        m_socket = socket;
        m_connectTimeout->stop();
        if (m_server) m_server->close();
        connect(m_socket, &QLocalSocket::readyRead, this, &AdminWindowHelper::onReadyRead);
        connect(m_socket, &QLocalSocket::disconnected, this, [this]() {
            qWarning() << "[AdminWindowHelper] El ayudante se desconecto";
            release();
            fail(I18n::tr("The helper for administrator windows stopped."));
        });
        qInfo() << "[AdminWindowHelper] Ayudante conectado";
        send(encodeEntries(m_entries, I18n::code(I18n::language())));
        onReadyRead();
    }
}

void AdminWindowHelper::onReadyRead()
{
    while (m_socket && m_socket->canReadLine()) {
        const QJsonObject object = QJsonDocument::fromJson(m_socket->readLine().trimmed()).object();
        if (object.contains(QStringLiteral("id"))) {
            emit actionFinished(object.value(QStringLiteral("id")).toInt(),
                                object.value(QStringLiteral("error")).toString());
        }
    }
}

void AdminWindowHelper::send(const QByteArray &data)
{
    if (!m_socket) {
        return;
    }
    m_socket->write(data);
    m_socket->flush();
}

void AdminWindowHelper::setEntries(const QList<Entry> &entries)
{
    m_entries = entries;
    send(encodeEntries(m_entries, I18n::code(I18n::language())));
}

bool AdminWindowHelper::run(int id)
{
    if (!m_socket) {
        return false;
    }
    send(line(QJsonObject{{QStringLiteral("run"), id}}));
    return true;
}

QByteArray AdminWindowHelper::encodeEntries(const QList<Entry> &entries, const QString &language)
{
    QJsonArray list;
    for (const Entry &entry : entries) {
        list.append(QJsonObject{{QStringLiteral("id"), entry.id},
                                {QStringLiteral("shortcut"), entry.shortcut.toPortableString()},
                                {QStringLiteral("minimize"), entry.minimize},
                                {QStringLiteral("area"), QJsonArray{entry.area.x(), entry.area.y(),
                                                                    entry.area.width(), entry.area.height()}}});
    }
    return line(QJsonObject{{QStringLiteral("lang"), language}, {QStringLiteral("layouts"), list}});
}

bool AdminWindowHelper::decodeEntries(const QByteArray &data, QList<Entry> *entries, QString *language)
{
    const QJsonDocument document = QJsonDocument::fromJson(data.trimmed());
    const QJsonObject object = document.object();
    const QJsonArray items = object.value(QStringLiteral("layouts")).toArray();
    if (!document.isObject() || !object.value(QStringLiteral("layouts")).isArray() || items.size() > kMaxEntries) {
        return false;
    }
    QList<Entry> decoded;
    QSet<int> ids;
    for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject();
        const QJsonArray area = item.value(QStringLiteral("area")).toArray();
        Entry entry;
        entry.id = item.value(QStringLiteral("id")).toInt();
        entry.shortcut = Shortcut::fromPortableString(item.value(QStringLiteral("shortcut")).toString());
        entry.minimize = item.value(QStringLiteral("minimize")).toBool();
        if (area.size() == 4) {
            entry.area = QRectF(area.at(0).toDouble(), area.at(1).toDouble(), area.at(2).toDouble(),
                                area.at(3).toDouble());
        }
        if (entry.id <= 0 || ids.contains(entry.id) || !acceptsShortcut(entry.shortcut)
            || (!entry.minimize && !validArea(entry.area))) {
            return false;
        }
        ids.insert(entry.id);
        decoded.append(entry);
    }
    if (entries) *entries = decoded;
    if (language) *language = object.value(QStringLiteral("lang")).toString();
    return true;
}

int AdminWindowHelper::runHelper(int argc, char *argv[])
{
    // Proceso elevado: sin plugins de Qt ni rutas de plugins que vengan del entorno del usuario.
    for (const char *name : {"QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QT_QPA_PLATFORM",
                             "QT_QPA_GENERIC_PLUGINS"}) {
        qunsetenv(name);
    }
    usePerMonitorDpi();
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    const int index = args.indexOf(QStringLiteral("--admin-window-helper"));
    bool parentOk = false;
    const DWORD parent = index >= 0 && index + 2 < args.size() ? args.at(index + 2).toULong(&parentOk) : 0;
    if (!parentOk || !parent) {
        return 2;
    }
    // Sin elevar no sirve de nada; y solo obedece a una copia de este mismo exe.
    const QString own = imagePath(GetCurrentProcessId());
    if (!WindowPlacement::currentProcessElevated() || own.isEmpty()
        || own.compare(imagePath(parent), Qt::CaseInsensitive) != 0) {
        return 3;
    }
    QLocalSocket socket;
    socket.connectToServer(args.at(index + 1));
    ULONG server = 0;
    if (!socket.waitForConnected(kHelperConnectMs)
        || !GetNamedPipeServerProcessId(reinterpret_cast<HANDLE>(socket.socketDescriptor()), &server)
        || server != parent) {
        return 4;
    }

    HotkeyService hotkeys;
    // Con una ventana comun al frente, la combinacion sigue al hook de la app como si esto no existiera.
    hotkeys.setPriorityFilter([]() { return WindowPlacement::foregroundIsElevated(); });
    QHash<int, Entry> entries;
    // Desde el hook propio solo actua sobre ventanas elevadas. Un {"run"} de la app llega cuando la app
    // no pudo leer la elevacion de la ventana: ahi actua sobre cualquier ventana comun, que la app
    // tambien podria mover, y siempre contesta.
    const auto act = [&](int id, bool fromHook) {
        const auto it = entries.constFind(id);
        if (fromHook && (it == entries.cend() || !WindowPlacement::foregroundIsElevated())) {
            return;
        }
        QString error;
        const bool ok = it != entries.cend()
            && (it->minimize ? WindowPlacement::minimizeForeground(&error)
                             : WindowPlacement::moveForeground(it->area, &error));
        if (!ok && error.isEmpty()) error = I18n::tr("This window could not be moved or minimized.");
        socket.write(line(QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("error"), ok ? QString() : error}}));
        socket.flush();
    };
    QObject::connect(&hotkeys, &HotkeyService::activated, &app, [&](int id) { act(id, true); });
    const auto readMessages = [&]() {
        while (socket.canReadLine()) {
            const QByteArray data = socket.readLine();
            const QJsonObject object = QJsonDocument::fromJson(data.trimmed()).object();
            if (object.contains(QStringLiteral("run"))) {
                act(object.value(QStringLiteral("run")).toInt(), false);
                continue;
            }
            QList<Entry> decoded;
            QString language;
            if (!decodeEntries(data, &decoded, &language)) {
                continue;
            }
            I18n::setLanguage(I18n::fromCode(language));
            hotkeys.unregisterAll();
            entries.clear();
            for (const Entry &entry : decoded) {
                if (hotkeys.registerPriorityHotkey(entry.id, entry.shortcut)) entries.insert(entry.id, entry);
            }
        }
    };
    QObject::connect(&socket, &QLocalSocket::readyRead, &app, readMessages);
    // Lo que la app mando antes de conectar la senal ya esta en el buffer.
    QTimer::singleShot(0, &app, readMessages);
    // La app se cerro, se cayo o apago la opcion: el ayudante no sigue solo.
    QObject::connect(&socket, &QLocalSocket::disconnected, &app, &QCoreApplication::quit);
    QObject::connect(&socket, &QLocalSocket::errorOccurred, &app, &QCoreApplication::quit);
    if (socket.state() != QLocalSocket::ConnectedState) {
        return 4;
    }
    return app.exec();
}
