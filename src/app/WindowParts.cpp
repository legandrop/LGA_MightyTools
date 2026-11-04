#include "app/WindowParts.h"
#include "core/I18n.h"

#include "ui/CustomTooltip.h"
#include "ui/Theme.h"
#include "ui/UiWidgets.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

// ------------------------------------------------------------------ ToolIcon

ToolIcon::ToolIcon(IconPainter painter, int size, QWidget *parent)
    : QWidget(parent)
    , m_painter(std::move(painter))
    , m_color(Theme::color(Theme::kToolIconOn))
    , m_glyph(size)
{
    // Cuadrado del tamano del dibujo. Quien necesita mas aire alrededor ensancha el widget aparte.
    setFixedSize(size, size);
    setAttribute(Qt::WA_TransparentForMouseEvents);
}

void ToolIcon::setColor(const QColor &color)
{
    if (color == m_color) {
        return;
    }
    m_color = color;
    update();
}

void ToolIcon::paintEvent(QPaintEvent *)
{
    if (!m_painter) {
        return;
    }
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF glyph((width() - m_glyph) / 2.0, (height() - m_glyph) / 2.0, m_glyph, m_glyph);
    m_painter(painter, glyph, m_color);
}

// ------------------------------------------------------------------ SidebarItem

SidebarItem::SidebarItem(const QString &id, const QString &title, IconPainter icon, bool withSwitch, QWidget *parent)
    : QWidget(parent)
    , m_id(id)
    , m_withSwitch(withSwitch)
{
    // Medidas del Pliego (`.nrow`): 42 de alto (46 General), la inicial en una columna de 30, la ficha con
    // 12 de aire a la izquierda y 8 a la derecha (14 en el diseno: con 14, Â«Old client still installedÂ» no
    // entraba entero), 11 entre icono y textos, 8 hasta el switch. Lo controla --ui-probe nav-fit.
    setObjectName(QStringLiteral("sideItem"));
    setFixedHeight(withSwitch ? 42 : 46);
    setAttribute(Qt::WA_Hover);
    setCursor(Qt::PointingHandCursor);
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 8, 0);
    row->setSpacing(0);
    // La inicial va a la altura del nombre: la caja de los dos renglones (18 + 15) queda centrada en la fila.
    m_letter = Ui::label(QString(), "navLetter", this);
    m_letter->setFixedSize(kLetterColumn, height());
    m_letter->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    m_letter->setContentsMargins(0, (height() - 33) / 2, 0, 0);
    row->addWidget(m_letter, 0, Qt::AlignTop);
    row->addSpacing(12);
    m_icon = new ToolIcon(std::move(icon), 16, this);
    row->addWidget(m_icon, 0, Qt::AlignVCenter);
    row->addSpacing(11);
    auto *texts = new QVBoxLayout();
    texts->setContentsMargins(0, 0, 0, 0);
    texts->setSpacing(0);
    m_name = Ui::label(title, "sideName", this);
    m_name->setFixedHeight(18);
    m_status = Ui::label(QString(), "sideStatus", this);
    m_status->setFixedHeight(15);
    m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_status->installEventFilter(this);
    texts->addWidget(m_name);
    texts->addWidget(m_status);
    row->addLayout(texts, 1);
    if (m_withSwitch) {
        row->addSpacing(8);
        m_switch = new ToggleSwitch(this);
        m_switch->setAccessibleName(title);
        row->addWidget(m_switch, 0, Qt::AlignVCenter);
        // clicked y no toggled: reflejar el estado con setChecked no es un pedido del usuario.
        connect(m_switch, &QAbstractButton::clicked, this, [this](bool checked) { emit toggleRequested(m_id, checked); });
    }
    refreshColors();
}

void SidebarItem::setLetter(const QString &letter)
{
    m_letter->setText(letter);
}

QRect SidebarItem::tabRect() const
{
    return rect().adjusted(kLetterColumn, 0, 0, 0);
}

