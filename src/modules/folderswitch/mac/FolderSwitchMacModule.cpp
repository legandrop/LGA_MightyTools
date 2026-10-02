#include "modules/folderswitch/mac/FolderSwitchMacModule.h"

#include "app/ModuleContext.h"
#include "core/I18n.h"
#include "modules/folderswitch/FolderSwitchLogic.h"
#include "modules/folderswitch/RecentFoldersPopup.h"
#include "platform/ForegroundWatcher.h"
#include "platform/SystemInput.h"
#include "ui/ShortcutRow.h"

#include <QAction>
#include <QCoreApplication>
#include <QCursor>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QUrl>

namespace {
constexpr int kManualHotkeyId = 1;
constexpr int kRecentHotkeyId = 2;
// Mientras falta el permiso de Accesibilidad, cada cuanto se vuelve a mirar (como Nuke Shortcuts). Con
// el permiso dado, el timer se apaga.
constexpr int kAccessibilityPollMs = 2000;
const QString kSourceFinder = QStringLiteral("Finder");
const QString kSourceRecent = QStringLiteral("Recent");
} // namespace

FolderSwitchMacModule::FolderSwitchMacModule(ModuleContext &context)
    : Module(context)
    , m_state(&context)
{
}

FolderSwitchMacModule::~FolderSwitchMacModule() = default;

void FolderSwitchMacModule::declareAndRegisterShortcuts()
{
    ModuleHotkeys *hk = context().hotkeys();
    const Shortcut manual = m_state.manualShortcut();
    const Shortcut recent = m_state.recentShortcut();
    m_manualTakenBy = hk->declare(kManualHotkeyId, manual);
    m_recentTakenBy = hk->declare(kRecentHotkeyId, recent);
    m_manualRegistered = m_manualTakenBy.isEmpty() && hk->registerHotkey(kManualHotkeyId, manual);
    m_recentRegistered = m_recentTakenBy.isEmpty() && hk->registerHotkey(kRecentHotkeyId, recent);
    qInfo() << "[folderSwitch] Atajo manual" << manual.toPortableString() << (m_manualRegistered ? "registrado" : "NO registrado")
            << "| recientes" << recent.toPortableString() << (m_recentRegistered ? "registrado" : "NO registrado");
}

void FolderSwitchMacModule::start()
{
    declareAndRegisterShortcuts();
    ForegroundWatcher *watcher = context().foreground();
    m_foregroundAcquired = true;
    connect(watcher, &ForegroundWatcher::foregroundChanged, this, &FolderSwitchMacModule::onForegroundChanged);
    connect(context().hotkeys(), &ModuleHotkeys::activated, this, &FolderSwitchMacModule::onHotkeyActivated);
    m_prevPid = watcher->foregroundPid();

    refreshAccessibility();
    if (!context().automatedRun() && !m_accessibilityGranted) {
        m_accessibilityTimer = new QTimer(this);
        m_accessibilityTimer->setInterval(kAccessibilityPollMs);
        connect(m_accessibilityTimer, &QTimer::timeout, this, &FolderSwitchMacModule::refreshAccessibility);
        m_accessibilityTimer->start();
    }
}

void FolderSwitchMacModule::stop()
{
    if (m_recentPopup) {
        m_recentPopup->hide();
    }
    delete m_accessibilityTimer;
    m_accessibilityTimer = nullptr;

    disconnect(context().hotkeys(), nullptr, this, nullptr);
    context().hotkeys()->unregisterAll();
    m_manualRegistered = false;
    m_recentRegistered = false;
    m_manualTakenBy.clear();
    m_recentTakenBy.clear();

    if (m_foregroundAcquired) {
        disconnect(context().foreground(), nullptr, this, nullptr);
        m_foregroundAcquired = false;
    }
    m_prevPid = 0;
    m_lastFinderSeenMs = 0;
    m_pendingReturnDialog = {};
    m_lastSwitchedDialog = {};
    m_lastDialog = {};
}

void FolderSwitchMacModule::refreshAccessibility()
{
    const bool granted = context().automatedRun() ? m_accessibilityGranted : SystemInput::accessibilityTrusted(false);
    if (granted == m_accessibilityGranted && m_accessibilityTimer) {
        return;
    }
    m_accessibilityGranted = granted;
    if (granted && m_accessibilityTimer) {
        m_accessibilityTimer->stop();
    }
    refreshPanel();
    emit statusChanged();
}

