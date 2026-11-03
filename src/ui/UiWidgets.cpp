#include "ui/UiWidgets.h"
#include "ui/CustomTooltip.h"
#include "ui/Theme.h"

#include <QBoxLayout>
#include <QtMath>
#include <QHBoxLayout>
#include <QDesktopServices>
#include <QEvent>
#include <QIconEngine>
#include <QMouseEvent>
#include <QUrl>
#include <QVBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QApplication>
#include <QPixmap>
#include <QPushButton>
#include <QStyle>
#include <QAbstractTextDocumentLayout>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>
#include <QVariant>
#include <QVariantAnimation>
#include <QPointer>
#include <QEasingCurve>
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
    case Icon::General:
        // "General" de la barra lateral: engranaje de trazos del canvas (circulo y ocho rayos).
        s.viewBox = 16; s.stroke = 1.4;
        p.addEllipse(QPointF(8, 8), 2.4, 2.4);
        p.moveTo(8, 1.5); p.lineTo(8, 3.5);
        p.moveTo(8, 12.5); p.lineTo(8, 14.5);
        p.moveTo(1.5, 8); p.lineTo(3.5, 8);
        p.moveTo(12.5, 8); p.lineTo(14.5, 8);
        p.moveTo(3.4, 3.4); p.lineTo(4.8, 4.8);
        p.moveTo(11.2, 11.2); p.lineTo(12.6, 12.6);
        p.moveTo(3.4, 12.6); p.lineTo(4.8, 11.2);
        p.moveTo(11.2, 4.8); p.lineTo(12.6, 3.4);
        break;
    case Icon::TreeClosed:
        // Flecha de una fila que se puede desplegar.
        s.viewBox = 16; s.stroke = 1.8;
        p.moveTo(6, 3.5); p.lineTo(10.5, 8); p.lineTo(6, 12.5);
        break;
    case Icon::TreeOpen:
        s.viewBox = 16; s.stroke = 1.8;
        p.moveTo(3.5, 6); p.lineTo(8, 10.5); p.lineTo(12.5, 6);
        break;
    case Icon::File:
        s.viewBox = 16; s.stroke = 1.3;
        p.moveTo(4, 1.8); p.lineTo(9.2, 1.8); p.lineTo(12.2, 4.8); p.lineTo(12.2, 14.2); p.lineTo(4, 14.2);
        p.closeSubpath();
        p.moveTo(9, 1.8); p.lineTo(9, 5.1); p.lineTo(12.2, 5.1);
        break;
    case Icon::Trash:
        s.viewBox = 16; s.stroke = 1.4;
        p.moveTo(2.8, 4.3); p.lineTo(13.2, 4.3);
        p.moveTo(6.3, 4.3); p.lineTo(6.3, 2.8); p.lineTo(9.7, 2.8); p.lineTo(9.7, 4.3);
        p.moveTo(4.2, 4.3); p.lineTo(4.8, 13.2); p.lineTo(11.2, 13.2); p.lineTo(11.8, 4.3);
        break;
    case Icon::Reveal:
        // "Show in Explorer": un cuadro con una flecha que sale.
        s.viewBox = 16; s.stroke = 1.4;
        p.moveTo(9, 2.5); p.lineTo(13.5, 2.5); p.lineTo(13.5, 7);
        p.moveTo(13.5, 2.5); p.lineTo(7.5, 8.5);
        p.moveTo(11.5, 9.5); p.lineTo(11.5, 12.7);
        p.cubicTo(11.5, 13.1, 11.1, 13.5, 10.7, 13.5);
        p.lineTo(3.3, 13.5);
        p.cubicTo(2.9, 13.5, 2.5, 13.1, 2.5, 12.7);
        p.lineTo(2.5, 5.3);
        p.cubicTo(2.5, 4.9, 2.9, 4.5, 3.3, 4.5);
        p.lineTo(6.5, 4.5);
        break;
    case Icon::Refresh:
        // "Rescan": un arco casi cerrado con su punta.
        s.viewBox = 16; s.stroke = 1.4;
        p.moveTo(13, 8);
        p.arcTo(QRectF(3, 3, 10, 10), 0, -314);
        p.moveTo(13, 2.3); p.lineTo(13, 5.5); p.lineTo(9.8, 5.5);
        break;
    case Icon::Shield:
        s.viewBox = 16; s.stroke = 1.4;
        p.moveTo(8, 1.8); p.lineTo(13, 3.6); p.lineTo(13, 7.8);
        p.cubicTo(13, 10.8, 10.9, 13, 8, 14.2);
        p.cubicTo(5.1, 13, 3, 10.8, 3, 7.8);
        p.lineTo(3, 3.6);
        p.closeSubpath();
        break;
    case Icon::List:
        // Tres rayas de mayor a menor: una lista ordenada por peso.
        s.viewBox = 16; s.stroke = 1.4;
        p.moveTo(2.5, 4); p.lineTo(13.5, 4);
        p.moveTo(2.5, 8); p.lineTo(10, 8);
        p.moveTo(2.5, 12); p.lineTo(7, 12);
        break;
    case Icon::Check:
        s.viewBox = 16; s.stroke = 2.0;
        p.moveTo(3.5, 8.3); p.lineTo(6.6, 11.3); p.lineTo(12.5, 4.8);
        break;
    case Icon::Dash:
        s.viewBox = 16; s.stroke = 2.0;
        p.moveTo(4, 8); p.lineTo(12, 8);
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
    updateTip();
    updateGeometry();
    update();
}