void SidebarItem::setSelected(bool selected)
{
    if (selected == m_selected) {
        return;
    }
    m_selected = selected;
    refreshColors();
    update();
}

void SidebarItem::setState(bool on, const QString &statusText, const QString &tone)
{
    m_on = on;
    m_statusText = statusText;
    Ui::setStyleProperty(m_status, "tone", tone);
    elideStatus();
    refreshColors();
    if (m_switch && m_switch->isChecked() != on) {
        m_switch->setChecked(on);
    }
}

bool SidebarItem::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_status && (event->type() == QEvent::Resize || event->type() == QEvent::FontChange)) {
        elideStatus();
    }
    return QWidget::eventFilter(watched, event);
}

int SidebarItem::statusSlack(const QString &text) const
{
    const int end = m_status->x() + m_status->fontMetrics().horizontalAdvance(text);
    const int limit = m_switch ? m_switch->x() : width() - layout()->contentsMargins().right();
    return limit - end;
}

void SidebarItem::elideStatus()
{
    const QString shown = m_status->fontMetrics().elidedText(m_statusText, Qt::ElideRight, m_status->width());
    if (shown != m_status->text()) {
        m_status->setText(shown);
    }
    CustomTooltip::instance()->setToolTip(m_status, shown == m_statusText ? QString() : m_statusText.toHtmlEscaped());
}

void SidebarItem::refreshColors()
{
    // Apagada, gris; prendida, clara; elegida, la mas clara (`.nrow.on`, `.nrow.sel`). General cuenta como
    // prendida.
    const bool on = m_on || !m_withSwitch;
    Ui::setStyleProperty(m_name, "on", on);
    Ui::setStyleProperty(m_name, "sel", m_selected);
    m_icon->setColor(Theme::color(m_selected ? Theme::kToolIconSel : on ? Theme::kToolIconOn : Theme::kToolIconOff));
}

void SidebarItem::paintEvent(QPaintEvent *)
{
    // La ficha elegida la pinta NavShell (unida a la hoja); aca solo el hover de las otras.
    if (m_selected || !underMouse()) {
        return;
    }
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Theme::color(Theme::kNavHover));
    painter.drawRoundedRect(QRectF(tabRect()).adjusted(0, 1, -4, -1), 10, 10);
}

void SidebarItem::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        emit picked(m_id);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

// ------------------------------------------------------------------ NavShell

namespace {
constexpr int kSheetTop = 4;
constexpr int kSheetMargin = 10;
constexpr qreal kSheetRadius = 12;
constexpr qreal kTabRadius = 10;
constexpr qreal kFillet = 10;

// Rectangulo redondeado con un radio por esquina (QPainterPath::addRoundedRect usa uno solo).
QPainterPath roundedRect(const QRectF &r, qreal topLeft, qreal topRight, qreal bottomRight, qreal bottomLeft)
{
    QPainterPath path;
    path.moveTo(r.left() + topLeft, r.top());
    path.lineTo(r.right() - topRight, r.top());
    if (topRight > 0) path.arcTo(QRectF(r.right() - 2 * topRight, r.top(), 2 * topRight, 2 * topRight), 90, -90);
    path.lineTo(r.right(), r.bottom() - bottomRight);
    if (bottomRight > 0) path.arcTo(QRectF(r.right() - 2 * bottomRight, r.bottom() - 2 * bottomRight, 2 * bottomRight, 2 * bottomRight), 0, -90);
    path.lineTo(r.left() + bottomLeft, r.bottom());
    if (bottomLeft > 0) path.arcTo(QRectF(r.left(), r.bottom() - 2 * bottomLeft, 2 * bottomLeft, 2 * bottomLeft), 270, -90);
    path.lineTo(r.left(), r.top() + topLeft);
    if (topLeft > 0) path.arcTo(QRectF(r.left(), r.top(), 2 * topLeft, 2 * topLeft), 180, -90);
    path.closeSubpath();
    return path;
}
} // namespace

NavShell::NavShell(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("navShell"));
}

