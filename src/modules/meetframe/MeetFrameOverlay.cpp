#include "modules/meetframe/MeetFrameOverlay.h"

#include "core/I18n.h"
#include "ui/CustomTooltip.h"
#include "ui/Theme.h"
#include "ui/UiWidgets.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QWindow>

namespace {

constexpr int kBand = 8;     // franja alrededor de la region: de ahi se estira el marco
constexpr int kLine = 2;     // el borde que se ve, pegado a la region por fuera
constexpr int kBarH = 30;    // la barrita de arriba
constexpr int kBarRadius = 6;

// Tooltip con el atajo en la linea de abajo (formato comun de los tooltips de LGA).
QString withKeys(const QString &text, const QString &keys)
{
    if (keys.isEmpty()) return text.toHtmlEscaped();
    return text.toHtmlEscaped() + QStringLiteral("<div align='center'><span style='color:#E8E8E8'><b>%1</b></span></div>").arg(keys.toHtmlEscaped());
}

} // namespace

MeetFrameOverlay::MeetFrameOverlay(bool capture, QWidget *parent)
    : QWidget(parent), m_capture(capture)
{
    setObjectName(QStringLiteral("meetFrameOverlay"));
    if (!capture) {
        setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus
                       | Qt::NoDropShadowWindowHint);
        setAttribute(Qt::WA_ShowWithoutActivating);
    }
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);
    setWindowTitle(QStringLiteral("Meet Frame"));

    m_bar = new QWidget(this);
    m_bar->setObjectName(QStringLiteral("meetFrameBar"));
    m_bar->setAttribute(Qt::WA_TranslucentBackground);
    auto *row = new QHBoxLayout(m_bar);
    row->setContentsMargins(10, 0, 4, 0);
    row->setSpacing(6);
    m_dot = new StatusDot(8, m_bar);
    row->addWidget(m_dot, 0, Qt::AlignVCenter);
    m_title = Ui::label(QStringLiteral("Meet Frame"), "itemName", m_bar);
    row->addWidget(m_title, 0, Qt::AlignVCenter);
    m_size = Ui::label(QString(), "meta", m_bar);
    row->addWidget(m_size, 0, Qt::AlignVCenter);
    m_hidden = Ui::label(QString(), "meta", m_bar);
    row->addWidget(m_hidden, 0, Qt::AlignVCenter);
    row->addSpacing(4);
    m_pause = Ui::button(I18n::tr("Pause"), QString(), QStringLiteral("sm"), m_bar);
    row->addWidget(m_pause, 0, Qt::AlignVCenter);
    m_sizeButton = Ui::button(I18n::tr("Size"), QString(), QStringLiteral("sm"), m_bar);
    Ui::setDropdownArrow(m_sizeButton);
    row->addWidget(m_sizeButton, 0, Qt::AlignVCenter);
    m_close = Ui::button(QString(), QStringLiteral("ghost"), QStringLiteral("icon"), m_bar);
    Ui::setIcon(m_close, Icon::X, Theme::color(Theme::kIcon), 12);
    m_close->setAccessibleName(I18n::tr("Hide the frame"));
    row->addWidget(m_close, 0, Qt::AlignVCenter);
    for (QPushButton *button : {m_pause, m_sizeButton, m_close}) button->setFocusPolicy(Qt::NoFocus);

    m_sizeMenu = new QMenu(this);
    const QList<QSize> sizes = {QSize(1280, 720), QSize(1600, 900), QSize(1920, 1080)};
    for (const QSize &size : sizes) {
        QAction *action = m_sizeMenu->addAction(QStringLiteral("%1 × %2").arg(size.width()).arg(size.height()));
        connect(action, &QAction::triggered, this, [this, size]() { emit sizeRequested(size); });
    }
    m_sizeMenu->addSeparator();
    connect(m_sizeMenu->addAction(I18n::tr("Center on the screen")), &QAction::triggered, this, [this]() { emit sizeRequested(QSize()); });

    if (!capture) {
        connect(m_pause, &QPushButton::clicked, this, &MeetFrameOverlay::pauseClicked);
        connect(m_close, &QPushButton::clicked, this, &MeetFrameOverlay::closeClicked);
        connect(m_sizeButton, &QPushButton::clicked, this, [this]() {
            m_sizeMenu->popup(m_sizeButton->mapToGlobal(QPoint(0, m_sizeButton->height() + 2)));
        });
    }
    m_settle.setSingleShot(true);
    m_settle.setInterval(400);
    connect(&m_settle, &QTimer::timeout, this, &MeetFrameOverlay::regionSettled);

    setMinimumSize(minimumRegion().grownBy(frameMargins()));
    setState(false, 0, QSize(), QString());
    updateTips();
}

QMargins MeetFrameOverlay::frameMargins()
{
    return QMargins(kBand, kBarH + kBand, kBand, kBand);
}

QSize MeetFrameOverlay::minimumRegion()
{
    return QSize(380, 200);
}

QRect MeetFrameOverlay::region() const
{
    return geometry().marginsRemoved(frameMargins());
}

void MeetFrameOverlay::setRegion(const QRect &region)
{
    setGeometry(region.marginsAdded(frameMargins()));
}

