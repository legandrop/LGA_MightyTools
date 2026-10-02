#ifndef MIGHTYTOOLS_SINGLEINSTANCE_H
#define MIGHTYTOOLS_SINGLEINSTANCE_H

#include <QObject>
#include <QString>

class QLocalServer;

// Segunda copia sin argumentos (plan 4.5): en vez de salir muda, le pide a la residente que muestre
// su ventana por un QLocalSocket y sale. El nombre del canal es por usuario: dos sesiones de
// Windows no se hablan entre si.
namespace SingleInstance {

QString serverName();
// Lo llama la segunda copia. True si la residente recibio el pedido.
bool askResidentToShow(int timeoutMs = 1500);
// --quit (lo usa instalador.bat antes de instalar): le pide a la residente que salga como desde
// «Quit» de la bandeja, asi saca su icono de la bandeja; un cierre forzado lo deja como fantasma.
// False si no hay residente o no recibio el pedido.
bool askResidentToQuit(int timeoutMs = 1500);

} // namespace SingleInstance

// El lado de la residente: escucha el canal y avisa showRequested() o quitRequested() por cada pedido.
class SingleInstanceServer : public QObject
{
    Q_OBJECT

public:
    explicit SingleInstanceServer(QObject *parent = nullptr);
    bool listening() const;

signals:
    void showRequested();
    void quitRequested();

private:
    QLocalServer *m_server = nullptr;
};

#endif // MIGHTYTOOLS_SINGLEINSTANCE_H
