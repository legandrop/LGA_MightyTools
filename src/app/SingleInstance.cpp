#include "app/SingleInstance.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>

namespace {

constexpr char kShowCommand[] = "show\n";
constexpr char kQuitCommand[] = "quit\n";

bool sendToResident(const char *command, int timeoutMs)
{
    QLocalSocket socket;
    socket.connectToServer(SingleInstance::serverName());
    if (!socket.waitForConnected(timeoutMs)) {
        qWarning() << "[SingleInstance] La residente no contesto:" << socket.errorString();
        return false;
    }
    socket.write(command);
    const bool written = socket.waitForBytesWritten(timeoutMs);
    socket.disconnectFromServer();
    return written;
}

} // namespace

namespace SingleInstance {

QString serverName()
{
    // El usuario va hasheado: el nombre del canal queda corto y sin caracteres raros.
    const QByteArray user = QDir::homePath().toUtf8();
    const QByteArray hash = QCryptographicHash::hash(user, QCryptographicHash::Sha1).toHex().left(12);
    return QStringLiteral("LGA_MightyTools-%1").arg(QString::fromLatin1(hash));
}

bool askResidentToShow(int timeoutMs)
{
    return sendToResident(kShowCommand, timeoutMs);
}

bool askResidentToQuit(int timeoutMs)
{
    return sendToResident(kQuitCommand, timeoutMs);
}

} // namespace SingleInstance

SingleInstanceServer::SingleInstanceServer(QObject *parent)
    : QObject(parent)
    , m_server(new QLocalServer(this))
{
    // Un canal viejo de una copia que se corto (en mac queda el archivo del socket) no deja escuchar.
    QLocalServer::removeServer(SingleInstance::serverName());
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server->listen(SingleInstance::serverName())) {
        qWarning() << "[SingleInstance] No se pudo escuchar el canal:" << m_server->errorString();
        return;
    }
    connect(m_server, &QLocalServer::newConnection, this, [this]() {
        while (QLocalSocket *socket = m_server->nextPendingConnection()) {
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QLocalSocket::readyRead, this, [this, socket]() {
                const QByteArray line = socket->readAll();
                if (line.startsWith("show")) {
                    qInfo() << "[SingleInstance] Otra copia pidio mostrar la ventana";
                    emit showRequested();
                } else if (line.startsWith("quit")) {
                    qInfo() << "[SingleInstance] Otra copia pidio salir (--quit)";
                    emit quitRequested();
                }
            });
        }
    });
}

bool SingleInstanceServer::listening() const
{
    return m_server->isListening();
}
