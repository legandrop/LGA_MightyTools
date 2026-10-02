#include "qa/SelfTest.h"

#include "app/ExternalDispatch.h"
#include "app/HostServices.h"
#include "app/HotkeyHub.h"
#include "app/ModuleContextImpl.h"
#include "app/MainWindow.h"
#include "app/ModuleHost.h"
#include "app/ModuleRegistry.h"
#include "app/SettingsStore.h"
#include "core/AppPaths.h"
#include "core/BuildTree.h"
#include "core/I18n.h"
#include "core/UiScale.h"
#include "modules/diskspace/cleanup/CleanupWindow.h"
#include "modules/openinnukex/OpenInNukeXOperations.h"
#include "platform/ForegroundWatcher.h"
#include "platform/ScreenInfo.h"
#include "platform/ProcessStats.h"
#include "platform/SystemNotifier.h"
#include "platform/ToastActivation.h"

#ifdef Q_OS_WIN
#include "qa/RegistryHiveTest.h"
#endif

#include <QXmlStreamReader>
#include <QCoreApplication>
#include <QImageReader>
#include <QImage>
#include <QFile>
#include <QEvent>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QRegularExpression>
#include <QPointer>
#include <QThread>
#include <QTimer>

#include <cstdio>
#include <functional>

namespace {

using Check = std::function<void(bool ok, const QString &what)>;

void flushDeletes()
{
    // Los deleteLater del host (modulo y panel) corren aca, como en el ciclo de eventos.
    for (int i = 0; i < 3; ++i) {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::sendPostedEvents();
    }
}

int qAppTimers()
{
    return int(qApp->findChildren<QTimer *>().size());
}

int qAppThreads()
{
    return int(qApp->findChildren<QThread *>().size());
}

// Lo que el modulo de prueba vio en su destructor: prueba que el contexto seguia vivo.
QString g_contextIdInDestructor;

// Modulo de prueba bien portado: timer e hilo propios, atajo declarado y registrado, inyector.
class ProbeModule : public Module
{
public:
    ProbeModule(ModuleContext &context, const Shortcut &shortcut, bool hideWindow)
        : Module(context)
        , m_shortcut(shortcut)
        , m_hideWindow(hideWindow)
    {
    }
    ~ProbeModule() override
    {
        stop();
        g_contextIdInDestructor = context().moduleId();
    }

    void start() override
    {
        if (m_started) {
            return;
        }
        m_started = true;
        m_timer = new QTimer(this);
        m_timer->start(60000);
        m_thread = new QThread(this);
        m_thread->start();
        m_declared = context().hotkeys()->declare(1, m_shortcut);
        m_registered = context().hotkeys()->registerHotkey(1, m_shortcut);
        context().injector();
        context().setValue(QStringLiteral("probe/started"), true);
        // En corrida automatizada una notificacion solo se loguea: nunca llega a la bandeja.
        context().notify(QStringLiteral("Probe"), QStringLiteral("Automated"), ModuleContext::NoticeIcon::Warning, 1000);
        if (m_hideWindow) {
            // Nunca la devuelve: la red de seguridad del contexto tiene que hacerlo.
            context().hideWindowTemporarily();
        }
    }
    void stop() override
    {
        if (!m_started) {
            return;
        }
        m_started = false;
        m_timer->stop();
        m_thread->quit();
        m_thread->wait();
        context().hotkeys()->unregisterAll();
    }
    ModuleStatus status() const override { return {ModuleTone::Active, QStringLiteral("On")}; }
    QWidget *createPanel(QWidget *) override { return nullptr; }
    QStringList captureStates() const override { return {QStringLiteral("on")}; }
    bool applyCaptureState(const QString &state) override { return state == QLatin1String("on"); }