QRect NavShell::sheetRect() const
{
    return QRect(kNavWidth, kSheetTop, width() - kNavWidth - kSheetMargin, height() - kSheetTop - kSheetMargin);
}

void NavShell::setSelectedItem(SidebarItem *item)
{
    m_selected = item;
    update();
}

void NavShell::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), Theme::color(Theme::kDesk));
    const QRectF sheet = sheetRect();
    QRectF tab;
    if (m_selected && m_selected->isVisibleTo(this)) {
        const QRect r = m_selected->tabRect();
        tab = QRectF(QRect(m_selected->mapTo(this, r.topLeft()), r.size()));
        tab.setRight(sheet.left());
    }
    // La ficha cerca del borde de la hoja: la esquina de la hoja se cuadra y la concava se achica, asi la
    // union no deja una muesca de mesa.
    const qreal topRoom = tab.isNull() ? 99 : tab.top() - sheet.top();
    const qreal bottomRoom = tab.isNull() ? 99 : sheet.bottom() - tab.bottom();
    const qreal sheetTopLeft = topRoom >= kSheetRadius + kFillet ? kSheetRadius : 0;
    const qreal sheetBottomLeft = bottomRoom >= kSheetRadius + kFillet ? kSheetRadius : 0;
    QPainterPath shape = roundedRect(sheet, sheetTopLeft, kSheetRadius, kSheetRadius, sheetBottomLeft);
    if (!tab.isNull()) {
        shape = shape.united(roundedRect(tab, kTabRadius, 0, 0, kTabRadius));
        // Esquinas concavas (`.fil`): el cuadrado entre la ficha y la hoja menos el cuarto de circulo.
        const qreal top = qMin(kFillet, qMax<qreal>(0, topRoom));
        if (top > 0) {
            const QRectF square(tab.right() - top, tab.top() - top, top, top);
            QPainterPath fillet;
            fillet.addRect(square);
            QPainterPath round;
            round.addEllipse(square.topLeft(), top, top);
            shape = shape.united(fillet.subtracted(round));
        }
        const qreal bottom = qMin(kFillet, qMax<qreal>(0, bottomRoom));
        if (bottom > 0) {
            const QRectF square(tab.right() - bottom, tab.bottom(), bottom, bottom);
            QPainterPath fillet;
            fillet.addRect(square);
            QPainterPath round;
            round.addEllipse(square.bottomLeft(), bottom, bottom);
            shape = shape.united(fillet.subtracted(round));
        }
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(Theme::color(Theme::kSheet));
    painter.drawPath(shape);
}

// ------------------------------------------------------------------ ModuleHeader

