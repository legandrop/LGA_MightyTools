#include "modules/folderswitch/FolderSwitchModule.h"
#include "core/I18n.h"

#include "app/ModuleContext.h"
#include "modules/folderswitch/DialogSwitcher.h"
#include "modules/folderswitch/FolderResolver.h"
#include "modules/folderswitch/FolderSwitchLogic.h"
#include "modules/folderswitch/RecentFoldersPopup.h"
#include "modules/folderswitch/UiaTimeouts.h"
#include "modules/folderswitch/WindowUtils.h"
#include "platform/ForegroundWatcher.h"
#include "ui/ShortcutRow.h"

#include <QAction>
#include <QCoreApplication>
#include <QCursor>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QSet>
#include <QTimer>

#include <exdisp.h>
#include <objbase.h>
#include <objidl.h>
#include <uiautomation.h>

namespace {
constexpr int kManualHotkeyId = 1;
constexpr int kRecentHotkeyId = 2;
} // namespace

FolderSwitchModule::FolderSwitchModule(ModuleContext &context)
    : Module(context)
    , m_state(&context)
{
}

FolderSwitchModule::~FolderSwitchModule() = default;

void FolderSwitchModule::declareAndRegisterShortcuts()
{
    ModuleHotkeys *hk = context().hotkeys();
    const Shortcut manual = m_state.manualShortcut();
    const Shortcut recent = m_state.recentShortcut();
    m_manualTakenBy = hk->declare(kManualHotkeyId, manual);
    m_recentTakenBy = hk->declare(kRecentHotkeyId, recent);

    m_manualRegistered = m_manualTakenBy.isEmpty() && hk->registerHotkey(kManualHotkeyId, manual);
    m_recentRegistered = m_recentTakenBy.isEmpty() && hk->registerHotkey(kRecentHotkeyId, recent);

    qInfo() << "[folderSwitch] Atajo manual" << manual.toPortableString() << "->"
            << (m_manualRegistered
                    ? QStringLiteral("registrado")
                    : (m_manualTakenBy.isEmpty() ? QStringLiteral("rechazado por el sistema")
                                                 : QStringLiteral("tomado por %1").arg(m_manualTakenBy)));
    qInfo() << "[folderSwitch] Atajo de recientes" << recent.toPortableString() << "->"
            << (m_recentRegistered
                    ? QStringLiteral("registrado")
                    : (m_recentTakenBy.isEmpty() ? QStringLiteral("rechazado por el sistema")
                                                 : QStringLiteral("tomado por %1").arg(m_recentTakenBy)));
}

void FolderSwitchModule::start()
{
    declareAndRegisterShortcuts();

    // Observador de ventana al frente COMPARTIDO (plan 4.4): el host crea uno solo por proceso, con
    // el primer modulo prendido que lo pide, y lo destruye cuando ya ninguno lo usa. Este modulo no
    // instala ningun hook propio.
    ForegroundWatcher *watcher = context().foreground();
    m_foregroundAcquired = true;
    connect(watcher, &ForegroundWatcher::foregroundChanged, this, &FolderSwitchModule::onForegroundChanged);
    connect(context().hotkeys(), &ModuleHotkeys::activated, this, &FolderSwitchModule::onHotkeyActivated);
}

void FolderSwitchModule::stop()
{
    // Contrato de Module.h ("Vida"): stop() cierra los popups no modales que el modulo haya abierto.
    // hide() dispara RecentFoldersPopup::hideEvent(), que termina su QEventLoop y hace que
    // handleRecentHotkey() vuelva con "" elegido (ver el guard con QPointer ahi).
    if (m_recentPopup) {
        m_recentPopup->hide();
    }

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

    m_lastManagerHwnd = nullptr;
    m_lastManagerType = ManagerType::None;
    m_lastManagerSeenMs = 0;
    m_lastDialogHwnd = nullptr;
    m_pendingReturnDialog = nullptr;
    m_lastSwitchedDialogHwnd = nullptr;
    m_prevManagerHwnd = nullptr;
    m_prevManagerType = ManagerType::None;
}

