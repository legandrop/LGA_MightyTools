#include "modules/apptoggle/AppToggleModule.h"

#include "app/ModuleContext.h"
#include "core/I18n.h"
#include "modules/apptoggle/AppTogglePanel.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QWidget>

#include <cstdio>

namespace {

// Una ventana y, al lado, una flecha arriba y abajo: la app que sube al frente y baja.
void paintAppToggleIcon(QPainter &painter, const QRectF &rect, const QColor &color)
{
    const qreal scale = qMin(rect.width(), rect.height()) / 16.0;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.translate(rect.center().x() - 8 * scale, rect.center().y() - 8 * scale);
    painter.scale(scale, scale);
    painter.setPen(QPen(color, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(1.6, 3.6, 8.4, 8.8), 1.5, 1.5);
    painter.drawLine(QPointF(1.6, 6.2), QPointF(10.0, 6.2));
    QPainterPath arrows;
    arrows.moveTo(13.3, 2.6);
    arrows.lineTo(13.3, 13.4);
    arrows.moveTo(11.6, 4.3);
    arrows.lineTo(13.3, 2.6);
    arrows.lineTo(15.0, 4.3);
    arrows.moveTo(11.6, 11.7);
    arrows.lineTo(13.3, 13.4);
    arrows.lineTo(15.0, 11.7);
    painter.drawPath(arrows);
    painter.restore();
}

// ---- QA: atajos y contexto de mentira (nunca tocan el sistema)

class FakeHotkeys : public ModuleHotkeys
{
public:
    bool accept = true;
    Shortcut takenByOther;
    QSet<int> registered;
    bool registerHotkey(int id, const Shortcut &) override { if (accept) registered.insert(id); return accept; }
    bool registerPriorityHotkey(int id, const Shortcut &) override { if (accept) registered.insert(id); return accept; }
    void unregisterHotkey(int id) override { registered.remove(id); }
    void unregisterAll() override { registered.clear(); }
    bool isRegistered(int id) const override { return registered.contains(id); }
    bool probe(const Shortcut &) override { return true; }
    void passThrough(const Shortcut &) override {}
    QString declare(int, const Shortcut &) override { return {}; }
    QString declaredByOtherModule(const Shortcut &shortcut) const override
    {
        return shortcut == takenByOther ? QStringLiteral("Window Layouts") : QString();
    }
    void fire(int id) { emit activated(id); }
};

class FakeContext : public ModuleContext
{
public:
    QString moduleId() const override { return QStringLiteral("appToggle"); }
    QString moduleTitle() const override { return QStringLiteral("App Toggle"); }
    QVariant value(const QString &key, const QVariant &fallback = {}) const override { return m_values.value(key, fallback); }
    void setValue(const QString &key, const QVariant &value) override { m_values.insert(key, value); }
    void removeValue(const QString &key) override { m_values.remove(key); }
    bool captureMode() const override { return false; }
    bool dryRunInput() const override { return true; }
    bool automatedRun() const override { return true; }
    bool persistentRegistrationAllowed() const override { return false; }
    ModuleHotkeys *hotkeys() override { return &fake; }
    InputInjector *injector() override { return nullptr; }
    ForegroundWatcher *foreground() override { return nullptr; }
    void notify(const QString &, const QString &, NoticeIcon, int) override {}
    void showPanel() override {}
    void hideWindowTemporarily() override {}
    void restoreWindow() override {}
    QWidget *window() const override { return nullptr; }

    FakeHotkeys fake;

private:
    QHash<QString, QVariant> m_values;
};

// La app que corre el self-test: existe en el disco, sirve de entrada valida para el modulo.
QString existingAppPath()
{
#ifdef Q_OS_MACOS
    QDir bundle(QCoreApplication::applicationDirPath()); // .../X.app/Contents/MacOS
    bundle.cdUp();
    bundle.cdUp();
    return bundle.absolutePath();
#else
    return QCoreApplication::applicationFilePath();
#endif
}

AppToggleSystem::Window window(quintptr id, const QString &path, bool visible = true)
{
    AppToggleSystem::Window window;
    window.id = id;
    window.processPath = path;
    window.visible = visible;
    return window;
}

void selfTest(const std::function<void(bool, const QString &)> &check)
{
    using namespace AppToggleSystem;
#ifdef Q_OS_MACOS
    const QString app = QStringLiteral("/Applications/Example Studio.app");
    const QString other = QStringLiteral("/Applications/Old/Example Studio.app");
#else
    const QString app = QStringLiteral("C:/Program Files/Example Studio/bin/64bit/example64.exe");
    const QString other = QStringLiteral("D:/Portable/Example Studio/example64.exe");
#endif

    // ---- La decision, con fotos del sistema armadas a mano.
    Snapshot nothing;
    check(decide(app, nothing).action == Action::Launch, QStringLiteral("decision: sin proceso la abre"));
    Snapshot elsewhere;
    elsewhere.processPaths << other;
    elsewhere.windows << window(1, other);
    check(decide(app, elsewhere).action == Action::Launch,
          QStringLiteral("decision: el mismo exe en otra carpeta no cuenta como abierta"));

    Snapshot tray;
    tray.processPaths << app;
    tray.windows << window(10, app, false);
    AppToggleSystem::Window tool = window(11, app);
    tool.toolWindow = true;
    tray.windows << tool;
    check(decide(app, tray).action == Action::Nothing,
          QStringLiteral("decision: proceso vivo sin ventana elegible (oculta o de herramientas) no hace nada"));

    Snapshot background;
    background.processPaths << app;
    AppToggleSystem::Window cloaked = window(20, app);
    cloaked.cloaked = true;
    AppToggleSystem::Window owned = window(21, app);
    owned.owned = true;
    background.windows << cloaked << owned << window(22, app) << window(23, app);
    background.foreground = 99;
    background.foregroundPath = other;
    Decision front = decide(app, background);
    check(front.action == Action::Activate && front.window == 22 && !front.restore,
          QStringLiteral("decision: de fondo la trae, la primera elegible en orden Z (saltea oculta por DWM y con dueno)"));

    Snapshot minimized = background;
    minimized.windows.clear();
    AppToggleSystem::Window icon = window(30, app);
    icon.minimized = true;
    minimized.windows << icon << window(31, app);
    front = decide(app, minimized);
    check(front.action == Action::Activate && front.window == 30 && front.restore,
          QStringLiteral("decision: la ultima usada minimizada se restaura"));
    icon.visible = false;
    minimized.windows = {icon};
    check(decide(app, minimized).window == 30, QStringLiteral("decision: minimizada sin WS_VISIBLE tambien cuenta"));

    Snapshot inFront = background;
    inFront.foreground = 23;
    inFront.foregroundPath = app;
    front = decide(app, inFront);
    check(front.action == Action::Minimize && front.window == 23,
          QStringLiteral("decision: con su ventana al frente (raiz del dueno) la minimiza"));

    // XYplorer (VB6): la ventana principal tiene un dueno oculto de herramientas y WS_EX_APPWINDOW.
    Snapshot vb6;
    vb6.processPaths << app;
    AppToggleSystem::Window hiddenOwner = window(40, app);
    hiddenOwner.toolWindow = true;
    AppToggleSystem::Window form = window(41, app);
    form.owned = true;
    form.appWindow = true;
    vb6.windows << hiddenOwner << form;
    front = decide(app, vb6);
    check(front.action == Action::Activate && front.window == 41,
          QStringLiteral("decision: una ventana con dueno y WS_EX_APPWINDOW (VB6) es elegible; su dueno oculto no"));

#ifdef Q_OS_WIN
    Snapshot otherCase = background;
    otherCase.processPaths = {QStringLiteral("c:\\program files\\EXAMPLE STUDIO\\bin\\64bit\\Example64.EXE")};
    for (AppToggleSystem::Window &each : otherCase.windows) each.processPath = otherCase.processPaths.first();
    check(decide(QStringLiteral("C:/Program Files/Example Studio/bin/./64bit//example64.exe"), otherCase).action
              == Action::Activate,
          QStringLiteral("decision: la ruta coincide con otras mayusculas, barras invertidas y ./"));
#else
    check(!samePath(app, app.toUpper()), QStringLiteral("decision: en mac la ruta distingue mayusculas"));
#endif
    check(samePath(app + QStringLiteral("/"), app) && !samePath(QString(), QString()),
          QStringLiteral("decision: barra final ignorada y rutas vacias nunca coinciden"));

    Snapshot whole;
    whole.wholeApp = true;
    whole.processPaths << app;
    whole.foregroundPath = app;
    check(decide(app, whole).action == Action::Minimize, QStringLiteral("decision mac: al frente la oculta"));
    whole.foregroundPath = other;
    check(decide(app, whole).action == Action::Activate, QStringLiteral("decision mac: corriendo de fondo la activa"));

    // ---- Settings: ida y vuelta, y entradas invalidas.
    AppEntry entry;
    entry.name = QStringLiteral("Example");
    entry.path = app;
    entry.arguments = QStringLiteral("--minimize-to-tray \"a b\"");
    entry.workingDir = QStringLiteral("C:/Example");
    entry.shortcut = Shortcut::fromPortableString(QStringLiteral("Meta+E"));
    AppToggleState state;
    check(state.upsert(entry) && entry.id == 1, QStringLiteral("estado: la primera entrada toma el id 1"));
    AppEntry second = entry;
    second.id = 0;
    second.name = QStringLiteral("Second");
    second.arguments.clear();
    second.workingDir.clear();
    second.enabled = false;
    second.shortcut = Shortcut::fromPortableString(QStringLiteral("Ctrl+Alt+Shift+Meta+O"));
    check(state.upsert(second) && second.id == 2, QStringLiteral("estado: la segunda toma el id 2"));
    const QList<AppEntry> loaded = AppToggleState::fromJson(AppToggleState::toJson(state.apps()));
    check(loaded.size() == 2 && loaded.at(0).name == entry.name && loaded.at(0).path == entry.path
              && loaded.at(0).arguments == entry.arguments && loaded.at(0).workingDir == entry.workingDir
              && loaded.at(0).shortcut == entry.shortcut && loaded.at(0).enabled && !loaded.at(1).enabled
              && loaded.at(1).arguments.isEmpty() && loaded.at(1).shortcut == second.shortcut,
          QStringLiteral("estado: JSON ida y vuelta con argumentos, carpeta, atajo y apagada"));
    AppEntry duplicate = second;
    duplicate.id = 0;
    duplicate.shortcut = entry.shortcut;
    check(!state.upsert(duplicate) && duplicate.id == 0 && state.apps().size() == 2,
          QStringLiteral("estado: un atajo repetido se rechaza sin gastar un id"));
    AppEntry bad = second;
    bad.id = 0;
    bad.shortcut = Shortcut::fromPortableString(QStringLiteral("Ctrl+Alt+B"));
    bad.name = QStringLiteral("   ");
    const bool emptyName = !state.upsert(bad);
    bad.name = QStringLiteral("Bad");
    bad.path = QStringLiteral("Example/example64.exe");
    const bool relative = !state.upsert(bad);
#ifdef Q_OS_MACOS
    bad.path = QStringLiteral("/Applications/Example.dmg");
#else
    bad.path = QStringLiteral("C:/Example/example.lnk");
#endif
    const bool shortcutFile = !state.upsert(bad);
    bad.path = app;
    bad.shortcut = Shortcut();
    const bool noShortcut = !state.upsert(bad);
    check(emptyName && relative && shortcutFile && noShortcut && state.apps().size() == 2,
          QStringLiteral("estado: sin nombre, ruta relativa, acceso directo o sin atajo no se guardan"));
    check(AppToggleState::fromJson(QStringLiteral("{\"id\":1}")).isEmpty()
              && AppToggleState::fromJson(QStringLiteral("[{\"id\":1,\"name\":\"x\",\"path\":\"x.exe\",\"shortcut\":\"Ctrl+Alt+X\"}]")).isEmpty()
              && AppToggleState::fromJson(QStringLiteral("no es json")).isEmpty(),
          QStringLiteral("estado: settings corruptos o con ruta relativa no crean entradas"));
    const QString twice = QStringLiteral("[%1,%1]").arg(AppToggleState::toJson({entry}).mid(1).chopped(1));
    check(AppToggleState::fromJson(twice).size() == 1, QStringLiteral("estado: un id repetido en el JSON entra una sola vez"));
    check(state.remove(1) && state.apps().size() == 1 && !state.remove(1), QStringLiteral("estado: borrar libera la entrada"));

    // ---- El modulo: choques, errores por entrada y la accion en modo solo loguear.
    FakeContext context;
    AppToggleModule module(context);
    module.start();
    AppEntry real;
    real.name = QStringLiteral("Self");
    real.path = existingAppPath();
    real.shortcut = Shortcut::fromPortableString(QStringLiteral("Ctrl+Alt+T"));
    QString error;
    check(module.saveApp(real, &error) && module.appsState().apps().size() == 1 && context.fake.registered.contains(1),
          QStringLiteral("modulo: una entrada valida se guarda y registra su atajo"));
    AppEntry clash = real;
    clash.name = QStringLiteral("Clash");
    check(!module.saveApp(clash, &error) && error == I18n::tr("Already used by another shortcut."),
          QStringLiteral("modulo: el mismo atajo en otra app se rechaza"));
    context.fake.takenByOther = Shortcut::fromPortableString(QStringLiteral("Ctrl+Alt+W"));
    clash.shortcut = context.fake.takenByOther;
    check(module.validateShortcut(0, clash.shortcut) == I18n::tr("Already used by %1.").arg(QStringLiteral("Window Layouts")),
          QStringLiteral("modulo: un atajo de otra herramienta se nombra"));
    clash.shortcut = Shortcut::fromPortableString(QStringLiteral("Ctrl+Alt+M"));
    clash.path = QFileInfo(existingAppPath()).absolutePath() + QStringLiteral("/no-such-app")
#ifdef Q_OS_MACOS
                 + QStringLiteral(".app");
#else
                 + QStringLiteral(".exe");
#endif
    check(!module.saveApp(clash, &error) && error.contains(QStringLiteral("no-such-app")),
          QStringLiteral("modulo: una app que no existe no se guarda"));
    clash.path = existingAppPath();
#ifdef Q_OS_WIN
    clash.workingDir = QStringLiteral("C:/no-such-folder-mighty");
    check(!module.saveApp(clash, &error) && error.contains(QStringLiteral("no-such-folder-mighty")),
          QStringLiteral("modulo: una carpeta de inicio que no existe no se guarda"));
    clash.workingDir.clear();
#endif
    clash.name.clear();
    check(module.saveApp(clash, &error) && !module.appsState().apps().last().name.isEmpty(),
          QStringLiteral("modulo: sin nombre toma el de la app"));
    const int clashId = module.appsState().apps().last().id;
    context.fake.accept = false;
    AppEntry rejected = real;
    rejected.id = 0;
    rejected.name = QStringLiteral("Rejected");
    rejected.shortcut = Shortcut::fromPortableString(QStringLiteral("Ctrl+Alt+R"));
    check(!module.saveApp(rejected, &error) && !error.isEmpty() && module.appsState().apps().size() == 2,
          QStringLiteral("modulo: si el sistema rechaza el atajo, la entrada nueva no queda guardada"));
    rejected.enabled = false;
    check(module.saveApp(rejected, &error) && module.appsState().apps().size() == 3 && module.registrationFailureCount() == 2,
          QStringLiteral("modulo: apagada se guarda sin registrar; las encendidas muestran su fallo"));
    check(module.setAppEnabled(clashId, false) && module.registrationFailureCount() == 1
              && module.registrationError(clashId).isEmpty() && !module.registrationError(1).isEmpty(),
          QStringLiteral("modulo: apagar una conserva el fallo de la otra"));
    context.fake.accept = true;
    check(module.setAppEnabled(clashId, true) && module.registrationFailureCount() == 0,
          QStringLiteral("modulo: al aceptar el sistema, los fallos se van"));
    // Una entrada que no existe en disco: si el modulo llegara a ejecutar la accion, fallaria y dejaria
    // el error. En una corrida automatizada solo se loguea.
    context.setValue(QStringLiteral("apps"),
                     AppToggleState::toJson({AppEntry{7, QStringLiteral("Ghost"), app, QString(), QString(),
                                                      Shortcut::fromPortableString(QStringLiteral("Ctrl+Alt+G")), true}}));
    AppToggleModule ghost(context);
    ghost.start();
    context.fake.fire(7);
    check(ghost.lastError().isEmpty() && ghost.appsState().apps().size() == 1,
          QStringLiteral("modulo: el atajo en una corrida automatizada solo loguea, no abre ni activa nada"));
    ghost.stop();
    module.stop();
    check(context.fake.registered.isEmpty(), QStringLiteral("modulo: stop suelta todos los atajos"));
}

int simulateAction(const QString &action, const QStringList &args)
{
    if (action != QLatin1String("decide")) {
        return 2;
    }
    const QString path = args.value(0);
    if (path.isEmpty()) {
        std::fprintf(stderr, "uso: --simulate-action appToggle:decide <ruta del exe o del .app>\n");
        return 1;
    }
    // Solo lectura: procesos, ventanas y la del frente. No se ejecuta nada.
    const AppToggleSystem::Snapshot snapshot = AppToggleSystem::snapshot(path);
    std::printf("app: %s\n", qPrintable(QDir::toNativeSeparators(path)));
    for (const QString &process : snapshot.processPaths) {
        std::printf("proceso: %s%s\n", qPrintable(process),
                    AppToggleSystem::samePath(process, path) ? "" : " (otra ruta)");
    }
    for (const AppToggleSystem::Window &window : snapshot.windows) {
        std::printf("ventana 0x%llx %s visible=%d minimizada=%d dueno=%d herramientas=%d appwindow=%d oculta_dwm=%d%s\n",
                    static_cast<unsigned long long>(window.id), qPrintable(window.className), window.visible,
                    window.minimized, window.owned, window.toolWindow, window.appWindow, window.cloaked,
                    AppToggleSystem::eligible(window) ? " ELEGIBLE" : "");
    }
    if (snapshot.foreground || snapshot.wholeApp) {
        std::printf("frente: 0x%llx %s\n", static_cast<unsigned long long>(snapshot.foreground),
                    qPrintable(snapshot.foregroundPath.isEmpty() ? QStringLiteral("(otra app)") : snapshot.foregroundPath));
    } else {
        std::printf("frente: no se pudo leer (desde el escritorio aparte del runner de QA no se ve; la app si)\n");
    }
    std::printf("haria: %s\n", qPrintable(AppToggleSystem::describe(AppToggleSystem::decide(path, snapshot))));
    return 0;
}

} // namespace

AppToggleModule::AppToggleModule(ModuleContext &context)
    : Module(context), m_state(&context)
{
}

AppToggleModule::~AppToggleModule()
{
    stop();
}

void AppToggleModule::start()
{
    if (m_started) {
        return;
    }
    m_started = true;
    registerApps();
}

void AppToggleModule::stop()
{
    if (!m_started) {
        return;
    }
    m_started = false;
    if (m_hotkeys) {
        disconnect(m_hotkeys, nullptr, this, nullptr);
        m_hotkeys->unregisterAll();
        for (int id : m_declaredIds) {
            m_hotkeys->declare(id, Shortcut());
        }
        m_declaredIds.clear();
        m_hotkeys = nullptr;
    }
}

void AppToggleModule::registerApps()
{
    if (!m_started) {
        return;
    }
    if (m_hotkeys) {
        m_hotkeys->unregisterAll();
        for (int id : m_declaredIds) {
            m_hotkeys->declare(id, Shortcut());
        }
        m_declaredIds.clear();
    }
    m_registrationErrors.clear();
    m_externalConflicts.clear();
    for (const AppEntry &entry : m_state.apps()) {
        if (!entry.enabled || !entry.shortcut.isValid()) {
            continue;
        }
        if (!m_hotkeys) {
            m_hotkeys = context().hotkeys();
            connect(m_hotkeys, &ModuleHotkeys::activated, this, &AppToggleModule::toggle);
        }
        const QString takenBy = m_hotkeys->declare(entry.id, entry.shortcut);
        if (!takenBy.isEmpty()) {
            m_registrationErrors.insert(entry.id, I18n::tr("%1 is already used by %2.").arg(entry.shortcut.displayText(), takenBy));
            continue;
        }
        m_declaredIds.insert(entry.id);
        // Win+E y las demas que reserva Windows salen ocupadas: igual responden por la prioridad.
        const bool shared = !m_hotkeys->probe(entry.shortcut);
        if (shared) m_externalConflicts.insert(entry.id);
#ifdef Q_OS_WIN
        const bool registered = m_hotkeys->registerPriorityHotkey(entry.id, entry.shortcut);
#else
        const bool registered = m_hotkeys->registerHotkey(entry.id, entry.shortcut);
#endif
        if (!registered) {
            m_registrationErrors.insert(entry.id,
#ifdef Q_OS_WIN
                                        I18n::tr("Could not activate %1 with priority.").arg(entry.shortcut.displayText())
#else
                                        shared ? I18n::tr("Another app or macOS uses %1.").arg(entry.shortcut.displayText())
                                               : I18n::tr("Could not activate %1.").arg(entry.shortcut.displayText())
#endif
            );
        }
    }
    emit statusChanged();
    emit appsChanged();
}

void AppToggleModule::toggle(int id)
{
    if (!m_started) {
        return;
    }
    for (const AppEntry &entry : m_state.apps()) {
        if (entry.id != id || !entry.enabled) {
            continue;
        }
        const AppToggleSystem::Snapshot snapshot = AppToggleSystem::snapshot(entry.path);
        const AppToggleSystem::Decision decision = AppToggleSystem::decide(entry.path, snapshot);
        qInfo().noquote() << "[appToggle]" << entry.name << "->" << AppToggleSystem::describe(decision);
        if (context().automatedRun()) {
            qInfo() << "[appToggle] Corrida automatizada: solo se loguea";
            return;
        }
        QString error;
        if (!AppToggleSystem::run(decision, entry.target(), &error)) {
            qWarning().noquote() << "[appToggle]" << entry.name << "fallo:" << error;
            m_lastError = error;
            emit statusChanged();
            emit appsChanged();
        } else if (!m_lastError.isEmpty()) {
            m_lastError.clear();
            emit statusChanged();
            emit appsChanged();
        }
        return;
    }
}

ModuleStatus AppToggleModule::status() const
{
    if (!m_lastError.isEmpty() || !m_registrationErrors.isEmpty()) {
        return {ModuleTone::Error, I18n::tr("Needs attention")};
    }
    int count = 0;
    for (const AppEntry &entry : m_state.apps()) count += entry.enabled;
    if (count == 0) {
        return {ModuleTone::Active, m_state.apps().isEmpty() ? I18n::tr("No apps yet") : I18n::tr("All apps are off")};
    }
    return {ModuleTone::Active, count == 1 ? I18n::tr("1 app ready") : I18n::tr("%1 apps ready").arg(count)};
}

QWidget *AppToggleModule::createPanel(QWidget *parent)
{
    auto *panel = new AppTogglePanel(this, !context().captureMode(), parent);
    connect(this, &AppToggleModule::appsChanged, panel, &AppTogglePanel::refresh);
    if (m_captureState == QLatin1String("editor-new")) panel->showEditorForCapture(0);
    if (m_captureState == QLatin1String("editor")) panel->showEditorForCapture(2);
    return panel;
}

QStringList AppToggleModule::captureStates() const
{
    return {QStringLiteral("list"), QStringLiteral("empty"), QStringLiteral("editor-new"), QStringLiteral("editor"),
            QStringLiteral("conflicts"), QStringLiteral("long-name")};
}

bool AppToggleModule::applyCaptureState(const QString &state)
{
    if (!context().captureMode() || !captureStates().contains(state)) {
        return false;
    }
    m_captureState = state;
    m_registrationErrors.clear();
    m_externalConflicts.clear();
    if (state == QLatin1String("empty")) {
        m_state.setCaptureApps({});
        return true;
    }
    // Nombres genericos. La primera es una app que existe en todo equipo: muestra su icono real; las
    // otras no existen y muestran el glifo.
#ifdef Q_OS_MACOS
    const QString files = QStringLiteral("/System/Library/CoreServices/Finder.app");
    const QString recorder = QStringLiteral("/Applications/Screen Recorder.app");
    const QString console = QStringLiteral("/Applications/Audio Console.app");
#else
    const QString files = QStringLiteral("C:\\Windows\\explorer.exe");
    const QString recorder = QStringLiteral("C:\\Program Files\\Screen Recorder\\bin\\64bit\\recorder64.exe");
    const QString console = QStringLiteral("C:\\Program Files (x86)\\Audio Console\\Console.exe");
#endif
    AppEntry a{1, I18n::tr("File manager"), files, QString(), QString(),
               Shortcut::fromPortableString(QStringLiteral("Meta+E")), true};
    AppEntry b{2, I18n::tr("Screen recorder"), recorder, QStringLiteral("--minimize-to-tray"), QString(),
               Shortcut::fromPortableString(QStringLiteral("Ctrl+Alt+Shift+Meta+R")), true};
    AppEntry c{3, I18n::tr("Audio console"), console, QString(), QString(),
               Shortcut::fromPortableString(QStringLiteral("Ctrl+Alt+Shift+Meta+U")), false};
    if (state == QLatin1String("long-name")) {
        b.name = QStringLiteral("Long personalized app name that should leave the shortcut and pencil visible");
    }
    if (state == QLatin1String("conflicts")) {
        c.enabled = true;
        m_externalConflicts.insert(a.id);
        m_registrationErrors.insert(c.id, I18n::tr("Could not activate %1 with priority.").arg(c.shortcut.displayText()));
    }
    m_state.setCaptureApps({a, b, c});
    return true;
}

QString AppToggleModule::validateShortcut(int editingId, const Shortcut &shortcut, bool enabled) const
{
    if (!shortcut.isValid()) {
        return I18n::tr("Choose a shortcut.");
    }
    for (const AppEntry &entry : m_state.apps()) {
        if (entry.id != editingId && entry.shortcut == shortcut) {
            return I18n::tr("Already used by another shortcut.");
        }
    }
    ModuleHotkeys *hotkeys = enabled ? (m_hotkeys ? m_hotkeys : context().hotkeys()) : nullptr;
    if (hotkeys) {
        const QString takenBy = hotkeys->declaredByOtherModule(shortcut);
        if (!takenBy.isEmpty()) {
            return I18n::tr("Already used by %1.").arg(takenBy);
        }
    }
    return {};
}

QString AppToggleModule::validateEntry(const AppEntry &entry) const
{
    const QString path = entry.path.trimmed();
    if (path.isEmpty()) {
        return I18n::tr("Choose the app.");
    }
    if (!AppToggleState::validPath(path)) {
#ifdef Q_OS_MACOS
        return I18n::tr("Choose an app (.app).");
#else
        return I18n::tr("Choose a program file (.exe).");
#endif
    }
    const QFileInfo file(path);
#ifdef Q_OS_MACOS
    if (!file.isDir()) {
#else
    if (!file.isFile()) {
#endif
        return I18n::tr("The app was not found: %1").arg(QDir::toNativeSeparators(path));
    }
#ifdef Q_OS_WIN
    const QString folder = entry.workingDir.trimmed();
    if (!folder.isEmpty() && (!QDir::isAbsolutePath(QDir::fromNativeSeparators(folder)) || !QFileInfo(folder).isDir())) {
        return I18n::tr("The start folder was not found: %1").arg(QDir::toNativeSeparators(folder));
    }
#endif
    return validateShortcut(entry.id, entry.shortcut, entry.enabled);
}

bool AppToggleModule::saveApp(AppEntry entry, QString *error)
{
    if (entry.name.trimmed().isEmpty() && AppToggleState::validPath(entry.path)) {
        entry.name = AppToggleSystem::productName(entry.path);
        if (entry.name.isEmpty()) entry.name = QFileInfo(entry.path.trimmed()).completeBaseName();
    }
    const QString invalid = validateEntry(entry);
    if (!invalid.isEmpty()) {
        if (error) *error = invalid;
        return false;
    }
    AppEntry previous;
    for (const AppEntry &stored : m_state.apps()) {
        if (stored.id == entry.id) previous = stored;
    }
    if (!m_state.upsert(entry)) {
        if (error) *error = I18n::tr("Check the name, or remove an app to make room.");
        return false;
    }
    registerApps();
    if (entry.enabled && m_registrationErrors.contains(entry.id)) {
        const QString failure = m_registrationErrors.value(entry.id);
        if (previous.id) {
            m_state.upsert(previous);
        } else {
            m_state.remove(entry.id);
        }
        registerApps();
        if (error) *error = failure;
        return false;
    }
    return true;
}

bool AppToggleModule::setAppEnabled(int id, bool enabled)
{
    for (AppEntry entry : m_state.apps()) {
        if (entry.id != id) continue;
        if (entry.enabled == enabled) return true;
        entry.enabled = enabled;
        if (!m_state.upsert(entry)) return false;
        registerApps();
        return true;
    }
    return false;
}

bool AppToggleModule::removeApp(int id)
{
    if (!m_state.remove(id)) {
        return false;
    }
    registerApps();
    return true;
}

ModuleDescriptor appToggleDescriptor()
{
    ModuleDescriptor d;
    d.id = QStringLiteral("appToggle");
    d.title = QStringLiteral("App Toggle");
    d.description = I18n::tr("One shortcut per app: opens it, brings it to the front or minimizes it.");
    d.offBullets = {I18n::tr("Opens the app if it isn't running"),
                    I18n::tr("Brings it to the front, restoring it if it's minimized"),
#ifdef Q_OS_MACOS
                    I18n::tr("Hides it if it's already in front")};
#else
                    I18n::tr("Minimizes it if it's already in front")};
#endif
    d.platforms = PlatformWindows | PlatformMac;
    d.paintIcon = &paintAppToggleIcon;
    d.create = [](ModuleContext &context) -> std::unique_ptr<Module> {
        return std::make_unique<AppToggleModule>(context);
    };
    d.configuredShortcuts = [](const SettingsReader &read) {
        QList<Shortcut> shortcuts;
        for (const AppEntry &entry : AppToggleState::fromJson(read(QStringLiteral("apps"), {}).toString())) {
            if (entry.enabled && entry.shortcut.isValid()) shortcuts.append(entry.shortcut);
        }
        return shortcuts;
    };
    d.selfTest = &selfTest;
    d.simulateAction = &simulateAction;
    return d;
}

HelpSection appToggleHelp(const SettingsReader &)
{
    HelpSection help;
    help.title = QStringLiteral("App Toggle");
#ifdef Q_OS_MACOS
    help.steps = {I18n::tr("Add an app and choose it in Applications. Arguments are optional."),
                  I18n::tr("Record a shortcut for it."),
                  I18n::tr("Press it to open the app or bring it to the front. If it's already in front, it hides.")};
    help.note = I18n::tr("Hiding works like ⌘H: the app comes back with all its windows.");
#else
    help.steps = {I18n::tr("Add an app and choose its program file. Arguments and the start folder are optional."),
                  I18n::tr("Record a shortcut for it."),
                  I18n::tr("Press it to open the app or bring it to the front. If it's already in front, it minimizes.")};
    help.note = I18n::tr("An app that is running only in the notification area is left as is.");
#endif
    return help;
}
