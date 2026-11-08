#include "app/MainWindow.h"

#include "app/ModuleHost.h"
#include "app/ModuleRegistry.h"
#include "app/SettingsStore.h"
#include "app/WindowParts.h"
#include "core/I18n.h"
#include "platform/WindowFrame.h"
#include "ui/CustomTooltip.h"
#include "ui/Theme.h"
#include "ui/TitleBar.h"
#include "ui/UiWidgets.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QCloseEvent>
#include <QDebug>
#include <QEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QScreen>
#include <QScrollArea>
#include <QShortcut>
#include <QShowEvent>
#include <QStackedWidget>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

namespace {

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

// Hasta v1.52 la franja de ayuda de cada herramienta se recordaba abierta o cerrada aca (una clave por
// herramienta). Ya no se guarda: se borra lo que haya quedado.
const QString kOldHelpGroup = QStringLiteral("app/howItWorks");

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
    WindowFrame::prepare(this); // mac: translucida, para las esquinas redondeadas
    buildUi();
    setFixedSize(kWidth, kHeight);

    m_paletteTimer = new QTimer(this);
    m_paletteTimer->setSingleShot(true);
    m_paletteTimer->setInterval(30);
    connect(m_paletteTimer, &QTimer::timeout, this, &MainWindow::applyPaletteChange);
    connect(Palette::Notifier::instance(), &Palette::Notifier::changed, this, &MainWindow::schedulePaletteRebuild);

