#include "ui/UiWidgets.h"
#include "ui/Theme.h"

#include <QHBoxLayout>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QStyle>
#include <QVariant>
#include <QtMath>

namespace {

struct IconSpec {
    qreal viewBox;
    qreal stroke;
    QPainterPath strokePath;
    QPainterPath fillPath;
    // Los trazos redondos son los de VideoDownloader; los glifos copiados de un SVG ajeno
    // (el ? de File Manager S3, los de la barra de titulo) conservan sus puntas rectas.
    Qt::PenCapStyle cap = Qt::RoundCap;
    Qt::PenJoinStyle join = Qt::RoundJoin;
};

IconSpec buildIcon(Icon icon)
{
    IconSpec s{16, 1.4, {}, {}};
    QPainterPath &p = s.strokePath;
    switch (icon) {
    case Icon::Help:
        // resources/icons/help.svg de LGA_FileManagerS3: signo de pregunta solido, sin circulo.
        // Relleno y ademas trazado con 26 de grosor y union en punta, como el original.
        s.viewBox = 512; s.stroke = 26;
        s.cap = Qt::FlatCap; s.join = Qt::MiterJoin;
        p.moveTo(215, 324.5);
        p.cubicTo(215, 271.7, 221.1, 248.6, 276.8, 212.8);
        p.cubicTo(300.4, 197.9, 314.4, 180.9, 314.4, 158.3);
        p.cubicTo(314.4, 115.4, 281.1, 103.8, 255.6, 103.8);
        p.cubicTo(201.1, 103.8, 193.2, 141.2, 190.2, 167.1);
        p.lineTo(190.2, 167.7);
        p.lineTo(104.8, 167.7);
        p.cubicTo(104.8, 72, 184.1, 39, 250.2, 39);
        p.cubicTo(316.3, 39, 405.3, 48.4, 405.3, 156.2);
        p.cubicTo(405.3, 264, 378.6, 227.2, 332, 257.4);
        p.cubicTo(306, 274.5, 295.1, 284.9, 295.1, 324.5);
        p.closeSubpath();
        p.moveTo(301, 481);
        p.lineTo(212.6, 481);
        p.lineTo(212.6, 403.4);
        p.lineTo(301, 403.4);
        p.closeSubpath();
        s.fillPath = p;
        break;
    case Icon::Minimize:
        s.viewBox = 10; s.stroke = 1.2; s.cap = Qt::FlatCap;
        p.moveTo(0, 5.5); p.lineTo(10, 5.5);
        break;
    case Icon::Close:
        s.viewBox = 10; s.stroke = 1.2; s.cap = Qt::FlatCap;
        p.moveTo(0.5, 0.5); p.lineTo(9.5, 9.5);
        p.moveTo(9.5, 0.5); p.lineTo(0.5, 9.5);
        break;
    case Icon::Folder:
        s.viewBox = 16; s.stroke = 1.4;
        p.moveTo(1.8, 4.2);
        p.cubicTo(1.8, 3.6, 2.3, 3.1, 2.9, 3.1);
        p.lineTo(5.9, 3.1);
        p.lineTo(7.4, 4.7);
        p.lineTo(13.1, 4.7);
        p.cubicTo(13.7, 4.7, 14.2, 5.2, 14.2, 5.8);
        p.lineTo(14.2, 12.1);
        p.cubicTo(14.2, 12.7, 13.7, 13.2, 13.1, 13.2);
        p.lineTo(2.9, 13.2);
        p.cubicTo(2.3, 13.2, 1.8, 12.7, 1.8, 12.1);
        p.closeSubpath();
        break;
    case Icon::X:
        s.viewBox = 14; s.stroke = 1.5;
        p.moveTo(3.5, 3.5); p.lineTo(10.5, 10.5);
        p.moveTo(10.5, 3.5); p.lineTo(3.5, 10.5);
        break;
    case Icon::Pencil:
        // El lapiz de "Change shortcut" del diseno: cuerpo inclinado con la punta abajo a la izquierda.
        s.viewBox = 16; s.stroke = 1.4;
        p.moveTo(3, 13); p.lineTo(3.6, 10.4); p.lineTo(10.6, 3.4); p.lineTo(12.6, 5.4); p.lineTo(5.6, 12.4);
        p.closeSubpath();
        break;
    case Icon::Plus:
        // "Add drive...": el + del diseno, trazo redondo.
        s.viewBox = 10; s.stroke = 1.5;
        p.moveTo(5, 1); p.lineTo(5, 9);
        p.moveTo(1, 5); p.lineTo(9, 5);
        break;
    case Icon::ChevronDown:
        // Flecha del desplegable del intervalo.
        s.viewBox = 8; s.stroke = 1.3; s.cap = Qt::FlatCap; s.join = Qt::MiterJoin;
        p.moveTo(1, 2.5); p.lineTo(4, 5.5); p.lineTo(7, 2.5);
        break;
    }
    return s;
}

class VectorIconEngine : public QIconEngine
{
public:
    VectorIconEngine(Icon icon, const QColor &color) : m_icon(icon), m_color(color) {}

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State) override
    {
        QColor c = m_color;
        if (mode == QIcon::Disabled) {
            c.setAlphaF(0.45);
        }
        Icons::paint(*painter, m_icon, rect, c);
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        QPixmap pm(size * scale);
        pm.setDevicePixelRatio(scale);
        pm.fill(Qt::transparent);
        QPainter painter(&pm);
        paint(&painter, QRect(QPoint(0, 0), size), mode, state);
        return pm;
    }

