#include "modules/windowlayouts/WindowLayoutsModule.h"

#include "app/ModuleContext.h"
#include "core/I18n.h"
#include "modules/windowlayouts/LayoutGeometry.h"
#include "modules/windowlayouts/WindowLayoutsPanel.h"
#include "platform/WindowPlacement.h"
#include "ui/Theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QHash>

namespace {

void paintLayoutIcon(QPainter &painter, const QRectF &rect, const QColor &color)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF box = rect.adjusted(rect.width() * .14, rect.height() * .14,
                                     -rect.width() * .14, -rect.height() * .14);
    painter.setPen(QPen(color, 1.4));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(box, 1.5, 1.5);
    painter.drawLine(QPointF(box.center().x(), box.top()), QPointF(box.center().x(), box.bottom()));
    painter.drawLine(QPointF(box.left(), box.center().y()), QPointF(box.right(), box.center().y()));
    painter.restore();
}

class RejectingHotkeys : public ModuleHotkeys
{
public:
    bool registerHotkey(int, const Shortcut &) override { return false; }
    bool registerPriorityHotkey(int, const Shortcut &) override { return false; }
    void unregisterHotkey(int) override {}
    void unregisterAll() override {}
    bool isRegistered(int) const override { return false; }
    bool probe(const Shortcut &) override { return true; }
    void passThrough(const Shortcut &) override {}
    QString declare(int, const Shortcut &) override { return {}; }
    QString declaredByOtherModule(const Shortcut &) const override { return {}; }
};

class RejectionContext : public ModuleContext
{
public:
    QString moduleId() const override { return QStringLiteral("windowLayouts"); }
    QString moduleTitle() const override { return QStringLiteral("Window Layouts"); }
    QVariant value(const QString &key, const QVariant &fallback = {}) const override { return m_values.value(key, fallback); }
    void setValue(const QString &key, const QVariant &value) override { m_values.insert(key, value); }
    void removeValue(const QString &key) override { m_values.remove(key); }
    bool captureMode() const override { return false; }
    bool dryRunInput() const override { return true; }
    bool automatedRun() const override { return true; }
    bool persistentRegistrationAllowed() const override { return false; }
    ModuleHotkeys *hotkeys() override { return &m_hotkeys; }
    InputInjector *injector() override { return nullptr; }
    ForegroundWatcher *foreground() override { return nullptr; }
    void notify(const QString &, const QString &, NoticeIcon, int) override {}
    void showPanel() override {}
    void hideWindowTemporarily() override {}
    void restoreWindow() override {}
    QWidget *window() const override { return nullptr; }

private:
    QHash<QString, QVariant> m_values;
    RejectingHotkeys m_hotkeys;
};

