#include "modules/diskspace/cleanup/SizeListView.h"

#include "ui/CustomTooltip.h"
#include "ui/Theme.h"

#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>

namespace {

// Medidas del canvas (`.lhead`, `.lrow`): 16 de relleno a los lados, 12 entre columnas, 18 por nivel,
// la flecha en una caja de 16 y 6 entre las piezas del nombre.
constexpr int kPadX = 16;
constexpr int kColumnGap = 12;
constexpr int kIndent = 18;
constexpr int kExpander = 16;
constexpr int kPartGap = 6;
constexpr int kIcon = 14;
constexpr int kShareText = 34;

QFont headerFont()
{
    QFont font = Theme::uiFont(11);
    font.setCapitalization(QFont::AllUppercase);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0.66); // .06em a 11 px
    return font;
}

} // namespace

SizeListView::SizeListView(QWidget *parent)
    : QAbstractScrollArea(parent)
{
    setObjectName(QStringLiteral("sizeList"));
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::NoFocus);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    viewport()->setMouseTracking(true);
    viewport()->setAutoFillBackground(false);
    verticalScrollBar()->setSingleStep(kRowHeight);
    m_tipAnchor = new QWidget(viewport());
    m_tipAnchor->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_tipAnchor->setAttribute(Qt::WA_NoSystemBackground);
    m_tipAnchor->setGeometry(0, 0, 0, 0);
}

void SizeListView::setColumns(const QString &nameTitle, const QList<SizeListColumn> &columns)
{
    m_nameTitle = nameTitle;
    m_columns = columns;
    // Una columna nunca es mas angosta que su titulo (en espanol son mas largos): se ensancha y las
    // demas se corren, en vez de cortarlo.
    const QFontMetrics metrics(headerFont());
    for (SizeListColumn &column : m_columns) {
        const QString title = column.sorted ? column.title + QStringLiteral(" ↓") : column.title;
        column.width = qMax(column.width, metrics.horizontalAdvance(title) + 2);
    }
    viewport()->update();
}

void SizeListView::setRows(const QList<SizeListRow> &rows)
{
    m_rows = rows;
    // De lo elegido queda lo que sigue existiendo.
    QSet<QString> alive;
    for (const SizeListRow &row : m_rows) {
        if (m_selected.contains(row.id)) {
            alive.insert(row.id);
        }
    }
    const bool changed = alive.size() != m_selected.size();
    m_selected = alive;
    m_hovered = -1;
    updateRowTip(-1, 0);
    updateScrollRange();
    viewport()->update();
    if (changed) {
        emit selectionChanged();
    }
}

QStringList SizeListView::selectedIds() const
{
    // En el orden de la lista, no en el del conjunto.
    QStringList ids;
    for (const SizeListRow &row : m_rows) {
        if (m_selected.contains(row.id)) {
            ids.append(row.id);
        }
    }
    return ids;
}

void SizeListView::setSelectedIds(const QStringList &ids)
{
    m_selected = QSet<QString>(ids.begin(), ids.end());
    viewport()->update();
    emit selectionChanged();
}

void SizeListView::clearSelection()
{
    if (m_selected.isEmpty()) {
        return;
    }
    m_selected.clear();
    viewport()->update();
    emit selectionChanged();
}

void SizeListView::setInteractive(bool interactive)
{
    m_interactive = interactive;
    if (!interactive) {
        setHovered(-1);
    }
}

void SizeListView::setEmptyText(const QString &text)
{
    m_emptyText = text;
    viewport()->update();
}

void SizeListView::setTree(bool tree)
{
    m_tree = tree;
    viewport()->update();
}

void SizeListView::updateScrollRange()
{
    const int visible = qMax(0, viewport()->height() - kHeaderHeight);
    const int total = int(m_rows.size()) * kRowHeight;
    verticalScrollBar()->setPageStep(visible);
    verticalScrollBar()->setRange(0, qMax(0, total - visible));
}

void SizeListView::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateScrollRange();
}

int SizeListView::rowAt(const QPoint &pos) const
{
    if (pos.y() < kHeaderHeight) {
        return -1;
    }
    const int row = (pos.y() - kHeaderHeight + verticalScrollBar()->value()) / kRowHeight;
    return row >= 0 && row < m_rows.size() ? row : -1;
}

