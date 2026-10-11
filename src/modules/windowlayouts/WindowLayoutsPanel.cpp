#include "modules/windowlayouts/WindowLayoutsPanel.h"

#include "core/I18n.h"
#include "modules/windowlayouts/WindowLayoutsModule.h"
#include "platform/WindowPlacement.h"
#include "ui/ShortcutRow.h"
#include "ui/Theme.h"
#include "ui/UiWidgets.h"

#include <QComboBox>
#include <QGuiApplication>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {

struct Preset
{
    LayoutGeometry::AxisSlice horizontal;
    LayoutGeometry::AxisSlice vertical;
};

const QList<Preset> &presets()
{
    static const QList<Preset> list = {
        {{1, 0}, {1, 0}},
        {{2, 0}, {1, 0}}, {{2, 1}, {1, 0}},
        {{1, 0}, {2, 0}}, {{1, 0}, {2, 1}},
        {{2, 0}, {2, 0}}, {{2, 1}, {2, 0}},
        {{2, 0}, {2, 1}}, {{2, 1}, {2, 1}},
        {{3, 0}, {1, 0}}, {{3, 1}, {1, 0}},
        {{3, 2}, {1, 0}}, {{1, 0}, {3, 0}},
        {{1, 0}, {3, 1}}, {{1, 0}, {3, 2}},
    };
    return list;
}

bool sameAxis(LayoutGeometry::AxisSlice a, LayoutGeometry::AxisSlice b)
{
    return a.parts == b.parts && a.index == b.index;
}

int axisKey(LayoutGeometry::AxisSlice axis)
{
    return axis.parts * 10 + axis.index;
}

QString axisText(LayoutGeometry::AxisSlice axis, bool horizontal)
{
    if (!axis.valid()) return QString();
    if (horizontal) {
        if (axis.parts == 1) return I18n::tr("Full width");
        if (axis.parts == 2 && axis.index == 0) return I18n::tr("Left half");
        if (axis.parts == 2) return I18n::tr("Right half");
        if (axis.parts == 3 && axis.index == 0) return I18n::tr("Left third");
        if (axis.parts == 3 && axis.index == 1) return I18n::tr("Center third");
        if (axis.parts == 3) return I18n::tr("Right third");
        if (axis.index == 0) return I18n::tr("Left quarter");
        if (axis.index == 1) return I18n::tr("Second quarter");
        if (axis.index == 2) return I18n::tr("Third quarter");
        return I18n::tr("Right quarter");
    }
    if (axis.parts == 1) return I18n::tr("Full height");
    if (axis.parts == 2 && axis.index == 0) return I18n::tr("Top half");
    if (axis.parts == 2) return I18n::tr("Bottom half");
    if (axis.parts == 3 && axis.index == 0) return I18n::tr("Top third");
    if (axis.parts == 3 && axis.index == 1) return I18n::tr("Middle third");
    if (axis.parts == 3) return I18n::tr("Bottom third");
    if (axis.index == 0) return I18n::tr("Top quarter");
    if (axis.index == 1) return I18n::tr("Second quarter");
    if (axis.index == 2) return I18n::tr("Third quarter");
    return I18n::tr("Bottom quarter");
}

QString presetText(int index)
{
    switch (index) {
    case 0: return I18n::tr("Full screen");
    case 1: return I18n::tr("Left half");
    case 2: return I18n::tr("Right half");
    case 3: return I18n::tr("Top half");
    case 4: return I18n::tr("Bottom half");
    case 5: return I18n::tr("Top-left quarter");
    case 6: return I18n::tr("Top-right quarter");
    case 7: return I18n::tr("Bottom-left quarter");
    case 8: return I18n::tr("Bottom-right quarter");
    case 9: return I18n::tr("Left third");
    case 10: return I18n::tr("Center third");
    case 11: return I18n::tr("Right third");
    case 12: return I18n::tr("Top third");
    case 13: return I18n::tr("Middle third");
    case 14: return I18n::tr("Bottom third");
    default: return QString();
    }
}

