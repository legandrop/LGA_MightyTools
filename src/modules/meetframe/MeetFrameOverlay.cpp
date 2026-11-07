#include "modules/meetframe/MeetFrameOverlay.h"

#include "core/I18n.h"
#include "platform/MeetFrameSystem.h"
#include "ui/CustomTooltip.h"
#include "ui/Theme.h"
#include "ui/UiWidgets.h"

#include <QCoreApplication>
#include <QDebug>
#include <QEnterEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QWindow>

#include <algorithm>

namespace {

constexpr int kBand = 12;    // franja alrededor de la region: de ahi se estira el marco
constexpr int kCorner = 28;  // cerca de una esquina (a lo largo de la franja) se estira en diagonal
constexpr int kSlack = 320;  // lienzo: holgura alrededor del marco (asi casi ningun paso cambia el tamaño del lienzo)
constexpr int kLine = 2;     // el borde que se ve, pegado a la region por fuera
constexpr int kBarH = 30;    // la barrita de arriba
constexpr int kBarRadius = 6;

// mac: Qt 6.5 no implementa startSystemResize y startSystemMove depende de NSApp.currentEvent, que con el click
// diferido de Qt a veces ya no es el mouseDown: el marco se mueve y se estira a mano, siguiendo al mouse.
//
// Lienzo (mac): la ventana del marco es mas grande que el marco (kSlack de holgura, dentro de la pantalla) y el
// marco se dibuja adentro. Mover o estirar solo redibuja las franjas que cambian; cambiar el tamaño de una
// ventana translucida obliga a Qt a crear una superficie nueva y redibujarla entera en cada paso (se veia como un
// arrastre pesado y atrasado). El lienzo cambia solo cuando el marco se le sale. No es la pantalla entera para no
// gastar memoria de mas (cada superficie de una pantalla 5K son ~60 MB). El mouse lo reparte
// MeetFrameSystem::trackFrameMouse: afuera del borde y la barrita, la ventana lo deja pasar.
#ifdef Q_OS_MACOS
constexpr bool kManualDrag = true;
constexpr bool kCanvas = true;
#else
constexpr bool kManualDrag = false;
constexpr bool kCanvas = false;
#endif

// Tooltip con el atajo en la linea de abajo (formato comun de los tooltips de LGA).
QString withKeys(const QString &text, const QString &keys)
{
    if (keys.isEmpty()) return text.toHtmlEscaped();
    return text.toHtmlEscaped() + QStringLiteral("<div align='center'><span style='color:#E8E8E8'><b>%1</b></span></div>").arg(keys.toHtmlEscaped());
}

} // namespace

MeetFrameOverlay::MeetFrameOverlay(bool capture, QWidget *parent)
    : QWidget(parent), m_capture(capture), m_canvas(kCanvas && !capture)
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
    if (m_canvas) connect(qApp, &QGuiApplication::screenRemoved, this, [this]() { QTimer::singleShot(0, this, &MeetFrameOverlay::replaceCanvas); });

    if (!m_canvas) setMinimumSize(minimumRegion().grownBy(frameMargins()));
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

QRect MeetFrameOverlay::frameRect() const
{
    return m_canvas ? m_frame : rect();
}

QRect MeetFrameOverlay::region() const
{
    return frameRect().translated(geometry().topLeft()).marginsRemoved(frameMargins());
}

void MeetFrameOverlay::setRegion(const QRect &region)
{
    setFrameGlobal(region.marginsAdded(frameMargins()));
}

void MeetFrameOverlay::setAspectRatio(const QSize &ratio)
{
    m_aspect = ratio.isValid() && !ratio.isEmpty() ? ratio : QSize();
}

QRegion MeetFrameOverlay::frameArea() const
{
    // Lo que el marco ocupa: la franja (con el borde) y la barrita.
    const QRect frame = frameRect();
    QRegion area = QRegion(frame.adjusted(0, kBarH, 0, 0)).subtracted(QRegion(frame.marginsRemoved(frameMargins())));
    return area + QRegion(m_bar->geometry());
}