void MeetFrameOverlay::setState(bool paused, int hiddenWindows, const QSize &pixels, const QString &error)
{
    m_paused = paused;
    m_error = error;
    m_dot->setTone(!error.isEmpty() ? QStringLiteral("err") : paused ? QStringLiteral("warn") : QStringLiteral("ok"));
    m_size->setText(pixels.isValid() ? QStringLiteral("%1 × %2").arg(pixels.width()).arg(pixels.height()) : QString());
    m_hidden->setText(hiddenWindows > 0 ? (hiddenWindows == 1 ? I18n::tr("1 hidden") : I18n::tr("%1 hidden").arg(hiddenWindows)) : QString());
    m_hidden->setVisible(hiddenWindows > 0);
    m_pause->setText(paused ? I18n::tr("Resume") : I18n::tr("Pause"));
    updateTips();
    m_bar->adjustSize();
    update();
}

void MeetFrameOverlay::setShortcuts(const QString &toggle, const QString &pause)
{
    m_toggleKeys = toggle;
    m_pauseKeys = pause;
    updateTips();
}

void MeetFrameOverlay::updateTips()
{
    CustomTooltip::instance()->setToolTip(m_pause, m_paused ? withKeys(I18n::tr("People see the picture move again."), m_pauseKeys)
                                                            : withKeys(I18n::tr("Freezes what people see."), m_pauseKeys));
    CustomTooltip::instance()->setToolTip(m_close, withKeys(I18n::tr("Hide the frame"), m_toggleKeys));
    CustomTooltip::instance()->setToolTip(m_hidden, I18n::tr("Windows covered in what you share right now."));
}

Qt::Edges MeetFrameOverlay::edgesAt(const QPoint &pos) const
{
    const QRect inner = rect().marginsRemoved(frameMargins());
    Qt::Edges edges;
    if (pos.y() < kBarH) {
        return edges; // la barrita (o el aire a su lado, que no recibe clicks)
    }
    if (pos.x() < inner.left()) edges |= Qt::LeftEdge;
    if (pos.x() > inner.right()) edges |= Qt::RightEdge;
    if (pos.y() < inner.top()) edges |= Qt::TopEdge;
    if (pos.y() > inner.bottom()) edges |= Qt::BottomEdge;
    return edges;
}

void MeetFrameOverlay::mousePressEvent(QMouseEvent *event)
{
    if (m_capture || event->button() != Qt::LeftButton || !windowHandle()) {
        return;
    }
    const QPoint pos = event->position().toPoint();
    if (m_bar->geometry().contains(pos)) {
        windowHandle()->startSystemMove();
        return;
    }
    const Qt::Edges edges = edgesAt(pos);
    if (edges) windowHandle()->startSystemResize(edges);
}

void MeetFrameOverlay::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint pos = event->position().toPoint();
    const Qt::Edges edges = edgesAt(pos);
    Qt::CursorShape shape = Qt::ArrowCursor;
    if (m_bar->geometry().contains(pos)) shape = Qt::SizeAllCursor;
    else if (edges == (Qt::LeftEdge | Qt::TopEdge) || edges == (Qt::RightEdge | Qt::BottomEdge)) shape = Qt::SizeFDiagCursor;
    else if (edges == (Qt::RightEdge | Qt::TopEdge) || edges == (Qt::LeftEdge | Qt::BottomEdge)) shape = Qt::SizeBDiagCursor;
    else if (edges & (Qt::LeftEdge | Qt::RightEdge)) shape = Qt::SizeHorCursor;
    else if (edges & (Qt::TopEdge | Qt::BottomEdge)) shape = Qt::SizeVerCursor;
    setCursor(shape);
}

void MeetFrameOverlay::leaveEvent(QEvent *event)
{
    unsetCursor();
    QWidget::leaveEvent(event);
}

void MeetFrameOverlay::moveEvent(QMoveEvent *event)
{
    QWidget::moveEvent(event);
    emit regionChanged();
    m_settle.start();
}

void MeetFrameOverlay::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    m_bar->adjustSize();
    m_bar->setGeometry(kBand - kLine, 0, m_bar->sizeHint().width(), kBarH - 4);
    updateMask();
    emit regionChanged();
    m_settle.start();
}

void MeetFrameOverlay::updateMask()
{
    // Solo la franja del borde y la barrita reciben el mouse: adentro y al costado de la barrita, los clicks
    // pasan a las ventanas de abajo.
    const QRect inner = rect().marginsRemoved(frameMargins());
    QRegion mask = QRegion(rect().adjusted(0, kBarH, 0, 0)).subtracted(QRegion(inner));
    mask += QRegion(m_bar->geometry());
    setMask(mask);
}

void MeetFrameOverlay::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRect inner = rect().marginsRemoved(frameMargins());
    // La franja casi invisible: con alfa 0 Windows deja pasar el click; con 1, el marco lo recibe.
    QRegion band = QRegion(rect().adjusted(0, kBarH, 0, 0)).subtracted(QRegion(inner));
    painter.save();
    painter.setClipRegion(band);
    painter.fillRect(rect(), QColor(0, 0, 0, 1));
    painter.restore();
    // El borde, por fuera de la region para que nunca salga en lo compartido.
    const QColor line(!m_error.isEmpty() ? Theme::kError : m_paused ? Theme::kWarn : Theme::kOk);
    painter.setPen(Qt::NoPen);
    painter.setBrush(line);
    QRegion ring = QRegion(inner.adjusted(-kLine, -kLine, kLine, kLine)).subtracted(QRegion(inner));
    painter.setClipRegion(ring);
    painter.drawRect(rect());
    painter.setClipping(false);
    // La barrita: tarjeta de la app con el color del borde a la izquierda.
    const QRectF bar = QRectF(m_bar->geometry()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(QPen(QColor(Theme::kCardBorder), 1));
    painter.setBrush(QColor(Theme::kCard));
    painter.drawRoundedRect(bar, kBarRadius, kBarRadius);
}
