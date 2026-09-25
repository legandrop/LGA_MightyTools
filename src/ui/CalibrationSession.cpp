#include "ui/CalibrationSession.h"

#include "actions/ActionRunner.h"
#include "platform/InputInjector.h"
#include "platform/NukeWatcher.h"
#include "platform/SystemInput.h"
#include "ui/Theme.h"
#include "ui/UiWidgets.h"

#include <QCursor>
#include <QDebug>
#include <QGuiApplication>
#include <QLabel>
#include <QPainter>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>

namespace {

constexpr int kPollMs = 16;
// Despues del click, antes de medir: en macOS el click trae a Nuke al frente y su ventana
// principal recien entonces responde por el API de Accesibilidad.
constexpr int kResolveDelayMs = 150;
constexpr int kBubbleOffset = 20;

} // namespace

// ------------------------------------------------------------------ CalibrationBubble

CalibrationBubble::CalibrationBubble(QWidget *parent)
    : QWidget(parent, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                          | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus)
{
    setObjectName(QStringLiteral("calibrationBubble"));
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedWidth(236);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 9, 12, 9);
    layout->setSpacing(3);
    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("bubbleTitle"));
    m_meta = new QLabel(this);
    m_meta->setObjectName(QStringLiteral("bubbleMeta"));
    layout->addWidget(m_title);
    layout->addWidget(m_meta);
    showOutside(false);
}

void CalibrationBubble::showOverNuke(const QPointF &spot)
{
    m_title->setText(QStringLiteral("Click inside the Dope Sheet"));
    Ui::setStyleProperty(m_title, "tone", QString());
    m_meta->setText(QStringLiteral("%1% · %2% of Nuke    Esc cancels").arg(qRound(spot.x() * 100)).arg(qRound(spot.y() * 100)));
    adjustSize();
}

void CalibrationBubble::showOutside(bool rejected)
{
    m_title->setText(rejected ? QStringLiteral("That's not Nuke. Try again.") : QStringLiteral("Move over the Nuke window"));
    Ui::setStyleProperty(m_title, "tone", rejected ? QStringLiteral("err") : QString());
    m_meta->setText(QStringLiteral("Esc cancels"));
    adjustSize();
}

void CalibrationBubble::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(0x33, 0x33, 0x33), 1.0));
    painter.setBrush(Theme::color(Theme::kCard));
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
}

// ------------------------------------------------------------------ CalibrationSession

CalibrationSession::CalibrationSession(NukeWatcher *watcher, InputInjector *injector, QObject *parent)
    : QObject(parent)
    , m_watcher(watcher)
    , m_injector(injector)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(kPollMs);
    connect(m_timer, &QTimer::timeout, this, &CalibrationSession::poll);
}

CalibrationSession::~CalibrationSession()
{
    delete m_bubble;
}

void CalibrationSession::start()
{
    qInfo() << "[Calibration] Esperando el click sobre el Dope Sheet";
    if (!m_bubble) {
        m_bubble = new CalibrationBubble();
    }
    m_buttonWasDown = true;
    m_resolving = false;
    m_rejected = false;
    m_bubble->showOutside(false);
    placeBubble();
    m_bubble->show();
    m_timer->start();
}

void CalibrationSession::cancel()
{
    m_timer->stop();
    if (m_bubble) {
        m_bubble->hide();
    }
    qInfo() << "[Calibration] Cancelada";
    emit cancelled();
}

void CalibrationSession::placeBubble()
{
    // A la derecha y abajo del puntero; si no entra en la pantalla, del otro lado.
    const QPoint cursor = QCursor::pos();
    QPoint topLeft = cursor + QPoint(kBubbleOffset, kBubbleOffset);
    if (const QScreen *screen = QGuiApplication::screenAt(cursor)) {
        const QRect area = screen->availableGeometry();
        if (topLeft.x() + m_bubble->width() > area.right()) {
            topLeft.setX(cursor.x() - kBubbleOffset - m_bubble->width());
        }
        if (topLeft.y() + m_bubble->height() > area.bottom()) {
            topLeft.setY(cursor.y() - kBubbleOffset - m_bubble->height());
        }
    }
    m_bubble->move(topLeft);
}

void CalibrationSession::poll()
{
    if (SystemInput::escapeDown()) {
        cancel();
        return;
    }
    if (m_resolving) {
        return;
    }
    const QPoint native = m_injector->cursorPos();
    const QRect frame = m_watcher->nukeFrameAt(native);
    if (frame.isValid()) {
        m_rejected = false;
        m_bubble->showOverNuke(ActionRunner::nativeToSpot(frame, native));
    } else {
        m_bubble->showOutside(m_rejected);
    }
    placeBubble();

    const bool down = SystemInput::primaryButtonDown();
    const bool pressed = down && !m_buttonWasDown;
    m_buttonWasDown = down;
    if (pressed) {
        m_resolving = true;
        QTimer::singleShot(kResolveDelayMs, this, [this, native]() { resolveClick(native); });
    }
}

void CalibrationSession::resolveClick(const QPoint &nativePoint)
{
    m_resolving = false;
    const QRect frame = m_watcher->nukeFrameAt(nativePoint);
    if (!frame.isValid()) {
        qInfo() << "[Calibration] Click fuera de Nuke en" << nativePoint;
        m_rejected = true;
        return;
    }
    const QPointF spot = ActionRunner::nativeToSpot(frame, nativePoint);
    qInfo() << "[Calibration] Punto guardado:" << spot << "click" << nativePoint << "marco de Nuke" << frame;
    m_timer->stop();
    m_bubble->hide();
    emit calibrated(spot);
}
