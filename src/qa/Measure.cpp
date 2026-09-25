#include "qa/Measure.h"

#include "app/AppController.h"
#include "app/HotkeyHub.h"
#include "app/MainWindow.h"
#include "app/ModuleHost.h"
#include "app/ModuleRegistry.h"
#include "app/SettingsStore.h"
#include "platform/ProcessStats.h"

#include <QApplication>
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
            window.selectPage(d.id);
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

    AppController::Options options;
    options.measurement = true;
    AppController app(options);
    // Lo que tarda en asentarse el arranque (pintado inicial, cargas perezosas de Qt).
    pump(3000);
    const ProcessStats start = ProcessStats::current();
    print("start", QStringLiteral("-"), start);
    std::printf("measure private-all-off KB=%lld tools-on=%d\n", static_cast<long long>(start.privateBytes / 1024),
                app.host()->runningCount());
    std::fflush(stdout);

    QEventLoop loop;
    QTimer::singleShot(seconds * 1000, &loop, &QEventLoop::quit);
    loop.exec();
    const ProcessStats end = ProcessStats::current();
    print("end", QStringLiteral("-"), end);
    const qint64 cpu = end.cpuMs - start.cpuMs;
    std::printf("measure result idle seconds=%d cpu_ms=%lld private_delta_KB=%lld %s\n", seconds, static_cast<long long>(cpu),
                static_cast<long long>((end.privateBytes - start.privateBytes) / 1024), cpu < 50 ? "ok" : "FALLO");
    std::fflush(stdout);
    return cpu < 50 ? 0 : 1;
}

} // namespace Measure