void MeetFrameOverlay::setFrameGlobal(const QRect &frame)
{
    if (!m_canvas) {
        setGeometry(frame); // moveEvent y resizeEvent avisan
        return;
    }
    // El lienzo va en la pantalla donde cae el centro del marco y lo contiene con holgura: solo cambia (y se
    // redimensiona) cuando el marco se le sale o pasa a otra pantalla.
    QScreen *screen = QGuiApplication::screenAt(frame.center());
    if (!screen) screen = this->screen() ? this->screen() : QGuiApplication::primaryScreen();
    const QRect screenArea = screen ? screen->geometry() : frame;
    const QRect needed = frame.intersected(screenArea).isEmpty() ? frame : frame.intersected(screenArea);
    QRect canvas = geometry();
    if (!screenArea.contains(canvas) || !canvas.contains(needed)) {
        canvas = needed.adjusted(-kSlack, -kSlack, kSlack, kSlack).intersected(screenArea);
        if (canvas.isEmpty()) canvas = needed;
    }
    watchScreen(screen);
    const QRegion before = frameArea();
    m_frame = frame.translated(-canvas.topLeft());
    if (geometry() != canvas) {
        m_placing = true;
        setGeometry(canvas);
        m_placing = false;
    }
    layoutBar();
    updateMask();
    update(before + frameArea());
    emit regionChanged();
    m_settle.start();
}

void MeetFrameOverlay::watchScreen(QScreen *screen)
{
    // Si la pantalla cambia (resolucion, otro monitor) el lienzo se vuelve a acomodar alrededor del marco.
    if (screen == m_screen) return;
    if (m_screen) disconnect(m_screen, nullptr, this, nullptr);
    m_screen = screen;
    if (screen) connect(screen, &QScreen::geometryChanged, this, &MeetFrameOverlay::replaceCanvas);
}

void MeetFrameOverlay::replaceCanvas()
{
    if (m_canvas && !m_placing && !m_dragging) setFrameGlobal(m_frame.translated(geometry().topLeft()));
}

void MeetFrameOverlay::setState(bool paused, int hiddenWindows, const QSize &pixels, const QString &error)
{
    // Se llama en cada cambio de la region: si lo que se ve no cambio, no se rearma la barrita ni sus tooltips.
    if (m_stateSet && paused == m_paused && hiddenWindows == m_hiddenCount && pixels == m_pixels && error == m_error) return;
    m_stateSet = true;
    m_hiddenCount = hiddenWindows;
    m_pixels = pixels;
    m_paused = paused;
    m_error = error;
    m_dot->setTone(!error.isEmpty() ? QStringLiteral("err") : paused ? QStringLiteral("warn") : QStringLiteral("ok"));
    m_size->setText(pixels.isValid() ? QStringLiteral("%1 × %2").arg(pixels.width()).arg(pixels.height()) : QString());
    m_hidden->setText(hiddenWindows > 0 ? (hiddenWindows == 1 ? I18n::tr("1 hidden") : I18n::tr("%1 hidden").arg(hiddenWindows)) : QString());
    m_hidden->setVisible(hiddenWindows > 0);
    m_pause->setText(paused ? I18n::tr("Resume") : I18n::tr("Pause"));
    updateTips();
    layoutBar();
    updateMask();
    update();
}

