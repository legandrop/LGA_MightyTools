#include "app/AppController.h"

#include "app/ExternalDispatch.h"
#include "app/MainWindow.h"
#include "app/ModuleHost.h"
#include "app/ModuleRegistry.h"
#include "app/SettingsStore.h"
#include "app/TrayMenu.h"
#include "core/AppPaths.h"
#include "core/BuildTree.h"
#include "core/AutomatedRun.h"
#include "core/I18n.h"
#include "core/UiScale.h"
#include "platform/AutoStart.h"
#include "platform/SystemNotifier.h"
#include "platform/ToastActivation.h"
#include "platform/WindowActivation.h"
#include "ui/HelpDialog.h"

#include "updates/UpdateService.h"

#include <QAction>
#include <QApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QFileOpenEvent>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QScreen>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace {

const QString kCheckUpdates = QStringLiteral("app/checkUpdatesAtStartup");
const QString kFirstRunCompleted = QStringLiteral("app/firstRunCompleted");
const QString kWelcomeDone = QStringLiteral("app/welcomeDone");
const QString kAutoStartDecided = QStringLiteral("app/autoStartDecided");
const QString kLanguage = QStringLiteral("app/language");

// Los argumentos que vuelven con el click en un aviso: "module=diskSpace&action=snooze&key=C%3A%2F".
QString noticeArguments(const QString &moduleId, const QString &action, const QString &key)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("module"), QString::fromLatin1(QUrl::toPercentEncoding(moduleId)));
    query.addQueryItem(QStringLiteral("action"), QString::fromLatin1(QUrl::toPercentEncoding(action)));
    if (!key.isEmpty()) {
        query.addQueryItem(QStringLiteral("key"), QString::fromLatin1(QUrl::toPercentEncoding(key)));
    }
    return query.toString(QUrl::FullyEncoded);
}

} // namespace