void FolderSwitchMacModule::openAccessibilitySettings()
{
    if (context().automatedRun()) {
        qInfo() << "[folderSwitch] (automatizada) se abriria Ajustes > Accesibilidad";
        return;
    }
    // Un solo paso por click, como Nuke Shortcuts: la primera vez el cartel del sistema (agrega la app a
    // la lista y trae su boton para abrir Ajustes); despues, Ajustes directo.
    const QString promptedKey = QStringLiteral("accessibilityPrompted");
    if (!context().value(promptedKey, false).toBool()) {
        context().setValue(promptedKey, true);
        SystemInput::accessibilityTrusted(true);
    } else {
        QDesktopServices::openUrl(QUrl(QStringLiteral("x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility")));
    }
    if (!m_accessibilityTimer) {
        m_accessibilityTimer = new QTimer(this);
        m_accessibilityTimer->setInterval(kAccessibilityPollMs);
        connect(m_accessibilityTimer, &QTimer::timeout, this, &FolderSwitchMacModule::refreshAccessibility);
    }
    m_accessibilityTimer->start();
}

ModuleStatus FolderSwitchMacModule::status() const
{
    ModuleStatus s;
    if (!m_accessibilityGranted) {
        s.tone = ModuleTone::Attention;
        s.text = I18n::tr("Accessibility needed");
        return s;
    }
    if (!m_manualRegistered || !m_recentRegistered) {
        s.tone = ModuleTone::Error;
        if (!m_manualRegistered && !m_recentRegistered) {
            s.text = QStringLiteral("Shortcuts off");
        } else {
            const Shortcut bad = m_manualRegistered ? m_state.recentShortcut() : m_state.manualShortcut();
            s.text = QStringLiteral("%1 off").arg(bad.displayText());
        }
        return s;
    }
    if (!m_state.enabled()) {
        s.tone = ModuleTone::Paused;
        s.text = I18n::tr("Paused");
        return s;
    }
    s.tone = ModuleTone::Active;
    s.text = I18n::tr("On");
    return s;
}

bool FolderSwitchMacModule::isPaused() const
{
    return !m_state.enabled();
}

FolderSwitchPanel::ViewState FolderSwitchMacModule::currentViewState() const
{
    FolderSwitchPanel::ViewState v;
    v.enabled = m_state.enabled();
    v.autoSwitch = m_state.autoSwitch();
    v.manualShortcut = m_state.manualShortcut();
    v.recentShortcut = m_state.recentShortcut();
    v.manualRegistered = m_manualRegistered;
    v.recentRegistered = m_recentRegistered;
    v.lastSwitch = m_state.lastSwitch();
    v.needsAccessibility = !m_accessibilityGranted;
    return v;
}

void FolderSwitchMacModule::refreshPanel()
{
    if (m_panel) {
        m_panel->setState(currentViewState());
    }
}

QWidget *FolderSwitchMacModule::createPanel(QWidget *parent)
{
    auto *panel = new FolderSwitchPanel(parent);
    m_panel = panel;
    panel->setState(currentViewState());
    panel->manualRow()->setValidator([this](const Shortcut &candidate) { return validateShortcut(candidate); });
    panel->recentRow()->setValidator([this](const Shortcut &candidate) { return validateShortcut(candidate); });
    connect(panel, &FolderSwitchPanel::toggleRequested, this, &FolderSwitchMacModule::setEnabled);
    connect(panel, &FolderSwitchPanel::autoSwitchToggled, this, &FolderSwitchMacModule::setAutoSwitch);
    connect(panel, &FolderSwitchPanel::manualShortcutRecorded, this, &FolderSwitchMacModule::setManualShortcut);
    connect(panel, &FolderSwitchPanel::recentShortcutRecorded, this, &FolderSwitchMacModule::setRecentShortcut);
    connect(panel, &FolderSwitchPanel::accessibilityRequested, this, &FolderSwitchMacModule::openAccessibilitySettings);

    if (m_pendingRowFixture == QLatin1String("recording")) {
        panel->manualRow()->showRecordingFixture(QStringLiteral("Ctrl + Alt + ..."));
    } else if (m_pendingRowFixture == QLatin1String("rejected")) {
        Shortcut rejected;
        rejected.modifiers = Qt::ControlModifier | Qt::AltModifier;
        rejected.key = Qt::Key_K;
        panel->manualRow()->setError(I18n::tr("%1 Kept %2.")
                                         .arg(I18n::tr("%1 is taken by another app.").arg(rejected.displayText()),
                                              m_state.manualShortcut().displayText()));
    }
    return panel;
}