void addAxisOptions(QComboBox *box, bool horizontal)
{
    for (int parts = 2; parts <= 4; ++parts) {
        for (int index = 0; index < parts; ++index) {
            const LayoutGeometry::AxisSlice axis{parts, index};
            box->addItem(axisText(axis, horizontal), axisKey(axis));
        }
    }
    box->insertItem(0, axisText({1, 0}, horizontal), axisKey({1, 0}));
    box->setFocusPolicy(Qt::NoFocus);
}

LayoutGeometry::AxisSlice selectedAxis(QComboBox *box)
{
    const int key = box->currentData().toInt();
    return {key / 10, key % 10};
}

QString description(const WindowLayout &layout)
{
    if (layout.destination == LayoutDestination::Custom) {
        return I18n::tr("Custom area · active window");
    }
    return I18n::tr("%1 × %2").arg(axisText(layout.horizontal, true), axisText(layout.vertical, false));
}

class MiniLayout : public QWidget
{
public:
    explicit MiniLayout(QWidget *parent = nullptr) : QWidget(parent) { setFixedSize(47, 31); }
    void setArea(const QRectF &area) { m_area = area; update(); }
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setPen(Theme::color(Theme::kBorderStrong));
        painter.setBrush(Theme::color(Theme::kWindow));
        painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 3, 3);
        if (LayoutGeometry::validArea(m_area)) {
            const QRectF inner = QRectF(rect()).adjusted(4, 4, -4, -4);
            painter.setPen(Theme::color(Theme::kLink));
            painter.setBrush(Theme::color(Theme::kPrimary));
            painter.drawRect(QRectF(inner.left() + m_area.x() * inner.width(),
                                    inner.top() + m_area.y() * inner.height(),
                                    m_area.width() * inner.width(), m_area.height() * inner.height()));
        }
    }
private:
    QRectF m_area;
};

} // namespace

class WorkAreaPreview : public QWidget
{
public:
    explicit WorkAreaPreview(bool captureMode, QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedHeight(108);
        setObjectName(QStringLiteral("windowLayoutsPreview"));
        if (captureMode) {
            m_screen = QRect(0, 0, 1000, 800);
            m_workArea = QRect(0, 0, 1000, 760);
        } else if (QScreen *screen = QGuiApplication::primaryScreen()) {
            m_screen = screen->geometry();
            m_workArea = WindowPlacement::pickerWorkAreaAt(m_screen.center());
        }
    }

    void setArea(const QRectF &area) { m_area = area; update(); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF border = QRectF(rect()).adjusted(.5, .5, -.5, -.5);
        painter.setPen(Theme::color(Theme::kBorderStrong));
        painter.setBrush(Theme::color(Theme::kWindow));
        painter.drawRoundedRect(border, 5, 5);
        const QRectF screen = border.adjusted(9, 8, -9, -8);
        painter.setBrush(Theme::color(Theme::kTile));
        painter.drawRect(screen);
        if (m_screen.isEmpty() || m_workArea.isEmpty()) {
            return;
        }
        const QRectF work(screen.left() + double(m_workArea.left() - m_screen.left()) / m_screen.width() * screen.width(),
                          screen.top() + double(m_workArea.top() - m_screen.top()) / m_screen.height() * screen.height(),
                          double(m_workArea.width()) / m_screen.width() * screen.width(),
                          double(m_workArea.height()) / m_screen.height() * screen.height());
        painter.setPen(QPen(Theme::color(Theme::kTextMuted), 1, Qt::DashLine));
        painter.setBrush(Theme::color(Theme::kModIconBg));
        painter.drawRect(work);
        if (!LayoutGeometry::validArea(m_area)) {
            return;
        }
        const QRectF selected(work.left() + m_area.x() * work.width(),
                              work.top() + m_area.y() * work.height(),
                              m_area.width() * work.width(), m_area.height() * work.height());
        painter.setPen(Theme::color(Theme::kLink));
        painter.setBrush(Theme::color(Theme::kPrimary));
        painter.drawRect(selected);
    }

private:
    QRect m_screen;
    QRect m_workArea;
    QRectF m_area;
};