ModuleHeader::ModuleHeader(const QString &title, const QString &platforms, const QString &description, bool withSwitch,
                           QWidget *parent)
    : QWidget(parent)
{
    // `.ph`: los textos a la izquierda y el switch grande arriba a la derecha, 16 entre los dos.
    setObjectName(QStringLiteral("modHead"));
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    auto *row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(16);

    auto *texts = new QVBoxLayout();
    texts->setContentsMargins(0, 0, 0, 0);
    texts->setSpacing(6);
    // `.ttl`: 40 de alto, el titulo a 34 (Bricolage ExtraBold, texto liso: sin el desregistro del mockup,
    // pedido de Lega) y la etiqueta de plataformas 12 a la derecha, 4 mas abajo que el centro.
    auto *titleRow = new QHBoxLayout();
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(12);
    QLabel *titleLabel = Ui::label(title, "pageTitle", this);
    QFont big = Theme::titleFont(34, 800);
    big.setLetterSpacing(QFont::PercentageSpacing, 98); // -.02em
    titleLabel->setFont(big);
    titleLabel->setFixedHeight(40);
    titleRow->addWidget(titleLabel, 0, Qt::AlignVCenter);
    if (!platforms.isEmpty()) {
        auto *tagBox = new QVBoxLayout();
        tagBox->setContentsMargins(0, 0, 0, 0);
        tagBox->setSpacing(0);
        tagBox->addStretch(1);
        QLabel *tag = Ui::label(platforms, "platTag", this);
        // Un QLabel con borde agrega una sangria automatica (media "x"): sin ella mide lo del diseno.
        tag->setIndent(0);
        tagBox->addWidget(tag);
        tagBox->addSpacing(8);
        titleRow->addLayout(tagBox);
    }
    titleRow->addStretch(1);
    texts->addLayout(titleRow);
    auto *descriptionLabel = new CaptionLabel(description, this, 20);
    descriptionLabel->setObjectName(QStringLiteral("pageDescription"));
    descriptionLabel->setMaximumWidth(470);
    texts->addWidget(descriptionLabel);
    row->addLayout(texts, 1);

    if (withSwitch) {
        m_switch = new ToggleSwitch(this);
        m_switch->setLarge(true);
        m_switch->setAccessibleName(title);
        auto *switchBox = new QVBoxLayout();
        switchBox->setContentsMargins(0, 9, 0, 0);
        switchBox->addWidget(m_switch);
        switchBox->addStretch(1);
        row->addLayout(switchBox);
        connect(m_switch, &QAbstractButton::clicked, this, &ModuleHeader::toggleRequested);
    }
    column->addLayout(row);

    // `.live`: 14 debajo del encabezado. Vacio y oculto hasta que el panel suba su estado.
    m_liveSlot = new QWidget(this);
    m_liveLayout = new QVBoxLayout(m_liveSlot);
    m_liveLayout->setContentsMargins(0, 14, 0, 0);
    m_liveLayout->setSpacing(0);
    m_liveSlot->hide();
    column->addWidget(m_liveSlot);
}

void ModuleHeader::setOn(bool on)
{
    if (m_switch && m_switch->isChecked() != on) {
        m_switch->setChecked(on);
    }
}

void ModuleHeader::adoptLiveStatus(QWidget *status, QObject *owner)
{
    // El lugar del estado se ve cuando se ve el contenedor de donde vino la tarjeta: en los paneles con lista
    // y editor (App Toggle, Mouse Buttons, Window Layouts) la tarjeta es de la lista y se va con ella.
    m_liveHome = status->parentWidget();
    if (m_liveHome) {
        m_liveHome->installEventFilter(this);
    }
    m_liveLayout->addWidget(status);
    status->show();
    m_liveSlot->setVisible(!m_liveHome || !m_liveHome->isHidden());
    // El estado es del panel: se va con el (al apagar la herramienta o al rearmar la ventana).
    connect(owner, &QObject::destroyed, status, &QObject::deleteLater);
    connect(status, &QObject::destroyed, m_liveSlot, &QWidget::hide);
}

bool ModuleHeader::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_liveHome && (event->type() == QEvent::Show || event->type() == QEvent::Hide)) {
        m_liveSlot->setVisible(event->type() == QEvent::Show);
    }
    return QWidget::eventFilter(watched, event);
}

QString platformsText(int platforms)
{
    const bool win = platforms & PlatformWindows;
    const bool mac = platforms & PlatformMac;
    if (win && mac) {
        return QStringLiteral("Win · mac");
    }
    return win ? QStringLiteral("Windows") : QStringLiteral("mac");
}

// ------------------------------------------------------------------ HowItWorksStrip