void MeetFrameOverlay::setShortcuts(const QString &toggle, const QString &pause)
{
    if (toggle == m_toggleKeys && pause == m_pauseKeys) return;
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
    const QRect frame = frameRect();
    const QRect inner = frame.marginsRemoved(frameMargins());
    Qt::Edges edges;
    if (!frame.contains(pos) || inner.contains(pos) || pos.y() < frame.top() + kBarH) {
        return edges; // afuera, adentro de la region, o la barrita (y el aire a su lado, que no recibe clicks)
    }
    if (pos.x() < inner.left()) edges |= Qt::LeftEdge;
    if (pos.x() > inner.right()) edges |= Qt::RightEdge;
    if (pos.y() < inner.top()) edges |= Qt::TopEdge;
    if (pos.y() > inner.bottom()) edges |= Qt::BottomEdge;
    // Esquinas generosas: en la franja, cerca de una esquina, se estira en diagonal.
    if ((edges & (Qt::LeftEdge | Qt::RightEdge)) && !(edges & (Qt::TopEdge | Qt::BottomEdge))) {
        if (pos.y() < inner.top() + kCorner) edges |= Qt::TopEdge;
        else if (pos.y() > inner.bottom() - kCorner) edges |= Qt::BottomEdge;
    } else if ((edges & (Qt::TopEdge | Qt::BottomEdge)) && !(edges & (Qt::LeftEdge | Qt::RightEdge))) {
        if (pos.x() < inner.left() + kCorner) edges |= Qt::LeftEdge;
        else if (pos.x() > inner.right() - kCorner) edges |= Qt::RightEdge;
    }
    return edges;
}

Qt::CursorShape MeetFrameOverlay::cursorAt(const QPoint &pos) const
{
    if (!frameRect().contains(pos)) return Qt::ArrowCursor;
    // Los botones de la barrita, con la flecha (en Windows ya lo resuelve Qt: cada boton tiene su cursor).
    for (QWidget *child = childAt(pos); child && child != this; child = child->parentWidget()) {
        if (qobject_cast<QPushButton *>(child)) return Qt::ArrowCursor;
    }
    if (m_bar->geometry().contains(pos)) return Qt::SizeAllCursor;
    const Qt::Edges edges = edgesAt(pos);
    if (edges == (Qt::LeftEdge | Qt::TopEdge) || edges == (Qt::RightEdge | Qt::BottomEdge)) return Qt::SizeFDiagCursor;
    if (edges == (Qt::RightEdge | Qt::TopEdge) || edges == (Qt::LeftEdge | Qt::BottomEdge)) return Qt::SizeBDiagCursor;
    if (edges & (Qt::LeftEdge | Qt::RightEdge)) return Qt::SizeHorCursor;
    if (edges & (Qt::TopEdge | Qt::BottomEdge)) return Qt::SizeVerCursor;
    return Qt::ArrowCursor;
}

void MeetFrameOverlay::applyCursor(Qt::CursorShape shape)
{
    m_cursor = shape;
    setCursor(shape);
    if (!m_capture) MeetFrameSystem::setFrameCursor(this, shape);
}

bool MeetFrameOverlay::hoverAt(const QPoint &pos)
{
    if (m_dragging) return true;
    const Qt::CursorShape shape = cursorAt(pos);
    // Solo al cambiar: adentro de la region (que deja pasar el mouse) el cursor de la app de abajo se pisa una
    // sola vez, con la flecha, al pasar del borde al hueco o al salir del marco.
    if (shape != m_cursor) applyCursor(shape);
    if (QGuiApplication::applicationState() != Qt::ApplicationActive) hoverChild(pos);
    return frameRect().contains(pos) && (m_bar->geometry().contains(pos) || edgesAt(pos));
}

void MeetFrameOverlay::hoverChild(const QPoint &pos)
{
    // Con la app inactiva (siempre en mac: el marco no la activa) Qt no avisa la entrada y la salida del mouse:
    // sin esto los botones de la barrita no se resaltan y sus tooltips (con el atajo) no aparecen.
    QWidget *under = nullptr;
    for (QWidget *child = frameRect().contains(pos) ? childAt(pos) : nullptr; child && child != this; child = child->parentWidget()) {
        if (child->parentWidget() == m_bar) {
            under = child;
            break;
        }
    }
    if (under == m_hoverChild) return;
    if (QWidget *old = m_hoverChild.data()) {
        old->setAttribute(Qt::WA_UnderMouse, false);
        QEvent leave(QEvent::Leave);
        QCoreApplication::sendEvent(old, &leave);
        old->update();
    }
    m_hoverChild = under;
    if (under) {
        under->setAttribute(Qt::WA_UnderMouse, true);
        const QPoint local = under->mapFrom(this, pos);
        QEnterEvent enter(QPointF(local), QPointF(local), QPointF(mapToGlobal(pos)));
        QCoreApplication::sendEvent(under, &enter);
        under->update();
    }
}

