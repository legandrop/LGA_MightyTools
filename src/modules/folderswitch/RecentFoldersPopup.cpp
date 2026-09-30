#include "modules/folderswitch/RecentFoldersPopup.h"
#include "ui/Theme.h"
#include "ui/UiWidgets.h"
#include "core/I18n.h"

#include <QEventLoop>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>

#include <windows.h>

// Copia de LGA_FolderSwitch (src/ui/RecentFoldersPopup.cpp): misma geometria y el mismo dibujo a
// mano, con los colores reemplazados por su token de Theme (tarjeta, badges y el hover de fila,
// Theme::kRowHover).

namespace {

// Medidas del canvas, en px logicos.
constexpr int kCardWidth = 380;
constexpr int kEmptyCardWidth = 300;
constexpr int kPad = 6;
constexpr int kHeaderHeight = 28;
constexpr int kDividerBlock = 7; // 2 de aire + 1 de linea + 4 de aire
constexpr int kRowHeight = 46;
// Estado vacio: aire arriba y abajo del bloque de icono + textos.
constexpr int kEmptyPad = 14;
constexpr int kEmptyIcon = 18;
constexpr int kBadge = 22;
constexpr int kRadius = 8;

// Aire alrededor de la tarjeta para la sombra (corrida hacia abajo, como en el canvas).
constexpr int kShadowLeft = 20;
constexpr int kShadowTop = 14;
constexpr int kShadowRight = 20;
constexpr int kShadowBottom = 30;
constexpr int kShadowBlur = 18;
constexpr int kShadowOffsetY = 8;

// La tarjeta aparece corrida del puntero, como un menu contextual.
const QPoint kCursorOffset(4, 6);

// Funciones y no constantes: el texto depende del idioma elegido al momento de dibujar.
QString emptyTitle()
{
    return I18n::tr("No folders yet");
}

QString emptyText()
{
    return I18n::tr("Open a folder in Explorer or XYplorer, then come back to this dialog.");
}

// Ancho del texto del estado vacio: la tarjeta menos su relleno, el icono y los aires.
int emptyTextWidth()
{
    return kEmptyCardWidth - 2 * kPad - 12 - kEmptyIcon - 13 - 12;
}

// Alto del estado vacio con el texto ya partido en lineas, con la fuente real (Qt parte distinto
// que un navegador, y un alto fijo dejaba la ultima linea afuera de la tarjeta).
int emptyBodyHeight()
{
    const QFontMetrics title(Theme::uiFont(13.5, QFont::Medium));
    const QFontMetrics text(Theme::uiFont(12.5));
    const QRect wrapped = text.boundingRect(QRect(0, 0, emptyTextWidth(), 1000), Qt::TextWordWrap, emptyText());
    return kEmptyPad + title.height() + 3 + wrapped.height() + kEmptyPad;
}

QColor cardColor() { return Theme::color(Theme::kCard); }
QColor cardBorderColor() { return Theme::color(Theme::kBorder); }
QColor rowHoverColor() { return Theme::color(Theme::kRowHover); }
QColor badgeColor() { return Theme::color(Theme::kPrimary); }
QColor badgeHoverColor() { return Theme::color(Theme::kPrimaryBorder); }

// "C:\A\B\carpeta\" -> {"carpeta", "C:\A\B"}; una raiz "N:\" queda entera como nombre.
QPair<QString, QString> splitFolder(QString path)
{
    path.replace(QLatin1Char('/'), QLatin1Char('\\'));
    while (path.size() > 3 && path.endsWith(QLatin1Char('\\'))) {
        path.chop(1);
    }
    const int slash = path.lastIndexOf(QLatin1Char('\\'));
    if (slash <= 0 || slash == path.size() - 1) {
        return {path, QString()};
    }
    QString parent = path.left(slash);
    if (parent.endsWith(QLatin1Char(':'))) {
        parent += QLatin1Char('\\');
    }
    return {path.mid(slash + 1), parent};
}

} // namespace

