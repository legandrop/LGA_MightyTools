#include "modules/folderswitch/FolderSwitchPanel.h"

#include "ui/ShortcutRow.h"
#include "ui/Theme.h"
#include "ui/UiWidgets.h"

#include <QCheckBox>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

// Sangria de lo que va debajo de un checkbox: indicador (16) + spacing (10). Mismo valor que
// GeneralPage (app/GeneralPage.cpp, kCheckIndent).
constexpr int kCheckIndent = 26;

// Encabezado de tarjeta (`.head`), igual al de GeneralPage: titulo de 22 de alto y 8 de aire abajo.
// `right` es lo que va a la derecha del titulo (chips, hora): vacio si no hace falta.
QHBoxLayout *addHead(QVBoxLayout *layout, const QString &title, QWidget *parent)
{
    auto *head = new QHBoxLayout();
    head->setContentsMargins(0, 0, 0, 0);
    head->setSpacing(6);
    QLabel *t = Ui::label(title, "cardTitle", parent);
    t->setMinimumHeight(22);
    head->addWidget(t, 1);
    layout->addLayout(head);
    layout->addSpacing(8);
    return head;
}

} // namespace

FolderSwitchPanel::FolderSwitchPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("folderSwitchPanel"));
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(10);

    m_status = new StatusCard(this);
    column->addWidget(m_status);
    connect(m_status->button(), &QPushButton::clicked, this,
            [this]() { emit toggleRequested(!m_state.enabled); });

    column->addWidget(buildShortcutsCard());
    m_lastCard = buildLastFolderCard();
    column->addWidget(m_lastCard);

    setState(ViewState());
}

QFrame *FolderSwitchPanel::buildShortcutsCard()
{
    QFrame *card = Ui::card(this);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(0);

    // "Switch automatically" (canvas: chk(true,'Switch automatically') + caption indentada).
    m_autoSwitch = new QCheckBox(QStringLiteral("Switch automatically"), card);
    layout->addWidget(m_autoSwitch);
    QLabel *autoCaption = Ui::caption(QStringLiteral("When you return to a dialog from the file manager."), card);
    autoCaption->setContentsMargins(kCheckIndent, 3, 0, 0);
    layout->addWidget(autoCaption);
    connect(m_autoSwitch, &QCheckBox::clicked, this, &FolderSwitchPanel::autoSwitchToggled);

    Ui::addDivider(layout, card);

    // Los dos atajos, editables con el lapiz (D-16): mismo componente que Nuke Shortcuts.
    m_manualRow = new ShortcutRow(QStringLiteral("Manual shortcut"),
                                  QStringLiteral("Press it inside a file dialog to jump right away."), card);
    layout->addWidget(m_manualRow);
    connect(m_manualRow, &ShortcutRow::shortcutRecorded, this, &FolderSwitchPanel::manualShortcutRecorded);

    Ui::addDivider(layout, card);

    m_recentRow = new ShortcutRow(QStringLiteral("Recent folders"),
                                  QStringLiteral("Press it inside a file dialog to pick a recent folder."), card);
    layout->addWidget(m_recentRow);
    connect(m_recentRow, &ShortcutRow::shortcutRecorded, this, &FolderSwitchPanel::recentShortcutRecorded);

    return card;
}

QFrame *FolderSwitchPanel::buildLastFolderCard()
{
    QFrame *card = Ui::card(this);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(0);

    QHBoxLayout *head = addHead(layout, QStringLiteral("Last folder"), card);
    m_lastSourceChip = new Chip(card);
    head->addWidget(m_lastSourceChip, 0, Qt::AlignVCenter);
    m_lastResultChip = new Chip(card);
    head->addWidget(m_lastResultChip, 0, Qt::AlignVCenter);
    m_lastTimeLabel = Ui::label(QString(), "meta", card);
    m_lastTimeLabel->setContentsMargins(2, 0, 0, 0);
    head->addWidget(m_lastTimeLabel, 0, Qt::AlignVCenter);

    // Campo de solo lectura (`.field.wide` del canvas): icono de carpeta + ruta elidida al medio.
    auto *field = new QFrame(card);
    field->setObjectName(QStringLiteral("field"));
    auto *fieldLayout = new QHBoxLayout(field);
    fieldLayout->setContentsMargins(8, 0, 8, 0);
    fieldLayout->setSpacing(6);
    auto *folderIcon = new IconWidget(Icon::Folder, Theme::color(Theme::kIcon), 14, field);
    fieldLayout->addWidget(folderIcon, 0, Qt::AlignVCenter);
    m_lastFieldValue = new ElidedLabel(field);
    m_lastFieldValue->setObjectName(QStringLiteral("fieldValue"));
    m_lastFieldValue->setElideMode(Qt::ElideMiddle);
    fieldLayout->addWidget(m_lastFieldValue, 1);
    layout->addWidget(field);

    m_lastCaption = Ui::caption(QString(), card);
    m_lastCaption->setContentsMargins(0, 3, 0, 0);
    layout->addWidget(m_lastCaption);

    return card;
}

