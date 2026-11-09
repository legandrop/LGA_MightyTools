#include "modules/apptoggle/AppTogglePanel.h"

#include "core/I18n.h"
#include "modules/apptoggle/AppToggleModule.h"
#include "ui/CustomTooltip.h"
#include "ui/ShortcutRow.h"
#include "ui/UiWidgets.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QString description(const AppEntry &entry)
{
    const QString file = QFileInfo(QDir::fromNativeSeparators(entry.path)).fileName();
    return entry.arguments.isEmpty() ? file : QStringLiteral("%1 · %2").arg(file, entry.arguments);
}

QLineEdit *field(QWidget *parent)
{
    auto *edit = new QLineEdit(parent);
    edit->setFocusPolicy(Qt::ClickFocus);
    edit->setObjectName(QStringLiteral("ruleField"));
    return edit;
}

} // namespace

AppTogglePanel::AppTogglePanel(AppToggleModule *module, bool interactive, QWidget *parent)
    : QWidget(parent), m_module(module), m_interactive(interactive)
{
    setObjectName(QStringLiteral("appTogglePanel"));
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

QWidget *AppTogglePanel::createListPage()
{
    auto *page = new QWidget(this);
    auto *column = new QVBoxLayout(page);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(10);
    m_statusCard = new StatusCard(page);
    m_statusCard->setLive(true); // sube al encabezado de la hoja (D-73); se ve solo con la lista (ModuleHeader)
    if (m_interactive) {
        connect(m_statusCard->button(), &QPushButton::clicked, m_module, &AppToggleModule::dismissAttempt);
    }
    column->addWidget(m_statusCard);
    auto *saved = Ui::card(page);
    auto *savedColumn = new QVBoxLayout(saved);
    savedColumn->setContentsMargins(18, 15, 18, 16);
    savedColumn->setSpacing(8);
    savedColumn->addWidget(Ui::label(I18n::tr("Saved apps"), "cardTitle", saved));
    m_rows = new QWidget(saved);
    m_rows->setLayout(new QVBoxLayout());
    m_rows->layout()->setContentsMargins(0, 0, 0, 0);
    m_rows->layout()->setSpacing(0);
    savedColumn->addWidget(m_rows);
#ifdef Q_OS_MACOS
    auto *hint = Ui::caption(I18n::tr("Each shortcut opens its app, brings it to the front, or hides it when it's already in front."), saved);
#else
    auto *hint = Ui::caption(I18n::tr("Each shortcut opens its app, brings it to the front, or minimizes it when it's already in front."), saved);
#endif
    hint->setWordWrap(true);
    savedColumn->addWidget(hint);
    column->addWidget(saved);
    auto *addCard = Ui::card(page);
    auto *addRow = new QHBoxLayout(addCard);
    addRow->setContentsMargins(18, 15, 18, 16);
    auto *texts = new QVBoxLayout();
    texts->addWidget(Ui::label(I18n::tr("Create an app shortcut"), "cardTitle", addCard));
    auto *caption = Ui::caption(I18n::tr("Choose a program and record its shortcut."), addCard);
    caption->setWordWrap(true);
    texts->addWidget(caption);
    addRow->addLayout(texts, 1);
    auto *add = Ui::button(I18n::tr("Add app"), QStringLiteral("primary"), QString(), addCard);
    addRow->addWidget(add, 0, Qt::AlignVCenter);
    if (m_interactive) connect(add, &QPushButton::clicked, this, [this]() { editApp(); });
    column->addWidget(addCard);
    column->addStretch();
    return page;
}

QWidget *AppTogglePanel::createEditorPage()
{
    auto *page = new QWidget(this);
    auto *column = new QVBoxLayout(page);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(6);
    auto *card = Ui::card(page);
    auto *form = new QVBoxLayout(card);
    form->setContentsMargins(18, 15, 18, 16);
    form->setSpacing(6);
    form->addWidget(Ui::label(I18n::tr("App shortcut"), "cardTitle", card));
    form->addWidget(Ui::caption(I18n::tr("Name"), card));
    m_name = field(card);
    m_name->setAccessibleName(I18n::tr("Name"));
    form->addWidget(m_name);
    form->addWidget(Ui::caption(I18n::tr("App"), card));
    auto *pathRow = new QHBoxLayout();
    pathRow->setSpacing(6);
    m_path = field(card);
    m_path->setAccessibleName(I18n::tr("App"));
    pathRow->addWidget(m_path, 1);
    auto *browse = Ui::button(I18n::tr("Browse..."), QString(), QStringLiteral("sm"), card);
    pathRow->addWidget(browse);
    form->addLayout(pathRow);
    form->addWidget(Ui::caption(I18n::tr("Arguments"), card));
    m_arguments = field(card);
    m_arguments->setAccessibleName(I18n::tr("Arguments"));
    m_arguments->setPlaceholderText(I18n::tr("Optional"));
    form->addWidget(m_arguments);
#ifdef Q_OS_WIN
    form->addWidget(Ui::caption(I18n::tr("Start in"), card));
    m_folder = field(card);
    m_folder->setAccessibleName(I18n::tr("Start in"));
    form->addWidget(m_folder);
#endif
#ifdef Q_OS_MACOS
    m_shortcut = new ShortcutRow(I18n::tr("Shortcut"), I18n::tr("Opens the app, brings it to the front or hides it."), card);
#else
    m_shortcut = new ShortcutRow(I18n::tr("Shortcut"), I18n::tr("Opens the app, brings it to the front or minimizes it."), card);
#endif
    m_shortcut->showWindowsShortcutNote(true);
    form->addWidget(m_shortcut);
    m_editorError = Ui::caption(QString(), card);
    m_editorError->setWordWrap(true);
    Ui::setStyleProperty(m_editorError, "tone", QStringLiteral("err"));
    m_editorError->hide();
    form->addWidget(m_editorError);
    Ui::addDivider(form, card);
    auto *actions = new QHBoxLayout();
    m_remove = Ui::button(I18n::tr("Delete"), QStringLiteral("ghost"), QStringLiteral("sm"), card);
    m_remove->setObjectName(QStringLiteral("removeApp"));
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
        connect(browse, &QPushButton::clicked, this, &AppTogglePanel::browse);
        connect(m_path, &QLineEdit::textChanged, this, &AppTogglePanel::updateFolderPlaceholder);
        // Una ruta pegada con «Copiar como ruta» llega entre comillas: se le sacan a la vista.
        for (QLineEdit *edit : {m_path, m_folder}) {
            if (!edit) continue;
            connect(edit, &QLineEdit::editingFinished, this, [edit]() {
                const QString clean = AppToggleState::cleanPath(edit->text());
                if (clean != edit->text()) edit->setText(clean);
            });
        }
        connect(m_shortcut, &ShortcutRow::shortcutRecorded, this, [this](const Shortcut &shortcut) {
            m_draft.shortcut = shortcut;
            m_shortcut->setShortcut(shortcut);
        });
        m_shortcut->setValidator([this](const Shortcut &shortcut) {
            return m_module->validateShortcut(m_draft.id, shortcut, m_draft.enabled);
        });
        connect(cancel, &QPushButton::clicked, this, [this]() {
            resetDeleteConfirmation();
            m_pages->setCurrentWidget(m_listPage);
        });
        connect(save, &QPushButton::clicked, this, &AppTogglePanel::saveEditor);
        for (QLineEdit *edit : {m_name, m_path, m_arguments, m_folder}) {
            if (edit) connect(edit, &QLineEdit::returnPressed, this, &AppTogglePanel::saveEditor);
        }
        connect(m_remove, &QPushButton::clicked, this, [this]() {
            if (!m_draft.id) return;
            if (!m_confirmDelete) {
                m_confirmDelete = true;
                m_remove->setText(I18n::tr("Confirm delete"));
                Ui::setStyleProperty(m_remove, "variant", QStringLiteral("danger"));
                return;
            }
            if (m_module->removeApp(m_draft.id)) {
                resetDeleteConfirmation();
                m_pages->setCurrentWidget(m_listPage);
            }
        });
    }
    return page;
}

