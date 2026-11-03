#ifndef MIGHTYTOOLS_INSTALLERCLEANUP_H
#define MIGHTYTOOLS_INSTALLERCLEANUP_H

#include "modules/diskspace/cleanup/InstallerOrphans.h"

#include <QList>
#include <QObject>
#include <QStringList>

class QLocalServer;
class QLocalSocket;
class QTimer;
class QWinEventNotifier;

// Solo Windows (win/InstallerCleanupWin.cpp). Las copias huerfanas de %WINDIR%\Installer solo se
// pueden mover con permiso de administrador: lo hace un paso elevado, el mismo exe lanzado con
// --installer-cleanup, una sola vez por limpieza (un solo cartel de UAC). El resto de la app sigue sin
// elevar.
//
// El paso elevado no confia en lo que le mandan: recibe solo NOMBRES de archivo, arma la ruta con la
// carpeta de instaladores que el mismo resuelve, vuelve a verificar cada uno con la consulta de TODOS los
// usuarios (completa o nada, InstallerOrphans::reverify) y lo manda a la Papelera; si la Papelera falla,
// no borra. Contesta un resultado por archivo.
//
// Como el ayudante de Window Layouts (D-61): la app abre un pipe con nombre al azar, el paso elevado se
// conecta y cada lado verifica el proceso del otro. Sin ventanas ni foco: el paso elevado es una
// QCoreApplication sin interfaz. En una corrida automatizada no se lanza nunca.
class InstallerCleanup : public QObject
{
    Q_OBJECT

public:
    struct Outcome
    {
        bool denied = false; ///< se cancelo el pedido de administrador: no se toco nada
        QString error;       ///< el paso elevado no termino (no arranco, no respondio, se corto)
        bool finished = false; ///< el paso elevado llego al final de la lista
        QList<InstallerOrphans::FileResult> files;
    };

    explicit InstallerCleanup(QObject *parent = nullptr);
    ~InstallerCleanup() override;

    // Lanza el paso elevado sin frenar la UI (el cartel de UAC se contesta en otro hilo). `owner`: la
    // ventana duena del cartel. false si no se pudo ni preparar (y outcome() dice por que).
    bool start(const QStringList &names, quintptr owner);
    // Deja de esperar: corta el pipe y el paso elevado sale antes del proximo archivo.
    void cancel();
    bool isRunning() const { return m_running; }
    // Esperando que se conteste el pedido de administrador.
    bool isWaitingForPermission() const { return m_running && m_launching; }
    Outcome outcome() const { return m_outcome; }

    // Si ahora se puede lanzar el paso elevado: nunca en una corrida automatizada. start() lo mira antes
    // que nada; el self-test lo prueba sin lanzar nada.
    static bool mayLaunch();
    // El modo --installer-cleanup <pipe> <pid de la app>. Devuelve el codigo de salida.
    static int runHelper(int argc, char *argv[]);

signals:
    void finished();

private:
    void onLaunched(quint64 generation, void *process, quint32 code);
    void onNewConnection();
    void acceptPending();
    void onReadyRead();
    void finish(const QString &error);
    void release();

    QLocalServer *m_server = nullptr;
    QLocalSocket *m_socket = nullptr;
    QList<QLocalSocket *> m_pending;
    QTimer *m_timeout = nullptr;
    QWinEventNotifier *m_exitNotifier = nullptr;
    void *m_process = nullptr;
    quint32 m_processId = 0;
    quint64 m_generation = 0;
    bool m_launching = false;
    bool m_running = false;
    QStringList m_names;
    Outcome m_outcome;
};

#endif // MIGHTYTOOLS_INSTALLERCLEANUP_H
