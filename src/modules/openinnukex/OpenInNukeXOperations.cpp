#include "modules/openinnukex/OpenInNukeXOperations.h"

#include <QCoreApplication>
#include <QDebug>
#include <QThread>
#include <QTimer>

#include <atomic>

#ifdef Q_OS_WIN
#include "modules/openinnukex/win/OldClientMigration.h"
#include "modules/openinnukex/win/WinFileAssociation.h"
#elif defined(Q_OS_MACOS)
#include "modules/openinnukex/mac/MacFileAssociation.h"
#endif

namespace {
// useFakeApplyForTest(): -1 es el Apply real.
std::atomic<int> g_fakeApplyMs{-1};
} // namespace

#ifdef Q_OS_WIN
namespace {

// Corre WinFileAssociation::apply() (que puede tardar hasta ~1.5 s entre los msleep de reintento y el hash de
// UserChoiceLatest) en un QThread propio, para no congelar la ventana con Apply/Re-apply apretado. Devuelve
// CODIGOS de falla, nunca texto (la traduccion la hace la UI).
class ApplyWorker : public QObject
{
    Q_OBJECT
public:
    // `parentHwnd`: la ventana dueña del selector nativo "Abrir con" si el hash silencioso no alcanza. HWND se
    // declara sin windows.h en WinFileAssociation.h (la UI no lleva headers de Win32).
    ApplyWorker(bool reapply, HWND parentHwnd) : m_reapply(reapply), m_parentHwnd(parentHwnd) {}

public slots:
    void run()
    {
        if (const int fakeMs = g_fakeApplyMs.load(); fakeMs >= 0) {
            QThread::msleep(static_cast<unsigned long>(fakeMs));
            emit finished(true, false, {});
            return;
        }
        const WinFileAssociation::ApplyOutcome outcome = WinFileAssociation::apply(m_reapply, m_parentHwnd);
        QList<int> issues;
        for (const ApplyIssue issue : outcome.issues) {
            issues << static_cast<int>(issue);
        }
        emit finished(outcome.result == WinFileAssociation::ApplyResult::Success,
                      outcome.result == WinFileAssociation::ApplyResult::NeedsUserConfirmation, issues);
    }

signals:
    void finished(bool success, bool needsConfirmation, QList<int> issues);

private:
    bool m_reapply;
    HWND m_parentHwnd;
};

// "Uninstall old app": la misma funcion que usa el instalador (--remove-old-client). Lanza el desinstalador del
// cliente viejo (pide su propio UAC), espera hasta ~2 min a que desaparezca y retoma los .nk. En un hilo propio:
// la ventana sigue viva mientras tanto.
class RemoveOldClientWorker : public QObject
{
    Q_OBJECT
public:
    explicit RemoveOldClientWorker(const OldClientMigration::Options &options) : m_options(options) {}

public slots:
    void run()
    {
        // Sin settings: con el panel abierto el modulo ya esta prendido.
        const OldClientMigration::Report report = OldClientMigration::removeOldClient(nullptr, m_options);
        for (const QString &line : report.lines) {
            qInfo().noquote() << "[openInNukeX] quitar cliente viejo:" << line;
        }
        emit finished(report.stillInstalled, report.launched);
    }

signals:
    void finished(bool stillInstalled, bool launched);

private:
    OldClientMigration::Options m_options;
};

} // namespace
#endif

namespace {
OpenInNukeXOperations *s_instance = nullptr;
}

OpenInNukeXOperations *OpenInNukeXOperations::instance()
{
    if (!s_instance) {
        // Cuelga de la aplicacion: se borra con ella, antes de que termine el proceso.
        s_instance = new OpenInNukeXOperations(QCoreApplication::instance());
    }
    s_instance->m_destroyWhenIdle = false; // alguien lo vuelve a usar: la herramienta esta prendida de nuevo
    return s_instance;
}

void OpenInNukeXOperations::useFakeApplyForTest(int sleepMs)
{
    g_fakeApplyMs = sleepMs;
}

OpenInNukeXOperations *OpenInNukeXOperations::existing()
{
    return s_instance;
}

bool OpenInNukeXOperations::busy()
{
    return s_instance && (s_instance->m_applyRunning || s_instance->m_uninstallRunning);
}

void OpenInNukeXOperations::shutdownIfIdle()
{
    if (!s_instance) {
        return;
    }
    // Nadie va a mirar esos resultados: la herramienta se apago.
    s_instance->discardPending();
    if (s_instance->m_applyRunning || s_instance->m_uninstallRunning) {
        s_instance->m_destroyWhenIdle = true;
        return;
    }
    s_instance->destroyWhenIdle();
}

void OpenInNukeXOperations::destroyWhenIdle()
{
    if (s_instance == this) {
        s_instance = nullptr;
    }
    // Despues de volver al ciclo de eventos: puede llamarse desde una de sus propias senales.
    deleteLater();
}

OpenInNukeXOperations::OpenInNukeXOperations(QObject *parent)
    : QObject(parent)
{
}

