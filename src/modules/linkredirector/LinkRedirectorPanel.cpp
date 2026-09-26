#include "modules/linkredirector/LinkRedirectorPanel.h"
#include "ui/Theme.h"
#include "ui/UiWidgets.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

// Campo "combo" (canvas: `.combo`, medido contra su CSS: height 28px, border-radius 3px, padding
// 0 8px 0 10px, color del valor @textStrong). Reusa el marco de un campo de solo lectura (QFrame#field,
// Theme.cpp: mismo alto y radio) para no tocar la hoja de estilo compartida, pero corrige el color del
// valor por instancia: ElidedLabel#fieldValue esta pensado para un campo de solo lectura atenuado
// (@textCaption) y el combo del canvas pide @textStrong, mas brillante.
class LinkRedirectorComboField : public QFrame
{
    Q_OBJECT

public:
    explicit LinkRedirectorComboField(QWidget *parent = nullptr) : QFrame(parent)
    {
        setObjectName(QStringLiteral("field"));
        setCursor(Qt::PointingHandCursor);
        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(10, 0, 8, 0); // canvas .combo: padding 0 8px 0 10px
        row->setSpacing(8);                   // canvas .combo: gap 8px
        m_value = new ElidedLabel(this);
        m_value->setObjectName(QStringLiteral("fieldValue"));
        row->addWidget(m_value, 1);
        m_chevron = new IconWidget(Icon::ChevronDown, Theme::color(Theme::kTextMuted), 8, this);
        row->addWidget(m_chevron, 0, Qt::AlignVCenter);
    }

    void setValueText(const QString &text, bool empty)
    {
        m_value->setText(text);
        // canvas .combo .v: color @textStrong (no @textCaption, mas atenuado, del campo de solo
        // lectura del que sale este widget); "-" cae al gris de placeholder, como el resto de la app.
        m_value->setStyleSheet(QStringLiteral("color:%1;")
                                    .arg(empty ? QLatin1String(Theme::kTextPlaceholder) : QLatin1String(Theme::kTextStrong)));
    }

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            emit clicked();
            event->accept();
            return;
        }
        QFrame::mousePressEvent(event);
    }

private:
    ElidedLabel *m_value = nullptr;
    IconWidget *m_chevron = nullptr;
};

// Editor de Match words con el fondo rayado del canvas (`.textarea`, medido con getComputedStyle):
// "background-image: repeating-linear-gradient(transparent 0 20px, #222 20px 21px); background-
// position: 0 6px" -- un renglon de 1 px cada 21 px (20 px transparentes + 1 px de linea), corrido
// 6 px hacia abajo (el padding-top del campo). QSS no tiene repeating-linear-gradient, asi que se
// pinta a mano en el viewport, ANTES de que QPlainTextEdit dibuje el texto encima.
//
// El periodo real que se usa es QFontMetrics::lineSpacing() de la fuente del editor, no el 21 px fijo
// del canvas: asi el renglon queda SIEMPRE debajo de cada linea de texto de este widget (el pedido
// del encargo), aunque la metrica exacta de Inter en Qt/Fusion no sea identica a la del navegador
// donde se midio el canvas. offset = 6 px, igual al padding-top del QSS de mas abajo.
class LinkRedirectorMatchWordsEdit : public QPlainTextEdit
{
public:
    using QPlainTextEdit::QPlainTextEdit;

protected:
    void paintEvent(QPaintEvent *event) override
    {
        {
            QPainter painter(viewport());
            painter.setRenderHint(QPainter::Antialiasing, false);
            painter.setPen(Theme::color(Theme::kTextareaRuleLine));
            const int period = qMax(1, QFontMetrics(font()).lineSpacing());
            constexpr int kOffsetTop = 6; // canvas: background-position 0 6px (= padding-top)
            const int width = viewport()->width();
            const int height = viewport()->height();
            // El primer renglon cae al final del primer periodo (el tramo #222 es 20-21px del
            // patron, es decir el ULTIMO pixel de cada periodo), corrido por el offset.
            for (int y = kOffsetTop + period - 1; y < height; y += period) {
                painter.drawLine(0, y, width, y);
            }
        }
        QPlainTextEdit::paintEvent(event);
    }
};