WindowLayoutsPanel::WindowLayoutsPanel(WindowLayoutsModule *module, bool interactive, QWidget *parent)
    : QWidget(parent), m_module(module), m_interactive(interactive)
{
    setObjectName(QStringLiteral("windowLayoutsPanel"));
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(10);
    m_pages = new QStackedWidget(this);
    m_listPage = createListPage();
    m_editorPage = createEditorPage();
    m_pages->addWidget(m_listPage);
    m_pages->addWidget(m_editorPage);
    column->addWidget(m_pages);
    refresh();
}

QWidget *WindowLayoutsPanel::createListPage()
{
    auto *page = new QWidget(this);
    auto *column = new QVBoxLayout(page);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(10);
    m_statusCard = new StatusCard(page);
    column->addWidget(m_statusCard);
    if (m_interactive) {
        connect(m_statusCard->button(), &QPushButton::clicked, this,
                [this]() { m_module->setPaused(!m_module->layoutsState().paused()); });
    }
    auto *saved = Ui::card(page);
    auto *savedColumn = new QVBoxLayout(saved);
    savedColumn->setContentsMargins(14, 12, 14, 12);
    savedColumn->setSpacing(8);
    savedColumn->addWidget(Ui::label(I18n::tr("Saved layouts"), "cardTitle", saved));
    m_rows = new QWidget(saved);
    m_rows->setLayout(new QVBoxLayout());
    m_rows->layout()->setContentsMargins(0, 0, 0, 0);
    m_rows->layout()->setSpacing(0);
    savedColumn->addWidget(m_rows);
    auto *hint = Ui::caption(I18n::tr("Shortcuts move the window in front. Fractions use the available area of its monitor."), saved);
    hint->setWordWrap(true);
    savedColumn->addWidget(hint);
    column->addWidget(saved);
    auto *addCard = Ui::card(page);
    auto *addRow = new QHBoxLayout(addCard);
    addRow->setContentsMargins(14, 12, 14, 12);
    auto *texts = new QVBoxLayout();
    texts->addWidget(Ui::label(I18n::tr("Create another layout"), "cardTitle", addCard));
    auto *caption = Ui::caption(I18n::tr("Combine fractions, draw an area, or copy a window's bounds."), addCard);
    caption->setWordWrap(true);
    texts->addWidget(caption);
    addRow->addLayout(texts, 1);
    auto *add = Ui::button(I18n::tr("Add layout"), QStringLiteral("primary"), QString(), addCard);
    addRow->addWidget(add, 0, Qt::AlignVCenter);
    if (m_interactive) connect(add, &QPushButton::clicked, this, [this]() { editLayout(); });
    column->addWidget(addCard);
    column->addStretch();
    return page;
}

