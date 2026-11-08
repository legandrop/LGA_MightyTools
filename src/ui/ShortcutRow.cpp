#include "ui/ShortcutRow.h"
#include "platform/HotkeyService.h"
#include "ui/Theme.h"
#include "ui/UiWidgets.h"
#include "core/I18n.h"

#include <QApplication>
#include <QDebug>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace {

Qt::KeyboardModifiers relevantModifiers(Qt::KeyboardModifiers modifiers)
{
    return modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier);
}

// La tecla del evento como Qt::Key de letra, digito, F1-F12 o flecha. En Windows se lee la virtual-key: con
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

class ElidedShortcutName : public QLabel
{
public:
    ElidedShortcutName(const QString &name, QWidget *parent) : QLabel(name, parent), m_fullName(name)
    {
        setTextFormat(Qt::PlainText);
        setAccessibleName(name);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        setText(fontMetrics().elidedText(m_fullName, Qt::ElideRight, width()));
    }

private:
    QString m_fullName;
};

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
    m_name = new ElidedShortcutName(name, this);
    m_name->setObjectName(QStringLiteral("shortcutName"));
    row->addWidget(m_name, 1);
    m_topRow = row;

    // El chip opcional (Folder Switch: "In use"), el grabador y la nota de Windows se crean recien cuando
    // hacen falta (ensureExtraChip, ensureRecorder, ensureWindowsNote): una pagina con doce atajos armaba,
    // pulia y medía seis widgets ocultos por fila.
    m_keys = new QWidget(this);
    m_keysLayout = new QHBoxLayout(m_keys);
    m_keysLayout->setContentsMargins(0, 0, 0, 0);
    m_keysLayout->setSpacing(5);
    row->addWidget(m_keys, 0, Qt::AlignVCenter);

    m_editButton = Ui::button(QString(), QStringLiteral("ghost"), QStringLiteral("icon"), this);
    m_editButton->setObjectName(QStringLiteral("editShortcut"));
    Ui::setIcon(m_editButton, Icon::Pencil, Theme::color(Theme::kIcon), 14);
    m_editButton->setAccessibleName(I18n::tr("Change shortcut"));
    // Mismo aire que entre las teclas (`.row` con gap 5 del canvas).
    row->addWidget(m_editButton, 0, Qt::AlignVCenter);
    column->addLayout(row);

    m_caption = new CaptionLabel(description, this);
    m_caption->setObjectName(QStringLiteral("caption"));
    m_caption->setWordWrap(true);
    m_captionRow = new QHBoxLayout();
    m_captionRow->setContentsMargins(0, 0, 0, 0);
    m_captionRow->setSpacing(5);
    m_captionRow->addWidget(m_caption, 1);
    column->addLayout(m_captionRow);

    connect(m_editButton, &QPushButton::clicked, this, [this]() {
        if (m_editHandler) {
            m_editHandler();
            return;
        }
        if (m_recording) {
            cancelRecording();
        } else {
            startRecording();
        }
    });
}

void ShortcutRow::setTrailingWidget(QWidget *widget)
{
    m_captionRow->addWidget(widget, 0, Qt::AlignVCenter);
}

void ShortcutRow::setExtraChip(const QString &tone, const QString &text)
{
    if (tone.isEmpty()) {
        if (m_extraChip) {
            m_extraChip->hide();
        }
        return;
    }
    if (!m_extraChip) {
        // Entre el nombre y las teclas.
        m_extraChip = new Chip(this);
        m_extraChip->hide();
        m_topRow->insertWidget(m_topRow->indexOf(m_keys), m_extraChip, 0, Qt::AlignVCenter);
    }
    m_extraChip->set(tone, text);
    m_extraChip->show();
}