using LinkRedirectorRouting::ComboItem;

LinkRedirectorPanel::LinkRedirectorPanel(ModuleContext &context, LinkRedirectorPanelSources sources, QWidget *parent)
    : QWidget(parent)
    , m_context(context)
    , m_sources(std::move(sources))
{
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(10);

    // ---- Tarjeta de estado (canvas, seccion 2: "routing" / "Make Default").
    m_statusCard = new StatusCard(this);
    column->addWidget(m_statusCard);
    connect(m_statusCard->button(), &QPushButton::clicked, this, [this]() {
        // Guardar cual era el default del sistema ANTES de pedir el cambio, para sincronizarlo como
        // "Default browser" de la herramienta si el pedido tiene exito (mainwindow.cpp:1562-1601 del
        // origen, LinkRedirectorRouting::browserToSyncAsDefault).
        m_pendingPreviousDefaultExe = m_sources.exePathForHandlerId && m_sources.currentDefaultHandlerId
            ? m_sources.exePathForHandlerId(m_sources.currentDefaultHandlerId())
            : QString();
        if (m_sources.requestSetAsDefault) {
            m_sources.requestSetAsDefault();
        }
        m_makeDefaultRequested = true;
        refreshStatus();
        // El cambio puede requerir una eleccion manual (Windows) o ser asincrono (mac): dos
        // reintentos, como el origen.
        QTimer::singleShot(2500, this, &LinkRedirectorPanel::refreshStatus);
        QTimer::singleShot(6000, this, &LinkRedirectorPanel::refreshStatus);
    });

    // ---- Tarjeta "Browsers": dos combos lado a lado.
    QFrame *browsersCard = Ui::card(this);
    auto *browsersLayout = new QVBoxLayout(browsersCard);
    browsersLayout->setContentsMargins(14, 12, 14, 12);
    browsersLayout->setSpacing(0);
    QLabel *browsersTitle = Ui::label(QStringLiteral("Browsers"), "cardTitle", browsersCard);
    browsersTitle->setMinimumHeight(22); // canvas .head: min-height 22px
    browsersLayout->addWidget(browsersTitle);
    browsersLayout->addSpacing(8);

    auto *grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(4);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);

    auto addBrowserColumn = [&](int col, const QString &label, const QString &caption, const QString &settingsKey,
                                LinkRedirectorComboField *&outField) {
        auto *box = new QVBoxLayout();
        box->setContentsMargins(0, 0, 0, 0);
        box->setSpacing(4);
        QLabel *rowLabel = Ui::label(label, "optionLabel", browsersCard);
        rowLabel->setMinimumHeight(22); // canvas .row: min-height 22px
        box->addWidget(rowLabel);
        box->addWidget(Ui::caption(caption, browsersCard));
        outField = buildBrowserField(settingsKey, browsersCard);
        box->addWidget(outField);
        grid->addLayout(box, 0, col);
    };
    addBrowserColumn(0, QStringLiteral("Default browser"), QStringLiteral("For non-matching links"),
                     QStringLiteral("defaultBrowser"), m_defaultField);
    addBrowserColumn(1, QStringLiteral("Alternative browser"), QStringLiteral("For matching links"),
                     QStringLiteral("alternativeBrowser"), m_alternativeField);
    browsersLayout->addLayout(grid);
    column->addWidget(browsersCard);

    // ---- Tarjeta "Match words": autoguardado, una palabra por linea.
    QFrame *wordsCard = Ui::card(this);
    auto *wordsLayout = new QVBoxLayout(wordsCard);
    wordsLayout->setContentsMargins(14, 12, 14, 12);
    wordsLayout->setSpacing(0);
    auto *wordsHead = new QHBoxLayout();
    wordsHead->setContentsMargins(0, 0, 0, 0);
    wordsHead->setSpacing(6);
    QLabel *wordsTitle = Ui::label(QStringLiteral("Match words"), "cardTitle", wordsCard);
    wordsTitle->setMinimumHeight(22); // canvas .head: min-height 22px
    wordsHead->addWidget(wordsTitle, 1);
    auto *autosavedChip = new Chip(wordsCard);
    autosavedChip->set(QStringLiteral("src"), QStringLiteral("Autosaved"));
    wordsHead->addWidget(autosavedChip, 0, Qt::AlignVCenter);
    wordsLayout->addLayout(wordsHead);
    wordsLayout->addSpacing(8);
    QLabel *wordsCaption = Ui::caption(
        QStringLiteral("Links containing any of these open in the alternative browser. One keyword per line."),
        wordsCard);
    wordsCaption->setWordWrap(true);
    wordsCaption->setContentsMargins(0, 0, 0, 8);
    wordsLayout->addWidget(wordsCaption);

    m_matchWords = new LinkRedirectorMatchWordsEdit(wordsCard);
    m_matchWords->setObjectName(QStringLiteral("linkRedirectorMatchWords"));
    m_matchWords->setPlaceholderText(QStringLiteral("netflixstudios"));
    // Solo por click (regla de foco de la app): con StrongFocus (el default de QPlainTextEdit),
    // activar la ventana le daria el foco al primer campo que acepta Tab.
    m_matchWords->setFocusPolicy(Qt::ClickFocus);
    m_matchWords->setTabChangesFocus(false);
    m_matchWords->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_matchWords->setMinimumHeight(110); // canvas .textarea: min-height 110px
    // Estilo local (esta app no tiene un QPlainTextEdit compartido en Theme.cpp): mismos tokens que
    // un campo de settings (@field/@fieldBorder) y el mismo border-radius/padding que .textarea del
    // canvas. A proposito SIN "color:" aca: la paleta de la app ya distingue texto (kText) de
    // placeholder (kTextPlaceholder, mas apagado); fijar "color" por QSS pisa esa distincion y el
    // placeholder se ve igual de brillante que una palabra real.
    m_matchWords->setStyleSheet(QStringLiteral(
                                    "QPlainTextEdit#linkRedirectorMatchWords { background-color:%1; "
                                    "border:1px solid %2; border-radius:3px; padding:6px 8px; "
                                    "selection-background-color:#393455; selection-color:%3; } "
                                    "QPlainTextEdit#linkRedirectorMatchWords:focus { border-color:%4; }")
                                    .arg(QLatin1String(Theme::kField), QLatin1String(Theme::kFieldBorder),
                                         QLatin1String(Theme::kTextBright), QLatin1String(Theme::kAccent)));
    wordsLayout->addWidget(m_matchWords);
    column->addWidget(wordsCard);

    // DESVIO DELIBERADO de "Enter guarda" (pedido del encargo de la etapa 2): Match words es una
    // palabra por linea, asi que Enter tiene que insertar una linea nueva, no confirmar. Solo Escape
    // y un click afuera sueltan el foco (sin revertir nada: el autoguardado ya corrio).
    m_matchWords->installEventFilter(this);
    qApp->installEventFilter(this);

    m_autosaveTimer = new QTimer(this);
    m_autosaveTimer->setSingleShot(true);
    m_autosaveTimer->setInterval(500);
    connect(m_autosaveTimer, &QTimer::timeout, this, &LinkRedirectorPanel::saveMatchWordsNow);
    connect(m_matchWords, &QPlainTextEdit::textChanged, this, &LinkRedirectorPanel::scheduleAutosave);

    refreshMatchWordsFromSettings();
    refreshBrowserFields();
    refreshStatus();
}