    connect(m_host, &ModuleHost::moduleToggled, this, &MainWindow::onModuleToggled);
    connect(m_host, &ModuleHost::moduleStatusChanged, this, &MainWindow::refreshItem);
    // La franja de la pagina que se ve sigue a la herramienta: un atajo cambiado o un estado nuevo rearman sus
    // pasos. Solo la pagina actual (las otras se rearman al elegirlas).
    const auto followHelp = [this](const QString &id) {
        if (id == m_current) {
            refreshHelpText(id);
        }
    };
    connect(m_host, &ModuleHost::moduleStatusChanged, this, followHelp);
    connect(m_host, &ModuleHost::moduleSettingsChanged, this, followHelp);
    // Ctrl+Alt+← → (⌘⌥ en mac: en Qt/mac Control es la tecla Cmd) pasan de una pagina a la otra en el orden del
    // indice, como en DayTrace. Atajo de la ventana: tambien con el foco en un campo, que no usa esas teclas.
    for (const int direction : {-1, 1}) {
        const Qt::Key arrow = direction < 0 ? Qt::Key_Left : Qt::Key_Right;
        auto *shortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::ALT | arrow), this);
        shortcut->setContext(Qt::WindowShortcut);
        shortcut->setAutoRepeat(false);
        connect(shortcut, &QShortcut::activated, this, [this, direction, arrow]() {
            // Un ShortcutRow grabando toma el teclado con grabKeyboard(), que el mapa de atajos de Qt no mira: la
            // combinacion es para el, que la puede querer como atajo. Se le entrega con event() y no con sendEvent():
            // QApplication::notify vuelve a probar los atajos con cada KeyPress y este se dispararia sin fin.
            if (QObject *grabber = QWidget::keyboardGrabber()) {
                CustomTooltip::instance()->hideToolTip(); // event() saltea el filtro que lo cierra con cada tecla
                QKeyEvent press(QEvent::KeyPress, arrow, Qt::ControlModifier | Qt::AltModifier);
                grabber->event(&press);
                return;
            }
            stepPage(direction);
        });
    }
    if (m_mode == Mode::Normal && m_appStore) {
        // Solo lecturas si no quedo nada: el .ini se reescribe una sola vez.
        for (const ModuleDescriptor &d : m_host->descriptors()) {
            if (m_appStore->value(kOldHelpGroup + QLatin1Char('/') + d.id).isValid()) {
                m_appStore->remove(kOldHelpGroup);
                break;
            }
        }
    }
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
    // `.pg` del Pliego: 24 arriba y abajo, 28 a los lados, 12 entre bloques; scroll vertical si no entra.
    auto *scroll = new QScrollArea(m_stack);
    scroll->setObjectName(QStringLiteral("pane"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(false);
    auto *inner = new QWidget(scroll);
    inner->setObjectName(QStringLiteral("paneContent"));
    auto *column = new QVBoxLayout(inner);
    column->setContentsMargins(28, 24, 28, 24);
    column->setSpacing(12);
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
    connect(m_titleBar, &TitleBar::helpClicked, this, [this]() { setHelpOpen(!helpOpen()); });

    // La mesa: el indice a la izquierda y la hoja (las paginas) a la derecha. NavShell pinta la hoja y la
    // ficha elegida; el indice y las paginas son transparentes encima.
    m_shell = new NavShell(central);
    root->addWidget(m_shell, 1);
    auto *shell = new QHBoxLayout(m_shell);
    shell->setContentsMargins(0, 0, 0, 0);
    shell->setSpacing(0);

    // ---------- Indice (`.nav`): General arriba, a la altura del borde de la hoja (ver NavShell::kSheetTop),
    // el rotulo TOOLS y una fila por herramienta.
    auto *sidebar = new QWidget(m_shell);
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setFixedWidth(NavShell::kNavWidth);
    auto *side = new QVBoxLayout(sidebar);
    side->setContentsMargins(0, NavShell::kSheetTop, 0, 8);
    side->setSpacing(0);
    m_generalItem = new SidebarItem(
        kGeneral, QStringLiteral("General"),
        [](QPainter &p, const QRectF &r, const QColor &c) { Icons::paint(p, Icon::General, r, c); }, false, sidebar);
    side->addWidget(m_generalItem);
    connect(m_generalItem, &SidebarItem::picked, this, &MainWindow::selectPage);

    // `.nlabel`: 30 de alto, el texto 12 abajo del borde y alineado con los iconos de las fichas.
    QLabel *toolsLabel = Ui::label(QStringLiteral("TOOLS"), "sideLabel", sidebar);
    toolsLabel->setFixedHeight(30);
    toolsLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    toolsLabel->setContentsMargins(SidebarItem::kTabInset + 12, 11, 0, 0);
    toolsLabel->ensurePolished();
    QFont spaced = toolsLabel->font();
    spaced.setLetterSpacing(QFont::AbsoluteSpacing, 1.1); // .1em a 11 px
    toolsLabel->setFont(spaced);
    side->addWidget(toolsLabel);

    auto *sheet = new QVBoxLayout();
    sheet->setContentsMargins(0, NavShell::kSheetTop, NavShell::kSheetMargin, NavShell::kSheetMargin); // sheetRect()
    m_stack = new QStackedWidget(m_shell);
    m_stack->setObjectName(QStringLiteral("pages"));
    sheet->addWidget(m_stack);

    // ---------- Pagina General
    {
        QWidget *content = nullptr;
        QVBoxLayout *column = nullptr;
        m_generalScroll = makePage(&content, &column);
        m_general = new GeneralPage(m_host, content);
        column->addWidget(m_general, 1);
        connect(m_general, &GeneralPage::toolToggleRequested, this, &MainWindow::toggleRequested);
    }

    // ---------- Una fila y una pagina por herramienta (el registro ya viene en orden alfabetico).
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
        page.header = new ModuleHeader(d.title, platformsText(d.platforms), d.description, true, content);
        column->addWidget(page.header);
        const QString id = d.id;
        connect(page.header, &ModuleHeader::toggleRequested, this, [this, id](bool on) { emit toggleRequested(id, on); });
        // `.how`: 14 debajo del encabezado (12 del layout + 2).
        column->addSpacing(2);
        page.help = new HowItWorksStrip(content);
        column->addWidget(page.help);
        connect(page.help, &HowItWorksStrip::toggledByUser, this, [this]() {
            refreshHelpButton();
            relayoutCurrentPage();
        });
        page.body = new QWidget(content);
        page.body->setObjectName(QStringLiteral("moduleBody"));
        page.bodyLayout = new QVBoxLayout(page.body);
        page.bodyLayout->setContentsMargins(0, 0, 0, 0);
        page.bodyLayout->setSpacing(12);
        column->addWidget(page.body);
        column->addStretch(1);
        m_pages.insert(d.id, page);
        refreshItem(d.id);
    }
    side->addStretch(1);
    shell->addWidget(sidebar);
    shell->addLayout(sheet, 1);
    refreshGeneralItem();

    setCentralWidget(central);
}

