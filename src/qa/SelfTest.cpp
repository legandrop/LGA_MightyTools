#include "qa/SelfTest.h"

#include "app/ExternalDispatch.h"
#include "app/HostServices.h"
#include "app/HotkeyHub.h"
#include "app/ModuleContextImpl.h"
#include "app/ModuleHost.h"
#include "app/ModuleRegistry.h"
#include "app/SettingsStore.h"
#include "core/AppPaths.h"
#include "core/BuildTree.h"

#include <QCoreApplication>
#include <QEvent>
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

    // La logica de cada herramienta, con sus propios casos negativos.
    for (const ModuleDescriptor &d : ModuleRegistry::all()) {
        if (!d.selfTest) {
            continue;
        }
        const QString prefix = QStringLiteral("[%1] ").arg(d.id);
        d.selfTest([&check, prefix](bool ok, const QString &what) { check(ok, prefix + what); });
    }

    std::printf("%s: %d fallas\n", failures == 0 ? "self-test ok" : "self-test FALLO", failures);
    return failures == 0 ? 0 : 1;
}

} // namespace SelfTest