    bool started() const { return m_started; }
    QString declaredResult() const { return m_declared; }
    bool registered() const { return m_registered; }
    ModuleHotkeys *hotkeys() { return context().hotkeys(); }

private:
    Shortcut m_shortcut;
    bool m_hideWindow = false;
    bool m_started = false;
    QTimer *m_timer = nullptr;
    QThread *m_thread = nullptr;
    QString m_declared;
    bool m_registered = false;
};

// Modulo mal portado: deja un QTimer colgado de qApp. La medicion tiene que verlo (caso negativo).
class LeakyModule : public Module
{
public:
    using Module::Module;
    void start() override { m_leak = new QTimer(qApp); }
    void stop() override {}
    ModuleStatus status() const override { return {ModuleTone::Active, QStringLiteral("On")}; }
    QWidget *createPanel(QWidget *) override { return nullptr; }
    QPointer<QTimer> m_leak;
};

// La ventana de mentira del self-test: cuenta cuantas veces se escondio y se devolvio.
class FakeServices : public HostServices
{
public:
    void notify(const QString &, const QString &, const QString &, ModuleContext::NoticeIcon, int) override { ++notified; }
    void showPanel(const QString &) override {}
    bool hideWindow() override
    {
        ++hidden;
        return true;
    }
    void showWindow() override { ++shown; }
    QWidget *window() const override { return nullptr; }
    int notified = 0;
    int hidden = 0;
    int shown = 0;
};

ModuleDescriptor probeDescriptor(const QString &id, const QString &title, const Shortcut &shortcut, bool hideWindow)
{
    ModuleDescriptor d;
    d.id = id;
    d.title = title;
    d.description = QStringLiteral("Probe");
    d.create = [shortcut, hideWindow](ModuleContext &context) -> std::unique_ptr<Module> {
        return std::make_unique<ProbeModule>(context, shortcut, hideWindow);
    };
    return d;
}

void testHost(const Check &check)
{
    const Shortcut k = Shortcut::fromPortableString(QStringLiteral("Ctrl+Alt+K"));
    const Shortcut j = Shortcut::fromPortableString(QStringLiteral("Ctrl+Alt+J"));

    QList<ModuleDescriptor> descriptors;
    descriptors << probeDescriptor(QStringLiteral("probeA"), QStringLiteral("Probe A"), k, true);
    descriptors << probeDescriptor(QStringLiteral("probeB"), QStringLiteral("Probe B"), j, false);
    // C nunca se prende: solo aporta su atajo configurado.
    ModuleDescriptor c = probeDescriptor(QStringLiteral("probeC"), QStringLiteral("Probe C"), j, false);
    c.configuredShortcuts = [](const SettingsReader &value) {
        return QList<Shortcut>{Shortcut::fromPortableString(value(QStringLiteral("shortcut"), QStringLiteral("Ctrl+Alt+J")).toString())};
    };
    descriptors << c;
    ModuleDescriptor leaky;
    leaky.id = QStringLiteral("leaky");
    leaky.title = QStringLiteral("Leaky");
    leaky.create = [](ModuleContext &context) -> std::unique_ptr<Module> { return std::make_unique<LeakyModule>(context); };
    descriptors << leaky;

    MemorySettingsStore store;
    HostOptions options;
    options.automatedRun = true;
    FakeServices services;
    ModuleHost host(descriptors, &store, options);
    host.setHostServices(&services);

    const int timers0 = qAppTimers();
    const int threads0 = qAppThreads();

    check(!host.isRunning(QStringLiteral("probeA")) && host.module(QStringLiteral("probeA")) == nullptr,
          QStringLiteral("host: apagada de fabrica, sin objeto (D-06)"));

    host.setEnabled(QStringLiteral("probeA"), true);
    auto *a = static_cast<ProbeModule *>(host.module(QStringLiteral("probeA")));
    QPointer<Module> aGuard(a);
    check(a && a->started(), QStringLiteral("host: prender construye y llama start()"));
    check(store.value(QStringLiteral("modules/probeA/enabled")).toBool(), QStringLiteral("host: [modules]/probeA/enabled queda en true"));
    check(store.value(QStringLiteral("probeA/probe/started")).toBool(), QStringLiteral("host: el modulo escribe en SU seccion [probeA]"));
    check(a && a->declaredResult().isEmpty() && a->registered(), QStringLiteral("atajos: A declara y registra Ctrl+Alt+K"));
    check(host.hotkeyHub()->registeredCount() == 1, QStringLiteral("atajos: 1 registrado en el host"));
    check(!host.hotkeyHub()->systemServiceAlive(), QStringLiteral("atajos: en corrida automatizada no se crea el servicio del sistema"));
    check(host.injectorAlive(), QStringLiteral("inyector: creado con el primer pedido"));
    check(services.hidden == 1, QStringLiteral("ventana: A la escondio"));
    check(services.notified == 0, QStringLiteral("notificaciones: en corrida automatizada no llegan a la bandeja"));

    host.setEnabled(QStringLiteral("probeB"), true);
    auto *b = static_cast<ProbeModule *>(host.module(QStringLiteral("probeB")));
    QPointer<Module> bGuard(b);
    check(b && b->declaredResult().isEmpty(), QStringLiteral("atajos: B declara Ctrl+Alt+J aunque C (apagada) lo tenga configurado"));
    check(b && b->hotkeys()->declare(2, k) == QLatin1String("Probe A"),
          QStringLiteral("choque: B no puede declarar Ctrl+Alt+K, gana A que declaro primero"));
    check(b && !b->hotkeys()->registerHotkey(2, k), QStringLiteral("choque: B no registra Ctrl+Alt+K (sin llamar al sistema)"));
    check(b && b->hotkeys()->declaredByOtherModule(k) == QLatin1String("Probe A"),
          QStringLiteral("choque: el grabador de B ve \"Already used by Probe A\""));
    const Shortcut n = Shortcut::fromPortableString(QStringLiteral("Ctrl+Alt+N"));
    check(b && b->hotkeys()->declare(4, n).isEmpty() && b->hotkeys()->declaredByOtherModule(n).isEmpty(),
          QStringLiteral("choque: su propio atajo no cuenta como de otra"));
    const Shortcut l = Shortcut::fromPortableString(QStringLiteral("Ctrl+Alt+L"));
    store.setValue(QStringLiteral("probeC/shortcut"), QStringLiteral("Ctrl+Alt+L"));
    check(b && b->hotkeys()->declaredByOtherModule(l) == QLatin1String("Probe C"),
          QStringLiteral("choque: los atajos configurados de una apagada cuentan para el grabador"));
    check(b && b->hotkeys()->declare(3, l).isEmpty(), QStringLiteral("choque: declare() compite solo contra las prendidas"));
    check(host.hotkeyHub()->registeredCount() == 2, QStringLiteral("atajos: 2 registrados con A y B"));

    host.setEnabled(QStringLiteral("probeA"), false);
    check(host.module(QStringLiteral("probeA")) == nullptr, QStringLiteral("host: apagar deja el puntero nulo en el acto"));
    check(!store.value(QStringLiteral("modules/probeA/enabled")).toBool(), QStringLiteral("host: [modules]/probeA/enabled queda en false"));
    flushDeletes();
    check(aGuard.isNull(), QStringLiteral("host: el modulo apagado se borra (deleteLater)"));
    check(g_contextIdInDestructor == QLatin1String("probeA"), QStringLiteral("host: el contexto sigue vivo durante el destructor del modulo"));
    check(services.shown == 1, QStringLiteral("ventana: el contexto devuelve la ventana que el modulo escondio y no devolvio"));
    check(b && b->hotkeys()->declare(2, k).isEmpty(), QStringLiteral("choque: apagada A, B ya puede declarar Ctrl+Alt+K"));

    host.setEnabled(QStringLiteral("probeB"), false);
    flushDeletes();
    check(bGuard.isNull() && host.runningCount() == 0, QStringLiteral("host: B apagada y borrada"));
    check(host.hotkeyHub()->registeredCount() == 0 && host.hotkeyHub()->declaredCount() == 0,
          QStringLiteral("atajos: 0 registrados y 0 declarados con todo apagado"));
    check(host.hotkeyHub()->clientCount() == 0, QStringLiteral("atajos: ningun modulo tiene atajos pedidos"));
    check(!host.injectorAlive(), QStringLiteral("inyector: se destruye cuando lo suelta el ultimo"));
    check(qAppTimers() == timers0 && qAppThreads() == threads0,
          QStringLiteral("consumo: QTimer y QThread de qApp vuelven a los iniciales (%1/%2 -> %3/%4)")
              .arg(timers0).arg(threads0).arg(qAppTimers()).arg(qAppThreads()));

    // 20 ciclos: nada crece.
    for (int i = 0; i < 20; ++i) {
        host.setEnabled(QStringLiteral("probeA"), true);
        host.setEnabled(QStringLiteral("probeA"), false);
    }
    flushDeletes();
    check(qAppTimers() == timers0 && qAppThreads() == threads0 && host.hotkeyHub()->registeredCount() == 0,
          QStringLiteral("consumo: 20 ciclos de prender y apagar no dejan nada"));

    // Caso negativo: un modulo que deja un timer en qApp. La guarda tiene que verlo.
    host.setEnabled(QStringLiteral("leaky"), true);
    QPointer<QTimer> leak = static_cast<LeakyModule *>(host.module(QStringLiteral("leaky")))->m_leak;
    host.setEnabled(QStringLiteral("leaky"), false);
    flushDeletes();
    check(qAppTimers() != timers0, QStringLiteral("negativo: la medicion detecta un QTimer que queda en qApp"));
    delete leak.data();
    check(qAppTimers() == timers0, QStringLiteral("negativo: limpio el timer de prueba"));

    // Arranque: prende lo que settings.ini dice.
    store.setValue(QStringLiteral("modules/probeB/enabled"), true);
    host.startEnabled();
    check(host.isRunning(QStringLiteral("probeB")) && !host.isRunning(QStringLiteral("probeA")),
          QStringLiteral("host: startEnabled prende solo las marcadas"));
    host.shutdown();
    check(host.runningCount() == 0 && store.value(QStringLiteral("modules/probeB/enabled")).toBool(),
          QStringLiteral("host: shutdown apaga sin tocar lo que eligio el usuario"));
}

void testCapture(const Check &check)
{
    QList<ModuleDescriptor> descriptors;
    descriptors << probeDescriptor(QStringLiteral("probeA"), QStringLiteral("Probe A"),
                                   Shortcut::fromPortableString(QStringLiteral("Ctrl+Alt+K")), false);
    MemorySettingsStore store;
    HostOptions options;
    options.captureMode = true;
    ModuleHost host(descriptors, &store, options);
    check(!host.enableForCapture(QStringLiteral("probeA"), QStringLiteral("no-such-state")),
          QStringLiteral("captura: un estado que no existe se rechaza"));
    check(host.enableForCapture(QStringLiteral("probeA"), QStringLiteral("on")), QStringLiteral("captura: estado conocido"));
    auto *a = static_cast<ProbeModule *>(host.module(QStringLiteral("probeA")));
    check(a && !a->started(), QStringLiteral("captura: el modulo se construye sin start()"));
    check(!host.enableForCapture(QStringLiteral("nope"), QString()), QStringLiteral("captura: herramienta desconocida"));
}

void testPersistentRegistration(const Check &check)
{
    check(!ModuleContextImpl::persistentAllowed(false, true), QStringLiteral("registro persistente: prohibido en un arbol de build"));
    check(!ModuleContextImpl::persistentAllowed(true, false), QStringLiteral("registro persistente: prohibido en corrida automatizada"));
    check(ModuleContextImpl::persistentAllowed(false, false), QStringLiteral("registro persistente: permitido en la copia instalada"));

    // Por el contexto real, con un host que corre desde un arbol de build.
    QList<ModuleDescriptor> descriptors;
    descriptors << probeDescriptor(QStringLiteral("probeA"), QStringLiteral("Probe A"), Shortcut(), false);
    MemorySettingsStore store;
    HostOptions options;
    options.buildTree = true;
    ModuleHost host(descriptors, &store, options);
    ModuleContextImpl context(&host, QStringLiteral("probeA"), QStringLiteral("Probe A"), 0);
    check(!context.persistentRegistrationAllowed() && !context.automatedRun(),
          QStringLiteral("registro persistente: el contexto de un build no es automatizado pero no registra"));
    std::printf("info arbol de build de este exe: %s\n", LgaBuildTree::isBuildTree(AppPaths::exeDir()) ? "si" : "no");
}

void testExternal(const Check &check)
{
    QList<ModuleDescriptor> descriptors;
    ModuleDescriptor ext;
    ext.id = QStringLiteral("ext");
    ext.title = QStringLiteral("External probe");
    ext.claimsExternal = [](const QString &argument) { return argument.startsWith(QLatin1String("test://")); };
    bool sawEnabled = false;
    bool sawDryRun = false;
    QString sawValue;
    ext.runExternal = [&](const ExternalRequest &request) -> ExternalResult {
        sawEnabled = request.moduleEnabled;
        sawDryRun = request.dryRun;
        sawValue = request.value(QStringLiteral("browser"), QString()).toString();
        if (request.argument == QLatin1String("test://done")) {
            return ExternalResult::Done;
        }
        if (request.argument == QLatin1String("test://pending")) {
            auto finished = request.finished;
            QTimer::singleShot(50, qApp, [finished]() { finished(7); });
            return ExternalResult::Pending;
        }
        if (request.argument == QLatin1String("test://hang")) {
            return ExternalResult::Pending; // nunca llama a finished
        }
        return ExternalResult::NotMine;
    };
    descriptors << ext;
    MemorySettingsStore store;
    store.setValue(QStringLiteral("modules/ext/enabled"), true);
    store.setValue(QStringLiteral("ext/browser"), QStringLiteral("chrome"));

    check(ExternalDispatch::firstPlainArgument({QStringLiteral("app.exe"), QStringLiteral("--dry-run-input"), QStringLiteral("C:/a.nk")})
              == QLatin1String("C:/a.nk"),
          QStringLiteral("modo corto: el primer argumento que no es un flag"));
    check(ExternalDispatch::firstPlainArgument({QStringLiteral("app.exe")}).isEmpty(), QStringLiteral("modo corto: sin argumentos, nada"));
    check(ExternalDispatch::claimant(descriptors, QStringLiteral("C:/a.txt")) == nullptr,
          QStringLiteral("modo corto: una entrada que nadie reclama sigue a la instancia unica"));

    ExternalDispatch::Outcome done = ExternalDispatch::run(descriptors, QStringLiteral("test://done"), &store, true, 1000);
    check(done.handled && done.exitCode == 0 && !done.timedOut, QStringLiteral("modo corto: Done sale 0 sin esperar"));
    check(sawEnabled && sawDryRun && sawValue == QLatin1String("chrome"),
          QStringLiteral("modo corto: el pedido lleva enabled, dryRun y la seccion del modulo"));

    ExternalDispatch::Outcome pending = ExternalDispatch::run(descriptors, QStringLiteral("test://pending"), &store, true, 2000);
    check(pending.handled && pending.exitCode == 7 && !pending.timedOut,
          QStringLiteral("modo corto: Pending espera a finished(7) y sale con 7"));

    ExternalDispatch::Outcome hang = ExternalDispatch::run(descriptors, QStringLiteral("test://hang"), &store, true, 200);
    check(hang.handled && hang.exitCode == 1 && hang.timedOut, QStringLiteral("modo corto: Pending sin finished sale con 1 al tope"));

    ExternalDispatch::Outcome notMine = ExternalDispatch::run(descriptors, QStringLiteral("test://other"), &store, true, 200);
    check(!notMine.handled, QStringLiteral("modo corto: NotMine sigue a la instancia unica"));
}

// Cada herramienta registrada, de verdad (en corrida automatizada: atajos contados, inyector en
// solo loguear): prender y apagar 20 veces no deja objeto, timers, hilos, atajos ni inyector.
void testRealModules(const Check &check)
{
    MemorySettingsStore store;
    HostOptions options;
    options.automatedRun = true;
    // El hook de ventana al frente de verdad (solo observa): sus objetos del sistema tienen que volver.
    options.observeForeground = true;
    ModuleHost host(ModuleRegistry::all(), &store, options);
    const int timers0 = qAppTimers();
    const int threads0 = qAppThreads();
    for (const ModuleDescriptor &d : host.descriptors()) {
        // Un ciclo de calentamiento: lo que Qt carga una sola vez por proceso no es del modulo.
        host.setEnabled(d.id, true);
        host.setEnabled(d.id, false);
        flushDeletes();
        const ProcessStats before = ProcessStats::current();
        bool alwaysBuilt = true;
        for (int i = 0; i < 20; ++i) {
            host.setEnabled(d.id, true);
            alwaysBuilt = alwaysBuilt && host.isRunning(d.id);
            host.setEnabled(d.id, false);
        }
        flushDeletes();
        check(alwaysBuilt && host.module(d.id) == nullptr && host.runningCount() == 0,
              QStringLiteral("[%1] 20 ciclos: se construye al prender y queda nulo al apagar").arg(d.id));
        check(host.hotkeyHub()->registeredCount() == 0 && host.hotkeyHub()->declaredCount() == 0
                  && host.hotkeyHub()->clientCount() == 0 && !host.injectorAlive(),
              QStringLiteral("[%1] apagada: 0 atajos registrados o declarados, sin inyector").arg(d.id));
        check(qAppTimers() == timers0 && qAppThreads() == threads0,
              QStringLiteral("[%1] QTimer y QThread de qApp vuelven a los iniciales (%2/%3 -> %4/%5)")
                  .arg(d.id).arg(timers0).arg(threads0).arg(qAppTimers()).arg(qAppThreads()));
        // Lo que Qt no ve: handles, hilos y objetos GDI/USER del sistema (un timer del sistema que
        // queda vivo es un objeto USER).
        const ProcessStats after = ProcessStats::current();
        const auto near = [](qint64 a, qint64 b) { return a < 0 || b < 0 || qAbs(a - b) <= 2; };
        check(near(before.handles, after.handles) && near(before.threads, after.threads)
                  && near(before.gdiObjects, after.gdiObjects) && near(before.userObjects, after.userObjects),
              QStringLiteral("[%1] 20 ciclos: handles %2, hilos %3, GDI %4, USER %5 (tolerancia 2)")
                  .arg(d.id)
                  .arg(after.handles - before.handles)
                  .arg(after.threads - before.threads)
                  .arg(after.gdiObjects - before.gdiObjects)
                  .arg(after.userObjects - before.userObjects));
    }
}

// Plan 4.4: un solo hook de ventana al frente para todas las herramientas. Con el observador real
// (solo observa): Nuke Shortcuts y Folder Switch prendidos -> 1 hook; los dos apagados -> 0.
void testSharedForeground(const Check &check)
{
    MemorySettingsStore store;
    HostOptions options;
    options.automatedRun = true;
    options.observeForeground = true;
    ModuleHost host(ModuleRegistry::all(), &store, options);
    const QString ns = QStringLiteral("nukeShortcuts");
    const QString fs = QStringLiteral("folderSwitch");
    if (!host.descriptor(ns) || !host.descriptor(fs)) {
        std::printf("info sin Folder Switch en esta plataforma: el conteo de hooks se prueba solo con Nuke Shortcuts\n");
    }
    const int hooks0 = ForegroundWatcher::installedHooks();
    check(hooks0 == 0, QStringLiteral("ventana al frente: 0 hooks antes de prender nada (%1)").arg(hooks0));
    host.setEnabled(ns, true);
    check(ForegroundWatcher::installedHooks() == 1 && host.foregroundAlive(),
          QStringLiteral("ventana al frente: Nuke Shortcuts prendido -> 1 hook (%1)").arg(ForegroundWatcher::installedHooks()));
    if (host.descriptor(fs)) {
        host.setEnabled(fs, true);
        check(ForegroundWatcher::installedHooks() == 1,
              QStringLiteral("ventana al frente: con Folder Switch tambien, sigue 1 hook (%1)").arg(ForegroundWatcher::installedHooks()));
        host.setEnabled(ns, false);
        flushDeletes();
        check(ForegroundWatcher::installedHooks() == 1 && host.foregroundAlive(),
              QStringLiteral("ventana al frente: apagado Nuke Shortcuts, Folder Switch conserva el hook"));
        host.setEnabled(fs, false);
    } else {
        host.setEnabled(ns, false);
    }
    flushDeletes();
    check(ForegroundWatcher::installedHooks() == 0 && !host.foregroundAlive(),
          QStringLiteral("ventana al frente: los dos apagados -> 0 hooks y sin servicio (%1)").arg(ForegroundWatcher::installedHooks()));
    // Negativo: dos observadores sueltos son dos hooks. El contador los ve.
    {
        ForegroundWatcher first(true);
        ForegroundWatcher second(true);
        check(ForegroundWatcher::installedHooks() == 2, QStringLiteral("negativo: dos observadores sueltos cuentan 2 hooks"));
    }
    check(ForegroundWatcher::installedHooks() == 0, QStringLiteral("negativo: al destruirlos vuelve a 0"));
}

// Notificaciones (SystemNotifier, copia de PipeSync): en corrida automatizada no se lanza nada, y el
// icono del toast es el frame MAS GRANDE del .ico escrito como PNG (no el primero, que es 16x16).
void testNotifier(const Check &check)
{
    QFile::remove(SystemNotifier::iconTempPath(true));
    SystemNotifier notifier(true);
    check(!notifier.workerRunning(), QStringLiteral("notificaciones: sin avisos no hay hilo"));
    notifier.show(QStringLiteral("Frame Dope Sheet"), QStringLiteral("It's \"quoted\""));
    const SystemNotifier::Last last = notifier.last();
    check(!last.launched && !notifier.workerRunning(),
          QStringLiteral("notificaciones: en corrida automatizada no se lanza PowerShell ni se crea el hilo"));
#ifdef Q_OS_WIN
    // El PNG del icono es del toast de Windows; en macOS el aviso lleva el icono que pone el sistema.
    QImageReader reader(QStringLiteral(":/icons/LGA_MightyTools.ico"));
    int largest = 0;
    for (int i = 0; i < qMax(1, reader.imageCount()); ++i) {
        if (i > 0 && !reader.jumpToImage(i)) {
            break;
        }
        largest = qMax(largest, reader.read().width());
    }
    const QImage written(last.icon.path);
    check(largest > 16 && last.icon.size.width() == largest && written.width() == largest,
          QStringLiteral("notificaciones: el PNG del toast es el frame mas grande del .ico (%1 px de %2 frames; escrito %3)")
              .arg(largest).arg(last.icon.frames).arg(written.width()));
    notifier.show(QStringLiteral("Again"), QStringLiteral("Body"));
    check(notifier.last().icon.reused, QStringLiteral("notificaciones: el PNG reciente se reusa"));
#endif
    check(SystemNotifier::escapeForScript(QStringLiteral("It's")) == QLatin1String("It''s"),
          QStringLiteral("notificaciones: las comillas simples se duplican para PowerShell"));
    check(SystemNotifier::escapeForScript(QStringLiteral("It’s")) == QStringLiteral("It’’s"),
          QStringLiteral("notificaciones: la comilla tipografica tambien se duplica"));

    // El XML del aviso con desplegable: bien formado, con el texto escapado, y "reminder" solo con
    // desplegable.
    SystemNotifier::Notice notice;
    notice.title = QStringLiteral("C: is <low> & \"full\"");
    notice.body = QStringLiteral("35 GB free of 930 GB. It's %1");
    notice.launch = QStringLiteral("module=diskSpace&action=open");
    notice.persistent = true;
    const QString plain = SystemNotifier::toastXml(notice, QStringLiteral("C:\\x\\icon.png"));
    check(!plain.contains(QLatin1String("scenario")) && !plain.contains(QLatin1String("<actions")),
          QStringLiteral("aviso: sin desplegable no hay acciones ni modo recordatorio"));
    notice.choiceLabel = QStringLiteral("Remind me again in");
    notice.choices = {{QStringLiteral("15"), QStringLiteral("15 min")}, {QStringLiteral("60"), QStringLiteral("1 hour")}};
    notice.choiceDefault = QStringLiteral("60");
    notice.button = QStringLiteral("Remind me");
    notice.buttonArguments = QStringLiteral("module=diskSpace&action=snooze&key=C%3A%2F");
    const QString xml = SystemNotifier::toastXml(notice, QStringLiteral("C:\\x\\icon.png"));
    QXmlStreamReader xmlReader(xml);
    QString title;
    QString defaultInput;
    QString buttonArguments;
    QString launch;
    QString scenario;
    int selections = 0;
    while (!xmlReader.atEnd()) {
        if (xmlReader.readNext() != QXmlStreamReader::StartElement) {
            continue;
        }
        const QXmlStreamAttributes attributes = xmlReader.attributes();
        if (xmlReader.name() == QLatin1String("toast")) {
            launch = attributes.value(QLatin1String("launch")).toString();
            scenario = attributes.value(QLatin1String("scenario")).toString();
        } else if (xmlReader.name() == QLatin1String("text") && title.isEmpty()) {
            title = xmlReader.readElementText();
        } else if (xmlReader.name() == QLatin1String("input")) {
            defaultInput = attributes.value(QLatin1String("defaultInput")).toString();
        } else if (xmlReader.name() == QLatin1String("selection")) {
            ++selections;
        } else if (xmlReader.name() == QLatin1String("action") && buttonArguments.isEmpty()) {
            buttonArguments = attributes.value(QLatin1String("arguments")).toString();
        }
    }
    check(!xmlReader.hasError(), QStringLiteral("aviso: el XML esta bien formado (%1)").arg(xmlReader.errorString()));
    check(title == notice.title && launch == notice.launch && buttonArguments == notice.buttonArguments,
          QStringLiteral("aviso: titulo, click y boton vuelven intactos del XML (& < > comillas)"));
    check(selections == 2 && defaultInput == QLatin1String("60") && scenario == QLatin1String("reminder"),
          QStringLiteral("aviso: dos opciones, la preseleccionada y el modo recordatorio"));
    notifier.show(notice);
    check(!notifier.last().launched && notifier.last().notice.choices.size() == 2,
          QStringLiteral("aviso con desplegable: en corrida automatizada no se lanza ni se anota nada"));
    check(ToastActivation::isActivationArgument(QStringLiteral("-Embedding"))
              && ToastActivation::isActivationArgument(QStringLiteral("/embedding"))
              && ToastActivation::isActivationArgument(QStringLiteral("--toast-activated"))
              && !ToastActivation::isActivationArgument(QStringLiteral("C:/shots/a.nk")),
          QStringLiteral("aviso: -Embedding y --toast-activated no son un archivo ni un link"));
    QFile::remove(SystemNotifier::iconTempPath(true));
}

// Idioma: la tabla ingles -> espanol, el camino de un texto en espanol hasta el aviso de Windows.
// Lista ORDENADA (multiconjunto): "%1 ... %1" y "%1" no son lo mismo.
QStringList placeholdersOf(const QString &text)
{
    QStringList found;
    static const QRegularExpression placeholder(QStringLiteral("%[1-9]"));
    auto it = placeholder.globalMatch(text);
    while (it.hasNext()) {
        found.append(it.next().captured());
    }
    found.sort();
    return found;
}

#ifdef Q_OS_WIN
// Lo que PowerShell lee entre comillas simples a partir de `marker`: una comilla (recta o tipografica,
// U+2018 a U+201B) seguida de otra es una sola; una sola cierra la cadena. Es la misma regla que
// SystemNotifier::escapeForScript duplica.
QString powerShellQuoted(const QString &script, const QString &marker)
{
    const int start = script.indexOf(marker);
    if (start < 0) {
        return QString();
    }
    const auto isQuote = [](QChar c) { return c == QLatin1Char('\'') || (c.unicode() >= 0x2018 && c.unicode() <= 0x201B); };
    QString out;
    for (int i = start + marker.size(); i < script.size(); ++i) {
        const QChar c = script.at(i);
        if (isQuote(c)) {
            if (i + 1 < script.size() && isQuote(script.at(i + 1))) {
                ++i;
                out += script.at(i);
                continue;
            }
            return out;
        }
        out += c;
    }
    return QString();
}
#endif

// Lee un toast: los <text> y los textos de action / selection / input.
bool readToast(const QString &document, QStringList *texts, QStringList *contents)
{
    QXmlStreamReader reader(document);
    while (!reader.atEnd()) {
        if (reader.readNext() != QXmlStreamReader::StartElement) {
            continue;
        }
        if (reader.name() == QLatin1String("text")) {
            texts->append(reader.readElementText());
        } else if (reader.name() == QLatin1String("action") || reader.name() == QLatin1String("selection")) {
            contents->append(reader.attributes().value(QLatin1String("content")).toString());
        } else if (reader.name() == QLatin1String("input")) {
            contents->append(reader.attributes().value(QLatin1String("title")).toString());
        }
    }
    return !reader.hasError();
}

#ifdef Q_OS_WIN
// Lo "en curso" de Open in NukeX vive fuera del panel (OpenInNukeXOperations). Nada de esto escribe el registro ni
// lanza procesos: la desinstalacion corre en seco (solo lee) y el Apply se marca "en curso" con un hook de prueba.
void testOperations(const Check &check)
{
    using Ops = OpenInNukeXOperations;
    const auto flushDeferred = []() {
        for (int i = 0; i < 3; ++i) {
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }
    };
    const auto waitIdle = [](int limitMs) {
        QElapsedTimer waited;
        waited.start();
        while (Ops::busy() && waited.elapsed() < limitMs) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        return !Ops::busy();
    };

    check(Ops::existing() == nullptr && !Ops::busy(), QStringLiteral("operaciones: sin panel ni operacion el singleton ni existe"));
    Ops::shutdownIfIdle(); // sin singleton: no hace nada
    check(Ops::existing() == nullptr, QStringLiteral("operaciones: apagar sin singleton no lo crea"));

    // 1. La desinstalacion (en seco) queda en curso, no deja lanzar otra ni un Apply, y su resultado espera.
    Ops *operations = Ops::instance();
    operations->startUninstall(false, /*automatedRun=*/true);
    check(operations->uninstallRunning() && Ops::busy(),
          QStringLiteral("operaciones: la desinstalacion queda en curso aunque no haya panel"));
    operations->startApply(false, nullptr); // no debe lanzar nada: hay una desinstalacion en curso
    check(!operations->applyRunning(), QStringLiteral("operaciones: con una desinstalacion en curso no arranca un Apply"));
    check(waitIdle(15000), QStringLiteral("operaciones: la desinstalacion de prueba termino"));
    check(Ops::existing() == operations, QStringLiteral("operaciones: con la herramienta prendida el singleton sigue"));

    // 2. El resultado que llego sin panel sobrevive a dos rearmados seguidos (un panel que se conecta y muere sin
    // llegar a tomarlo no lo pierde) y se entrega una sola vez.
    {
        QObject firstPanel;
        QObject::connect(operations, &Ops::uninstallFinished, &firstPanel, [](bool, bool) {});
    }
    {
        QObject secondPanel;
        QObject::connect(operations, &Ops::uninstallFinished, &secondPanel, [](bool, bool) {});
    }
    check(operations->takePendingUninstall().valid,
          QStringLiteral("operaciones: el resultado pendiente sobrevive a dos rearmados seguidos"));
    check(!operations->takePendingUninstall().valid, QStringLiteral("operaciones: y se entrega una sola vez"));

    // 3. Apagar la herramienta descarta lo pendiente y destruye el singleton si no hay nada en curso.
    operations->startUninstall(false, true);
    check(waitIdle(15000), QStringLiteral("operaciones: segunda desinstalacion de prueba"));
    Ops::shutdownIfIdle();
    check(Ops::existing() == nullptr, QStringLiteral("operaciones: apagar sin nada en curso destruye el singleton"));
    flushDeferred();
    operations = Ops::instance();
    check(!operations->takePendingUninstall().valid && !operations->takePendingApply().valid,
          QStringLiteral("operaciones: apagar descarto los resultados pendientes"));

    // 4. Apagar con algo en curso: se destruye solo al terminar, y nadie recibe el resultado.
    operations->startUninstall(false, true);
    Ops::shutdownIfIdle();
    check(Ops::existing() == operations && Ops::busy(),
          QStringLiteral("operaciones: apagar con algo en curso NO lo destruye todavia"));
    check(waitIdle(15000), QStringLiteral("operaciones: lo que estaba en curso termino"));
    flushDeferred();
    check(Ops::existing() == nullptr, QStringLiteral("operaciones: y el singleton se destruyo solo al terminar"));

    // 5. Volver a usarlo (la herramienta se prendio de nuevo) cancela la destruccion diferida.
    operations = Ops::instance();
    operations->startUninstall(false, true);
    Ops::shutdownIfIdle();
    Ops *again = Ops::instance();
    check(again == operations, QStringLiteral("operaciones: instance() devuelve el mismo mientras sigue en curso"));
    check(waitIdle(15000), QStringLiteral("operaciones: termino"));
    flushDeferred();
    check(Ops::existing() == operations && operations->takePendingUninstall().valid,
          QStringLiteral("operaciones: si se volvio a usar, no se destruye y el resultado espera al panel"));

    // 6. "Release .nk association" no escribe mientras hay un Apply en curso (marcado por el hook, sin correrlo).
    const QList<ModuleDescriptor> all = ModuleRegistry::all();
    const ModuleDescriptor *nuke = nullptr;
    for (const ModuleDescriptor &d : all) {
        if (d.id == QLatin1String("openInNukeX")) {
            nuke = &d;
        }
    }
    operations->markApplyRunningForTest(true);
    QString error;
    const bool released = nuke && nuke->releaseSystem ? nuke->releaseSystem(&error) : true;
    check(nuke && !released && error == QLatin1String("An association change is still running. Try again in a moment."),
          QStringLiteral("operaciones: release .nk se rechaza con un mensaje claro mientras hay un Apply en curso (%1)").arg(error));
    operations->startUninstall(false, true);
    check(!operations->uninstallRunning(), QStringLiteral("operaciones: con un Apply en curso no arranca una desinstalacion"));
    operations->markApplyRunningForTest(false);
    Ops::shutdownIfIdle();
    flushDeferred();
    check(Ops::existing() == nullptr, QStringLiteral("operaciones: limpio al final"));

    // 7. Apagar la herramienta en medio de un Apply con su hilo de verdad (el Apply de prueba duerme y no toca
    //    el registro): el objeto se borra solo al terminar y nunca destruye un QThread vivo (antes abortaba
    //    con "QThread: Destroyed while thread is still running"). El apagado cae antes, durante y justo
    //    despues del final del hilo.
    Ops::useFakeApplyForTest(20);
    int survivors = 0;
    for (int round = 0; round < 12; ++round) {
        Ops::instance()->startApply(false, nullptr);
        QTimer::singleShot((round % 6) * 6, []() { Ops::shutdownIfIdle(); });
        QEventLoop loop;
        QTimer::singleShot(90, &loop, &QEventLoop::quit);
        loop.exec();
        flushDeferred();
        if (Ops::existing() != nullptr) {
            ++survivors;
            Ops::shutdownIfIdle();
            flushDeferred();
        }
    }
    Ops::useFakeApplyForTest(-1);
    check(survivors == 0, QStringLiteral("operaciones: apagar en medio de un Apply real (hilo de prueba) no aborta y el objeto se borra (%1 de 12 sobrevivieron)").arg(survivors));
}
#endif

// Tamano de la interfaz (core/UiScale.h): niveles, factores y la variable de entorno que solo vive
// hasta que existe la QApplication.
void testUiScale(const Check &check)
{
    check(qFuzzyCompare(UiScale::factor(0), 1.0) && qFuzzyCompare(UiScale::factor(1), 1.1) && qFuzzyCompare(UiScale::factor(2), 1.2),
          QStringLiteral("tamano de interfaz: 0, 1 y 2 valen 1.0, 1.1 y 1.2"));
    check(UiScale::clampLevel(-1) == 1 && UiScale::clampLevel(3) == 1 && qFuzzyCompare(UiScale::factor(7), 1.1),
          QStringLiteral("tamano de interfaz: un nivel fuera de rango vale como el de fabrica (1)"));
    check(UiScale::readSavedLevel() == UiScale::kDefaultLevel && UiScale::kDefaultLevel == 1, QStringLiteral("tamano de interfaz: sin settings.ini (en memoria) arranca en 1"));
    // Limite por pantalla: 960 x 676 por el factor tiene que entrar en el area util.
    check(UiScale::maxFittingLevel(QSize(1920, 1032)) == 2 && UiScale::maxFittingLevel(QSize(3440, 1392)) == 2,
          QStringLiteral("tamano de interfaz: en 1920x1080 y 3440x1440 entran los tres"));
    check(UiScale::maxFittingLevel(QSize(1400, 760)) == 1, QStringLiteral("tamano de interfaz: con 760 de alto util, hasta el 1"));
    check(UiScale::maxFittingLevel(QSize(1366, 728)) == 0 && UiScale::fitLevel(1, QSize(1366, 728)) == 0,
          QStringLiteral("tamano de interfaz: en 1366x768 solo el 0 (ni el 1 de fabrica)"));
    check(UiScale::fitLevel(2, QSize(1400, 760)) == 1 && UiScale::fitLevel(0, QSize(1366, 728)) == 0,
          QStringLiteral("tamano de interfaz: lo guardado se recorta, nunca se agranda"));
    check(UiScale::maxFittingLevel(QSize()) == UiScale::kMaxLevel, QStringLiteral("tamano de interfaz: sin dato de pantalla no se limita"));
    // Bordes exactos: 960 x 676 por 1,1 = 1056 x 743,6 y por 1,2 = 1152 x 811,2.
    check(UiScale::maxFittingLevel(QSize(1056, 744)) == 1 && UiScale::maxFittingLevel(QSize(1056, 743)) == 0
              && UiScale::maxFittingLevel(QSize(1152, 812)) == 2 && UiScale::maxFittingLevel(QSize(1151, 812)) == 1,
          QStringLiteral("tamano de interfaz: los bordes exactos de cada nivel"));
#ifdef Q_OS_WIN
    // En Windows siempre hay pantalla (en mac, una sesion sin servidor de ventanas no tiene ninguna).
    const QSize screenArea = ScreenInfo::primaryAvailableSize();
    check(screenArea.width() >= 640 && screenArea.height() >= 400,
          QStringLiteral("tamano de interfaz: area util de la pantalla principal leida (%1x%2, hasta el nivel %3)")
              .arg(screenArea.width()).arg(screenArea.height()).arg(UiScale::maxFittingLevel(screenArea)));
#endif
    const QSize largest = UiScale::largestWindow();
    check(MainWindow::kWidth <= largest.width() && MainWindow::kHeight <= largest.height()
              && CleanupWindow::kWidth <= largest.width() && CleanupWindow::kHeight <= largest.height(),
          QStringLiteral("tamano de interfaz: ninguna ventana es mas grande que largestWindow()"));
    const int before = UiScale::sessionLevel();
    const bool hadEnv = qEnvironmentVariableIsSet("QT_SCALE_FACTOR");
    if (!UiScale::supported()) {
        // macOS (D-45): ningun nivel toca el entorno y la sesion queda en el tamano del diseno.
        UiScale::applyBeforeApp(2);
        check(!qEnvironmentVariableIsSet("QT_SCALE_FACTOR") && UiScale::sessionLevel() == 0,
              QStringLiteral("tamano de interfaz: en la Mac no se ofrece y ningun nivel fija QT_SCALE_FACTOR"));
        UiScale::clearEnvironmentAfterApp();
    } else if (!hadEnv) {
        UiScale::applyBeforeApp(2);
        check(qgetenv("QT_SCALE_FACTOR") == "1.20" && UiScale::sessionLevel() == 2,
              QStringLiteral("tamano de interfaz: el 2 fija QT_SCALE_FACTOR=1.20 antes de la app"));
        UiScale::clearEnvironmentAfterApp();
        check(!qEnvironmentVariableIsSet("QT_SCALE_FACTOR") && UiScale::sessionLevel() == 2,
              QStringLiteral("tamano de interfaz: la variable se saca (no la heredan NukeX ni el navegador) y el nivel queda"));
        UiScale::applyBeforeApp(0);
        check(!qEnvironmentVariableIsSet("QT_SCALE_FACTOR"), QStringLiteral("tamano de interfaz: el 0 no toca el entorno"));
        UiScale::applyBeforeApp(before);
        UiScale::clearEnvironmentAfterApp();
    }
}

void testI18n(const Check &check)
{
    const I18n::Language before = I18n::language();
    const auto &table = I18n::spanishTable();
    check(!table.isEmpty(), QStringLiteral("idioma: la tabla tiene %1 entradas").arg(table.size()));

    check(placeholdersOf(QStringLiteral("%1 a %1")) != placeholdersOf(QStringLiteral("%1")) && placeholdersOf(QStringLiteral("%2 %1")) == placeholdersOf(QStringLiteral("%1 %2")),
          QStringLiteral("idioma: la comparacion de marcadores distingue \"%1 ... %1\" de \"%1\" y no depende del orden"));
    int emptyTranslations = 0;
    int placeholderMismatches = 0;
    QStringList offenders;
    static const QRegularExpression contextPrefix(QStringLiteral("^[a-z]+\\|"));
    for (auto it = table.constBegin(); it != table.constEnd(); ++it) {
        QString english = it.key();
        english.remove(contextPrefix);
        if (it.value().trimmed().isEmpty()) {
            ++emptyTranslations;
            offenders << it.key();
        } else if (placeholdersOf(english) != placeholdersOf(it.value())) {
            ++placeholderMismatches;
            offenders << it.key();
        }
    }
    check(emptyTranslations == 0, QStringLiteral("idioma: ninguna traduccion vacia (%1)").arg(emptyTranslations));
    check(placeholderMismatches == 0,
          QStringLiteral("idioma: cada traduccion lleva los mismos %1..%9 que su clave (distintas: %2)")
              .arg(QStringLiteral("%"), offenders.join(QStringLiteral(" | "))));

    I18n::setLanguage(I18n::Language::English);
    check(I18n::tr("Check now") == QLatin1String("Check now")
              && I18n::trc("tool", "%1 is off") == QLatin1String("%1 is off"),
          QStringLiteral("idioma: en ingles el texto sale igual"));
    I18n::setLanguage(I18n::Language::Spanish);
    check(I18n::tr("Check now") == QStringLiteral("Buscar ahora")
              && I18n::trc("tool", "%1 is off") == QStringLiteral("%1 está apagada")
              && I18n::trc("shortcut", "%1 is off") == QStringLiteral("%1 desactivado")
              && I18n::trc("tool", "Off") == QStringLiteral("Apagada")
              && I18n::trc("chip", "Off") == QStringLiteral("Inactivo"),
          QStringLiteral("idioma: en espanol sale la traduccion y los homonimos se separan por contexto"));
    check(I18n::tr("Text that is not in the table") == QLatin1String("Text that is not in the table"),
          QStringLiteral("idioma: lo que falta en la tabla queda en ingles, nunca vacio"));

    // El camino de un texto en espanol hasta el aviso de Windows: acentos, enes, apertura de
    // interrogacion, comillas angulares, raya y comillas (rectas y tipograficas) llegan enteros.
    SystemNotifier::Notice notice;
    notice.title = QStringLiteral("Poco espacio en C: — ¿qué pasó? «Recordarme»");
    notice.body = QStringLiteral("Quedan 35 GB libres de 930 GB. Niño d'Ávila ‘simple’ “doble” & <x>");
    notice.launch = QStringLiteral("module=diskSpace&action=open");
    notice.choiceLabel = QStringLiteral("Recordarme de nuevo en");
    notice.choices = {{QStringLiteral("15"), QStringLiteral("15 min")}, {QStringLiteral("60"), QStringLiteral("1 hora")}};
    notice.choiceDefault = QStringLiteral("60");
    notice.button = QStringLiteral("Recordarme");
    notice.buttonArguments = QStringLiteral("module=diskSpace&action=snooze&key=C%3A%2F");
    notice.persistent = true;
    const QString xml = SystemNotifier::toastXml(notice, QString());
    QStringList texts;
    QStringList contents;
    const bool wellFormed = readToast(xml, &texts, &contents);
    const QStringList expectedContents{notice.choiceLabel, QStringLiteral("15 min"), QStringLiteral("1 hora"),
                                       notice.button, QStringLiteral("Descartar")};
    check(wellFormed && texts == QStringList({notice.title, notice.body}),
          QStringLiteral("aviso en espanol: el XML esta bien formado y titulo y cuerpo vuelven intactos"));
    check(contents == expectedContents,
          QStringLiteral("aviso en espanol: desplegable, opciones, boton y Descartar llegan enteros (%1)")
              .arg(contents.join(QStringLiteral(" | "))));

#ifdef Q_OS_WIN
    const QString script = SystemNotifier::registeredToastScript(notice, QStringLiteral("C:\\x\\icon.png"),
                                                                 QStringLiteral("C:\\x\\app's.exe"));
    const QString embedded = powerShellQuoted(script, QStringLiteral("$toastXml.LoadXml('"));
    QStringList scriptTexts;
    QStringList scriptContents;
    const bool scriptXmlOk = readToast(embedded, &scriptTexts, &scriptContents);
    check(scriptXmlOk && scriptTexts == texts && scriptContents == contents,
          QStringLiteral("aviso en espanol: el script de PowerShell lleva el mismo XML despues de sus comillas"));
    const QByteArray decoded = QByteArray::fromBase64(SystemNotifier::encodeCommand(script).toLatin1());
    const QString roundTrip = QString::fromUtf16(reinterpret_cast<const char16_t *>(decoded.constData()),
                                                 decoded.size() / 2);
    check(roundTrip == script, QStringLiteral("aviso en espanol: -EncodedCommand (UTF-16LE base64) vuelve identico"));
    const QString plainScript = SystemNotifier::plainToastScript(notice.title, notice.body, QString(),
                                                                 QStringLiteral("C:\\x\\app.exe"));
    check(powerShellQuoted(plainScript, QStringLiteral("InnerText = '")) == notice.title,
          QStringLiteral("aviso en espanol sin anotar: el titulo llega entero al script de -Command"));
#endif

    // Cambio de idioma con la app abierta: el host vuelve a tomar los textos de los descriptores (titulo,
    // descripcion, vinetas) y las vinetas de Folder Switch salen con el atajo CONFIGURADO.
    {
        I18n::setLanguage(I18n::Language::English);
        MemorySettingsStore store;
        HostOptions options;
        options.captureMode = true;
        ModuleHost host(ModuleRegistry::all(), &store, options);
        const ModuleDescriptor *disk = host.descriptor(QStringLiteral("diskSpace"));
        const QString before = disk ? disk->description : QString();
        I18n::setLanguage(I18n::Language::Spanish);
        host.refreshDescriptorTexts(ModuleRegistry::all());
        disk = host.descriptor(QStringLiteral("diskSpace"));
        check(disk && before.startsWith(QLatin1String("Watches your local drives"))
                  && disk->description.startsWith(QStringLiteral("Vigila los discos locales")),
              QStringLiteral("idioma: el host retoma los textos de los descriptores al cambiar de idioma"));
#ifdef Q_OS_WIN
        const ModuleDescriptor *folder = host.descriptor(QStringLiteral("folderSwitch"));
        if (folder && folder->offBulletsFor) {
            const QStringList bullets = folder->offBulletsFor([](const QString &key, const QVariant &fallback) {
                return key == QLatin1String("shortcuts/manual") ? QVariant(QStringLiteral("Ctrl+Alt+K")) : fallback;
            });
            check(bullets.size() == 3 && bullets.at(1) == QStringLiteral("Dos atajos: Ctrl+Alt+K y Ctrl+Alt+Shift+O"),
                  QStringLiteral("Folder Switch: la vineta de atajos muestra el configurado (%1)")
                      .arg(bullets.value(1)));
        } else {
            check(false, QStringLiteral("Folder Switch: el descriptor trae offBulletsFor"));
        }
#endif
    }

    I18n::setLanguage(before);
}

} // namespace