QWidget *WindowLayoutsPanel::createEditorPage()
{
    auto *page = new QWidget(this);
    auto *column = new QVBoxLayout(page);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(6);
    auto *back = Ui::button(I18n::tr("‹ Saved layouts"), QStringLiteral("ghost"), QString(), page);
    column->addWidget(back, 0, Qt::AlignLeft);
    if (m_interactive) connect(back, &QPushButton::clicked, this, [this]() {
        resetDeleteConfirmation();
        m_pages->setCurrentWidget(m_listPage);
    });
    auto *card = Ui::card(page);
    auto *form = new QVBoxLayout(card);
    form->setContentsMargins(14, 12, 14, 12);
    form->setSpacing(6);
    form->addWidget(Ui::label(I18n::tr("New layout"), "cardTitle", card));
    form->addWidget(Ui::caption(I18n::tr("Name"), card));
    m_name = new QLineEdit(card);
    m_name->setFocusPolicy(Qt::ClickFocus);
    m_name->setObjectName(QStringLiteral("ruleField"));
    form->addWidget(m_name);
    form->addWidget(Ui::caption(I18n::tr("Quick preset"), card));
    m_preset = new QComboBox(card);
    m_preset->setFocusPolicy(Qt::NoFocus);
    for (int i = 0; i < presets().size(); ++i) m_preset->addItem(presetText(i));
    m_preset->addItem(I18n::tr("Custom mix"));
    m_preset->addItem(I18n::tr("Custom area"));
    form->addWidget(m_preset);
    auto *axes = new QHBoxLayout();
    axes->setSpacing(9);
    for (int which = 0; which < 2; ++which) {
        auto *axisColumn = new QVBoxLayout();
        axisColumn->setSpacing(5);
        axisColumn->addWidget(Ui::caption(which == 0 ? I18n::tr("Horizontal") : I18n::tr("Vertical"), card));
        auto *box = new QComboBox(card);
        addAxisOptions(box, which == 0);
        axisColumn->addWidget(box);
        axes->addLayout(axisColumn, 1);
        (which == 0 ? m_horizontal : m_vertical) = box;
    }
    form->addLayout(axes);
    m_preview = new WorkAreaPreview(!m_interactive, card);
    form->addWidget(m_preview);
    m_workAreaText = Ui::caption(I18n::tr("Available area excludes visible taskbar, Dock and menu bar."), card);
    form->addWidget(m_workAreaText);
    auto *methods = new QHBoxLayout();
    auto *draw = Ui::button(I18n::tr("Draw an area..."), QString(), QStringLiteral("sm"), card);
    auto *pick = Ui::button(I18n::tr("Pick a window..."), QString(), QStringLiteral("sm"), card);
    methods->addWidget(draw);
    methods->addWidget(pick);
    form->addLayout(methods);
    auto *methodHint = Ui::caption(I18n::tr("Draw a rectangle or copy an open window's bounds instead of using fractions."), card);
    methodHint->setWordWrap(true);
    form->addWidget(methodHint);
    m_shortcut = new ShortcutRow(I18n::tr("Shortcut"), I18n::tr("Moves the window in front to this area."), card);
    form->addWidget(m_shortcut);
    m_editorError = Ui::caption(QString(), card);
    Ui::setStyleProperty(m_editorError, "tone", QStringLiteral("err"));
    m_editorError->hide();
    form->addWidget(m_editorError);
    Ui::addDivider(form, card);
    auto *actions = new QHBoxLayout();
    m_remove = Ui::button(I18n::tr("Delete"), QStringLiteral("ghost"), QStringLiteral("sm"), card);
    m_remove->setObjectName(QStringLiteral("removeLayout"));
    actions->addWidget(m_remove);
    actions->addStretch();
    auto *cancel = Ui::button(I18n::tr("Cancel"), QString(), QStringLiteral("sm"), card);
    auto *save = Ui::button(I18n::tr("Save layout"), QStringLiteral("primary"), QStringLiteral("sm"), card);
    actions->addWidget(cancel);
    actions->addWidget(save);
    form->addLayout(actions);
    column->addWidget(card);
    column->addStretch();
    if (m_interactive) {
        connect(m_preset, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &WindowLayoutsPanel::applyQuickPreset);
        auto axisChanged = [this]() {
            m_draft.name = m_name->text();
            if (m_autoName) m_draft.name = I18n::tr("Custom mix");
            m_draft.destination = LayoutDestination::Grid;
            m_draft.horizontal = selectedAxis(m_horizontal);
            m_draft.vertical = selectedAxis(m_vertical);
            updateEditor();
        };
        connect(m_horizontal, QOverload<int>::of(&QComboBox::currentIndexChanged), this, axisChanged);
        connect(m_vertical, QOverload<int>::of(&QComboBox::currentIndexChanged), this, axisChanged);
        connect(m_shortcut, &ShortcutRow::shortcutRecorded, this, [this](const Shortcut &shortcut) {
            m_draft.shortcut = shortcut;
            m_shortcut->setShortcut(shortcut);
        });
        m_shortcut->setValidator([this](const Shortcut &shortcut) {
            return m_module->validateShortcut(m_draft.id, shortcut);
        });
        connect(draw, &QPushButton::clicked, this, [this]() { selectArea(false); });
        connect(pick, &QPushButton::clicked, this, [this]() { selectArea(true); });
        connect(cancel, &QPushButton::clicked, this, [this]() {
            resetDeleteConfirmation();
            m_pages->setCurrentWidget(m_listPage);
        });
        connect(save, &QPushButton::clicked, this, &WindowLayoutsPanel::saveEditor);
        connect(m_name, &QLineEdit::returnPressed, this, &WindowLayoutsPanel::saveEditor);
        connect(m_name, &QLineEdit::textEdited, this, [this]() { m_autoName = false; });
        connect(m_remove, &QPushButton::clicked, this, [this]() {
            if (!m_draft.id) return;
            if (!m_confirmDelete) {
                m_confirmDelete = true;
                m_remove->setText(I18n::tr("Confirm delete"));
                Ui::setStyleProperty(m_remove, "variant", QStringLiteral("danger"));
                return;
            }
            if (m_module->removeLayout(m_draft.id)) {
                resetDeleteConfirmation();
                m_pages->setCurrentWidget(m_listPage);
            }
        });
    }
    return page;
}