    QIconEngine *clone() const override { return new VectorIconEngine(m_icon, m_color); }

private:
    Icon m_icon;
    QColor m_color;
};

} // namespace

namespace Icons {

void paint(QPainter &painter, Icon icon, const QRectF &rect, const QColor &color)
{
    const IconSpec spec = buildIcon(icon);
    const qreal scale = qMin(rect.width(), rect.height()) / spec.viewBox;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.translate(rect.center().x() - spec.viewBox * scale / 2.0, rect.center().y() - spec.viewBox * scale / 2.0);
    painter.scale(scale, scale);
    painter.setPen(QPen(color, spec.stroke, Qt::SolidLine, spec.cap, spec.join));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(spec.strokePath);
    if (!spec.fillPath.isEmpty()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawPath(spec.fillPath);
    }
    painter.restore();
}

QIcon icon(Icon icon, const QColor &color)
{
    return QIcon(new VectorIconEngine(icon, color));
}

} // namespace Icons

// ------------------------------------------------------------------ IconWidget

IconWidget::IconWidget(Icon icon, const QColor &color, int size, QWidget *parent)
    : QWidget(parent), m_icon(icon), m_color(color)
{
    setFixedSize(size, size);
    setAttribute(Qt::WA_TransparentForMouseEvents);
}

void IconWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    Icons::paint(painter, m_icon, rect(), m_color);
}

// ------------------------------------------------------------------ ElidedLabel

ElidedLabel::ElidedLabel(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
}

void ElidedLabel::setText(const QString &text)
{
    if (text == m_text) {
        return;
    }
    m_text = text;
    setToolTip(text);
    updateGeometry();
    update();
}

void ElidedLabel::setElideMode(Qt::TextElideMode mode)
{
    m_mode = mode;
    update();
}

QString ElidedLabel::shownText() const
{
    return fontMetrics().elidedText(m_text, m_mode, width());
}

QSize ElidedLabel::sizeHint() const
{
    const QFontMetrics fm(font());
    return QSize(fm.horizontalAdvance(m_text), fm.height());
}

QSize ElidedLabel::minimumSizeHint() const
{
    return QSize(0, QFontMetrics(font()).height());
}

void ElidedLabel::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setFont(font());
    painter.setPen(palette().color(QPalette::WindowText));
    painter.drawText(rect(), Qt::AlignLeft | Qt::AlignVCenter, shownText());
}

// ------------------------------------------------------------------ Chip

Chip::Chip(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("chip"));
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(7, 0, 7, 0);
    layout->setSpacing(0);
    m_label = new QLabel(this);
    layout->addWidget(m_label);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void Chip::set(const QString &tone, const QString &text)
{
    if (tone != m_tone) {
        m_tone = tone;
        setProperty("tone", tone);
        Ui::repolish(this);
        Ui::repolish(m_label);
    }
    m_label->setText(text);
}

QString Chip::text() const
{
    return m_label->text();
}

// ------------------------------------------------------------------ Ui

namespace Ui {

QPushButton *button(const QString &text, const QString &variant, const QString &size, QWidget *parent)
{
    auto *b = new QPushButton(text, parent);
    if (!variant.isEmpty()) {
        b->setProperty("variant", variant);
    }
    if (!size.isEmpty()) {
        b->setProperty("btnSize", size);
    }
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

void setIcon(QPushButton *button, Icon icon, const QColor &color, int size)
{
    button->setIcon(Icons::icon(icon, color));
    button->setIconSize(QSize(size, size));
}

void repolish(QWidget *widget)
{
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

void setStyleProperty(QWidget *widget, const char *name, const QVariant &value)
{
    if (widget->property(name) == value) {
        return;
    }
    widget->setProperty(name, value);
    repolish(widget);
}

} // namespace Ui