void FolderSwitchMacModule::fillTrayMenu(QMenu *menu)
{
    // En mac la barra de menu no muestra menu (D-39); queda por contrato del modulo.
    QAction *toggle = menu->addAction(m_state.enabled() ? I18n::tr("Pause switching") : I18n::tr("Resume switching"));
    connect(toggle, &QAction::triggered, this, [this]() { setEnabled(!m_state.enabled()); });
}

void FolderSwitchMacModule::setEnabled(bool enabled)
{
    if (enabled == m_state.enabled()) {
        return;
    }
    m_state.setEnabled(enabled);
    refreshPanel();
    emit statusChanged();
}

void FolderSwitchMacModule::setAutoSwitch(bool autoSwitch)
{
    if (autoSwitch == m_state.autoSwitch()) {
        return;
    }
    m_state.setAutoSwitch(autoSwitch);
    refreshPanel();
}

QString FolderSwitchMacModule::validateShortcut(const Shortcut &candidate) const
{
    ModuleHotkeys *hk = context().hotkeys();
    const QString other = hk->declaredByOtherModule(candidate);
    if (!other.isEmpty()) {
        return I18n::tr("Already used by %1.").arg(other);
    }
    if (!hk->probe(candidate)) {
        return I18n::tr("%1 is taken by another app.").arg(candidate.displayText());
    }
    return QString();
}

bool FolderSwitchMacModule::setManualShortcut(const Shortcut &shortcut)
{
    ModuleHotkeys *hk = context().hotkeys();
    const Shortcut previous = m_state.manualShortcut();
    const bool ok = hk->declare(kManualHotkeyId, shortcut).isEmpty() && hk->registerHotkey(kManualHotkeyId, shortcut);
    if (ok) {
        m_state.setManualShortcut(shortcut);
        m_manualRegistered = true;
    } else {
        hk->declare(kManualHotkeyId, previous);
        m_manualRegistered = hk->registerHotkey(kManualHotkeyId, previous);
    }
    m_manualTakenBy.clear();
    refreshPanel();
    emit statusChanged();
    return ok;
}

bool FolderSwitchMacModule::setRecentShortcut(const Shortcut &shortcut)
{
    ModuleHotkeys *hk = context().hotkeys();
    const Shortcut previous = m_state.recentShortcut();
    const bool ok = hk->declare(kRecentHotkeyId, shortcut).isEmpty() && hk->registerHotkey(kRecentHotkeyId, shortcut);
    if (ok) {
        m_state.setRecentShortcut(shortcut);
        m_recentRegistered = true;
    } else {
        hk->declare(kRecentHotkeyId, previous);
        m_recentRegistered = hk->registerHotkey(kRecentHotkeyId, previous);
    }
    m_recentTakenBy.clear();
    refreshPanel();
    emit statusChanged();
    return ok;
}

