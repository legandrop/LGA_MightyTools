#include "modules/mousebuttons/MouseButtonsModule.h"

#include "app/ModuleContext.h"
#include "core/I18n.h"
#include "modules/mousebuttons/MouseButtonsPanel.h"
#include "platform/ForegroundWatcher.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QPainter>
#include <QWidget>

#include <cstdio>

using MouseButtonsSystem::Button;
using MouseButtonsSystem::Event;
using MouseButtonsSystem::Machine;
using MouseButtonsSystem::Result;
using MouseButtonsSystem::Rule;
using MouseButtonsSystem::Step;

namespace {

// Un mouse visto de arriba: el cuerpo y la ruedita. Se lee a 16 px.
void paintMouseButtonsIcon(QPainter &painter, const QRectF &rect, const QColor &color)
{
    const qreal scale = qMin(rect.width(), rect.height()) / 16.0;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.translate(rect.center().x() - 8 * scale, rect.center().y() - 8 * scale);
    painter.scale(scale, scale);
    painter.setPen(QPen(color, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(3.3, 1.3, 9.4, 13.4), 4.7, 4.7);
    painter.drawLine(QPointF(8, 4.2), QPointF(8, 6.9));
    painter.restore();
}

// ---- QA: contexto de mentira (nunca toca el sistema)

class FakeContext : public ModuleContext
{
public:
    QString moduleId() const override { return QStringLiteral("mouseButtons"); }
    QString moduleTitle() const override { return QStringLiteral("Mouse Buttons"); }
    QVariant value(const QString &key, const QVariant &fallback = {}) const override { return m_values.value(key, fallback); }
    void setValue(const QString &key, const QVariant &value) override { m_values.insert(key, value); }
    void removeValue(const QString &key) override { m_values.remove(key); }
    bool captureMode() const override { return false; }
    bool dryRunInput() const override { return true; }
    bool automatedRun() const override { return true; }
    bool persistentRegistrationAllowed() const override { return false; }
    ModuleHotkeys *hotkeys() override { return nullptr; }
    InputInjector *injector() override { return nullptr; }
    ForegroundWatcher *foreground() override { ++requests; return &watcher; }
    void notify(const QString &, const QString &, NoticeIcon, int) override {}
    void showPanel() override {}
    void hideWindowTemporarily() override {}
    void restoreWindow() override {}
    QWidget *window() const override { return nullptr; }

    ForegroundWatcher watcher{false}; // sin gancho: el self-test avisa el frente a mano
    int requests = 0;

private:
    QHash<QString, QVariant> m_values;
};

Step down(int key) { return {Step::KeyDown, key}; }
Step up(int key) { return {Step::KeyUp, key}; }
const Step kLeftDown{Step::LeftDown, 0};
const Step kLeftUp{Step::LeftUp, 0};

Event event(Button button, bool isDown, bool overTarget = true, bool ownInjected = false)
{
    Event e;
    e.button = button;
    e.down = isDown;
    e.overTarget = overTarget;
    e.ownInjected = ownInjected;
    return e;
}

void selfTest(const std::function<void(bool, const QString &)> &check)
{
    // ---- La maquina de estados, con secuencias de eventos armadas a mano.
    const Rule photoshop; // boton del medio -> Espacio + clic izquierdo (la regla de fabrica)
    Machine machine({photoshop});
    Result r = machine.handle(event(Button::Middle, true));
    check(r.swallow && r.steps == QList<Step>{down(Qt::Key_Space), kLeftDown} && machine.holding(),
          QStringLiteral("maquina: el boton de la regla sobre la app se traga e inyecta Espacio y despues el clic"));
    r = machine.handle(event(Button::Middle, false, false));
    check(r.swallow && r.steps == QList<Step>{kLeftUp, up(Qt::Key_Space)} && !machine.holding(),
          QStringLiteral("maquina: soltarlo (aunque el puntero ya este en otra app) suelta en orden inverso"));
    r = machine.handle(event(Button::Middle, true, false));
    check(!r.swallow && r.steps.isEmpty(), QStringLiteral("maquina: sobre otra app (o con otra al frente) el clic pasa intacto"));
    r = machine.handle(event(Button::Middle, false));
    check(!r.swallow && r.steps.isEmpty(), QStringLiteral("maquina: soltar sin haber tomado la apretada pasa intacto"));
    r = machine.handle(event(Button::Back, true));
    Result r2 = machine.handle(event(Button::Back, false));
    check(!r.swallow && !r2.swallow && r.steps.isEmpty() && r2.steps.isEmpty(),
          QStringLiteral("maquina: otro boton pasa intacto"));
    r = machine.handle(event(Button::Middle, true, true, true));
    check(!r.swallow && r.steps.isEmpty() && !machine.holding(),
          QStringLiteral("maquina: un evento inyectado por esta app nunca se toma"));
    machine.handle(event(Button::Middle, true));
    r = machine.handle(event(Button::Middle, true));
    r2 = machine.handle(event(Button::Middle, false));
    Result r3 = machine.handle(event(Button::Middle, false));
    check(r.swallow && r.steps.isEmpty() && r2.swallow && r2.steps.size() == 2 && !r3.swallow,
          QStringLiteral("maquina: dos apretadas seguidas no aprietan dos veces; una soltada suelta y la siguiente pasa"));
    machine.handle(event(Button::Middle, true));
    const QList<Step> released = machine.releaseAll();
    r = machine.handle(event(Button::Middle, false));
    check(released == QList<Step>{kLeftUp, up(Qt::Key_Space)} && !machine.holding() && !r.swallow && machine.releaseAll().isEmpty(),
          QStringLiteral("maquina: cambio de frente a mitad suelta todo; la soltada fisica despues pasa y no queda nada"));
    machine.handle(event(Button::Middle, true));
    r = machine.handle(event(Button::Middle, false, true, true));
    check(!r.swallow && machine.holding() && machine.releaseAll().size() == 2,
          QStringLiteral("maquina: una soltada inyectada por esta app no suelta la regla"));

    Rule chord;
    chord.button = Button::Forward;
    chord.keys = {Qt::ControlModifier | Qt::ShiftModifier | Qt::MetaModifier, Qt::Key_F5};
    chord.holdLeft = false;
    Machine two({photoshop, chord});
    r = two.handle(event(Button::Forward, true));
    check(r.swallow && r.steps == QList<Step>{down(Qt::Key_Control), down(Qt::Key_Shift), down(Qt::Key_Meta), down(Qt::Key_F5)},
          QStringLiteral("maquina: modificadores primero (Ctrl, Shift, Win), despues la tecla, sin clic si no se pide"));
    two.handle(event(Button::Middle, true));
    const QList<Step> all = two.releaseAll();
    check(all.size() == 6 && all.contains(up(Qt::Key_Meta)) && all.contains(kLeftUp) && !two.holding(),
          QStringLiteral("maquina: soltar todo suelta los dos botones que estaban apretados"));
    check(Machine(QList<Rule>()).handle(event(Button::Middle, true)).steps.isEmpty(),
          QStringLiteral("maquina: sin reglas (regla apagada) no toma nada"));

    // ---- Lo que se mandaria a Windows: Espacio por su tecla virtual y la marca propia; soltar Win, con la tecla muda.
    const QStringList inputs = MouseButtonsSystem::describeInputs({down(Qt::Key_Space), kLeftDown, up(Qt::Key_Meta)});
    check(inputs.size() == 5 && inputs.at(0).contains(QStringLiteral("vk=0x20")) && inputs.at(0).contains(QStringLiteral("marca=0x4c47414d"))
              && inputs.at(1).startsWith(QStringLiteral("mouse")) && inputs.at(2).contains(QStringLiteral("vk=0xe8"))
              && inputs.at(4).contains(QStringLiteral("vk=0x5b")) && inputs.at(4).contains(QStringLiteral("arriba")),
          QStringLiteral("inyeccion: Espacio 0x20 con la marca propia, el clic, y la tecla muda antes de soltar Win (%1)")
              .arg(inputs.join(QStringLiteral(" | "))));

    // ---- Settings: ida y vuelta, nombres de app y reglas invalidas.
    check(MouseButtonsState::appName(QStringLiteral("C:\\Program Files\\Adobe\\Adobe Photoshop 2025\\Photoshop.exe")) == QLatin1String("Photoshop.exe")
              && MouseButtonsState::appName(QStringLiteral(" photoshop.EXE ")) == QLatin1String("photoshop.EXE")
              && MouseButtonsState::appName(QStringLiteral("Photoshop")).isEmpty()
              && MouseButtonsState::appName(QStringLiteral("a|b.exe")).isEmpty()
              && MouseButtonsState::sameApp(QStringLiteral("PHOTOSHOP.exe"), QStringLiteral("Photoshop.exe")),
          QStringLiteral("estado: el nombre del exe sale de una ruta o del nombre solo y se compara sin mayusculas"));
    MouseButtonsState state;
    MouseRule rule;
    rule.app = QStringLiteral("Photoshop.exe");
    rule.path = QStringLiteral("C:/Program Files/Adobe/Adobe Photoshop 2025/Photoshop.exe");
    check(state.upsert(rule) && rule.id == 1, QStringLiteral("estado: la primera regla toma el id 1"));
    MouseRule second;
    second.app = QStringLiteral("notepad.exe");
    second.button = Button::Back;
    second.keys = {Qt::ControlModifier, Qt::Key_Space};
    second.holdLeft = false;
    second.enabled = false;
    check(state.upsert(second) && second.id == 2, QStringLiteral("estado: la segunda toma el id 2"));
    const QList<MouseRule> loaded = MouseButtonsState::fromJson(MouseButtonsState::toJson(state.rules()));
    check(loaded.size() == 2 && loaded.at(0).app == rule.app && loaded.at(0).path == rule.path
              && loaded.at(0).button == Button::Middle && loaded.at(0).keys == Shortcut{Qt::NoModifier, Qt::Key_Space}
              && loaded.at(0).holdLeft && loaded.at(0).enabled && loaded.at(1).button == Button::Back
              && loaded.at(1).keys == second.keys && !loaded.at(1).holdLeft && !loaded.at(1).enabled && loaded.at(1).path.isEmpty(),
          QStringLiteral("estado: JSON ida y vuelta con ruta, boton, Espacio solo, Ctrl+Espacio, sin clic y apagada (%1)")
              .arg(MouseButtonsState::toJson(state.rules())));
    MouseRule clash = rule;
    clash.id = 0;
    clash.app = QStringLiteral("PHOTOSHOP.EXE");
    clash.path.clear();
    check(!state.upsert(clash) && clash.id == 0 && state.rules().size() == 2,
          QStringLiteral("estado: otra regla para el mismo boton de la misma app se rechaza sin gastar un id"));
    clash.button = Button::Forward;
    check(state.upsert(clash) && state.remove(clash.id) && !state.remove(clash.id),
          QStringLiteral("estado: otro boton de la misma app se acepta; borrar libera la regla"));
    MouseRule bad = second;
    bad.id = 0;
    bad.button = Button::Forward;
    bad.app = QStringLiteral("notepad");
    const bool notExe = !state.upsert(bad);
    bad.app = QStringLiteral("C:/Windows/notepad.exe");
    const bool withFolder = !state.upsert(bad);
    bad.app = QStringLiteral("notepad.exe");
    bad.keys = Shortcut();
    const bool noKeys = !state.upsert(bad);
    bad.keys = {Qt::NoModifier, Qt::Key_Escape};
    const bool escape = !state.upsert(bad);
    bad.keys = {Qt::NoModifier, Qt::Key_Space};
    bad.path = QStringLiteral("C:/Windows/explorer.exe");
    const bool otherPath = !state.upsert(bad);
    check(notExe && withFolder && noKeys && escape && otherPath && state.rules().size() == 2,
          QStringLiteral("estado: sin .exe, con carpeta en el nombre, sin teclas, con Esc o con la ruta de otro exe no se guardan"));
    check(MouseButtonsState::fromJson(QStringLiteral("{\"id\":1}")).isEmpty()
              && MouseButtonsState::fromJson(QStringLiteral("no es json")).isEmpty()
              && MouseButtonsState::fromJson(QStringLiteral("[{\"id\":1,\"app\":\"a.exe\",\"button\":\"wheel\",\"keys\":\"Space\"}]")).isEmpty()
              && MouseButtonsState::fromJson(QStringLiteral("[{\"id\":1,\"app\":\"a.exe\",\"button\":\"middle\",\"keys\":\"Ctrl+Alt\"}]")).isEmpty()
              && MouseButtonsState::fromJson(QStringLiteral("[{\"id\":1,\"app\":\"a.exe\",\"button\":\"middle\",\"keys\":\"Space\"},"
                                                            "{\"id\":1,\"app\":\"b.exe\",\"button\":\"middle\",\"keys\":\"Space\"},"
                                                            "{\"id\":2,\"app\":\"A.EXE\",\"button\":\"middle\",\"keys\":\"Space\"}]")).size() == 1,
          QStringLiteral("estado: settings corruptos, boton o teclas que no existen, id repetido o la misma app y boton no crean reglas"));
    check(MouseButtonsState::keysFromString(QStringLiteral("Space")) == Shortcut{Qt::NoModifier, Qt::Key_Space}
              && MouseButtonsState::keysFromString(QStringLiteral("Meta+Ctrl+Space")).modifiers == (Qt::ControlModifier | Qt::MetaModifier),
          QStringLiteral("estado: las teclas para mantener aceptan Espacio solo y con modificadores"));

    // ---- El modulo: el gancho solo con la app de una regla activada al frente, y nunca en una corrida automatizada.
    FakeContext context;
    {
        MouseButtonsModule module(context);
        module.start();
        check(context.requests == 0 && module.hookPid() == 0,
              QStringLiteral("modulo: prendido sin reglas no pide el observador del frente ni gancho"));
        MouseRule mine;
        mine.app = QStringLiteral("Example.exe");
        QString error;
        check(module.saveRule(mine, &error) && context.requests == 1, QStringLiteral("modulo: la primera regla pide el observador del frente"));
        module.onForeground(4242, QStringLiteral("EXAMPLE.EXE"));
        check(module.hookPid() == 4242 && MouseButtonsSystem::liveHooks() == 0,
              QStringLiteral("modulo: con su app al frente pide el gancho (en una corrida automatizada no se instala: %1 vivos)")
                  .arg(MouseButtonsSystem::liveHooks()));
        module.onForeground(77, QStringLiteral("explorer.exe"));
        check(module.hookPid() == 0, QStringLiteral("modulo: otra app al frente, sin gancho"));
        module.onForeground(4242, QStringLiteral("Example.exe"));
        const int id = module.rulesState().rules().first().id;
        check(module.setRuleEnabled(id, false) && module.hookPid() == 0,
              QStringLiteral("modulo: apagar la regla con su app al frente retira el gancho"));
        check(module.setRuleEnabled(id, true) && module.hookPid() == 4242, QStringLiteral("modulo: prenderla lo vuelve a pedir"));
        MouseRule dup = mine;
        dup.app = QStringLiteral("C:\\Tools\\example.EXE");
        check(!module.saveRule(dup, &error) && error == I18n::tr("This app already has a rule for that button."),
              QStringLiteral("modulo: la misma app y boton se rechaza con su motivo"));
        dup.app = QStringLiteral("example");
        check(!module.saveRule(dup, &error) && error == I18n::tr("Choose a program file (.exe)."),
              QStringLiteral("modulo: un nombre sin .exe se rechaza"));
        dup.app = QStringLiteral("C:/Tools/other.exe");
        dup.path = QStringLiteral("C:/Tools/other.exe");
        check(module.saveRule(dup, &error) && module.rulesState().rules().last().app == QLatin1String("other.exe")
                  && module.rulesState().rules().last().path == QLatin1String("C:/Tools/other.exe"),
              QStringLiteral("modulo: de una ruta guarda el nombre del exe y la ruta para el icono"));
        check(module.removeRule(module.rulesState().rules().last().id) && module.hookPid() == 4242,
              QStringLiteral("modulo: borrar otra regla no toca el gancho de la app al frente"));
        module.stop();
        check(module.hookPid() == 0 && MouseButtonsSystem::liveHooks() == 0,
              QStringLiteral("modulo: apagado no deja gancho ni hilo"));
        module.onForeground(4242, QStringLiteral("Example.exe"));
        check(module.hookPid() == 0, QStringLiteral("modulo: apagado no reacciona al cambio de frente"));
    }
    QString error;
    const bool refused = !MouseButtonsSystem::Hook::install({photoshop}, 4242, &error);
    check(refused && MouseButtonsSystem::liveHooks() == 0,
          QStringLiteral("gancho: en una corrida automatizada no se instala aunque se lo pida (%1)").arg(error));
}

int simulateAction(const QString &action, const QStringList &args)
{
    if (action != QLatin1String("simulate")) {
        return 2;
    }
    const QString app = MouseButtonsState::appName(args.value(0));
    const QString which = args.value(1, QStringLiteral("middle"));
    Rule rule;
    rule.button = which == QLatin1String("back") ? Button::Back : which == QLatin1String("forward") ? Button::Forward : Button::Middle;
    if (args.size() > 2 && !args.at(2).startsWith(QLatin1String("--"))) rule.keys = MouseButtonsState::keysFromString(args.at(2));
    rule.holdLeft = !args.contains(QStringLiteral("--no-left"));
    if (app.isEmpty() || !QStringList{QStringLiteral("middle"), QStringLiteral("back"), QStringLiteral("forward")}.contains(which)
        || !rule.keys.isValid()) {
        std::fprintf(stderr, "uso: --simulate-action mouseButtons:simulate <app.exe> <middle|back|forward> [teclas, Space por defecto] [--no-left]\n");
        return 1;
    }
    MouseRule shown;
    shown.button = rule.button;
    shown.keys = rule.keys;
    shown.holdLeft = rule.holdLeft;
    std::printf("regla: %s / %s -> %s\n", qPrintable(app), qPrintable(MouseButtonsModule::buttonText(rule.button)),
                qPrintable(MouseButtonsModule::summary(shown)));
    // La misma maquina que usa el gancho, con eventos armados: nada se instala ni se inyecta.
    Machine machine({rule});
    const auto run = [&machine](const char *what, const Event &e) {
        const Result result = machine.handle(e);
        std::printf("%s: %s\n", what, result.swallow ? "se traga" : "pasa intacto");
        for (const QString &line : MouseButtonsSystem::describeInputs(result.steps)) {
            std::printf("  SendInput %s\n", qPrintable(line));
        }
    };
    run("apretar sobre la app", event(rule.button, true));
    run("soltar", event(rule.button, false));
    run("apretar sobre otra app", event(rule.button, true, false));
    run("soltar", event(rule.button, false, false));
    run("otro boton sobre la app", event(rule.button == Button::Middle ? Button::Back : Button::Middle, true));
    std::printf("nada se inyecto: solo se describe lo que se mandaria\n");
    return 0;
}

} // namespace

MouseButtonsModule::MouseButtonsModule(ModuleContext &context)
    : Module(context), m_state(&context)
{
}

MouseButtonsModule::~MouseButtonsModule()
{
    stop();
}

void MouseButtonsModule::start()
{
    if (m_started) {
        return;
    }
    m_started = true;
    syncWatcher();
}

void MouseButtonsModule::stop()
{
    if (!m_started) {
        return;
    }
    m_started = false;
    syncWatcher();
}

void MouseButtonsModule::syncWatcher()
{
    bool any = false;
    for (const MouseRule &rule : m_state.rules()) any = any || rule.enabled;
    if (m_started && any && !m_watcher) {
        // El observador compartido del frente (uno por proceso para todas las herramientas) se pide con la
        // primera regla activada. Queda hasta apagar la herramienta: el contexto lo tiene igual hasta entonces.
        m_watcher = context().foreground();
        if (m_watcher) {
            connect(m_watcher, &ForegroundWatcher::foregroundChanged, this,
                    [this](quintptr, quint32 pid, const QString &exeName) { onForeground(pid, exeName); });
            m_frontPid = m_watcher->foregroundPid();
            m_frontExe = m_watcher->foregroundExeName();
        }
    } else if (!m_started && m_watcher) {
        disconnect(m_watcher, nullptr, this, nullptr);
        m_watcher = nullptr;
    }
    syncHook();
}

void MouseButtonsModule::onForeground(quint32 pid, const QString &exeName)
{
    m_frontPid = pid;
    m_frontExe = exeName;
    syncHook();
}

void MouseButtonsModule::syncHook()
{
    QList<Rule> rules;
    if (m_started && m_watcher && m_frontPid) {
        for (const MouseRule &rule : m_state.rules()) {
            if (rule.enabled && MouseButtonsState::sameApp(rule.app, m_frontExe)) rules.append(rule.rule());
        }
    }
    const quint32 pid = rules.isEmpty() ? 0 : m_frontPid;
    if (pid == m_hookPid && rules == m_hookRules) {
        return;
    }
    if (m_hookPid) qInfo() << "[mouseButtons] Gancho retirado del proceso" << m_hookPid;
    m_hook.reset(); // suelta todo lo que hubiera apretado
    m_hookPid = 0;
    m_hookRules.clear();
    if (!pid) {
        return;
    }
    if (context().automatedRun()) {
        qInfo().noquote() << "[mouseButtons] Corrida automatizada: el gancho para" << m_frontExe << "no se instala";
        m_hookPid = pid;
        m_hookRules = rules;
        return;
    }
    if (MouseButtonsSystem::injectionBlocked(pid)) {
        qInfo().noquote() << "[mouseButtons]" << m_frontExe << "corre como administrador: sus reglas no actuan";
        return;
    }
    QString error;
    m_hook = MouseButtonsSystem::Hook::install(rules, pid, &error);
    if (!m_hook) {
        qWarning().noquote() << "[mouseButtons] No se pudo instalar el gancho:" << error;
        m_lastError = I18n::tr("Windows did not let Mouse Buttons watch the mouse (error %1).").arg(error);
        emit statusChanged();
        emit rulesChanged();
        return;
    }
    qInfo().noquote() << "[mouseButtons] Gancho para" << m_frontExe << "proceso" << pid << "con" << rules.size() << "regla(s)";
    m_hookPid = pid;
    m_hookRules = rules;
    if (!m_lastError.isEmpty()) {
        m_lastError.clear();
        emit statusChanged();
        emit rulesChanged();
    }
}

void MouseButtonsModule::changed()
{
    syncWatcher();
    emit statusChanged();
    emit rulesChanged();
}

ModuleStatus MouseButtonsModule::status() const
{
    if (!m_lastError.isEmpty()) {
        return {ModuleTone::Error, I18n::tr("Needs attention")};
    }
    int count = 0;
    for (const MouseRule &rule : m_state.rules()) count += rule.enabled;
    if (count == 0) {
        return {ModuleTone::Active, m_state.rules().isEmpty() ? I18n::tr("No rules yet") : I18n::tr("All rules are off")};
    }
    return {ModuleTone::Active, count == 1 ? I18n::tr("1 rule on") : I18n::tr("%1 rules on").arg(count)};
}

QString MouseButtonsModule::buttonText(Button button)
{
    switch (button) {
    case Button::Back: return I18n::tr("Back button");
    case Button::Forward: return I18n::tr("Forward button");
    case Button::Middle: break;
    }
    return I18n::tr("Middle button");
}

QString MouseButtonsModule::summary(const MouseRule &rule)
{
    const QString keys = rule.keys.displayText();
    return rule.holdLeft ? I18n::tr("Holds %1 + left click").arg(keys) : I18n::tr("Holds %1").arg(keys);
}

QWidget *MouseButtonsModule::createPanel(QWidget *parent)
{
    auto *panel = new MouseButtonsPanel(this, !context().captureMode(), parent);
    connect(this, &MouseButtonsModule::rulesChanged, panel, &MouseButtonsPanel::refresh);
    if (m_captureState == QLatin1String("editor-new")) panel->showEditorForCapture(0, false);
    if (m_captureState == QLatin1String("editor")) panel->showEditorForCapture(1, false);
    if (m_captureState == QLatin1String("editor-menu")) panel->showEditorForCapture(1, true);
    return panel;
}

QStringList MouseButtonsModule::captureStates() const
{
    return {QStringLiteral("list"), QStringLiteral("empty"), QStringLiteral("editor-new"), QStringLiteral("editor"),
            QStringLiteral("editor-menu"), QStringLiteral("long-name")};
}

bool MouseButtonsModule::applyCaptureState(const QString &state)
{
    if (!context().captureMode() || !captureStates().contains(state)) {
        return false;
    }
    m_captureState = state;
    if (state == QLatin1String("empty")) {
        m_state.setCaptureRules({});
        return true;
    }
    // La regla de Photoshop (sin ruta: muestra el glifo) y otra apagada con una app que existe en todo equipo,
    // que muestra su icono real.
    MouseRule photoshop;
    photoshop.id = 1;
    photoshop.app = QStringLiteral("Photoshop.exe");
    MouseRule other;
    other.id = 2;
    other.app = QStringLiteral("notepad.exe");
    other.path = QStringLiteral("C:\\Windows\\notepad.exe");
    other.button = Button::Back;
    other.keys = {Qt::ControlModifier, Qt::Key_Space};
    other.holdLeft = false;
    other.enabled = false;
    if (state == QLatin1String("long-name")) {
        photoshop.app = QStringLiteral("Long program name that should leave the button and pencil visible.exe");
    }
    m_state.setCaptureRules({photoshop, other});
    return true;
}

QString MouseButtonsModule::validateRule(const MouseRule &rule) const
{
    if (rule.app.trimmed().isEmpty()) {
        return I18n::tr("Choose the app.");
    }
    const QString app = MouseButtonsState::appName(rule.app);
    if (app.isEmpty()) {
        return I18n::tr("Choose a program file (.exe).");
    }
    if (!MouseButtonsState::holdKey(rule.keys.key)) {
        return I18n::tr("Record the keys to hold.");
    }
    for (const MouseRule &stored : m_state.rules()) {
        if (stored.id != rule.id && stored.button == rule.button && MouseButtonsState::sameApp(stored.app, app)) {
            return I18n::tr("This app already has a rule for that button.");
        }
    }
    return {};
}

bool MouseButtonsModule::saveRule(MouseRule rule, QString *error)
{
    const QString invalid = validateRule(rule);
    if (!invalid.isEmpty()) {
        if (error) *error = invalid;
        return false;
    }
    rule.app = MouseButtonsState::appName(rule.app);
    // La ruta solo vale si es la de este exe (Browse... y despues se escribio otro nombre: sin icono).
    if (!MouseButtonsState::sameApp(MouseButtonsState::appName(rule.path), rule.app)
        || !QDir::isAbsolutePath(QDir::fromNativeSeparators(rule.path.trimmed()))) {
        rule.path.clear();
    }
    if (!m_state.upsert(rule)) {
        if (error) *error = I18n::tr("Check the app, or remove a rule to make room.");
        return false;
    }
    changed();
    return true;
}

bool MouseButtonsModule::setRuleEnabled(int id, bool enabled)
{
    for (MouseRule rule : m_state.rules()) {
        if (rule.id != id) continue;
        if (rule.enabled == enabled) return true;
        rule.enabled = enabled;
        if (!m_state.upsert(rule)) return false;
        changed();
        return true;
    }
    return false;
}

bool MouseButtonsModule::removeRule(int id)
{
    if (!m_state.remove(id)) {
        return false;
    }
    changed();
    return true;
}

ModuleDescriptor mouseButtonsDescriptor()
{
    ModuleDescriptor d;
    d.id = QStringLiteral("mouseButtons");
    d.title = QStringLiteral("Mouse Buttons");
    d.description = I18n::tr("Hold a mouse button to hold keys and the left button, app by app.");
    d.offBullets = {I18n::tr("Holds keys and the left button while you hold a mouse button"),
                    I18n::tr("Acts only in the apps you choose, while they're in front"),
                    I18n::tr("Other buttons and other apps are left as is")};
    d.platforms = PlatformWindows;
    d.paintIcon = &paintMouseButtonsIcon;
    d.create = [](ModuleContext &context) -> std::unique_ptr<Module> {
        return std::make_unique<MouseButtonsModule>(context);
    };
    d.selfTest = &selfTest;
    d.simulateAction = &simulateAction;
    return d;
}

HelpSection mouseButtonsHelp(const SettingsReader &)
{
    HelpSection help;
    help.title = QStringLiteral("Mouse Buttons");
    help.steps = {I18n::tr("Add a rule and choose the app's program file, or type its name, like Photoshop.exe."),
                  I18n::tr("Choose the mouse button and record the keys to hold. A single key, like Space, works."),
                  I18n::tr("In that app, holding the button holds the keys and the left button. Letting go releases them.")};
    help.note = I18n::tr("Rules don't act on apps opened as administrator. While typing text, like with Photoshop's text tool, Space types a space.");
    return help;
}