RecentFoldersPopup::RecentFoldersPopup(const QStringList &folders, QWidget *parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint)
    , m_folders(folders)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    setFixedSize(sizeHint());
}

QSize RecentFoldersPopup::sizeHint() const
{
    const int width = m_folders.isEmpty() ? kEmptyCardWidth : kCardWidth;
    const int body = m_folders.isEmpty() ? emptyBodyHeight() : kRowHeight * static_cast<int>(m_folders.size());
    const int height = kPad + kHeaderHeight + kDividerBlock + body + kPad;
    return QSize(kShadowLeft + width + kShadowRight, kShadowTop + height + kShadowBottom);
}

QRect RecentFoldersPopup::cardRect() const
{
    return rect().adjusted(kShadowLeft, kShadowTop, -kShadowRight, -kShadowBottom);
}

QRect RecentFoldersPopup::rowRect(int index) const
{
    const QRect card = cardRect();
    const int top = card.top() + kPad + kHeaderHeight + kDividerBlock + index * kRowHeight;
    return QRect(card.left() + kPad, top, card.width() - 2 * kPad, kRowHeight);
}

int RecentFoldersPopup::rowAt(const QPoint &pos) const
{
    for (int i = 0; i < m_folders.size(); ++i) {
        if (rowRect(i).contains(pos)) {
            return i;
        }
    }
    return -1;
}

void RecentFoldersPopup::setCurrentIndex(int index)
{
    if (index != m_current) {
        m_current = index;
        update();
    }
}

QString RecentFoldersPopup::exec(const QPoint &pos)
{
    // La tarjeta al lado del puntero; si no entra, del otro lado, y siempre dentro de la pantalla.
    const QSize card = cardRect().size();
    const QScreen *screen = QGuiApplication::screenAt(pos);
    const QRect area = screen ? screen->availableGeometry() : QRect(pos, card);
    QPoint cardTopLeft = pos + kCursorOffset;
    if (cardTopLeft.x() + card.width() > area.right()) {
        cardTopLeft.setX(pos.x() - kCursorOffset.x() - card.width());
    }
    if (cardTopLeft.y() + card.height() > area.bottom()) {
        cardTopLeft.setY(pos.y() - kCursorOffset.y() - card.height());
    }
    cardTopLeft.setX(qBound(area.left(), cardTopLeft.x(), area.right() - card.width()));
    cardTopLeft.setY(qBound(area.top(), cardTopLeft.y(), area.bottom() - card.height()));
    move(cardTopLeft - QPoint(kShadowLeft, kShadowTop));

    m_chosen.clear();
    show();
    // Una app en segundo plano no recibe el click de afuera (que cierra el popup) ni las teclas si
    // su ventana no pasa a primer plano. WM_HOTKEY le da a este proceso el permiso para hacerlo.
    SetForegroundWindow(reinterpret_cast<HWND>(winId()));
    activateWindow();

    QEventLoop loop;
    m_loop = &loop;
    loop.exec();
    m_loop = nullptr;
    return m_chosen;
}

void RecentFoldersPopup::choose(int index)
{
    if (index >= 0 && index < m_folders.size()) {
        m_chosen = m_folders.at(index);
    }
    hide();
}

void RecentFoldersPopup::hideEvent(QHideEvent *event)
{
    // Un Qt::Popup se oculta solo con un click afuera: eso tambien termina el exec(). Es tambien lo
    // que llama stop() del modulo para cerrarlo si el usuario apaga la herramienta con el popup
    // abierto (contrato de Module.h, "Vida").
    if (m_loop) {
        m_loop->quit();
    }
    QWidget::hideEvent(event);
}

void RecentFoldersPopup::mouseMoveEvent(QMouseEvent *event)
{
    setCurrentIndex(rowAt(event->position().toPoint()));
    QWidget::mouseMoveEvent(event);
}

