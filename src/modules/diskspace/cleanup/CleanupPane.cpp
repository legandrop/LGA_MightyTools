#include "modules/diskspace/cleanup/CleanupPane.h"

#include "core/I18n.h"
#include "modules/diskspace/DiskSpace.h"
#include "modules/diskspace/cleanup/CleanupRules.h"
#include "ui/Theme.h"
#include "ui/UiWidgets.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QVBoxLayout>

namespace {

// Medidas del canvas (`.crow`, `.irow`): flecha de 16, casilla de 14, 10 entre piezas, el peso en una
// columna de 126. Un renglon arranca debajo del nombre de su categoria: 16 + 10 + 14 + 10 = 50.
constexpr int kGap = 10;
constexpr int kExpander = 16;
constexpr int kSizeWidth = 126;
constexpr int kItemIndent = kExpander + kGap + 14 + kGap;
constexpr int kVisibleItems = 10;
constexpr int kItemsBeforeFolding = 12;

// Zona que responde al click sin ser un boton (el nombre de una categoria la despliega).
class ClickArea : public QWidget
{
public:
    ClickArea(std::function<void()> onClick, QWidget *parent)
        : QWidget(parent)
        , m_onClick(std::move(onClick))
    {
        if (m_onClick) {
            setCursor(Qt::PointingHandCursor);
        }
    }

protected:
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (m_onClick && event->button() == Qt::LeftButton && rect().contains(event->pos())) {
            m_onClick();
        }
    }

private:
    std::function<void()> m_onClick;
};

// Boton que es solo un icono (la flecha de una categoria).
class GlyphButton : public QAbstractButton
{
public:
    GlyphButton(Icon icon, const QSize &size, int glyph, QWidget *parent)
        : QAbstractButton(parent)
        , m_icon(icon)
        , m_glyph(glyph)
    {
        setFixedSize(size);
        setFocusPolicy(Qt::NoFocus);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        const QRectF glyph((width() - m_glyph) / 2.0, (height() - m_glyph) / 2.0, m_glyph, m_glyph);
        Icons::paint(painter, m_icon, glyph, Theme::color(underMouse() ? Theme::kTextBright : Theme::kIcon));
    }

private:
    Icon m_icon;
    int m_glyph;
};

QLabel *sizeLabel(const QString &text, bool dim, QWidget *parent)
{
    auto *label = Ui::label(text, "cleanSize", parent);
    label->setTextFormat(Qt::RichText);
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    label->setFixedWidth(kSizeWidth);
    if (dim) {
        label->setProperty("dim", true);
    }
    return label;
}

QFrame *rowRule(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setObjectName(QStringLiteral("divider"));
    return line;
}

qint64 totalBytes(const Cleanup::Category &category)
{
    qint64 total = 0;
    for (const Cleanup::Item &item : category.items) {
        total += item.bytes;
    }
    return total;
}

qint64 checkedBytes(const Cleanup::Category &category)
{
    qint64 total = 0;
    for (const Cleanup::Item &item : category.items) {
        if (item.checked && !item.blocked) {
            total += item.bytes;
        }
    }
    return total;
}

} // namespace

// ------------------------------------------------------------------ TriBox

TriBox::TriBox(QWidget *parent)
    : QAbstractButton(parent)
{
    setFixedSize(14, 14);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
}

void TriBox::setState(int state)
{
    if (state == m_state) {
        return;
    }
    m_state = state;
    update();
}

void TriBox::paintEvent(QPaintEvent *)
{
    // `.box` del canvas: 14 x 14, radio 3; tildada o a medias con el violeta de "elegido".
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setOpacity(isEnabled() ? 1.0 : 0.35);
    const bool on = m_state != 0;
    painter.setPen(QPen(Theme::color(on ? Theme::kChosenBorder : Theme::kCheckOffBorder), 1.0));
    painter.setBrush(Theme::color(on ? Theme::kChosenBg : Theme::kCheckOffBg));
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
    if (on) {
        Icons::paint(painter, m_state == 1 ? Icon::Dash : Icon::Check, QRectF(2, 2, 10, 10), Theme::color(Theme::kChosenText));
    }
}

