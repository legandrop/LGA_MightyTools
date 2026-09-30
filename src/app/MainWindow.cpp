#include "app/MainWindow.h"

#include "app/ModuleHost.h"
#include "app/SettingsStore.h"
#include "app/WindowParts.h"
#include "core/I18n.h"
#include "platform/WindowFrame.h"
#include "ui/Theme.h"
#include "ui/TitleBar.h"
#include "ui/UiWidgets.h"

#include <QCloseEvent>
#include <QDebug>
#include <QEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QScreen>
#include <QScrollArea>
#include <QShowEvent>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {

constexpr int kSidebarWidth = 224;

QString toneName(ModuleTone tone)
{
    switch (tone) {
    case ModuleTone::Active:
        return QStringLiteral("ok");
    case ModuleTone::Attention:
        return QStringLiteral("warn");
    case ModuleTone::Error:
        return QStringLiteral("err");
    case ModuleTone::Paused:
    case ModuleTone::Off:
        break;
    }
    return QString();
}

} // namespace

MainWindow::MainWindow(ModuleHost *host, Mode mode, SettingsStore *appStore, QWidget *parent)
    : QMainWindow(parent)
    , m_host(host)
    , m_mode(mode)
    , m_appStore(appStore)
{
    setWindowTitle(QStringLiteral("LGA Mighty Tools"));
    // La barra de titulo la dibuja la app (TitleBar). En Windows, applyNativeFrame() le devuelve a
    // la ventana la sombra y las esquinas del sistema.
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    buildUi();
    setFixedSize(kWidth, kHeight);

    connect(m_host, &ModuleHost::moduleToggled, this, &MainWindow::onModuleToggled);
    connect(m_host, &ModuleHost::moduleStatusChanged, this, &MainWindow::refreshItem);
    selectPage(kGeneral);
}

MainWindow::~MainWindow()
{
    if (isVisible()) {
        savePosition();
    }
}

QScrollArea *MainWindow::makePage(QWidget **content, QVBoxLayout **layout)
{
    // `.pane`: 14 / 16 / 18 de relleno y 10 entre bloques; scroll vertical si no entra.
    auto *scroll = new QScrollArea(m_stack);
    scroll->setObjectName(QStringLiteral("pane"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *inner = new QWidget(scroll);
    inner->setObjectName(QStringLiteral("paneContent"));
    auto *column = new QVBoxLayout(inner);
    column->setContentsMargins(16, 14, 16, 18);
    column->setSpacing(10);
    scroll->setWidget(inner);
    m_stack->addWidget(scroll);
    *content = inner;
    *layout = column;
    return scroll;
}

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("central"));
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_titleBar = new TitleBar(central);
    root->addWidget(m_titleBar);
    connect(m_titleBar, &TitleBar::helpClicked, this, &MainWindow::helpRequested);

    auto *shell = new QHBoxLayout();
    shell->setContentsMargins(0, 0, 0, 0);
    shell->setSpacing(0);
    root->addLayout(shell, 1);

    // ---------- Barra lateral (`.side`): 224 px, 10 x 8 de relleno, 2 entre filas.
    auto *sidebar = new QFrame(central);
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setFixedWidth(kSidebarWidth);
    auto *side = new QVBoxLayout(sidebar);
    side->setContentsMargins(8, 10, 8, 10);
    side->setSpacing(2);
    m_generalItem = new SidebarItem(
        kGeneral, QStringLiteral("General"),
        [](QPainter &p, const QRectF &r, const QColor &c) { Icons::paint(p, Icon::General, r, c); }, false, sidebar);
    side->addWidget(m_generalItem);
    connect(m_generalItem, &SidebarItem::picked, this, &MainWindow::selectPage);

    QLabel *toolsLabel = Ui::label(QStringLiteral("TOOLS"), "sideLabel", sidebar);
    toolsLabel->setContentsMargins(8, 10, 8, 6);
    toolsLabel->ensurePolished();
    QFont spaced = toolsLabel->font();
    spaced.setLetterSpacing(QFont::AbsoluteSpacing, 0.66); // .06em a 11 px
    toolsLabel->setFont(spaced);
    side->addWidget(toolsLabel);

    m_stack = new QStackedWidget(central);
    m_stack->setObjectName(QStringLiteral("pages"));

    // ---------- Pagina General
    {
        QWidget *content = nullptr;
        QVBoxLayout *column = nullptr;
        m_generalScroll = makePage(&content, &column);
        m_general = new GeneralPage(m_host, content);
        column->addWidget(m_general);
        column->addStretch(1);
        connect(m_general, &GeneralPage::helpRequested, this, &MainWindow::helpRequested);
        connect(m_general, &GeneralPage::toolToggleRequested, this, &MainWindow::toggleRequested);
    }

    // ---------- Una fila y una pagina por herramienta
    for (const ModuleDescriptor &d : m_host->descriptors()) {
        auto *item = new SidebarItem(d.id, d.title, d.paintIcon, true, sidebar);
        side->addWidget(item);
        m_items.insert(d.id, item);
        connect(item, &SidebarItem::picked, this, &MainWindow::selectPage);
        connect(item, &SidebarItem::toggleRequested, this, [this](const QString &id, bool on) {
            // Prender o apagar desde la fila tambien la elige, como en el canvas.
            selectPage(id);
            emit toggleRequested(id, on);
        });

        ToolPage page;
        QWidget *content = nullptr;
        QVBoxLayout *column = nullptr;
        page.scroll = makePage(&content, &column);
        page.header = new ModuleHeader(d.title, platformsText(d.platforms), d.description, d.paintIcon, true, content);
        column->addWidget(page.header);
        const QString id = d.id;
        connect(page.header, &ModuleHeader::toggleRequested, this, [this, id](bool on) { emit toggleRequested(id, on); });
        page.body = new QWidget(content);
        page.body->setObjectName(QStringLiteral("moduleBody"));
        page.bodyLayout = new QVBoxLayout(page.body);
        page.bodyLayout->setContentsMargins(0, 0, 0, 0);
        page.bodyLayout->setSpacing(10);
        column->addWidget(page.body);
        column->addStretch(1);
        m_pages.insert(d.id, page);
        refreshItem(d.id);
    }
    side->addStretch(1);
    shell->addWidget(sidebar);
    shell->addWidget(m_stack, 1);
    refreshGeneralItem();

    setCentralWidget(central);
}