void FolderSwitchPanel::setState(const ViewState &state)
{
    m_state = state;

    // Tarjeta de estado: solo dos formas (on / paused), sin variante de error -- el atajo tomado se
    // ve en su propia fila, mas abajo (canvas, "Folder Switch · estados").
    if (state.enabled) {
        m_status->set(QStringLiteral("on"), QStringLiteral("Switching is on"),
                      QStringLiteral("Dialogs jump to the last folder you used."), QStringLiteral("Pause"),
                      QString(), QString());
    } else {
        const bool anyShortcutAlive = state.manualRegistered || state.recentRegistered;
        m_status->set(QStringLiteral("paused"), QStringLiteral("Switching is paused"),
                      anyShortcutAlive ? QStringLiteral("Only the shortcuts work while paused.")
                                       : QStringLiteral("Dialogs keep their own folder."),
                      QStringLiteral("Resume"), QStringLiteral("primary"), QString());
    }

    m_autoSwitch->blockSignals(true);
    m_autoSwitch->setChecked(state.autoSwitch);
    m_autoSwitch->blockSignals(false);

    if (!m_manualRow->isRecording()) {
        m_manualRow->setShortcut(state.manualShortcut);
        m_manualRow->setExtraChip(state.manualRegistered ? QString() : QStringLiteral("err"), QStringLiteral("In use"));
        m_manualRow->setError(state.manualRegistered ? QString()
                                                     : QStringLiteral("Another app took it. Automatic switching isn't affected."));
    }
    if (!m_recentRow->isRecording()) {
        m_recentRow->setShortcut(state.recentShortcut);
        m_recentRow->setExtraChip(state.recentRegistered ? QString() : QStringLiteral("err"), QStringLiteral("In use"));
        m_recentRow->setError(state.recentRegistered ? QString()
                                                     : QStringLiteral("Another app took it. The other shortcut isn't affected."));
    }

    updateLastFolderCard();
}

void FolderSwitchPanel::updateLastFolderCard()
{
    const bool valid = m_state.lastSwitch.isValid();
    m_lastSourceChip->setVisible(valid);
    m_lastResultChip->setVisible(valid);
    m_lastTimeLabel->setVisible(valid);

    if (valid) {
        m_lastSourceChip->set(QStringLiteral("src"), m_state.lastSwitch.source);
        m_lastResultChip->set(m_state.lastSwitch.applied ? QStringLiteral("ok") : QStringLiteral("err"),
                              m_state.lastSwitch.applied ? QStringLiteral("Applied") : QStringLiteral("Not applied"));
        m_lastTimeLabel->setText(m_state.lastSwitch.when.toString(QStringLiteral("HH:mm")));
        const QString nativePath = QDir::toNativeSeparators(m_state.lastSwitch.path);
        Ui::setStyleProperty(m_lastFieldValue, "empty", false);
        m_lastFieldValue->setText(nativePath);
        m_lastFieldValue->setToolTip(nativePath);
    } else {
        Ui::setStyleProperty(m_lastFieldValue, "empty", true);
        m_lastFieldValue->setText(QStringLiteral("No folder yet"));
        m_lastFieldValue->setToolTip(QString());
    }

    QString caption;
    QString tone;
    if (!valid) {
        caption = QStringLiteral("Open a folder in Explorer, then go to a file dialog.");
    } else if (!m_state.lastSwitch.applied) {
        caption = m_state.manualRegistered ? QStringLiteral("The dialog didn't take it. Retry with Ctrl+Alt+O.")
                                           : QStringLiteral("The dialog didn't take it. Pick the folder by hand.");
        tone = QStringLiteral("err");
    }
    m_lastCaption->setText(caption);
    m_lastCaption->setVisible(!caption.isEmpty());
    Ui::setStyleProperty(m_lastCaption, "tone", tone);
}
