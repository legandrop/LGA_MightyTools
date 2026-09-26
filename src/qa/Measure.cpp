#include "qa/Measure.h"

#include "app/AppController.h"
#include "app/HotkeyHub.h"
#include "app/MainWindow.h"
#include "app/ModuleHost.h"
#include "app/ModuleRegistry.h"
#include "app/SettingsStore.h"
#include "platform/ProcessStats.h"

#include <QAbstractEventDispatcher>
#include <QApplication>
#include <QSet>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>
#include <QTimer>

#include <cstdio>

namespace {

void pump(int ms)
{
    // Eventos pendientes, deleteLater incluidos, y un rato de ciclo real.
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
}

int qAppCount(bool timers)
{
    return timers ? int(qApp->findChildren<QTimer *>().size()) : int(qApp->findChildren<QThread *>().size());
}

void print(const char *label, const QString &id, const ProcessStats &s)
{
    std::printf("measure %s %s %s qtimers=%d qthreads=%d\n", label, qPrintable(id), qPrintable(s.toString()),
                qAppCount(true), qAppCount(false));
    std::fflush(stdout);
}

bool within(qint64 before, qint64 after, qint64 tolerance)
{
    return before < 0 || after < 0 || qAbs(after - before) <= tolerance;
}

QString argAfter(const QStringList &arguments, const QString &flag, int offset)
{
    const int index = arguments.indexOf(flag);
    if (index < 0 || index + offset >= arguments.size()) {
        return QString();
    }
    const QString value = arguments.at(index + offset);
    return value.startsWith(QLatin1String("--")) ? QString() : value;
}

} // namespace

