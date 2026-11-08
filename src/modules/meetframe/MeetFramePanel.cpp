#include "modules/meetframe/MeetFramePanel.h"

#include "core/I18n.h"
#include "modules/meetframe/MeetFrameModule.h"
#include "platform/AppToggleSystem.h"
#include "platform/MeetFrameSystem.h"
#include "ui/CustomTooltip.h"
#include "ui/ShortcutRow.h"
#include "ui/Theme.h"
#include "ui/UiWidgets.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QFrame *titledCard(QWidget *parent, const QString &title, QVBoxLayout **column, int spacing = 6)
{
    QFrame *card = Ui::card(parent);
    *column = new QVBoxLayout(card);
    (*column)->setContentsMargins(14, 12, 14, 12);
    (*column)->setSpacing(spacing);
    (*column)->addWidget(Ui::label(title, "cardTitle", card));
    return card;
}

// El click de un boton del panel puede apagar la herramienta (y borrar el panel): se hace afuera del click.
template <class F>
void runLater(QObject *context, F f)
{
    QTimer::singleShot(0, context, f);
}

// La forma fija de cada opcion de «Frame shape».
QString lockedAspect(int index)
{
    return index == 1 ? QStringLiteral("4:3") : index == 2 ? QStringLiteral("1:1") : index == 3 ? QStringLiteral("current") : QStringLiteral("16:9");
}

QString groupTitle(const QString &id)
{
    if (id == QLatin1String("messaging")) return I18n::tr("Messaging apps");
    if (id == QLatin1String("mail")) return I18n::tr("Mail");
    return I18n::tr("Password managers");
}

QString groupCaption(const QString &id)
{
#if defined(Q_OS_MACOS)
    if (id == QLatin1String("messaging")) return I18n::tr("WhatsApp, Telegram, Messages, Signal, Slack, Discord and more, also WhatsApp and Telegram in the browser.");
    if (id == QLatin1String("mail")) return I18n::tr("Mail, Outlook, Thunderbird and Spark, also Gmail and Outlook in the browser.");
    return I18n::tr("Passwords, 1Password, Bitwarden, KeePassXC and more.");
#else
    if (id == QLatin1String("messaging")) return I18n::tr("WhatsApp, Telegram, Signal, Slack, Discord and more, also WhatsApp and Telegram in the browser.");
    if (id == QLatin1String("mail")) return I18n::tr("Outlook, Thunderbird, Spark and eM Client, also Gmail and Outlook in the browser.");
    return I18n::tr("1Password, Bitwarden, KeePass and more.");
#endif
}

} // namespace

MeetFramePanel::MeetFramePanel(MeetFrameModule *module, bool interactive, QWidget *parent)
    : QWidget(parent), m_module(module), m_interactive(interactive)
{
    setObjectName(QStringLiteral("meetFramePanel"));
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(10);
    m_status = new StatusCard(this);
    m_status->setLive(true); // sube al encabezado de la hoja (D-73)
    column->addWidget(m_status);
    column->addWidget(buildShortcutCard());
    column->addWidget(buildHideCard());
    column->addWidget(buildAppsCard());
    column->addWidget(buildPictureCard());
    column->addStretch();
    if (m_interactive) {
        connect(m_status->button(), &QPushButton::clicked, this, [this]() { runLater(this, [this]() { statusAction(); }); });
    }
    refresh();
}

QWidget *MeetFramePanel::buildShortcutCard()
{
    QVBoxLayout *column = nullptr;
    QFrame *card = titledCard(this, I18n::tr("Shortcuts"), &column);
    m_toggle = new ShortcutRow(I18n::tr("Show or hide the frame"), I18n::tr("The frame comes back where you left it."), card);
    m_toggle->editButton()->setAccessibleName(I18n::tr("Record the shortcut to show the frame"));
    column->addWidget(m_toggle);
    Ui::addDivider(column, card);
    m_pause = new ShortcutRow(I18n::tr("Pause"), I18n::tr("Freezes what people see. Press it again to resume. It works while the frame is shown."), card);
    m_pause->editButton()->setAccessibleName(I18n::tr("Record the pause shortcut"));
    column->addWidget(m_pause);
    if (m_interactive) {
        const auto wire = [this](ShortcutRow *row, int id) {
            row->setValidator([this, id](const Shortcut &keys) { return m_module->validateShortcut(id, keys); });
            connect(row, &ShortcutRow::shortcutRecorded, this, [this, row, id](const Shortcut &keys) {
                QString error;
                if (!m_module->setShortcut(id, keys, &error)) row->setError(error);
            });
        };
        wire(m_toggle, MeetFrameModule::ToggleShortcut);
        wire(m_pause, MeetFrameModule::PauseShortcut);
    }
    return card;
}

