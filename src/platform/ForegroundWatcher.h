#ifndef MIGHTYTOOLS_FOREGROUNDWATCHER_H
#define MIGHTYTOOLS_FOREGROUNDWATCHER_H

#include <QObject>
#include <QString>

#include <memory>

// Servicio compartido "que app o ventana esta al frente" (plan 4.4): UN solo observador del
// sistema para todos los modulos que lo necesitan (Folder Switch y Nuke Shortcuts). Lo crea el host
// con el primer modulo que lo pide (ModuleContext::foreground(), refcount en
// ModuleHost::acquireForeground/releaseForeground) y lo destruye cuando lo suelta el ultimo: con los
// dos prendidos hay un solo hook; con los dos apagados, ninguno.
//
// Sin punteros estaticos por instancia: el callback de Windows resuelve la instancia duena por el
// handle de SU hook en un registro compartido (ForegroundWatcherWin.cpp), y en mac el observador de
// NSWorkspace captura su instancia en el bloque. installedHooks() cuenta los observadores del sistema
// instalados en el proceso (lo usan el self-test y la medicion).
//
// `active = false` (corridas automatizadas: --self-test, --ui-shot, --simulate-action) crea el objeto
// pero NO instala nada ni consulta el sistema, igual que InputInjector(dryRun). La medicion de
// consumo y el conteo de hooks del self-test lo crean activo (solo observa, no actua).
//
// Una implementacion por plataforma:
//  - Windows (platform/win/ForegroundWatcherWin.cpp): SetWinEventHook(EVENT_SYSTEM_FOREGROUND). hwnd
//    es el HWND nativo como quintptr, listo para reinterpret_cast<HWND>; exeName, "Nuke15.1.exe".
//  - macOS (platform/mac/ForegroundWatcherMac.mm): NSWorkspaceDidActivateApplicationNotification (la
//    app activa). hwnd va en 0 (mac no expone la ventana); pid y exeName ("Nuke15.1") son los de la
//    app activa.
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

    // Observadores del sistema instalados ahora en todo el proceso (hooks de Windows, observadores
    // de NSWorkspace en mac).
    static int installedHooks();

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