void MainWindow::rebuildUi()
{
    const QString current = m_current.isEmpty() ? kGeneral : m_current;
    const bool firstRun = m_general && m_general->firstRun();
    if (QWidget *focused = focusWidget()) {
        focused->clearFocus();
    }
    // En el acto y no con deleteLater: el host guarda cada panel en un QPointer, y recien en nulo
    // selectPage() le pide uno nuevo, armado en el idioma nuevo.
    m_items.clear();
    m_pages.clear();
    m_generalItem = nullptr;
    m_general = nullptr;
    m_generalScroll = nullptr;
    m_stack = nullptr;
    m_titleBar = nullptr;
    delete takeCentralWidget();
    buildUi();
    m_general->setFirstRun(firstRun);
    m_general->setUpdateState(m_updateState);
    selectPage(current);
    emit rebuilt();
}

void MainWindow::selectPage(const QString &id)
{
    const QString target = (id == kGeneral || m_pages.contains(id)) ? id : kGeneral;
    m_current = target;
    m_generalItem->setSelected(target == kGeneral);
    for (auto it = m_items.constBegin(); it != m_items.constEnd(); ++it) {
        it.value()->setSelected(it.key() == target);
    }
    if (target == kGeneral) {
        m_stack->setCurrentWidget(m_generalScroll);
        return;
    }
    syncBody(target);
    // Elegir una herramienta en la barra lateral tambien relee su estado.
    refreshItem(target);
    m_stack->setCurrentWidget(m_pages.value(target).scroll);
}

void MainWindow::syncBody(const QString &id)
{
    auto it = m_pages.find(id);
    if (it == m_pages.end()) {
        return;
    }
    ToolPage &page = it.value();
    const bool running = m_host->isRunning(id);
    page.header->setOn(running);
    if (running) {
        if (page.offPanel) {
            page.offPanel->hide();
            page.offPanel->deleteLater();
            page.offPanel = nullptr;
        }
        // El panel se construye recien cuando se ve la pagina (regla de consumo).
        if (!page.panel && m_current == id) {
            page.panel = m_host->panel(id, page.body);
            if (page.panel) {
                page.bodyLayout->addWidget(page.panel);
                page.panel->show();
            }
        }
        return;
    }
    // Apagada: el host ya pidio borrar el panel; aca se dibuja el de apagado.
    page.panel = nullptr;
    if (!page.offPanel) {
        const ModuleDescriptor *d = m_host->descriptor(id);
        ModuleOffNotice notice;
        if (d->offNotice) {
            notice = d->offNotice(m_offNoticeStates.value(id, m_mode == Mode::Capture ? QStringLiteral("none") : QString()));
        }
        // Las vinetas con datos vivos (un atajo configurado) se piden ahora: no son las del descriptor estatico.
        ModuleDescriptor shown = *d;
        if (d->offBulletsFor) {
            shown.offBullets = d->offBulletsFor(m_host->reader(id));
        }
        auto *off = new OffPanel(shown, notice, page.body);
        connect(off, &OffPanel::turnOnRequested, this, [this, id]() { emit toggleRequested(id, true); });
        connect(off, &OffPanel::releaseRequested, this, [this, id]() { emit releaseRequested(id); });
        page.bodyLayout->addWidget(off);
        off->show();
        page.offPanel = off;
    }
}

void MainWindow::setOffNoticeCaptureState(const QString &id, const QString &state)
{
    m_offNoticeStates.insert(id, state);
    auto it = m_pages.find(id);
    if (it != m_pages.end() && it->offPanel) {
        it->offPanel->hide();
        delete it->offPanel.data();
        syncBody(id);
    }
}

