#ifndef MIGHTYTOOLS_FOREGROUNDWATCHER_H
#define MIGHTYTOOLS_FOREGROUNDWATCHER_H

#include <QObject>
#include <QString>

#include <memory>

// Observador de "que ventana esta al frente", pensado para terminar siendo un SERVICIO COMPARTIDO
// del host (plan 4.4): un solo hook de sistema para todos los modulos que lo necesiten (hoy Folder
// Switch; mas adelante Nuke Shortcuts, que hoy tiene el suyo propio en NukeWatcherWin.cpp con un
// `g_instance` sin tocar). Mientras no exista ese servicio, cada modulo crea y destruye su propia
// instancia en start()/stop().
//
// A diferencia de NukeWatcherWin (un `g_instance` global que se pisa si se instancia dos veces), esta
// clase admite VARIAS instancias vivas a la vez sin punteros estaticos por instancia: el callback de
// Windows resuelve la instancia dueña por el handle del hook en un registro compartido (ver
// ForegroundWatcherWin.cpp). Asi el dia de mañana el host puede tener una sola instancia repartida
// entre modulos, o (como ahora) cada modulo la suya, sin que se pisen entre si.
//
// `active = false` (usado en toda corrida automatizada: --self-test, --ui-shot, --simulate-action, la
// medicion de consumo) crea el objeto pero NO instala ningun hook ni consulta el sistema: sirve para
// ejercitar el ciclo de vida (crear en start(), destruir en stop()) sin tocar nada real, igual que
// InputInjector(dryRun).
//
// Coordenadas y HWND en formato NATIVO (ver NukeWatcher.h): el hwnd se entrega como quintptr, listo
// para reinterpret_cast<HWND>. Una implementacion por plataforma:
//  - Windows (platform/win/ForegroundWatcherWin.cpp): SetWinEventHook(EVENT_SYSTEM_FOREGROUND).
//  - macOS: sin implementar todavia (Folder Switch es solo Windows). El header queda listo para
//    cuando Nuke Shortcuts en mac lo necesite (NSWorkspace, app activa).
class ForegroundWatcher : public QObject
{
    Q_OBJECT

public:
    explicit ForegroundWatcher(bool active, QObject *parent = nullptr);
    ~ForegroundWatcher() override;

    // Lo ultimo que aviso el sistema (0 / vacio si `active` era false, o todavia no llego el primer
    // aviso).
    quintptr foregroundHwnd() const { return m_hwnd; }
    quint32 foregroundPid() const { return m_pid; }
    QString foregroundExeName() const { return m_exeName; }

signals:
    // hwnd: HWND nativo como quintptr. pid: PID del proceso dueño de esa ventana. exeName: nombre del
    // ejecutable ("explorer.exe", "Nuke15.1.exe"...), vacio si no se pudo leer. Se emite en cola
    // (llega unos milisegundos despues del cambio real), igual que NukeWatcher.
    void foregroundChanged(quintptr hwnd, quint32 pid, const QString &exeName);

private:
    void setForeground(quintptr hwnd, quint32 pid, const QString &exeName);

    struct Private;
    std::unique_ptr<Private> d;
    quintptr m_hwnd = 0;
    quint32 m_pid = 0;
    QString m_exeName;
};

#endif // MIGHTYTOOLS_FOREGROUNDWATCHER_H