void ShortcutRow::ensureRecorder()
{
    if (m_recorder) {
        return;
    }
    m_recorder = new QFrame(this);
    m_recorder->setObjectName(QStringLiteral("recorder"));
    m_recorder->setFixedWidth(168);
    auto *recorderLayout = new QHBoxLayout(m_recorder);
    recorderLayout->setContentsMargins(8, 0, 8, 0);
    recorderLayout->setSpacing(6);
    m_recorderText = new QLabel(m_recorder);
    m_recorderText->setObjectName(QStringLiteral("recorderText"));
    recorderLayout->addWidget(m_recorderText, 1);
    // Grabando: el punto magenta (lo que esta haciendo el usuario), como el resto de las marcas.
    auto *dot = new StatusDot(9, m_recorder);
    dot->setObjectName(QStringLiteral("recorderDot"));
    dot->setTone(QStringLiteral("accent"));
    recorderLayout->addWidget(dot, 0, Qt::AlignVCenter);
    m_recorder->hide();
    // Donde estan las teclas, antes del lapiz.
    m_topRow->insertWidget(m_topRow->indexOf(m_editButton), m_recorder, 0, Qt::AlignVCenter);
}

void ShortcutRow::syncWindowsNote()
{
    const bool visible = m_windowsNoteEnabled && !m_holdKeys && m_shortcut.replacesWindowsShortcut();
    if (!visible) {
        if (m_windowsNote) {
            m_windowsNote->hide();
        }
        return;
    }
    if (!m_windowsNote) {
        m_windowsNote = Ui::caption(I18n::tr("Replaces the Windows shortcut for this key."), this);
        // El objectName "caption" es el que le da el gris de las descripciones: la marca va aparte.
        m_windowsNote->setProperty("windowsShortcutNote", true);
        // Debajo de la descripcion: lo ultimo de la columna.
        layout()->addWidget(m_windowsNote);
    }
    m_windowsNote->show();
}

void ShortcutRow::setShortcut(const Shortcut &shortcut)
{
    m_shortcut = shortcut;
    m_keyLabels.clear();
    rebuildKeys();
}

void ShortcutRow::setKeyLabels(const QStringList &labels)
{
    m_shortcut = Shortcut();
    m_keyLabels = labels;
    rebuildKeys();
}

void ShortcutRow::showWindowsShortcutNote(bool enabled)
{
    m_windowsNoteEnabled = enabled;
    syncWindowsNote();
}

