#ifndef MIGHTYTOOLS_ADMINWINDOWHELPER_H
#define MIGHTYTOOLS_ADMINWINDOWHELPER_H

#include "core/Shortcut.h"

#include <QList>
#include <QObject>
#include <QRectF>

class QLocalServer;
class QLocalSocket;
class QTimer;
class QWinEventNotifier;

// Solo Windows (platform/win/AdminWindowHelperWin.cpp). Un proceso sin elevar no recibe atajos ni
// puede mover la ventana de una app abierta como administrador (UIPI): con el Administrador de
// tareas al frente, el hook de la app ni se entera. Este ayudante es el mismo exe lanzado elevado
// con --admin-window-helper: escucha los mismos atajos y SOLO actua cuando la ventana del frente es
// de administrador; con cualquier otra deja pasar la combinacion. El resto de la app sigue sin
// elevar, asi que lo que abre (navegador, Nuke, carpetas) nunca hereda el permiso.
//
// La app abre un pipe local con nombre al azar y el ayudante se conecta: cada lado verifica el
// proceso del otro. Si la app se cierra o se cae, el pipe se corta y el ayudante sale solo.
class AdminWindowHelper : public QObject
{
    Q_OBJECT

public:
    struct Entry
    {
        int id = 0;
        Shortcut shortcut;
        bool minimize = false;
        QRectF area;
    };

    explicit AdminWindowHelper(QObject *parent = nullptr);
    ~AdminWindowHelper() override;

    // Lanza el ayudante elevado sin frenar la UI: el cartel de permiso de Windows se contesta en
    // otro hilo y un rechazo llega por failed(). owner es la ventana duena del cartel (0: ninguna).
    // False si ni siquiera se pudo preparar el lanzamiento.
    bool start(QString *error = nullptr, quintptr owner = 0);
    void stop();
    bool isConnected() const { return m_socket != nullptr; }
    // Los atajos que escucha el ayudante. Se reenvian al conectarse y en cada cambio.
    void setEntries(const QList<Entry> &entries);
    // La app recibio el atajo con una ventana de administrador al frente: que la mueva el ayudante.
    bool run(int id);

    // Solo combinaciones con Ctrl, Alt o Win: el ayudante nunca intercepta teclas sueltas.
    static bool acceptsShortcut(const Shortcut &shortcut);
    // Una linea JSON por mensaje. Expuestas para el self-test.
    static QByteArray encodeEntries(const QList<Entry> &entries, const QString &language);
    static bool decodeEntries(const QByteArray &line, QList<Entry> *entries, QString *language);

    // El modo --admin-window-helper <pipe> <pid de la app>. Devuelve el codigo de salida.
    static int runHelper(int argc, char *argv[]);

signals:
    // El ayudante resolvio un atajo: error vacio si la ventana quedo donde se pidio.
    void actionFinished(int id, const QString &error);
    // El ayudante no arranco, no respondio o se cerro solo.
    void failed(const QString &error);

private:
    void onLaunched(quint64 generation, void *process, quint32 code);
    void onNewConnection();
    void acceptPending();
    void onReadyRead();
    void send(const QByteArray &line);
    void release();
    void fail(const QString &error);

    QLocalServer *m_server = nullptr;
    QLocalSocket *m_socket = nullptr;
    QList<QLocalSocket *> m_pending;
    QTimer *m_connectTimeout = nullptr;
    QWinEventNotifier *m_exitNotifier = nullptr;
    void *m_process = nullptr;
    quint32 m_processId = 0;
    quint64 m_generation = 0;
    bool m_launching = false;
    QList<Entry> m_entries;
};

#endif // MIGHTYTOOLS_ADMINWINDOWHELPER_H
