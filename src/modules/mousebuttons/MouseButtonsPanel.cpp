#include "modules/mousebuttons/MouseButtonsPanel.h"

#include "core/I18n.h"
#include "modules/mousebuttons/MouseButtonsModule.h"
#include "platform/AppToggleSystem.h"
#include "ui/CustomTooltip.h"
#include "ui/ShortcutRow.h"
#include "ui/UiWidgets.h"

#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

using MouseButtonsSystem::Button;

MouseButtonsPanel::MouseButtonsPanel(MouseButtonsModule *module, bool interactive, QWidget *parent)
    : QWidget(parent), m_module(module), m_interactive(interactive)
{
    setObjectName(QStringLiteral("mouseButtonsPanel"));
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

QWidget *MouseButtonsPanel::createListPage()
{
    auto *page = new QWidget(this);
    auto *column = new QVBoxLayout(page);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(10);
    m_statusCard = new StatusCard(page);
    m_statusCard->setLive(true); // sube al encabezado de la hoja (D-73); se ve solo con la lista (ModuleHeader)
    column->addWidget(m_statusCard);
    auto *saved = Ui::card(page);
    auto *savedColumn = new QVBoxLayout(saved);
    savedColumn->setContentsMargins(14, 12, 14, 12);
    savedColumn->setSpacing(8);
    savedColumn->addWidget(Ui::label(I18n::tr("Saved rules"), "cardTitle", saved));
    m_rows = new QWidget(saved);
    m_rows->setLayout(new QVBoxLayout());
    m_rows->layout()->setContentsMargins(0, 0, 0, 0);
    m_rows->layout()->setSpacing(0);
    savedColumn->addWidget(m_rows);
    auto *hint = Ui::caption(I18n::tr("A rule acts only while its app is in front and under the pointer."), saved);
    hint->setWordWrap(true);
    savedColumn->addWidget(hint);
    column->addWidget(saved);
    auto *addCard = Ui::card(page);
    auto *addRow = new QHBoxLayout(addCard);
    addRow->setContentsMargins(14, 12, 14, 12);
    auto *texts = new QVBoxLayout();
    texts->addWidget(Ui::label(I18n::tr("Create a mouse rule"), "cardTitle", addCard));
    auto *caption = Ui::caption(I18n::tr("Choose an app, a button and the keys to hold."), addCard);
    caption->setWordWrap(true);
    texts->addWidget(caption);
    addRow->addLayout(texts, 1);
    auto *add = Ui::button(I18n::tr("Add rule"), QStringLiteral("primary"), QString(), addCard);
    addRow->addWidget(add, 0, Qt::AlignVCenter);
    if (m_interactive) connect(add, &QPushButton::clicked, this, [this]() { editRule(); });
    column->addWidget(addCard);
    column->addStretch();
    return page;
}

QWidget *MouseButtonsPanel::createEditorPage()
{
    auto *page = new QWidget(this);
    auto *column = new QVBoxLayout(page);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(6);
    auto *card = Ui::card(page);
    auto *form = new QVBoxLayout(card);
    form->setContentsMargins(14, 12, 14, 12);
    form->setSpacing(6);
    form->addWidget(Ui::label(I18n::tr("Mouse rule"), "cardTitle", card));
    form->addWidget(Ui::caption(I18n::tr("App"), card));
    auto *appRow = new QHBoxLayout();
    appRow->setSpacing(6);
    m_app = new QLineEdit(card);
    m_app->setFocusPolicy(Qt::ClickFocus);
    m_app->setObjectName(QStringLiteral("ruleField"));
    m_app->setAccessibleName(I18n::tr("App"));
    m_app->setPlaceholderText(I18n::tr("Program name, like Photoshop.exe"));
    appRow->addWidget(m_app, 1);
    auto *browse = Ui::button(I18n::tr("Browse..."), QString(), QStringLiteral("sm"), card);
    appRow->addWidget(browse);
    form->addLayout(appRow);
    form->addWidget(Ui::caption(I18n::tr("Button"), card));
    m_button = Ui::button(QString(), QString(), QString(), card);
    m_button->setObjectName(QStringLiteral("fieldButton"));
    m_button->setFocusPolicy(Qt::NoFocus);
    m_button->setStyleSheet(QStringLiteral("text-align: left;"));
    m_button->setAccessibleName(I18n::tr("Button"));
    Ui::setDropdownArrow(m_button);
    m_button->installEventFilter(this);
    form->addWidget(m_button);
    m_keys = new ShortcutRow(I18n::tr("Keys to hold"), I18n::tr("Held down while you hold the mouse button."), card);
    m_keys->setHoldKeys(true);
    m_keys->editButton()->setAccessibleName(I18n::tr("Record the keys to hold"));
    form->addWidget(m_keys);
    m_holdLeft = new SwitchRow(I18n::tr("Also hold the left button"),
                               I18n::tr("Drags as you move the mouse, like the hand tool."), card);
    m_holdLeft->setObjectName(QStringLiteral("holdLeftRow"));
    // Dos filas de opcion seguidas: el aire entre filas de la lista (10), no el de un rotulo con su campo (6).
    form->addSpacing(4);
    form->addWidget(m_holdLeft);
    m_editorError = Ui::caption(QString(), card);
    m_editorError->setWordWrap(true);
    Ui::setStyleProperty(m_editorError, "tone", QStringLiteral("err"));
    m_editorError->hide();
    form->addWidget(m_editorError);
    Ui::addDivider(form, card);
    auto *actions = new QHBoxLayout();
    m_remove = Ui::button(I18n::tr("Delete"), QStringLiteral("ghost"), QStringLiteral("sm"), card);
    m_remove->setObjectName(QStringLiteral("removeRule"));
    actions->addWidget(m_remove);
    actions->addStretch();
    auto *cancel = Ui::button(I18n::tr("Cancel"), QString(), QStringLiteral("sm"), card);
    auto *save = Ui::button(I18n::tr("Save rule"), QStringLiteral("primary"), QStringLiteral("sm"), card);
    actions->addWidget(cancel);
    actions->addWidget(save);
    form->addLayout(actions);
    column->addWidget(card);
    column->addStretch();
    if (m_interactive) {
        connect(browse, &QPushButton::clicked, this, &MouseButtonsPanel::browse);
        connect(m_button, &QPushButton::clicked, this, [this]() {
            QMenu menu(this);
            fillButtonMenu(&menu);
            if (QAction *chosen = menu.exec(m_button->mapToGlobal(QPoint(0, m_button->height() + 2)))) {
                m_draft.button = Button(chosen->data().toInt());
                m_button->setText(MouseButtonsModule::buttonText(m_draft.button));
            }
        });
        connect(m_keys, &ShortcutRow::shortcutRecorded, this, [this](const Shortcut &keys) {
            m_draft.keys = keys;
            m_keys->setShortcut(keys);
        });
        connect(m_holdLeft, &SwitchRow::clicked, this, [this](bool checked) { m_draft.holdLeft = checked; });
        connect(cancel, &QPushButton::clicked, this, [this]() {
            resetDeleteConfirmation();
            m_pages->setCurrentWidget(m_listPage);
        });
        connect(save, &QPushButton::clicked, this, &MouseButtonsPanel::saveEditor);
        connect(m_app, &QLineEdit::returnPressed, this, &MouseButtonsPanel::saveEditor);
        connect(m_remove, &QPushButton::clicked, this, [this]() {
            if (!m_draft.id) return;
            if (!m_confirmDelete) {
                m_confirmDelete = true;
                m_remove->setText(I18n::tr("Confirm delete"));
                Ui::setStyleProperty(m_remove, "variant", QStringLiteral("danger"));
                return;
            }
            if (m_module->removeRule(m_draft.id)) {
                resetDeleteConfirmation();
                m_pages->setCurrentWidget(m_listPage);
            }
        });
    }
    return page;
}

void MouseButtonsPanel::fillButtonMenu(QMenu *menu) const
{
    // Como un desplegable: tan ancho como el campo, con la opcion elegida marcada.
    menu->setMinimumWidth(m_button->width());
    for (const Button button : {Button::Middle, Button::Back, Button::Forward}) {
        QAction *action = menu->addAction(MouseButtonsModule::buttonText(button));
        action->setData(int(button));
        action->setCheckable(true);
        action->setChecked(button == m_draft.button);
    }
}

void MouseButtonsPanel::refresh()
{
    const QList<MouseRule> &rules = m_module->rulesState().rules();
    int enabled = 0;
    for (const MouseRule &rule : rules) enabled += rule.enabled;
    const QString error = m_module->lastError();
    if (!error.isEmpty()) {
        m_statusCard->set(QStringLiteral("error"), I18n::tr("Mouse rules need attention"), error,
                          QString(), QString(), QStringLiteral("err"));
    } else {
        m_statusCard->set(QStringLiteral("on"), I18n::tr("Mouse rules are on"),
                          rules.isEmpty() ? I18n::tr("No rules yet. Add one to get started.")
                          : enabled == 0 ? I18n::tr("All rules are off. Use their switches to turn them on.")
                          : I18n::tr("%1 of %2 rules are on.").arg(enabled).arg(rules.size()),
                          QString(), QString(), QString());
    }
    if (m_pages->currentWidget() == m_editorPage && !error.isEmpty()) {
        m_editorError->setText(error);
        m_editorError->show();
    }
    rebuildList();
}

QIcon MouseButtonsPanel::iconFor(const QString &path)
{
    if (path.isEmpty()) {
        return QIcon();
    }
    const auto cached = m_icons.constFind(path);
    if (cached != m_icons.constEnd()) {
        return *cached;
    }
    const QIcon icon = AppToggleSystem::icon(path);
    m_icons.insert(path, icon);
    return icon;
}

void MouseButtonsPanel::rebuildList()
{
    auto *column = static_cast<QVBoxLayout *>(m_rows->layout());
    while (QLayoutItem *item = column->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const QList<MouseRule> rules = m_module->rulesState().rules();
    if (rules.isEmpty()) {
        auto *empty = Ui::caption(I18n::tr("No rules yet. Add one to choose an app, a button and keys to hold."), m_rows);
        empty->setWordWrap(true);
        column->addWidget(empty);
        return;
    }
    for (const MouseRule &rule : rules) {
        auto *row = new QWidget(m_rows);
        auto *line = new QHBoxLayout(row);
        line->setContentsMargins(0, 10, 0, 10);
        line->setSpacing(9);
        auto *tile = new AppIconTile(row);
        tile->setIcon(iconFor(rule.path));
        line->addWidget(tile, 0, Qt::AlignTop);
        // Las cajas muestran lo que se aprieta (el boton del mouse), como las teclas en App Toggle.
        auto *shortcut = new ShortcutRow(rule.app, MouseButtonsModule::summary(rule), row);
        shortcut->setKeyLabels({MouseButtonsModule::buttonText(rule.button)});
        shortcut->editButton()->setAccessibleName(I18n::tr("Edit mouse rule"));
        auto *toggle = new ToggleSwitch(shortcut);
        toggle->setObjectName(QStringLiteral("ruleEnabledSwitch"));
        toggle->setAccessibleName(I18n::tr("Enable %1").arg(rule.app));
        toggle->setChecked(rule.enabled);
        CustomTooltip::instance()->setToolTip(toggle, I18n::tr("Enable or disable only this rule."));
        shortcut->setTrailingWidget(toggle);
        if (m_interactive) {
            shortcut->setEditHandler([this, id = rule.id]() { editRule(id); });
            // Aplicarlo rearma la lista y borra este switch: se espera a que termine de deslizar (160 ms).
            connect(toggle, &ToggleSwitch::toggled, this, [this, id = rule.id](bool on) {
                QTimer::singleShot(180, this, [this, id, on]() { m_module->setRuleEnabled(id, on); });
            });
        }
        line->addWidget(shortcut, 1);
        column->addWidget(row);
        Ui::addDivider(column, m_rows);
    }
}

void MouseButtonsPanel::editRule(int id)
{
    resetDeleteConfirmation();
    m_draft = MouseRule();
    for (const MouseRule &rule : m_module->rulesState().rules()) {
        if (rule.id == id) {
            m_draft = rule;
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

void MouseButtonsPanel::resetDeleteConfirmation()
{
    m_confirmDelete = false;
    if (m_remove) {
        m_remove->setText(I18n::tr("Delete"));
        Ui::setStyleProperty(m_remove, "variant", QStringLiteral("ghost"));
    }
}

void MouseButtonsPanel::hideEvent(QHideEvent *event)
{
    resetDeleteConfirmation();
    QWidget::hideEvent(event);
}

void MouseButtonsPanel::updateEditor()
{
    m_app->setText(m_draft.app);
    m_button->setText(MouseButtonsModule::buttonText(m_draft.button));
    m_keys->setShortcut(m_draft.keys);
    m_holdLeft->setChecked(m_draft.holdLeft);
}

void MouseButtonsPanel::browse()
{
    const QString current = QDir::fromNativeSeparators(m_draft.path);
    const QString start = QDir::isAbsolutePath(current) ? QFileInfo(current).absolutePath() : QStringLiteral("C:/Program Files");
    // El dialogo corre su propio ciclo: si la herramienta se apaga mientras esta abierto, el panel ya no existe.
    QPointer<MouseButtonsPanel> safe(this);
    const QString chosen = QFileDialog::getOpenFileName(this, I18n::tr("Choose an app"), start, I18n::tr("Executable (*.exe)"));
    if (!safe || chosen.isEmpty()) {
        return;
    }
    // La regla reconoce la app por el nombre del exe; la ruta queda solo para mostrar su icono.
    m_draft.path = QDir::toNativeSeparators(chosen);
    m_app->setText(QFileInfo(chosen).fileName());
}

void MouseButtonsPanel::saveEditor()
{
    m_draft.app = m_app->text().trimmed();
    QString error;
    if (!m_module->saveRule(m_draft, &error)) {
        m_editorError->setText(error);
        m_editorError->show();
        return;
    }
    resetDeleteConfirmation();
    m_pages->setCurrentWidget(m_listPage);
}

bool MouseButtonsPanel::eventFilter(QObject *watched, QEvent *event)
{
    // Captura de QA: el menu de prueba sigue al desplegable cuando el layout lo acomoda.
    if (m_captureMenu && watched == m_button && (event->type() == QEvent::Move || event->type() == QEvent::Resize)) {
        m_captureMenu->setMinimumWidth(m_button->width());
        m_captureMenu->adjustSize();
        m_captureMenu->move(m_button->mapTo(m_button->parentWidget(), QPoint(0, m_button->height() + 2)));
    }
    return QWidget::eventFilter(watched, event);
}

void MouseButtonsPanel::showEditorForCapture(int id, bool buttonMenuOpen)
{
    editRule(id);
    if (!buttonMenuOpen) {
        return;
    }
    // El mismo menu que abre el click, como hijo de la tarjeta (sin ventana propia) para que salga en la captura.
    m_draft.button = Button::Middle;
    m_captureMenu = new QMenu(m_button->parentWidget());
    m_captureMenu->setWindowFlags(Qt::Widget);
    fillButtonMenu(m_captureMenu);
    m_captureMenu->setActiveAction(m_captureMenu->actions().value(1));
    m_captureMenu->raise();
    m_captureMenu->show();
}