ModuleStatus FolderSwitchModule::status() const
{
    ModuleStatus s;
    if (!m_manualRegistered || !m_recentRegistered) {
        s.tone = ModuleTone::Error;
        // En ingles en los dos idiomas (decision de Lega): la fila de la barra lateral tiene ~125 px y
        // "Ctrl+Alt+Shift+O off" es lo unico que entra. El detalle traducido esta en el panel.
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

bool FolderSwitchModule::isPaused() const
{
    return !m_state.enabled();
}

FolderSwitchPanel::ViewState FolderSwitchModule::currentViewState() const
{
    FolderSwitchPanel::ViewState v;
    v.enabled = m_state.enabled();
    v.autoSwitch = m_state.autoSwitch();
    v.manualShortcut = m_state.manualShortcut();
    v.recentShortcut = m_state.recentShortcut();
    v.manualRegistered = m_manualRegistered;
    v.recentRegistered = m_recentRegistered;
    v.lastSwitch = m_state.lastSwitch();
    return v;
}

void FolderSwitchModule::refreshPanel()
{
    if (m_panel) {
        m_panel->setState(currentViewState());
    }
}

QWidget *FolderSwitchModule::createPanel(QWidget *parent)
{
    auto *panel = new FolderSwitchPanel(parent);
    m_panel = panel;
    panel->setState(currentViewState());
    panel->manualRow()->setValidator([this](const Shortcut &candidate) { return validateShortcut(candidate); });
    panel->recentRow()->setValidator([this](const Shortcut &candidate) { return validateShortcut(candidate); });

    connect(panel, &FolderSwitchPanel::toggleRequested, this, &FolderSwitchModule::setEnabled);
    connect(panel, &FolderSwitchPanel::autoSwitchToggled, this, &FolderSwitchModule::setAutoSwitch);
    connect(panel, &FolderSwitchPanel::manualShortcutRecorded, this, &FolderSwitchModule::setManualShortcut);
    connect(panel, &FolderSwitchPanel::recentShortcutRecorded, this, &FolderSwitchModule::setRecentShortcut);

    // Fixture de captura que necesita el panel ya construido (ver comentario de m_pendingRowFixture).
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

void FolderSwitchModule::fillTrayMenu(QMenu *menu)
{
    QAction *toggle = menu->addAction(m_state.enabled() ? I18n::tr("Pause switching") : I18n::tr("Resume switching"));
    connect(toggle, &QAction::triggered, this, [this]() { setEnabled(!m_state.enabled()); });
}

void FolderSwitchModule::setEnabled(bool enabled)
{
    if (enabled == m_state.enabled()) {
        return;
    }
    m_state.setEnabled(enabled);
    refreshPanel();
    emit statusChanged();
}

void FolderSwitchModule::setAutoSwitch(bool autoSwitch)
{
    if (autoSwitch == m_state.autoSwitch()) {
        return;
    }
    m_state.setAutoSwitch(autoSwitch);
    refreshPanel();
}

QString FolderSwitchModule::validateShortcut(const Shortcut &candidate) const
{
    // El grabador pregunta declaredByOtherModule() ANTES de probe() (ModuleContext.h): asi un choque
    // con otra herramienta de Mighty Tools da "Already used by X." en vez de confundirse con el
    // generico "taken by another app" del sistema operativo.
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

bool FolderSwitchModule::setManualShortcut(const Shortcut &shortcut)
{
    ModuleHotkeys *hk = context().hotkeys();
    const Shortcut previous = m_state.manualShortcut();
    const QString takenBy = hk->declare(kManualHotkeyId, shortcut);
    const bool ok = takenBy.isEmpty() && hk->registerHotkey(kManualHotkeyId, shortcut);
    if (ok) {
        m_manualTakenBy.clear();
        m_manualRegistered = true;
        m_state.setManualShortcut(shortcut);
    } else {
        // Rechazada (por otra herramienta o por el sistema operativo): se vuelve a declarar y
        // registrar la anterior, que seguia andando. El motivo del rechazo ya lo mostro el validador
        // del ShortcutRow ("Already used by X."/"Y is taken by another app."); aca solo importa que
        // la combinacion vieja no se pierda ni quede la declaracion apuntando a una que no se activo.
        hk->declare(kManualHotkeyId, previous);
        m_manualTakenBy.clear();
        m_manualRegistered = hk->registerHotkey(kManualHotkeyId, previous);
    }
    refreshPanel();
    emit statusChanged();
    return ok;
}

bool FolderSwitchModule::setRecentShortcut(const Shortcut &shortcut)
{
    ModuleHotkeys *hk = context().hotkeys();
    const Shortcut previous = m_state.recentShortcut();
    const QString takenBy = hk->declare(kRecentHotkeyId, shortcut);
    const bool ok = takenBy.isEmpty() && hk->registerHotkey(kRecentHotkeyId, shortcut);
    if (ok) {
        m_recentTakenBy.clear();
        m_recentRegistered = true;
        m_state.setRecentShortcut(shortcut);
    } else {
        hk->declare(kRecentHotkeyId, previous);
        m_recentTakenBy.clear();
        m_recentRegistered = hk->registerHotkey(kRecentHotkeyId, previous);
    }
    refreshPanel();
    emit statusChanged();
    return ok;
}

void FolderSwitchModule::onForegroundChanged(quintptr hwndValue, quint32 /*pid*/, const QString & /*exeName*/)
{
    // En corrida automatizada el watcher compartido esta inerte, salvo cuando la corrida pide
    // observar de verdad (HostOptions::observeForeground: la medicion de consumo y el conteo de
    // hooks del self-test). Ahi los avisos SI llegan, y por eso esta guarda: nunca se toca un dialogo
    // ajeno en una corrida automatizada.
    if (context().automatedRun()) {
        return;
    }
    HWND hwnd = reinterpret_cast<HWND>(hwndValue);
    if (!hwnd) {
        return;
    }

    // Historial: la carpeta que queda en un manager cuando el usuario se va de el.
    if (m_prevManagerHwnd && m_prevManagerHwnd != hwnd) {
        recordManagerFolder(m_prevManagerHwnd, m_prevManagerType);
        m_prevManagerHwnd = nullptr;
        m_prevManagerType = ManagerType::None;
    }

    if (WindowUtils::isFileManagerWindow(hwnd)) {
        const bool isExplorer = WindowUtils::isExplorerWindow(hwnd);
        m_lastManagerHwnd = hwnd;
        m_lastManagerType = isExplorer ? ManagerType::Explorer : ManagerType::XYplorer;
        m_lastManagerSeenMs = QDateTime::currentMSecsSinceEpoch();
        m_lastSwitchedDialogHwnd = nullptr;
        m_prevManagerHwnd = hwnd;
        m_prevManagerType = m_lastManagerType;
        // Si el usuario venia de un dialogo que sigue vivo, al volver a ESE dialogo hay que inyectar.
        // Inmune a ventanas intermedias (Alt+Tab).
        if (m_lastDialogHwnd && IsWindow(m_lastDialogHwnd)) {
            m_pendingReturnDialog = m_lastDialogHwnd;
        }
    } else if (WindowUtils::isFileDialogWindow(hwnd) || WindowUtils::isQtFileDialog(hwnd)) {
        m_lastDialogHwnd = hwnd;

        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        const bool managerFresh = m_lastManagerHwnd
            && FolderSwitchLogic::isManagerFresh(m_lastManagerSeenMs, nowMs);
        const bool isPendingReturn = (hwnd == m_pendingReturnDialog);
        const bool alreadySwitched = (hwnd == m_lastSwitchedDialogHwnd);

        if (FolderSwitchLogic::shouldAutoSwitch(m_state.autoSwitch(), m_state.enabled(), managerFresh,
                                                isPendingReturn, alreadySwitched)) {
            m_pendingReturnDialog = nullptr;
            m_lastSwitchedDialogHwnd = hwnd;
            scheduleSwitch(hwnd);
        }
    }
}

void FolderSwitchModule::onHotkeyActivated(int localId)
{
    if (context().automatedRun()) {
        // ModuleHotkeys tampoco dispara esto de verdad en una corrida automatizada (no llama a
        // RegisterHotKey); queda como defensa en profundidad si algun dia cambia esa garantia.
        return;
    }
    if (localId == kManualHotkeyId) {
        handleManualHotkey();
    } else if (localId == kRecentHotkeyId) {
        handleRecentHotkey();
    }
}

void FolderSwitchModule::handleManualHotkey()
{
    // Igual que HotkeyFilter::hotkeyPressed en el origen: funciona tambien en pausa (no mira
    // m_state.enabled() ni autoSwitch), solo exige un file dialog al frente y un manager guardado.
    HWND fg = GetForegroundWindow();
    if (!fg || !(WindowUtils::isFileDialogWindow(fg) || WindowUtils::isQtFileDialog(fg))) {
        qDebug() << "[folderSwitch] Atajo manual: el foreground no es un file dialog.";
        return;
    }
    if (!m_lastManagerHwnd || !IsWindow(m_lastManagerHwnd)) {
        qDebug() << "[folderSwitch] Atajo manual: no hay manager guardado valido.";
        return;
    }
    performSwitch(fg);
}

void FolderSwitchModule::handleRecentHotkey()
{
    // Como el atajo manual: funciona tambien en pausa, solo exige un file dialog al frente.
    HWND dialog = GetForegroundWindow();
    if (!dialog || !(WindowUtils::isFileDialogWindow(dialog) || WindowUtils::isQtFileDialog(dialog))) {
        qDebug() << "[folderSwitch] Recientes: el foreground no es un file dialog.";
        return;
    }
    if (m_recentPopup) {
        return; // ya hay uno abierto
    }

    // Vive en la pila, con window() de padre (contrato de Module.h). RecentFoldersPopup::exec()
    // corre un QEventLoop propio: si el usuario apaga el modulo (o lo borra) mientras esta abierto,
    // stop() lo cierra llamando hide() -- y el QPointer de aca evita tocar `this` si ademas el
    // modulo ya se borro cuando exec() vuelve.
    RecentFoldersPopup popup(m_state.recentFolders(), context().window());
    m_recentPopup = &popup;
    QPointer<FolderSwitchModule> self(this);
    const QString path = popup.exec(QCursor::pos());
    if (!self) {
        return;
    }
    m_recentPopup = nullptr;

    if (path.isEmpty() || !IsWindow(dialog)) {
        return;
    }
    // Devolverle el foco al dialogo antes de escribirle la ruta, con la misma demora que el cambio
    // automatico.
    SetForegroundWindow(dialog);
    QPointer<FolderSwitchModule> weakSelf(this);
    QTimer::singleShot(FolderSwitchLogic::kSwitchDelayMs, this, [weakSelf, dialog, path]() {
        if (weakSelf && IsWindow(dialog)) {
            weakSelf->applyFolder(dialog, path, QStringLiteral("Recent"));
        }
    });
}

QString FolderSwitchModule::resolveLastManagerPath() const
{
    if (!m_lastManagerHwnd || !IsWindow(m_lastManagerHwnd)) {
        return QString();
    }
    if (m_lastManagerType == ManagerType::Explorer) {
        return FolderResolver::resolveExplorerPath(m_lastManagerHwnd);
    }
    if (m_lastManagerType == ManagerType::XYplorer) {
        return FolderResolver::resolveXYplorerPath(m_lastManagerHwnd);
    }
    return QString();
}

void FolderSwitchModule::scheduleSwitch(HWND dialogHwnd)
{
    QTimer::singleShot(FolderSwitchLogic::kSwitchDelayMs, this, [this, dialogHwnd]() { performSwitch(dialogHwnd); });
}

void FolderSwitchModule::performSwitch(HWND dialogHwnd)
{
    if (!dialogHwnd || !IsWindow(dialogHwnd)) {
        qDebug() << "[folderSwitch] El dialogo ya no existe, se cancela el switch.";
        return;
    }
    const QString path = resolveLastManagerPath();
    if (path.isEmpty()) {
        qDebug() << "[folderSwitch] No se pudo resolver el path del manager guardado.";
        return;
    }
    applyFolder(dialogHwnd, path,
                m_lastManagerType == ManagerType::XYplorer ? QStringLiteral("XYplorer") : QStringLiteral("Explorer"));
}

void FolderSwitchModule::applyFolder(HWND dialogHwnd, const QString &path, const QString &source)
{
    if (context().automatedRun()) {
        qInfo() << "[folderSwitch] (dry-run) se aplicaria" << path << "origen" << source;
        return;
    }
    const bool ok = DialogSwitcher::switchDialog(dialogHwnd, path);
    FolderSwitchState::LastSwitch last;
    last.path = path;
    last.source = source;
    last.applied = ok;
    last.when = QDateTime::currentDateTime();
    m_state.setLastSwitch(last);
    m_state.addRecentFolder(path);
    refreshPanel();
    qDebug() << "[folderSwitch] switchDialog" << (ok ? "OK" : "FALLO") << "path=" << path << "source=" << source;
}

void FolderSwitchModule::recordManagerFolder(HWND managerHwnd, ManagerType type)
{
    // Diferido: el foreground llega desde el hook, y resolver la carpeta de Explorer llama a COM. Se
    // hace en el loop normal, ya fuera del callback (igual que el origen).
    QTimer::singleShot(0, this, [this, managerHwnd, type]() {
        if (!IsWindow(managerHwnd)) {
            return;
        }
        const QString path = type == ManagerType::XYplorer ? FolderResolver::resolveXYplorerPath(managerHwnd)
                                                            : FolderResolver::resolveExplorerPath(managerHwnd);
        if (!path.isEmpty()) {
            m_state.addRecentFolder(path);
        }
    });
}

// ---------------------------------------------------------------------------------------------
// Capturas de QA (--ui-shot). Ver Module.h, "Captura": nunca se lee el sistema, todo es fixture.
// ---------------------------------------------------------------------------------------------

namespace {
// Ruta larga de prueba: fuerza la elision al medio del campo "Last folder" (canvas, seccion 3), como
// en el ejemplo del propio canvas ("N:\Proyectos\2026_Serie…230\Comp\Renders\v012\").
const QString kFixturePath =
    QStringLiteral("N:\\Proyectos\\2026_SerieDocumental_Temporada03_Episodio230\\Comp\\Renders\\v012\\");

QStringList fixtureRecentFolders()
{
    return {
        QStringLiteral("N:\\Proyectos\\2026_SerieDocumental\\Comp\\Renders\\v012\\"),
        QStringLiteral("N:\\Proyectos\\2026_SerieDocumental\\Comp\\Renders\\v011\\"),
        QStringLiteral("D:\\Descargas"),
        QStringLiteral("C:\\Users\\lega\\Documents\\Referencias"),
        QStringLiteral("N:\\Proyectos\\Cliente_XYZ\\Assets"),
    };
}
} // namespace

QStringList FolderSwitchModule::captureStates() const
{
    return {
        QStringLiteral("on"),      QStringLiteral("empty"),  QStringLiteral("paused"),
        QStringLiteral("paused-no-shortcuts"), QStringLiteral("failed"), QStringLiteral("hotkey-busy"),
        QStringLiteral("failed-hotkey-busy"), QStringLiteral("recording"), QStringLiteral("rejected"),
        QStringLiteral("recent-popup"), QStringLiteral("recent-popup-one"), QStringLiteral("recent-popup-empty"),
    };
}

bool FolderSwitchModule::applyCaptureState(const QString &state)
{
    m_pendingRowFixture.clear();
    auto setLast = [this](bool applied) {
        FolderSwitchState::LastSwitch last;
        last.path = kFixturePath;
        last.source = QStringLiteral("Explorer");
        last.applied = applied;
        last.when = QDateTime(QDate(2026, 9, 25), QTime(12, 41));
        m_state.setLastSwitch(last);
    };

    if (state == QLatin1String("on")) {
        m_state.setEnabled(true);
        m_state.setAutoSwitch(true);
        m_manualRegistered = true;
        m_recentRegistered = true;
        setLast(true);
    } else if (state == QLatin1String("empty")) {
        m_state.setEnabled(true);
        m_state.setAutoSwitch(true);
        m_manualRegistered = true;
        m_recentRegistered = true;
        m_state.setLastSwitch(FolderSwitchState::LastSwitch());
    } else if (state == QLatin1String("paused")) {
        m_state.setEnabled(false);
        m_manualRegistered = true;
        m_recentRegistered = true;
        setLast(true);
    } else if (state == QLatin1String("paused-no-shortcuts")) {
        m_state.setEnabled(false);
        m_manualRegistered = false;
        m_recentRegistered = false;
        m_state.setLastSwitch(FolderSwitchState::LastSwitch());
    } else if (state == QLatin1String("failed")) {
        m_state.setEnabled(true);
        m_manualRegistered = true;
        m_recentRegistered = true;
        setLast(false);
    } else if (state == QLatin1String("hotkey-busy")) {
        // El canvas ('busy') tomaba el atajo manual: el de recientes sigue libre.
        m_state.setEnabled(true);
        m_manualRegistered = false;
        m_recentRegistered = true;
        setLast(true);
    } else if (state == QLatin1String("failed-hotkey-busy")) {
        m_state.setEnabled(true);
        m_manualRegistered = false;
        m_recentRegistered = true;
        setLast(false);
    } else if (state == QLatin1String("recording") || state == QLatin1String("rejected")) {
        m_state.setEnabled(true);
        m_manualRegistered = true;
        m_recentRegistered = true;
        setLast(true);
        m_pendingRowFixture = state;
    } else if (state == QLatin1String("recent-popup") || state == QLatin1String("recent-popup-one")
               || state == QLatin1String("recent-popup-empty")) {
        // Sin panel que fijar: createCaptureWidget() arma el popup con datos de prueba.
    } else {
        return false;
    }
    refreshPanel();
    return true;
}

QWidget *FolderSwitchModule::createCaptureWidget(const QString &state, QWidget *parent)
{
    if (state == QLatin1String("recent-popup")) {
        return new RecentFoldersPopup(fixtureRecentFolders(), parent);
    }
    if (state == QLatin1String("recent-popup-one")) {
        return new RecentFoldersPopup(QStringList{fixtureRecentFolders().first()}, parent);
    }
    if (state == QLatin1String("recent-popup-empty")) {
        return new RecentFoldersPopup(QStringList{}, parent);
    }
    return nullptr;
}

// ---------------------------------------------------------------------------------------------
// Descriptor: icono, textos exactos del canvas, fabrica, atajos configurados.
// ---------------------------------------------------------------------------------------------

namespace {

// Carpeta con flecha, vectorial y monocromo (regla de UI): mismo glifo que I.fs en el canvas de
// diseno, a escala de `rect` (el trazo original esta pensado para un lienzo de 16x16).
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

QStringList folderSwitchOffBullets(const SettingsReader &value)
{
    const auto shortcutOf = [&value](const QString &key, const Shortcut &fallback) {
        const Shortcut configured = Shortcut::fromPortableString(value(key, fallback.toPortableString()).toString());
        return configured.isValid() ? configured : fallback;
    };
    const Shortcut manual = shortcutOf(QStringLiteral("shortcuts/manual"), FolderSwitchState::defaultManualShortcut());
    const Shortcut recent = shortcutOf(QStringLiteral("shortcuts/recent"), FolderSwitchState::defaultRecentShortcut());
    return {
        I18n::tr("Watches which window is in front"),
        I18n::tr("Two shortcuts: %1 and %2").arg(manual.displayText(), recent.displayText()),
        I18n::tr("Remembers your last 5 folders"),
    };
}

QList<Shortcut> folderSwitchConfiguredShortcuts(const SettingsReader &value)
{
    QList<Shortcut> list;
    const Shortcut manual = Shortcut::fromPortableString(
        value(QStringLiteral("shortcuts/manual"), FolderSwitchState::defaultManualShortcut().toPortableString())
            .toString());
    if (manual.isValid()) {
        list << manual;
    }
    const Shortcut recent = Shortcut::fromPortableString(
        value(QStringLiteral("shortcuts/recent"), FolderSwitchState::defaultRecentShortcut().toPortableString())
            .toString());
    if (recent.isValid()) {
        list << recent;
    }
    return list;
}

// -----------------------------------------------------------------------------------------------
// Test doubles de ModuleContext/ModuleHotkeys, solo para --self-test: todavia no existe una
// implementacion real de ModuleContext ajena a esta corrida (el host es real, pero levantarlo entero
// para un self-test de un solo modulo es mas de lo que hace falta). Viven aca, no se compilan ni se
// linkean fuera de este modulo.
// -----------------------------------------------------------------------------------------------

class FakeModuleHotkeys : public ModuleHotkeys
{
public:
    bool registerHotkey(int localId, const Shortcut &shortcut) override
    {
        if (!shortcut.isValid() || m_rejectRegister.contains(shortcut.toPortableString())) {
            return false;
        }
        m_registered.insert(localId, shortcut);
        return true;
    }
    void unregisterHotkey(int localId) override { m_registered.remove(localId); }
    void unregisterAll() override { m_registered.clear(); }
    bool isRegistered(int localId) const override { return m_registered.contains(localId); }

    bool probe(const Shortcut &shortcut) override { return !m_rejectRegister.contains(shortcut.toPortableString()); }
    void passThrough(const Shortcut &) override {}

    QString declare(int localId, const Shortcut &shortcut) override
    {
        if (!shortcut.isValid()) {
            m_declared.remove(localId);
            return QString();
        }
        const QString foreignTitle = m_foreignDeclarations.value(shortcut.toPortableString());
        if (!foreignTitle.isEmpty()) {
            return foreignTitle;
        }
        m_declared.insert(localId, shortcut);
        return QString();
    }
    QString declaredByOtherModule(const Shortcut &shortcut) const override
    {
        return m_foreignDeclarations.value(shortcut.toPortableString());
    }

    // Helpers de prueba: simulan lo que en la app real decidirian otra herramienta o el sistema
    // operativo.
    void setForeignDeclaration(const Shortcut &shortcut, const QString &title)
    {
        m_foreignDeclarations.insert(shortcut.toPortableString(), title);
    }
    void setRejectRegister(const Shortcut &shortcut) { m_rejectRegister.insert(shortcut.toPortableString()); }

private:
    QHash<int, Shortcut> m_registered;
    QHash<int, Shortcut> m_declared;
    QHash<QString, QString> m_foreignDeclarations;
    QSet<QString> m_rejectRegister;
};

class FakeModuleContext : public ModuleContext
{
public:
    FakeModuleContext() : m_foreground(false) {}

    QString moduleId() const override { return QStringLiteral("folderSwitch"); }
    QString moduleTitle() const override { return QStringLiteral("Folder Switch"); }

    QVariant value(const QString &key, const QVariant &defaultValue) const override
    {
        return m_values.value(key, defaultValue);
    }
    void setValue(const QString &key, const QVariant &value) override { m_values.insert(key, value); }
    void removeValue(const QString &key) override { m_values.remove(key); }

    bool captureMode() const override { return false; }
    bool dryRunInput() const override { return true; }
    bool automatedRun() const override { return true; }
    bool persistentRegistrationAllowed() const override { return false; }

    ModuleHotkeys *hotkeys() override { return &m_hotkeys; }
    InputInjector *injector() override { return nullptr; } // el modulo no lo usa (ver comentario del .h)
    ForegroundWatcher *foreground() override { return &m_foreground; }

    void notify(const QString &, const QString &, NoticeIcon, int) override {}
    void showPanel() override {}
    void hideWindowTemporarily() override {}
    void restoreWindow() override {}
    QWidget *window() const override { return nullptr; }

    FakeModuleHotkeys &hotkeysForTest() { return m_hotkeys; }

private:
    QHash<QString, QVariant> m_values;
    FakeModuleHotkeys m_hotkeys;
    ForegroundWatcher m_foreground; // active=false: sin hook real (ver constructor)
};

// Ventanas de prueba invisibles (nunca se muestran: sin WS_VISIBLE, sin ShowWindow) para ejercitar la
// clasificacion por clase de WindowUtils con casos reales y sus negativos, sin depender de que haya
// un Explorer, un XYplorer o un dialogo real abiertos en la maquina.
void windowClassificationSelfTest(const std::function<void(bool, const QString &)> &check)
{
    const HINSTANCE inst = GetModuleHandleW(nullptr);
    auto registerClass = [inst](const wchar_t *name) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = inst;
        wc.lpszClassName = name;
        // Puede fallar si la clase ya existe (p.ej. "#32770", predefinida por USER32 en todo
        // proceso): se ignora, CreateWindowExW usa igual la clase ya registrada.
        RegisterClassExW(&wc);
    };
    auto makeWindow = [inst](const wchar_t *className, HWND ownerOrParent, bool asChild) -> HWND {
        const DWORD style = asChild ? static_cast<DWORD>(WS_CHILD) : static_cast<DWORD>(WS_OVERLAPPED);
        return CreateWindowExW(0, className, L"", style, 0, 0, 10, 10, ownerOrParent, nullptr, inst, nullptr);
    };

    registerClass(L"CabinetWClass");
    registerClass(L"FolderSwitchSelfTestOther");
    HWND explorerLike = makeWindow(L"CabinetWClass", nullptr, false);
    HWND otherLike = makeWindow(L"FolderSwitchSelfTestOther", nullptr, false);
    if (!explorerLike || !otherLike) {
        check(false, QStringLiteral("no se pudieron crear ventanas de prueba (sin estacion de "
                                    "ventana): se omite la clasificacion por clase"));
    } else {
        check(WindowUtils::isExplorerWindow(explorerLike), QStringLiteral("CabinetWClass se reconoce como Explorer"));
        check(!WindowUtils::isExplorerWindow(otherLike),
              QStringLiteral("una clase ajena no es Explorer (negativo)"));
        check(WindowUtils::isFileManagerWindow(explorerLike),
              QStringLiteral("Explorer cuenta como file manager"));
        check(!WindowUtils::isFileManagerWindow(otherLike),
              QStringLiteral("clase ajena no es file manager (negativo)"));

        const QString ownExe = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
        check(!ownExe.isEmpty()
                  && WindowUtils::processExeName(explorerLike).compare(ownExe, Qt::CaseInsensitive) == 0,
              QStringLiteral("processExeName identifica el exe dueño de la ventana"));
    }
    if (explorerLike) DestroyWindow(explorerLike);
    if (otherLike) DestroyWindow(otherLike);

    registerClass(L"ThunderRT6FormDC");
    HWND xyLike = makeWindow(L"ThunderRT6FormDC", nullptr, false);
    if (xyLike) {
        check(WindowUtils::isXYplorerWindow(xyLike), QStringLiteral("ThunderRT6FormDC se reconoce como XYplorer"));
        DestroyWindow(xyLike);
    }

    registerClass(L"SHELLDLL_DefView");
    HWND dialogWithChild = makeWindow(L"#32770", nullptr, false);
    if (dialogWithChild) {
        HWND child = makeWindow(L"SHELLDLL_DefView", dialogWithChild, true);
        check(child != nullptr && WindowUtils::isFileDialogWindow(dialogWithChild),
              QStringLiteral("#32770 con hijo SHELLDLL_DefView es file dialog"));
        DestroyWindow(dialogWithChild); // destruye tambien al hijo
    }
    HWND dialogWithoutChild = makeWindow(L"#32770", nullptr, false);
    if (dialogWithoutChild) {
        check(!WindowUtils::isFileDialogWindow(dialogWithoutChild),
              QStringLiteral("#32770 sin los hijos esperados no es file dialog (negativo)"));
        DestroyWindow(dialogWithoutChild);
    }

    registerClass(L"QtSelfTestDialog");
    registerClass(L"NotQtDialog");
    HWND ownerWindow = makeWindow(L"FolderSwitchSelfTestOther", nullptr, false);
    HWND qtNoOwner = makeWindow(L"QtSelfTestDialog", nullptr, false);
    HWND nonQtWithOwner = ownerWindow ? makeWindow(L"NotQtDialog", ownerWindow, false) : nullptr;
    if (qtNoOwner) {
        check(!WindowUtils::isQtFileDialog(qtNoOwner),
              QStringLiteral("clase 'Qt...' sin owner no es dialogo Qt (negativo)"));
        DestroyWindow(qtNoOwner);
    }
    if (nonQtWithOwner) {
        check(!WindowUtils::isQtFileDialog(nonQtWithOwner),
              QStringLiteral("owner sin clase 'Qt...' no es dialogo Qt (negativo)"));
        DestroyWindow(nonQtWithOwner);
    }
    if (ownerWindow) DestroyWindow(ownerWindow);

    // Nulos: los cuatro clasificadores tienen que resolver false sin tocar nada (los llama el modulo
    // apenas arranca, antes de que haya ningun manager guardado).
    check(!WindowUtils::isExplorerWindow(nullptr) && !WindowUtils::isXYplorerWindow(nullptr)
              && !WindowUtils::isFileManagerWindow(nullptr) && !WindowUtils::isFileDialogWindow(nullptr)
              && !WindowUtils::isQtFileDialog(nullptr),
          QStringLiteral("los clasificadores devuelven false con hwnd nulo"));
}

// Logica pura del cache de isQtFileDialog (auditoria etapa 2, punto 3): vence, y distingue una clave
// de otra. Claves sinteticas (no HWNDs reales) y reloj inyectado: no toca ninguna ventana.
void qtDialogCacheSelfTest(const std::function<void(bool, const QString &)> &check)
{
    using FolderSwitchLogic::QtDialogCache;

    QtDialogCache cache(1000); // TTL de prueba: 1000 ms
    bool value = false;

    check(!cache.lookup(0x1000, 0, &value), QStringLiteral("QtDialogCache: sin nada guardado, no hay hit (negativo)"));

    cache.store(0x1000, true, 0);
    check(cache.lookup(0x1000, 0, &value) && value, QStringLiteral("QtDialogCache: hit inmediato con el valor guardado"));
    check(cache.lookup(0x1000, 999, &value) && value, QStringLiteral("QtDialogCache: sigue vigente justo antes del TTL"));
    check(!cache.lookup(0x1000, 1000, &value),
          QStringLiteral("QtDialogCache: vencio justo al llegar al TTL (negativo)"));

    // Distingue HWND: una clave no pisa ni contamina a otra.
    cache.store(0x2000, false, 0);
    check(cache.lookup(0x1000, 500, &value) && value, QStringLiteral("QtDialogCache: la clave 0x1000 conserva su valor"));
    check(cache.lookup(0x2000, 500, &value) && !value,
          QStringLiteral("QtDialogCache: la clave 0x2000 tiene el suyo propio, distinto (negativo de mezcla)"));

    cache.invalidate(0x1000);
    check(!cache.lookup(0x1000, 500, &value), QStringLiteral("QtDialogCache: invalidate() saca esa entrada (negativo)"));
    check(cache.lookup(0x2000, 500, &value), QStringLiteral("QtDialogCache: invalidar una clave no afecta a otra"));

    cache.clear();
    check(!cache.lookup(0x2000, 500, &value), QStringLiteral("QtDialogCache: clear() vacia todo (negativo)"));
    check(cache.size() == 0, QStringLiteral("QtDialogCache: size() en 0 despues de clear()"));

    QtDialogCache capCache(1000);
    for (quintptr i = 0; i < 130; ++i) {
        capCache.store(i, true, 0);
    }
    check(capCache.size() <= 128, QStringLiteral("QtDialogCache: no crece sin limite (se poda pasado un tope)"));
}

// Que UiaTimeouts::createAutomation()+apply() de verdad hayan fijado los timeouts (auditoria etapa 2,
// puntos 3 y de la correccion posterior: CLSID_CUIAutomation8 es el que expone IUIAutomation2, el
// viejo CLSID_CUIAutomation nunca lo hace). Crea una instancia real (COM ya esta inicializado por
// ComApartment en main) sin tocar ninguna ventana, y lee los valores de vuelta por IUIAutomation2.
void uiaTimeoutSelfTest(const std::function<void(bool, const QString &)> &check)
{
    IUIAutomation *automation = nullptr;
    const HRESULT hr = UiaTimeouts::createAutomation(&automation);
    if (FAILED(hr) || !automation) {
        check(false, QStringLiteral("UiaTimeouts: no se pudo crear IUIAutomation para probar (hr=%1)").arg(hr));
        return;
    }
    UiaTimeouts::apply(automation);

    IUIAutomation2 *automation2 = nullptr;
    const bool hasAutomation2 = SUCCEEDED(automation->QueryInterface(IID_IUIAutomation2,
                                                                     reinterpret_cast<void **>(&automation2)))
        && automation2;
    // En Windows 8+ (la maquina de Lega es Windows 11) createAutomation() consigue CLSID_CUIAutomation8,
    // que SIEMPRE expone IUIAutomation2: si esto no se cumple aca es una regresion real, no un Windows 7.
    check(hasAutomation2, QStringLiteral("UiaTimeouts: la instancia expone IUIAutomation2 (CLSID_CUIAutomation8)"));
    if (hasAutomation2) {
        DWORD connectionMs = 0;
        DWORD transactionMs = 0;
        automation2->get_ConnectionTimeout(&connectionMs);
        automation2->get_TransactionTimeout(&transactionMs);
        check(connectionMs == 500, QStringLiteral("UiaTimeouts: ConnectionTimeout queda leible en 500 ms"));
        check(transactionMs == 1000, QStringLiteral("UiaTimeouts: TransactionTimeout queda leible en 1000 ms"));
        automation2->Release();
    }
    automation->Release();
}

// El limite de las llamadas a Explorer: el valor tiene que ser una constante RPC_C_BINDING_* (con
// milisegundos, IRpcOptions::Set falla con E_INVALIDARG y el proxy queda sin limite), y Set tiene que
// aceptarlo sobre el proxy real de IShellWindows (solo lectura: no toca ninguna ventana).
void explorerRpcTimeoutSelfTest(const std::function<void(bool, const QString &)> &check)
{
    const ULONG value = FolderResolver::kExplorerRpcTimeout;
    check(value == RPC_C_BINDING_MIN_TIMEOUT || value == RPC_C_BINDING_DEFAULT_TIMEOUT
              || value == RPC_C_BINDING_MAX_TIMEOUT,
          QStringLiteral("Explorer: el limite RPC es una constante RPC_C_BINDING_* (no milisegundos)"));

    IShellWindows *shellWindows = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_IShellWindows,
                                reinterpret_cast<void **>(&shellWindows)))
        || !shellWindows) {
        qInfo() << "[folderSwitch] self-test: sin IShellWindows en esta sesion, no se prueba IRpcOptions::Set";
        return;
    }
    IRpcOptions *rpcOptions = nullptr;
    if (SUCCEEDED(shellWindows->QueryInterface(IID_IRpcOptions, reinterpret_cast<void **>(&rpcOptions)))
        && rpcOptions) {
        const HRESULT hr = rpcOptions->Set(shellWindows, COMBND_RPCTIMEOUT, value);
        check(SUCCEEDED(hr), QStringLiteral("Explorer: IRpcOptions::Set acepta el limite sobre IShellWindows (hr=%1)").arg(hr));
        // Negativo: 2000 (milisegundos) es rechazado, que es lo que dejaba el proxy sin limite.
        const HRESULT bad = rpcOptions->Set(shellWindows, COMBND_RPCTIMEOUT, 2000);
        check(FAILED(bad), QStringLiteral("Explorer: IRpcOptions::Set rechaza 2000 (no son milisegundos)"));
        rpcOptions->Set(shellWindows, COMBND_RPCTIMEOUT, value);
        rpcOptions->Release();
    }
    shellWindows->Release();
}

void folderSwitchSelfTest(const std::function<void(bool, const QString &)> &check)
{
    // ---- Deduplicado y orden de recientes (sin ModuleContext: todo en memoria) ----
    {
        FolderSwitchState state(nullptr);
        state.addRecentFolder(QStringLiteral("C:\\Proyectos\\A"));
        state.addRecentFolder(QStringLiteral("C:\\Proyectos\\B"));
        state.addRecentFolder(QStringLiteral("c:\\proyectos\\a\\")); // misma que A: mayus/minus + barra final
        check(state.recentFolders()
                  == QStringList({QStringLiteral("c:\\proyectos\\a\\"), QStringLiteral("C:\\Proyectos\\B")}),
              QStringLiteral("recientes: reingresar una carpeta la mueve al tope sin duplicarla"));
        for (int i = 0; i < 6; ++i) {
            state.addRecentFolder(QStringLiteral("C:\\Carpeta%1").arg(i));
        }
        check(state.recentFolders().size() == FolderSwitchState::kMaxRecentFolders,
              QStringLiteral("recientes: nunca pasa de %1").arg(FolderSwitchState::kMaxRecentFolders));
        check(state.recentFolders().first() == QStringLiteral("C:\\Carpeta5"),
              QStringLiteral("recientes: la ultima agregada queda primera"));
        check(FolderSwitchState::sameFolder(QStringLiteral("D:\\X\\"), QStringLiteral("d:\\x")),
              QStringLiteral("sameFolder: mayusculas y barra final no distinguen"));
        check(!FolderSwitchState::sameFolder(QStringLiteral("D:\\X"), QStringLiteral("D:\\Y")),
              QStringLiteral("sameFolder: carpetas distintas no son iguales (negativo)"));
    }

    // ---- Atajos de fabrica y su ida y vuelta por texto portable ----
    check(FolderSwitchState::defaultManualShortcut().toPortableString() == QLatin1String("Ctrl+Alt+O"),
          QStringLiteral("atajo manual de fabrica: Ctrl+Alt+O"));
    check(FolderSwitchState::defaultRecentShortcut().toPortableString() == QLatin1String("Ctrl+Alt+Shift+O"),
          QStringLiteral("atajo de recientes de fabrica: Ctrl+Alt+Shift+O"));
    for (const Shortcut &s : {FolderSwitchState::defaultManualShortcut(), FolderSwitchState::defaultRecentShortcut()}) {
        check(Shortcut::fromPortableString(s.toPortableString()) == s,
              QStringLiteral("atajo: ida y vuelta por el .ini: %1").arg(s.toPortableString()));
    }

    // ---- Frescura del manager y decision de auto-switch (FolderSwitchLogic, sin HWNDs) ----
    using FolderSwitchLogic::isManagerFresh;
    using FolderSwitchLogic::shouldAutoSwitch;
    check(isManagerFresh(0, 59999), QStringLiteral("frescura: 59,999 s despues, sigue fresco"));
    check(!isManagerFresh(0, 60000), QStringLiteral("frescura: a los 60 s deja de estar fresco (negativo)"));
    check(shouldAutoSwitch(true, true, true, true, false),
          QStringLiteral("auto-switch: las cinco condiciones se cumplen"));
    check(!shouldAutoSwitch(false, true, true, true, false),
          QStringLiteral("auto-switch: autoSwitch apagado, no dispara (negativo)"));
    check(!shouldAutoSwitch(true, false, true, true, false),
          QStringLiteral("auto-switch: modulo en pausa, no dispara (negativo)"));
    check(!shouldAutoSwitch(true, true, false, true, false),
          QStringLiteral("auto-switch: manager viejo, no dispara (negativo)"));
    check(!shouldAutoSwitch(true, true, true, false, false),
          QStringLiteral("auto-switch: no es el dialogo pendiente de retorno, no dispara (negativo)"));
    check(!shouldAutoSwitch(true, true, true, true, true),
          QStringLiteral("auto-switch: ya se le inyecto a este dialogo, no repite (negativo)"));

    // ---- Ida y vuelta de los atajos por el .ini (FolderSwitchState + ModuleContext de prueba) ----
    {
        FakeModuleContext ctx;
        FolderSwitchState state(&ctx);
        Shortcut altManual;
        altManual.modifiers = Qt::ControlModifier | Qt::ShiftModifier;
        altManual.key = Qt::Key_K;
        state.setManualShortcut(altManual);
        FolderSwitchState reloaded(&ctx);
        check(reloaded.manualShortcut() == altManual,
              QStringLiteral("shortcuts/manual: lo que se guarda se vuelve a leer igual"));
    }

    // ---- Ciclo de vida del modulo: declarar/registrar en start(), 0 atajos y sin watcher en stop() ----
    {
        FakeModuleContext ctx;
        FolderSwitchModule module(ctx);
        check(!module.hasForegroundWatcher(), QStringLiteral("recien construido: sin watcher"));
        module.start();
        check(module.manualShortcutRegistered() && module.recentShortcutRegistered(),
              QStringLiteral("start(): los dos atajos de fabrica se registran sin choques"));
        check(ctx.hotkeysForTest().isRegistered(1) && ctx.hotkeysForTest().isRegistered(2),
              QStringLiteral("start(): ModuleHotkeys tiene los dos ids registrados"));
        check(module.hasForegroundWatcher(), QStringLiteral("start(): toma el observador de ventana al frente compartido"));
        check(module.status().tone == ModuleTone::Active, QStringLiteral("status(): On con los dos atajos libres"));
        module.stop();
        check(!ctx.hotkeysForTest().isRegistered(1) && !ctx.hotkeysForTest().isRegistered(2),
              QStringLiteral("stop(): 0 atajos registrados"));
        check(!module.hasForegroundWatcher(), QStringLiteral("stop(): suelta el observador compartido"));
        check(!module.hasRecentPopup(), QStringLiteral("stop(): sin popup de recientes abierto"));
    }

    // ---- Choque de atajo con otra herramienta (declare()) ----
    {
        FakeModuleContext ctx;
        ctx.hotkeysForTest().setForeignDeclaration(FolderSwitchState::defaultManualShortcut(),
                                                   QStringLiteral("Otra herramienta"));
        FolderSwitchModule module(ctx);
        module.start();
        check(!module.manualShortcutRegistered(),
              QStringLiteral("start(): el atajo manual tomado por otro modulo no se registra"));
        check(module.manualShortcutTakenBy() == QStringLiteral("Otra herramienta"),
              QStringLiteral("start(): el estado conoce quien tiene el atajo"));
        check(module.recentShortcutRegistered(),
              QStringLiteral("start(): el atajo de recientes no se ve afectado por el choque del otro"));
        check(module.status().tone == ModuleTone::Error, QStringLiteral("status(): Error con un atajo tomado"));
        module.stop();
    }

    // ---- Rechazo del sistema operativo (RegisterHotKey devuelve false) ----
    {
        FakeModuleContext ctx;
        ctx.hotkeysForTest().setRejectRegister(FolderSwitchState::defaultRecentShortcut());
        FolderSwitchModule module(ctx);
        module.start();
        check(module.manualShortcutRegistered() && !module.recentShortcutRegistered(),
              QStringLiteral("start(): el sistema rechaza el atajo de recientes, el manual sigue andando"));
        check(module.recentShortcutTakenBy().isEmpty(),
              QStringLiteral("start(): un rechazo del sistema no es 'tomado por otro modulo' (negativo)"));
        check(module.status().tone == ModuleTone::Error,
              QStringLiteral("status(): Error si el sistema rechaza uno de los dos"));
        module.stop();
    }

    // ---- validateShortcut(): el mensaje que ve el ShortcutRow de la etapa 2 ----
    {
        FakeModuleContext ctx;
        Shortcut takenByOther;
        takenByOther.modifiers = Qt::ControlModifier;
        takenByOther.key = Qt::Key_K;
        ctx.hotkeysForTest().setForeignDeclaration(takenByOther, QStringLiteral("Nuke Shortcuts"));
        Shortcut takenBySystem;
        takenBySystem.modifiers = Qt::ControlModifier;
        takenBySystem.key = Qt::Key_J;

        FolderSwitchModule module(ctx);
        check(module.validateShortcut(takenByOther) == QStringLiteral("Already used by Nuke Shortcuts."),
              QStringLiteral("validateShortcut: choque con otra herramienta (declaredByOtherModule)"));
        ctx.hotkeysForTest().setRejectRegister(takenBySystem);
        check(module.validateShortcut(takenBySystem) == QStringLiteral("Ctrl+J is taken by another app."),
              QStringLiteral("validateShortcut: rechazo del sistema (probe)"));
        Shortcut free;
        free.modifiers = Qt::ControlModifier;
        free.key = Qt::Key_H;
        check(module.validateShortcut(free).isEmpty(),
              QStringLiteral("validateShortcut: una combinacion libre no da error (negativo)"));
    }

    // ---- setManualShortcut(): D-16, el atajo editable con el lapiz ----
    {
        FakeModuleContext ctx;
        FolderSwitchModule module(ctx);
        module.start();
        Shortcut fresh;
        fresh.modifiers = Qt::ControlModifier | Qt::AltModifier;
        fresh.key = Qt::Key_P;
        check(module.setManualShortcut(fresh), QStringLiteral("setManualShortcut: una combinacion libre se acepta"));
        check(module.manualShortcutRegistered(), QStringLiteral("setManualShortcut: queda registrada"));

        ctx.hotkeysForTest().setForeignDeclaration(FolderSwitchState::defaultRecentShortcut(),
                                                   QStringLiteral("Otra herramienta"));
        check(!module.setManualShortcut(FolderSwitchState::defaultRecentShortcut()),
              QStringLiteral("setManualShortcut: una combinacion de otra herramienta se rechaza (negativo)"));
        check(module.manualShortcutRegistered(),
              QStringLiteral("setManualShortcut: al rechazar, la anterior sigue registrada"));
        module.stop();
    }

    windowClassificationSelfTest(check);
    qtDialogCacheSelfTest(check);
    uiaTimeoutSelfTest(check);
    explorerRpcTimeoutSelfTest(check);
    // El popup de recientes (RecentFoldersPopup) es un QWidget: --self-test corre bajo
    // QCoreApplication (sin QApplication), asi que no se puede construir aca -- "si se puede" del
    // encargo no se da. Su navegacion por teclado (orden, wrap, Esc) se verifica con las capturas
    // recent-popup/-one/-empty (--ui-shot) y a mano, no automatizado.
}

// --simulate-action switch-dialog <hwnd> <folder>: equivalente al --test-switch del origen, pero solo
// clasifica y loguea el camino que se tomaria (Win32 o UI Automation) y la carpeta ya normalizada. En
// una corrida automatizada NUNCA escribe en el dialogo ni manda teclas: no llama a
// DialogSwitcher::switchDialog ni a UiaSwitcher.
int folderSwitchSimulateAction(const QString &action, const QStringList &args)
{
    if (action != QLatin1String("switch-dialog")) {
        qWarning() << "[folderSwitch] simulate-action desconocida:" << action
                   << "(uso: switch-dialog <hwnd> <folder>)";
        return 2;
    }
    if (args.size() < 2) {
        qWarning() << "[folderSwitch] simulate-action switch-dialog: uso <hwnd> <folder>";
        return 2;
    }
    bool ok = false;
    const quintptr hwndValue = args.at(0).toULongLong(&ok);
    if (!ok) {
        qWarning() << "[folderSwitch] hwnd invalido:" << args.at(0);
        return 2;
    }
    const HWND dlg = reinterpret_cast<HWND>(hwndValue);
    if (!dlg || !IsWindow(dlg)) {
        qInfo() << "[folderSwitch] simulate-action: hwnd" << args.at(0) << "no existe, no se puede clasificar";
        return 1;
    }

    QString path = QDir::toNativeSeparators(args.at(1));
    if (!path.endsWith(QChar::fromLatin1('\\'))) {
        path += QChar::fromLatin1('\\');
    }
    const bool viaUia = WindowUtils::isQtFileDialog(dlg);
    qInfo() << "[folderSwitch] simulate-action: hwnd=" << dlg
            << "camino=" << (viaUia ? "UI Automation (dialogo Qt)" : "Win32 (ComboBoxEx32/Edit)")
            << "path=" << path;
    return 0;
}

} // namespace