// Un glifo de Icons que cambia de dibujo y de color (el icono y la flecha de la franja).
class StripGlyph : public QWidget
{
public:
    StripGlyph(int size, QWidget *parent) : QWidget(parent)
    {
        setFixedSize(size, size);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    void set(Icon icon, const QColor &color)
    {
        m_icon = icon;
        m_color = color;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        Icons::paint(painter, m_icon, QRectF(rect()), m_color);
    }

private:
    Icon m_icon = Icon::HelpCircle;
    QColor m_color;
};

namespace {

// La linea de 36 de la franja: un boton sin foco (la regla de la app) con sus piezas adentro.
class StripHeader : public QAbstractButton
{
public:
    explicit StripHeader(QWidget *parent) : QAbstractButton(parent)
    {
        setFocusPolicy(Qt::NoFocus);
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(36);
    }

protected:
    void paintEvent(QPaintEvent *) override {}
};

// Los atajos resaltados con HelpSection::strong() pasan a teclas; lo demas queda resaltado.
QString withKeycaps(const QString &html)
{
    static const QRegularExpression strong(QStringLiteral("<span style=\"color:[^\"]*; font-weight:600;\">([^<]*)</span>"));
    QString out;
    int last = 0;
    for (auto it = strong.globalMatch(html); it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        const QString keys = Ui::keycapsHtml(match.captured(1));
        out += html.mid(last, match.capturedStart() - last);
        out += keys.isEmpty() ? match.captured(0) : keys;
        last = match.capturedEnd();
    }
    return out + html.mid(last);
}

} // namespace

HowItWorksStrip::HowItWorksStrip(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("howItWorks"));
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(1, 1, 1, 1);
    column->setSpacing(0);

    // `.how-h`: 36 de alto, 14 a los lados, 9 entre piezas.
    m_header = new StripHeader(this);
    m_header->setAccessibleName(I18n::tr("How it works"));
    auto *head = new QHBoxLayout(m_header);
    head->setContentsMargins(13, 0, 13, 0);
    head->setSpacing(9);
    m_icon = new StripGlyph(15, m_header);
    head->addWidget(m_icon, 0, Qt::AlignVCenter);
    QLabel *title = Ui::label(I18n::tr("How it works"), "howTitle", m_header);
    title->setAttribute(Qt::WA_TransparentForMouseEvents);
    head->addWidget(title, 0, Qt::AlignVCenter);
    head->addStretch(1);
    m_count = Ui::label(QString(), "howCount", m_header);
    m_count->setAttribute(Qt::WA_TransparentForMouseEvents);
    head->addWidget(m_count, 0, Qt::AlignVCenter);
    m_chevron = new StripGlyph(12, m_header);
    head->addWidget(m_chevron, 0, Qt::AlignVCenter);
    column->addWidget(m_header);

    // `.how-b`: 18 a la derecha, 14 abajo y a la izquierda.
    m_body = new QWidget(this);
    m_bodyLayout = new QVBoxLayout(m_body);
    m_bodyLayout->setContentsMargins(13, 0, 17, 13);
    m_bodyLayout->setSpacing(0);
    column->addWidget(m_body);

    connect(m_header, &QAbstractButton::clicked, this, [this]() {
        setOpen(!m_open);
        emit toggledByUser(m_open);
    });
    setOpen(false);
}