void FolderSwitchMacModule::onForegroundChanged(quintptr, quint32 pidValue, const QString &)
{
    // Nunca se toca un dialogo ajeno en una corrida automatizada.
    if (context().automatedRun() || !m_accessibilityGranted) {
        return;
    }
    const qint64 pid = pidValue;
    // Esta app (la ventana de ajustes, el popup de recientes) no cuenta como ida ni como vuelta.
    if (pid <= 0 || pid == QCoreApplication::applicationPid()) {
        return;
    }
    const qint64 prev = m_prevPid;
    m_prevPid = pid;

    // El dialogo del que se va el usuario (lo abrio dentro de su app, sin cambio de app): queda como el
    // ultimo dialogo visto, aunque despues pase por otras apps antes de llegar al Finder (como en Windows,
    // "inmune a ventanas intermedias").
    if (prev > 0 && !MacFileDialogs::isFinder(prev)) {
        const MacFileDialogs::Dialog from = MacFileDialogs::focusedDialog(prev);
        if (from.isValid()) {
            m_lastDialog = from;
        }
    }

    // Historial: la carpeta que queda en el Finder cuando el usuario se va.
    if (prev > 0 && MacFileDialogs::isFinder(prev) && !MacFileDialogs::isFinder(pid)) {
        recordFinderFolder();
    }

    if (MacFileDialogs::isFinder(pid)) {
        m_lastFinderSeenMs = QDateTime::currentMSecsSinceEpoch();
        m_lastSwitchedDialog = {};
        if (MacFileDialogs::isOpen(m_lastDialog)) {
            m_pendingReturnDialog = m_lastDialog;
        }
        return;
    }

    const MacFileDialogs::Dialog dialog = MacFileDialogs::focusedDialog(pid);
    if (!dialog.isValid()) {
        return;
    }
    m_lastDialog = dialog;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const bool finderFresh = m_lastFinderSeenMs > 0 && FolderSwitchLogic::isManagerFresh(m_lastFinderSeenMs, nowMs);
    if (FolderSwitchLogic::shouldAutoSwitch(m_state.autoSwitch(), m_state.enabled(), finderFresh,
                                            MacFileDialogs::same(dialog, m_pendingReturnDialog),
                                            MacFileDialogs::same(dialog, m_lastSwitchedDialog))) {
        m_pendingReturnDialog = {};
        m_lastSwitchedDialog = dialog;
        scheduleSwitch(dialog);
    }
}

void FolderSwitchMacModule::onHotkeyActivated(int localId)
{
    if (context().automatedRun()) {
        return;
    }
    if (localId == kManualHotkeyId) {
        handleManualHotkey();
    } else if (localId == kRecentHotkeyId) {
        handleRecentHotkey();
    }
}

void FolderSwitchMacModule::handleManualHotkey()
{
    // Como en Windows: funciona tambien en pausa; solo exige un dialogo de archivos al frente. La
    // carpeta es la de la ventana del frente del Finder.
    const MacFileDialogs::Dialog dialog = MacFileDialogs::focusedDialog(MacFileDialogs::frontPid());
    if (!dialog.isValid()) {
        qDebug() << "[folderSwitch] Atajo manual: el frente no es un dialogo de archivos.";
        return;
    }
    scheduleSwitch(dialog);
}

void FolderSwitchMacModule::handleRecentHotkey()
{
    const MacFileDialogs::Dialog dialog = MacFileDialogs::focusedDialog(MacFileDialogs::frontPid());
    if (!dialog.isValid()) {
        qDebug() << "[folderSwitch] Recientes: el frente no es un dialogo de archivos.";
        return;
    }
    if (m_recentPopup) {
        return;
    }
    RecentFoldersPopup popup(m_state.recentFolders(), context().window());
    m_recentPopup = &popup;
    QPointer<FolderSwitchMacModule> self(this);
    const QString path = popup.exec(QCursor::pos());
    if (!self) {
        return;
    }
    m_recentPopup = nullptr;
    if (path.isEmpty() || !MacFileDialogs::isOpen(dialog)) {
        return;
    }
    // El popup activo esta app: el foco vuelve a la del dialogo antes de escribirle la ruta.
    MacFileDialogs::activate(dialog);
    QTimer::singleShot(FolderSwitchLogic::kSwitchDelayMs, this, [self, dialog, path]() {
        if (self && MacFileDialogs::isOpen(dialog)) {
            self->applyFolder(dialog, path, kSourceRecent);
        }
    });
}