ModuleDescriptor folderSwitchDescriptor()
{
    ModuleDescriptor d;
    d.id = QStringLiteral("folderSwitch");
    d.title = QStringLiteral("Folder Switch");
    // Texto exacto del canvas de diseno (MODS, id 'fs').
    d.description = I18n::tr("Open and Save dialogs jump to the folder you have open in Explorer or XYplorer.");
    d.offBullets = folderSwitchOffBullets([](const QString &, const QVariant &fallback) { return fallback; });
    // Los atajos de la vineta son los CONFIGURADOS (settings.ini), no los de fabrica.
    d.offBulletsFor = &folderSwitchOffBullets;
    d.platforms = PlatformWindows;
    d.paintIcon = paintFolderSwitchIcon;
    d.create = [](ModuleContext &context) -> std::unique_ptr<Module> {
        return std::make_unique<FolderSwitchModule>(context);
    };
    d.configuredShortcuts = folderSwitchConfiguredShortcuts;
    d.selfTest = folderSwitchSelfTest;
    d.simulateAction = folderSwitchSimulateAction;
    return d;
}

HelpSection folderSwitchHelp(const SettingsReader &)
{
    HelpSection section;
    section.title = QStringLiteral("Folder Switch");
    section.steps = {
        I18n::tr("Open a folder in %1 or %2.")
            .arg(HelpSection::strong(QStringLiteral("Explorer")), HelpSection::strong(QStringLiteral("XYplorer"))),
        I18n::tr("Go to the %1 or %2 dialog of any app.")
            .arg(HelpSection::strong(I18n::tr("Open")), HelpSection::strong(I18n::tr("Save"))),
        I18n::tr("The dialog jumps to that folder."),
    };
    section.note = I18n::tr("Works with Windows file dialogs and Qt ones, like Nuke's.");
    return section;
}
