#include "modules/meetframe/MeetFrameModule.h"

#include "app/ModuleContext.h"
#include "core/DebugFlags.h"
#include "core/I18n.h"
#include "modules/meetframe/MeetFrameOverlay.h"
#include "modules/meetframe/MeetFramePanel.h"
#include "modules/meetframe/MeetFrameRules.h"
#include "ui/Theme.h"

#include <QDebug>
#include <QGuiApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QPainter>
#include <QScreen>

#include <cstdio>

namespace {

const QString kWindowTitle = QStringLiteral("Meet Frame");

QString shortcutKey(int id)
{
    return id == MeetFrameModule::PauseShortcut ? QStringLiteral("shortcutPause") : QStringLiteral("shortcutToggle");
}

// Un marco con una esquina marcada: lo que se comparte. Se lee a 16 px.
void paintMeetFrameIcon(QPainter &painter, const QRectF &rect, const QColor &color)
{
    const qreal scale = qMin(rect.width(), rect.height()) / 16.0;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.translate(rect.center().x() - 8 * scale, rect.center().y() - 8 * scale);
    painter.scale(scale, scale);
    painter.setPen(QPen(color, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    // Las cuatro esquinas de un marco de 16:9.
    const qreal l = 1.5, t = 3.2, r = 14.5, b = 12.8, k = 3.6;
    painter.drawPolyline(QPolygonF({QPointF(l, t + k), QPointF(l, t), QPointF(l + k, t)}));
    painter.drawPolyline(QPolygonF({QPointF(r - k, t), QPointF(r, t), QPointF(r, t + k)}));
    painter.drawPolyline(QPolygonF({QPointF(r, b - k), QPointF(r, b), QPointF(r - k, b)}));
    painter.drawPolyline(QPolygonF({QPointF(l + k, b), QPointF(l, b), QPointF(l, b - k)}));
    // Un punto en el centro: lo que se ve.
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawEllipse(QPointF(8, 8), 1.7, 1.7);
    painter.restore();
}

// ---- QA: contexto de mentira (nunca toca el sistema)

class FakeHotkeys : public ModuleHotkeys
{
public:
    bool registerHotkey(int id, const Shortcut &) override { registered.insert(id); return true; }
    void unregisterHotkey(int id) override { registered.remove(id); }
    void unregisterAll() override { registered.clear(); }
    bool isRegistered(int id) const override { return registered.contains(id); }
    bool probe(const Shortcut &) override { return true; }
    void passThrough(const Shortcut &) override {}
    QString declare(int, const Shortcut &) override { return {}; }
    QString declaredByOtherModule(const Shortcut &shortcut) const override
    {
        return shortcut == other ? QStringLiteral("Dictation") : QString();
    }
    void fire(int id) { emit activated(id); }
    QSet<int> registered;
    Shortcut other;
};

class FakeContext : public ModuleContext
{
public:
    QString moduleId() const override { return QStringLiteral("meetFrame"); }
    QString moduleTitle() const override { return QStringLiteral("Meet Frame"); }
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

MeetFrameRules::WindowInfo win(const QRect &rect, const QString &app, bool covering = true)
{
    MeetFrameRules::WindowInfo w;
    w.rect = rect;
    w.app = app;
    w.covering = covering;
    return w;
}

void selfTest(const std::function<void(bool, const QString &)> &check)
{
    using namespace MeetFrameRules;
#if defined(Q_OS_MACOS)
    const QString whatsapp = QStringLiteral("net.whatsapp.whatsapp");
    const QString telegram = QStringLiteral("ru.keepcoder.telegram");
    const QString chrome = QStringLiteral("com.google.chrome");
    const QString notes = QStringLiteral("com.apple.notes");
    const QString notifications = QStringLiteral("com.apple.notificationcenterui");
#else
    const QString whatsapp = QStringLiteral("whatsapp.root.exe");
    const QString telegram = QStringLiteral("telegram.exe");
    const QString chrome = QStringLiteral("chrome.exe");
    const QString notes = QStringLiteral("notepad.exe");
    const QString notifications = QStringLiteral("shellexperiencehost.exe");
#endif

    // ---- Reglas
    const Rules defaults = build({QStringLiteral("messaging"), QStringLiteral("passwords")}, {}, true, true);
    check(defaults.apps.contains(whatsapp) && defaults.apps.contains(telegram) && !defaults.apps.contains(notes)
              && defaults.browserTitles.contains(QStringLiteral("whatsapp")) && defaults.browsers.contains(chrome),
          QStringLiteral("reglas: Mensajeria trae WhatsApp y Telegram, y las palabras del navegador"));
    check(hides(win(QRect(), whatsapp), defaults) && hides(win(QRect(), notifications), defaults) && !hides(win(QRect(), notes), defaults),
          QStringLiteral("reglas: se tapan WhatsApp y los avisos; el Bloc de notas no"));
    Rules noNotices = defaults;
    noNotices.hideNotifications = false;
    check(!hides(win(QRect(), notifications), noNotices), QStringLiteral("reglas: con los avisos apagados, los avisos se ven"));
    WindowInfo web = win(QRect(), chrome);
    web.title = QStringLiteral("(3) WhatsApp - Google Chrome");
    WindowInfo docs = win(QRect(), chrome);
    docs.title = QStringLiteral("Presupuesto - Google Docs");
    check(hides(web, defaults) && !hides(docs, defaults), QStringLiteral("reglas: WhatsApp Web en el navegador se tapa; otra pestaña no"));
    WindowInfo notesTitled = win(QRect(), notes);
    notesTitled.title = QStringLiteral("whatsapp.txt");
    check(!hides(notesTitled, defaults), QStringLiteral("reglas: el titulo solo cuenta en los navegadores"));
#if !defined(Q_OS_MACOS)
    check(appKey(QStringLiteral("\"C:\\Program Files\\Foo\\Foo App.EXE\"")) == QLatin1String("foo app.exe") && appKey(QStringLiteral("foo")).isEmpty()
              && appKey(QStringLiteral("a|b.exe")).isEmpty() && appKey(QStringLiteral(".exe")).isEmpty(),
          QStringLiteral("reglas: el exe sale de una ruta con comillas; sin .exe o con caracteres prohibidos, nada"));
#else
    check(appKey(QStringLiteral(" com.Example.App ")) == QLatin1String("com.example.app") && appKey(QStringLiteral("/Applications/X.app")).isEmpty()
              && appKey(QStringLiteral("nada")).isEmpty(),
          QStringLiteral("reglas: un bundle id se acepta en minusculas; una ruta o una palabra suelta, no"));
#endif
    const Rules custom = build({}, {QStringLiteral("NotePad.exe"), notes, QStringLiteral("basura")}, false, false);
#if !defined(Q_OS_MACOS)
    check(custom.apps == QSet<QString>{notes} && !custom.hideDesktop && custom.browserTitles.isEmpty(),
          QStringLiteral("reglas: las apps propias entran en minusculas y sin repetir; lo invalido no entra"));
#else
    check(custom.apps.contains(notes) && !custom.hideDesktop, QStringLiteral("reglas: las apps propias entran en minusculas"));
#endif

    // ---- Mascara: lo que se tapa en una region de 1000 x 600.
    const QRect region(0, 0, 1000, 600);
    Rules noDesktop = defaults;
    noDesktop.hideDesktop = false;
    // Arriba de todo el Bloc de notas, abajo WhatsApp, que asoma a la derecha.
    QList<WindowInfo> stack = {win(QRect(0, 0, 600, 600), notes), win(QRect(400, 100, 400, 300), whatsapp)};
    Mask mask = computeMask(stack, region, noDesktop);
    check(mask.hiddenWindows == 1 && mask.region == QRegion(QRect(600, 100, 200, 300)),
          QStringLiteral("mascara: de una ventana a tapar se tapa solo la parte que se ve"));
    stack.first().covering = false; // el de arriba es semitransparente: no se sabe que tapa
    mask = computeMask(stack, region, noDesktop);
    check(mask.region == QRegion(QRect(400, 100, 400, 300)),
          QStringLiteral("mascara: una ventana que no es opaca no esconde lo de abajo (se tapa de mas, nunca de menos)"));
    stack = {win(QRect(0, 0, 600, 600), notes), win(QRect(100, 100, 200, 200), whatsapp)};
    mask = computeMask(stack, region, noDesktop);
    check(mask.hiddenWindows == 0 && mask.region.isEmpty(), QStringLiteral("mascara: WhatsApp entero detras de otra ventana no cuenta ni se tapa"));
    mask = computeMask(stack, region, defaults);
    check(mask.region == QRegion(QRect(600, 0, 400, 600)), QStringLiteral("mascara: el escritorio es lo que no cubre ninguna ventana"));
    WindowInfo desktop;
    desktop.desktop = true;
    WindowInfo below = win(QRect(0, 0, 1000, 600), whatsapp);
    mask = computeMask({win(QRect(0, 0, 500, 600), notes), desktop, below}, region, noDesktop);
    check(mask.hiddenWindows == 0 && mask.region.isEmpty(), QStringLiteral("mascara: lo que esta debajo del escritorio no se ve"));
    WindowInfo shared = win(QRect(0, 0, 1000, 600), QString(), false);
    shared.forceMask = true;
    mask = computeMask({win(QRect(0, 0, 500, 600), notes), shared}, region, noDesktop);
    check(mask.region == QRegion(QRect(500, 0, 500, 600)),
          QStringLiteral("mascara: la ventana compartida, si se ve, se tapa siempre (sin espejo infinito)"));
    WindowInfo frame = win(QRect(-10, -40, 1020, 650), QString());
    frame.ignored = true;
    mask = computeMask({frame, win(QRect(0, 0, 1000, 600), whatsapp)}, region, noDesktop);
    check(mask.hiddenWindows == 1 && mask.region == QRegion(region), QStringLiteral("mascara: el marco no cuenta como ventana que tapa"));
    mask = computeMask({win(QRect(-500, -500, 2000, 2000), whatsapp)}, region, noDesktop);
    check(mask.region == QRegion(region), QStringLiteral("mascara: nunca se sale de la region"));

    // ---- Modulo: atajos, abrir el marco en una corrida automatizada y ajustes.
    FakeContext context;
    {
        MeetFrameModule module(context);
        module.start();
        check(context.fake.registered == QSet<int>{MeetFrameModule::ToggleShortcut},
              QStringLiteral("modulo: prendido registra solo el atajo del marco; el de pausa espera a que se abra"));
        context.fake.fire(MeetFrameModule::ToggleShortcut);
        check(module.frameShown() && context.fake.registered.contains(MeetFrameModule::PauseShortcut),
              QStringLiteral("modulo: el atajo abre el marco (en una corrida automatizada sin captura) y suma el de pausa"));
        context.fake.fire(MeetFrameModule::PauseShortcut);
        check(module.paused() && module.status().tone == ModuleTone::Paused, QStringLiteral("modulo: pausa con su atajo"));
        context.fake.fire(MeetFrameModule::ToggleShortcut);
        check(!module.frameShown() && !module.paused() && !context.fake.registered.contains(MeetFrameModule::PauseShortcut),
              QStringLiteral("modulo: cerrar el marco quita la pausa y su atajo"));
        QString error;
        context.fake.other = Shortcut::fromPortableString(QStringLiteral("Ctrl+Alt+D"));
        check(!module.setShortcut(MeetFrameModule::ToggleShortcut, context.fake.other, &error) && error.contains(QStringLiteral("Dictation")),
              QStringLiteral("modulo: un atajo de otra herramienta se rechaza con su nombre"));
        check(!module.setShortcut(MeetFrameModule::ToggleShortcut, module.shortcut(MeetFrameModule::PauseShortcut), &error),
              QStringLiteral("modulo: los dos atajos no pueden ser el mismo"));
        const Shortcut keys = Shortcut::fromPortableString(QStringLiteral("Ctrl+Shift+F9"));
        check(module.setShortcut(MeetFrameModule::ToggleShortcut, keys, &error) && module.shortcut(MeetFrameModule::ToggleShortcut) == keys,
              QStringLiteral("modulo: un atajo nuevo se guarda"));
        module.setGroupEnabled(QStringLiteral("mail"), true);
        module.setHideDesktop(false);
        check(module.groupEnabled(QStringLiteral("mail")) && module.groupEnabled(QStringLiteral("messaging")) && !module.hideDesktop()
                  && module.hideNotifications(),
              QStringLiteral("modulo: grupos y escritorio se guardan; Mensajeria y avisos vienen prendidos"));
        MeetFrameModule::CustomApp app{QStringLiteral("Foo.exe"), QStringLiteral("Foo"), QStringLiteral("C:/Foo/Foo.exe")};
#if defined(Q_OS_MACOS)
        app.id = QStringLiteral("com.example.Foo");
#endif
        check(module.addApp(app) && !module.addApp(app) && module.customApps().size() == 1, QStringLiteral("modulo: una app propia entra una sola vez"));
        const QString id = module.customApps().first().id;
        check(id == app.id.toLower() && module.removeApp(id) && module.customApps().isEmpty(),
              QStringLiteral("modulo: la app propia se guarda en minusculas y se borra"));
        module.stop();
        check(context.fake.registered.isEmpty(), QStringLiteral("modulo: apagado no deja atajos"));
    }
    const QList<MeetFrameModule::CustomApp> apps = MeetFrameModule::appsFromJson(
        QStringLiteral("[{\"id\":\"a.exe\",\"name\":\"A\"},{\"id\":\"\"},{\"id\":\"A.exe\",\"name\":\"otra\"},\"x\"]"));
#if !defined(Q_OS_MACOS)
    check(apps.size() == 1 && apps.first().name == QLatin1String("A") && MeetFrameModule::appsFromJson(QStringLiteral("no")).isEmpty(),
          QStringLiteral("ajustes: lista de apps con basura o repetidas queda limpia"));
#else
    Q_UNUSED(apps);
#endif
}

int simulateAction(const QString &action, const QStringList &args)
{
    if (action != QLatin1String("windows")) {
        return 2;
    }
    QRect region;
    if (args.size() >= 4) {
        region = QRect(args.at(0).toInt(), args.at(1).toInt(), args.at(2).toInt(), args.at(3).toInt());
    }
    const MeetFrameRules::Rules rules = MeetFrameRules::build({QStringLiteral("messaging"), QStringLiteral("passwords")}, {}, true, true);
    const QList<MeetFrameRules::WindowInfo> windows = MeetFrameSystem::listWindows(region);
    std::printf("ventanas de arriba hacia abajo (solo lectura): %d\n", int(windows.size()));
    for (const MeetFrameRules::WindowInfo &w : windows) {
        if (w.desktop) {
            std::printf("  [escritorio] %s\n", qPrintable(w.className));
            continue;
        }
        std::printf("  %s %-24s %-28s %4d,%4d %4dx%-4d %s  \"%s\"\n", MeetFrameRules::hides(w, rules) ? "TAPA" : "    ", qPrintable(w.app),
                    qPrintable(w.className.left(28)), w.rect.x(), w.rect.y(), w.rect.width(), w.rect.height(),
                    w.covering ? "opaca" : "no-opaca", qPrintable(w.title.left(60)));
    }
    if (!region.isEmpty()) {
        const MeetFrameRules::Mask mask = MeetFrameRules::computeMask(windows, region, rules);
        std::printf("region %d,%d %dx%d: %d ventana(s) tapada(s), %d rectangulo(s)\n", region.x(), region.y(), region.width(), region.height(),
                    mask.hiddenWindows, int(mask.region.rectCount()));
    }
    return 0;
}

} // namespace

// ------------------------------------------------------------------ Vida

namespace {
constexpr int kRegionMs = 100;   // la captura sigue al marco como mucho 10 veces por segundo mientras se arrastra
constexpr int kWatchMs = 100;    // reloj para medir cuanto se atrasa el hilo principal
constexpr int kStatsMs = 15000;  // un resumen en el log cada 15 s con el marco a la vista
} // namespace

MeetFrameModule::MeetFrameModule(ModuleContext &context)
    : Module(context)
{
    m_regionTimer.setSingleShot(true);
    m_regionTimer.setInterval(kRegionMs);
    connect(&m_regionTimer, &QTimer::timeout, this, &MeetFrameModule::applyRegion);
    m_watch.setInterval(kWatchMs);
    m_watch.setTimerType(Qt::PreciseTimer);
    connect(&m_watch, &QTimer::timeout, this, &MeetFrameModule::watchTick);
}

MeetFrameModule::~MeetFrameModule()
{
    stop();
}

void MeetFrameModule::start()
{
    if (m_started) {
        return;
    }
    m_started = true;
    registerShortcuts();
}

void MeetFrameModule::stop()
{
    if (!m_started) {
        return;
    }
    hideFrame();
    unregisterShortcuts();
    m_started = false;
}

// ------------------------------------------------------------------ Atajos

Shortcut MeetFrameModule::defaultShortcut(int id)
{
    return Shortcut::fromPortableString(id == PauseShortcut ? QStringLiteral("Ctrl+Alt+P") : QStringLiteral("Ctrl+Alt+M"));
}

Shortcut MeetFrameModule::shortcut(int id) const
{
    const Shortcut keys = Shortcut::fromPortableString(context().value(shortcutKey(id), defaultShortcut(id).toPortableString()).toString());
    return keys.isValid() ? keys : defaultShortcut(id);
}

QString MeetFrameModule::validateShortcut(int id, const Shortcut &keys) const
{
    if (!keys.isValid()) {
        return I18n::tr("Choose a shortcut.");
    }
    if (keys == shortcut(id == PauseShortcut ? ToggleShortcut : PauseShortcut)) {
        return I18n::tr("The other Meet Frame shortcut already uses it.");
    }
    ModuleHotkeys *hotkeys = m_hotkeys ? m_hotkeys : (context().captureMode() ? nullptr : context().hotkeys());
    const QString takenBy = hotkeys ? hotkeys->declaredByOtherModule(keys) : QString();
    return takenBy.isEmpty() ? QString() : I18n::tr("Already used by %1.").arg(takenBy);
}

bool MeetFrameModule::setShortcut(int id, const Shortcut &keys, QString *error)
{
    const QString invalid = validateShortcut(id, keys);
    if (!invalid.isEmpty()) {
        if (error) *error = invalid;
        return false;
    }
    const Shortcut previous = shortcut(id);
    context().setValue(shortcutKey(id), keys.toPortableString());
    registerShortcuts();
    if (!m_shortcutError.isEmpty()) {
        if (error) *error = m_shortcutError;
        context().setValue(shortcutKey(id), previous.toPortableString());
        registerShortcuts();
        changed();
        return false;
    }
    changed();
    return true;
}

void MeetFrameModule::registerShortcuts()
{
    if (!m_started) {
        return;
    }
    if (!m_hotkeys) {
        m_hotkeys = context().hotkeys();
        if (!m_hotkeys) return;
        connect(m_hotkeys, &ModuleHotkeys::activated, this, [this](int id) {
            if (id == ToggleShortcut) toggleFrame();
            else if (id == PauseShortcut) togglePause();
        });
    }
    m_shortcutError.clear();
    m_hotkeys->unregisterHotkey(ToggleShortcut);
    m_hotkeys->unregisterHotkey(PauseShortcut);
    m_pauseRegistered = false;
    for (int id : {ToggleShortcut, PauseShortcut}) {
        m_hotkeys->declare(id, Shortcut());
        const Shortcut keys = shortcut(id);
        const QString takenBy = m_hotkeys->declare(id, keys);
        if (!takenBy.isEmpty() && m_shortcutError.isEmpty()) {
            m_shortcutError = I18n::tr("%1 is already used by %2.").arg(keys.displayText(), takenBy);
        }
    }
    const Shortcut toggle = shortcut(ToggleShortcut);
    if (m_shortcutError.isEmpty() && !m_hotkeys->registerHotkey(ToggleShortcut, toggle)) {
        m_shortcutError = I18n::tr("Another app is using %1.").arg(toggle.displayText());
    }
    if (!m_shortcutError.isEmpty()) {
        qWarning().noquote() << "[meetFrame]" << m_shortcutError;
    }
    syncPauseShortcut();
}

void MeetFrameModule::syncPauseShortcut()
{
    // El de pausa existe solo con el marco abierto: el resto del tiempo esa combinacion queda libre.
    if (!m_hotkeys) return;
    const bool want = m_started && m_shown;
    if (want && !m_pauseRegistered) {
        m_pauseRegistered = m_hotkeys->registerHotkey(PauseShortcut, shortcut(PauseShortcut));
        if (!m_pauseRegistered) qWarning().noquote() << "[meetFrame] No se pudo registrar el atajo de pausa";
    } else if (!want && m_pauseRegistered) {
        m_hotkeys->unregisterHotkey(PauseShortcut);
        m_pauseRegistered = false;
    }
}

void MeetFrameModule::unregisterShortcuts()
{
    if (!m_hotkeys) return;
    disconnect(m_hotkeys, nullptr, this, nullptr);
    m_hotkeys->unregisterAll();
    m_hotkeys->declare(ToggleShortcut, Shortcut());
    m_hotkeys->declare(PauseShortcut, Shortcut());
    m_hotkeys = nullptr;
    m_pauseRegistered = false;
}

// ------------------------------------------------------------------ El marco

bool MeetFrameModule::needsPermission() const
{
    if (context().captureMode()) return m_captureState == QLatin1String("permission");
    return !context().automatedRun() && !MeetFrameSystem::hasCapturePermission();
}

void MeetFrameModule::requestPermission()
{
    if (context().automatedRun()) return;
    MeetFrameSystem::requestCapturePermission();
}

int MeetFrameModule::hiddenWindows() const
{
    if (context().captureMode()) return m_captureHidden;
    return m_session ? m_session->hiddenWindows() : 0;
}

void MeetFrameModule::toggleFrame()
{
    if (m_shown) hideFrame();
    else showFrame();
}

void MeetFrameModule::showFrame()
{
    if (!m_started || m_shown) {
        return;
    }
    m_error.clear();
    if (context().automatedRun()) {
        qInfo().noquote() << "[meetFrame] Corrida automatizada: el marco y la captura no se abren";
        m_shown = true;
        syncPauseShortcut();
        changed();
        return;
    }
    if (!MeetFrameSystem::hasCapturePermission()) {
        MeetFrameSystem::requestCapturePermission();
        m_error = I18n::tr("Allow screen recording for LGA Mighty Tools, then try again.");
        changed();
        context().showPanel();
        return;
    }
    auto *overlay = new MeetFrameOverlay(false);
    overlay->winId();
    MeetFrameSystem::prepareFrame(overlay);
    QPointer<MeetFrameOverlay> hover(overlay);
    MeetFrameSystem::trackFrameMouse(overlay, [hover](const QPoint &pos) {
        return hover ? hover->hoverAt(pos) : false;
    });
    MeetFrameSystem::excludeFromCapture(overlay->sizeMenu());
    overlay->setRegion(savedRegion());
    m_overlay = overlay;
    connect(overlay, &MeetFrameOverlay::closeClicked, this, &MeetFrameModule::hideFrame);
    connect(overlay, &MeetFrameOverlay::pauseClicked, this, &MeetFrameModule::togglePause);
    connect(overlay, &MeetFrameOverlay::sizeRequested, this, &MeetFrameModule::resizeRegion);
    connect(overlay, &MeetFrameOverlay::regionChanged, this, &MeetFrameModule::onRegionChanged);
    connect(overlay, &MeetFrameOverlay::regionSettled, this, &MeetFrameModule::saveRegion);
    overlay->show();
    QString error;
    m_session = MeetFrameSystem::start(kWindowTitle, MeetFrameSystem::captureRegion(overlay, MeetFrameOverlay::frameMargins()), options(), &error);
    if (!m_session) {
        qWarning().noquote() << "[meetFrame] No se pudo empezar a compartir:" << error;
        delete m_overlay.data();
        m_error = I18n::tr("Could not start sharing (%1).").arg(error);
        changed();
        return;
    }
    m_session->setIgnoredWindows({overlay, overlay->sizeMenu()});
    connect(m_session.get(), &MeetFrameSystem::Session::changed, this, [this]() {
        syncOverlay();
        emit statusChanged();
    });
    connect(m_session.get(), &MeetFrameSystem::Session::closeRequested, this, &MeetFrameModule::hideFrame);
    m_shown = true;
    m_paused = false;
    syncPauseShortcut();
    syncOverlay();
    qInfo().noquote() << "[meetFrame] Marco abierto, captura" << m_session->engine();
    m_regionRequests = m_regionApplied = 0;
    m_watchMaxLateMs = 0;
    m_watchLate = 0;
    m_watchClock.start();
    m_statsClock.start();
    // El reloj que mide el atraso despierta el hilo principal 10 veces por segundo: solo con el log prendido.
    if (DebugFlags::isOn(QStringLiteral("log"))) m_watch.start();
    MeetFrameSystem::syncFrameMouse(overlay);
    changed();
}

void MeetFrameModule::hideFrame()
{
    if (!m_shown && !m_overlay) {
        return;
    }
    if (m_overlay) saveRegion();
    m_regionTimer.stop();
    if (m_watch.isActive()) {
        m_watch.stop();
        logStats("al cerrar");
    }
    m_session.reset();
    delete m_overlay.data();
    m_shown = false;
    m_paused = false;
    syncPauseShortcut();
    qInfo().noquote() << "[meetFrame] Marco cerrado";
    changed();
}

void MeetFrameModule::togglePause()
{
    if (!m_shown) {
        return;
    }
    m_paused = !m_paused;
    applyOptions();
    syncOverlay();
    changed();
}

void MeetFrameModule::syncOverlay()
{
    if (!m_overlay) return;
    const QString sessionError = m_session ? m_session->error() : QString();
    QSize pixels;
    if (m_session) pixels = MeetFrameSystem::pixelSize(m_overlay, MeetFrameSystem::captureRegion(m_overlay, MeetFrameOverlay::frameMargins()));
    m_overlay->setState(m_paused, hiddenWindows(), pixels, sessionError.isEmpty() ? QString() : I18n::tr("Could not capture the screen."));
    m_overlay->setShortcuts(shortcut(ToggleShortcut).displayText(), shortcut(PauseShortcut).displayText());
}

void MeetFrameModule::onRegionChanged()
{
    if (!m_overlay || !m_session) return;
    // Mover o estirar manda un cambio por pixel (y dos, moveEvent y resizeEvent): se juntan.
    ++m_regionRequests;
    if (!m_regionTimer.isActive()) m_regionTimer.start();
}

void MeetFrameModule::applyRegion()
{
    if (!m_overlay || !m_session) return;
    ++m_regionApplied;
    m_session->setRegion(MeetFrameSystem::captureRegion(m_overlay, MeetFrameOverlay::frameMargins()));
    syncOverlay();
}

void MeetFrameModule::watchTick()
{
    const qint64 late = m_watchClock.restart() - kWatchMs;
    if (late > m_watchMaxLateMs) m_watchMaxLateMs = late;
    if (late > 50) ++m_watchLate;
    if (m_statsClock.elapsed() >= kStatsMs) logStats("cada 15 s");
}

void MeetFrameModule::logStats(const char *when)
{
    // Una linea con lo que paso desde la anterior: si el hilo principal se atrasa (lag), cuanto trabajo hizo
    // la captura y que paso con el mouse sobre el marco.
    qInfo().noquote() << QStringLiteral("[meetFrame] Resumen (%1, %2 s): hilo principal atrasado max %3 ms, %4 veces mas de 50 ms; "
                                        "region %5 cambios pedidos, %6 aplicados")
                             .arg(QLatin1String(when)).arg(m_statsClock.elapsed() / 1000)
                             .arg(m_watchMaxLateMs).arg(m_watchLate).arg(m_regionRequests).arg(m_regionApplied);
    if (m_session) {
        const QString capture = m_session->takeStats();
        if (!capture.isEmpty()) qInfo().noquote() << "[meetFrame]   captura:" << capture;
    }
    const QString mouse = MeetFrameSystem::takeFrameMouseStats();
    if (!mouse.isEmpty()) qInfo().noquote() << "[meetFrame]  " << mouse;
    m_regionRequests = m_regionApplied = 0;
    m_watchMaxLateMs = 0;
    m_watchLate = 0;
    m_statsClock.restart();
}

QRect MeetFrameModule::savedRegion() const
{
    const QStringList parts = context().value(QStringLiteral("region")).toString().split(QLatin1Char(','));
    if (parts.size() == 4) {
        const QRect saved(parts.at(0).toInt(), parts.at(1).toInt(), parts.at(2).toInt(), parts.at(3).toInt());
        QScreen *screen = QGuiApplication::screenAt(saved.center());
        if (screen && saved.width() >= MeetFrameOverlay::minimumRegion().width() && saved.height() >= MeetFrameOverlay::minimumRegion().height()) {
            return saved;
        }
    }
    // La primera vez (o si esa pantalla ya no esta): 1280 x 720 reales, centrado en la pantalla principal.
    QScreen *screen = QGuiApplication::primaryScreen();
    const QRect area = screen ? screen->availableGeometry() : QRect(0, 0, 1600, 900);
    const qreal dpr = screen ? screen->devicePixelRatio() : 1.0;
    QSize size(qRound(1280 / dpr), qRound(720 / dpr));
    size = size.boundedTo(area.size().shrunkBy(MeetFrameOverlay::frameMargins()));
    QRect region(QPoint(), size);
    region.moveCenter(area.center());
    return region;
}

void MeetFrameModule::saveRegion()
{
    if (!m_overlay) return;
    const QRect r = m_overlay->region();
    context().setValue(QStringLiteral("region"), QStringLiteral("%1,%2,%3,%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height()));
}

void MeetFrameModule::resizeRegion(const QSize &pixels)
{
    if (!m_overlay) return;
    QScreen *screen = m_overlay->screen();
    if (!screen) return;
    const QRect area = screen->availableGeometry().marginsRemoved(MeetFrameOverlay::frameMargins());
    QRect region = m_overlay->region();
    if (pixels.isValid()) {
        const qreal dpr = screen->devicePixelRatio();
        const QPoint center = region.center();
        region.setSize(QSize(qRound(pixels.width() / dpr), qRound(pixels.height() / dpr)).boundedTo(area.size()));
        region.moveCenter(center);
    } else {
        region.moveCenter(area.center());
    }
    // Que la barra y el borde queden adentro de la pantalla.
    if (region.right() > area.right()) region.moveRight(area.right());
    if (region.bottom() > area.bottom()) region.moveBottom(area.bottom());
    if (region.left() < area.left()) region.moveLeft(area.left());
    if (region.top() < area.top()) region.moveTop(area.top());
    m_overlay->setRegion(region);
    onRegionChanged();
    saveRegion();
    MeetFrameSystem::syncFrameMouse(m_overlay);
}

// ------------------------------------------------------------------ Ajustes

MeetFrameSystem::Options MeetFrameModule::options() const
{
    QSet<QString> groups;
    for (const MeetFrameRules::Group &group : MeetFrameRules::groups()) {
        if (groupEnabled(group.id)) groups.insert(group.id);
    }
    QStringList ids;
    for (const CustomApp &app : customApps()) ids << app.id;
    MeetFrameSystem::Options options;
    options.rules = MeetFrameRules::build(groups, ids, hideDesktop(), hideNotifications());
    options.fill = Theme::color(Theme::kSheet);
    options.showCursor = showCursor();
    options.fps = fps();
    options.paused = m_paused;
    return options;
}

void MeetFrameModule::applyOptions()
{
    if (m_session) m_session->setOptions(options());
}

bool MeetFrameModule::groupEnabled(const QString &id) const
{
    for (const MeetFrameRules::Group &group : MeetFrameRules::groups()) {
        if (group.id == id) return context().value(QStringLiteral("group_") + id, group.defaultOn).toBool();
    }
    return false;
}

void MeetFrameModule::setGroupEnabled(const QString &id, bool enabled)
{
    context().setValue(QStringLiteral("group_") + id, enabled);
    applyOptions();
    changed();
}

bool MeetFrameModule::hideNotifications() const
{
    return context().value(QStringLiteral("hideNotifications"), true).toBool();
}

void MeetFrameModule::setHideNotifications(bool hide)
{
    context().setValue(QStringLiteral("hideNotifications"), hide);
    applyOptions();
    changed();
}

bool MeetFrameModule::hideDesktop() const
{
    return context().value(QStringLiteral("hideDesktop"), true).toBool();
}

void MeetFrameModule::setHideDesktop(bool hide)
{
    context().setValue(QStringLiteral("hideDesktop"), hide);
    applyOptions();
    changed();
}

bool MeetFrameModule::showCursor() const
{
    return context().value(QStringLiteral("showCursor"), true).toBool();
}

void MeetFrameModule::setShowCursor(bool show)
{
    context().setValue(QStringLiteral("showCursor"), show);
    applyOptions();
    changed();
}

int MeetFrameModule::fps() const
{
    const int value = context().value(QStringLiteral("fps"), 30).toInt();
    return (value == 15 || value == 30 || value == 60) ? value : 30;
}

void MeetFrameModule::setFps(int fps)
{
    context().setValue(QStringLiteral("fps"), fps);
    applyOptions();
    changed();
}

QList<MeetFrameModule::CustomApp> MeetFrameModule::appsFromJson(const QString &json)
{
    QList<CustomApp> apps;
    QSet<QString> seen;
    const QJsonArray array = QJsonDocument::fromJson(json.toUtf8()).array();
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        CustomApp app;
        app.id = MeetFrameRules::appKey(object.value(QStringLiteral("id")).toString());
        app.name = object.value(QStringLiteral("name")).toString().simplified();
        app.path = object.value(QStringLiteral("path")).toString().trimmed();
        if (app.id.isEmpty() || seen.contains(app.id)) continue;
        if (app.name.isEmpty()) app.name = app.id;
        seen.insert(app.id);
        apps.append(app);
    }
    return apps;
}

QString MeetFrameModule::appsToJson(const QList<CustomApp> &apps)
{
    QJsonArray array;
    for (const CustomApp &app : apps) {
        QJsonObject object;
        object.insert(QStringLiteral("id"), app.id);
        object.insert(QStringLiteral("name"), app.name);
        if (!app.path.isEmpty()) object.insert(QStringLiteral("path"), app.path);
        array.append(object);
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QList<MeetFrameModule::CustomApp> MeetFrameModule::customApps() const
{
    return appsFromJson(context().value(QStringLiteral("apps"), QStringLiteral("[]")).toString());
}

bool MeetFrameModule::addApp(const CustomApp &app)
{
    CustomApp clean = app;
    clean.id = MeetFrameRules::appKey(app.id);
    if (clean.id.isEmpty()) return false;
    if (clean.name.trimmed().isEmpty()) clean.name = clean.id;
    QList<CustomApp> apps = customApps();
    for (const CustomApp &stored : apps) {
        if (stored.id == clean.id) return false;
    }
    apps.append(clean);
    context().setValue(QStringLiteral("apps"), appsToJson(apps));
    applyOptions();
    changed();
    return true;
}

bool MeetFrameModule::removeApp(const QString &id)
{
    QList<CustomApp> apps = customApps();
    for (int i = 0; i < apps.size(); ++i) {
        if (apps.at(i).id != id) continue;
        apps.removeAt(i);
        context().setValue(QStringLiteral("apps"), appsToJson(apps));
        applyOptions();
        changed();
        return true;
    }
    return false;
}

void MeetFrameModule::changed()
{
    syncOverlay();
    emit statusChanged();
    emit settingsChanged();
}

// ------------------------------------------------------------------ Estado, bandeja y panel

ModuleStatus MeetFrameModule::status() const
{
    if (!m_error.isEmpty() || !m_shortcutError.isEmpty() || (m_session && !m_session->error().isEmpty())) {
        return {ModuleTone::Error, I18n::tr("Needs attention")};
    }
    if (m_shown && m_paused) {
        return {ModuleTone::Paused, I18n::tr("Paused")};
    }
    if (m_shown) {
        const int hidden = hiddenWindows();
        if (hidden == 0) return {ModuleTone::Active, I18n::tr("Sharing")};
        return {ModuleTone::Active, hidden == 1 ? I18n::tr("Sharing · 1 hidden") : I18n::tr("Sharing · %1 hidden").arg(hidden)};
    }
    return {ModuleTone::Active, I18n::tr("Ready · %1").arg(shortcut(ToggleShortcut).displayText())};
}

bool MeetFrameModule::isPaused() const
{
    return m_shown && m_paused;
}

void MeetFrameModule::fillTrayMenu(QMenu *menu)
{
    QAction *frame = menu->addAction(m_shown ? I18n::tr("Hide frame") : I18n::tr("Show frame"));
    connect(frame, &QAction::triggered, this, &MeetFrameModule::toggleFrame);
    if (m_shown) {
        QAction *pause = menu->addAction(m_paused ? I18n::tr("Resume") : I18n::tr("Pause"));
        connect(pause, &QAction::triggered, this, &MeetFrameModule::togglePause);
    }
}

QWidget *MeetFrameModule::createPanel(QWidget *parent)
{
    auto *panel = new MeetFramePanel(this, !context().captureMode(), parent);
    connect(this, &MeetFrameModule::settingsChanged, panel, &MeetFramePanel::refresh);
    connect(this, &Module::statusChanged, panel, &MeetFramePanel::refresh);
    return panel;
}

QStringList MeetFrameModule::captureStates() const
{
    return {QStringLiteral("ready"),  QStringLiteral("sharing"), QStringLiteral("paused"),       QStringLiteral("apps"),
            QStringLiteral("error"),  QStringLiteral("permission"), QStringLiteral("frame"),     QStringLiteral("frame-paused")};
}

bool MeetFrameModule::applyCaptureState(const QString &state)
{
    if (!context().captureMode() || !captureStates().contains(state)) {
        return false;
    }
    m_captureState = state;
    m_shown = state == QLatin1String("sharing") || state == QLatin1String("paused") || state == QLatin1String("apps");
    m_paused = state == QLatin1String("paused");
    m_captureHidden = m_shown ? 2 : 0;
    m_error = state == QLatin1String("error") ? I18n::tr("Could not start sharing (%1).").arg(QStringLiteral("CreateWindowEx 5"))
              : state == QLatin1String("permission") ? I18n::tr("Allow screen recording for LGA Mighty Tools, then try again.")
                                                     : QString();
    if (state == QLatin1String("apps")) {
#if defined(Q_OS_MACOS)
        context().setValue(QStringLiteral("apps"), appsToJson({{QStringLiteral("com.apple.notes"), QStringLiteral("Notes"), QStringLiteral("/System/Applications/Notes.app")},
                                                               {QStringLiteral("com.example.longname"), QStringLiteral("An app with a very long name that should be cut nicely"), QString()}}));
#else
        context().setValue(QStringLiteral("apps"), appsToJson({{QStringLiteral("notepad.exe"), QStringLiteral("Notepad"), QStringLiteral("C:\\Windows\\notepad.exe")},
                                                               {QStringLiteral("longname.exe"), QStringLiteral("An app with a very long name that should be cut nicely"), QString()}}));
#endif
        context().setValue(QStringLiteral("group_mail"), true);
    }
    return true;
}

QWidget *MeetFrameModule::createCaptureWidget(const QString &state, QWidget *parent)
{
    if (!state.startsWith(QLatin1String("frame"))) {
        return nullptr;
    }
    // El marco sobre un fondo gris: la barrita y el borde, sin ventana nativa ni captura.
    auto *host = new QWidget(parent);
    host->setObjectName(QStringLiteral("meetFrameShotHost"));
    host->setAutoFillBackground(true);
    QPalette palette = host->palette();
    palette.setColor(QPalette::Window, Theme::color(Theme::kDesk));
    host->setPalette(palette);
    auto *overlay = new MeetFrameOverlay(true, host);
    const QRect region(QPoint(24, 24) + QPoint(MeetFrameOverlay::frameMargins().left(), MeetFrameOverlay::frameMargins().top()), QSize(560, 315));
    overlay->setRegion(region);
    overlay->setState(state == QLatin1String("frame-paused"), 2, QSize(1280, 720), QString());
    overlay->setShortcuts(shortcut(ToggleShortcut).displayText(), shortcut(PauseShortcut).displayText());
    host->setFixedSize(region.marginsAdded(MeetFrameOverlay::frameMargins()).bottomRight().x() + 24,
                 region.marginsAdded(MeetFrameOverlay::frameMargins()).bottomRight().y() + 24);
    return host;
}

// ------------------------------------------------------------------ Descriptor y ayuda

ModuleDescriptor meetFrameDescriptor()
{
    ModuleDescriptor d;
    d.id = QStringLiteral("meetFrame");
    d.title = QStringLiteral("Meet Frame");
    d.description = I18n::tr("Share part of your screen in Teams, Meet or Zoom, without your chats or notifications.");
    d.offBulletsFor = [](const SettingsReader &value) {
        const Shortcut keys = Shortcut::fromPortableString(
            value(QStringLiteral("shortcutToggle"), MeetFrameModule::defaultShortcut(MeetFrameModule::ToggleShortcut).toPortableString()).toString());
        return QStringList{I18n::tr("Press %1 to show a frame, then share the window named Meet Frame")
                               .arg((keys.isValid() ? keys : MeetFrameModule::defaultShortcut(MeetFrameModule::ToggleShortcut)).displayText()),
                           I18n::tr("Covers notifications, the desktop and the apps you choose"),
                           I18n::tr("Pause freezes the picture while you do something private")};
    };
    d.offBullets = d.offBulletsFor([](const QString &, const QVariant &fallback) { return fallback; });
    d.platforms = PlatformWindows | PlatformMac;
    d.paintIcon = &paintMeetFrameIcon;
    d.create = [](ModuleContext &context) -> std::unique_ptr<Module> {
        return std::make_unique<MeetFrameModule>(context);
    };
    d.configuredShortcuts = [](const SettingsReader &value) {
        QList<Shortcut> list;
        for (int id : {MeetFrameModule::ToggleShortcut, MeetFrameModule::PauseShortcut}) {
            const Shortcut keys = Shortcut::fromPortableString(value(shortcutKey(id), MeetFrameModule::defaultShortcut(id).toPortableString()).toString());
            list << (keys.isValid() ? keys : MeetFrameModule::defaultShortcut(id));
        }
        return list;
    };
    d.selfTest = &selfTest;
    d.simulateAction = &simulateAction;
    return d;
}

HelpSection meetFrameHelp(const SettingsReader &value)
{
    const auto keysFor = [&value](int id) {
        const Shortcut keys = Shortcut::fromPortableString(value(shortcutKey(id), MeetFrameModule::defaultShortcut(id).toPortableString()).toString());
        return (keys.isValid() ? keys : MeetFrameModule::defaultShortcut(id)).displayText();
    };
    HelpSection help;
    help.title = QStringLiteral("Meet Frame");
    help.steps = {I18n::tr("Press %1 to show the frame. Move it by its bar and resize it from the edges.").arg(HelpSection::strong(keysFor(MeetFrameModule::ToggleShortcut))),
                  I18n::tr("In Teams, Meet or Zoom, share the window named Meet Frame. People see what's inside the frame."),
                  I18n::tr("Notifications, the desktop and the apps you choose are covered in what you share. %1 freezes the picture and resumes it.")
                      .arg(HelpSection::strong(keysFor(MeetFrameModule::PauseShortcut)))};
    help.note = I18n::tr("Only what you share changes: on your screen everything stays the same. The shared window stays behind everything; you don't need to see it.");
    return help;
}