AppController::AppController(const Options &options, QObject *parent)
    : QObject(parent)
    , m_options(options)
{
    if (m_options.measurement) {
        m_store = std::make_unique<MemorySettingsStore>();
    } else {
        m_store = std::make_unique<FileSettingsStore>();
    }
    m_buildTree = LgaBuildTree::isBuildTree(AppPaths::exeDir());
    // Antes de armar cualquier texto. Sin valor guardado (o en la medicion): ingles.
    I18n::setLanguage(I18n::fromCode(m_store->value(kLanguage).toString()));

    HostOptions hostOptions;
    hostOptions.automatedRun = m_options.measurement;
    hostOptions.buildTree = m_buildTree;
    hostOptions.dryRunInput = m_options.dryRunInput;
    // El objeto es barato: el hilo y PowerShell nacen con la primera notificacion.
    m_notifier = new SystemNotifier(m_options.measurement, this);
    m_host = new ModuleHost(ModuleRegistry::all(), m_store.get(), hostOptions, this);
    m_host->setHostServices(this);

    m_window = new MainWindow(m_host, MainWindow::Mode::Normal, m_options.measurement ? nullptr : m_store.get());
    connect(m_window, &MainWindow::toggleRequested, this, &AppController::onToggleRequested);
    connect(m_window, &MainWindow::releaseRequested, this, &AppController::onReleaseRequested);
    connect(m_window, &MainWindow::helpRequested, this, [this]() {
        const auto providers = ModuleRegistry::helpProviders();
        QList<HelpSection> sections;
        for (const ModuleDescriptor &d : m_host->descriptors()) {
            if (providers.contains(d.id)) {
                sections.append(providers.value(d.id)(m_host->reader(d.id)));
            } else {
                sections.append(HelpSection{d.title, {}, d.description});
            }
        }
        HelpDialog dialog(sections, m_window);
        dialog.execOver(m_window);
    });
    connect(m_window, &MainWindow::rebuilt, this, &AppController::wireGeneralPage);
#ifdef Q_OS_MACOS
    // Cmd+Q con la ventana al frente sale de la app, como en cualquier app de mac (no hay barra de menu
    // propia: LSUIElement).
    auto *quitAction = new QAction(m_window);
    quitAction->setShortcut(QKeySequence::Quit);
    quitAction->setShortcutContext(Qt::WindowShortcut);
    connect(quitAction, &QAction::triggered, this, &AppController::quit);
    m_window->addAction(quitAction);
#endif

    connect(m_host, &ModuleHost::moduleToggled, this, [this]() { refreshTray(); });
    connect(m_host, &ModuleHost::moduleStatusChanged, this, [this]() { refreshTray(); });
    connect(m_host, &ModuleHost::moduleDestroyed, this, [this]() {
        // El menu nunca se rearma abierto: con el menu a la vista queda para el proximo aboutToShow.
        if (m_menu && !m_menu->isVisible()) {
            rebuildMenu();
        }
    });

    if (!m_options.measurement) {
        m_menu = new QMenu();
        connect(m_menu, &QMenu::aboutToShow, this, &AppController::rebuildMenu);
        m_tray = new QSystemTrayIcon(this);
#ifdef Q_OS_MACOS
        // macOS (pedido de Lega, 2026-10-02): el click en la barra de menu abre la ventana, sin menu.
        // Pausar, calibrar y salir viven en la ventana (Quit en General).
#else
        m_tray->setContextMenu(m_menu);
#endif
        connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::DoubleClick || reason == QSystemTrayIcon::Trigger) {
                showSettings();
            }
        });
        // El click en una notificacion abre la ventana en la herramienta que aviso.
        connect(m_tray, &QSystemTrayIcon::messageClicked, this, [this]() {
            if (m_lastNotifier.isEmpty()) {
                showSettings();
            } else {
                showPanel(m_lastNotifier);
            }
        });

        // El canal de la instancia unica lo abre main() desde el arranque, antes de la bandeja.
        // mac: los .nk y los links llegan a la residente como QFileOpenEvent (los que llegaron antes
        // de este punto los guarda main() y los pasa por openExternal).
        qApp->installEventFilter(this);
        // mac: abrir la app desde Finder o Spotlight con esta copia corriendo abre la ventana.
        WindowActivation::onReopenRequested([this]() { showSettings(); });
    }

    if (!m_options.measurement) {
        // parentWindow es la ventana (normalmente oculta): sus dialogos igual se centran en pantalla.
        m_updates = new UpdateService(m_window, this);
        m_updates->setSettingsStore(m_store.get());
        m_updates->setAutomaticChecksEnabled([this]() { return m_store->value(kCheckUpdates, true).toBool(); });
        connect(m_updates, &UpdateService::checking, this, [this]() {
            m_window->setUpdateState(UpdateRowState{UpdateRowState::Kind::Checking, QString()});
        });
        connect(m_updates, &UpdateService::upToDate, this, [this](const QString &version) {
            m_window->setUpdateState(UpdateRowState{UpdateRowState::Kind::Latest, version});
        });
        connect(m_updates, &UpdateService::updateAvailable, this, [this](const QString &version) {
            m_window->setUpdateState(UpdateRowState{UpdateRowState::Kind::Available, version});
        });
        connect(m_updates, &UpdateService::checkFailed, this,
                [this]() { m_window->setUpdateState(UpdateRowState()); });
    }

    wireGeneralPage();
    const bool checkUpdates = m_store->value(kCheckUpdates, true).toBool();

    m_host->startEnabled();
    m_window->setFirstRun(firstRunView());

    if (m_tray) {
        refreshTray();
        rebuildMenu();
        m_tray->show();
    }

    if (!m_options.measurement) {
        // Los clicks en los avisos, con las herramientas ya prendidas. Incluye los que llegaron antes
        // (la app lanzada por Windows desde un aviso, con el click esperando).
        ToastActivation::setHandler([this](const ToastActivation::Activation &activation) {
            onNoticeClicked(activation.arguments, activation.inputs.value(SystemNotifier::choiceInputId()));
        });
    }

    if (m_updates) {
        if (checkUpdates) {
            m_updates->scheduleAutomaticCheck();
        } else {
            qInfo() << "[AppController] Chequeo de updates al arrancar: desactivado por el usuario";
        }
    }

    if (!m_options.measurement) {
        // La ventana se abre sola SOLO en el primer arranque de la copia instalada (D-06, canvas
        // seccion 7); despues, cuando la abre el usuario (bandeja, otra copia). La marca se guarda
        // solo en una copia instalada: un arranque desde build/ no consume el primer arranque de la
        // instalacion futura. main() pide openWindow si no hay bandeja o si otra copia la pidio antes.
        const bool installed = AutoStart::availability().available;
        const bool firstLaunch = installed && !m_store->value(kFirstRunCompleted, false).toBool();
        if (firstLaunch) {
            m_store->setValue(kFirstRunCompleted, true);
        }
        if (firstLaunch || m_options.openWindow) {
            showSettings();
        }
    }
}