namespace SelfTest {

int run()
{
    int failures = 0;
    const Check check = [&failures](bool ok, const QString &what) {
        std::printf("%s %s\n", ok ? "ok  " : "FAIL", qPrintable(what));
        std::fflush(stdout);
        if (!ok) {
            ++failures;
        }
    };

    testHost(check);
    testCapture(check);
    testPersistentRegistration(check);
    testExternal(check);
    testRealModules(check);
    testSharedForeground(check);
    testNotifier(check);
    testI18n(check);
    testUiScale(check);
#ifdef Q_OS_WIN
    testOperations(check);
#endif

    // La logica de cada herramienta, con sus propios casos negativos.
    for (const ModuleDescriptor &d : ModuleRegistry::all()) {
        if (!d.selfTest) {
            continue;
        }
        const QString prefix = QStringLiteral("[%1] ").arg(d.id);
        d.selfTest([&check, prefix](bool ok, const QString &what) { check(ok, prefix + what); });
    }

#ifdef Q_OS_WIN
    // Registro y --uninstall-cleanup sobre un hive privado (nunca el HKCU real).
    RegistryHiveTest::run([&check](bool ok, const QString &what) { check(ok, QStringLiteral("[registro] ") + what); });
#endif

    std::printf("%s: %d fallas\n", failures == 0 ? "self-test ok" : "self-test FALLO", failures);
    return failures == 0 ? 0 : 1;
}

} // namespace SelfTest