QWidget *MeetFramePanel::buildHideCard()
{
    QVBoxLayout *column = nullptr;
    QFrame *card = titledCard(this, I18n::tr("Cover while sharing"), &column, 4);
#if defined(Q_OS_MACOS)
    m_notifications = new SwitchRow(I18n::tr("Notifications"), I18n::tr("Banners and Notification Center."), card);
#else
    m_notifications = new SwitchRow(I18n::tr("Notifications"), I18n::tr("Windows notifications and the notification center."), card);
#endif
    column->addWidget(m_notifications);
    m_desktop = new SwitchRow(I18n::tr("Desktop"), I18n::tr("The wallpaper and the desktop icons."), card);
    column->addWidget(m_desktop);
    for (const MeetFrameRules::Group &group : MeetFrameRules::groups()) {
        auto *row = new SwitchRow(groupTitle(group.id), groupCaption(group.id), card);
        m_groups.insert(group.id, row);
        column->addWidget(row);
        if (m_interactive) {
            const QString id = group.id;
            connect(row, &SwitchRow::clicked, this, [this, id](bool on) { m_module->setGroupEnabled(id, on); });
        }
    }
    auto *hint = Ui::caption(I18n::tr("Covered parts show a plain gray to the others. On your screen nothing changes."), card);
    hint->setWordWrap(true);
    column->addSpacing(4);
    column->addWidget(hint);
    if (m_interactive) {
        connect(m_notifications, &SwitchRow::clicked, this, [this](bool on) { m_module->setHideNotifications(on); });
        connect(m_desktop, &SwitchRow::clicked, this, [this](bool on) { m_module->setHideDesktop(on); });
    }
    return card;
}

QWidget *MeetFramePanel::buildAppsCard()
{
    QVBoxLayout *column = nullptr;
    QFrame *card = titledCard(this, I18n::tr("Other apps to cover"), &column, 6);
    m_appRows = new QWidget(card);
    m_appLayout = new QVBoxLayout(m_appRows);
    m_appLayout->setContentsMargins(0, 0, 0, 0);
    m_appLayout->setSpacing(4);
    column->addWidget(m_appRows);
    auto *line = new QHBoxLayout();
    line->setContentsMargins(0, 4, 0, 0);
    m_add = Ui::button(I18n::tr("Add app"), QString(), QStringLiteral("sm"), card);
    m_add->setFocusPolicy(Qt::NoFocus);
    Ui::setDropdownArrow(m_add);
    line->addWidget(m_add, 0, Qt::AlignLeft);
    line->addStretch(1);
    column->addLayout(line);
    if (m_interactive) {
        connect(m_add, &QPushButton::clicked, this, &MeetFramePanel::showAddMenu);
    }
    return card;
}

QWidget *MeetFramePanel::buildPictureCard()
{
    QVBoxLayout *column = nullptr;
    QFrame *card = titledCard(this, I18n::tr("Picture"), &column, 4);
    m_cursor = new SwitchRow(I18n::tr("Show the pointer"), QString(), card);
    column->addWidget(m_cursor);
    auto *row = new QHBoxLayout();
    row->setContentsMargins(0, 4, 0, 0);
    row->setSpacing(8);
    row->addWidget(Ui::label(I18n::tr("Frame rate"), "optionLabel", card), 0, Qt::AlignVCenter);
    m_fps = new SegmentedSwitch({QStringLiteral("15 fps"), QStringLiteral("30 fps"), QStringLiteral("60 fps")}, 1, card);
    row->addWidget(m_fps, 0, Qt::AlignVCenter);
    row->addStretch(1);
    column->addLayout(row);
    auto *hint = Ui::caption(I18n::tr("More frames move smoother and use more of the computer."), card);
    hint->setWordWrap(true);
    column->addWidget(hint);
    // La forma: libre (cualquier tamaño) o fija al estirar. Prender el interruptor fija la ultima proporcion
    // elegida; elegir una proporcion tambien la fija.
    m_aspectLock = new SwitchRow(I18n::tr("Lock the frame shape"), QString(), card);
    column->addWidget(m_aspectLock);
    auto *shapeRow = new QHBoxLayout();
    shapeRow->setContentsMargins(0, 4, 0, 0);
    shapeRow->setSpacing(8);
    shapeRow->addWidget(Ui::label(I18n::tr("Frame shape"), "optionLabel", card), 0, Qt::AlignVCenter);
    m_aspect = new SegmentedSwitch({QStringLiteral("16:9"), QStringLiteral("4:3"), QStringLiteral("1:1"), I18n::tr("Current")}, 0, card);
    shapeRow->addWidget(m_aspect, 0, Qt::AlignVCenter);
    shapeRow->addStretch(1);
    column->addLayout(shapeRow);
    m_aspectHint = Ui::caption(QString(), card);
    m_aspectHint->setWordWrap(true);
    column->addWidget(m_aspectHint);
    if (m_interactive) {
        connect(m_cursor, &SwitchRow::clicked, this, [this](bool on) { m_module->setShowCursor(on); });
        connect(m_fps, &SegmentedSwitch::currentChanged, this, [this](int index) { m_module->setFps(index == 0 ? 15 : index == 2 ? 60 : 30); });
        connect(m_aspectLock, &SwitchRow::clicked, this, [this](bool on) {
            m_module->setAspect(on ? m_module->lastLockedAspect() : QStringLiteral("free"));
        });
        connect(m_aspect, &SegmentedSwitch::currentChanged, this, [this](int index) { m_module->setAspect(lockedAspect(index)); });
    }
    return card;
}