void MeetFrameOverlay::mousePressEvent(QMouseEvent *event)
{
    if (m_capture || event->button() != Qt::LeftButton || !windowHandle()) {
        return;
    }
    const QPoint pos = event->position().toPoint();
    const bool onBar = m_bar->geometry().contains(pos);
    const Qt::Edges edges = onBar ? Qt::Edges() : edgesAt(pos);
    if (!onBar && !edges) return;
    // Con una forma fija, Windows estira a mano (el sistema no sabe mantenerla).
    if (!kManualDrag && (onBar || !m_aspect.isValid()) && (onBar ? windowHandle()->startSystemMove() : windowHandle()->startSystemResize(edges))) {
        return;
    }
    m_dragging = true;
    m_dragClock.start();
    m_dragSteps = 0;
    m_dragStepMs = m_dragStepMaxMs = 0;
    m_dragEdges = edges;
    m_aspectAxis = 0;
    m_pressGlobal = event->globalPosition().toPoint();
    m_pressGeometry = frameRect().translated(geometry().topLeft());
    applyCursor(onBar ? Qt::ClosedHandCursor : cursorAt(pos));
}

void MeetFrameOverlay::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging) {
        if (event->buttons() & Qt::LeftButton) {
            dragTo(event->globalPosition().toPoint());
            return;
        }
        m_dragging = false; // se solto afuera sin que llegara el release
    }
    hoverAt(event->position().toPoint());
}

void MeetFrameOverlay::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_dragging && event->button() == Qt::LeftButton) {
        dragTo(event->globalPosition().toPoint());
        m_dragging = false;
        qInfo().noquote() << QStringLiteral("[meetFrame] %1: %2 pasos en %3 ms, cada paso med %4 ms max %5 ms; region %6x%7")
                                 .arg(m_dragEdges ? QStringLiteral("Estirar") : QStringLiteral("Mover"))
                                 .arg(m_dragSteps).arg(m_dragClock.elapsed())
                                 .arg(m_dragSteps ? m_dragStepMs / m_dragSteps : 0.0, 0, 'f', 2).arg(m_dragStepMaxMs, 0, 'f', 1)
                                 .arg(region().width()).arg(region().height());
        m_cursor = Qt::BitmapCursor; // fuerza a elegir de nuevo el cursor donde quedo el mouse
        hoverAt(event->position().toPoint());
    }
    QWidget::mouseReleaseEvent(event);
}

void MeetFrameOverlay::dragTo(const QPoint &global)
{
    // Lo que tarda cada paso (mover o estirar la ventana y todo lo que eso dispara), para el log.
    QElapsedTimer clock;
    clock.start();
    struct Count
    {
        MeetFrameOverlay *self;
        QElapsedTimer *clock;
        ~Count()
        {
            const double ms = clock->nsecsElapsed() / 1e6;
            ++self->m_dragSteps;
            self->m_dragStepMs += ms;
            self->m_dragStepMaxMs = std::max(self->m_dragStepMaxMs, ms);
        }
    } count{this, &clock};
    const QPoint delta = global - m_pressGlobal;
    QRect next = m_pressGeometry;
    if (!m_dragEdges) {
        next.translate(delta);
        if (m_canvas) setFrameGlobal(next);
        else move(next.topLeft());
        return;
    }
    // Cada borde sigue al mouse sin achicar la region por debajo del minimo (el borde opuesto queda quieto).
    const QSize least = minimumRegion().grownBy(frameMargins());
    if (m_dragEdges & Qt::LeftEdge) next.setLeft(std::min(next.left() + delta.x(), next.right() + 1 - least.width()));
    if (m_dragEdges & Qt::RightEdge) next.setRight(std::max(next.right() + delta.x(), next.left() + least.width() - 1));
    if (m_dragEdges & Qt::TopEdge) next.setTop(std::min(next.top() + delta.y(), next.bottom() + 1 - least.height()));
    if (m_dragEdges & Qt::BottomEdge) next.setBottom(std::max(next.bottom() + delta.y(), next.top() + least.height() - 1));
    if (m_aspect.isValid()) next = keepAspect(next);
    setFrameGlobal(next);
}