void MainWindow::rebuildUi()
{
    ++m_rebuildSerial;
    const QString current = m_current.isEmpty() ? kGeneral : m_current;
    const bool firstRun = m_general && m_general->firstRun();
    // Rearmar no es entrar a la pagina: la ayuda queda como estaba.
    const bool helpWasOpen = helpOpen();
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
    m_shell = nullptr;
    m_titleBar = nullptr;
    delete takeCentralWidget();
    buildUi();
    m_general->setFirstRun(firstRun);
    m_general->setUpdateState(m_updateState);
    selectPage(current);
    if (helpWasOpen) {
        setHelpOpen(true);
    }
    emit rebuilt();
}

void MainWindow::selectPage(const QString &id)
{
    const QString target = (id == kGeneral || m_pages.contains(id)) ? id : kGeneral;
    // Volver a elegir la pagina que ya se ve (un click en su fila, su switch del indice) no es entrar.
    const bool entering = target != m_current;
    m_current = target;
    m_generalItem->setSelected(target == kGeneral);
    for (auto it = m_items.constBegin(); it != m_items.constEnd(); ++it) {
        it.value()->setSelected(it.key() == target);
    }
    m_shell->setSelectedItem(target == kGeneral ? m_generalItem : m_items.value(target));
    if (target == kGeneral) {
        m_stack->setCurrentWidget(m_generalScroll);
        refreshHelpButton();
        relayoutCurrentPage();
        return;
    }
    syncBody(target);
    syncHelp(target, entering);
    // Elegir una herramienta en la barra lateral tambien relee su estado.
    refreshItem(target);
    m_stack->setCurrentWidget(m_pages.value(target).scroll);
    refreshHelpButton();
    relayoutCurrentPage();
}

void MainWindow::refreshHelpText(const QString &id)
{
    auto it = m_pages.find(id);
    if (it == m_pages.end()) {
        return;
    }
    const ModuleDescriptor *d = m_host->descriptor(id);
    const auto providers = ModuleRegistry::helpProviders();
    // Los pasos muestran los atajos configurados: se leen de settings cada vez y la franja se rearma solo
    // si el texto cambio.
    const HelpSection section = providers.contains(id) ? providers.value(id)(m_host->reader(id))
                                                       : HelpSection{d ? d->title : id, {}, QString()};
    const QString text = section.steps.join(QChar(0x1F)) + QChar(0x1E) + section.note;
    if (text == it->helpText) {
        return;
    }
    it->helpText = text;
    it->help->setSection(section);
    it->help->setVisible(!section.steps.isEmpty());
    if (id == m_current) {
        refreshHelpButton();
        relayoutCurrentPage();
    }
}

void MainWindow::syncHelp(const QString &id, bool entering)
{
    auto it = m_pages.find(id);
    if (it == m_pages.end()) {
        return;
    }
    refreshHelpText(id);
    // Cada vez que se entra a una herramienta la franja esta cerrada (pedido de Lega): quien la quiere ver
    // la abre con un click o con el «?». No se recuerda.
    if (entering) {
        it->help->setOpen(false);
    }
}

bool MainWindow::helpOpen() const
{
    const auto it = m_pages.constFind(m_current);
    return it != m_pages.constEnd() && !it->help->isHidden() && it->help->isOpen();
}

void MainWindow::setHelpOpen(bool open)
{
    auto it = m_pages.find(m_current);
    if (it == m_pages.end() || it->help->stepCount() == 0) {
        return;
    }
    it->help->setOpen(open);
    refreshHelpButton();
    relayoutCurrentPage();
}

void MainWindow::refreshHelpButton()
{
    // El «?» abre o cierra la franja de la pagina que se ve. En General no hay franja: queda apagado.
    const auto it = m_pages.constFind(m_current);
    const bool available = it != m_pages.constEnd() && it->help->stepCount() > 0;
    m_titleBar->setHelpEnabled(available);
    m_titleBar->setHelpActive(available && it->help->isOpen());
}