int SizeListView::nameLeft(const SizeListRow &row) const
{
    return kPadX + row.depth * kIndent;
}

int SizeListView::columnsLeft() const
{
    int width = 0;
    for (const SizeListColumn &column : m_columns) {
        width += column.width + kColumnGap;
    }
    return viewport()->width() - kPadX - width;
}

QRect SizeListView::expanderRect(int row) const
{
    const int top = kHeaderHeight + row * kRowHeight - verticalScrollBar()->value();
    return QRect(nameLeft(m_rows.at(row)), top + (kRowHeight - 20) / 2, kExpander, 20);
}

void SizeListView::setHovered(int row)
{
    if (row == m_hovered) {
        return;
    }
    m_hovered = row;
    const bool hand = row >= 0 && m_interactive && m_rows.at(row).selectable;
    viewport()->setCursor(hand ? Qt::PointingHandCursor : Qt::ArrowCursor);
    viewport()->update();
}

void SizeListView::updateRowTip(int row, int x)
{
    if (row == m_tipRow) {
        return;
    }
    m_tipRow = row;
    CustomTooltip::instance()->hideToolTip();
    if (row < 0 || row >= m_rows.size()) {
        m_tipAnchor->setGeometry(0, 0, 0, 0);
        return;
    }
    // Solo cuando agrega algo: la ruta entera de una fila que muestra nada mas que el nombre.
    const SizeListRow &entry = m_rows.at(row);
    if (entry.tooltip.isEmpty() || entry.tooltip == entry.name) {
        m_tipAnchor->setGeometry(0, 0, 0, 0);
        return;
    }
    const int top = kHeaderHeight + row * kRowHeight - verticalScrollBar()->value();
    m_tipAnchor->setGeometry(0, top, viewport()->width(), kRowHeight);
    CustomTooltip::instance()->requestToolTip(entry.tooltip.toHtmlEscaped(), m_tipAnchor,
                                              m_tipAnchor->mapToGlobal(QPoint(x, kRowHeight)));
}

void SizeListView::mouseMoveEvent(QMouseEvent *event)
{
    const int row = m_interactive ? rowAt(event->pos()) : -1;
    setHovered(row);
    updateRowTip(row, event->pos().x());
}

void SizeListView::leaveEvent(QEvent *event)
{
    setHovered(-1);
    updateRowTip(-1, 0);
    QAbstractScrollArea::leaveEvent(event);
}

void SizeListView::mousePressEvent(QMouseEvent *event)
{
    if (!m_interactive || event->button() != Qt::LeftButton) {
        return;
    }
    const int index = rowAt(event->pos());
    if (index < 0) {
        return;
    }
    const SizeListRow &row = m_rows.at(index);
    if (row.expander != SizeListRow::Expander::None && expanderRect(index).adjusted(-3, -4, 3, 4).contains(event->pos())) {
        emit expanderClicked(row.id);
        return;
    }
    if (!row.selectable) {
        return;
    }
    if (m_selected.contains(row.id)) {
        m_selected.remove(row.id);
    } else {
        m_selected.insert(row.id);
    }
    viewport()->update();
    emit selectionChanged();
}

void SizeListView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (!m_interactive || event->button() != Qt::LeftButton) {
        return;
    }
    const int index = rowAt(event->pos());
    if (index < 0 || m_rows.at(index).muted) {
        return;
    }
    // El segundo click del doble click ya solto la fila que el primero habia elegido: se vuelve a elegir.
    const SizeListRow &row = m_rows.at(index);
    if (row.selectable && !m_selected.contains(row.id)) {
        m_selected.insert(row.id);
        viewport()->update();
        emit selectionChanged();
    }
    emit activated(row.id);
}

