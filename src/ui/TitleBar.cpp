#include "ui/TitleBar.h"
#include "ui/Theme.h"
#include "ui/UiWidgets.h"
#include "core/I18n.h"

#include <QAbstractButton>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QWindow>
#include <QtMath>

namespace {

// Medidas del Pliego (`.tb`): 40 px de alto, sin linea abajo (la barra es la misma tinta que la mesa).
constexpr int kBarHeight = 40;

// Boton de la barra: solo un glifo. Con el mouse encima se aclara el glifo y aparece un fondo; el «?»
// activo (ayuda abierta) va en magenta sobre su fondo.
class TitleButton : public QAbstractButton
{
public:
    TitleButton(Icon icon, const QSize &size, int glyph, int radius, const QString &name, QWidget *parent)
        : QAbstractButton(parent)
        , m_icon(icon)
        , m_glyph(glyph)
        , m_radius(radius)
    {
        setFixedSize(size);
        setFocusPolicy(Qt::NoFocus);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        // Sin tooltip: minimizar, cerrar y ayuda se entienden por su icono.
        setAccessibleName(name);
    }

    void setActive(bool active)
    {
        if (active != m_active) {
            m_active = active;
            update();
        }
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const bool hot = isEnabled() && underMouse();
        QColor glyphColor = Theme::color(Theme::kIcon);
        if (!isEnabled()) {
            glyphColor = Theme::color(Theme::kTextPlaceholder);
        } else if (m_active) {
            glyphColor = Theme::color(Theme::kAccent);
        } else if (hot) {
            glyphColor = Theme::color(Theme::kTextStrong);
        }
        if (m_active || hot) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(Theme::color(m_active ? Theme::kHelpActiveBg : Theme::kTile));
            painter.drawRoundedRect(QRectF(rect()), m_radius, m_radius);
        }
        const QRectF glyph((width() - m_glyph) / 2.0, (height() - m_glyph) / 2.0, m_glyph, m_glyph);
        Icons::paint(painter, m_icon, glyph, glyphColor);
    }

private:
    Icon m_icon;
    int m_glyph;
    int m_radius;
    bool m_active = false;
};

#ifdef Q_OS_MACOS
// Boton del "semaforo" de macOS: circulo de 12 px, gris en reposo y con su color (rojo cerrar,
// amarillo minimizar) cuando el mouse pasa por la barra, como las ventanas del sistema. Son los colores
// del sistema, no tokens de la app.
class TrafficLight : public QAbstractButton
{
public:
    TrafficLight(const QColor &color, const QString &name, QWidget *parent)
        : QAbstractButton(parent)
        , m_color(color)
    {
        setFixedSize(20, 20);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover);
        setAccessibleName(name);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        const bool hot = parentWidget() && parentWidget()->underMouse();
        painter.setBrush(hot || underMouse() ? m_color : Theme::color(Theme::kBorderStrong));
        painter.drawEllipse(QRectF(4, 4, 12, 12));
    }

private:
    QColor m_color;
};
#endif

} // namespace

TitleBar::TitleBar(QWidget *parent)
    : TitleBar(QStringLiteral("LGA Mighty Tools"), true, parent)
{
}

void TitleBar::setTitle(const QString &title)
{
    m_title->setText(title);
}

void TitleBar::setHelpActive(bool active)
{
    if (auto *help = static_cast<TitleButton *>(m_help)) {
        help->setActive(active);
        help->setAccessibleDescription(active ? I18n::trc("state", "Open") : QString());
    }
}