void MainWindow::relayoutCurrentPage()
{
    // El panel se arma con la pagina todavia oculta, y al mostrarla Qt no vuelve a repartir el alto de
    // los textos de varias lineas: la pagina los aplastaba en vez de mostrar la barra (Open in NukeX en
    // espanol con el tamano 1). Invalidar todos sus layouts y pedirle a la QScrollArea que recalcule
    // reparte de nuevo con heightForWidth. Lo controla `--ui-probe page-fit`.
    auto *scroll = m_stack ? qobject_cast<QScrollArea *>(m_stack->currentWidget()) : nullptr;
    if (!scroll || !scroll->widget()) {
        return;
    }
    for (QLayout *layout : scroll->widget()->findChildren<QLayout *>()) {
        layout->invalidate();
    }
    QCoreApplication::postEvent(scroll, new QEvent(QEvent::LayoutRequest));
    if (m_shell) {
        m_shell->update();
    }
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
                // El estado vivo del panel («Shortcuts are on» + Pause) sube al encabezado, arriba de la ayuda.
                for (StatusCard *card : page.panel->findChildren<StatusCard *>()) {
                    if (card->isLive()) {
                        page.header->adoptLiveStatus(card, page.panel);
                    }
                }
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
    // Prender una herramienta con su pagina a la vista cambia el panel de apagado por el real.
    if (id == m_current) {
        relayoutCurrentPage();
    }
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
    if (okX && okY) {
        if (const QScreen *screen = QGuiApplication::screenAt(QPoint(x + 60, y + 18))) {
            // Entera adentro de esa pantalla: con otro tamano de interfaz (core/UiScale.h) la misma
            // posicion logica puede dejar la ventana, mas grande, saliendo por un borde.
            const QRect area = screen->availableGeometry();
            move(qBound(area.left(), x, qMax(area.left(), area.right() + 1 - kWidth)),
                 qBound(area.top(), y, qMax(area.top(), area.bottom() + 1 - kHeight)));
            return;
        }
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
    // La pagina pudo armarse con la ventana oculta en la bandeja.
    relayoutCurrentPage();
}

void MainWindow::hideEvent(QHideEvent *event)
{
    savePosition();
    QMainWindow::hideEvent(event);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // No se cierra la app: se oculta a la bandeja. Salir solo desde "Quit" del menu (mac: el boton Quit de General, D-39).
    hide();
    event->ignore();
}

// Escape cierra la ventana y la app vuelve a la bandeja (Windows) o a la barra de menu (mac), por el mismo camino
// que la cruz (closeEvent), como en DayTrace. Llega aca solo el Escape que nadie de adentro uso: un campo que
// descarta lo escrito, un atajo grabandose o un menu abierto se lo quedan. Si el foco esta en un campo que no lo
// usa, el primer Escape solo lo suelta y el segundo cierra.
void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape && event->modifiers() == Qt::NoModifier) {
        // Escape mantenido: un campo se queda con la primera pulsacion (descarta y suelta el foco) y las repeticiones
        // llegarian aca sin foco; no cierran.
        if (event->isAutoRepeat()) {
            event->accept();
            return;
        }
        QWidget *focus = QApplication::focusWidget();
        if (focus && isAncestorOf(focus)
            && (qobject_cast<QLineEdit *>(focus) || qobject_cast<QTextEdit *>(focus)
                || qobject_cast<QPlainTextEdit *>(focus) || qobject_cast<QAbstractSpinBox *>(focus))) {
            focus->clearFocus();
        } else {
            close();
        }
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::stepPage(int direction)
{
    QStringList order{kGeneral};
    for (const ModuleDescriptor &d : m_host->descriptors()) {
        if (m_pages.contains(d.id)) {
            order << d.id;
        }
    }
    const int at = qMax(0, order.indexOf(m_current));
    selectPage(order.at((at + direction + order.size()) % order.size()));
}

bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
    if (m_nativeFrameApplied && WindowFrame::handleNativeEvent(message, result)) {
        return true;
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}

void MainWindow::schedulePaletteRebuild()
{
    m_paletteTimer->start();
}

void MainWindow::applyPaletteChange()
{
    Theme::refresh(*qApp);
    rebuildWhenNoPopup();
}

void MainWindow::rebuildWhenNoPopup()
{
    // Un menu abierto (el de Colors con las flechas, o cualquier otro) puede ser hijo de lo que se rearma: el
    // rearmado espera a que se cierre. El de Colors lo vuelve a pedir al cerrarse; este reintento cubre los demas
    // (un cambio del editor de desarrollo con un menu abierto).
    if (QApplication::activePopupWidget()) {
        if (!m_paletteRebuildWaiting) {
            m_paletteRebuildWaiting = true;
            QTimer::singleShot(250, this, [this, serial = m_rebuildSerial]() {
                m_paletteRebuildWaiting = false;
                // Si mientras tanto alguien ya la rearmo (el menu de Colors al cerrarse), no hace falta otra vez.
                if (serial == m_rebuildSerial) {
                    rebuildWhenNoPopup();
                }
            });
        }
        return;
    }
    rebuildUi();
}
