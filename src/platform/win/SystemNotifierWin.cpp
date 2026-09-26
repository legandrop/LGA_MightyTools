#include "platform/SystemNotifier.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QQueue>
#include <QThread>
#include <QWaitCondition>

#include <windows.h>

// Copia de NotificationWorker y showWindowsNotificationInternal de LGA_PipeSync_2
// (src/services/Notifications.cpp): una cola en un hilo propio, un PowerShell por toast con la
// plantilla ToastImageAndText02 y los mismos respaldos. Cambios de esta app: PowerShell sin ventana
// (CREATE_NO_WINDOW), el proceso se espera dentro del hilo de la cola (en PipeSync el hilo nunca
// vuelve a su ciclo de eventos y el aviso de "termino" no llegaba), y el globo de respaldo usa el
// icono del exe en vez del generico de informacion.

namespace {

constexpr int kPauseBetweenToastsMs = 500;
constexpr int kPowerShellTimeoutMs = 20000;
constexpr int kWaitSliceMs = 100;

struct Toast
{
    QString title;
    QString body;
};

QString toastScript(const QString &title, const QString &body, const QString &imagePath, const QString &exePath)
{
    // Notificador sin AUMID: un espacio de ancho cero, como PipeSync. Windows muestra el toast con el
    // encabezado vacio y un click no vuelve a la app.
    const QString aumid = QString(QChar(0x200B));
    const QString fallback = QStringLiteral(
        "} catch {"
        "    Write-Host 'API nativa fallo, usando BurntToast';"
        "    try {"
        "        if (Get-Module -ListAvailable -Name BurntToast) {"
        "            Import-Module BurntToast;"
        "            New-BurntToastNotification -Text '%1', '%2';"
        "            Write-Host 'BurntToast exitoso';"
        "        } else {"
        "            Add-Type -AssemblyName System.Windows.Forms;"
        "            Add-Type -AssemblyName System.Drawing;"
        "            $notification = New-Object System.Windows.Forms.NotifyIcon;"
        "            $notification.Icon = [System.Drawing.Icon]::ExtractAssociatedIcon('%4');"
        "            $notification.BalloonTipIcon = 'None';"
        "            $notification.BalloonTipText = '%2';"
        "            $notification.BalloonTipTitle = '%1';"
        "            $notification.Visible = $true;"
        "            $notification.ShowBalloonTip(5000);"
        "            Start-Sleep -Seconds 2;"
        "            $notification.Dispose();"
        "            Write-Host 'Globo completado';"
        "        }"
        "    } catch {"
        "        Write-Host 'Error en respaldo:' $_.Exception.Message;"
        "        exit 1;"
        "    }"
        "}");
    QString script;
    if (!imagePath.isEmpty()) {
        script = QStringLiteral(
            "try {"
            "    $imagePath = '%3';"
            "    [Windows.UI.Notifications.ToastNotificationManager, Windows.UI.Notifications, ContentType = WindowsRuntime] | Out-Null;"
            "    [Windows.Data.Xml.Dom.XmlDocument, Windows.Data.Xml.Dom, ContentType = WindowsRuntime] | Out-Null;"
            "    $templateType = [Windows.UI.Notifications.ToastTemplateType]::ToastImageAndText02;"
            "    $toastXml = [Windows.UI.Notifications.ToastNotificationManager]::GetTemplateContent($templateType);"
            "    $textNodes = $toastXml.GetElementsByTagName('text');"
            "    $imageNode = $toastXml.GetElementsByTagName('image')[0];"
            "    $textNodes.Item(0).InnerText = '%1';"
            "    $textNodes.Item(1).InnerText = '%2';"
            "    $imageNode.SetAttribute('src', $imagePath);"
            "    $toast = [Windows.UI.Notifications.ToastNotification]::new($toastXml);"
            "    $notifier = [Windows.UI.Notifications.ToastNotificationManager]::CreateToastNotifier('%5');"
            "    $notifier.Show($toast);"
            "    Write-Host 'Toast nativo con imagen enviado';")
                 + fallback;
    } else {
        script = QStringLiteral(
            "try {"
            "    [Windows.UI.Notifications.ToastNotificationManager, Windows.UI.Notifications, ContentType = WindowsRuntime] | Out-Null;"
            "    [Windows.Data.Xml.Dom.XmlDocument, Windows.Data.Xml.Dom, ContentType = WindowsRuntime] | Out-Null;"
            "    $templateType = [Windows.UI.Notifications.ToastTemplateType]::ToastText02;"
            "    $toastXml = [Windows.UI.Notifications.ToastNotificationManager]::GetTemplateContent($templateType);"
            "    $textNodes = $toastXml.GetElementsByTagName('text');"
            "    $textNodes.Item(0).InnerText = '%1';"
            "    $textNodes.Item(1).InnerText = '%2';"
            "    $toast = [Windows.UI.Notifications.ToastNotification]::new($toastXml);"
            "    $notifier = [Windows.UI.Notifications.ToastNotificationManager]::CreateToastNotifier('%5');"
            "    $notifier.Show($toast);"
            "    Write-Host 'Toast nativo sin imagen enviado';")
                 + fallback;
    }
    // Un solo arg() con todos: un "%2" dentro del titulo no se reemplaza de nuevo.
    return script.arg(SystemNotifier::escapeForScript(title), SystemNotifier::escapeForScript(body),
                      SystemNotifier::escapeForScript(imagePath), SystemNotifier::escapeForScript(exePath), aumid);
}

// El worker de la cola: vive en su hilo y procesa de a un toast.
class NotificationWorker : public QObject
{
public:
    void enqueue(const Toast &toast)
    {
        QMutexLocker locker(&m_mutex);
        m_queue.enqueue(toast);
        m_condition.wakeOne();
    }

