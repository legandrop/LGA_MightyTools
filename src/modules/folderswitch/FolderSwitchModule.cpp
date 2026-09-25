#include "modules/folderswitch/FolderSwitchModule.h"

#include "app/ModuleContext.h"
#include "modules/folderswitch/DialogSwitcher.h"
#include "modules/folderswitch/FolderResolver.h"
#include "modules/folderswitch/FolderSwitchLogic.h"
#include "modules/folderswitch/WindowUtils.h"
#include "platform/ForegroundWatcher.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QSet>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

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
    m_manualShortcut = m_state.manualShortcut();
    m_recentShortcut = m_state.recentShortcut();

    ModuleHotkeys *hk = context().hotkeys();
    m_manualTakenBy = hk->declare(kManualHotkeyId, m_manualShortcut);
    m_recentTakenBy = hk->declare(kRecentHotkeyId, m_recentShortcut);

    m_manualRegistered = m_manualTakenBy.isEmpty() && hk->registerHotkey(kManualHotkeyId, m_manualShortcut);
    m_recentRegistered = m_recentTakenBy.isEmpty() && hk->registerHotkey(kRecentHotkeyId, m_recentShortcut);

    qInfo() << "[folderSwitch] Atajo manual" << m_manualShortcut.toPortableString() << "->"
            << (m_manualRegistered
                    ? QStringLiteral("registrado")
                    : (m_manualTakenBy.isEmpty() ? QStringLiteral("rechazado por el sistema")
                                                 : QStringLiteral("tomado por %1").arg(m_manualTakenBy)));
    qInfo() << "[folderSwitch] Atajo de recientes" << m_recentShortcut.toPortableString() << "->"
            << (m_recentRegistered
                    ? QStringLiteral("registrado")
                    : (m_recentTakenBy.isEmpty() ? QStringLiteral("rechazado por el sistema")
                                                 : QStringLiteral("tomado por %1").arg(m_recentTakenBy)));
}

void FolderSwitchModule::start()
{
    declareAndRegisterShortcuts();

    // active=false en toda corrida automatizada: el objeto existe (para que stop() tenga algo que
    // destruir y el self-test pueda medir el ciclo de vida) pero no instala ningun hook real.
    m_foregroundWatcher = std::make_unique<ForegroundWatcher>(!context().automatedRun());
    connect(m_foregroundWatcher.get(), &ForegroundWatcher::foregroundChanged, this,
            &FolderSwitchModule::onForegroundChanged);
    connect(context().hotkeys(), &ModuleHotkeys::activated, this, &FolderSwitchModule::onHotkeyActivated);
}

void FolderSwitchModule::stop()
{
    disconnect(context().hotkeys(), nullptr, this, nullptr);
    context().hotkeys()->unregisterAll();
    m_manualRegistered = false;
    m_recentRegistered = false;
    m_manualTakenBy.clear();
    m_recentTakenBy.clear();

    m_foregroundWatcher.reset();

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
        if (!m_manualRegistered && !m_recentRegistered) {
            s.text = QStringLiteral("Both shortcuts are off");
        } else {
            const Shortcut &bad = m_manualRegistered ? m_recentShortcut : m_manualShortcut;
            s.text = QStringLiteral("%1 is off").arg(bad.displayText());
        }
        return s;
    }
    if (!m_state.enabled()) {
        s.tone = ModuleTone::Paused;
        s.text = QStringLiteral("Paused");
        return s;
    }
    s.tone = ModuleTone::Active;
    s.text = QStringLiteral("On");
    return s;
}

bool FolderSwitchModule::isPaused() const
{
    return !m_state.enabled();
}

QWidget *FolderSwitchModule::createPanel(QWidget *parent)
{
    // Placeholder de la etapa 1: sin la ventana/ModuleHost integrados todavia no hay donde probar el
    // panel real (atajos editables, checkbox de auto-switch, tarjeta de ultima carpeta, popup de
    // recientes). Eso es la etapa 2.
    auto *panel = new QWidget(parent);
    auto *layout = new QVBoxLayout(panel);
    auto *label = new QLabel(QStringLiteral("Folder Switch"), panel);
    layout->addWidget(label);
    layout->addStretch();
    return panel;
}

