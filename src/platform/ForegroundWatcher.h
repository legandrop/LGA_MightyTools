#ifndef MIGHTYTOOLS_FOREGROUNDWATCHER_H
#define MIGHTYTOOLS_FOREGROUNDWATCHER_H

#include <QObject>
#include <QString>

#include <memory>

// Observador de "que ventana esta al frente", servicio COMPARTIDO del host (plan 4.4): una sola
// instancia por proceso, que ModuleHost crea con el primer modulo prendido que la pide
// (ModuleContext::foreground()) y destruye cuando ya ninguno la usa (refcount, igual que el
// inyector). Hoy la usa Folder Switch; Nuke Shortcuts todavia tiene su propio hook en
// NukeWatcherWin.cpp con un `g_instance` sin tocar, pendiente de migrar aca.
//
// A diferencia de NukeWatcherWin (un `g_instance` global que se pisa si se instancia dos veces), esta
// clase admite VARIAS instancias vivas a la vez sin punteros estaticos por instancia: el callback de
// Windows resuelve la instancia dueña por el handle del hook en un registro compartido (ver
// ForegroundWatcherWin.cpp). Eso es lo que permite que hoy haya una sola instancia repartida entre
// los modulos que la piden (el caso real) y que el self-test pueda construir la suya propia sin
// pisar la del host.
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