void ShortcutRow::rebuildKeys()
{
    while (QLayoutItem *item = m_keysLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    for (const QString &key : m_keyLabels.isEmpty() ? m_shortcut.displayKeys() : m_keyLabels) {
        auto *chip = new Chip(m_keys);
        chip->set(QStringLiteral("key"), key);
        m_keysLayout->addWidget(chip, 0, Qt::AlignVCenter);
    }
    // Sin atajo (una entrada nueva de App Toggle): el lapiz solo, sin nada al lado, parece un hueco.
    if (m_keysLayout->isEmpty()) {
        m_keysLayout->addWidget(Ui::caption(I18n::tr("Not set"), m_keys), 0, Qt::AlignVCenter);
    }
    syncWindowsNote();
}

QString ShortcutRow::partialText(Qt::KeyboardModifiers modifiers) const
{
    if (!relevantModifiers(modifiers)) {
        return m_holdKeys ? I18n::tr("Press the keys...") : I18n::tr("Press a shortcut...");
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
    if (recording) {
        ensureRecorder();
    }
    m_keys->setVisible(!recording);
    if (m_recorder) {
        m_recorder->setVisible(recording);
        m_recorderText->setText(partial);
    }
    if (recording) {
        m_caption->setText(m_holdKeys ? I18n::tr("Press the keys to hold, like Space. Esc cancels.")
                                      : I18n::tr("Press the new combination. Esc cancels."));
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
    // Windows: Win+E y otras que reserva el sistema nunca llegan a la ventana; las atrapa un gancho que
    // vive solo mientras se graba.
    m_catcher = new ShortcutCatcher(this);
    connect(m_catcher, &ShortcutCatcher::caught, this, [this](const Shortcut &shortcut) {
        if (m_recording) acceptCandidate(shortcut);
    });
    // Un click afuera (en otra fila, otra tarjeta, la barra lateral) cancela, como el umbral de un
    // disco lo confirma: la grabacion nunca queda colgada esperando teclas.
    qApp->installEventFilter(this);
    qDebug() << "[ShortcutRow] Grabando atajo para" << m_name->accessibleName();
}

void ShortcutRow::cancelRecording()
{
    if (!m_recording) {
        return;
    }
    finishRecording();
    setError(QString());
}

bool ShortcutRow::eventFilter(QObject *watched, QEvent *event)
{
    // Pasar a otra app cancela: la grabacion nunca toma teclas que el usuario le escribe a otra ventana.
    if (m_recording && event->type() == QEvent::ApplicationDeactivate) {
        cancelRecording();
        return QWidget::eventFilter(watched, event);
    }
    if (m_recording && event->type() == QEvent::MouseButtonPress && watched->isWidgetType()) {
        auto *target = static_cast<QWidget *>(watched);
        // El lapiz tiene su propio click (alterna): el resto de la fila y todo lo de afuera cancela.
        if (target != m_editButton && !m_editButton->isAncestorOf(target)) {
            cancelRecording();
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ShortcutRow::hideEvent(QHideEvent *event)
{
    // Otra pagina, otra herramienta o la ventana a la bandeja: la fila oculta deja de grabar.
    cancelRecording();
    QWidget::hideEvent(event);
}

void ShortcutRow::finishRecording()
{
    m_recording = false;
    if (m_catcher) {
        disconnect(m_catcher, nullptr, this, nullptr);
        m_catcher->retire();
        m_catcher = nullptr;
    }
    qApp->removeEventFilter(this);
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
        if (m_recorderText) m_recorderText->setText(partialText(modifiers));
        return;
    }
    Shortcut candidate;
    candidate.modifiers = modifiers;
    candidate.key = keyFromEvent(event);
    acceptCandidate(candidate);
}

void ShortcutRow::acceptCandidate(const Shortcut &candidate)
{
    const Qt::KeyboardModifiers modifiers = candidate.modifiers;
    const Shortcut previous = m_shortcut;
    finishRecording();

    // Teclas para mantener (Mouse Buttons): una sola sin modificadores es lo comun (Espacio en Photoshop).
    const bool keyOk = Shortcut::isSupportedKey(candidate.key) || (m_holdKeys && candidate.key == Qt::Key_Space);
    QString error;
    if (!keyOk) {
        error = m_holdKeys ? I18n::tr("Use a letter, a number, F1-F12, an arrow key or Space.")
                           : I18n::tr("Use a letter, a number, F1-F12 or an arrow key.");
    } else if (!m_holdKeys && !(modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
#ifdef Q_OS_MACOS
        error = I18n::tr("Add ⌘, ⌥ or ⌃ so it doesn't take a plain key.");
#else
        error = I18n::tr("Add Ctrl, Alt or Win so it doesn't take a plain key.");
#endif
    } else if (candidate.isLockShortcut()) {
        error = I18n::tr("Windows keeps Win+L to lock the session.");
    } else if (candidate == previous) {
        setError(QString());
        return;
    } else if (m_validator) {
        error = m_validator(candidate);
    }
    if (!error.isEmpty()) {
        qInfo() << "[ShortcutRow] Rechazado" << candidate.toPortableString() << "->" << error;
        // Una entrada nueva no tenia atajo: no hay nada que "se conserve".
        setError(previous.isValid() ? I18n::tr("%1 Kept %2.").arg(error, previous.displayText()) : error);
        return;
    }
    qInfo() << "[ShortcutRow] Nuevo atajo para" << m_name->accessibleName() << ":" << candidate.toPortableString();
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
    if (m_recorderText) m_recorderText->setText(partialText(relevantModifiers(event->modifiers())));
}