void SizeListView::paintEvent(QPaintEvent *)
{
    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing, true);
    const int width = viewport()->width();
    const int height = viewport()->height();
    painter.fillRect(viewport()->rect(), Theme::color(Theme::kWindow));

    const QFont nameFont = Theme::uiFont(13);
    const QFont sizeFont = Theme::uiFont(13, QFont::Medium);
    const QFont dimFont = Theme::uiFont(12);
    QFont noteFont = dimFont;
    noteFont.setItalic(true);
    QFont mutedFont = nameFont;
    mutedFont.setItalic(true);
    const QFont chipFont = Theme::uiFont(11.5, QFont::Medium);

    const QColor text = Theme::color(Theme::kText);
    const QColor strong = Theme::color(Theme::kTextStrong);
    const QColor bright = Theme::color(Theme::kTextBright);
    const QColor faint = Theme::color(Theme::kTextFaint);
    const QColor iconColor = Theme::color(Theme::kIcon);

    // ---- Filas visibles.
    const int offset = verticalScrollBar()->value();
    const int first = qMax(0, offset / kRowHeight);
    const int last = qMin(int(m_rows.size()) - 1, (offset + height - kHeaderHeight) / kRowHeight);
    const int columnsStart = columnsLeft();
    for (int index = first; index <= last; ++index) {
        const SizeListRow &row = m_rows.at(index);
        const int top = kHeaderHeight + index * kRowHeight - offset;
        const QRect rowRect(0, top, width, kRowHeight);
        const bool selected = m_selected.contains(row.id);
        if (selected) {
            painter.fillRect(rowRect, Theme::color(Theme::kSideSelected));
        } else if (index == m_hovered && !row.muted) {
            painter.fillRect(rowRect, Theme::color(Theme::kSideHover));
        }

        // -- Nombre: flecha, icono, texto, aclaracion, etiqueta, ruta.
        int x = nameLeft(row);
        const int nameRight = columnsStart;
        if (row.expander != SizeListRow::Expander::None) {
            const QRectF glyph(x + (kExpander - 10) / 2.0, top + (kRowHeight - 10) / 2.0, 10, 10);
            Icons::paint(painter, row.expander == SizeListRow::Expander::Open ? Icon::TreeOpen : Icon::TreeClosed, glyph, iconColor);
        }
        if (m_tree) {
            x += kExpander + kPartGap;
        }
        if (row.hasIcon) {
            const QRectF glyph(x, top + (kRowHeight - kIcon) / 2.0, kIcon, kIcon);
            Icons::paint(painter, row.icon, glyph, selected ? Theme::color(Theme::kLink) : iconColor);
            x += kIcon + kPartGap;
        }
        int available = nameRight - x;
        if (available > 8) {
            painter.setFont(row.muted ? mutedFont : nameFont);
            painter.setPen(row.muted ? faint : (selected ? bright : text));
            const QFontMetrics nameMetrics(painter.font());
            // Con una ruta al lado, el nombre toma lo suyo (hasta el 60 %) y la ruta el resto.
            const int extras = (row.note.isEmpty() ? 0 : QFontMetrics(noteFont).horizontalAdvance(row.note) + kPartGap)
                               + (row.chip.isEmpty() ? 0 : QFontMetrics(chipFont).horizontalAdvance(row.chip) + 14 + kPartGap);
            int nameWidth = nameMetrics.horizontalAdvance(row.name);
            const int nameLimit = row.detail.isEmpty() ? available - extras : int(available * 0.6);
            nameWidth = qMin(nameWidth, qMax(8, nameLimit));
            painter.drawText(QRect(x, top, nameWidth, kRowHeight), Qt::AlignLeft | Qt::AlignVCenter,
                             nameMetrics.elidedText(row.name, Qt::ElideRight, nameWidth));
            x += nameWidth + kPartGap;
            if (!row.note.isEmpty() && x < nameRight) {
                painter.setFont(noteFont);
                painter.setPen(faint);
                const int noteWidth = qMin(QFontMetrics(noteFont).horizontalAdvance(row.note), nameRight - x);
                painter.drawText(QRect(x, top, noteWidth, kRowHeight), Qt::AlignLeft | Qt::AlignVCenter, row.note);
                x += noteWidth + kPartGap;
            }
            if (!row.chip.isEmpty() && x < nameRight) {
                // `.chip.src`: fondo transparente, borde #333, 18 de alto.
                painter.setFont(chipFont);
                const int chipWidth = QFontMetrics(chipFont).horizontalAdvance(row.chip) + 14;
                const QRectF chipRect(x + 0.5, top + (kRowHeight - 18) / 2.0 + 0.5, chipWidth - 1, 17);
                painter.setPen(QPen(QColor(0x33, 0x33, 0x33), 1)); // el borde de `chip[tone="src"]`
                painter.setBrush(Qt::NoBrush);
                painter.drawRoundedRect(chipRect, 4, 4);
                painter.setPen(Theme::color(Theme::kTextMuted));
                painter.drawText(chipRect, Qt::AlignCenter, row.chip);
                x += chipWidth + kPartGap;
            }
            // La ruta va a 10 del nombre (`.lrow .pth`), no a 6 como las demas piezas.
            x += row.detail.isEmpty() ? 0 : 4;
            available = nameRight - x;
            if (!row.detail.isEmpty() && available > 30) {
                painter.setFont(dimFont);
                painter.setPen(faint);
                painter.drawText(QRect(x, top, available, kRowHeight), Qt::AlignLeft | Qt::AlignVCenter,
                                 QFontMetrics(dimFont).elidedText(row.detail, Qt::ElideLeft, available));
            }
        }

        // -- Columnas.
        int columnX = columnsStart + kColumnGap;
        for (int c = 0; c < m_columns.size(); ++c) {
            const SizeListColumn &column = m_columns.at(c);
            const QRect cell(columnX, top, column.width, kRowHeight);
            const QString value = row.cells.value(c);
            switch (column.kind) {
            case SizeListColumn::Kind::Size: {
                painter.setFont(row.muted ? mutedFont : sizeFont);
                QColor color = row.muted ? faint : strong;
                if (row.tone == 1) {
                    color = Theme::color(Theme::kWarn);
                } else if (row.tone == 2) {
                    color = Theme::color(Theme::kOk);
                }
                painter.setPen(color);
                painter.drawText(cell, Qt::AlignRight | Qt::AlignVCenter, value);
                break;
            }
            case SizeListColumn::Kind::Share: {
                if (row.share >= 0.0) {
                    const QRectF rail(cell.left(), top + (kRowHeight - 4) / 2.0, cell.width() - kShareText - 8, 4);
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(Theme::color(Theme::kTile));
                    painter.drawRoundedRect(rail, 2, 2);
                    QRectF fill = rail;
                    fill.setWidth(qMax(2.0, rail.width() * qBound(0.0, row.share, 1.0)));
                    painter.setBrush(Theme::color(selected ? Theme::kAccent : Theme::kBarFill));
                    painter.drawRoundedRect(fill, 2, 2);
                    painter.setFont(dimFont);
                    painter.setPen(faint);
                    painter.drawText(cell, Qt::AlignRight | Qt::AlignVCenter, value);
                }
                break;
            }
            case SizeListColumn::Kind::Text:
                painter.setFont(dimFont);
                painter.setPen(faint);
                painter.drawText(cell, Qt::AlignRight | Qt::AlignVCenter, value);
                break;
            }
            columnX += column.width + kColumnGap;
        }
    }

    if (m_rows.isEmpty() && !m_emptyText.isEmpty()) {
        painter.setFont(Theme::uiFont(13));
        painter.setPen(faint);
        painter.drawText(QRect(kPadX, kHeaderHeight, width - 2 * kPadX, height - kHeaderHeight), Qt::AlignCenter | Qt::TextWordWrap,
                         m_emptyText);
    }

    // ---- Encabezado, fijo arriba (se pinta al final para tapar la fila que pasa por debajo).
    painter.fillRect(QRect(0, 0, width, kHeaderHeight), Theme::color(Theme::kWindow));
    painter.fillRect(QRect(0, kHeaderHeight - 1, width, 1), Theme::color(Theme::kDivider));
    painter.setFont(headerFont());
    painter.setPen(faint);
    painter.drawText(QRect(kPadX, 0, columnsStart - kPadX, kHeaderHeight - 1), Qt::AlignLeft | Qt::AlignVCenter, m_nameTitle);
    int columnX = columnsStart + kColumnGap;
    for (const SizeListColumn &column : m_columns) {
        painter.setPen(column.sorted ? strong : faint);
        painter.drawText(QRect(columnX, 0, column.width, kHeaderHeight - 1), Qt::AlignRight | Qt::AlignVCenter,
                         column.sorted ? column.title + QStringLiteral(" ↓") : column.title);
        columnX += column.width + kColumnGap;
    }
}