OpenInNukeXOperations::~OpenInNukeXOperations()
{
    if (s_instance == this) {
        s_instance = nullptr;
    }
    // Con un Apply en vuelo (la app se cierra, o la herramienta se apago y el borrado diferido llego antes
    // que el quit() en cola del hilo) se espera al hilo: un segundo y medio como mucho. El hilo no es hijo
    // de este objeto, asi que borrarlo nunca destruye un QThread vivo; m_applyThread se anula solo cuando
    // el hilo se borra. La desinstalacion no se espera: su hilo se borra solo y el proceso termina.
    if (m_applyThread && m_applyThread->isRunning()) {
        m_applyThread->quit();
        m_applyThread->wait();
    }
}

void OpenInNukeXOperations::startApply(bool reapply, void *parentWindow)
{
    // Las dos operaciones escriben la misma asociacion en HKCU: una a la vez.
    if (m_applyRunning || m_uninstallRunning) {
        return;
    }
    m_applyRunning = true;
#ifdef Q_OS_WIN
    // Sin padre Qt, como el de la desinstalacion: el hilo se borra solo al terminar (finished ->
    // deleteLater) aunque este objeto ya no exista.
    auto *thread = new QThread();
    auto *worker = new ApplyWorker(reapply, static_cast<HWND>(parentWindow));
    worker->moveToThread(thread);
    connect(thread, &QThread::started, worker, &ApplyWorker::run);
    connect(worker, &ApplyWorker::finished, this, &OpenInNukeXOperations::onApplyDone);
    connect(worker, &ApplyWorker::finished, thread, &QThread::quit);
    connect(worker, &ApplyWorker::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    m_applyThread = thread;
    thread->start();
#elif defined(Q_OS_MACOS)
    Q_UNUSED(reapply);
    Q_UNUSED(parentWindow);
    QPointer<OpenInNukeXOperations> self(this);
    MacFileAssociation::setAsDefaultNkHandler([self](bool ok, const QString &error) {
        Q_UNUSED(error); // el detalle crudo solo va al log de quien lo llama
        if (self) {
            self->onApplyDone(ok, false, ok ? QList<int>() : QList<int>{int(ApplyIssue::Unknown)});
        }
    });
#else
    Q_UNUSED(reapply);
    Q_UNUSED(parentWindow);
    QTimer::singleShot(0, this, [this]() { onApplyDone(false, false, {}); });
#endif
}

void OpenInNukeXOperations::startUninstall(bool buildTree, bool automatedRun)
{
    if (m_uninstallRunning || m_applyRunning) {
        return;
    }
#ifdef Q_OS_WIN
    OldClientMigration::Options options;
    options.buildTree = buildTree;
    // Corrida automatizada: nunca se lanza el desinstalador ni se escribe nada (solo se loguea).
    options.launchAllowed = !automatedRun;
    options.dryRun = automatedRun;

    m_uninstallRunning = true;
    // Sin padre Qt y sin esperarlo (puede tardar hasta ~2 min): el hilo se borra solo al terminar.
    auto *thread = new QThread();
    auto *worker = new RemoveOldClientWorker(options);
    worker->moveToThread(thread);
    connect(thread, &QThread::started, worker, &RemoveOldClientWorker::run);
    connect(worker, &RemoveOldClientWorker::finished, this, &OpenInNukeXOperations::onUninstallDone);
    connect(worker, &RemoveOldClientWorker::finished, thread, &QThread::quit);
    connect(worker, &RemoveOldClientWorker::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
#else
    Q_UNUSED(buildTree);
    Q_UNUSED(automatedRun);
#endif
}

void OpenInNukeXOperations::onApplyDone(bool success, bool needsConfirmation, const QList<int> &issues)
{
    m_applyRunning = false;
    // m_applyThread NO se anula aca: el hilo puede seguir corriendo hasta procesar su quit() en cola, y
    // el destructor lo tiene que ver para esperarlo. El QPointer se anula solo cuando el hilo se borra.
    if (m_destroyWhenIdle) {
        // La herramienta se apago con esto en curso: nadie mira el resultado.
        if (!m_uninstallRunning) {
            destroyWhenIdle();
        }
        return;
    }
    // Sin ningun panel escuchando (se estaba rearmando la ventana), el resultado espera al proximo.
    if (receivers(SIGNAL(applyFinished(bool,bool,QList<int>))) == 0) {
        m_pendingApply = {true, success, needsConfirmation, issues};
        return;
    }
    emit applyFinished(success, needsConfirmation, issues);
}

void OpenInNukeXOperations::onUninstallDone(bool stillInstalled, bool launched)
{
    m_uninstallRunning = false;
    if (m_destroyWhenIdle) {
        if (!m_applyRunning) {
            destroyWhenIdle();
        }
        return;
    }
    if (receivers(SIGNAL(uninstallFinished(bool,bool))) == 0) {
        m_pendingUninstall = {true, stillInstalled, launched};
        return;
    }
    emit uninstallFinished(stillInstalled, launched);
}

OpenInNukeXOperations::ApplyResult OpenInNukeXOperations::takePendingApply()
{
    const ApplyResult result = m_pendingApply;
    m_pendingApply = ApplyResult();
    return result;
}

OpenInNukeXOperations::UninstallResult OpenInNukeXOperations::takePendingUninstall()
{
    const UninstallResult result = m_pendingUninstall;
    m_pendingUninstall = UninstallResult();
    return result;
}

void OpenInNukeXOperations::discardPending()
{
    m_pendingApply = ApplyResult();
    m_pendingUninstall = UninstallResult();
}

#ifdef Q_OS_WIN
#include "OpenInNukeXOperations.moc"
#endif