void HowItWorksStrip::setSection(const HelpSection &section)
{
    while (QLayoutItem *item = m_bodyLayout->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    m_steps = int(section.steps.size());
    m_count->setText(m_steps == 1 ? I18n::tr("1 step") : I18n::tr("%1 steps").arg(m_steps));
    QList<QWidget *> added;
    // `.steps li`: numero de 18 de ancho y 12 hasta el texto, 4 arriba y abajo, 22 por linea.
    for (int i = 0; i < section.steps.size(); ++i) {
        auto *rowWidget = new QWidget(m_body);
        auto *row = new QHBoxLayout(rowWidget);
        row->setContentsMargins(0, 4, 0, 4);
        row->setSpacing(12);
        QLabel *number = Ui::label(QString::number(i + 1), "howNumber", rowWidget);
        number->setFixedSize(18, 22);
        number->setAlignment(Qt::AlignCenter);
        row->addWidget(number, 0, Qt::AlignTop);
        auto *step = new RichLineLabel(withKeycaps(section.steps.at(i)), 22, rowWidget);
        step->setObjectName(QStringLiteral("howStep"));
        row->addWidget(step, 1); // sin alineacion: con ella el layout no le pregunta el alto por ancho
        m_bodyLayout->addWidget(rowWidget);
        added << rowWidget;
    }
    // `.note`: 6 arriba, 30 a la izquierda (la columna del numero), una linea y 11 hasta el texto.
    if (!section.note.isEmpty()) {
        auto *noteBox = new QWidget(m_body);
        auto *note = new QVBoxLayout(noteBox);
        note->setContentsMargins(30, 6, 0, 0);
        note->setSpacing(11);
        auto *rule = new QFrame(noteBox);
        rule->setObjectName(QStringLiteral("howRule"));
        note->addWidget(rule);
        auto *text = new CaptionLabel(section.note, noteBox, 18);
        text->setObjectName(QStringLiteral("howNote"));
        note->addWidget(text);
        m_bodyLayout->addWidget(noteBox);
        added << noteBox;
    }
    // Los hijos nuevos de un contenedor ya mostrado se ven recien en el proximo ciclo: se muestran ya, asi
    // el layout los cuenta al medir. Solo los nuevos: los viejos esperan su deleteLater ocultos.
    for (QWidget *child : added) {
        child->setVisible(true);
    }
    m_bodyLayout->invalidate();
}

void HowItWorksStrip::setOpen(bool open)
{
    m_open = open;
    Ui::setStyleProperty(this, "open", open);
    m_body->setVisible(open);
    m_count->setVisible(!open);
    m_icon->set(Icon::HelpCircle, Theme::color(open ? Theme::kAccent : Theme::kIcon));
    m_chevron->set(open ? Icon::ChevronUp : Icon::ChevronDownRound, Theme::color(Theme::kIcon));
    m_header->setAccessibleDescription(open ? I18n::trc("state", "Open") : QString());
}

// ------------------------------------------------------------------ OffPanel

OffPanel::OffPanel(const ModuleDescriptor &descriptor, const ModuleOffNotice &notice, QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("offPanel"));
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(12);

    // `.card` del apagado: el titulo, lo que no se carga, lo que hace al prenderse y «Turn on».
    QFrame *view = Ui::card(this);
    view->setObjectName(QStringLiteral("card"));
    auto *layout = new QVBoxLayout(view);
    layout->setContentsMargins(18, 15, 18, 16);
    layout->setSpacing(10);
    layout->addWidget(Ui::label(I18n::trc("tool", "%1 is off").arg(descriptor.title), "cardTitle", view));
    layout->addWidget(
        Ui::caption(I18n::tr("Nothing of it is loaded: no memory, no CPU, no shortcuts, no system hooks."), view));
    if (!descriptor.offBullets.isEmpty()) {
        auto *bullets = new QVBoxLayout();
        bullets->setContentsMargins(0, 2, 0, 0);
        bullets->setSpacing(2);
        for (const QString &text : descriptor.offBullets) {
            auto *row = new QHBoxLayout();
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(10);
            // Un rombo chico al margen, a la altura de la primera linea.
            auto *markBox = new QVBoxLayout();
            markBox->setContentsMargins(4, 6, 0, 0);
            auto *mark = new StatusDot(8, view);
            mark->setTone(QStringLiteral("paused"));
            markBox->addWidget(mark);
            markBox->addStretch(1);
            row->addLayout(markBox);
            QLabel *line = Ui::label(text, "offBullet", view);
            line->setWordWrap(true);
            line->setMinimumHeight(19);
            row->addWidget(line, 1);
            bullets->addLayout(row);
        }
        layout->addLayout(bullets);
    }
    layout->addSpacing(2);
    QPushButton *turnOn = Ui::button(I18n::tr("Turn on"), QStringLiteral("primary"), QString(), view);
    turnOn->setObjectName(QStringLiteral("turnOnButton"));
    layout->addWidget(turnOn, 0, Qt::AlignLeft);
    column->addWidget(view);
    connect(turnOn, &QPushButton::clicked, this, &OffPanel::turnOnRequested);

    if (notice.visible) {
        auto *warning = new StatusCard(this);
        warning->set(QStringLiteral("warn"), notice.title, notice.caption, notice.actionText, QString(),
                     QStringLiteral("warn"), QStringLiteral("sm"));
        column->addWidget(warning);
        connect(warning->button(), &QPushButton::clicked, this, &OffPanel::releaseRequested);
    }
}
