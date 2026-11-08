#include "qa/Measure.h"

#include "app/AppController.h"
#include "app/HotkeyHub.h"
#include "app/MainWindow.h"
#include "app/ModuleHost.h"
#include "app/ModuleRegistry.h"
#include "platform/SystemNotifier.h"
#include "app/SettingsStore.h"
#include "platform/ForegroundWatcher.h"
#include "platform/ProcessStats.h"
#include "ui/Theme.h"

#include <QAbstractEventDispatcher>
#include <QApplication>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QScreen>
#include <QSet>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QDir>
#include <QHash>
#include <QPixmapCache>
#include <QtGui/private/qfont_p.h>




#include <QSettings>
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
    // El hook de ventana al frente de verdad (solo observa): es un objeto USER y tiene que contar.
    options.observeForeground = true;
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
    // Plan 4.4: las herramientas que miran la ventana del frente comparten UN hook.
    const QStringList pair = {QStringLiteral("nukeShortcuts"), QStringLiteral("folderSwitch")};
    if (only.isEmpty() && host.descriptor(pair.at(0)) && host.descriptor(pair.at(1))) {
        const int hooks0 = ForegroundWatcher::installedHooks();
        const ProcessStats before = ProcessStats::current();
        int hooksBoth = -1;
        for (int i = 0; i < count; ++i) {
            host.setEnabled(pair.at(0), true);
            host.setEnabled(pair.at(1), true);
            hooksBoth = ForegroundWatcher::installedHooks();
            host.setEnabled(pair.at(0), false);
            host.setEnabled(pair.at(1), false);
            pump(20);
        }
        pump(300);
        const ProcessStats after = ProcessStats::current();
        const int hooksOff = ForegroundWatcher::installedHooks();
        const bool pass = hooks0 == 0 && hooksBoth == 1 && hooksOff == 0 && within(before.handles, after.handles, 2)
                          && within(before.threads, after.threads, 2) && within(before.gdiObjects, after.gdiObjects, 2)
                          && within(before.userObjects, after.userObjects, 2);
        std::printf("measure result pair nukeShortcuts+folderSwitch cycles=%d hooks_off=%d hooks_both=%d hooks_after=%d "
                    "handles=%+lld threads=%+lld gdi=%+lld user=%+lld %s\n",
                    count, hooks0, hooksBoth, hooksOff, static_cast<long long>(after.handles - before.handles),
                    static_cast<long long>(after.threads - before.threads),
                    static_cast<long long>(after.gdiObjects - before.gdiObjects),
                    static_cast<long long>(after.userObjects - before.userObjects), pass ? "ok" : "FALLO");
        std::fflush(stdout);
        if (!pass) {
            ++failures;
        }
    }
    host.shutdown();
    return failures == 0 ? 0 : 1;
}
// --measure-pages [rondas] [on|off] [--settings-copy <ini>]: lo que tarda cambiar de pagina en la ventana real
// y lo que ocupa. Por cada pagina mide la llamada a selectPage, los eventos que deja pendientes (layouts) y el
// pintado entero de la ventana; la primera visita (arma el panel) aparte de las siguientes. Al final, la memoria
// con la ventana escondida y despues de destruirla (lo que pasa cuando queda en la bandeja). «on» prende todas
// las herramientas antes (modo automatizado: nada actua de verdad). Con -Platform windows del runner mide la
// plataforma real en un escritorio aparte.
int pages(const QStringList &arguments)
{
    bool ok = false;
    int rounds = argAfter(arguments, QStringLiteral("--measure-pages"), 1).toInt(&ok);
    if (!ok || rounds <= 0) {
        rounds = 5;
    }
    const bool allOn = argAfter(arguments, QStringLiteral("--measure-pages"), 2) != QLatin1String("off");
    // --cpu-cap N: N % de un nucleo (25 = una PC unas cuatro veces mas lenta que esta en un solo hilo).
    const int cpuCap = argAfter(arguments, QStringLiteral("--cpu-cap"), 1).toInt();
    if (cpuCap > 0) {
        std::printf("measure cpu-cap=%d%% of one core applied=%s\n", cpuCap, ProcessStats::capCpuForTest(cpuCap) ? "si" : "no");
    }

    MemorySettingsStore store;
    // --settings-copy <ini>: los valores de una COPIA de un settings.ini (solo se lee), para medir con una
    // configuracion real (atajos, discos, layouts). La memoria no escribe nada.
    const QString copy = argAfter(arguments, QStringLiteral("--settings-copy"), 1);
    if (!copy.isEmpty()) {
        const QSettings ini(copy, QSettings::IniFormat);
        for (const QString &key : ini.allKeys()) {
            store.setValue(key, ini.value(key));
        }
    }
    HostOptions options;
    options.automatedRun = true;
    ModuleHost host(ModuleRegistry::all(), &store, options);
    if (!copy.isEmpty()) {
        host.startEnabled();
    } else if (allOn) {
        for (const ModuleDescriptor &d : host.descriptors()) {
            host.setEnabled(d.id, true);
        }
    }
    pump(200);
    std::printf("measure mode=pages rounds=%d tools_on=%s platform=%s\n", rounds,
                copy.isEmpty() ? (allOn ? "all" : "none") : "settings-copy", qPrintable(QGuiApplication::platformName()));
    std::printf("measure no-window %s\n", qPrintable(ProcessStats::current().toString()));

    QElapsedTimer build;
    // --probe-native: una ventana vacia y sin marco antes que la real, para separar lo que cuesta la PRIMERA
    // ventana nativa del proceso (Windows y Qt, una vez) de lo que cuesta la de la app.
    if (arguments.contains(QStringLiteral("--probe-native"))) {
        build.start();
        auto *plain = new QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint);
        plain->resize(400, 300);
        plain->winId();
        const qint64 native = build.nsecsElapsed();
        plain->show();
        QCoreApplication::processEvents();
        plain->repaint();
        std::printf("measure probe-native plain native=%.2f show+paint=%.2f ms\n", native / 1e6,
                    (build.nsecsElapsed() - native) / 1e6);
        delete plain;
        pump(100);
        std::printf("measure after-first-native %s\n", qPrintable(ProcessStats::current().toString()));
    }
    build.restart();
    auto *window = new MainWindow(&host, MainWindow::Mode::Capture, nullptr);
    QObject::connect(window, &MainWindow::toggleRequested, &host, &ModuleHost::setEnabled);
    const qint64 built = build.nsecsElapsed();
    window->ensurePolished();
    const qint64 polished = build.nsecsElapsed();
    window->winId();
    const qint64 native = build.nsecsElapsed();
    window->show();
    const qint64 showCall = build.nsecsElapsed();
    QCoreApplication::sendPostedEvents();
    QCoreApplication::processEvents();
    const qint64 events = build.nsecsElapsed();
    window->repaint();
    std::printf("measure open build=%.2f show+paint=%.2f ms (polish=%.2f native=%.2f show=%.2f events=%.2f paint=%.2f)\n",
                built / 1e6, (build.nsecsElapsed() - built) / 1e6, (polished - built) / 1e6, (native - polished) / 1e6,
                (showCall - native) / 1e6, (events - showCall) / 1e6, (build.nsecsElapsed() - events) / 1e6);
    pump(300);
    std::printf("measure window-open %s\n", qPrintable(ProcessStats::current().toString()));

    QStringList ids{MainWindow::kGeneral};
    for (const ModuleDescriptor &d : host.descriptors()) {
        ids << d.id;
    }
    QHash<QString, qint64> totalNext;
    qint64 sumFirst = 0;
    qint64 sumNext = 0;
    int countNext = 0;
    for (int round = 0; round <= rounds; ++round) {
        for (const QString &id : ids) {
            QElapsedTimer clock;
            clock.start();
            window->selectPage(id);
            const qint64 select = clock.nsecsElapsed();
            QCoreApplication::sendPostedEvents();
            QCoreApplication::processEvents();
            const qint64 events = clock.nsecsElapsed() - select;
            window->repaint();
            const qint64 total = clock.nsecsElapsed();
            const qint64 paint = total - select - events;
            if (round == 0) {
                sumFirst += total;
                std::printf("measure page first %-16s select=%6.2f events=%6.2f paint=%6.2f total=%6.2f ms\n",
                            qPrintable(id), select / 1e6, events / 1e6, paint / 1e6, total / 1e6);
            } else {
                totalNext[id] += total;
                sumNext += total;
                ++countNext;
                if (round == 1) {
                    std::printf("measure page next  %-16s select=%6.2f events=%6.2f paint=%6.2f total=%6.2f ms\n",
                                qPrintable(id), select / 1e6, events / 1e6, paint / 1e6, total / 1e6);
                }
            }
            std::fflush(stdout);
            pump(30);
        }
    }
    for (const QString &id : ids) {
        std::printf("measure page avg   %-16s %6.2f ms\n", qPrintable(id), totalNext.value(id) / 1e6 / rounds);
    }
    std::printf("measure all-pages-seen %s\n", qPrintable(ProcessStats::current().toString()));
    window->hide();
    pump(300);
    std::printf("measure window-hidden %s\n", qPrintable(ProcessStats::current().toString()));
    delete window;
    pump(300);
    std::printf("measure window-destroyed %s\n", qPrintable(ProcessStats::current().toString()));
    // Cuanto de lo que queda es cache de Qt y cuanto memoria libre que el heap no devolvio.
    QPixmapCache::clear();
    pump(100);
    std::printf("measure after-pixmapcache %s\n", qPrintable(ProcessStats::current().toString()));
    ProcessStats::releaseFreeHeap();
    pump(100);
    std::printf("measure after-heap-release %s\n", qPrintable(ProcessStats::current().toString()));
    QFontCache::instance()->clear();
    ProcessStats::releaseFreeHeap();
    pump(100);
    std::printf("measure after-fontcache %s\n", qPrintable(ProcessStats::current().toString()));
    {
        // Lo que sigue vivo despues de destruir la ventana: por clase, las mas numerosas.
        QHash<QString, int> classes;
        const QWidgetList all = QApplication::allWidgets();
        for (QWidget *w : all) {
            classes[QString::fromLatin1(w->metaObject()->className())]++;
        }
        std::printf("measure widgets-alive=%lld\n", static_cast<long long>(all.size()));
        for (auto it = classes.constBegin(); it != classes.constEnd(); ++it) {
            std::printf("measure widget-class %s=%d\n", qPrintable(it.key()), it.value());
        }
    }
    // Reabrirla: lo que cuesta volver de la bandeja cuando la ventana se solto.
    for (int again = 0; again < 2; ++again) {
        build.restart();
        window = new MainWindow(&host, MainWindow::Mode::Capture, nullptr);
        const qint64 rebuilt = build.nsecsElapsed();
        window->show();
        QCoreApplication::sendPostedEvents();
        QCoreApplication::processEvents();
        window->repaint();
        const qint64 shown = build.nsecsElapsed();
        window->selectPage(ids.value(1));
        QCoreApplication::sendPostedEvents();
        QCoreApplication::processEvents();
        window->repaint();
        std::printf("measure reopen build=%.2f show+paint=%.2f first-tool=%.2f ms\n", rebuilt / 1e6, (shown - rebuilt) / 1e6,
                    (build.nsecsElapsed() - shown) / 1e6);
        pump(200);
        delete window;
        pump(300);
        std::printf("measure reopen-destroyed %s\n", qPrintable(ProcessStats::current().toString()));
    }
    std::printf("measure result pages first_total=%.1f ms next_avg=%.2f ms\n", sumFirst / 1e6,
                countNext ? sumNext / 1e6 / countNext : 0.0);
    std::fflush(stdout);
    host.shutdown();
    return 0;
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

