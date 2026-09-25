#ifndef MIGHTYTOOLS_OPENINNUKEX_NUKEOPENER_H
#define MIGHTYTOOLS_OPENINNUKEX_NUKEOPENER_H

#include <QObject>
#include <QString>
#include <QTcpSocket>
#include <QTimer>

#include <functional>

// Manda un .nk a un NukeX ya corriendo (Nuke Bridge, TCP a localhost:54325) o, si no contesta,
// lanza NukeX con la ruta preferida de nukeXpath.txt. Portado de
// `~/.nuke/LGA_OpenInNukeX/QtClient/src/nukeopener.{h,cpp}` (v1.83).
//
// Diferencia deliberada con el origen: el origen llama `QApplication::quit()` en cada salida
// (tiene sentido en una app que se lanza, hace su trabajo y muere en cada doble click). Ese modo
// corto SIGUE existiendo en Mighty Tools (plan 4.5), pero el mismo codigo tambien corre dentro de
// la copia residente en mac (D-05, D-09: "sin quit() en el camino de la app residente", plan
// seccion 9). Por eso NukeOpener nunca llama a quit(): informa que termino por el callback
// `finished(exitCode)` de ExternalRequest (app/Module.h), y quien maneja el modo corto de Windows
// es quien decide salir con ese codigo.
//
// NO llama a OpenInNukeXMessages::report() por errores silenciosos (conexion cerrada por el
// servidor, error de socket desconocido): el origen tampoco mostraba nada ahi (los showMessage()
// correspondientes estan comentados en nukeopener.cpp), solo lo dejaba en el log.
class NukeOpener : public QObject
{
    Q_OBJECT

public:
    struct Options
    {
        QString nkFilePath;                        ///< el .nk que se quiere abrir
        QString nukeXPathFile;                      ///< ruta a nukeXpath.txt (inyectable para tests)
        QString host = QStringLiteral("localhost");
        int port = 54325;
        bool showLaunchNotice = false;              ///< setting showLaunchNotice (apagado por defecto)
    };

    explicit NukeOpener(QObject *parent = nullptr);

    // Asincronico. Llama a `finished(exitCode)` EXACTAMENTE una vez (0 = ok, !=0 = error). Nunca
    // hay que llamarla dos veces sobre el mismo NukeOpener sin que haya terminado la anterior.
    void open(const Options &options, std::function<void(int)> finished);

    // Lanza NukeX con `--nukex <nkFilePath>`. Publica para el self-test y para el modulo apagado
    // (paso directo, D-05: lanza NukeX sin pasar por el bridge y sin ningun mensaje).
    static bool launchNukeXProcess(const QString &nukeExecutablePath, const QString &nkFilePath, QString *errorOut);

private slots:
    void onConnected();
    void onResponseReceived();
    void onErrorOccurred(QAbstractSocket::SocketError error);
    void onConnectTimeout();

private:
    void handleNoBridgeRunning();
    void finishOnce(int exitCode);
    void cleanupSocket();

    QTcpSocket *m_socket = nullptr;
    QTimer *m_connectTimeoutTimer = nullptr;
    Options m_options;
    std::function<void(int)> m_finished;
    bool m_done = false;
};

#endif // MIGHTYTOOLS_OPENINNUKEX_NUKEOPENER_H