// ------------------------------------------------------------------ CleanupPane

CleanupPane::CleanupPane(QWidget *parent)
    : QScrollArea(parent)
{
    setObjectName(QStringLiteral("cleanScroll"));
    setFrameShape(QFrame::NoFrame);
    setWidgetResizable(true);
    setFocusPolicy(Qt::NoFocus);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rebuild();
}

void CleanupPane::setCategories(const QList<Cleanup::Category> &categories, bool measuring)
{
    m_categories = categories;
    m_measuring = measuring;
    rebuild();
}

void CleanupPane::setBanner(const QString &text)
{
    if (text == m_banner) {
        return;
    }
    m_banner = text;
    rebuild();
}

void CleanupPane::setSkipped(const QString &summary, const QList<SkippedLine> &lines)
{
    m_skippedSummary = summary;
    m_skipped = lines;
    rebuild();
}

void CleanupPane::setInteractive(bool interactive)
{
    if (interactive == m_interactive) {
        return;
    }
    m_interactive = interactive;
    rebuild();
}

void CleanupPane::setOpen(const QString &categoryId, bool open)
{
    if (open) {
        m_open.insert(categoryId);
    } else {
        m_open.remove(categoryId);
    }
    rebuild();
}

void CleanupPane::rebuild()
{
    const int scroll = verticalScrollBar()->value();
    auto *content = new QWidget(this);
    content->setObjectName(QStringLiteral("cleanContent"));
    auto *column = new QVBoxLayout(content);
    // `.pane.pad` del canvas: 12 / 14 de relleno y 10 entre tarjetas.
    column->setContentsMargins(14, 12, 14, 12);
    column->setSpacing(10);

    if (!m_banner.isEmpty()) {
        auto *banner = new QFrame(content);
        banner->setObjectName(QStringLiteral("okBanner"));
        auto *row = new QHBoxLayout(banner);
        row->setContentsMargins(12, 9, 12, 9);
        row->setSpacing(10);
        row->addWidget(new IconWidget(Icon::Check, Theme::color(Theme::kOk), 10, banner), 0, Qt::AlignVCenter);
        row->addWidget(Ui::label(m_banner, "bannerText", banner), 1);
        column->addWidget(banner);
    }
    if (!m_skipped.isEmpty()) {
        QFrame *card = Ui::card(content);
        auto *layout = new QVBoxLayout(card);
        layout->setContentsMargins(14, 12, 14, 12);
        layout->setSpacing(0);
        auto *head = new QHBoxLayout();
        head->setSpacing(6);
        head->addWidget(Ui::label(I18n::tr("Skipped"), "cardTitle", card), 1);
        auto *chip = new Chip(card);
        chip->set(QStringLiteral("src"), m_skippedSummary);
        head->addWidget(chip, 0, Qt::AlignVCenter);
        layout->addLayout(head);
        layout->addSpacing(8);
        layout->addWidget(Ui::caption(I18n::tr("In use by a running program, or not allowed. They can go next time, once that program is closed."), card));
        Ui::addDivider(layout, card);
        for (const SkippedLine &line : m_skipped) {
            auto *row = new QHBoxLayout();
            row->setSpacing(8);
            QLabel *name = Ui::label(line.name, "optionLabel", card);
            name->setMinimumHeight(22);
            row->addWidget(name, 1);
            row->addWidget(Ui::label(line.detail, "meta", card), 0);
            layout->addLayout(row);
        }
        column->addWidget(card);
    }

    for (const Cleanup::Group group : {Cleanup::Group::Safe, Cleanup::Group::Admin, Cleanup::Group::Yours}) {
        if (QWidget *card = groupCard(group, content)) {
            column->addWidget(card);
        }
    }

    // "Add a folder rule..." va siempre: es la forma de sumar carpetas propias a "Yours to decide".
    auto *add = Ui::button(I18n::tr("Add a folder rule..."), QString(), QString(), content);
    add->setObjectName(QStringLiteral("linkButton"));
    Ui::setIcon(add, Icon::Plus, Theme::color(Theme::kLink), 10);
    add->setEnabled(m_interactive);
    connect(add, &QPushButton::clicked, this, &CleanupPane::addRuleRequested);
    auto *addRow = new QHBoxLayout();
    addRow->setContentsMargins(0, 0, 0, 0);
    addRow->addWidget(add, 0, Qt::AlignLeft);
    addRow->addStretch(1);
    column->addLayout(addRow);
    column->addStretch(1);

    // El contenido anterior se borra DESPUES: esto puede venir del click de uno de sus botones.
    if (QWidget *old = takeWidget()) {
        old->hide();
        old->deleteLater();
    }
    setWidget(content);
    content->show();
    verticalScrollBar()->setValue(scroll);
}