void MeetFramePanel::refresh()
{
    const QString toggleKeys = m_module->shortcut(MeetFrameModule::ToggleShortcut).displayText();
    const QString shortcutError = m_module->shortcutError();
    const QString sm = QStringLiteral("sm");
    if (m_module->needsPermission()) {
        m_status->set(QStringLiteral("warn"), I18n::tr("Allow screen recording"),
                      I18n::tr("macOS asks for permission to record the screen. Turn on LGA Mighty Tools in Screen Recording, then show the frame again."),
                      I18n::tr("Open System Settings"), QStringLiteral("primary"), QStringLiteral("warn"), sm);
    } else if (!m_module->error().isEmpty() || !shortcutError.isEmpty()) {
        m_status->set(QStringLiteral("error"), I18n::tr("Meet Frame needs attention"),
                      !m_module->error().isEmpty() ? m_module->error() : shortcutError,
                      m_module->error().isEmpty() ? QString() : I18n::tr("Try again"), QString(), QStringLiteral("err"), sm);
    } else if (m_module->frameShown() && m_module->paused()) {
        m_status->set(QStringLiteral("paused"), I18n::tr("Paused"), I18n::tr("People see the last picture until you resume."), I18n::tr("Resume"),
                      QStringLiteral("primary"), QString(), sm);
    } else if (m_module->frameShown()) {
        const int hidden = m_module->hiddenWindows();
        const QString text = hidden == 0   ? I18n::tr("In the meeting, share the window named Meet Frame. Nothing is covered right now.")
                             : hidden == 1 ? I18n::tr("In the meeting, share the window named Meet Frame. 1 window is covered right now.")
                                           : I18n::tr("In the meeting, share the window named Meet Frame. %1 windows are covered right now.").arg(hidden);
        m_status->set(QStringLiteral("on"), I18n::tr("Sharing the frame"), text, I18n::tr("Hide frame"), QString(), QString(), sm);
    } else {
        m_status->set(QStringLiteral("on"), I18n::tr("Ready to share"),
                      I18n::tr("Press %1 or show the frame here, then share the window named Meet Frame in the meeting.").arg(toggleKeys),
                      I18n::tr("Show frame"), QStringLiteral("primary"), QString(), sm);
    }

    m_toggle->setShortcut(m_module->shortcut(MeetFrameModule::ToggleShortcut));
    m_pause->setShortcut(m_module->shortcut(MeetFrameModule::PauseShortcut));
    m_notifications->setChecked(m_module->hideNotifications());
    m_desktop->setChecked(m_module->hideDesktop());
    for (auto it = m_groups.constBegin(); it != m_groups.constEnd(); ++it) it.value()->setChecked(m_module->groupEnabled(it.key()));
    m_cursor->setChecked(m_module->showCursor());
    const int fps = m_module->fps();
    m_fps->setCurrent(fps == 15 ? 0 : fps == 60 ? 2 : 1);
    const QString aspect = m_module->aspect();
    const bool locked = aspect != QLatin1String("free");
    m_aspectLock->setChecked(locked);
    // Libre: ninguna proporcion marcada (al prender el interruptor vuelve la ultima elegida).
    m_aspect->setCurrent(!locked ? -1 : aspect == QLatin1String("4:3") ? 1 : aspect == QLatin1String("1:1") ? 2 : aspect == QLatin1String("current") ? 3 : 0);
    m_aspectHint->setText(locked ? I18n::tr("Stretching the frame keeps this shape. Meetings show 16:9 without black bars.")
                                 : I18n::tr("The frame stretches to any size. Meetings show 16:9 without black bars."));
    rebuildApps();
}