void TitleBar::setHelpEnabled(bool enabled)
{
    if (m_help) {
        m_help->setEnabled(enabled);
        m_help->setCursor(enabled ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }
}

TitleBar::TitleBar(const QString &titleText, bool withHelp, QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("titleBar"));
    setFixedHeight(kBarHeight);
    setAttribute(Qt::WA_Hover);

#ifdef Q_OS_MACOS
    // macOS: cerrar y minimizar a la IZQUIERDA como en toda ventana del sistema, sin icono de app
    // (el Dock no lo muestra: la app vive en la barra de menu) y la ayuda a la derecha.
    auto *macLayout = new QHBoxLayout(this);
    macLayout->setContentsMargins(9, 0, 8, 0);
    macLayout->setSpacing(0);
    auto *closeLight = new TrafficLight(QColor(0xff, 0x5f, 0x57), I18n::tr("Close"), this);
    auto *minimizeLight = new TrafficLight(QColor(0xfe, 0xbc, 0x2e), I18n::tr("Minimize"), this);
    macLayout->addWidget(closeLight, 0, Qt::AlignVCenter);
    macLayout->addWidget(minimizeLight, 0, Qt::AlignVCenter);
    macLayout->addSpacing(10);
    auto *macTitle = new QLabel(titleText, this);
    macTitle->setObjectName(QStringLiteral("titleBarTitle"));
    macTitle->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_title = macTitle;
    macLayout->addWidget(macTitle, 0, Qt::AlignVCenter);
    macLayout->addStretch(1);
    if (withHelp) {
        auto *macHelp = new TitleButton(Icon::Help, QSize(34, 28), 16, 6, I18n::tr("Help"), this);
        m_help = macHelp;
        macLayout->addWidget(macHelp, 0, Qt::AlignVCenter);
        connect(macHelp, &QAbstractButton::clicked, this, &TitleBar::helpClicked);
    }
    connect(minimizeLight, &QAbstractButton::clicked, this, [this]() { window()->showMinimized(); });
    connect(closeLight, &QAbstractButton::clicked, this, [this]() { window()->close(); });
    return;
#endif

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(14, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(new AppLogo(26, this), 0, Qt::AlignVCenter);
    layout->addSpacing(10);
    auto *title = new QLabel(titleText, this);
    title->setObjectName(QStringLiteral("titleBarTitle"));
    title->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_title = title;
    layout->addWidget(title, 0, Qt::AlignVCenter);
    layout->addStretch(1);

    if (withHelp) {
        auto *help = new TitleButton(Icon::Help, QSize(34, 28), 16, 6, I18n::tr("Help"), this);
        m_help = help;
        layout->addWidget(help, 0, Qt::AlignVCenter);
        connect(help, &QAbstractButton::clicked, this, &TitleBar::helpClicked);

        // `.tb-sep`: 1 x 14, 6 a la izquierda y 4 a la derecha.
        layout->addSpacing(6);
        auto *separator = new QFrame(this);
        separator->setObjectName(QStringLiteral("titleBarSeparator"));
        separator->setFixedSize(1, 14);
        layout->addWidget(separator, 0, Qt::AlignVCenter);
        layout->addSpacing(4);
    }

    auto *minimize = new TitleButton(Icon::Minimize, QSize(42, kBarHeight), 10, 0, I18n::tr("Minimize"), this);
    layout->addWidget(minimize, 0, Qt::AlignTop);
    auto *close = new TitleButton(Icon::Close, QSize(42, kBarHeight), 10, 0, I18n::tr("Close"), this);
    layout->addWidget(close, 0, Qt::AlignTop);

    connect(minimize, &QAbstractButton::clicked, this, [this]() { window()->showMinimized(); });
    connect(close, &QAbstractButton::clicked, this, [this]() { window()->close(); });
}

bool TitleBar::event(QEvent *event)
{
    // El semaforo de mac se colorea cuando el mouse entra a la barra: hay que repintarlo.
    if (event->type() == QEvent::HoverEnter || event->type() == QEvent::HoverLeave) {
        for (QWidget *child : findChildren<QWidget *>()) {
            child->update();
        }
    }
    return QWidget::event(event);
}

void TitleBar::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), Theme::color(Theme::kTitleBar));
}

void TitleBar::mousePressEvent(QMouseEvent *event)
{
    // Los botones se quedan con su propio click; lo que llega aca es la zona libre de la barra.
    if (event->button() == Qt::LeftButton && window()->windowHandle()) {
        window()->windowHandle()->startSystemMove();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}
