#include "modules/windowlayouts/WindowLayoutsPanel.h"

#include "core/I18n.h"
#include "modules/windowlayouts/WindowLayoutsModule.h"
#include "platform/WindowPlacement.h"
#include "ui/CustomTooltip.h"
#include "ui/ShortcutRow.h"
#include "ui/Theme.h"
#include "ui/UiWidgets.h"

#include <QGuiApplication>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
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

QPushButton *dropdown(QWidget *parent)
{
    auto *button = Ui::button(QString(), QString(), QString(), parent);
    button->setObjectName(QStringLiteral("fieldButton"));
    button->setFocusPolicy(Qt::NoFocus);
    button->setStyleSheet(QStringLiteral("text-align: left;"));
    Ui::setDropdownArrow(button);
    return button;
}

QList<LayoutGeometry::AxisSlice> axisOptions()
{
    QList<LayoutGeometry::AxisSlice> options = {{1, 0}};
    for (int parts = 2; parts <= 4; ++parts) {
        for (int index = 0; index < parts; ++index) options.append({parts, index});
    }
    return options;
}

QString description(const WindowLayout &layout)
{
    if (layout.destination == LayoutDestination::Minimize) {
        return I18n::tr("Minimize · active window");
    }
    if (layout.destination == LayoutDestination::Custom) {
        return I18n::tr("Custom area · active window");
    }
    return I18n::tr("%1 × %2").arg(axisText(layout.horizontal, true), axisText(layout.vertical, false));
}