void MeetFramePanel::rebuildApps()
{
    const QList<MeetFrameModule::CustomApp> apps = m_module->customApps();
    QString key;
    for (const MeetFrameModule::CustomApp &app : apps) key += app.id + QLatin1Char('|') + app.name + QLatin1Char('\n');
    if (key == m_appsKey && m_appLayout->count() > 0) {
        return;
    }
    m_appsKey = key;
    while (QLayoutItem *item = m_appLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    if (apps.isEmpty()) {
        auto *empty = Ui::caption(I18n::tr("None yet. Add any app you don't want people to see."), m_appRows);
        empty->setWordWrap(true);
        m_appLayout->addWidget(empty);
        return;
    }
    for (const MeetFrameModule::CustomApp &app : apps) {
        auto *row = new QWidget(m_appRows);
        auto *line = new QHBoxLayout(row);
        line->setContentsMargins(0, 2, 0, 2);
        line->setSpacing(8);
        auto *tile = new AppIconTile(row);
        if (!app.path.isEmpty()) tile->setIcon(AppToggleSystem::icon(app.path));
        line->addWidget(tile, 0, Qt::AlignVCenter);
        auto *name = new ElidedLabel(row);
        name->setObjectName(QStringLiteral("optionLabel"));
        name->setText(app.name);
        line->addWidget(name, 1, Qt::AlignVCenter);
        auto *id = Ui::label(app.id, "meta", row);
        line->addWidget(id, 0, Qt::AlignVCenter);
        auto *remove = Ui::button(QString(), QStringLiteral("ghost"), QStringLiteral("icon"), row);
        remove->setFocusPolicy(Qt::NoFocus);
        Ui::setIcon(remove, Icon::X, Theme::color(Theme::kIcon), 12);
        remove->setAccessibleName(I18n::tr("Stop covering %1").arg(app.name));
        CustomTooltip::instance()->setToolTip(remove, I18n::tr("Stop covering this app"));
        line->addWidget(remove, 0, Qt::AlignVCenter);
        if (m_interactive) {
            const QString appId = app.id;
            connect(remove, &QPushButton::clicked, this, [this, appId]() { runLater(this, [this, appId]() { m_module->removeApp(appId); }); });
        }
        m_appLayout->addWidget(row);
    }
}

void MeetFramePanel::showAddMenu()
{
    QPointer<MeetFramePanel> safe(this);
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    QSet<QString> already;
    for (const MeetFrameModule::CustomApp &app : m_module->customApps()) already.insert(app.id);
    const QList<MeetFrameSystem::RunningApp> running = MeetFrameSystem::runningApps();
    int added = 0;
    for (const MeetFrameSystem::RunningApp &app : running) {
        if (already.contains(app.id)) continue;
        QAction *action = menu->addAction(AppToggleSystem::icon(app.path), app.name);
        const MeetFrameModule::CustomApp entry{app.id, app.name, app.path};
        connect(action, &QAction::triggered, this, [this, entry]() { runLater(this, [this, entry]() { m_module->addApp(entry); }); });
        ++added;
    }
    if (added == 0) {
        menu->addAction(I18n::tr("No other open apps"))->setEnabled(false);
    }
    menu->addSeparator();
    connect(menu->addAction(I18n::tr("Browse...")), &QAction::triggered, this, [this]() { runLater(this, [this]() { browse(); }); });
    menu->popup(m_add->mapToGlobal(QPoint(0, m_add->height() + 2)));
}

void MeetFramePanel::browse()
{
#ifdef Q_OS_MACOS
    const QString start = QStringLiteral("/Applications");
    const QString filter = I18n::tr("Applications (*.app);;All files (*)");
#else
    const QString start = QStringLiteral("C:/Program Files");
    const QString filter = I18n::tr("Executable (*.exe)");
#endif
    QPointer<MeetFramePanel> safe(this);
    const QString chosen = QFileDialog::getOpenFileName(this, I18n::tr("Choose an app"), start, filter);
    if (!safe || chosen.isEmpty()) {
        return;
    }
    const QString id = MeetFrameSystem::appIdForPath(chosen);
    if (id.isEmpty()) {
        return;
    }
    QString name = AppToggleSystem::productName(chosen);
    if (name.isEmpty()) name = QFileInfo(chosen).completeBaseName();
    m_module->addApp({id, name, QDir::toNativeSeparators(chosen)});
}

void MeetFramePanel::statusAction()
{
    if (m_module->needsPermission()) {
        m_module->requestPermission();
        return;
    }
    if (m_module->frameShown() && m_module->paused()) {
        m_module->togglePause();
        return;
    }
    if (m_module->frameShown()) {
        m_module->hideFrame();
        return;
    }
    m_module->showFrame();
}
