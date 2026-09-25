#include "ui/ShortcutRow.h"
#include "ui/Theme.h"
#include "ui/UiWidgets.h"

#include <QDebug>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

Qt::KeyboardModifiers relevantModifiers(Qt::KeyboardModifiers modifiers)
{
    return modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier);
}

// La tecla del evento como Qt::Key de letra, digito o F1-F12. En Windows se lee la virtual-key: con
// Shift apretado, event->key() de "1" es "!" y la combinacion no se podria guardar.
int keyFromEvent(const QKeyEvent *event)
{
#ifdef Q_OS_WIN
    const quint32 vk = event->nativeVirtualKey();
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
        return static_cast<int>(vk);
    }
    if (vk >= 0x70 && vk <= 0x7B) { // VK_F1..VK_F12
        return Qt::Key_F1 + static_cast<int>(vk - 0x70);
    }
#endif
    return event->key();
}

} // namespace

ShortcutRow::ShortcutRow(const QString &name, const QString &description, QWidget *parent)
    : QWidget(parent)
    , m_description(description)
{
    setObjectName(QStringLiteral("shortcutRow"));
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(3);

    auto *row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(5);
    m_name = new QLabel(name, this);
    m_name->setObjectName(QStringLiteral("shortcutName"));
    row->addWidget(m_name, 1);

    m_keys = new QWidget(this);
    m_keysLayout = new QHBoxLayout(m_keys);
    m_keysLayout->setContentsMargins(0, 0, 0, 0);
    m_keysLayout->setSpacing(5);
    row->addWidget(m_keys, 0, Qt::AlignVCenter);

    m_recorder = new QFrame(this);
    m_recorder->setObjectName(QStringLiteral("recorder"));
    m_recorder->setFixedWidth(168);
    auto *recorderLayout = new QHBoxLayout(m_recorder);
    recorderLayout->setContentsMargins(8, 0, 8, 0);
    recorderLayout->setSpacing(6);
    m_recorderText = new QLabel(m_recorder);
    m_recorderText->setObjectName(QStringLiteral("recorderText"));
    recorderLayout->addWidget(m_recorderText, 1);
    auto *dot = new QLabel(m_recorder);
    dot->setObjectName(QStringLiteral("recorderDot"));
    recorderLayout->addWidget(dot, 0, Qt::AlignVCenter);
    m_recorder->hide();
    row->addWidget(m_recorder, 0, Qt::AlignVCenter);

    m_editButton = Ui::button(QString(), QStringLiteral("ghost"), QStringLiteral("icon"), this);
    m_editButton->setObjectName(QStringLiteral("editShortcut"));
    Ui::setIcon(m_editButton, Icon::Pencil, Theme::color(Theme::kIcon), 14);
    m_editButton->setToolTip(QStringLiteral("Change shortcut"));
    m_editButton->setAccessibleName(QStringLiteral("Change shortcut"));
    row->addSpacing(4);
    row->addWidget(m_editButton, 0, Qt::AlignVCenter);
    column->addLayout(row);

    m_caption = new QLabel(description, this);
    m_caption->setObjectName(QStringLiteral("caption"));
    m_caption->setWordWrap(true);
    column->addWidget(m_caption);

    connect(m_editButton, &QPushButton::clicked, this, [this]() {
        if (m_recording) {
            cancelRecording();
        } else {
            startRecording();
        }
    });
}

void ShortcutRow::setShortcut(const Shortcut &shortcut)
{
    m_shortcut = shortcut;
    rebuildKeys();
}