QWidget *CleanupPane::groupCard(Cleanup::Group group, QWidget *parent)
{
    QList<const Cleanup::Category *> categories;
    qint64 total = 0;
    for (const Cleanup::Category &category : m_categories) {
        if (category.group == group) {
            categories.append(&category);
            if (!category.info) {
                total += totalBytes(category);
            }
        }
    }
    if (categories.isEmpty()) {
        return nullptr;
    }

    QFrame *card = Ui::card(parent);
    auto *layout = new QVBoxLayout(card);
    // `.card.grp`: 10 / 14 / 6.
    layout->setContentsMargins(14, 10, 14, 6);
    layout->setSpacing(0);

    QString title;
    QString caption;
    switch (group) {
    case Cleanup::Group::Safe:
        title = I18n::tr("Safe to delete");
        caption = I18n::tr("Caches and leftovers. Programs build or download them again when they need them; nothing of yours is lost. "
                           "Open a row to pick what goes.");
        break;
    case Cleanup::Group::Admin:
        title = I18n::tr("Needs administrator");
        caption = I18n::tr("Windows cleans these itself, with administrator permission. Here you only see what they weigh.");
        break;
    case Cleanup::Group::Yours:
        title = I18n::tr("Yours to decide");
        caption = I18n::tr("Big things that look disposable but only you know. Nothing here is ticked for you: open a row and tick what can go.");
        break;
    }

    auto *head = new QHBoxLayout();
    head->setContentsMargins(0, 0, 0, 0);
    head->setSpacing(8);
    QLabel *titleLabel = Ui::label(title, "cardTitle", card);
    titleLabel->setMinimumHeight(22);
    head->addWidget(titleLabel, 1);
    if (group == Cleanup::Group::Admin) {
        auto *open = Ui::button(I18n::tr("Open Windows cleanup"), QString(), QStringLiteral("sm"), card);
        open->setEnabled(m_interactive);
        connect(open, &QPushButton::clicked, this, &CleanupPane::systemCleanupRequested);
        head->addWidget(open, 0, Qt::AlignVCenter);
    } else if (!m_measuring && total > 0) {
        auto *chip = new Chip(card);
        chip->set(QStringLiteral("src"), DiskSpace::formatSize(total));
        head->addWidget(chip, 0, Qt::AlignVCenter);
    }
    layout->addLayout(head);
    layout->addSpacing(2);
    auto *captionLabel = new CaptionLabel(caption, card, 16);
    captionLabel->setObjectName(QStringLiteral("cleanGroupCaption"));
    layout->addWidget(captionLabel);
    layout->addSpacing(8);

    for (const Cleanup::Category *category : categories) {
        addCategory(layout, *category, card);
    }
    return card;
}