void ElidedLabel::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateTip();
}

void ElidedLabel::updateTip()
{
    const bool cut = !m_text.isEmpty() && QFontMetricsF(font()).horizontalAdvance(m_text) > width();
    CustomTooltip::instance()->setToolTip(this, cut ? m_text.toHtmlEscaped() : QString());
}

void ElidedLabel::setElideMode(Qt::TextElideMode mode)
{
    m_mode = mode;
    update();
}

void ElidedLabel::setAlignment(Qt::Alignment alignment)
{
    m_alignment = alignment & Qt::AlignHorizontal_Mask;
    update();
}

QString ElidedLabel::shownText() const
{
    // Medido con decimales: con el tamano de interfaz 1 o 2 el ancho entero redondeado de sizeHint() podia
    // quedar por debajo del exacto y el texto se cortaba en su ultima letra aunque entrara.
    const QFontMetricsF metrics(font());
    return metrics.horizontalAdvance(m_text) <= width() ? m_text : metrics.elidedText(m_text, m_mode, width());
}

QSize ElidedLabel::sizeHint() const
{
    const QFontMetrics fm(font());
    return QSize(qCeil(QFontMetricsF(font()).horizontalAdvance(m_text)), fm.height());
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
    painter.drawText(rect(), m_alignment | Qt::AlignVCenter, shownText());
}

// ------------------------------------------------------------------ CaptionLabel

CaptionLabel::CaptionLabel(const QString &text, QWidget *parent, int lineHeight)
    : QLabel(text, parent)
    , m_lineHeight(lineHeight)
{
    setObjectName(QStringLiteral("caption"));
    setWordWrap(true);
    setTextFormat(Qt::PlainText);
}

int CaptionLabel::lineCount(int width) const
{
    if (text().isEmpty()) {
        return 1;
    }
    QTextLayout layout(text(), font());
    QTextOption option;
    option.setWrapMode(wordWrap() ? QTextOption::WordWrap : QTextOption::NoWrap);
    layout.setTextOption(option);
    int lines = 0;
    layout.beginLayout();
    for (QTextLine line = layout.createLine(); line.isValid(); line = layout.createLine()) {
        line.setLineWidth(qMax(1, width));
        ++lines;
    }
    layout.endLayout();
    return qMax(1, lines);
}

int CaptionLabel::heightForWidth(int width) const
{
    const QMargins m = contentsMargins();
    return lineCount(width - m.left() - m.right()) * m_lineHeight + m.top() + m.bottom();
}

QSize CaptionLabel::sizeHint() const
{
    const QMargins m = contentsMargins();
    const int natural = QFontMetrics(font()).horizontalAdvance(text()) + 1;
    return QSize(natural + m.left() + m.right(), m_lineHeight + m.top() + m.bottom());
}

QSize CaptionLabel::minimumSizeHint() const
{
    const QMargins m = contentsMargins();
    return QSize(wordWrap() ? 0 : sizeHint().width(), m_lineHeight + m.top() + m.bottom());
}