QRect MeetFrameOverlay::keepAspect(const QRect &frame)
{
    // La proporcion es la de la region (lo que se comparte), no la del marco con su barrita.
    const QMargins margins = frameMargins();
    const QRect start = m_pressGeometry.marginsRemoved(margins);
    const QRect free = frame.marginsRemoved(margins);
    const double ratio = double(m_aspect.width()) / double(m_aspect.height());
    const bool horizontal = m_dragEdges & (Qt::LeftEdge | Qt::RightEdge);
    const bool vertical = m_dragEdges & (Qt::TopEdge | Qt::BottomEdge);
    double width = free.width();
    double height = free.height();
    // En una esquina manda el lado que mas cambio (en proporcion), decidido una vez por arrastre al pasar unos
    // pixeles (si no, con cambios de signo opuesto saltaba entre uno y otro); en un borde, ese borde.
    bool byWidth = horizontal;
    if (horizontal && vertical) {
        const double dw = qAbs(width / start.width() - 1.0);
        const double dh = qAbs(height / start.height() - 1.0);
        if (m_aspectAxis == 0 && (qAbs(free.width() - start.width()) > 4 || qAbs(free.height() - start.height()) > 4)) {
            m_aspectAxis = dw >= dh ? 1 : 2;
        }
        byWidth = m_aspectAxis == 0 ? dw >= dh : m_aspectAxis == 1;
    }
    if (byWidth) height = width / ratio;
    else width = height * ratio;
    const QSize least = minimumRegion();
    if (width < least.width()) {
        width = least.width();
        height = width / ratio;
    }
    if (height < least.height()) {
        height = least.height();
        width = height * ratio;
    }
    // Nunca mas grande que la pantalla (con la barrita y la franja adentro): el borde que se tira frena en el
    // borde de la pantalla, y un eje centrado no pasa del alto o el ancho de la pantalla.
    QScreen *screen = QGuiApplication::screenAt(start.center());
    if (!screen) screen = this->screen();
    const QRect area = screen ? screen->availableGeometry().marginsRemoved(margins) : QRect();
    if (area.isValid()) {
        double maxWidth = area.width();
        double maxHeight = area.height();
        if (m_dragEdges & Qt::RightEdge) maxWidth = std::min(maxWidth, double(area.right() + 1 - start.left()));
        if (m_dragEdges & Qt::LeftEdge) maxWidth = std::min(maxWidth, double(start.right() + 1 - area.left()));
        if (m_dragEdges & Qt::BottomEdge) maxHeight = std::min(maxHeight, double(area.bottom() + 1 - start.top()));
        if (m_dragEdges & Qt::TopEdge) maxHeight = std::min(maxHeight, double(start.bottom() + 1 - area.top()));
        maxWidth = std::max(maxWidth, double(least.width()));
        maxHeight = std::max(maxHeight, double(least.height()));
        if (width > maxWidth) {
            width = maxWidth;
            height = width / ratio;
        }
        if (height > maxHeight) {
            height = maxHeight;
            width = height * ratio;
        }
    }
    const int w = qRound(width);
    const int h = qRound(height);
    // El borde opuesto al que se tira queda quieto; en el otro eje, el centro.
    const int x = (m_dragEdges & Qt::LeftEdge) ? start.right() + 1 - w : (m_dragEdges & Qt::RightEdge) ? start.left() : start.center().x() - (w - 1) / 2;
    const int y = (m_dragEdges & Qt::TopEdge) ? start.bottom() + 1 - h : (m_dragEdges & Qt::BottomEdge) ? start.top() : start.center().y() - (h - 1) / 2;
    QRect result(x, y, w, h);
    // El eje centrado crece hacia los dos lados: si se sale de la pantalla, se corre para adentro (la barrita
    // nunca queda debajo de la barra de menu ni fuera de alcance).
    if (area.isValid() && !(m_dragEdges & (Qt::LeftEdge | Qt::RightEdge))) {
        if (result.left() < area.left()) result.moveLeft(area.left());
        if (result.right() > area.right()) result.moveRight(area.right());
    }
    if (area.isValid() && !(m_dragEdges & (Qt::TopEdge | Qt::BottomEdge))) {
        if (result.top() < area.top()) result.moveTop(area.top());
        if (result.bottom() > area.bottom()) result.moveBottom(area.bottom());
    }
    return result.marginsAdded(margins);
}