void selfTest(const std::function<void(bool, const QString &)> &check)
{
    using namespace LayoutGeometry;
    check(Shortcut::fromPortableString(QStringLiteral("Ctrl+Alt+L")).isValid()
              && Shortcut::fromPortableString(QStringLiteral("Ctrl+Alt+Shift+L")).isValid()
              && !Shortcut::fromPortableString(QStringLiteral("Ctrl+Home")).isValid(),
          QStringLiteral("atajos: las combinaciones de ventanas se admiten, Ctrl+Home no"));
    const QRectF mixed = grid({3, 0}, {4, 1});
    check(validArea(mixed) && qAbs(mixed.x()) < 1e-9 && qAbs(mixed.y() - .25) < 1e-9
              && qAbs(mixed.width() - 1.0 / 3.0) < 1e-9 && qAbs(mixed.height() - .25) < 1e-9,
          QStringLiteral("grid: tercio horizontal combinado con segundo cuarto vertical"));
    check(!validArea(grid({0, 0}, {1, 0})) && !validArea(grid({3, 3}, {1, 0})),
          QStringLiteral("grid: ejes invalidos rechazados"));
    const QRect work(100, 50, 900, 600);
    check(resolve(work, mixed) == QRect(100, 200, 300, 150),
          QStringLiteral("grid: fracciones resueltas dentro del area util desplazada"));
    const QRectF clipped = capture(QRect(50, 100, 200, 200), work);
    check(validArea(clipped) && resolve(work, clipped) == QRect(100, 100, 150, 200),
          QStringLiteral("captura: rectangulo recortado al area util"));
    check(!validArea(capture(QRect(0, 0, 20, 20), work)),
          QStringLiteral("captura: fuera del monitor no guarda destino"));

    WindowLayoutsState state;
    WindowLayout layout;
    layout.name = QStringLiteral("Mixto");
    layout.shortcut = Shortcut::fromPortableString(QStringLiteral("Ctrl+Alt+L"));
    layout.horizontal = {3, 0};
    layout.vertical = {4, 1};
    check(state.upsert(layout) && state.layouts().size() == 1 && state.layouts().first().id == 1,
          QStringLiteral("estado: primer id local estable"));
    const QList<WindowLayout> loaded = WindowLayoutsState::fromJson(WindowLayoutsState::toJson(state.layouts()));
    check(loaded.size() == 1 && loaded.first().shortcut == layout.shortcut
              && loaded.first().horizontal.parts == 3 && loaded.first().vertical.parts == 4,
          QStringLiteral("estado: persistencia de atajo y ambos ejes"));
    check(WindowLayoutsState::fromJson(QStringLiteral("[{\"id\":1,\"name\":\"x\",\"horizontalParts\":9}]")).isEmpty(),
          QStringLiteral("estado: ajustes corruptos no crean un layout"));
    check(state.remove(1) && state.layouts().isEmpty(), QStringLiteral("estado: borrar libera el id local"));
    WindowLayout minimize;
    minimize.name = QStringLiteral("Minimizar");
    minimize.shortcut = Shortcut::fromPortableString(QStringLiteral("Ctrl+Alt+Down"));
    minimize.destination = LayoutDestination::Minimize;
    check(state.upsert(minimize) && !LayoutGeometry::validArea(minimize.normalizedArea()),
          QStringLiteral("estado: minimizar no requiere area"));
    const QList<WindowLayout> restored = WindowLayoutsState::fromJson(WindowLayoutsState::toJson(state.layouts()));
    QString minimizeError;
    check(restored.size() == 1 && restored.first().destination == LayoutDestination::Minimize
              && WindowPlacement::minimizeForeground(&minimizeError, true) && minimizeError.isEmpty(),
          QStringLiteral("estado: minimizar persiste y dry-run no toca ventanas"));
    minimize.enabled = false;
    check(!WindowLayoutsState::fromJson(WindowLayoutsState::toJson({minimize})).first().enabled,
          QStringLiteral("estado: una disposicion apagada persiste"));
    // El descriptor y el constructor no registran nada: solo start() toma el servicio de atajos.
    RejectionContext context;
    WindowLayoutsModule module(context);
    module.start();
    WindowLayout rejected = layout;
    rejected.id = 0;
    QString error;
    check(!module.saveLayout(rejected, &error) && !error.isEmpty()
              && module.layoutsState().layouts().isEmpty()
              && context.value(QStringLiteral("layouts")).toString() == QStringLiteral("[]"),
          QStringLiteral("registro: rechazo entre probe y register revierte el primer layout persistido"));
    rejected.enabled = false;
    error.clear();
    check(module.saveLayout(rejected, &error) && module.layoutsState().layouts().size() == 1
              && !module.layoutsState().layouts().first().enabled && module.registrationFailureCount() == 0,
          QStringLiteral("registro: una disposicion apagada se guarda sin registrar atajo"));
    const int firstId = module.layoutsState().layouts().first().id;
    WindowLayout second = rejected;
    second.shortcut = Shortcut::fromPortableString(QStringLiteral("Ctrl+Alt+J"));
    check(module.saveLayout(second) && module.layoutsState().layouts().size() == 2,
          QStringLiteral("registro: dos disposiciones apagadas persisten por separado"));
    const int secondId = module.layoutsState().layouts().last().id;
    check(module.setLayoutEnabled(firstId, true) && module.registrationFailureCount() == 1,
          QStringLiteral("registro: encender un atajo rechazado muestra su fallo individual"));
    check(module.setLayoutEnabled(secondId, true) && module.registrationFailureCount() == 2
              && !module.registrationError(firstId).isEmpty() && !module.registrationError(secondId).isEmpty(),
          QStringLiteral("registro: los dos fallos aparecen sin pisarse"));
    check(module.setLayoutEnabled(firstId, false) && module.registrationFailureCount() == 1
              && module.registrationError(firstId).isEmpty() && !module.registrationError(secondId).isEmpty(),
          QStringLiteral("registro: apagar uno conserva el fallo del otro"));
    module.stop();
}

} // namespace