void ShortcutRow::rebuildKeys()
{
    while (QLayoutItem *item = m_keysLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    for (const QString &key : m_shortcut.displayKeys()) {
        auto *chip = new Chip(m_keys);
        chip->set(QStringLiteral("key"), key);
        m_keysLayout->addWidget(chip, 0, Qt::AlignVCenter);
    }
}

QString ShortcutRow::partialText(Qt::KeyboardModifiers modifiers)
{
    if (!relevantModifiers(modifiers)) {
        return QStringLiteral("Press a shortcut...");
    }
    Shortcut probe;
    probe.modifiers = relevantModifiers(modifiers);
    probe.key = Qt::Key_A; // solo para armar los nombres de los modificadores
    QStringList keys = probe.displayKeys();
    keys.removeLast();
#ifdef Q_OS_MACOS
    return keys.join(QString()) + QStringLiteral(" ...");
#else
    return keys.join(QStringLiteral(" + ")) + QStringLiteral(" + ...");
#endif
}

void ShortcutRow::setRecordingVisible(bool recording, const QString &partial)
{
    m_keys->setVisible(!recording);
    m_recorder->setVisible(recording);
    m_recorderText->setText(partial);
    if (recording) {
        m_caption->setText(QStringLiteral("Press the new combination. Esc cancels."));
        Ui::setStyleProperty(m_caption, "tone", QString());
    }
}

void ShortcutRow::startRecording()
{
    if (m_recording) {
        return;
    }
    m_recording = true;
    setRecordingVisible(true, partialText(Qt::NoModifier));
    // grabKeyboard y no el foco: la app no da foco de teclado a nada (Theme::apply).
    grabKeyboard();
    qDebug() << "[ShortcutRow] Grabando atajo para" << m_name->text();
}

void ShortcutRow::cancelRecording()
{
    if (!m_recording) {
        return;
    }
    finishRecording();
    setError(QString());
}

void ShortcutRow::finishRecording()
{
    m_recording = false;
    releaseKeyboard();
    setRecordingVisible(false, QString());
}

void ShortcutRow::setError(const QString &error)
{
    m_caption->setText(error.isEmpty() ? m_description : error);
    Ui::setStyleProperty(m_caption, "tone", error.isEmpty() ? QString() : QStringLiteral("err"));
}

void ShortcutRow::showRecordingFixture(const QString &partial)
{
    setRecordingVisible(true, partial);
}

void ShortcutRow::keyPressEvent(QKeyEvent *event)
{
    if (!m_recording) {
        QWidget::keyPressEvent(event);
        return;
    }
    event->accept();
    const Qt::KeyboardModifiers modifiers = relevantModifiers(event->modifiers());
    if (event->key() == Qt::Key_Escape && !modifiers) {
        cancelRecording();
        return;
    }
    if (Shortcut::isModifierKey(event->key())) {
        m_recorderText->setText(partialText(modifiers));
        return;
    }
    Shortcut candidate;
    candidate.modifiers = modifiers;
    candidate.key = keyFromEvent(event);

    const Shortcut previous = m_shortcut;
    finishRecording();

    QString error;
    if (!Shortcut::isSupportedKey(candidate.key)) {
        error = QStringLiteral("Use a letter, a number or F1-F12.");
    } else if (!(modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
#ifdef Q_OS_MACOS
        error = QStringLiteral("Add ⌘, ⌥ or ⌃ so it doesn't take a plain key.");
#else
        error = QStringLiteral("Add Ctrl or Alt so it doesn't take a plain key.");
#endif
    } else if (candidate == previous) {
        setError(QString());
        return;
    } else if (m_validator) {
        error = m_validator(candidate);
    }
    if (!error.isEmpty()) {
        qInfo() << "[ShortcutRow] Rechazado" << candidate.toPortableString() << "->" << error;
        setError(QStringLiteral("%1 Kept %2.").arg(error, previous.displayText()));
        return;
    }
    qInfo() << "[ShortcutRow] Nuevo atajo para" << m_name->text() << ":" << candidate.toPortableString();
    setError(QString());
    emit shortcutRecorded(candidate);
}

void ShortcutRow::keyReleaseEvent(QKeyEvent *event)
{
    if (!m_recording) {
        QWidget::keyReleaseEvent(event);
        return;
    }
    event->accept();
    m_recorderText->setText(partialText(relevantModifiers(event->modifiers())));
}