LinkRedirectorPanel::~LinkRedirectorPanel()
{
    qApp->removeEventFilter(this);
}

void LinkRedirectorPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // "El sistema puede haber cambiado por fuera" (GeneralPage/MainWindow, el mismo criterio): al
    // mostrar el panel se recalcula si esta app sigue siendo el navegador por defecto.
    refreshStatus();
}

bool LinkRedirectorPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_matchWords && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            m_matchWords->clearFocus();
            return true;
        }
        // Return/Enter: NO se intercepta. Ver el comentario del constructor.
        return false;
    }
    if (event->type() == QEvent::MouseButtonPress && m_matchWords && m_matchWords->hasFocus()) {
        auto *target = qobject_cast<QWidget *>(watched);
        if (target && target != m_matchWords && !m_matchWords->isAncestorOf(target)) {
            m_matchWords->clearFocus();
        }
    }
    return QWidget::eventFilter(watched, event);
}

void LinkRedirectorPanel::refreshStatus()
{
    const bool isDefault = m_sources.isDefaultBrowser && m_sources.isDefaultBrowser();
    if (isDefault && m_makeDefaultRequested) {
        const QString synced = LinkRedirectorRouting::browserToSyncAsDefault(true, m_pendingPreviousDefaultExe);
        if (!synced.isEmpty()) {
            m_context.setValue(QStringLiteral("defaultBrowser"), synced);
            refreshBrowserFields();
        }
        m_pendingPreviousDefaultExe.clear();
        m_makeDefaultRequested = false;
    }
    if (isDefault) {
        m_statusCard->set(QStringLiteral("on"), QStringLiteral("Link Redirector is routing your links"),
                          QStringLiteral("Default browser is active · matching links use your alternative browser"),
                          QString(), QString(), QString());
    } else {
        m_statusCard->set(QStringLiteral("warn"), QStringLiteral("Make LGA Mighty Tools your default browser"),
                          QStringLiteral("Choose Make Default to start routing links automatically"),
                          QStringLiteral("Make Default"), QStringLiteral("primary"), QStringLiteral("warn"));
    }
}