void WindowLayoutsPanel::refresh()
{
    const WindowLayoutsState &state = m_module->layoutsState();
    const int count = state.layouts().size();
    const QString error = m_module->lastError();
    if (state.paused()) {
        m_statusCard->set(QStringLiteral("paused"), I18n::tr("Window shortcuts are paused"),
                          I18n::tr("Resume when you want to use them."), I18n::tr("Resume"), QStringLiteral("primary"), QString());
    } else if (!error.isEmpty()) {
        m_statusCard->set(QStringLiteral("error"), I18n::tr("Window shortcuts need attention"), error,
                          I18n::tr("Pause"), QString(), QStringLiteral("err"));
    } else {
        m_statusCard->set(QStringLiteral("on"), I18n::tr("Window shortcuts are on"),
                          count == 0 ? I18n::tr("No layouts yet. Add one to get started.")
                          : count == 1 ? I18n::tr("1 layout ready to use.") : I18n::tr("%1 layouts ready to use.").arg(count),
                          I18n::tr("Pause"), QString(), QString());
    }
    if (m_pages->currentWidget() == m_editorPage && !error.isEmpty()) {
        m_editorError->setText(error);
        m_editorError->show();
    }
    rebuildList();
}

void WindowLayoutsPanel::rebuildList()
{
    auto *column = static_cast<QVBoxLayout *>(m_rows->layout());
    while (QLayoutItem *item = column->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const QList<WindowLayout> layouts = m_module->layoutsState().layouts();
    if (layouts.isEmpty()) {
        auto *empty = Ui::caption(I18n::tr("No layouts yet. Add one to choose an area and a shortcut."), m_rows);
        empty->setWordWrap(true);
        column->addWidget(empty);
        return;
    }
    for (const WindowLayout &layout : layouts) {
        auto *row = new QWidget(m_rows);
        auto *line = new QHBoxLayout(row);
        line->setContentsMargins(0, 7, 0, 7);
        line->setSpacing(9);
        auto *mini = new MiniLayout(row);
        mini->setArea(layout.normalizedArea());
        line->addWidget(mini);
        auto *texts = new QVBoxLayout();
        texts->setSpacing(2);
        auto *name = Ui::label(layout.name, "cardTitle", row);
        name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        texts->addWidget(name);
        auto *detail = Ui::caption(description(layout), row);
        texts->addWidget(detail);
        line->addLayout(texts, 1);
        auto *shortcut = Ui::caption(layout.shortcut.displayText(), row);
        line->addWidget(shortcut);
        auto *edit = Ui::button(I18n::tr("Edit"), QStringLiteral("ghost"), QStringLiteral("sm"), row);
        line->addWidget(edit);
        if (m_interactive) connect(edit, &QPushButton::clicked, this, [this, id = layout.id]() { editLayout(id); });
        column->addWidget(row);
        Ui::addDivider(column, m_rows);
    }
}

void WindowLayoutsPanel::editLayout(int id)
{
    resetDeleteConfirmation();
    m_draft = WindowLayout();
    m_draft.name = I18n::tr("Left third");
    m_draft.horizontal = {3, 0};
    m_draft.vertical = {1, 0};
    m_autoName = id == 0;
    for (const WindowLayout &layout : m_module->layoutsState().layouts()) {
        if (layout.id == id) {
            m_draft = layout;
            break;
        }
    }
    m_editorError->hide();
    updateEditor();
    m_pages->setCurrentWidget(m_editorPage);
    m_remove->setVisible(id != 0);
}

void WindowLayoutsPanel::resetDeleteConfirmation()
{
    m_confirmDelete = false;
    if (m_remove) {
        m_remove->setText(I18n::tr("Delete"));
        Ui::setStyleProperty(m_remove, "variant", QStringLiteral("ghost"));
    }
}

void WindowLayoutsPanel::hideEvent(QHideEvent *event)
{
    resetDeleteConfirmation();
    QWidget::hideEvent(event);
}

void WindowLayoutsPanel::updateEditor()
{
    const QSignalBlocker blockPreset(m_preset);
    const QSignalBlocker blockHorizontal(m_horizontal);
    const QSignalBlocker blockVertical(m_vertical);
    m_name->setText(m_draft.name);
    const int h = m_horizontal->findData(axisKey(m_draft.horizontal));
    const int v = m_vertical->findData(axisKey(m_draft.vertical));
    m_horizontal->setCurrentIndex(qMax(0, h));
    m_vertical->setCurrentIndex(qMax(0, v));
    m_horizontal->setEnabled(m_draft.destination == LayoutDestination::Grid);
    m_vertical->setEnabled(m_draft.destination == LayoutDestination::Grid);
    int preset = presets().size() + 1;
    if (m_draft.destination == LayoutDestination::Grid) {
        preset = presets().size();
        for (int i = 0; i < presets().size(); ++i) {
            if (sameAxis(presets().at(i).horizontal, m_draft.horizontal)
                && sameAxis(presets().at(i).vertical, m_draft.vertical)) {
                preset = i;
                break;
            }
        }
    }
    m_preset->setCurrentIndex(preset);
    m_preview->setArea(m_draft.normalizedArea());
    m_shortcut->setShortcut(m_draft.shortcut);
}

void WindowLayoutsPanel::applyQuickPreset(int index)
{
    m_draft.name = m_name->text();
    if (index >= 0 && index < presets().size()) {
        m_draft.destination = LayoutDestination::Grid;
        m_draft.horizontal = presets().at(index).horizontal;
        m_draft.vertical = presets().at(index).vertical;
        if (m_autoName) m_draft.name = presetText(index);
    } else if (index == presets().size()) {
        m_draft.destination = LayoutDestination::Grid;
        if (m_autoName) m_draft.name = I18n::tr("Custom mix");
    } else if (index == presets().size() + 1) {
        m_draft.destination = LayoutDestination::Custom;
        if (m_autoName) m_draft.name = I18n::tr("Custom area");
    }
    updateEditor();
}

void WindowLayoutsPanel::selectArea(bool pickWindow)
{
    m_draft.name = m_name->text();
    if (m_autoName) m_draft.name = I18n::tr("Custom area");
    QPointer<WindowLayoutsPanel> safe(this);
    m_module->openAreaPicker(pickWindow ? AreaPicker::Mode::Window : AreaPicker::Mode::Draw,
                             [safe](const QRectF &area) {
        if (!safe) return;
        safe->m_draft.destination = LayoutDestination::Custom;
        safe->m_draft.customArea = area;
        safe->updateEditor();
    });
}

void WindowLayoutsPanel::saveEditor()
{
    m_draft.name = m_name->text().trimmed();
    QString error;
    if (!m_module->saveLayout(m_draft, &error)) {
        m_editorError->setText(error);
        m_editorError->show();
        return;
    }
    resetDeleteConfirmation();
    m_pages->setCurrentWidget(m_listPage);
}

void WindowLayoutsPanel::showEditorForCapture()
{
    editLayout();
    m_draft.name = I18n::tr("Custom mix");
    m_draft.horizontal = {3, 0};
    m_draft.vertical = {4, 1};
    m_draft.shortcut = Shortcut::fromPortableString(QStringLiteral("Ctrl+Alt+L"));
    updateEditor();
}