void CaptionLabel::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setPen(palette().color(foregroundRole()));
    const QRect area = contentsRect();
    QTextLayout layout(text(), font());
    QTextOption option;
    option.setWrapMode(wordWrap() ? QTextOption::WordWrap : QTextOption::NoWrap);
    layout.setTextOption(option);
    layout.beginLayout();
    int index = 0;
    for (QTextLine line = layout.createLine(); line.isValid(); line = layout.createLine()) {
        line.setLineWidth(qMax(1, area.width()));
        // Como CSS: la caja de cada linea mide m_lineHeight y el texto queda centrado en ella.
        const qreal halfLeading = (m_lineHeight - (line.ascent() + line.descent())) / 2.0;
        line.setPosition(QPointF(0, index * m_lineHeight + halfLeading));
        ++index;
    }
    layout.endLayout();
    layout.draw(&painter, area.topLeft());
}

// ------------------------------------------------------------------ RichLineLabel

RichLineLabel::RichLineLabel(const QString &html, int lineHeight, QWidget *parent)
    : QLabel(html, parent)
    , m_lineHeight(lineHeight)
{
    setTextFormat(Qt::RichText);
    setWordWrap(true);
    // El alto sale del ancho (lineas partidas): el layout lo pregunta por la politica.
    QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
}

void RichLineLabel::layoutDocument(QTextDocument &document, int width) const
{
    document.setDefaultFont(font());
    document.setDocumentMargin(0);
    document.setHtml(text());
    QTextCursor cursor(&document);
    cursor.select(QTextCursor::Document);
    QTextBlockFormat format;
    format.setLineHeight(m_lineHeight, QTextBlockFormat::FixedHeight);
    cursor.mergeBlockFormat(format);
    document.setTextWidth(qMax(1, width));
}

int RichLineLabel::heightForWidth(int width) const
{
    QTextDocument document;
    layoutDocument(document, width);
    document.size(); // el layout del documento es perezoso: sin esto las lineas todavia no existen
    int lines = 0;
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        lines += qMax(1, block.layout() ? block.layout()->lineCount() : 1);
    }
    return qMax(1, lines) * m_lineHeight;
}

QSize RichLineLabel::sizeHint() const
{
    QTextDocument document;
    layoutDocument(document, QWIDGETSIZE_MAX / 4);
    return QSize(int(document.idealWidth()) + 1, m_lineHeight);
}

QSize RichLineLabel::minimumSizeHint() const
{
    return QSize(0, m_lineHeight);
}

void RichLineLabel::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    QTextDocument document;
    layoutDocument(document, width());
    // Con altura fija Qt pone la linea arriba de su caja: se baja la mitad del aire, como CSS.
    const QFontMetricsF metrics(font());
    const qreal halfLeading = (m_lineHeight - (metrics.ascent() + metrics.descent())) / 2.0;
    painter.translate(0, qMax(0.0, halfLeading));
    QAbstractTextDocumentLayout::PaintContext context;
    context.palette = palette();
    context.palette.setColor(QPalette::Text, palette().color(foregroundRole()));
    document.documentLayout()->draw(&painter, context);
}

// ------------------------------------------------------------------ Chip

Chip::Chip(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("chip"));
    // 6 + el borde de 1 = los 7 de `.chip`/`.kc` del canvas (padding 0 6px con borde).
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 0, 6, 0);
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

// ------------------------------------------------------------------ LinkLabel

LinkLabel::LinkLabel(const QString &text, const QString &url, QWidget *parent)
    : QLabel(text, parent)
    , m_url(url)
{
    setObjectName(QStringLiteral("linkLabel"));
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover);
    // A donde lleva, solo si el texto no lo dice ya ("github.com/x" con "https://github.com/x").
    QString bare = url;
    for (const char *scheme : {"https://", "http://"}) {
        if (bare.startsWith(QLatin1String(scheme))) {
            bare.remove(0, int(qstrlen(scheme)));
        }
    }
    if (bare != text && url != text) {
        CustomTooltip::instance()->setToolTip(this, url.toHtmlEscaped());
    }
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

bool LinkLabel::event(QEvent *event)
{
    if (event->type() == QEvent::HoverEnter || event->type() == QEvent::HoverLeave) {
        Ui::setStyleProperty(this, "hover", event->type() == QEvent::HoverEnter);
    }
    return QLabel::event(event);
}