WindowLayoutsModule::WindowLayoutsModule(ModuleContext &context)
    : Module(context), m_state(&context)
{
}

WindowLayoutsModule::~WindowLayoutsModule()
{
    stop();
}

void WindowLayoutsModule::start()
{
    if (m_started) {
        return;
    }
    m_started = true;
    registerLayouts();
}

void WindowLayoutsModule::stop()
{
    if (!m_started) {
        return;
    }
    m_started = false;
    if (m_picker) {
        m_picker->close();
        m_picker->deleteLater();
        m_picker = nullptr;
        context().restoreWindow();
    }
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

void WindowLayoutsModule::registerLayouts()
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
    m_lastError.clear();
    for (const WindowLayout &layout : m_state.layouts()) {
        if (!layout.enabled || !layout.shortcut.isValid()) {
            continue;
        }
        if (!m_hotkeys) {
            m_hotkeys = context().hotkeys();
            connect(m_hotkeys, &ModuleHotkeys::activated, this, &WindowLayoutsModule::onHotkey);
        }
        const QString takenBy = m_hotkeys->declare(layout.id, layout.shortcut);
        if (!takenBy.isEmpty()) {
            m_registrationErrors.insert(layout.id, I18n::tr("%1 is already used by %2.").arg(layout.shortcut.displayText(), takenBy));
            continue;
        }
        m_declaredIds.insert(layout.id);
        const bool shared = !m_hotkeys->probe(layout.shortcut);
        if (shared) m_externalConflicts.insert(layout.id);
#ifdef Q_OS_WIN
        const bool registered = m_hotkeys->registerPriorityHotkey(layout.id, layout.shortcut);
#else
        const bool registered = m_hotkeys->registerHotkey(layout.id, layout.shortcut);
#endif
        if (!registered) {
            m_registrationErrors.insert(layout.id,
#ifdef Q_OS_WIN
                                        I18n::tr("Could not activate %1 with priority.").arg(layout.shortcut.displayText())
#else
                                        shared ? I18n::tr("Another app or macOS uses %1.").arg(layout.shortcut.displayText())
                                               : I18n::tr("Could not activate %1.").arg(layout.shortcut.displayText())
#endif
            );
        }
    }
    emit statusChanged();
    emit layoutsChanged();
}

void WindowLayoutsModule::onHotkey(int id)
{
    if (!m_started) {
        return;
    }
    for (const WindowLayout &layout : m_state.layouts()) {
        if (layout.id != id || !layout.enabled) {
            continue;
        }
        QString error;
        const bool succeeded = layout.destination == LayoutDestination::Minimize
            ? WindowPlacement::minimizeForeground(&error, context().automatedRun())
            : WindowPlacement::moveForeground(layout.normalizedArea(), &error, context().automatedRun());
        if (!succeeded) {
            m_lastError = error.isEmpty() ? I18n::tr("This window could not be moved or minimized.") : error;
            emit statusChanged();
            emit layoutsChanged();
        } else if (!m_lastError.isEmpty()) {
            m_lastError.clear();
            emit statusChanged();
            emit layoutsChanged();
        }
        break;
    }
}