namespace Measure {

int cycles(const QStringList &arguments)
{
    bool ok = false;
    int count = argAfter(arguments, QStringLiteral("--measure-cycles"), 1).toInt(&ok);
    if (!ok || count <= 0) {
        count = 20;
    }
    const QString only = argAfter(arguments, QStringLiteral("--measure-cycles"), 2);
    // --no-panel: sin abrir la pagina de la herramienta (separa lo del modulo de lo del panel).
    const bool withPanel = !arguments.contains(QStringLiteral("--no-panel"));

    MemorySettingsStore store;
    HostOptions options;
    options.automatedRun = true;
    ModuleHost host(ModuleRegistry::all(), &store, options);
    // La ventana real (con sus paginas), sin mostrarla: el panel de cada herramienta se crea en ella.
    MainWindow window(&host, MainWindow::Mode::Capture, nullptr);
    window.setAttribute(Qt::WA_DontShowOnScreen, true);
    QObject::connect(&window, &MainWindow::toggleRequested, &host, &ModuleHost::setEnabled);
    pump(200);

    int failures = 0;
    std::printf("measure mode=cycles count=%d platform=%s tools=%d\n", count, qPrintable(QGuiApplication::platformName()),
                int(host.descriptors().size()));
    for (const ModuleDescriptor &d : host.descriptors()) {
        if (!only.isEmpty() && d.id != only) {
            continue;
        }
        // Un ciclo de calentamiento: fuentes, estilos y caches de Qt que se cargan una sola vez por
        // proceso no son del modulo.
        host.setEnabled(d.id, true);
        window.selectPage(d.id);
        pump(100);
        host.setEnabled(d.id, false);
        window.selectPage(MainWindow::kGeneral);
        pump(200);

        const ProcessStats before = ProcessStats::current();
        const int timers0 = qAppCount(true);
        const int threads0 = qAppCount(false);
        print("before", d.id, before);
        QElapsedTimer clock;
        clock.start();
        for (int i = 0; i < count; ++i) {
            host.setEnabled(d.id, true);
            if (withPanel) {
                window.selectPage(d.id);
            }
            pump(20);
            host.setEnabled(d.id, false);
            window.selectPage(MainWindow::kGeneral);
            pump(20);
        }
        pump(300);
        const ProcessStats after = ProcessStats::current();
        print("after", d.id, after);
        const bool okHandles = within(before.handles, after.handles, 2);
        const bool okThreads = within(before.threads, after.threads, 2);
        const bool okGdi = within(before.gdiObjects, after.gdiObjects, 2);
        const bool okUser = within(before.userObjects, after.userObjects, 2);
        const bool okQt = qAppCount(true) == timers0 && qAppCount(false) == threads0;
        const bool okHotkeys = host.hotkeyHub()->registeredCount() == 0 && host.module(d.id) == nullptr;
        const bool pass = okHandles && okThreads && okGdi && okUser && okQt && okHotkeys;
        std::printf("measure result %s cycles=%d ms=%lld handles=%+lld threads=%+lld gdi=%+lld user=%+lld qt=%s hotkeys=%d %s\n",
                    qPrintable(d.id), count, static_cast<long long>(clock.elapsed()),
                    static_cast<long long>(after.handles - before.handles),
                    static_cast<long long>(after.threads - before.threads),
                    static_cast<long long>(after.gdiObjects - before.gdiObjects),
                    static_cast<long long>(after.userObjects - before.userObjects), okQt ? "igual" : "DISTINTO",
                    host.hotkeyHub()->registeredCount(), pass ? "ok" : "FALLO");
        std::fflush(stdout);
        if (!pass) {
            ++failures;
        }
    }
    host.shutdown();
    return failures == 0 ? 0 : 1;
}

int idle(const QStringList &arguments)
{
    bool ok = false;
    int seconds = argAfter(arguments, QStringLiteral("--measure-idle"), 1).toInt(&ok);
    if (!ok || seconds <= 0) {
        seconds = 60;
    }
    const ProcessStats boot = ProcessStats::current();
    std::printf("measure mode=idle seconds=%d platform=%s\n", seconds, qPrintable(QGuiApplication::platformName()));
    print("boot", QStringLiteral("-"), boot);

    // El reposo se cuenta despues de asentarse el arranque (plan 4.3: "despues del chequeo de
    // updates"): en los primeros segundos trabajan hilos del pool de Windows que se retiran solos.
    bool okSettle = false;
    int settle = argAfter(arguments, QStringLiteral("--measure-idle"), 2).toInt(&okSettle);
    if (!okSettle || settle < 0) {
        settle = 60;
    }

    AppController::Options options;
    options.measurement = true;
    AppController app(options);
    pump(3000);
    const ProcessStats early = ProcessStats::current();
    print("early", QStringLiteral("-"), early);
    std::printf("measure private-all-off KB=%lld tools-on=%d\n", static_cast<long long>(early.privateBytes / 1024),
                app.host()->runningCount());
    std::fflush(stdout);
    if (settle > 3) {
        pump((settle - 3) * 1000);
    }
    const ProcessStats start = ProcessStats::current();
    print("start", QStringLiteral("-"), start);
    // Todo lo que tiene un timer registrado en el despachador (QTimer, QBasicTimer, animaciones de
    // estilo): lo unico que puede despertar al hilo principal con todo apagado.
    {
        QSet<QObject *> objects;
        for (QWidget *widget : QApplication::allWidgets()) {
            objects.insert(widget);
            for (QObject *child : widget->findChildren<QObject *>()) {
                objects.insert(child);
            }
        }
        for (QObject *child : qApp->findChildren<QObject *>()) {
            objects.insert(child);
        }
        for (QObject *child : app.findChildren<QObject *>()) {
            objects.insert(child);
        }
        objects.insert(&app);
        objects.insert(qApp);
        int timers = 0;
        for (QObject *object : objects) {
            for (const QAbstractEventDispatcher::TimerInfo &timer :
                 QAbstractEventDispatcher::instance()->registeredTimers(object)) {
                ++timers;
                std::printf("measure timer class=%s name=%s interval=%dms\n", object->metaObject()->className(),
                            qPrintable(object->objectName()), timer.interval);
            }
        }
        std::printf("measure timers-registered=%d\n", timers);
        std::fflush(stdout);
    }
    // El reposo se mide con la lectura barata del CPU: la foto completa (ProcessStats::current)
    // recorre los hilos de todo el sistema y su costo entraria en la medicion.
    const qint64 startCpu = ProcessStats::cpuMsNow();

    // Una muestra por minuto: la curva dice si hay un consumo continuo o solo el arranque.
    for (int elapsed = 0; elapsed < seconds;) {
        const int step = qMin(60, seconds - elapsed);
        QEventLoop loop;
        QTimer::singleShot(step * 1000, &loop, &QEventLoop::quit);
        loop.exec();
        elapsed += step;
        std::printf("measure sample t=%ds cpu_since_start=%lldms\n", elapsed,
                    static_cast<long long>(ProcessStats::cpuMsNow() - startCpu));
        std::fflush(stdout);
    }
    const qint64 endCpu = ProcessStats::cpuMsNow();
    const ProcessStats end = ProcessStats::current();
    print("end", QStringLiteral("-"), end);
    const qint64 cpu = endCpu - startCpu;
    std::printf("measure result idle seconds=%d settle=%d cpu_ms=%lld cpu_total_ms=%lld private_delta_KB=%lld %s\n",
                seconds, settle, static_cast<long long>(cpu), static_cast<long long>(endCpu - early.cpuMs),
                static_cast<long long>((end.privateBytes - start.privateBytes) / 1024), cpu < 50 ? "ok" : "FALLO");
    std::fflush(stdout);
    return cpu < 50 ? 0 : 1;
}

} // namespace Measure