void FolderSwitchMacModule::scheduleSwitch(const MacFileDialogs::Dialog &dialog)
{
    QPointer<FolderSwitchMacModule> self(this);
    QTimer::singleShot(FolderSwitchLogic::kSwitchDelayMs, this, [self, dialog]() {
        if (!self || !MacFileDialogs::isOpen(dialog)) {
            qDebug() << "[folderSwitch] El dialogo ya no existe, se cancela el switch.";
            return;
        }
        bool denied = false;
        const QString path = MacFileDialogs::finderFolder(&denied);
        if (denied) {
            if (self->m_deniedNoticeShown) {
                return;
            }
            self->m_deniedNoticeShown = true;
            self->context().notify(QStringLiteral("Folder Switch"),
                                   I18n::tr("Allow LGA Mighty Tools to control Finder in System Settings > Privacy & Security > Automation."),
                                   ModuleContext::NoticeIcon::Warning, 8000);
            return;
        }
        if (path.isEmpty()) {
            qDebug() << "[folderSwitch] El Finder no tiene una carpeta abierta al frente.";
            return;
        }
        self->applyFolder(dialog, path, kSourceFinder);
    });
}

void FolderSwitchMacModule::applyFolder(const MacFileDialogs::Dialog &dialog, const QString &path, const QString &source)
{
    if (context().automatedRun()) {
        qInfo() << "[folderSwitch] (dry-run) se aplicaria" << path << "origen" << source;
        return;
    }
    QPointer<FolderSwitchMacModule> self(this);
    MacFileDialogs::switchTo(dialog, path, [self, path, source](bool ok) {
        if (!self) {
            return;
        }
        FolderSwitchState::LastSwitch last;
        last.path = path;
        last.source = source;
        last.applied = ok;
        last.when = QDateTime::currentDateTime();
        self->m_state.setLastSwitch(last);
        self->m_state.addRecentFolder(path);
        self->refreshPanel();
        qInfo() << "[folderSwitch] cambio de carpeta" << (ok ? "OK" : "FALLO") << "origen" << source;
    });
}

void FolderSwitchMacModule::recordFinderFolder()
{
    // Diferido: fuera del aviso de cambio de app.
    QPointer<FolderSwitchMacModule> self(this);
    QTimer::singleShot(0, this, [self]() {
        if (!self) {
            return;
        }
        const QString path = MacFileDialogs::finderFolder(nullptr, /*mayAsk=*/false);
        if (!path.isEmpty()) {
            self->m_state.addRecentFolder(path);
        }
    });
}

// ---------------------------------------------------------------------------------------------
// Capturas de QA (--ui-shot): todo fixture, nunca se lee el sistema.
// ---------------------------------------------------------------------------------------------

namespace {
const QString kFixturePath = QStringLiteral("/Volumes/Proyectos/2026_SerieDocumental_Temporada03_Episodio230/Comp/Renders/v012/");

QStringList fixtureRecentFolders()
{
    return {
        QStringLiteral("/Volumes/Proyectos/2026_SerieDocumental/Comp/Renders/v012/"),
        QStringLiteral("/Volumes/Proyectos/2026_SerieDocumental/Comp/Renders/v011/"),
        QStringLiteral("/Users/lega/Downloads/"),
        QStringLiteral("/Users/lega/Documents/Referencias/"),
        QStringLiteral("/Volumes/Proyectos/Cliente_XYZ/Assets/"),
    };
}
} // namespace

QStringList FolderSwitchMacModule::captureStates() const
{
    return {
        QStringLiteral("on"),         QStringLiteral("empty"),          QStringLiteral("paused"),
        QStringLiteral("failed"),     QStringLiteral("hotkey-busy"),    QStringLiteral("needs-accessibility"),
        QStringLiteral("recording"),  QStringLiteral("rejected"),       QStringLiteral("recent-popup"),
        QStringLiteral("recent-popup-empty"),
    };
}

bool FolderSwitchMacModule::applyCaptureState(const QString &state)
{
    m_pendingRowFixture.clear();
    m_accessibilityGranted = true;
    m_manualRegistered = true;
    m_recentRegistered = true;
    m_state.setEnabled(true);
    m_state.setAutoSwitch(true);
    auto setLast = [this](bool applied) {
        FolderSwitchState::LastSwitch last;
        last.path = kFixturePath;
        last.source = kSourceFinder;
        last.applied = applied;
        last.when = QDateTime(QDate(2026, 10, 2), QTime(12, 41));
        m_state.setLastSwitch(last);
    };
    if (state == QLatin1String("on")) {
        setLast(true);
    } else if (state == QLatin1String("empty")) {
        m_state.setLastSwitch(FolderSwitchState::LastSwitch());
    } else if (state == QLatin1String("paused")) {
        m_state.setEnabled(false);
        setLast(true);
    } else if (state == QLatin1String("failed")) {
        setLast(false);
    } else if (state == QLatin1String("hotkey-busy")) {
        m_manualRegistered = false;
        setLast(true);
    } else if (state == QLatin1String("needs-accessibility")) {
        m_accessibilityGranted = false;
        m_state.setLastSwitch(FolderSwitchState::LastSwitch());
    } else if (state == QLatin1String("recording") || state == QLatin1String("rejected")) {
        setLast(true);
        m_pendingRowFixture = state;
    } else if (state != QLatin1String("recent-popup") && state != QLatin1String("recent-popup-empty")) {
        return false;
    }
    refreshPanel();
    return true;
}