ModuleStatus WindowLayoutsModule::status() const
{
    if (!m_lastError.isEmpty() || !m_registrationErrors.isEmpty()) {
        return {ModuleTone::Error, I18n::tr("Needs attention")};
    }
    int count = 0;
    for (const WindowLayout &layout : m_state.layouts()) count += layout.enabled;
    if (count == 0) {
        return {ModuleTone::Active, m_state.layouts().isEmpty() ? I18n::tr("No shortcuts yet")
                                                            : I18n::tr("All shortcuts are off")};
    }
    return {ModuleTone::Active,
            count == 1 ? I18n::tr("1 shortcut ready") : I18n::tr("%1 shortcuts ready").arg(count)};
}

QWidget *WindowLayoutsModule::createPanel(QWidget *parent)
{
    auto *panel = new WindowLayoutsPanel(this, !context().captureMode(), parent);
    connect(this, &WindowLayoutsModule::layoutsChanged, panel, &WindowLayoutsPanel::refresh);
    if (m_captureState == QLatin1String("editor")) panel->showEditorForCapture();
    return panel;
}

QStringList WindowLayoutsModule::captureStates() const
{
    return {QStringLiteral("list"), QStringLiteral("long-name"), QStringLiteral("editor"),
            QStringLiteral("shared"), QStringLiteral("failed"), QStringLiteral("minimize"), QStringLiteral("picker"),
            QStringLiteral("draw")};
}

bool WindowLayoutsModule::applyCaptureState(const QString &state)
{
    if (!context().captureMode() || !captureStates().contains(state)) {
        return false;
    }
    m_captureState = state;
    if (state == QLatin1String("minimize")) {
        WindowLayout layout;
        layout.id = 1;
        layout.name = I18n::tr("Minimize");
        layout.shortcut = Shortcut::fromPortableString(QStringLiteral("Ctrl+Alt+Down"));
        layout.destination = LayoutDestination::Minimize;
        m_state.setCaptureLayouts({layout});
    } else if (state == QLatin1String("list") || state == QLatin1String("long-name")
               || state == QLatin1String("shared") || state == QLatin1String("failed")) {
        WindowLayout a;
        a.id = 1;
        a.name = I18n::tr("Left third");
        a.shortcut = Shortcut::fromPortableString(QStringLiteral("Ctrl+Alt+L"));
        a.horizontal = {3, 0};
        WindowLayout b;
        b.id = 2;
        b.name = state == QLatin1String("long-name")
            ? QStringLiteral("Long personalized layout name that should leave the shortcut and pencil visible")
            : I18n::tr("Three quarters left");
        b.shortcut = Shortcut::fromPortableString(QStringLiteral("Meta+Ctrl+Alt+Shift+Left"));
        b.destination = LayoutDestination::Custom;
        b.customArea = QRectF(0, 0, .75, 1);
        m_state.setCaptureLayouts({a, b});
        if (state == QLatin1String("shared")) m_externalConflicts.insert(b.id);
        if (state == QLatin1String("failed")) {
            m_externalConflicts.insert(a.id);
            m_externalConflicts.insert(b.id);
            m_registrationErrors.insert(a.id, I18n::tr("Could not activate %1 with priority.").arg(a.shortcut.displayText()));
            m_registrationErrors.insert(b.id, I18n::tr("Could not activate %1 with priority.").arg(b.shortcut.displayText()));
        }
    } else {
        m_state.setCaptureLayouts({});
    }
    return true;
}

QWidget *WindowLayoutsModule::createCaptureWidget(const QString &state, QWidget *parent)
{
    if (state == QLatin1String("picker") || state == QLatin1String("draw")) {
        return new AreaPicker(state == QLatin1String("draw") ? AreaPicker::Mode::Draw : AreaPicker::Mode::Window,
                              true, parent);
    }
    return nullptr;
}