AppController::~AppController()
{
    ToastActivation::setHandler(nullptr);
    // Primero las herramientas (sus paneles son hijos de la ventana), despues la ventana y el menu.
    m_host->shutdown();
    delete m_window;
    delete m_menu;
}

bool AppController::firstRunView() const
{
    return !m_store->value(kWelcomeDone, false).toBool() && m_host->runningCount() == 0;
}

void AppController::onToggleRequested(const QString &id, bool on)
{
    m_host->setEnabled(id, on);
    if (on && m_host->isRunning(id)) {
        onFirstToolOn();
        m_window->selectPage(id);
    }
}

void AppController::onFirstToolOn()
{
    if (!m_store->value(kWelcomeDone, false).toBool()) {
        m_store->setValue(kWelcomeDone, true);
        m_window->setFirstRun(false);
    }
    // El inicio con el sistema se activa solo con la primera herramienta que se prende (D-06), una
    // sola vez y solo en una copia instalada: si despues el usuario lo apaga, queda apagado.
    if (m_store->value(kAutoStartDecided, false).toBool() || m_options.measurement || m_buildTree) {
        return;
    }
    const AutoStart::Availability availability = AutoStart::availability();
    if (!availability.available) {
        qInfo() << "[AppController] Primera herramienta prendida: inicio automatico no disponible (" << availability.text
                << ")";
        return;
    }
    m_store->setValue(kAutoStartDecided, true);
    const bool ok = AutoStart::setEnabled(true);
    qInfo() << "[AppController] Primera herramienta prendida: inicio con la sesion activado ok:" << ok;
    refreshAutoStart();
}

void AppController::onReleaseRequested(const QString &id)
{
    const ModuleDescriptor *d = m_host->descriptor(id);
    if (!d || !d->releaseSystem || m_options.measurement) {
        return;
    }
    QString error;
    if (!d->releaseSystem(&error)) {
        QMessageBox::warning(m_window, d->title, error);
    }
    m_window->selectPage(id);
}

void AppController::refreshAutoStart()
{
    if (m_options.measurement) {
        m_window->generalPage()->setAutoStart(false, false);
        return;
    }
    m_window->generalPage()->setAutoStart(AutoStart::isEnabled(), AutoStart::availability().available);
}

void AppController::onAutoStartToggled(bool enabled)
{
    // El click EXPLICITO del usuario se respeta tambien desde un build (ver AutoStart.h).
    const bool ok = AutoStart::setEnabled(enabled);
    qInfo() << "[AppController] AutoStart" << (enabled ? "ON" : "OFF") << "ok:" << ok << "| activo ahora:"
            << AutoStart::isEnabled() << "| valor en Run:"
            << (AutoStart::storedCommand().isEmpty() ? QStringLiteral("(ninguno)") : AutoStart::storedCommand());
    m_store->setValue(kAutoStartDecided, true);
    refreshAutoStart();
}