QWidget *FolderSwitchMacModule::createCaptureWidget(const QString &state, QWidget *parent)
{
    if (state == QLatin1String("recent-popup")) {
        return new RecentFoldersPopup(fixtureRecentFolders(), parent);
    }
    if (state == QLatin1String("recent-popup-empty")) {
        return new RecentFoldersPopup(QStringList{}, parent);
    }
    return nullptr;
}

// ---------------------------------------------------------------------------------------------
// Descriptor. El icono, las vinetas y los atajos configurados son copia de los de Windows
// (FolderSwitchModule.cpp, que no se toca desde la Mac porque ahi no se puede compilar Windows; roadmap:
// unificarlos en un archivo comun la proxima vez que se compile en Windows).
// ---------------------------------------------------------------------------------------------

namespace {

void paintFolderSwitchIcon(QPainter &painter, const QRectF &rect, const QColor &color)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.translate(rect.topLeft());
    const qreal scale = rect.width() / 16.0;
    painter.scale(scale, scale);

    QPen folderPen(color);
    folderPen.setWidthF(1.4);
    folderPen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(folderPen);
    painter.setBrush(Qt::NoBrush);
    QPainterPath folder;
    folder.moveTo(1.8, 4.2);
    folder.cubicTo(1.8, 3.6, 2.3, 3.1, 2.9, 3.1);
    folder.lineTo(5.9, 3.1);
    folder.lineTo(7.4, 4.7);
    folder.lineTo(13.1, 4.7);
    folder.cubicTo(13.7, 4.7, 14.2, 5.2, 14.2, 5.8);
    folder.lineTo(14.2, 12.1);
    folder.cubicTo(14.2, 12.7, 13.7, 13.2, 13.1, 13.2);
    folder.lineTo(2.9, 13.2);
    folder.cubicTo(2.3, 13.2, 1.8, 12.7, 1.8, 12.1);
    folder.closeSubpath();
    painter.drawPath(folder);

    QPen arrowPen(color);
    arrowPen.setWidthF(1.3);
    arrowPen.setCapStyle(Qt::RoundCap);
    arrowPen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(arrowPen);
    QPainterPath arrow;
    arrow.moveTo(5.5, 9.0);
    arrow.lineTo(10.5, 9.0);
    arrow.moveTo(8.8, 7.3);
    arrow.lineTo(10.5, 9.0);
    arrow.lineTo(8.8, 10.7);
    painter.drawPath(arrow);
    painter.restore();
}

Shortcut configuredShortcut(const SettingsReader &value, const QString &key, const Shortcut &fallback)
{
    const Shortcut configured = Shortcut::fromPortableString(value(key, fallback.toPortableString()).toString());
    return configured.isValid() ? configured : fallback;
}

QStringList folderSwitchOffBullets(const SettingsReader &value)
{
    const Shortcut manual = configuredShortcut(value, QStringLiteral("shortcuts/manual"), FolderSwitchState::defaultManualShortcut());
    const Shortcut recent = configuredShortcut(value, QStringLiteral("shortcuts/recent"), FolderSwitchState::defaultRecentShortcut());
    return {
        I18n::tr("Watches which window is in front"),
        I18n::tr("Two shortcuts: %1 and %2").arg(manual.displayText(), recent.displayText()),
        I18n::tr("Remembers your last 5 folders"),
    };
}

