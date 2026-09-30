#ifndef MIGHTYTOOLS_OPENINNUKEX_OPERATIONS_H
#define MIGHTYTOOLS_OPENINNUKEX_OPERATIONS_H

#include "modules/openinnukex/ApplyIssue.h"

#include <QList>
#include <QObject>
#include <QPointer>

class QThread;

// Lo que Open in NukeX tiene "en curso" y tiene que sobrevivir al panel: Apply / Re-apply (hasta ~1.5 s) y
// "Uninstall old app" (hasta ~2 min). El panel se borra y se arma de nuevo al cambiar el idioma: si el hilo y la
// bandera "en curso" vivieran en el panel, el panel nuevo dejaria lanzar otra operacion, y borrar el viejo
// congelaria la UI esperando el hilo.
//
// Vida (regla "lo apagado no consume nada"): se crea con la primera operacion o el primer panel interactivo, y al
// APAGAR la herramienta (OpenInNukeXModule::stop -> shutdownIfIdle) se destruye en el acto si no hay nada en
// curso, o al terminar lo que este en curso. Salir de la app espera al Apply en vuelo (destructor).
//
// Las dos operaciones escriben HKCU (asociacion .nk): nunca corren a la vez entre si ni con "Release .nk
// association" (releaseSystem del descriptor, que consulta busy()).
//
// Los hilos de trabajo devuelven CODIGOS (ApplyIssue), nunca texto: la traduccion se hace en la UI.
class OpenInNukeXOperations : public QObject
{
    Q_OBJECT

public:
    // Crea el singleton si no existe. Si existe y estaba esperando que terminara una operacion para
    // destruirse (la herramienta se apago con algo en curso), lo cancela. Si el borrado ya se pidio
    // (deleteLater), crea uno nuevo: el viejo no toca el puntero global al terminar de borrarse.
    static OpenInNukeXOperations *instance();
    // El singleton si existe, sin crearlo (consultas desde quien no debe hacer que exista).
    static OpenInNukeXOperations *existing();
    // True si hay un Apply o una desinstalacion en curso (false si el singleton ni existe).
    static bool busy();
    // La herramienta se apaga: descarta los resultados pendientes y destruye el singleton si no hay nada en
    // curso; si hay algo, se destruye solo al terminar.
    static void shutdownIfIdle();

    ~OpenInNukeXOperations() override;

    bool applyRunning() const { return m_applyRunning; }
    bool uninstallRunning() const { return m_uninstallRunning; }

    // Apply (reapply = volver a aplicar). `parentWindow`: el HWND dueño del selector nativo "Abrir con" (Windows),
    // capturado en el hilo de UI; nullptr en mac. No hace nada si ya hay una operacion en curso.
    void startApply(bool reapply, void *parentWindow);
    // Desinstala el cliente viejo (solo Windows; en otras plataformas no hace nada). Con `automatedRun` solo lee.
    void startUninstall(bool buildTree, bool automatedRun);

    struct ApplyResult
    {
        bool valid = false;
        bool success = false;
        bool needsConfirmation = false;
        QList<int> issues; ///< ApplyIssue como enteros
    };
    struct UninstallResult
    {
        bool valid = false;
        bool stillInstalled = false;
        bool launched = false;
    };
    // El resultado que llego sin ningun panel escuchando (una sola vez). Quien lo toma es el que lo muestra:
    // conviene tomarlo recien al mostrarlo, asi un panel que muere antes no lo pierde.
    ApplyResult takePendingApply();
    UninstallResult takePendingUninstall();
    void discardPending();

    // Solo para el self-test: marca "Apply en curso" sin correr nada ni tocar el registro.
    void markApplyRunningForTest(bool running) { m_applyRunning = running; }
    // Solo para el self-test: con sleepMs >= 0 el Apply corre en su hilo de verdad pero solo duerme ese
    // tiempo y devuelve exito, sin tocar el registro. -1 vuelve al Apply real.
    static void useFakeApplyForTest(int sleepMs);

signals:
    void applyFinished(bool success, bool needsConfirmation, const QList<int> &issues);
    void uninstallFinished(bool stillInstalled, bool launched);

private:
    explicit OpenInNukeXOperations(QObject *parent);
    void onApplyDone(bool success, bool needsConfirmation, const QList<int> &issues);
    void onUninstallDone(bool stillInstalled, bool launched);
    void destroyWhenIdle();

    bool m_applyRunning = false;
    bool m_uninstallRunning = false;
    bool m_destroyWhenIdle = false;
    QPointer<QThread> m_applyThread;
    ApplyResult m_pendingApply;
    UninstallResult m_pendingUninstall;
};

#endif // MIGHTYTOOLS_OPENINNUKEX_OPERATIONS_H