void AppController::wireGeneralPage()
{
    GeneralPage *general = m_window->generalPage();
    connect(general, &GeneralPage::autoStartToggled, this, &AppController::onAutoStartToggled);
    connect(general, &GeneralPage::checkUpdatesAtStartupToggled, this,
            [this](bool check) { m_store->setValue(kCheckUpdates, check); });
    connect(general, &GeneralPage::languageChangeRequested, this, &AppController::onLanguageChangeRequested);
    connect(general, &GeneralPage::uiSizeChangeRequested, this, &AppController::onUiSizeChangeRequested);
    connect(general, &GeneralPage::quitRequested, this, &AppController::quit);
    if (m_updates) {
        connect(general, &GeneralPage::checkNowRequested, m_updates, &UpdateService::checkInline);
        connect(general, &GeneralPage::updateRequested, m_updates, &UpdateService::installAvailable);
    }
    general->setCheckUpdatesAtStartup(m_store->value(kCheckUpdates, true).toBool());
    refreshAutoStart();
}

void AppController::onLanguageChangeRequested(const QString &code)
{
    const I18n::Language language = I18n::fromCode(code);
    if (language == I18n::language()) {
        return;
    }
    m_store->setValue(kLanguage, I18n::code(language));
    I18n::setLanguage(language);
    m_host->refreshDescriptorTexts(ModuleRegistry::all());
    qInfo().noquote() << QStringLiteral("[AppController] Idioma: %1").arg(I18n::code(language));
    // Fuera de la senal: el desplegable que la mando es parte de lo que se borra. El menu de la
    // bandeja, los avisos y la ayuda se arman al vuelo y ya salen en el idioma nuevo.
    QTimer::singleShot(0, this, [this]() {
        m_window->rebuildUi();
        refreshTray();
    });
}

void AppController::onUiSizeChangeRequested(int level)
{
    level = UiScale::clampLevel(level);
    if (level == UiScale::sessionLevel()) {
        return;
    }
    // Una corrida automatizada nunca relanza la app (no deberia llegar aca: no hay ventana real).
    if (m_options.measurement || AutomatedRun::active()) {
        return;
    }
    qInfo().noquote() << QStringLiteral("[AppController] Tamano de interfaz: %1 -> %2").arg(UiScale::sessionLevel()).arg(level);
    // La posicion se guarda en pixeles logicos, que cambian de tamano con el factor: se convierte para
    // que la ventana vuelva a abrir donde estaba (MainWindow::restorePosition la mete en la pantalla
    // si no entra entera). Qt deja el origen de cada pantalla sin escalar: se escala solo la distancia
    // a ese origen, asi tambien vale en un segundo monitor. Primero se oculta: al ocultarse la ventana
    // guarda su posicion tal cual, y despues ya no la vuelve a guardar.
    const qreal ratio = UiScale::factor(UiScale::sessionLevel()) / UiScale::factor(level);
    const QPoint position = m_window->pos();
    const QPoint origin = m_window->screen() ? m_window->screen()->geometry().topLeft() : QPoint();
    // Qt lee el factor solo al arrancar: se lanza una copia nueva que espera a que esta suelte la
    // instancia unica (--relaunch) y abre la ventana. El nivel se guarda ANTES: la copia nueva lo lee
    // apenas arranca. Si no se pudo lanzar, vuelve lo que habia guardado (puede ser mayor que el de esta
    // sesion, recortado por la pantalla) y la app sigue como estaba.
    const int previousSaved = UiScale::readSavedLevel();
    m_store->setValue(UiScale::settingsKey(), level);
    if (!QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--relaunch")})) {
        qWarning() << "[AppController] No se pudo relanzar la app; se mantiene el tamano de interfaz actual";
        m_store->setValue(UiScale::settingsKey(), previousSaved);
        m_window->generalPage()->showSessionUiSize();
        return;
    }
    m_window->hide();
    const QPoint converted = origin + QPoint(qRound((position.x() - origin.x()) * ratio), qRound((position.y() - origin.y()) * ratio));
    m_store->setValue(QStringLiteral("window/x"), converted.x());
    m_store->setValue(QStringLiteral("window/y"), converted.y());
    QTimer::singleShot(0, this, &AppController::quit);
}