QList<Shortcut> folderSwitchConfiguredShortcuts(const SettingsReader &value)
{
    QList<Shortcut> list;
    for (const auto &[key, fallback] : {std::pair{QStringLiteral("shortcuts/manual"), FolderSwitchState::defaultManualShortcut()},
                                        std::pair{QStringLiteral("shortcuts/recent"), FolderSwitchState::defaultRecentShortcut()}}) {
        const Shortcut shortcut = Shortcut::fromPortableString(value(key, fallback.toPortableString()).toString());
        if (shortcut.isValid()) {
            list << shortcut;
        }
    }
    return list;
}

// Las reglas que decide la logica comun, con los valores de mac (Finder en lugar de Explorer). Lo que
// toca dialogos reales se prueba a mano: en una corrida automatizada nunca se lee ni se escribe un
// dialogo ajeno.
void folderSwitchSelfTest(const std::function<void(bool, const QString &)> &check)
{
    using FolderSwitchLogic::shouldAutoSwitch;
    check(shouldAutoSwitch(true, true, true, true, false), QStringLiteral("mac: vuelve del Finder al mismo dialogo -> cambia"));
    check(!shouldAutoSwitch(true, true, true, false, false), QStringLiteral("mac: otro dialogo -> no cambia"));
    check(!shouldAutoSwitch(true, true, false, true, false), QStringLiteral("mac: Finder visto hace mas de 60 s -> no cambia"));
    check(!shouldAutoSwitch(true, false, true, true, false), QStringLiteral("mac: en pausa -> no cambia"));
    check(!shouldAutoSwitch(false, true, true, true, false), QStringLiteral("mac: sin cambio automatico -> no cambia"));
    check(!shouldAutoSwitch(true, true, true, true, true), QStringLiteral("mac: ya se le cambio -> no repite"));
    check(FolderSwitchState::sameFolder(QStringLiteral("/Users/x/Desktop/"), QStringLiteral("/Users/x/Desktop")),
          QStringLiteral("mac: la barra final no cuenta como otra carpeta"));
    FolderSwitchState state;
    for (int i = 0; i < 7; ++i) {
        state.addRecentFolder(QStringLiteral("/Volumes/P/%1/").arg(i));
    }
    state.addRecentFolder(QStringLiteral("/Volumes/P/5"));
    check(state.recentFolders().size() == FolderSwitchState::kMaxRecentFolders
              && state.recentFolders().first() == QLatin1String("/Volumes/P/5"),
          QStringLiteral("mac: recientes sin repetidas, la ultima primero, hasta 5"));
}

} // namespace

ModuleDescriptor folderSwitchDescriptor()
{
    ModuleDescriptor d;
    d.id = QStringLiteral("folderSwitch");
    d.title = QStringLiteral("Folder Switch");
    d.description = I18n::tr(
        "Quick access to the folder already open in Finder from file open or save dialogs. Simply switch to Finder and back, and the dialog moves to that folder. Inside the dialog, one shortcut jumps there right away and another picks a recent folder. Also works in Nuke.");
    d.offBullets = folderSwitchOffBullets([](const QString &, const QVariant &fallback) { return fallback; });
    d.offBulletsFor = &folderSwitchOffBullets;
    d.platforms = PlatformWindows | PlatformMac; // el chip dice «Win · mac»: en Windows lo atiende FolderSwitchModule
    d.paintIcon = paintFolderSwitchIcon;
    d.create = [](ModuleContext &context) -> std::unique_ptr<Module> {
        return std::make_unique<FolderSwitchMacModule>(context);
    };
    d.configuredShortcuts = folderSwitchConfiguredShortcuts;
    d.selfTest = folderSwitchSelfTest;
    return d;
}

HelpSection folderSwitchHelp(const SettingsReader &)
{
    HelpSection section;
    section.title = QStringLiteral("Folder Switch");
    section.steps = {
        I18n::tr("Open a folder in %1.").arg(HelpSection::strong(QStringLiteral("Finder"))),
        I18n::tr("Go to the %1 or %2 dialog of any app.")
            .arg(HelpSection::strong(I18n::tr("Open")), HelpSection::strong(I18n::tr("Save"))),
        I18n::tr("The dialog jumps to that folder."),
    };
    section.note = I18n::tr("Works with macOS file dialogs and Qt ones, like Nuke's. The first time, macOS asks to let LGA Mighty Tools control Finder.");
    return section;
}