int notifyPreview(const QStringList &)
{
    // El mismo notificador que usa la app, en modo automatizado: prepara el icono y anota lo que
    // mostraria, sin lanzar PowerShell ni crear el hilo.
    SystemNotifier notifier(true);
    const char *const samples[][2] = {
        {"Frame Dope Sheet", "Calibrate the Dope Sheet first: one click, from the tray menu."},
        {"42 GB free on D:", "D: is running low (1.82 TB in total). You asked to be warned under 100 GB."},
    };
    bool ok = true;
    for (const auto &sample : samples) {
        notifier.show(QString::fromUtf8(sample[0]), QString::fromUtf8(sample[1]));
        const SystemNotifier::Last last = notifier.last();
        std::printf("notify title='%s' body='%s' icon=%s size=%dx%d frames=%d reused=%s launched=%s\n",
                    qPrintable(last.title), qPrintable(last.body), qPrintable(QDir::toNativeSeparators(last.icon.path)),
                    last.icon.size.width(), last.icon.size.height(), last.icon.frames, last.icon.reused ? "si" : "no",
                    last.launched ? "si" : "no");
        ok = ok && !last.launched && !last.icon.path.isEmpty();
    }
    std::printf("notify worker=%s\n", notifier.workerRunning() ? "creado" : "no-creado");
    std::fflush(stdout);
    return ok && !notifier.workerRunning() ? 0 : 1;
}