void AppController::refreshTray()
{
    if (!m_tray) {
        return;
    }
    m_tray->setIcon(systemTrayIcon(m_host->allPausedOrOff()));
    // El unico tooltip nativo de la app: el del icono de la bandeja lo dibuja el sistema.
    m_tray->setToolTip(trayTooltip(m_host));
}

void AppController::rebuildMenu()
{
    if (!m_menu) {
        return;
    }
    const TrayMenuActions actions = fillTrayMenu(m_menu, m_host);
    connect(actions.settings, &QAction::triggered, this, &AppController::showSettings);
    connect(actions.quit, &QAction::triggered, this, &AppController::quit);
#ifdef Q_OS_WIN
    if (m_updates) {
        connect(actions.updates, &QAction::triggered, this, [this]() { m_updates->checkForUpdates(true); });
    }
#endif
}

void AppController::notify(const QString &moduleId, const QString &title, const QString &body,
                           ModuleContext::NoticeIcon icon, int msecs)
{
    // Toast del sistema con el icono de la app en grande (SystemNotifier, copia de PipeSync), nunca
    // los genericos de informacion o advertencia (pedido de Lega): el tipo de aviso ya lo dice el
    // texto de cada herramienta, asi que NoticeIcon no cambia nada visible. El toast no tiene AUMID:
    // un click no vuelve a la app (diferencia declarada con el plan 4.4).
    Q_UNUSED(icon);
    Q_UNUSED(msecs);
    m_lastNotifier = moduleId;
    SystemNotifier::Notice notice;
    notice.title = title;
    notice.body = body;
    notice.launch = noticeArguments(moduleId, QStringLiteral("open"), QString());
    m_notifier->show(notice);
}

void AppController::notifyWithChoice(const QString &moduleId, const QString &title, const QString &body,
                                     const NoticeChoice &choice)
{
    m_lastNotifier = moduleId;
    SystemNotifier::Notice notice;
    notice.title = title;
    notice.body = body;
    notice.launch = noticeArguments(moduleId, QStringLiteral("open"), QString());
    // El tag de Windows es corto: un hash de la key, con el modulo como grupo.
    notice.tag = QString::fromLatin1(QCryptographicHash::hash(choice.key.toUtf8(), QCryptographicHash::Md5).toHex().left(16));
    notice.group = moduleId;
    notice.persistent = choice.persistent;
    notice.choiceLabel = choice.label;
    notice.choices = choice.options;
    notice.choiceDefault = choice.defaultId;
    notice.button = choice.button;
    notice.buttonArguments = noticeArguments(moduleId, choice.action, choice.key);
    if (!choice.extraButton.isEmpty()) {
        notice.extraButton = choice.extraButton;
        notice.extraButtonArguments = noticeArguments(moduleId, choice.extraAction, choice.key);
    }
    m_notifier->show(notice);
}

void AppController::onNoticeClicked(const QString &arguments, const QString &choice)
{
    const QUrlQuery query(arguments);
    const QString moduleId = query.queryItemValue(QStringLiteral("module"), QUrl::FullyDecoded);
    const QString action = query.queryItemValue(QStringLiteral("action"), QUrl::FullyDecoded);
    const QString key = query.queryItemValue(QStringLiteral("key"), QUrl::FullyDecoded);
    if (action.isEmpty() || action == QLatin1String("open")) {
        // El cuerpo del aviso: la ventana en la herramienta que aviso.
        if (moduleId.isEmpty()) {
            showSettings();
        } else {
            showPanel(moduleId);
        }
        return;
    }
    if (!m_host->isRunning(moduleId)) {
        qInfo().noquote() << QStringLiteral("[AppController] Boton de un aviso de %1 con la herramienta apagada: se ignora")
                                 .arg(moduleId);
        return;
    }
    m_host->module(moduleId)->noticeAction(action, key, choice);
}