void RecentFoldersPopup::leaveEvent(QEvent *event)
{
    setCurrentIndex(-1);
    QWidget::leaveEvent(event);
}

void RecentFoldersPopup::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        return;
    }
    const int row = rowAt(event->position().toPoint());
    if (row >= 0) {
        choose(row);
    } else if (!cardRect().contains(event->position().toPoint())) {
        hide(); // click en la zona de la sombra: es "afuera"
    }
}

void RecentFoldersPopup::keyPressEvent(QKeyEvent *event)
{
    const int key = event->key();
    const int count = static_cast<int>(m_folders.size());
    if (key >= Qt::Key_1 && key <= Qt::Key_9) {
        const int index = key - Qt::Key_1;
        if (index < count) {
            choose(index);
        }
        return;
    }
    switch (key) {
    case Qt::Key_Escape:
        hide();
        return;
    case Qt::Key_Down:
        if (count > 0) {
            setCurrentIndex((m_current + 1) % count);
        }
        return;
    case Qt::Key_Up:
        if (count > 0) {
            setCurrentIndex(m_current <= 0 ? count - 1 : m_current - 1);
        }
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (m_current >= 0) {
            choose(m_current);
        }
        return;
    default:
        QWidget::keyPressEvent(event);
    }
}

void RecentFoldersPopup::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRect card = cardRect();

    // Sombra suave: capas redondeadas apenas mas grandes, cada una casi transparente; donde se
    // superponen se oscurece. Sin blur real, que en QPainter pediria una imagen intermedia.
    painter.setPen(Qt::NoPen);
    for (int i = kShadowBlur; i >= 1; --i) {
        painter.setBrush(QColor(0, 0, 0, 7));
        const QRectF layer = QRectF(card).translated(0, kShadowOffsetY).adjusted(-i, -i, i, i);
        painter.drawRoundedRect(layer, kRadius + i, kRadius + i);
    }
    painter.setBrush(QColor(0, 0, 0, 70));
    painter.drawRoundedRect(QRectF(card).translated(0, 1).adjusted(-1, -1, 1, 1), kRadius + 1, kRadius + 1);

    // Tarjeta
    painter.setBrush(cardColor());
    painter.setPen(QPen(cardBorderColor(), 1.0));
    painter.drawRoundedRect(QRectF(card).adjusted(0.5, 0.5, -0.5, -0.5), kRadius, kRadius);

    // Titulo y ayuda de teclado
    const QRect header(card.left() + kPad + 10, card.top() + kPad, card.width() - 2 * kPad - 18, kHeaderHeight);
    painter.setFont(Theme::uiFont(12, QFont::DemiBold));
    painter.setPen(Theme::color(Theme::kTextMuted));
    painter.drawText(header, Qt::AlignLeft | Qt::AlignVCenter, I18n::tr("Recent folders"));
    if (!m_folders.isEmpty()) {
        // "Press 1-N" con los numeros en el violeta de acento.
        const QString last = QString::number(qMin<int>(m_folders.size(), 9));
        const QString press = I18n::tr("Press") + QLatin1Char(' ');
        const QList<QPair<QString, bool>> parts = m_folders.size() > 1
            ? QList<QPair<QString, bool>>{{press, false}, {QStringLiteral("1"), true},
                                          {QStringLiteral("\u2013"), false}, {last, true}}
            : QList<QPair<QString, bool>>{{press, false}, {QStringLiteral("1"), true}};
        const QFont plain = Theme::uiFont(11.5);
        const QFont strong = Theme::uiFont(11.5, QFont::Bold);
        int width = 0;
        for (const auto &part : parts) {
            width += QFontMetrics(part.second ? strong : plain).horizontalAdvance(part.first);
        }
        int x = header.right() - width;
        for (const auto &part : parts) {
            const QFont &font = part.second ? strong : plain;
            painter.setFont(font);
            painter.setPen(part.second ? Theme::color(Theme::kAccent) : Theme::color(Theme::kTextFaint));
            const int advance = QFontMetrics(font).horizontalAdvance(part.first);
            painter.drawText(QRect(x, header.top(), advance + 1, header.height()), Qt::AlignLeft | Qt::AlignVCenter,
                             part.first);
            x += advance;
        }
    }

    // Divisor
    painter.setPen(Qt::NoPen);
    painter.setBrush(Theme::color(Theme::kDivider));
    painter.drawRect(QRect(card.left() + kPad + 4, card.top() + kPad + kHeaderHeight + 2, card.width() - 2 * kPad - 8, 1));

    if (m_folders.isEmpty()) {
        const int top = card.top() + kPad + kHeaderHeight + kDividerBlock;
        const QRect icon(card.left() + kPad + 12, top + kEmptyPad, kEmptyIcon, kEmptyIcon);
        Icons::paint(painter, Icon::Folder, icon, Theme::color(Theme::kTextPlaceholder));
        const int textLeft = icon.right() + 1 + 13;
        painter.setFont(Theme::uiFont(13.5, QFont::Medium));
        painter.setPen(Theme::color(Theme::kText));
        const int titleHeight = QFontMetrics(painter.font()).height();
        painter.drawText(QRect(textLeft, top + kEmptyPad, emptyTextWidth(), titleHeight), Qt::AlignLeft | Qt::AlignVCenter,
                         emptyTitle());
        painter.setFont(Theme::uiFont(12.5));
        painter.setPen(Theme::color(Theme::kTextFaint));
        painter.drawText(QRect(textLeft, top + kEmptyPad + titleHeight + 3, emptyTextWidth(), 1000), Qt::TextWordWrap,
                         emptyText());
        return;
    }

    const QFont nameFont = Theme::uiFont(13.5, QFont::Medium);
    const QFont parentFont = Theme::uiFont(12);
    const QFont badgeFont = Theme::uiFont(11, QFont::DemiBold);
    const QFontMetrics nameMetrics(nameFont);
    const QFontMetrics parentMetrics(parentFont);
    for (int i = 0; i < m_folders.size(); ++i) {
        const QRect row = rowRect(i);
        const bool hot = i == m_current;
        if (hot) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(rowHoverColor());
            painter.drawRoundedRect(QRectF(row), 5, 5);
        }

        const QRect badge(row.left() + 10, row.center().y() - kBadge / 2 + 1, kBadge, kBadge);
        painter.setPen(Qt::NoPen);
        painter.setBrush(hot ? badgeHoverColor() : badgeColor());
        painter.drawRoundedRect(QRectF(badge), 4, 4);
        painter.setFont(badgeFont);
        painter.setPen(Qt::white);
        painter.drawText(badge, Qt::AlignCenter, QString::number(i + 1));

        const auto [name, parent] = splitFolder(m_folders.at(i));
        const int textLeft = badge.right() + 13;
        const int textWidth = row.right() - 10 - textLeft;
        const int block = parent.isEmpty() ? nameMetrics.height() : nameMetrics.height() + 2 + parentMetrics.height();
        const int top = row.top() + (row.height() - block) / 2;
        painter.setFont(nameFont);
        painter.setPen(hot ? Theme::color(Theme::kTextBright) : Theme::color(Theme::kTextStrong));
        painter.drawText(QRect(textLeft, top, textWidth, nameMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                         nameMetrics.elidedText(name, Qt::ElideMiddle, textWidth));
        if (!parent.isEmpty()) {
            // La ruta se recorta por la izquierda: lo que distingue dos carpetas esta al final.
            painter.setFont(parentFont);
            painter.setPen(Theme::color(Theme::kTextFaint));
            painter.drawText(QRect(textLeft, top + nameMetrics.height() + 2, textWidth, parentMetrics.height()),
                             Qt::AlignLeft | Qt::AlignVCenter, parentMetrics.elidedText(parent, Qt::ElideLeft, textWidth));
        }
    }
}
