#include "modules/windowlayouts/AreaPicker.h"

#include "core/I18n.h"
#include "modules/windowlayouts/LayoutGeometry.h"
#include "platform/WindowPlacement.h"
#include "ui/Theme.h"

#include <QCloseEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>

AreaPicker::AreaPicker(Mode mode, bool captureMode, QWidget *parent)
    : QWidget(parent), m_mode(mode), m_captureMode(captureMode)
{
    setObjectName(QStringLiteral("windowLayoutsAreaPicker"));
    setMouseTracking(true);
    if (captureMode) {
        setWindowFlags(Qt::Widget);
        setFixedSize(780, 700);
        m_windows = {{QStringLiteral("XYplorer"), QRect(75, 130, 345, 282)},
                     {QStringLiteral("OBS Studio"), QRect(287, 200, 405, 333)}};
        m_hovered = 1;
        m_start = QPoint(135, 120);
        m_current = QPoint(590, 460);
        m_dragging = mode == Mode::Draw;
    } else {
        setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        setAttribute(Qt::WA_TranslucentBackground);
        if (mode == Mode::Window) {
            refreshCandidates();
        }
        if (QScreen *screen = QGuiApplication::primaryScreen()) {
            setGeometry(screen->virtualGeometry());
        }
    }
}

void AreaPicker::refreshCandidates()
{
    if (m_captureMode || m_mode != Mode::Window) return;
    m_windows.clear();
    const QList<WindowPlacement::WindowInfo> windows = WindowPlacement::selectableWindows(&m_error);
    for (const WindowPlacement::WindowInfo &window : windows) {
        if (!window.geometry.isEmpty()) m_windows.append({window.title, window.geometry});
    }
    m_lastRefresh.restart();
}

int AreaPicker::candidateAt(const QPoint &global) const
{
    for (int i = 0; i < m_windows.size(); ++i) {
        if (m_windows.at(i).geometry.contains(global)) return i;
    }
    return -1;
}

void AreaPicker::open()
{
    if (m_captureMode || !m_error.isEmpty()) {
        return;
    }
    show();
    grabKeyboard();
}

QRect AreaPicker::workAreaAt(const QPoint &global) const
{
    if (m_captureMode) {
        return QRect(0, 0, width(), height() - 35);
    }
    return WindowPlacement::pickerWorkAreaAt(global);
}

void AreaPicker::finish(const QRect &selection, const QPoint &at)
{
    const QRectF area = LayoutGeometry::capture(selection, workAreaAt(at));
    if (!LayoutGeometry::validArea(area)) {
        return;
    }
    if (m_chosen) {
        m_chosen(area);
    }
    close();
    deleteLater();
}

void AreaPicker::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (m_captureMode) {
        painter.fillRect(rect(), Theme::color(Theme::kWindow));
    } else {
        painter.fillRect(rect(), QColor(0, 0, 0, 85));
    }
    const QColor accent = Theme::color(Theme::kLink);
    if (m_captureMode) {
        painter.setBrush(Theme::color(Theme::kTile));
        painter.setPen(Theme::color(Theme::kBorderStrong));
        for (const Candidate &window : m_windows) {
            painter.drawRoundedRect(window.geometry, 6, 6);
            painter.setPen(Theme::color(Theme::kTextStrong));
            painter.drawText(window.geometry.adjusted(10, 7, -8, -8), Qt::AlignTop, window.title);
            painter.setPen(Theme::color(Theme::kBorderStrong));
        }
    }
    QRect selection;
    if (m_mode == Mode::Draw && m_dragging) {
        selection = QRect(mapFromGlobal(m_start), mapFromGlobal(m_current)).normalized();
        if (m_captureMode) {
            selection = QRect(m_start, m_current).normalized();
        }
    } else if (m_mode == Mode::Window && m_hovered >= 0 && m_hovered < m_windows.size()) {
        selection = m_captureMode ? m_windows.at(m_hovered).geometry
                                  : QRect(mapFromGlobal(m_windows.at(m_hovered).geometry.topLeft()),
                                          m_windows.at(m_hovered).geometry.size());
    }
    if (!selection.isEmpty()) {
        QColor fill = accent;
        fill.setAlpha(48);
        painter.setBrush(fill);
        painter.setPen(QPen(accent, 2));
        painter.drawRect(selection);
    }
    const QString instruction = !m_error.isEmpty() ? m_error
        : m_mode == Mode::Draw ? I18n::tr("Drag to draw an area. Esc to cancel.")
        : I18n::tr("Click a window to use its bounds. Esc to cancel.");
    painter.setBrush(Theme::color(Theme::kModIconBg));
    painter.setPen(Theme::color(Theme::kModIconBorder));
    const QRect banner((width() - 470) / 2, 18, 470, 38);
    painter.drawRoundedRect(banner, 6, 6);
    painter.setPen(Theme::color(Theme::kTextBright));
    painter.drawText(banner, Qt::AlignCenter, instruction);
}

void AreaPicker::mousePressEvent(QMouseEvent *event)
{
    if (m_captureMode || event->button() != Qt::LeftButton) {
        return;
    }
    if (m_mode == Mode::Draw) {
        m_start = event->globalPosition().toPoint();
        m_current = m_start;
        m_dragging = true;
        update();
    } else {
        refreshCandidates();
        m_hovered = candidateAt(event->globalPosition().toPoint());
        m_windowPressed = m_hovered >= 0;
        update();
    }
}

void AreaPicker::mouseMoveEvent(QMouseEvent *event)
{
    if (m_captureMode) {
        return;
    }
    const QPoint point = event->globalPosition().toPoint();
    if (m_mode == Mode::Draw && m_dragging) {
        m_current = point;
    } else if (m_mode == Mode::Window) {
        if (!m_lastRefresh.isValid() || m_lastRefresh.elapsed() >= 200) refreshCandidates();
        m_hovered = candidateAt(point);
    }
    update();
}

void AreaPicker::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_captureMode || event->button() != Qt::LeftButton) {
        return;
    }
    const QPoint point = event->globalPosition().toPoint();
    if (m_mode == Mode::Draw && m_dragging) {
        m_current = point;
        m_dragging = false;
        if ((m_current - m_start).manhattanLength() >= 8) {
            finish(QRect(m_start, m_current).normalized(), m_start);
        }
        update();
    } else if (m_mode == Mode::Window) {
        refreshCandidates();
        m_hovered = candidateAt(point);
        if (m_windowPressed && m_hovered >= 0) finish(m_windows.at(m_hovered).geometry, point);
        m_windowPressed = false;
        update();
    }
}

void AreaPicker::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        deleteLater();
        return;
    }
    QWidget::keyPressEvent(event);
}

void AreaPicker::closeEvent(QCloseEvent *event)
{
    if (!m_captureMode) {
        releaseKeyboard();
    }
    QWidget::closeEvent(event);
}