void AppController::showPanel(const QString &moduleId)
{
    m_window->selectPage(moduleId);
    showSettings();
}

bool AppController::hideWindow()
{
    const bool visible = m_window->isVisible();
    m_window->hide();
    return visible;
}

void AppController::showWindow()
{
    showSettings();
}

QWidget *AppController::window() const
{
    return m_window;
}

void AppController::showSettings()
{
    if (m_options.measurement) {
        return;
    }
    // Estado real en cada apertura: un cambio hecho por fuera (Task Manager > Startup, Ajustes de
    // macOS, otra copia de la app) tiene que verse sin reiniciar.
    refreshAutoStart();
    m_window->show();
    m_window->raise();
    m_window->activateWindow();
}

bool AppController::eventFilter(QObject *watched, QEvent *event)
{
#ifdef Q_OS_MACOS
    // D-46: con una ventana de la app a la vista, icono en el Dock y lugar en Cmd+Tab. Se recalcula
    // despues del evento (la ventana ya cambio de estado) y solo para ventanas de verdad: los popups,
    // los tooltips y el popup de Folder Switch (Qt::Tool) no cuentan.
    if ((event->type() == QEvent::Show || event->type() == QEvent::Hide) && watched->isWidgetType()) {
        auto *widget = static_cast<QWidget *>(watched);
        if (widget->isWindow() && (widget->windowType() == Qt::Window || widget->windowType() == Qt::Dialog)) {
            QTimer::singleShot(0, this, &AppController::refreshDockIcon);
        }
    }
    // Con icono en el Dock la app tiene su menu, y "Quit" (o Cmd+Q, o cerrar la sesion) llega como
    // QEvent::Quit, que primero cierra las ventanas: la principal se esconde en vez de cerrarse y la
    // salida se cancelaria. Se esconde antes para que la salida siga.
    if (watched == qApp && event->type() == QEvent::Quit && m_window) {
        m_window->hide();
    }
#endif
    if (event->type() == QEvent::FileOpen) {
        // mac: un .nk o un link que llega a la app residente.
        auto *open = static_cast<QFileOpenEvent *>(event);
        if (openExternal(open->url().isLocalFile() ? open->file() : open->url().toString())) {
            return true;
        }
    }
    return QObject::eventFilter(watched, event);
}

bool AppController::openExternal(const QString &argument)
{
    // Se despacha al modulo prendido, o al paso directo del descriptor si esta apagado (D-05). La app
    // nunca sale por esto.
    const ModuleDescriptor *d = ExternalDispatch::claimant(m_host->descriptors(), argument);
    if (!d) {
        return false;
    }
    ExternalRequest request;
    request.argument = argument;
    request.resident = true;
    request.moduleEnabled = m_host->isRunning(d->id);
    request.value = m_host->reader(d->id);
    request.finished = [](int) {};
    if (Module *module = m_host->module(d->id)) {
        module->handleExternal(request);
    } else {
        d->runExternal(request);
    }
    return true;
}

void AppController::refreshDockIcon()
{
    bool anyOpen = false;
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (widget->isVisible() && !widget->testAttribute(Qt::WA_DontShowOnScreen)
            && (widget->windowType() == Qt::Window || widget->windowType() == Qt::Dialog)) {
            anyOpen = true;
            break;
        }
    }
    WindowActivation::setDockIconVisible(anyOpen);
}

void AppController::quit()
{
    qApp->quit();
}