void AppTogglePanel::refresh()
{
    const QList<AppEntry> &apps = m_module->appsState().apps();
    int enabled = 0;
    int sharedFailed = 0; // las que fallaron y ademas comparten teclas
    for (const AppEntry &entry : apps) {
        enabled += entry.enabled;
        sharedFailed += m_module->hasExternalConflict(entry.id) && !m_module->registrationError(entry.id).isEmpty();
    }
    const int failed = m_module->registrationFailureCount();
    const int shared = m_module->externalConflictCount();
    // Un atajo que no pudo abrir su app es una nota gris con su hora, no un problema: va debajo del estado y
    // se descarta con el boton.
    const QString note = Ui::attemptNoteText(m_module->attemptNote(), m_module->attemptTime());
    const auto withNote = [&note](const QString &text) {
        return note.isEmpty() ? text : text + QLatin1Char('\n') + note;
    };
    const QString dismiss = note.isEmpty() ? QString() : I18n::tr("Dismiss");
    if (failed) {
        m_statusCard->set(QStringLiteral("error"), failed == 1 ? I18n::tr("1 shortcut could not start")
                                                              : I18n::tr("%1 shortcuts could not start").arg(failed),
                          withNote(sharedFailed ? (sharedFailed == 1
                                        ? I18n::tr("1 affected shortcut also shares keys with another app or the system. Check its marked row and error.")
                                        : I18n::tr("%1 also share keys with another app or the system. Check the marked rows and their errors.").arg(sharedFailed))
                                 : I18n::tr("Check each row's error. Turn off or edit the affected shortcuts.")),
                          dismiss, QString(), QStringLiteral("err"), QStringLiteral("sm"));
    } else if (shared) {
        m_statusCard->set(QStringLiteral("warn"), shared == 1 ? I18n::tr("1 shared shortcut is active")
                                                            : I18n::tr("%1 shared shortcuts are active").arg(shared),
#ifdef Q_OS_WIN
                          withNote(I18n::tr("Yellow dots mark shortcuts also used elsewhere. App Toggle tries to respond first, but priority is not guaranteed.")),
#else
                          withNote(I18n::tr("Yellow dots mark shortcuts also used elsewhere. The other app may respond too.")),
#endif
                          dismiss, QString(), QString(), QStringLiteral("sm"));
    } else {
        m_statusCard->set(QStringLiteral("on"), I18n::tr("App shortcuts are on"),
                          withNote(apps.isEmpty() ? I18n::tr("No apps yet. Add one to get started.")
                                   : enabled == 0 ? I18n::tr("All apps are off. Use their switches to turn them on.")
                                   : I18n::tr("%1 of %2 apps are on.").arg(enabled).arg(apps.size())),
                          dismiss, QString(), QString(), QStringLiteral("sm"));
    }
    rebuildList();
}