void LinkRedirectorPanel::refreshBrowserFields()
{
    refreshBrowserField(m_defaultField, QStringLiteral("defaultBrowser"));
    refreshBrowserField(m_alternativeField, QStringLiteral("alternativeBrowser"));
}

void LinkRedirectorPanel::refreshBrowserField(LinkRedirectorComboField *field, const QString &settingsKey)
{
    const QString configured = m_context.value(settingsKey, QString()).toString();
    const QList<DetectedBrowser> detected = m_sources.detectedBrowsers ? m_sources.detectedBrowsers() : QList<DetectedBrowser>();
    const QList<ComboItem> items = LinkRedirectorRouting::buildBrowserComboItems(configured, detected);
    const QString label = LinkRedirectorRouting::selectedComboLabel(items);
    field->setValueText(label, label == QLatin1String("-"));
}

LinkRedirectorComboField *LinkRedirectorPanel::buildBrowserField(const QString &settingsKey, QWidget *parent)
{
    auto *field = new LinkRedirectorComboField(parent);
    connect(field, &LinkRedirectorComboField::clicked, this, [this, settingsKey, field]() { showBrowserMenu(settingsKey, field); });
    return field;
}

QMenu *LinkRedirectorPanel::buildMenu(const QList<ComboItem> &items, QWidget *parent)
{
    auto *menu = new QMenu(parent);
    for (const ComboItem &item : items) {
        if (item.kind == ComboItem::Kind::Browse) {
            menu->addSeparator();
        }
        QAction *action = menu->addAction(item.label);
        // Checkable para que Theme.cpp (QMenu::indicator) dibuje el tilde violeta del elegido y nada
        // en los demas (canvas ".dd div.cur::before"); "Browse..." nunca es checkable.
        const bool checkable = item.kind != ComboItem::Kind::Browse;
        action->setCheckable(checkable);
        if (checkable) {
            action->setChecked(item.selected);
        }
        action->setData(int(item.kind));
        action->setProperty("exePath", item.exePath);
    }
    return menu;
}

