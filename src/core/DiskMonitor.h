#ifndef MIGHTYTOOLS_DISKMONITOR_H
#define MIGHTYTOOLS_DISKMONITOR_H

#include "core/DiskSpace.h"

#include <QDateTime>
#include <QHash>
#include <QObject>

#include <functional>

class AppState;
class QTimer;

// Chequeo periodico del espacio libre. Cada AppState::diskCheckMinutes lee los discos vigilados,
// deja la lectura en AppState (la tarjeta y el menu de la bandeja la muestran) y emite lowSpace()
// cuando corresponde avisar (DiskSpace::shouldNotify).
//
// No lee el sistema por su cuenta: recibe las funciones de lectura. TrayController le pasa las de
// platform/LocalDrives; el self-test, discos falsos y un reloj propio. Asi la logica del aviso se
// prueba entera sin tocar un disco.
class DiskMonitor : public QObject
{
    Q_OBJECT

public:
    struct Sources
    {
        // Todos los discos locales (fijos y removibles) con su lectura. Para el menu "Add drive".
        std::function<QList<DriveInfo>()> listAll;
        // Un disco puntual. false si no esta enchufado o no se pudo leer.
        std::function<bool(const QString &root, DriveInfo *drive)> query;
        // Hora actual (el self-test la adelanta a mano). Vacia = QDateTime::currentDateTime.
        std::function<QDateTime()> now;
    };

    DiskMonitor(AppState *state, Sources sources, QObject *parent = nullptr);

    // Arranca el timer. El primer chequeo que puede avisar llega a los `firstCheckDelayMs`: al
    // iniciar con la sesion no se suma un aviso al arranque de Windows.
    void start(int firstCheckDelayMs);

    // Lee los discos vigilados ya. Con mayNotify = false solo actualiza la lectura (abrir Settings,
    // cambiar un umbral): el aviso queda para el chequeo periodico.
    void checkNow(bool mayNotify);
    // Lista todos los discos locales (abre el menu "Add drive").
    void refreshAll();

signals:
    void lowSpace(const DriveInfo &drive, const DiskWatch &watch);

private:
    void onStateChanged();
    QDateTime now() const;

    AppState *m_state = nullptr;
    Sources m_sources;
    QTimer *m_timer = nullptr;
    int m_timerMinutes = 0;
    QHash<QString, DiskSpace::AlertState> m_alerts;
};

#endif // MIGHTYTOOLS_DISKMONITOR_H