void LinkLabel::mouseReleaseEvent(QMouseEvent *event)
{
    // Solo un click real del usuario llega aca: las corridas automatizadas nunca abren links.
    if (event->button() == Qt::LeftButton && rect().contains(event->pos())) {
        QDesktopServices::openUrl(QUrl(m_url));
    }
    QLabel::mouseReleaseEvent(event);
}

// ------------------------------------------------------------------ SegmentedSwitch

SegmentedSwitch::SegmentedSwitch(const QStringList &labels, int current, QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("segmentSwitch"));
    // Sin esto un QWidget comun no pinta el fondo, el borde ni el radio de la hoja de estilo.
    setAttribute(Qt::WA_StyledBackground, true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    // 24 de alto como el desplegable: borde de 1, aire de 1 y segmentos de 20.
    setFixedHeight(24);
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(2, 2, 2, 2);
    row->setSpacing(1);
    for (int i = 0; i < labels.size(); ++i) {
        auto *segment = new QPushButton(labels.at(i), this);
        segment->setObjectName(QStringLiteral("segment"));
        segment->setCheckable(true);
        segment->setFocusPolicy(Qt::NoFocus);
        segment->setCursor(Qt::PointingHandCursor);
        row->addWidget(segment);
        m_segments.append(segment);
        connect(segment, &QPushButton::clicked, this, [this, i]() {
            if (i == m_current) {
                // Un click en la elegida la destildaria: queda como estaba.
                m_segments.at(i)->setChecked(true);
                return;
            }
            setCurrent(i);
            emit currentChanged(i);
        });
    }
    // Todos del ancho del mas ancho: con digitos ("0", "1", "2") cada uno mediria distinto.
    int widest = 0;
    for (QPushButton *segment : m_segments) {
        widest = qMax(widest, segment->sizeHint().width());
    }
    for (QPushButton *segment : m_segments) {
        segment->setMinimumWidth(widest);
    }
    setCurrent(current);
}

void SegmentedSwitch::setSegmentEnabled(int index, bool enabled)
{
    if (QPushButton *segment = m_segments.value(index)) {
        segment->setEnabled(enabled);
        segment->setCursor(enabled ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }
}

void SegmentedSwitch::setCurrent(int index)
{
    m_current = index;
    for (int i = 0; i < m_segments.size(); ++i) {
        m_segments.at(i)->setChecked(i == index);
    }
}

// ------------------------------------------------------------------ ToggleSwitch

ToggleSwitch::ToggleSwitch(QWidget *parent)
    : QAbstractButton(parent)
{
    setCheckable(true);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(30, 17);
}

QSize ToggleSwitch::sizeHint() const
{
    return QSize(30, 17);
}

void ToggleSwitch::nextCheckState()
{
    // Todo click del usuario pasa por aca (mouse, o click() desde el texto de su SwitchRow). Con el
    // mouse, Qt pone blockRefresh y su setChecked NO llama a checkStateSet() (qabstractbutton.cpp,
    // igual que QCheckBox): la animacion se arranca aca, al volver de la base. El guard cubre un
    // handler de toggled que borre el switch.
    QPointer<ToggleSwitch> guard(this);
    m_byUser = true;
    QAbstractButton::nextCheckState();
    if (!guard) {
        return;
    }
    m_byUser = false;
    animateTo(isChecked() ? 1.0 : 0.0);
}

void ToggleSwitch::checkStateSet()
{
    // Un setChecked() del codigo: refleja un estado y salta al final. Si repite el destino de una
    // animacion en marcha (un panel que refresca apenas se hizo click), la deja seguir.
    if (m_byUser) {
        return;
    }
    const qreal target = isChecked() ? 1.0 : 0.0;
    if (m_anim && m_anim->state() == QAbstractAnimation::Running && target == m_target) {
        return;
    }
    if (m_anim) {
        m_anim->stop();
    }
    m_knob = m_tint = m_target = target;
    update();
}

void ToggleSwitch::animateTo(qreal target)
{
    m_target = target;
    if (!isVisible()) {
        if (m_anim) {
            m_anim->stop();
        }
        m_knob = m_tint = target;
        update();
        return;
    }
    if (!m_anim) {
        m_anim = new QVariantAnimation(this);
        m_anim->setStartValue(0.0);
        m_anim->setEndValue(1.0);
        m_anim->setDuration(160);
        // El tiempo corre lineal: el color se funde parejo y la perilla toma la curva en valueChanged.
        connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            // cubic-bezier(.2, .8, .2, 1) del mockup: arranca rapido y frena al llegar, sin rebote.
            static const QEasingCurve knobCurve = [] {
                QEasingCurve curve(QEasingCurve::BezierSpline);
                curve.addCubicBezierSegment(QPointF(0.2, 0.8), QPointF(0.2, 1.0), QPointF(1.0, 1.0));
                return curve;
            }();
            const qreal f = value.toReal();
            m_knob = m_knobFrom + (m_target - m_knobFrom) * knobCurve.valueForProgress(f);
            m_tint = m_tintFrom + (m_target - m_tintFrom) * f;
            update();
        });
    }
    // Un click en medio de una animacion arranca la vuelta desde donde esta.
    m_anim->stop();
    m_knobFrom = m_knob;
    m_tintFrom = m_tint;
    m_anim->start();
}