void LinkRedirectorPanel::showBrowserMenu(const QString &settingsKey, LinkRedirectorComboField *field)
{
    const QString configured = m_context.value(settingsKey, QString()).toString();
    const QList<DetectedBrowser> detected = m_sources.detectedBrowsers ? m_sources.detectedBrowsers() : QList<DetectedBrowser>();
    const QList<ComboItem> items = LinkRedirectorRouting::buildBrowserComboItems(configured, detected);

    QMenu *menu = buildMenu(items, this);
    QAction *chosen = menu->exec(field->mapToGlobal(QPoint(0, field->height() + 2)));
    if (!chosen) {
        menu->deleteLater();
        return;
    }
    const auto kind = static_cast<ComboItem::Kind>(chosen->data().toInt());
    const QString exePath = chosen->property("exePath").toString();
    menu->deleteLater();
    if (kind == ComboItem::Kind::Browse) {
        openBrowseDialog(settingsKey, field);
        return;
    }
    chooseBrowser(settingsKey, field, exePath);
}

void LinkRedirectorPanel::chooseBrowser(const QString &settingsKey, LinkRedirectorComboField *field, const QString &exePath)
{
    // "-" limpia el rol (deja la clave vacia, no la borra: mismo comportamiento que el origen).
    m_context.setValue(settingsKey, exePath);
    refreshBrowserField(field, settingsKey);
}

void LinkRedirectorPanel::openBrowseDialog(const QString &settingsKey, LinkRedirectorComboField *field)
{
#if defined(Q_OS_MACOS)
    const QString title = QStringLiteral("Select browser app or executable");
    const QString filter = QStringLiteral("Applications (*.app);;All files (*)");
    const QString startDir = QStringLiteral("/Applications");
#else
    const QString title = QStringLiteral("Select browser executable");
    const QString filter = QStringLiteral("Executables (*.exe)");
    const QString startDir = QStringLiteral("C:/Program Files");
#endif
    const QString chosen = QFileDialog::getOpenFileName(this, title, startDir, filter);
    if (chosen.isEmpty()) {
        return;
    }
    chooseBrowser(settingsKey, field, QDir::fromNativeSeparators(chosen));
}

void LinkRedirectorPanel::refreshMatchWordsFromSettings()
{
    const QStringList words = m_context.value(QStringLiteral("matchWords"), QStringList()).toStringList();
    const QSignalBlocker blocker(m_matchWords);
    m_matchWords->setPlainText(words.join(QStringLiteral("\n")));
}

void LinkRedirectorPanel::scheduleAutosave()
{
    m_autosaveTimer->start();
}

void LinkRedirectorPanel::saveMatchWordsNow()
{
    QStringList cleaned;
    for (const QString &line : m_matchWords->toPlainText().split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            cleaned << trimmed;
        }
    }
    m_context.setValue(QStringLiteral("matchWords"), cleaned);
}

QWidget *LinkRedirectorPanel::buildDropdownPreview(const QList<ComboItem> &items, QWidget *parent)
{
    // Auditoria (item 3): capturar el QMenu REAL de showBrowserMenu(), no una reconstruccion aparte.
    // Un QMenu no se puede exec()/show() en una corrida automatizada; en cambio, WA_DontShowOnScreen
    // deja que el arnes de --ui-shot lo mida y lo renderice (UiShot.cpp ya hace
    // widget->setWindowFlags(Qt::Widget) sobre lo que devuelve esta funcion, asi que el Qt::Popup del
    // menu se reemplaza por un widget hijo comun antes de agregarlo al layout de la captura).
    QMenu *menu = buildMenu(items, parent);
    menu->setAttribute(Qt::WA_DontShowOnScreen, true);
    menu->ensurePolished();
    menu->adjustSize();
    return menu;
}

#include "LinkRedirectorPanel.moc"