QIcon AppTogglePanel::iconFor(const QString &path)
{
    const auto cached = m_icons.constFind(path);
    if (cached != m_icons.constEnd()) {
        return *cached;
    }
    const QIcon icon = AppToggleState::validPath(path) ? AppToggleSystem::icon(path) : QIcon();
    m_icons.insert(path, icon);
    return icon;
}

void AppTogglePanel::rebuildList()
{
    auto *column = static_cast<QVBoxLayout *>(m_rows->layout());
    while (QLayoutItem *item = column->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const QList<AppEntry> apps = m_module->appsState().apps();
    if (apps.isEmpty()) {
        auto *empty = Ui::caption(I18n::tr("No apps yet. Add one to choose a program and a key combination."), m_rows);
        empty->setWordWrap(true);
        column->addWidget(empty);
        return;
    }
    for (const AppEntry &entry : apps) {
        auto *row = new QWidget(m_rows);
        auto *line = new QHBoxLayout(row);
        line->setContentsMargins(0, 10, 0, 10);
        line->setSpacing(9);
        auto *tile = new AppIconTile(row);
        tile->setIcon(iconFor(entry.path));
        line->addWidget(tile, 0, Qt::AlignTop);
        auto *shortcut = new ShortcutRow(entry.name, description(entry), row);
        shortcut->setShortcut(entry.shortcut);
        shortcut->editButton()->setAccessibleName(I18n::tr("Edit app shortcut"));
        const QString registrationError = m_module->registrationError(entry.id);
        if (!registrationError.isEmpty()) shortcut->setError(registrationError);
        auto *controls = new QWidget(shortcut);
        auto *controlRow = new QHBoxLayout(controls);
        controlRow->setContentsMargins(0, 0, 0, 0);
        controlRow->setSpacing(10);
        if (m_module->hasExternalConflict(entry.id)) {
            auto *dot = new StatusDot(9, controls);
            dot->setObjectName(QStringLiteral("sharedShortcutDot"));
            dot->setTone(QStringLiteral("warn"));
            dot->setAttribute(Qt::WA_TransparentForMouseEvents, false);
            dot->setAccessibleName(I18n::tr("Also used elsewhere"));
#ifdef Q_OS_WIN
            CustomTooltip::instance()->setToolTip(dot, I18n::tr("Also used by another app or the system. App Toggle tries to respond first, but cannot guarantee priority."));
#else
            CustomTooltip::instance()->setToolTip(dot, I18n::tr("Also used by another app or the system. The other app may respond too."));
#endif
            controlRow->addWidget(dot, 0, Qt::AlignVCenter);
        }
        auto *toggle = new ToggleSwitch(controls);
        toggle->setObjectName(QStringLiteral("appEnabledSwitch"));
        toggle->setAccessibleName(I18n::tr("Enable %1").arg(entry.name));
        toggle->setChecked(entry.enabled);
        CustomTooltip::instance()->setToolTip(toggle, I18n::tr("Enable or disable only this shortcut."));
        controlRow->addWidget(toggle, 0, Qt::AlignVCenter);
        shortcut->setTrailingWidget(controls);
        if (m_interactive) {
            shortcut->setEditHandler([this, id = entry.id]() { editApp(id); });
            // Aplicarlo rearma la lista y borra este switch: se espera a que termine de deslizar (160 ms).
            connect(toggle, &ToggleSwitch::toggled, this, [this, id = entry.id](bool on) {
                QTimer::singleShot(180, this, [this, id, on]() { m_module->setAppEnabled(id, on); });
            });
        }
        line->addWidget(shortcut, 1);
        column->addWidget(row);
        Ui::addDivider(column, m_rows);
    }
}

void AppTogglePanel::editApp(int id)
{
    resetDeleteConfirmation();
    m_draft = AppEntry();
    for (const AppEntry &entry : m_module->appsState().apps()) {
        if (entry.id == id) {
            m_draft = entry;
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

void AppTogglePanel::resetDeleteConfirmation()
{
    m_confirmDelete = false;
    if (m_remove) {
        m_remove->setText(I18n::tr("Delete"));
        Ui::setStyleProperty(m_remove, "variant", QStringLiteral("ghost"));
    }
}

void AppTogglePanel::hideEvent(QHideEvent *event)
{
    resetDeleteConfirmation();
    QWidget::hideEvent(event);
}

void AppTogglePanel::updateEditor()
{
    m_name->setText(m_draft.name);
    m_path->setText(m_draft.path);
    m_arguments->setText(m_draft.arguments);
    if (m_folder) m_folder->setText(m_draft.workingDir);
    m_shortcut->setShortcut(m_draft.shortcut);
    updateFolderPlaceholder();
}

void AppTogglePanel::updateFolderPlaceholder()
{
    if (!m_folder) return;
    const QString path = AppToggleState::cleanPath(m_path->text());
    // Vacio = la carpeta de la app: se muestra cual es.
    m_folder->setPlaceholderText(AppToggleState::validPath(path)
                                     ? QDir::toNativeSeparators(QFileInfo(QDir::fromNativeSeparators(path)).absolutePath())
                                     : I18n::tr("Same folder as the app"));
}

void AppTogglePanel::browse()
{
    const QString current = QDir::fromNativeSeparators(AppToggleState::cleanPath(m_path->text()));
#ifdef Q_OS_MACOS
    const QString start = AppToggleState::validPath(current) ? QFileInfo(current).absolutePath() : QStringLiteral("/Applications");
    const QString filter = I18n::tr("Applications (*.app);;All files (*)");
#else
    const QString start = AppToggleState::validPath(current) ? QFileInfo(current).absolutePath() : QStringLiteral("C:/Program Files");
    const QString filter = I18n::tr("Executable (*.exe)");
#endif
    // El dialogo corre su propio ciclo: si la herramienta se apaga mientras esta abierto, el panel ya no existe.
    QPointer<AppTogglePanel> safe(this);
    const QString chosen = QFileDialog::getOpenFileName(this, I18n::tr("Choose an app"), start, filter);
    if (!safe || chosen.isEmpty()) {
        return;
    }
    m_path->setText(QDir::toNativeSeparators(chosen));
    if (m_name->text().trimmed().isEmpty()) {
        const QString product = AppToggleSystem::productName(chosen);
        m_name->setText(product.isEmpty() ? QFileInfo(chosen).completeBaseName() : product);
    }
}

void AppTogglePanel::saveEditor()
{
    m_draft.name = m_name->text().trimmed();
    m_draft.path = AppToggleState::cleanPath(m_path->text());
    m_draft.arguments = m_arguments->text().trimmed();
    m_draft.workingDir = m_folder ? AppToggleState::cleanPath(m_folder->text()) : QString();
    m_path->setText(m_draft.path);
    if (m_folder) m_folder->setText(m_draft.workingDir);
    QString error;
    if (!m_module->saveApp(m_draft, &error)) {
        m_editorError->setText(error);
        m_editorError->show();
        return;
    }
    resetDeleteConfirmation();
    m_pages->setCurrentWidget(m_listPage);
}

void AppTogglePanel::showEditorForCapture(int id, const QString &error, const QString &recording)
{
    editApp(id);
    if (!error.isEmpty()) {
        m_editorError->setText(error);
        m_editorError->show();
    }
    if (!recording.isEmpty()) m_shortcut->showRecordingFixture(recording);
}

bool AppTogglePanel::hasUnsavedWork() const
{
    // El editor abierto: lo elegido y escrito se guarda solo con Save o Enter.
    return m_pages && m_pages->currentWidget() == m_editorPage;
}