class MiniLayout : public QWidget
{
public:
    explicit MiniLayout(QWidget *parent = nullptr) : QWidget(parent) { setFixedSize(47, 31); }
    void setArea(const QRectF &area, bool minimize = false) { m_area = area; m_minimize = minimize; update(); }
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setPen(Theme::color(Theme::kBorderStrong));
        painter.setBrush(Theme::color(Theme::kWindow));
        painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 3, 3);
        if (m_minimize) {
            painter.setPen(QPen(Theme::color(Theme::kPrimary), 2));
            painter.drawLine(QPointF(width() * .32, height() * .69), QPointF(width() * .68, height() * .69));
        } else if (LayoutGeometry::validArea(m_area)) {
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
    bool m_minimize = false;
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

class CurrentPageStack : public QWidget
{
public:
    explicit CurrentPageStack(QWidget *parent = nullptr) : QWidget(parent)
    {
        auto *column = new QVBoxLayout(this);
        column->setContentsMargins(0, 0, 0, 0);
    }

    void addWidget(QWidget *page)
    {
        layout()->addWidget(page);
        if (m_current) page->hide();
        else m_current = page;
    }

    QWidget *currentWidget() const { return m_current; }
    void setCurrentWidget(QWidget *page)
    {
        if (m_current == page) return;
        m_current->hide();
        m_current = page;
        m_current->show();
        layout()->invalidate();
    }

private:
    QWidget *m_current = nullptr;
};

WindowLayoutsPanel::WindowLayoutsPanel(WindowLayoutsModule *module, bool interactive, QWidget *parent)
    : QWidget(parent), m_module(module), m_interactive(interactive)
{
    setObjectName(QStringLiteral("windowLayoutsPanel"));
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(10);
    m_pages = new CurrentPageStack(this);
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
    auto *saved = Ui::card(page);
    auto *savedColumn = new QVBoxLayout(saved);
    savedColumn->setContentsMargins(14, 12, 14, 12);
    savedColumn->setSpacing(8);
    savedColumn->addWidget(Ui::label(I18n::tr("Saved shortcuts"), "cardTitle", saved));
    m_rows = new QWidget(saved);
    m_rows->setLayout(new QVBoxLayout());
    m_rows->layout()->setContentsMargins(0, 0, 0, 0);
    m_rows->layout()->setSpacing(0);
    savedColumn->addWidget(m_rows);
    auto *hint = Ui::caption(I18n::tr("Shortcuts act on the window in front. Fractions use the available area of its monitor."), saved);
    hint->setWordWrap(true);
    savedColumn->addWidget(hint);
    column->addWidget(saved);
    auto *addCard = Ui::card(page);
    auto *addRow = new QHBoxLayout(addCard);
    addRow->setContentsMargins(14, 12, 14, 12);
    auto *texts = new QVBoxLayout();
    texts->addWidget(Ui::label(I18n::tr("Create a window shortcut"), "cardTitle", addCard));
    auto *caption = Ui::caption(I18n::tr("Combine fractions, draw an area, or copy a window's bounds."), addCard);
    caption->setWordWrap(true);
    texts->addWidget(caption);
    addRow->addLayout(texts, 1);
    auto *add = Ui::button(I18n::tr("Add windows shortcut"), QStringLiteral("primary"), QString(), addCard);
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
    auto *card = Ui::card(page);
    auto *form = new QVBoxLayout(card);
    form->setContentsMargins(14, 12, 14, 12);
    form->setSpacing(6);
    form->addWidget(Ui::label(I18n::tr("Window shortcut"), "cardTitle", card));
    form->addWidget(Ui::caption(I18n::tr("Name"), card));
    m_name = new QLineEdit(card);
    m_name->setFocusPolicy(Qt::ClickFocus);
    m_name->setObjectName(QStringLiteral("ruleField"));
    form->addWidget(m_name);
    form->addWidget(Ui::caption(I18n::tr("Quick preset"), card));
    m_preset = dropdown(card);
    m_preset->setAccessibleName(I18n::tr("Quick preset"));
    form->addWidget(m_preset);
    auto *axes = new QHBoxLayout();
    axes->setSpacing(9);
    for (int which = 0; which < 2; ++which) {
        auto *axisColumn = new QVBoxLayout();
        axisColumn->setSpacing(5);
        axisColumn->addWidget(Ui::caption(which == 0 ? I18n::tr("Horizontal") : I18n::tr("Vertical"), card));
        auto *box = dropdown(card);
        box->setAccessibleName(which == 0 ? I18n::tr("Horizontal") : I18n::tr("Vertical"));
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
    m_shortcut = new ShortcutRow(I18n::tr("Shortcut"), I18n::tr("Acts on the window in front."), card);
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
    auto *save = Ui::button(I18n::tr("Save shortcut"), QStringLiteral("primary"), QStringLiteral("sm"), card);
    actions->addWidget(cancel);
    actions->addWidget(save);
    form->addLayout(actions);
    column->addWidget(card);
    column->addStretch();
    if (m_interactive) {
        connect(m_preset, &QPushButton::clicked, this, [this]() {
            QMenu menu(this);
            int selected = presets().size() + (m_draft.destination == LayoutDestination::Minimize ? 2 : 1);
            if (m_draft.destination == LayoutDestination::Grid) {
                selected = presets().size();
                for (int i = 0; i < presets().size(); ++i) {
                    if (sameAxis(presets().at(i).horizontal, m_draft.horizontal)
                        && sameAxis(presets().at(i).vertical, m_draft.vertical)) {
                        selected = i;
                        break;
                    }
                }
            }
            for (int i = 0; i < presets().size() + 3; ++i) {
                const QString title = i < presets().size() ? presetText(i)
                    : i == presets().size() ? I18n::tr("Custom mix")
                    : i == presets().size() + 1 ? I18n::tr("Custom area") : I18n::tr("Minimize");
                QAction *action = menu.addAction(title);
                action->setData(i);
                action->setCheckable(true);
                action->setChecked(i == selected);
            }
            if (QAction *chosen = menu.exec(m_preset->mapToGlobal(QPoint(0, m_preset->height() + 2)))) {
                if (chosen->data().toInt() != selected) applyQuickPreset(chosen->data().toInt());
            }
        });
        auto axisChanged = [this](LayoutGeometry::AxisSlice axis, bool horizontal) {
            m_draft.name = m_name->text();
            if (m_autoName) m_draft.name = I18n::tr("Custom mix");
            m_draft.destination = LayoutDestination::Grid;
            if (horizontal) m_draft.horizontal = axis;
            else m_draft.vertical = axis;
            updateEditor();
        };
        for (const bool horizontal : {true, false}) {
            QPushButton *button = horizontal ? m_horizontal : m_vertical;
            connect(button, &QPushButton::clicked, this, [this, button, horizontal, axisChanged]() {
                QMenu menu(this);
                const LayoutGeometry::AxisSlice current = horizontal ? m_draft.horizontal : m_draft.vertical;
                for (const LayoutGeometry::AxisSlice axis : axisOptions()) {
                    QAction *action = menu.addAction(axisText(axis, horizontal));
                    action->setData(axisKey(axis));
                    action->setCheckable(true);
                    action->setChecked(sameAxis(axis, current));
                }
                if (QAction *chosen = menu.exec(button->mapToGlobal(QPoint(0, button->height() + 2)))) {
                    const int key = chosen->data().toInt();
                    if (key != axisKey(current)) axisChanged({key / 10, key % 10}, horizontal);
                }
            });
        }
        connect(m_shortcut, &ShortcutRow::shortcutRecorded, this, [this](const Shortcut &shortcut) {
            m_draft.shortcut = shortcut;
            m_shortcut->setShortcut(shortcut);
        });
        m_shortcut->setValidator([this](const Shortcut &shortcut) {
            return m_module->validateShortcut(m_draft.id, shortcut, m_draft.enabled);
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
    int enabled = 0;
    for (const WindowLayout &layout : state.layouts()) enabled += layout.enabled;
    const int failed = m_module->registrationFailureCount();
    const int shared = m_module->externalConflictCount();
    const QString error = m_module->lastError();
    if (!error.isEmpty()) {
        m_statusCard->set(QStringLiteral("error"), I18n::tr("Window shortcuts need attention"), error,
                          QString(), QString(), QStringLiteral("err"));
    } else if (failed) {
        m_statusCard->set(QStringLiteral("error"), failed == 1 ? I18n::tr("1 shortcut could not start")
                                                              : I18n::tr("%1 shortcuts could not start").arg(failed),
                          shared ? (shared == 1
                                        ? I18n::tr("1 affected shortcut also shares keys with another app or the system. Check its marked row and error.")
                                        : I18n::tr("%1 also share keys with another app or the system. Check the marked rows and their errors.").arg(shared))
                                 : I18n::tr("Check each row's error. Turn off or edit the affected shortcuts."),
                          QString(), QString(), QStringLiteral("err"));
    } else if (shared) {
        m_statusCard->set(QStringLiteral("warn"), shared == 1 ? I18n::tr("1 shared shortcut is active")
                                                            : I18n::tr("%1 shared shortcuts are active").arg(shared),
#ifdef Q_OS_WIN
                          I18n::tr("Yellow dots mark shortcuts also used elsewhere. Window Layouts tries to respond first, but priority is not guaranteed."),
#else
                          I18n::tr("Yellow dots mark shortcuts also used elsewhere. The other app may respond too."),
#endif
                          QString(), QString(), QString());
    } else {
        m_statusCard->set(QStringLiteral("on"), I18n::tr("Window shortcuts are on"),
                          count == 0 ? I18n::tr("No shortcuts yet. Add one to get started.")
                          : enabled == 0 ? I18n::tr("All shortcuts are off. Use their switches to turn them on.")
                          : I18n::tr("%1 of %2 shortcuts are on.").arg(enabled).arg(count),
                          QString(), QString(), QString());
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
        auto *empty = Ui::caption(I18n::tr("No shortcuts yet. Add one to choose an area and key combination."), m_rows);
        empty->setWordWrap(true);
        column->addWidget(empty);
        return;
    }
    for (const WindowLayout &layout : layouts) {
        auto *row = new QWidget(m_rows);
        auto *line = new QHBoxLayout(row);
        line->setContentsMargins(0, 10, 0, 10);
        line->setSpacing(9);
        auto *mini = new MiniLayout(row);
        mini->setArea(layout.normalizedArea(), layout.destination == LayoutDestination::Minimize);
        line->addWidget(mini, 0, Qt::AlignTop);
        auto *shortcut = new ShortcutRow(layout.name, description(layout), row);
        shortcut->setShortcut(layout.shortcut);
        shortcut->editButton()->setAccessibleName(I18n::tr("Edit window shortcut"));
        const QString registrationError = m_module->registrationError(layout.id);
        if (!registrationError.isEmpty()) shortcut->setError(registrationError);
        auto *controls = new QWidget(shortcut);
        auto *controlRow = new QHBoxLayout(controls);
        controlRow->setContentsMargins(0, 0, 0, 0);
        controlRow->setSpacing(10);
        if (m_module->hasExternalConflict(layout.id)) {
            auto *dot = new StatusDot(9, controls);
            dot->setObjectName(QStringLiteral("sharedShortcutDot"));
            dot->setTone(QStringLiteral("warn"));
            dot->setAttribute(Qt::WA_TransparentForMouseEvents, false);
            dot->setAccessibleName(I18n::tr("Also used elsewhere"));
#ifdef Q_OS_WIN
            CustomTooltip::instance()->setToolTip(dot, I18n::tr("Also used by another app or the system. Window Layouts tries to respond first, but cannot guarantee priority."));
#else
            CustomTooltip::instance()->setToolTip(dot, I18n::tr("Also used by another app or the system. The other app may respond too."));
#endif
            controlRow->addWidget(dot, 0, Qt::AlignVCenter);
        }
        auto *toggle = new ToggleSwitch(controls);
        toggle->setObjectName(QStringLiteral("layoutEnabledSwitch"));
        toggle->setAccessibleName(I18n::tr("Enable %1").arg(layout.name));
        toggle->setChecked(layout.enabled);
        CustomTooltip::instance()->setToolTip(toggle, I18n::tr("Enable or disable only this shortcut."));
        controlRow->addWidget(toggle, 0, Qt::AlignVCenter);
        shortcut->setTrailingWidget(controls);
        if (m_interactive) {
            shortcut->setEditHandler([this, id = layout.id]() { editLayout(id); });
            connect(toggle, &ToggleSwitch::toggled, this, [this, id = layout.id](bool enabled) {
                QTimer::singleShot(0, this, [this, id, enabled]() { m_module->setLayoutEnabled(id, enabled); });
            });
        }
        line->addWidget(shortcut, 1);
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
    for (QWidget *ancestor = parentWidget(); ancestor; ancestor = ancestor->parentWidget()) {
        if (auto *scroll = qobject_cast<QScrollArea *>(ancestor)) {
            QTimer::singleShot(0, this, [scroll]() { scroll->verticalScrollBar()->setValue(0); });
            break;
        }
    }
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
    m_name->setText(m_draft.name);
    m_horizontal->setText(axisText(m_draft.horizontal, true));
    m_vertical->setText(axisText(m_draft.vertical, false));
    m_horizontal->setEnabled(m_draft.destination == LayoutDestination::Grid);
    m_vertical->setEnabled(m_draft.destination == LayoutDestination::Grid);
    int preset = presets().size() + (m_draft.destination == LayoutDestination::Minimize ? 2 : 1);
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
    m_preset->setText(preset < presets().size() ? presetText(preset)
                      : preset == presets().size() ? I18n::tr("Custom mix")
                      : preset == presets().size() + 1 ? I18n::tr("Custom area") : I18n::tr("Minimize"));
    m_preview->setArea(m_draft.normalizedArea());
    m_workAreaText->setText(m_draft.destination == LayoutDestination::Minimize
                                ? I18n::tr("This shortcut minimizes the active window.")
                                : I18n::tr("Available area excludes visible taskbar, Dock and menu bar."));
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
    } else if (index == presets().size() + 2) {
        m_draft.destination = LayoutDestination::Minimize;
        if (m_autoName) m_draft.name = I18n::tr("Minimize");
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