int fonts(const QStringList &)
{
    // Anchos de Chrome medidos en el canvas (render del HTML a DPR 1, Inter local): caja del elemento
    // menos su relleno y borde.
    struct Sample
    {
        const char *text;
        qreal px;
        int weight;
        qreal chrome;
    };
    const Sample samples[] = {
        {"Ctrl", 11.5, 600, 20.5},       {"Shift", 11.5, 600, 26.2},    {"Alt", 11.5, 600, 15.7},
        {"Calibrated", 11.5, 600, 57.8}, {"Calibrate...", 12.5, 500, 64.8}, {"Check now", 12.5, 500, 66.5},
        {"Help", 12.5, 500, 27.5},       {"Add drive...", 13, 500, 70.9}, {"github.com/legandrop", 13, 500, 137.9},
        {"Put the pointer over a knob in Nuke and press Ctrl+Shift+D to set a key.", 13, 400, 439.3},
        {"Calibrate the Dope Sheet once: one click on an empty spot.", 13, 400, 364.9},
    };
    std::printf("measure mode=fonts platform=%s dpi=%.1f\n", qPrintable(QGuiApplication::platformName()),
                QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->logicalDotsPerInchY() : 0.0);
    for (const Sample &s : samples) {
        const QFont font = Theme::uiFont(s.px, s.weight);
        QFont noHint = font;
        noHint.setHintingPreference(QFont::PreferNoHinting);
        const qreal qt = QFontMetricsF(font).horizontalAdvance(QString::fromLatin1(s.text));
        const qreal qtNoHint = QFontMetricsF(noHint).horizontalAdvance(QString::fromLatin1(s.text));
        std::printf("measure font px=%.1f w=%d text='%s' chrome=%.1f qt=%.2f (%+.1f%%) qt_sin_hinting=%.2f (%+.1f%%) "
                    "pixelSizeResuelto=%d\n",
                    s.px, s.weight, s.text, s.chrome, qt, (qt / s.chrome - 1) * 100, qtNoHint,
                    (qtNoHint / s.chrome - 1) * 100, QFontInfo(font).pixelSize());
    }
    std::fflush(stdout);
    return 0;
}

} // namespace Measure