void MeetFrameOverlay::leaveEvent(QEvent *event)
{
    if (!m_dragging) {
        m_cursor = Qt::ArrowCursor;
        unsetCursor();
    }
    QWidget::leaveEvent(event);
}

void MeetFrameOverlay::moveEvent(QMoveEvent *event)
{
    QWidget::moveEvent(event);
    if (m_canvas) {
        // Lo movio el sistema (otro monitor, otra resolucion): la region cambio con el; se vuelve a acomodar y avisa.
        if (!m_placing) QTimer::singleShot(0, this, &MeetFrameOverlay::replaceCanvas);
        return;
    }
    emit regionChanged();
    m_settle.start();
}

void MeetFrameOverlay::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    layoutBar();
    updateMask();
    if (m_canvas) {
        if (!m_placing) QTimer::singleShot(0, this, &MeetFrameOverlay::replaceCanvas);
        return;
    }
    emit regionChanged();
    m_settle.start();
}

void MeetFrameOverlay::layoutBar()
{
    const QRect frame = frameRect();
    m_bar->setGeometry(frame.left() + kBand - kLine, frame.top(), m_bar->sizeHint().width(), kBarH - 4);
}

void MeetFrameOverlay::updateMask()
{
    // Solo la franja del borde y la barrita reciben el mouse: adentro y al costado de la barrita, los clicks
    // pasan a las ventanas de abajo.
    setMask(frameArea());
}

void MeetFrameOverlay::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRect frame = frameRect();
    const QRect inner = frame.marginsRemoved(frameMargins());
    // La franja casi invisible: con alfa 0 Windows deja pasar el click; con 1, el marco lo recibe.
    QRegion band = QRegion(frame.adjusted(0, kBarH, 0, 0)).subtracted(QRegion(inner));
    painter.save();
    painter.setClipRegion(band);
    painter.fillRect(frame, QColor(0, 0, 0, 1));
    painter.restore();
    // El borde, por fuera de la region para que nunca salga en lo compartido.
    const QColor line(!m_error.isEmpty() ? Theme::kError : m_paused ? Theme::kWarn : Theme::kOk);
    painter.setPen(Qt::NoPen);
    painter.setBrush(line);
    QRegion ring = QRegion(inner.adjusted(-kLine, -kLine, kLine, kLine)).subtracted(QRegion(inner));
    painter.setClipRegion(ring);
    painter.drawRect(frame);
    painter.setClipping(false);
    // La barrita: tarjeta de la app con el color del borde a la izquierda.
    const QRectF bar = QRectF(m_bar->geometry()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(QPen(QColor(Theme::kCardBorder), 1));
    painter.setBrush(QColor(Theme::kCard));
    painter.drawRoundedRect(bar, kBarRadius, kBarRadius);
}