void MainWindow::refreshOffNotices()
{
    // El sistema puede haber cambiado por fuera (otra copia, Ajustes de Windows): se vuelve a leer al
    // abrir la ventana.
    for (auto it = m_pages.begin(); it != m_pages.end(); ++it) {
        if (it->offPanel) {
            it->offPanel->hide();
            delete it->offPanel.data();
            syncBody(it.key());
        }
    }
}

void MainWindow::onModuleToggled(const QString &id, bool)
{
    refreshItem(id);
    syncBody(id);
    m_general->refreshTools();
}

void MainWindow::refreshItem(const QString &id)
{
    SidebarItem *item = m_items.value(id);
    if (!item) {
        return;
    }
    const bool running = m_host->isRunning(id);
    const ModuleStatus status = m_host->status(id);
    item->setState(running, running ? status.text : I18n::trc("tool", "Off"), running ? toneName(status.tone) : QString());
}

void MainWindow::refreshGeneralItem()
{
    QString text = QStringLiteral("v" MIGHTYTOOLS_VERSION);
    QString tone;
    if (m_updateState.kind == UpdateRowState::Kind::Latest) {
        text = I18n::tr("v%1 · up to date").arg(QStringLiteral(MIGHTYTOOLS_VERSION));
    } else if (m_updateState.kind == UpdateRowState::Kind::Available) {
        text = I18n::tr("v%1 is available").arg(m_updateState.version);
        tone = QStringLiteral("warn");
    }
    m_generalItem->setState(true, text, tone);
}

void MainWindow::setFirstRun(bool firstRun)
{
    m_general->setFirstRun(firstRun);
}

void MainWindow::setUpdateState(const UpdateRowState &state)
{
    m_updateState = state;
    m_general->setUpdateState(state);
    refreshGeneralItem();
}

void MainWindow::restorePosition()
{
    if (m_positionRestored || m_mode != Mode::Normal || !m_appStore) {
        return;
    }
    m_positionRestored = true;
    bool okX = false;
    bool okY = false;
    const int x = m_appStore->value(QStringLiteral("window/x")).toInt(&okX);
    const int y = m_appStore->value(QStringLiteral("window/y")).toInt(&okY);
    // Solo si la barra de titulo cae en alguna pantalla de hoy: un monitor desenchufado no deja la
    // ventana afuera.
    if (okX && okY && QGuiApplication::screenAt(QPoint(x + 60, y + 18))) {
        move(x, y);
        return;
    }
    if (const QScreen *screen = QGuiApplication::primaryScreen()) {
        const QRect area = screen->availableGeometry();
        move(area.center() - QPoint(kWidth / 2, kHeight / 2));
    }
}

void MainWindow::savePosition()
{
    if (m_mode != Mode::Normal || !m_appStore || !m_positionRestored) {
        return;
    }
    const QPoint position = pos();
    if (m_appStore->value(QStringLiteral("window/x")).toInt() == position.x()
        && m_appStore->value(QStringLiteral("window/y")).toInt() == position.y()) {
        return;
    }
    m_appStore->setValue(QStringLiteral("window/x"), position.x());
    m_appStore->setValue(QStringLiteral("window/y"), position.y());
}

void MainWindow::changeEvent(QEvent *event)
{
    // El campo que tiene el teclado (el umbral de un disco) se confirma y se suelta al perder el
    // frente: al volver, el cursor no queda titilando adentro. Los paneles cortan sus grabaciones
    // de atajos con su propio filtro.
    if (event->type() == QEvent::ActivationChange && !isActiveWindow()) {
        if (QWidget *focused = focusWidget()) {
            focused->clearFocus();
        }
    }
    // Al volver al frente se relee el estado de cada herramienta: el usuario puede venir de Ajustes de
    // Windows (desinstalo el cliente viejo, eligio el navegador por defecto). Los paneles se
    // refrescan con el mismo evento.
    if (event->type() == QEvent::ActivationChange && isActiveWindow() && m_mode == Mode::Normal) {
        for (auto it = m_items.constBegin(); it != m_items.constEnd(); ++it) {
            refreshItem(it.key());
        }
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::showEvent(QShowEvent *event)
{
    restorePosition();
    // La primera vez que se muestra ya existe la ventana nativa. Nunca en la captura: no hay ventana
    // real.
    if (!m_nativeFrameApplied && m_mode == Mode::Normal) {
        // La bandera va ANTES de apply(): su SetWindowPos(SWP_FRAMECHANGED) manda WM_NCCALCSIZE en el
        // acto, y si nativeEvent todavia no lo atiende Windows le suma la barra de titulo nativa
        // (la barra blanca arriba de TitleBar). Como Nuke Shortcuts.
        m_nativeFrameApplied = true;
        m_nativeFrameApplied = WindowFrame::apply(this);
    }
    if (m_mode == Mode::Normal) {
        refreshOffNotices();
    }
    QMainWindow::showEvent(event);
}

void MainWindow::hideEvent(QHideEvent *event)
{
    savePosition();
    QMainWindow::hideEvent(event);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // No se cierra la app: se oculta a la bandeja. Salir solo desde "Quit" del menu.
    hide();
    event->ignore();
}

bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
    if (m_nativeFrameApplied && WindowFrame::handleNativeEvent(message, result)) {
        return true;
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}