    void stop()
    {
        QMutexLocker locker(&m_mutex);
        m_stop = true;
        m_condition.wakeAll();
    }

    bool stopRequested()
    {
        QMutexLocker locker(&m_mutex);
        return m_stop;
    }

    void process()
    {
        while (true) {
            Toast toast;
            {
                QMutexLocker locker(&m_mutex);
                while (m_queue.isEmpty() && !m_stop) {
                    m_condition.wait(&m_mutex);
                }
                if (m_stop) {
                    break;
                }
                toast = m_queue.dequeue();
            }
            showToast(toast);
            // Pausa entre avisos para no apilar toasts (PipeSync), cortada si la app se cierra.
            for (int waited = 0; waited < kPauseBetweenToastsMs && !stopRequested(); waited += kWaitSliceMs) {
                QThread::msleep(kWaitSliceMs);
            }
        }
    }

private:
    void showToast(const Toast &toast)
    {
        const SystemNotifier::IconFile icon =
            SystemNotifier::prepareIcon(QStringLiteral(":/icons/LGA_MightyTools.ico"), SystemNotifier::iconTempPath(false));
        const QString exePath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
        const QString script = toastScript(toast.title, toast.body, QDir::toNativeSeparators(icon.path), exePath);
        QProcess process;
        // Sin consola: PowerShell es una app de consola y abriria una ventana negra.
        process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments) {
            arguments->flags |= CREATE_NO_WINDOW;
        });
        process.start(QStringLiteral("powershell.exe"),
                      {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"), QStringLiteral("-ExecutionPolicy"),
                       QStringLiteral("Bypass"), QStringLiteral("-Command"), script});
        // Se espera de a tramos cortos: si la app se cierra con un toast en vuelo, se corta PowerShell
        // y el hilo termina enseguida (en PipeSync el cierre podia borrar el worker en uso).
        QElapsedTimer elapsed;
        elapsed.start();
        while (!process.waitForFinished(kWaitSliceMs)) {
            const bool stopping = stopRequested();
            if (stopping || elapsed.elapsed() >= kPowerShellTimeoutMs || process.state() == QProcess::NotRunning) {
                if (process.state() != QProcess::NotRunning) {
                    qWarning().noquote() << (stopping ? QStringLiteral("[SystemNotifier] La app se cierra: se corta PowerShell")
                                                      : QStringLiteral("[SystemNotifier] PowerShell no termino en %1 ms; se corta")
                                                            .arg(kPowerShellTimeoutMs));
                    process.kill();
                    process.waitForFinished(1000);
                }
                return;
            }
        }
        qInfo().noquote() << QStringLiteral("[SystemNotifier] Toast '%1' (icono %2x%3) -> salida %4: %5")
                                 .arg(toast.title)
                                 .arg(icon.size.width())
                                 .arg(icon.size.height())
                                 .arg(process.exitCode())
                                 .arg(QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed());
    }

    QMutex m_mutex;
    QWaitCondition m_condition;
    QQueue<Toast> m_queue;
    bool m_stop = false;
};

} // namespace

struct SystemNotifier::Private
{
    QThread *thread = nullptr;
    NotificationWorker *worker = nullptr;
};

SystemNotifier::SystemNotifier(bool automatedRun, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
    , m_automated(automatedRun)
{
}

SystemNotifier::~SystemNotifier()
{
    if (d->thread) {
        d->worker->stop();
        d->thread->quit();
        if (d->thread->wait(3000)) {
            delete d->worker;
        } else {
            // No deberia pasar (el hilo corta PowerShell en 100 ms). Si pasa, se sueltan hilo y worker
            // en vez de borrarlos en uso: la app ya esta saliendo.
            qWarning() << "[SystemNotifier] El hilo de notificaciones no termino a tiempo; se suelta";
            d->thread->setParent(nullptr);
        }
    }
}

bool SystemNotifier::workerRunning() const
{
    return d->thread != nullptr;
}

void SystemNotifier::show(const QString &title, const QString &body)
{
    m_last = Last{title, body, IconFile{}, false};
    if (m_automated) {
        // Corrida automatizada: sin PowerShell ni hilo. Se prepara el icono (en su propio PNG de QA)
        // y se anota lo que se mostraria.
        m_last.icon = prepareIcon(QStringLiteral(":/icons/LGA_MightyTools.ico"), iconTempPath(true));
        qInfo().noquote() << QStringLiteral("[SystemNotifier] (automatizada, sin mostrar) '%1' | '%2' | icono %3x%4 de %5 frames")
                                 .arg(title, body)
                                 .arg(m_last.icon.size.width())
                                 .arg(m_last.icon.size.height())
                                 .arg(m_last.icon.frames);
        return;
    }
    if (!d->thread) {
        // Perezoso: sin notificaciones, sin hilo.
        d->worker = new NotificationWorker();
        d->thread = new QThread(this);
        d->worker->moveToThread(d->thread);
        NotificationWorker *worker = d->worker;
        connect(d->thread, &QThread::started, worker, [worker]() { worker->process(); });
        d->thread->start();
        qInfo() << "[SystemNotifier] Hilo de notificaciones creado";
    }
    m_last.launched = true;
    d->worker->enqueue(Toast{title, body});
}
