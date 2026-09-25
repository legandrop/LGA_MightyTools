#include "modules/openinnukex/NukeOpener.h"

#include "modules/openinnukex/NukeXPath.h"
#include "modules/openinnukex/OpenInNukeXMessages.h"

#include <QDir>
#include <QFile>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <QProcess>
#endif

NukeOpener::NukeOpener(QObject *parent)
    : QObject(parent)
{
    m_connectTimeoutTimer = new QTimer(this);
    m_connectTimeoutTimer->setSingleShot(true);
    connect(m_connectTimeoutTimer, &QTimer::timeout, this, &NukeOpener::onConnectTimeout);
}

void NukeOpener::open(const Options &options, std::function<void(int)> finished)
{
    m_options = options;
    m_finished = std::move(finished);
    m_done = false;

    if (!QFile::exists(options.nkFilePath)) {
        qWarning("[openInNukeX] El archivo no existe: %s", qUtf8Printable(options.nkFilePath));
        finishOnce(1);
        return;
    }

    m_socket = new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::connected, this, &NukeOpener::onConnected);
    connect(m_socket, &QAbstractSocket::errorOccurred, this, &NukeOpener::onErrorOccurred);

    // Backstop: si connectToHost tarda mas que esto sin conectar ni fallar, se trata como si no
    // hubiera bridge (mismos 4000 ms que el origen).
    m_connectTimeoutTimer->start(4000);
    m_socket->connectToHost(options.host, options.port);
}

void NukeOpener::onConnected()
{
    m_connectTimeoutTimer->stop();

    const QString normalizedPath = QDir::fromNativeSeparators(m_options.nkFilePath);
    const QString command = QStringLiteral("run_script||%1").arg(normalizedPath);
    m_socket->write(command.toUtf8());
    // Sin waitForBytesWritten()/waitForReadyRead(): esos crean un event loop anidado que puede
    // procesar un segundo pedido externo mientras este todavia cuelga del socket (el mismo bug
    // que documenta nukeopener.cpp del origen sobre un segundo QFileOpenEvent en mac).
    connect(m_socket, &QIODevice::readyRead, this, &NukeOpener::onResponseReceived);

    QTimer::singleShot(5000, this, [this]() {
        if (m_socket) {
            qInfo("[openInNukeX] Timeout esperando respuesta del bridge, se da por hecho");
            finishOnce(0);
        }
    });
}

void NukeOpener::onResponseReceived()
{
    if (!m_socket) {
        return;
    }
    const QByteArray response = m_socket->readAll();
    qInfo("[openInNukeX] Respuesta del bridge: %s", response.constData());
    finishOnce(0);
}

void NukeOpener::onErrorOccurred(QAbstractSocket::SocketError error)
{
    m_connectTimeoutTimer->stop();

    if (error == QAbstractSocket::ConnectionRefusedError) {
        handleNoBridgeRunning();
        return;
    }
    if (error == QAbstractSocket::RemoteHostClosedError) {
        // Igual que el origen: no es un error para el usuario, el bridge ya proceso el pedido y
        // cerro la conexion de su lado.
        finishOnce(0);
        return;
    }
    qInfo("[openInNukeX] Error de socket desconocido: %s", qUtf8Printable(m_socket ? m_socket->errorString() : QString()));
    finishOnce(0);
}

void NukeOpener::onConnectTimeout()
{
    handleNoBridgeRunning();
}

void NukeOpener::handleNoBridgeRunning()
{
    const QString nukeExecutablePath = NukeXPath::read(m_options.nukeXPathFile);

    if (nukeExecutablePath.isEmpty()) {
        OpenInNukeXMessages::report(OpenInNukeXMessages::nukeNotConfigured(), m_options.parentWidget, m_options.automatedRun);
        finishOnce(1);
        return;
    }
    if (!QFile::exists(nukeExecutablePath)) {
        OpenInNukeXMessages::report(OpenInNukeXMessages::nukeXPathGone(nukeExecutablePath), m_options.parentWidget,
                                    m_options.automatedRun);
        finishOnce(1);
        return;
    }

    QString errorDetail;
    if (!launchNukeXProcess(nukeExecutablePath, m_options.nkFilePath, &errorDetail)) {
        OpenInNukeXMessages::report(OpenInNukeXMessages::nukeXFailedToStart(errorDetail), m_options.parentWidget,
                                    m_options.automatedRun);
        finishOnce(1);
        return;
    }

    if (m_options.showLaunchNotice) {
        // Cartel "NukeX Launcher" con cuenta regresiva (inventario, seccion homonima): no modal,
        // se cierra solo a los 3 segundos. Igual que el origen (nukeopener.cpp: 3 s del cartel +
        // 1 s de margen antes de salir), NukeOpener no termina hasta que el cartel se cierra.
        OpenInNukeXMessages::showLaunchNotice(m_options.parentWidget, m_options.automatedRun,
                                              [this]() { finishOnce(0); });
        return;
    }
    finishOnce(0);
}

bool NukeOpener::launchNukeXProcess(const QString &nukeExecutablePath, const QString &nkFilePath, QString *errorOut)
{
#ifdef Q_OS_WIN
    STARTUPINFOW startupInfo = { sizeof(STARTUPINFOW) };
    PROCESS_INFORMATION processInfo{};

    const QString commandLine = QStringLiteral("\"%1\" --nukex \"%2\"").arg(nukeExecutablePath, nkFilePath);
    std::wstring wCommandLine = commandLine.toStdWString();

    const BOOL success = CreateProcessW(nullptr, wCommandLine.data(), nullptr, nullptr, FALSE,
                                        CREATE_NEW_CONSOLE, nullptr, nullptr, &startupInfo, &processInfo);
    if (success) {
        CloseHandle(processInfo.hProcess);
        CloseHandle(processInfo.hThread);
        return true;
    }
    if (errorOut) {
        *errorOut = QStringLiteral("Windows error %1").arg(GetLastError());
    }
    return false;
#else
    QProcess process;
    const bool started = process.startDetached(nukeExecutablePath, {QStringLiteral("--nukex"), nkFilePath});
    if (!started && errorOut) {
        *errorOut = process.errorString();
    }
    return started;
#endif
}

void NukeOpener::finishOnce(int exitCode)
{
    if (m_done) {
        return;
    }
    m_done = true;
    cleanupSocket();
    if (m_finished) {
        m_finished(exitCode);
    }
}

void NukeOpener::cleanupSocket()
{
    if (!m_socket) {
        return;
    }
    m_socket->close();
    m_socket->deleteLater();
    m_socket = nullptr;
}