void ToggleSwitch::paintEvent(QPaintEvent *)
{
    // Medidas del canvas: caja 30 x 17 con borde de 1 y radio 9; perilla de 11 a 2 px del borde
    // interno (left 2 apagado, left 15 prendido). Durante la animacion los colores se mezclan por
    // m_tint y la perilla va por m_knob; quieto, son los tokens de siempre.
    const auto mix = [this](const char *off, const char *on) {
        const QColor a = Theme::color(off);
        const QColor b = Theme::color(on);
        const qreal t = m_tint;
        return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                                a.blueF() + (b.blueF() - a.blueF()) * t, a.alphaF() + (b.alphaF() - a.alphaF()) * t);
    };
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(QPen(mix(Theme::kSwitchOffBorder, Theme::kPrimaryBorder), 1.0));
    painter.setBrush(mix(Theme::kSwitchOff, Theme::kPrimary));
    painter.drawRoundedRect(box, 8.5, 8.5);
    painter.setPen(Qt::NoPen);
    painter.setBrush(mix(Theme::kSwitchKnobOff, Theme::kSwitchKnobOn));
    const qreal left = 1 + 2 + 13 * m_knob;
    painter.drawEllipse(QRectF(left, 1 + 2, 11, 11));
}

// ------------------------------------------------------------------ SwitchRow

SwitchRow::SwitchRow(const QString &text, const QString &caption, QWidget *parent)
    : QWidget(parent)
{
    setCursor(Qt::PointingHandCursor);
    m_row = new QHBoxLayout(this);
    m_row->setContentsMargins(0, 0, 0, 0);
    m_row->setSpacing(12);
    auto *texts = new QVBoxLayout();
    texts->setContentsMargins(0, 0, 0, 0);
    texts->setSpacing(3);
    // Con wordWrap el texto no fija el ancho minimo de la fila: si no entra (espanol, tamano 2, controles
    // sumados con addTrailingWidget) baja de linea en vez de empujar el switch fuera de la pagina.
    QLabel *title = Ui::label(text, "optionLabel", this);
    title->setWordWrap(true);
    texts->addWidget(title);
    if (!caption.isEmpty()) {
        m_caption = Ui::caption(caption, this);
        m_caption->setWordWrap(true);
        texts->addWidget(m_caption);
    }
    m_row->addLayout(texts, 1);
    m_switch = new ToggleSwitch(this);
    m_switch->setAccessibleName(text);
    m_row->addWidget(m_switch, 0, Qt::AlignVCenter);
    connect(m_switch, &QAbstractButton::clicked, this, &SwitchRow::clicked);
}

bool SwitchRow::isChecked() const
{
    return m_switch->isChecked();
}

void SwitchRow::setChecked(bool checked)
{
    const QSignalBlocker block(m_switch);
    m_switch->setChecked(checked);
}

void SwitchRow::addTrailingWidget(QWidget *widget)
{
    m_row->insertWidget(m_row->indexOf(m_switch), widget, 0, Qt::AlignVCenter);
    m_trailing.append(widget);
    // La mano de la fila solo donde un click hace algo.
    if (!qobject_cast<QAbstractButton *>(widget)) {
        widget->setCursor(Qt::ArrowCursor);
    }
}