bool FolderSwitchModule::setManualShortcut(const Shortcut &shortcut)
{
    ModuleHotkeys *hk = context().hotkeys();
    const QString takenBy = hk->declare(kManualHotkeyId, shortcut);
    if (!takenBy.isEmpty()) {
        m_manualTakenBy = takenBy;
        m_manualRegistered = false;
        emit statusChanged();
        return false;
    }
    const bool ok = hk->registerHotkey(kManualHotkeyId, shortcut);
    m_manualTakenBy.clear();
    if (ok) {
        m_manualShortcut = shortcut;
        m_manualRegistered = true;
        m_state.setManualShortcut(shortcut);
    } else {
        // El sistema lo rechazo: se vuelve a declarar y registrar el anterior para no dejar la
        // declaracion apuntando a una combinacion que no quedo activa.
        hk->declare(kManualHotkeyId, m_manualShortcut);
        m_manualRegistered = hk->registerHotkey(kManualHotkeyId, m_manualShortcut);
    }
    emit statusChanged();
    return ok;
}

bool FolderSwitchModule::setRecentShortcut(const Shortcut &shortcut)
{
    ModuleHotkeys *hk = context().hotkeys();
    const QString takenBy = hk->declare(kRecentHotkeyId, shortcut);
    if (!takenBy.isEmpty()) {
        m_recentTakenBy = takenBy;
        m_recentRegistered = false;
        emit statusChanged();
        return false;
    }
    const bool ok = hk->registerHotkey(kRecentHotkeyId, shortcut);
    m_recentTakenBy.clear();
    if (ok) {
        m_recentShortcut = shortcut;
        m_recentRegistered = true;
        m_state.setRecentShortcut(shortcut);
    } else {
        hk->declare(kRecentHotkeyId, m_recentShortcut);
        m_recentRegistered = hk->registerHotkey(kRecentHotkeyId, m_recentShortcut);
    }
    emit statusChanged();
    return ok;
}

void FolderSwitchModule::onForegroundChanged(quintptr hwndValue, quint32 /*pid*/, const QString & /*exeName*/)
{
    // Defensa en profundidad: el watcher esta inerte (sin hook real) en toda corrida automatizada, asi
    // que esto nunca deberia dispararse ahi. Si algun dia deja de serlo, no se toca ningun dialogo ajeno.
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
    // El popup de carpetas recientes es la etapa 2 (panel, popup, bandeja): el atajo ya se declara y
    // se registra ahora (chip "In use" del panel futuro y choques con otras herramientas ya
    // funcionan), pero al presionarlo todavia no hay nada que mostrar.
    qDebug() << "[folderSwitch] Atajo de recientes: popup pendiente para la etapa 2.";
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
// implementacion real de ModuleContext (la construye el host, en paralelo). Viven aca, no se
// compilan ni se linkean fuera de este modulo.
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

    void notify(const QString &, const QString &, NoticeIcon, int) override {}
    void showPanel() override {}
    void hideWindowTemporarily() override {}
    void restoreWindow() override {}
    QWidget *window() const override { return nullptr; }

    FakeModuleHotkeys &hotkeysForTest() { return m_hotkeys; }

private:
    QHash<QString, QVariant> m_values;
    FakeModuleHotkeys m_hotkeys;
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
        check(ctx.hotkeysForTest().isRegistered(kManualHotkeyId) && ctx.hotkeysForTest().isRegistered(kRecentHotkeyId),
              QStringLiteral("start(): ModuleHotkeys tiene los dos ids registrados"));
        check(module.hasForegroundWatcher(), QStringLiteral("start(): crea el observador de ventana al frente"));
        check(module.status().tone == ModuleTone::Active, QStringLiteral("status(): On con los dos atajos libres"));
        module.stop();
        check(!ctx.hotkeysForTest().isRegistered(kManualHotkeyId) && !ctx.hotkeysForTest().isRegistered(kRecentHotkeyId),
              QStringLiteral("stop(): 0 atajos registrados"));
        check(!module.hasForegroundWatcher(), QStringLiteral("stop(): sin watcher"));
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

    windowClassificationSelfTest(check);
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
    d.description = QStringLiteral("Open and Save dialogs jump to the folder you have open in Explorer or XYplorer.");
    d.offBullets = {
        QStringLiteral("Watches which window is in front"),
        QStringLiteral("Two shortcuts: Ctrl+Alt+O and Ctrl+Alt+Shift+O"),
        QStringLiteral("Remembers your last 5 folders"),
    };
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
