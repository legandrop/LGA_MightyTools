#include "app/WindowParts.h"
#include "core/I18n.h"

#include "ui/Theme.h"
#include "ui/UiWidgets.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

// ------------------------------------------------------------------ ToolIcon

ToolIcon::ToolIcon(IconPainter painter, int size, QWidget *parent)
    : QWidget(parent)
    , m_painter(std::move(painter))
    , m_color(Theme::color(Theme::kToolIconOn))
{
    setFixedSize(size, 16);
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
    // Medidas del canvas (`.item`): 38 de alto, 0 x 8 de relleno, 9 entre piezas, icono en 18.
    setObjectName(QStringLiteral("sideItem"));
    setFixedHeight(38);
    setAttribute(Qt::WA_Hover);
    setCursor(Qt::PointingHandCursor);
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(8, 0, 8, 0);
    row->setSpacing(9);
    m_icon = new ToolIcon(std::move(icon), 18, this);
    row->addWidget(m_icon, 0, Qt::AlignVCenter);
    auto *texts = new QVBoxLayout();
    texts->setContentsMargins(0, 0, 0, 0);
    texts->setSpacing(0);
    m_name = Ui::label(title, "sideName", this);
    m_name->setFixedHeight(15);
    m_status = Ui::label(QString(), "sideStatus", this);
    m_status->setFixedHeight(15);
    texts->addWidget(m_name);
    texts->addWidget(m_status);
    row->addLayout(texts, 1);
    if (m_withSwitch) {
        m_switch = new ToggleSwitch(this);
        row->addWidget(m_switch, 0, Qt::AlignVCenter);
        // clicked y no toggled: reflejar el estado con setChecked no es un pedido del usuario.
        connect(m_switch, &QAbstractButton::clicked, this, [this](bool checked) { emit toggleRequested(m_id, checked); });
    }
}

void SidebarItem::setSelected(bool selected)
{
    if (selected == m_selected) {
        return;
    }
    m_selected = selected;
    Ui::setStyleProperty(m_name, "sel", selected);
    update();
}

void SidebarItem::setState(bool on, const QString &statusText, const QString &tone)
{
    m_on = on;
    m_status->setText(statusText);
    Ui::setStyleProperty(m_status, "tone", tone);
    Ui::setStyleProperty(m_name, "off", m_withSwitch && !on);
    m_icon->setColor(Theme::color(on || !m_withSwitch ? Theme::kToolIconOn : Theme::kToolIconOff));
    if (m_switch && m_switch->isChecked() != on) {
        m_switch->setChecked(on);
    }
}

void SidebarItem::paintEvent(QPaintEvent *)
{
    if (!m_selected && !underMouse()) {
        return;
    }
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Theme::color(m_selected ? Theme::kSideSelected : Theme::kSideHover));
    painter.drawRoundedRect(QRectF(rect()), 6, 6);
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

// ------------------------------------------------------------------ ModuleHeader

ModuleHeader::ModuleHeader(const QString &title, const QString &platforms, const QString &description, IconPainter icon,
                           bool withSwitch, QWidget *parent)
    : QWidget(parent)
{
    // `.modHead`: 2 / 2 / 6 de relleno, 12 entre piezas, todo arriba.
    setObjectName(QStringLiteral("modHead"));
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(2, 2, 2, 6);
    row->setSpacing(12);

    auto *box = new QFrame(this);
    box->setObjectName(QStringLiteral("modIcon"));
    box->setFixedSize(34, 34);
    auto *boxLayout = new QHBoxLayout(box);
    boxLayout->setContentsMargins(0, 0, 0, 0);
    m_icon = new ToolIcon(std::move(icon), 16, box);
    boxLayout->addWidget(m_icon, 0, Qt::AlignCenter);
    row->addWidget(box, 0, Qt::AlignTop);

    auto *texts = new QVBoxLayout();
    texts->setContentsMargins(0, 0, 0, 0);
    texts->setSpacing(2);
    auto *titleRow = new QHBoxLayout();
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(8);
    titleRow->addWidget(Ui::label(title, "modTitle", this), 0, Qt::AlignVCenter);
    if (!platforms.isEmpty()) {
        QLabel *tag = Ui::label(platforms, "platTag", this);
        // Un QLabel con borde agrega una sangria automatica (media "x"): sin ella mide lo del canvas.
        tag->setIndent(0);
        titleRow->addWidget(tag, 0, Qt::AlignVCenter);
    }
    titleRow->addStretch(1);
    texts->addLayout(titleRow);
    texts->addWidget(Ui::caption(description, this));
    row->addLayout(texts, 1);

    if (withSwitch) {
        m_switch = new ToggleSwitch(this);
        row->addWidget(m_switch, 0, Qt::AlignTop);
        connect(m_switch, &QAbstractButton::clicked, this, &ModuleHeader::toggleRequested);
    }
}

void ModuleHeader::setOn(bool on)
{
    m_icon->setColor(Theme::color(on ? Theme::kToolIconOn : Theme::kIcon));
    if (m_switch && m_switch->isChecked() != on) {
        m_switch->setChecked(on);
    }
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

// ------------------------------------------------------------------ OffPanel

OffPanel::OffPanel(const ModuleDescriptor &descriptor, const ModuleOffNotice &notice, QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("offPanel"));
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(10);

    // `.card.offView`: 16 x 14 de relleno, 10 entre piezas, todo a la izquierda.
    QFrame *view = Ui::card(this);
    view->setObjectName(QStringLiteral("card"));
    auto *layout = new QVBoxLayout(view);
    layout->setContentsMargins(14, 16, 14, 16);
    layout->setSpacing(10);
    layout->addWidget(Ui::label(I18n::trc("tool", "%1 is off").arg(descriptor.title), "cardTitle", view));
    layout->addWidget(
        Ui::caption(I18n::tr("Nothing of it is loaded: no memory, no CPU, no shortcuts, no system hooks."), view));
    if (!descriptor.offBullets.isEmpty()) {
        auto *bullets = new QVBoxLayout();
        bullets->setContentsMargins(0, 2, 0, 0);
        bullets->setSpacing(0);
        for (const QString &text : descriptor.offBullets) {
            auto *row = new QHBoxLayout();
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(0);
            // Viñeta por fuera del texto, como la de una lista con 18 px de sangria.
            QLabel *mark = Ui::label(QStringLiteral("•"), "offBulletMark", view);
            mark->setFixedWidth(18);
            mark->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            mark->setContentsMargins(0, 0, 7, 0);
            mark->setFixedHeight(19);
            row->addWidget(mark, 0, Qt::AlignTop);
            QLabel *line = Ui::label(text, "offBullet", view);
            line->setWordWrap(true);
            line->setMinimumHeight(19);
            row->addWidget(line, 1);
            bullets->addLayout(row);
        }
        layout->addLayout(bullets);
    }
    QPushButton *turnOn = Ui::button(I18n::tr("Turn on"), QStringLiteral("primary"), QStringLiteral("sm"), view);
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