void CleanupPane::addCategory(QVBoxLayout *layout, const Cleanup::Category &category, QWidget *parent)
{
    layout->addWidget(rowRule(parent));
    const QString id = category.id;
    const bool expandable = !category.single && !category.info && !category.items.isEmpty();
    const bool open = expandable && m_open.contains(id);
    const int state = CleanupRules::checkState(category);
    const qint64 total = totalBytes(category);
    int checkable = 0;
    int checked = 0;
    for (const Cleanup::Item &item : category.items) {
        if (!item.blocked) {
            ++checkable;
            checked += item.checked ? 1 : 0;
        }
    }
    const Cleanup::Item *only = category.single && !category.items.isEmpty() ? &category.items.first() : nullptr;

    // ---- La fila de la categoria.
    auto *row = new QWidget(parent);
    row->setMinimumHeight(44);
    auto *line = new QHBoxLayout(row);
    line->setContentsMargins(0, 5, 0, 5);
    line->setSpacing(kGap);

    if (expandable) {
        auto *expander = new GlyphButton(open ? Icon::TreeOpen : Icon::TreeClosed, QSize(kExpander, 20), 10, row);
        expander->setEnabled(m_interactive);
        expander->setAccessibleName(open ? I18n::tr("Hide what is inside") : I18n::tr("Show what is inside"));
        connect(expander, &QAbstractButton::clicked, this, [this, id, open]() { setOpen(id, !open); });
        line->addWidget(expander, 0, Qt::AlignVCenter);
    } else {
        // Un hueco del mismo ancho que la flecha: la casilla queda en la misma columna en todas las filas.
        auto *gap = new QWidget(row);
        gap->setFixedSize(kExpander, 1);
        line->addWidget(gap);
    }

    if (category.info) {
        auto *gap = new QWidget(row);
        gap->setFixedSize(14, 1);
        line->addWidget(gap);
    } else {
        auto *box = new TriBox(row);
        box->setState(state);
        box->setEnabled(m_interactive && checkable > 0);
        connect(box, &QAbstractButton::clicked, this, [this, id]() { emit categoryToggled(id); });
        line->addWidget(box, 0, Qt::AlignVCenter);
    }

    std::function<void()> toggleOpen;
    if (expandable && m_interactive) {
        toggleOpen = [this, id, open]() { setOpen(id, !open); };
    }
    auto *texts = new ClickArea(toggleOpen, row);
    auto *textColumn = new QVBoxLayout(texts);
    textColumn->setContentsMargins(0, 0, 0, 0);
    textColumn->setSpacing(0);
    QLabel *name = Ui::label(category.title, "cleanName", texts);
    name->setAttribute(Qt::WA_TransparentForMouseEvents);
    textColumn->addWidget(name);
    auto *caption = new CaptionLabel(category.caption, texts, 16);
    caption->setObjectName(QStringLiteral("cleanCaption"));
    caption->setAttribute(Qt::WA_TransparentForMouseEvents);
    textColumn->addWidget(caption);
    line->addWidget(texts, 1);

    // A la derecha del nombre: cuantos renglones, el motivo del bloqueo o "Review".
    if (!category.reviewPath.isEmpty()) {
        auto *review = Ui::button(I18n::tr("Review"), QString(), QString(), row);
        review->setObjectName(QStringLiteral("linkButton"));
        review->setEnabled(m_interactive);
        const QString path = category.reviewPath;
        connect(review, &QPushButton::clicked, this, [this, path]() { emit reviewRequested(path); });
        line->addWidget(review, 0, Qt::AlignVCenter);
    } else if (only && only->blocked) {
        QLabel *blocked = Ui::label(I18n::tr("%1 is open").arg(only->blockerLabel), "meta", row);
        blocked->setProperty("tone", QStringLiteral("warn"));
        line->addWidget(blocked, 0, Qt::AlignVCenter);
    } else if (expandable) {
        const int count = int(category.items.size());
        QString meta;
        if (state == 1) {
            meta = I18n::tr("%1 of %2 items").arg(checked).arg(count);
        } else {
            meta = count == 1 ? I18n::tr("1 item") : I18n::tr("%1 items").arg(count);
        }
        line->addWidget(Ui::label(meta, "meta", row), 0, Qt::AlignVCenter);
    }
    if (id.startsWith(QLatin1String("rule:"))) {
        auto *remove = Ui::button(QString(), QStringLiteral("ghost"), QStringLiteral("icon"), row);
        Ui::setIcon(remove, Icon::X, Theme::color(Theme::kIcon), 12);
        remove->setToolTip(I18n::tr("Remove this rule (deletes nothing)"));
        remove->setAccessibleName(I18n::tr("Remove this rule (deletes nothing)"));
        remove->setEnabled(m_interactive);
        connect(remove, &QPushButton::clicked, this, [this, id]() { emit removeRuleRequested(id); });
        line->addWidget(remove, 0, Qt::AlignVCenter);
    }

    QString sizeText;
    if (!category.measurable) {
        sizeText = QStringLiteral("—");
    } else if (m_measuring) {
        sizeText = QStringLiteral("…");
    } else if (state == 1 && !category.info) {
        sizeText = QStringLiteral("%1 <span style=\"color:%2; font-weight:400\">%3</span>")
                       .arg(DiskSpace::formatSize(checkedBytes(category)), QLatin1String(Theme::kTextFaint),
                            I18n::tr("of %1").arg(DiskSpace::formatSize(total)));
    } else {
        sizeText = DiskSpace::formatSize(total);
    }
    line->addWidget(sizeLabel(sizeText, m_measuring || !category.measurable, row), 0, Qt::AlignVCenter);
    layout->addWidget(row);

    if (!open) {
        return;
    }

    // ---- Sus renglones.
    const QDateTime now = QDateTime::currentDateTime();
    const bool folded = category.items.size() > kItemsBeforeFolding && !m_showAll.contains(id);
    const int visible = folded ? kVisibleItems : int(category.items.size());
    for (int i = 0; i < visible; ++i) {
        const Cleanup::Item &item = category.items.at(i);
        auto *itemRow = new QWidget(parent);
        itemRow->setObjectName(QStringLiteral("cleanItem"));
        itemRow->setAttribute(Qt::WA_StyledBackground, true);
        itemRow->setMinimumHeight(28);
        auto *itemLine = new QHBoxLayout(itemRow);
        itemLine->setContentsMargins(kItemIndent, 0, 0, 0);
        itemLine->setSpacing(kGap);
        auto *box = new TriBox(itemRow);
        box->setState(item.checked && !item.blocked ? 2 : 0);
        box->setEnabled(m_interactive && !item.blocked);
        connect(box, &QAbstractButton::clicked, this, [this, id, i]() { emit itemToggled(id, i); });
        itemLine->addWidget(box, 0, Qt::AlignVCenter);

        QLabel *itemName = Ui::label(item.name, "itemName", itemRow);
        itemName->setProperty("off", !item.checked || item.blocked);
        itemLine->addWidget(itemName, 0, Qt::AlignVCenter);
        auto *path = new ElidedLabel(itemRow);
        path->setObjectName(QStringLiteral("itemPath"));
        path->setElideMode(Qt::ElideLeft);
        path->setText(item.path);
        itemLine->addWidget(path, 1, Qt::AlignVCenter);

        QLabel *age = Ui::label(item.blocked ? I18n::tr("%1 is open").arg(item.blockerLabel) : DiskSpace::ageText(item.newest, now),
                                "meta", itemRow);
        if (item.blocked) {
            age->setProperty("tone", QStringLiteral("warn"));
            age->setToolTip(I18n::tr("Close it to clean this."));
        }
        age->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        age->setMinimumWidth(64);
        itemLine->addWidget(age, 0, Qt::AlignVCenter);
        QLabel *size = sizeLabel(m_measuring ? QStringLiteral("…") : DiskSpace::formatSize(item.bytes),
                                 m_measuring || !item.checked || item.blocked, itemRow);
        size->setObjectName(QStringLiteral("itemSize"));
        itemLine->addWidget(size, 0, Qt::AlignVCenter);
        layout->addWidget(itemRow);
    }
    if (folded) {
        qint64 restBytes = 0;
        for (int i = visible; i < category.items.size(); ++i) {
            restBytes += category.items.at(i).bytes;
        }
        auto *more = Ui::button(I18n::tr("Show %1 more · %2").arg(category.items.size() - visible).arg(DiskSpace::formatSize(restBytes)),
                                QString(), QString(), parent);
        more->setObjectName(QStringLiteral("linkButton"));
        more->setEnabled(m_interactive);
        connect(more, &QPushButton::clicked, this, [this, id]() {
            m_showAll.insert(id);
            rebuild();
        });
        auto *moreRow = new QHBoxLayout();
        moreRow->setContentsMargins(kItemIndent, 6, 0, 2);
        moreRow->addWidget(more, 0, Qt::AlignLeft);
        moreRow->addStretch(1);
        layout->addLayout(moreRow);
    }
    layout->addSpacing(6);
}