void SwitchRow::mouseReleaseEvent(QMouseEvent *event)
{
    // Click en el texto o en el aire de la fila: como un click en el switch. Lo que se sumo con
    // addTrailingWidget (un resultado, un boton) no cambia la opcion.
    const QPoint pos = event->position().toPoint();
    QWidget *hit = childAt(pos);
    for (QWidget *trailing : std::as_const(m_trailing)) {
        if (hit && (hit == trailing || trailing->isAncestorOf(hit))) {
            QWidget::mouseReleaseEvent(event);
            return;
        }
    }
    if (event->button() == Qt::LeftButton && isEnabled() && rect().contains(pos)) {
        m_switch->click();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

// ------------------------------------------------------------------ AppIconTile

AppIconTile::AppIconTile(QWidget *parent)
    : QWidget(parent)
{
    setFixedSize(31, 31);
}

void AppIconTile::setIcon(const QIcon &icon)
{
    m_icon = icon;
    update();
}

void AppIconTile::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Theme::color(Theme::kBorderStrong));
    painter.setBrush(Theme::color(Theme::kWindow));
    painter.drawRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5), 3, 3);
    const QRectF box(QRectF(rect()).center() - QPointF(10, 10), QSizeF(20, 20));
    if (!m_icon.isNull()) {
        paintIcon(painter, box.toRect());
        return;
    }
    painter.setPen(QPen(Theme::color(Theme::kTextMuted), 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    const QRectF frame = box.adjusted(2.5, 3.5, -2.5, -3.5);
    painter.drawRoundedRect(frame, 1.5, 1.5);
    painter.drawLine(QPointF(frame.left(), frame.top() + 3.2), QPointF(frame.right(), frame.top() + 3.2));
}

// QIcon elige el cuadro por el tamano logico (20) y despues lo estira a la densidad de la pantalla: a 200 o
// 300 % el icono salia borroso. Aca se elige por pixeles reales (el cuadro mas chico que alcanza) y se achica
// suave. Sin tamanos a la vista (el icono de mac lo dibuja el sistema), QIcon como siempre.
void AppIconTile::paintIcon(QPainter &painter, const QRect &target) const
{
    const QList<QSize> sizes = m_icon.availableSizes();
    if (sizes.isEmpty()) {
        m_icon.paint(&painter, target);
        return;
    }
    // La escala real de lo que se pinta: la de la pantalla en la app, la del pixmap en una captura a --dpr 3
    // (ahi el widget sigue creyendo que su pantalla es de 1).
    const qreal dpr = qMax<qreal>(1.0, painter.deviceTransform().m11());
    const int device = qRound(target.width() * dpr);
    QSize pick;
    for (const QSize &size : sizes) {
        const bool fits = size.width() >= device;
        if (!pick.isValid() || (fits && (pick.width() < device || size.width() < pick.width()))
            || (!fits && pick.width() < device && size.width() > pick.width())) {
            pick = size;
        }
    }
    QPixmap pixmap = m_icon.pixmap(pick, 1.0);
    if (pixmap.width() != device) {
        pixmap = pixmap.scaled(device, device, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    pixmap.setDevicePixelRatio(dpr);
    painter.drawPixmap(target.topLeft(), pixmap);
}

// ------------------------------------------------------------------ StatusDot

StatusDot::StatusDot(int diameter, QWidget *parent)
    : QWidget(parent)
{
    setFixedSize(diameter, diameter);
    setAttribute(Qt::WA_TransparentForMouseEvents);
}

void StatusDot::setTone(const QString &tone)
{
    if (tone == m_tone) {
        return;
    }
    m_tone = tone;
    update();
}

void StatusDot::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r = QRectF(rect());
    if (m_tone == QLatin1String("off")) {
        // Hueco con borde de 1.5 px, como `.dot.off` del canvas.
        painter.setPen(QPen(Theme::color(Theme::kDotOffBorder), 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(r.adjusted(0.75, 0.75, -0.75, -0.75));
        return;
    }
    const char *hex = Theme::kOk;
    if (m_tone == QLatin1String("paused")) {
        hex = Theme::kDotPaused;
    } else if (m_tone == QLatin1String("warn")) {
        hex = Theme::kWarn;
    } else if (m_tone == QLatin1String("err")) {
        hex = Theme::kError;
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(Theme::color(hex));
    painter.drawEllipse(r);
}

// ------------------------------------------------------------------ StatusCard

StatusCard::StatusCard(QWidget *parent)
    : QFrame(parent)
{
    // Medidas del canvas (`.status`): 10 x 14 de relleno, 12 entre piezas, 58 de alto minimo.
    setObjectName(QStringLiteral("card"));
    setMinimumHeight(58);
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(14, 10, 14, 10);
    row->setSpacing(12);
    m_dot = Ui::label(QString(), "statusDot", this);
    row->addWidget(m_dot, 0, Qt::AlignVCenter);
    auto *texts = new QVBoxLayout();
    texts->setSpacing(2);
    m_title = Ui::label(QString(), "cardTitle", this);
    // Con ajuste de linea: un titulo largo (otro idioma) baja a una segunda linea en vez de ensanchar la
    // tarjeta y empujar el boton fuera de la pagina.
    m_title->setWordWrap(true);
    m_text = Ui::caption(QString(), this);
    texts->addWidget(m_title);
    texts->addWidget(m_text);
    row->addLayout(texts, 1);
    m_button = Ui::button(QString(), QString(), QString(), this);
    m_button->setObjectName(QStringLiteral("statusButton"));
    row->addWidget(m_button, 0, Qt::AlignVCenter);
}

void StatusCard::set(const QString &dot, const QString &title, const QString &text, const QString &button,
                     const QString &buttonVariant, const QString &cardTone, const QString &buttonSize)
{
    Ui::setStyleProperty(m_dot, "state", dot);
    Ui::setStyleProperty(this, "tone", cardTone);
    m_title->setText(title);
    m_text->setText(text);
    m_button->setText(button);
    m_button->setVisible(!button.isEmpty());
    // Boton de 78 de ancho minimo (`.btn`), salvo el chico (`.btn.sm`).
    m_button->setMinimumWidth(buttonSize.isEmpty() ? 78 : 0);
    Ui::setStyleProperty(m_button, "variant", buttonVariant);
    Ui::setStyleProperty(m_button, "btnSize", buttonSize);
}

QString StatusCard::title() const
{
    return m_title->text();
}

// ------------------------------------------------------------------ Ui

CurrentPageStack::CurrentPageStack(QWidget *parent) : QWidget(parent)
{
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
}

void CurrentPageStack::addWidget(QWidget *page)
{
    layout()->addWidget(page);
    if (m_current) page->hide();
    else m_current = page;
}

void CurrentPageStack::setCurrentWidget(QWidget *page)
{
    if (m_current == page) return;
    m_current->hide();
    m_current = page;
    m_current->show();
    layout()->invalidate();
}

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

void setDropdownArrow(QPushButton *button)
{
    constexpr int kGlyph = 8;
    constexpr int kGap = 6;
    // El aire va adentro del icono, a la izquierda del dibujo: con RightToLeft el icono queda a la derecha
    // del texto y esa franja transparente es la separacion.
    const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    QPixmap pixmap(QSize(kGap + kGlyph, kGlyph) * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    Icons::paint(painter, Icon::ChevronDown, QRectF(kGap, 0, kGlyph, kGlyph), Theme::color(Theme::kTextMuted));
    painter.end();
    button->setLayoutDirection(Qt::RightToLeft);
    button->setIcon(QIcon(pixmap));
    button->setIconSize(QSize(kGap + kGlyph, kGlyph));
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

QLabel *label(const QString &text, const char *objectName, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setObjectName(QLatin1String(objectName));
    return l;
}

QLabel *caption(const QString &text, QWidget *parent)
{
    return new CaptionLabel(text, parent);
}

QFrame *card(QWidget *parent)
{
    auto *frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("card"));
    return frame;
}

void addDivider(QBoxLayout *layout, QWidget *parent)
{
    layout->addSpacing(8);
    auto *divider = new QFrame(parent);
    divider->setObjectName(QStringLiteral("divider"));
    layout->addWidget(divider);
    layout->addSpacing(8);
}

} // namespace Ui