QString WindowLayoutsModule::validateShortcut(int editingId, const Shortcut &shortcut, bool enabled) const
{
    if (!shortcut.isValid()) {
        return I18n::tr("Choose a shortcut.");
    }
    for (const WindowLayout &layout : m_state.layouts()) {
        if (layout.id != editingId && layout.shortcut == shortcut) {
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

bool WindowLayoutsModule::saveLayout(WindowLayout layout, QString *error)
{
    const QString shortcutError = validateShortcut(layout.id, layout.shortcut, layout.enabled);
    if (!shortcutError.isEmpty()) {
        if (error) *error = shortcutError;
        return false;
    }
    WindowLayout previous;
    for (const WindowLayout &stored : m_state.layouts()) {
        if (stored.id == layout.id) previous = stored;
    }
    if (!m_state.upsert(layout)) {
        if (error) *error = I18n::tr("Check the name and area, or remove a shortcut to make room.");
        return false;
    }
    registerLayouts();
    if (layout.enabled && m_registrationErrors.contains(layout.id)) {
        const QString failure = m_registrationErrors.value(layout.id);
        if (previous.id) {
            m_state.upsert(previous);
        } else {
            m_state.remove(layout.id);
        }
        registerLayouts();
        if (error) *error = failure;
        return false;
    }
    return true;
}

bool WindowLayoutsModule::setLayoutEnabled(int id, bool enabled)
{
    for (WindowLayout layout : m_state.layouts()) {
        if (layout.id != id) continue;
        if (layout.enabled == enabled) return true;
        layout.enabled = enabled;
        if (!m_state.upsert(layout)) return false;
        registerLayouts();
        return true;
    }
    return false;
}

bool WindowLayoutsModule::removeLayout(int id)
{
    if (!m_state.remove(id)) {
        return false;
    }
    registerLayouts();
    return true;
}

void WindowLayoutsModule::openAreaPicker(AreaPicker::Mode mode, std::function<void(const QRectF &)> chosen)
{
    if (!m_started || m_picker || context().automatedRun()) {
        return;
    }
    auto *picker = new AreaPicker(mode, false);
    if (!picker->error().isEmpty()) {
        m_lastError = picker->error();
        picker->deleteLater();
        emit statusChanged();
        emit layoutsChanged();
        return;
    }
    if (mode == AreaPicker::Mode::Window && !picker->hasCandidates()) {
        m_lastError = I18n::tr("No selectable windows found. Draw an area instead.");
        picker->deleteLater();
        emit statusChanged();
        emit layoutsChanged();
        return;
    }
    m_picker = picker;
    picker->setChosen(std::move(chosen));
    connect(picker, &QObject::destroyed, this, [this]() {
        m_picker = nullptr;
        if (m_started) context().restoreWindow();
    });
    context().hideWindowTemporarily();
    picker->open();
}

ModuleDescriptor windowLayoutsDescriptor()
{
    ModuleDescriptor d;
    d.id = QStringLiteral("windowLayouts");
    d.title = QStringLiteral("Window Layouts");
    d.description = I18n::tr("Move or minimize the active window with a shortcut.");
    d.offBullets = {I18n::tr("Combine horizontal and vertical halves, thirds and quarters"),
                    I18n::tr("Draw an area or copy an open window's bounds"),
                    I18n::tr("Keeps clear of a visible taskbar, Dock and menu bar")};
    d.platforms = PlatformWindows | PlatformMac;
    d.paintIcon = &paintLayoutIcon;
    d.create = [](ModuleContext &context) -> std::unique_ptr<Module> {
        return std::make_unique<WindowLayoutsModule>(context);
    };
    d.configuredShortcuts = [](const SettingsReader &read) {
        QList<Shortcut> shortcuts;
        for (const WindowLayout &layout : WindowLayoutsState::fromJson(read(QStringLiteral("layouts"), {}).toString())) {
            if (layout.enabled && layout.shortcut.isValid()) shortcuts.append(layout.shortcut);
        }
        return shortcuts;
    };
    d.selfTest = &selfTest;
    return d;
}

HelpSection windowLayoutsHelp(const SettingsReader &)
{
    HelpSection help;
    help.title = QStringLiteral("Window Layouts");
    help.steps = {I18n::tr("Add a shortcut and choose a quick preset, or combine horizontal and vertical fractions."),
                  I18n::tr("Draw a destination area or click an open window to copy its bounds."),
                  I18n::tr("Record a shortcut. It acts on the window in front when you press it.")};
    help.note = I18n::tr("Visible system bars are excluded. Some protected windows may require accessibility permission or cannot be moved.");
    return help;
}
