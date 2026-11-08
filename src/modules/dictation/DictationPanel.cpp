#include "modules/dictation/DictationPanel.h"

#include "core/I18n.h"
#include "modules/dictation/DictationModule.h"
#include "modules/diskspace/DiskCard.h"
#include "modules/diskspace/DiskSpace.h"
#include "ui/CustomTooltip.h"
#include "ui/ShortcutRow.h"
#include "ui/Theme.h"
#include "ui/UiWidgets.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFileDialog>
#include <QFocusEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QStackedLayout>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

#include <iterator>

namespace {

using State = DictationModels::State;

// Al lado de la barra de una pieza que baja: el porcentaje, o que esta reintentando (regla de red).
QString downloadingText(const DictationModule::PieceInfo &piece)
{
    if (piece.retryAttempt > 0) {
        return I18n::tr("Retrying (%1 of %2)…").arg(piece.retryAttempt).arg(DictationModels::kAttempts);
    }
    return I18n::tr("%1%").arg(piece.progress);
}

// El desplegable de la app (`fieldButton`): la rueda no lo cambia (un QPushButton no la usa) y la opción se
// elige en su menú.
QPushButton *dropdown(QWidget *parent)
{
    auto *button = Ui::button(QString(), QString(), QString(), parent);
    button->setObjectName(QStringLiteral("fieldButton"));
    button->setFocusPolicy(Qt::NoFocus);
    Ui::setDropdownArrow(button);
    return button;
}

QLineEdit *field(const QString &placeholder, const QString &accessibleName, QWidget *parent)
{
    auto *edit = new QLineEdit(parent);
    edit->setObjectName(QStringLiteral("ruleField"));
    edit->setFocusPolicy(Qt::ClickFocus);
    edit->setPlaceholderText(placeholder);
    edit->setAccessibleName(accessibleName);
    return edit;
}

// Las palabras propias: un campo de varias líneas que crece con el texto (de 2 a 8 renglones), con los
// materiales de «ruleField». Enter guarda (las palabras se separan con comas, no con renglones).
class WordsEdit : public QPlainTextEdit
{
public:
    explicit WordsEdit(QWidget *parent)
        : QPlainTextEdit(parent)
    {
        setObjectName(QStringLiteral("ruleArea"));
        setFocusPolicy(Qt::ClickFocus);
        setTabChangesFocus(true);
        setLineWrapMode(QPlainTextEdit::WidgetWidth);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        document()->setDocumentMargin(2);
        connect(document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, this,
                [this]() { fitHeight(); });
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QPlainTextEdit::resizeEvent(event);
        fitHeight();
    }

private:
    void fitHeight()
    {
        // QPlainTextDocumentLayout mide el alto del documento en renglones visibles (con el ajuste de línea).
        const int needed = int(std::ceil(document()->documentLayout()->documentSize().height()));
        const int lines = qBound(2, needed, 8);
        // La barra solo cuando el texto pasa de 8 renglones; si no, el alto alcanza para todo.
        setVerticalScrollBarPolicy(needed > 8 ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
        const int chrome = height() - viewport()->height();
        const int wanted = lines * fontMetrics().lineSpacing() + 2 * int(document()->documentMargin()) + chrome + 4;
        if (wanted != height()) setFixedHeight(wanted);
    }
};

// Una tarjeta con su título en negrita, como «Saved rules» y «Create a mouse rule» en Mouse Buttons.
QFrame *titledCard(QWidget *parent, const QString &title, QVBoxLayout **column, int spacing = 6)
{
    QFrame *card = Ui::card(parent);
    *column = new QVBoxLayout(card);
    (*column)->setContentsMargins(18, 15, 18, 16);
    (*column)->setSpacing(spacing);
    (*column)->addWidget(Ui::label(title, "cardTitle", card));
    return card;
}

// Lo de la derecha de una fila (acciones normales) y, en su mismo lugar, «Confirm delete» mientras se
// confirma: la fila no salta y los controles quedan contra el borde derecho (como Desktop Icons).
struct DeleteSlot
{
    QWidget *actions = nullptr;
    QHBoxLayout *normal = nullptr; ///< ya trae el estiramiento: lo que se agrega queda a la derecha
    QPushButton *confirm = nullptr;
    QStackedLayout *slot = nullptr;
};

DeleteSlot deleteSlot(QWidget *row)
{
    DeleteSlot d;
    d.actions = new QWidget(row);
    d.slot = new QStackedLayout(d.actions);
    d.slot->setContentsMargins(0, 0, 0, 0);
    auto *normalPage = new QWidget(d.actions);
    d.normal = new QHBoxLayout(normalPage);
    d.normal->setContentsMargins(0, 0, 0, 0);
    d.normal->setSpacing(5);
    d.normal->addStretch(1);
    auto *armedPage = new QWidget(d.actions);
    auto *armedLine = new QHBoxLayout(armedPage);
    armedLine->setContentsMargins(0, 0, 0, 0);
    d.confirm = Ui::button(I18n::tr("Confirm delete"), QStringLiteral("danger"), QStringLiteral("sm"), armedPage);
    d.confirm->setFocusPolicy(Qt::NoFocus);
    d.confirm->setSizePolicy(QSizePolicy::Expanding, d.confirm->sizePolicy().verticalPolicy());
    armedLine->addWidget(d.confirm, 1, Qt::AlignVCenter);
    d.slot->addWidget(normalPage);
    d.slot->addWidget(armedPage);
    return d;
}

void clearLayout(QLayout *layout)
{
    while (QLayoutItem *item = layout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
}

QPushButton *iconButton(Icon icon, const QString &accessibleName, QWidget *parent)
{
    auto *button = Ui::button(QString(), QStringLiteral("ghost"), QStringLiteral("icon"), parent);
    button->setFocusPolicy(Qt::NoFocus);
    Ui::setIcon(button, icon, Theme::color(Theme::kIcon), 14);
    button->setAccessibleName(accessibleName);
    return button;
}

} // namespace

DictationPanel::DictationPanel(DictationModule *module, bool interactive, QWidget *parent)
    : QWidget(parent)
    , m_module(module)
    , m_interactive(interactive)
{
    setObjectName(QStringLiteral("dictationPanel"));
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(10);
    m_status = new StatusCard(this);
    m_status->setLive(true); // sube al encabezado de la hoja (D-73)
    column->addWidget(m_status);
    column->addWidget(buildShortcutCard());
    column->addWidget(buildSpeechCard());
    column->addWidget(buildWordsCard());
    column->addWidget(buildReplacementsCard());
    column->addWidget(buildModelCard());
    column->addWidget(buildRecentCard());
    column->addStretch();
    m_devices = m_module->devices();
    if (m_interactive) {
        connect(m_status->button(), &QPushButton::clicked, this, [this]() {
            runLater([this]() {
                bool downloading = false;
                for (const DictationModule::PieceInfo &piece : m_module->pieces()) {
                    if (piece.state == State::Downloading) {
                        downloading = true;
                        m_module->cancelDownload(piece.id);
                    }
                }
                if (!downloading) m_module->downloadMissing();
            });
        });
        // Los demás controles no toman foco: un click afuera no se lo saca a un campo. Lo hace este filtro.
        qApp->installEventFilter(this);
    }
    refresh();
}

QWidget *DictationPanel::buildShortcutCard()
{
    QVBoxLayout *column = nullptr;
    QFrame *card = titledCard(this, I18n::tr("Shortcut"), &column);
    m_shortcut = new ShortcutRow(I18n::tr("Dictate"),
                                 I18n::tr("Tap to start and again to finish, or hold it while you speak. Esc cancels."), card);
    // Win + letra reemplaza el atajo de Windows (Win+H abre su dictado): la nota gris lo dice.
    m_shortcut->showWindowsShortcutNote(true);
    m_shortcut->editButton()->setAccessibleName(I18n::tr("Record the dictation shortcut"));
    column->addWidget(m_shortcut);
    if (m_interactive) {
        m_shortcut->setValidator([this](const Shortcut &keys) { return m_module->validateShortcut(keys); });
        connect(m_shortcut, &ShortcutRow::shortcutRecorded, this, [this](const Shortcut &keys) {
            QString error;
            if (!m_module->setShortcut(keys, &error)) m_shortcut->setError(error);
        });
    }
    return card;
}

QWidget *DictationPanel::buildSpeechCard()
{
    QVBoxLayout *column = nullptr;
    QFrame *card = titledCard(this, I18n::tr("Voice"), &column);
    // Como Language y Zoom en General: una grilla para que los dos desplegables arranquen en la misma columna.
    auto *box = new QWidget(card);
    auto *grid = new QGridLayout(box);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(4);
    grid->setColumnStretch(2, 1);
    grid->setRowMinimumHeight(0, 26);
    grid->setRowMinimumHeight(1, 26);
    grid->addWidget(Ui::label(I18n::tr("Language"), "optionLabel", box), 0, 0, Qt::AlignVCenter);
    m_language = dropdown(box);
    m_language->setAccessibleName(I18n::tr("Language"));
    grid->addWidget(m_language, 0, 1, Qt::AlignVCenter | Qt::AlignLeft | Qt::AlignAbsolute);
    grid->addWidget(Ui::label(I18n::tr("Microphone"), "optionLabel", box), 1, 0, Qt::AlignVCenter);
    m_microphone = dropdown(box);
    m_microphone->setAccessibleName(I18n::tr("Microphone"));
    grid->addWidget(m_microphone, 1, 1, Qt::AlignVCenter | Qt::AlignLeft | Qt::AlignAbsolute);
    column->addWidget(box);
    if (m_interactive) {
        connect(m_language, &QPushButton::clicked, this, [this]() {
            QList<QPair<QString, QString>> options;
            for (const QString &code : DictationModule::languageCodes()) options.append({code, DictationModule::languageName(code)});
            showMenu(m_language, options, m_module->language(), [this](const QString &code) { m_module->setLanguage(code); });
        });
        connect(m_microphone, &QPushButton::clicked, this, [this]() {
            QList<QPair<QString, QString>> options{{QString(), I18n::tr("Default microphone")}};
            for (const AudioCapture::Device &device : std::as_const(m_devices)) options.append({device.id, device.name});
            showMenu(m_microphone, options, m_module->microphone(), [this](const QString &id) { m_module->setMicrophone(id); });
        });
    }
    return card;
}

QWidget *DictationPanel::buildWordsCard()
{
    QVBoxLayout *column = nullptr;
    QFrame *card = titledCard(this, I18n::tr("Custom words"), &column);
    m_words = new WordsEdit(card);
    m_words->setPlaceholderText(I18n::tr("Words separated by commas"));
    m_words->setAccessibleName(I18n::tr("Custom words"));
    column->addWidget(m_words);
    auto *wordsHint = Ui::caption(I18n::tr("Names and terms it should spell right, separated by commas."), card);
    wordsHint->setWordWrap(true);
    column->addWidget(wordsHint);
    // Guardar: al perder el foco (Enter, un click afuera o Tab), en eventFilter.
    return card;
}

QWidget *DictationPanel::buildReplacementsCard()
{
    QVBoxLayout *column = nullptr;
    QFrame *card = titledCard(this, I18n::tr("Replacements"), &column);
    auto *replaceHint = Ui::caption(I18n::tr("When it writes the text on the left, it puts the one on the right instead."), card);
    replaceHint->setWordWrap(true);
    column->addWidget(replaceHint);
    m_replacementRows = new QWidget(card);
    auto *rows = new QVBoxLayout(m_replacementRows);
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setSpacing(0);
    column->addWidget(m_replacementRows);
    auto *editor = new QHBoxLayout();
    editor->setSpacing(6);
    m_from = field(I18n::tr("It writes"), I18n::tr("Text to replace"), card);
    m_to = field(I18n::tr("Put instead"), I18n::tr("Replacement text"), card);
    editor->addWidget(m_from, 1);
    editor->addWidget(Ui::label(QStringLiteral("→"), "meta", card));
    editor->addWidget(m_to, 1);
    m_addReplacement = Ui::button(I18n::tr("Add"), QString(), QStringLiteral("sm"), card);
    m_addReplacement->setFocusPolicy(Qt::NoFocus);
    editor->addWidget(m_addReplacement);
    m_cancelReplacement = Ui::button(I18n::tr("Cancel"), QStringLiteral("ghost"), QStringLiteral("sm"), card);
    m_cancelReplacement->setFocusPolicy(Qt::NoFocus);
    m_cancelReplacement->hide();
    editor->addWidget(m_cancelReplacement);
    column->addLayout(editor);
    m_replacementError = Ui::caption(QString(), card);
    Ui::setStyleProperty(m_replacementError, "tone", QStringLiteral("err"));
    m_replacementError->setWordWrap(true);
    m_replacementError->hide();
    column->addWidget(m_replacementError);
    if (m_interactive) {
        connect(m_addReplacement, &QPushButton::clicked, this, &DictationPanel::submitReplacement);
        // Enter en cualquiera de los dos: con los dos escritos agrega; si no, suelta el campo. Nada mueve el
        // foco por su cuenta (los campos lo toman solo por click).
        connect(m_from, &QLineEdit::returnPressed, this, [this]() { finishReplacementField(m_from); });
        connect(m_to, &QLineEdit::returnPressed, this, [this]() { finishReplacementField(m_to); });
        connect(m_cancelReplacement, &QPushButton::clicked, this, &DictationPanel::resetReplacementEditor);
    }
    return card;
}

QWidget *DictationPanel::buildModelCard()
{
    QVBoxLayout *column = nullptr;
    QFrame *card = titledCard(this, I18n::tr("Model"), &column);
    // Rotulo de campo como «Folder» en las reglas de Disk Space y «Language» en General.
    column->addWidget(Ui::label(I18n::tr("Models folder"), "optionLabel", card));
    auto *rootRow = new QHBoxLayout();
    rootRow->setSpacing(6);
    m_root = field(QString(), I18n::tr("Models folder"), card);
    rootRow->addWidget(m_root, 1);
    auto *browse = Ui::button(I18n::tr("Browse..."), QString(), QStringLiteral("sm"), card);
    browse->setFocusPolicy(Qt::NoFocus);
    rootRow->addWidget(browse);
    column->addLayout(rootRow);
    m_rootError = Ui::caption(I18n::tr("Write a full path, like E:\\LGA_AI."), card);
    Ui::setStyleProperty(m_rootError, "tone", QStringLiteral("err"));
    m_rootError->hide();
    column->addWidget(m_rootError);
    auto *rootHint = Ui::caption(I18n::tr("Changing it doesn't move what's already downloaded."), card);
    rootHint->setWordWrap(true);
    column->addWidget(rootHint);
    Ui::addDivider(column, card);
    m_pieceRows = new QWidget(card);
    auto *rows = new QVBoxLayout(m_pieceRows);
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setSpacing(0);
    column->addWidget(m_pieceRows);
    auto *unloadRow = new QWidget(card);
    unloadRow->setMinimumHeight(26);
    auto *unload = new QHBoxLayout(unloadRow);
    unload->setContentsMargins(0, 0, 0, 0);
    unload->setSpacing(8);
    unload->addWidget(Ui::label(I18n::tr("Unload from memory after"), "optionLabel", unloadRow), 0, Qt::AlignVCenter);
    m_unload = dropdown(unloadRow);
    m_unload->setAccessibleName(I18n::tr("Unload from memory after"));
    unload->addWidget(m_unload, 0, Qt::AlignVCenter);
    unload->addStretch(1);
    column->addWidget(unloadRow);
    auto *unloadHint = Ui::caption(I18n::tr("Frees the memory while you're not dictating. The next dictation loads it again."), card);
    unloadHint->setWordWrap(true);
    column->addWidget(unloadHint);
    if (m_interactive) {
        connect(browse, &QPushButton::clicked, this, &DictationPanel::browseRoot);
        connect(m_root, &QLineEdit::editingFinished, this, &DictationPanel::saveRoot);
        connect(m_root, &QLineEdit::returnPressed, m_root, &QWidget::clearFocus);
        connect(m_unload, &QPushButton::clicked, this, [this]() {
            QList<QPair<QString, QString>> options;
            for (const int minutes : DictationModule::unloadChoices()) {
                options.append({QString::number(minutes), DictationModule::unloadText(minutes)});
            }
            showMenu(m_unload, options, QString::number(m_module->unloadMinutes()),
                     [this](const QString &minutes) { m_module->setUnloadMinutes(minutes.toInt()); });
        });
    }
    return card;
}

QWidget *DictationPanel::buildRecentCard()
{
    QVBoxLayout *column = nullptr;
    QFrame *card = titledCard(this, I18n::trc("dictation", "Recent"), &column);
    m_recentRows = new QWidget(card);
    auto *rows = new QVBoxLayout(m_recentRows);
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setSpacing(0);
    column->addWidget(m_recentRows);
    return card;
}

// ------------------------------------------------------------------ Refresco

void DictationPanel::refresh()
{
    refreshStatus();
    if (!m_shortcut->isRecording()) {
        m_shortcut->setShortcut(m_module->shortcut());
        m_shortcut->setError(m_module->shortcutError());
    }
    m_language->setText(DictationModule::languageName(m_module->language()));
    m_microphone->setText(microphoneText());
    if (!m_words->hasFocus() && m_words->toPlainText() != m_module->customWords()) m_words->setPlainText(m_module->customWords());
    if (!m_root->hasFocus()) {
        m_root->setText(QDir::toNativeSeparators(m_module->modelsRoot()));
        m_root->setCursorPosition(0); // el principio de la ruta a la vista
    }
    m_unload->setText(DictationModule::unloadText(m_module->unloadMinutes()));
    refreshReplacements();
    refreshPieces();
    refreshRecent();
}

void DictationPanel::refreshStatus()
{
    qint64 missingBytes = 0;
    qint64 downloadingBytes = 0;
    int progress = 0;
    int retryAttempt = 0;
    bool downloading = false;
    bool verifying = false;
    QString pieceError;
    for (const DictationModule::PieceInfo &piece : m_module->pieces()) {
        if (piece.state == State::Missing || piece.state == State::Error) missingBytes += piece.size;
        if (piece.state == State::Error && pieceError.isEmpty()) pieceError = piece.error;
        if (piece.state == State::Downloading) {
            downloading = true;
            progress = piece.progress;
            downloadingBytes = piece.size;
            retryAttempt = qMax(retryAttempt, piece.retryAttempt);
        }
        if (piece.state == State::Verifying) verifying = true;
    }
    const QString sm = QStringLiteral("sm");
    if (!m_module->shortcutError().isEmpty() || m_module->engineState() == DictationModule::EngineState::Error) {
        const QString error = m_module->shortcutError().isEmpty() ? m_module->engineError() : m_module->shortcutError();
        m_status->set(QStringLiteral("error"), I18n::tr("Dictation needs attention"), error, QString(), QString(),
                      QStringLiteral("err"), sm);
    } else if (!pieceError.isEmpty()) {
        m_status->set(QStringLiteral("error"), I18n::tr("Download the model to start"), pieceError, I18n::tr("Try again"),
                      QStringLiteral("primary"), QStringLiteral("err"), sm);
    } else if (downloading) {
        // Reintentando (regla de red): el pedido se corto y sale otra vez en unos segundos.
        const QString text = retryAttempt > 0 ? I18n::tr("The download was interrupted. Retrying (%1 of %2)…")
                                                    .arg(retryAttempt)
                                                    .arg(DictationModels::kAttempts)
                                              : I18n::tr("%1% of %2. You can keep working while it downloads.")
                                                    .arg(progress)
                                                    .arg(DiskSpace::formatBytes(downloadingBytes));
        m_status->set(QStringLiteral("on"), I18n::tr("Downloading the model"), text, I18n::tr("Cancel"), QString(),
                      QString(), sm);
    } else if (verifying) {
        m_status->set(QStringLiteral("on"), I18n::tr("Checking the download"),
                      I18n::tr("Making sure the files are complete."), QString(), QString(), QString(), sm);
    } else if (missingBytes > 0) {
        m_status->set(QStringLiteral("warn"), I18n::tr("Download the model to start"),
                      I18n::tr("Dictation runs on this computer. It needs a speech engine and a model, downloaded once."),
                      I18n::tr("Download (%1)").arg(DiskSpace::formatBytes(missingBytes)), QStringLiteral("primary"),
                      QStringLiteral("warn"), sm);
    } else if (m_module->engineState() == DictationModule::EngineState::Loading) {
        m_status->set(QStringLiteral("on"), I18n::tr("Dictation is loading"),
                      I18n::tr("You can start now: what you say waits until the model is loaded."), QString(), QString(),
                      QString(), sm);
    } else {
        QString text = I18n::tr("Press %1 in any text field and speak.").arg(m_module->shortcut().displayText());
        if (m_module->engineState() == DictationModule::EngineState::Unloaded) {
            text += QLatin1Char(' ') + I18n::tr("The model was unloaded to free memory and loads again when you dictate.");
        }
        m_status->set(QStringLiteral("on"), I18n::tr("Dictation is ready"), text, QString(), QString(), QString(), sm);
    }
}

void DictationPanel::refreshReplacements()
{
    const DictationText::Replacements list = m_module->replacements();
    const QString shown = DictationModule::replacementsToJson(list);
    if (shown == m_shownReplacements && m_replacementRows->layout()->count() > 0) {
        return;
    }
    m_shownReplacements = shown;
    auto *rows = static_cast<QVBoxLayout *>(m_replacementRows->layout());
    clearLayout(rows);
    for (auto it = m_deleteSlots.begin(); it != m_deleteSlots.end();) {
        it = it.key().startsWith(QLatin1Char('r')) ? m_deleteSlots.erase(it) : std::next(it);
    }
    if (list.isEmpty()) {
        rows->addWidget(Ui::caption(I18n::tr("No replacements yet."), m_replacementRows));
        return;
    }
    for (int index = 0; index < list.size(); ++index) {
        auto *row = new QWidget(m_replacementRows);
        row->setObjectName(QStringLiteral("replacementRow"));
        auto *line = new QHBoxLayout(row);
        line->setContentsMargins(0, 4, 0, 4);
        line->setSpacing(6);
        auto *from = new ElidedLabel(row);
        from->setObjectName(QStringLiteral("optionLabel"));
        from->setText(list.at(index).first);
        line->addWidget(from, 1, Qt::AlignVCenter);
        line->addWidget(Ui::label(QStringLiteral("→"), "meta", row), 0, Qt::AlignVCenter);
        auto *to = new ElidedLabel(row);
        to->setObjectName(QStringLiteral("optionLabel"));
        to->setText(list.at(index).second);
        line->addWidget(to, 1, Qt::AlignVCenter);
        // Lápiz y tacho contra el borde derecho; «Confirm delete» en su mismo lugar (como Desktop Icons).
        const DeleteSlot actions = deleteSlot(row);
        auto *edit = iconButton(Icon::Pencil, I18n::tr("Edit replacement"), actions.actions);
        actions.normal->addWidget(edit, 0, Qt::AlignVCenter);
        auto *remove = iconButton(Icon::Trash, I18n::tr("Delete replacement"), actions.actions);
        CustomTooltip::instance()->setToolTip(remove, I18n::tr("Delete this replacement. Click again to confirm."));
        actions.normal->addWidget(remove, 0, Qt::AlignVCenter);
        line->addWidget(actions.actions, 0, Qt::AlignVCenter);
        const QString key = QStringLiteral("r%1").arg(index);
        m_deleteSlots.insert(key, actions.slot);
        actions.slot->setCurrentIndex(m_armed == key ? 1 : 0);
        rows->addWidget(row);
        if (m_interactive) {
            connect(edit, &QPushButton::clicked, this, [this, index]() { editReplacement(index); });
            connect(remove, &QPushButton::clicked, this, [this, key]() { armDelete(key); });
            connect(actions.confirm, &QPushButton::clicked, this, [this, index]() {
                runLater([this, index]() {
                    DictationText::Replacements current = m_module->replacements();
                    if (index < current.size()) current.removeAt(index);
                    m_armed.clear();
                    resetReplacementEditor();
                    m_module->setReplacements(current);
                });
            });
        }
    }
}

void DictationPanel::refreshPieces()
{
    const QList<DictationModule::PieceInfo> pieces = m_module->pieces();
    QString shown;
    for (const DictationModule::PieceInfo &piece : pieces) {
        shown += QStringLiteral("%1:%2:%3;").arg(piece.id).arg(int(piece.state)).arg(piece.error);
    }
    if (shown == m_shownPieces) {
        // Solo avanzo el porcentaje: la barra y el número, sin rearmar la fila (Cancel sigue bajo el mouse).
        for (const DictationModule::PieceInfo &piece : pieces) {
            const PieceProgress view = m_progress.value(piece.id);
            if (view.bar) view.bar->set(true, piece.progress / 100.0, -1.0, false);
            if (view.percent && piece.state == State::Downloading) view.percent->setText(downloadingText(piece));
        }
        return;
    }
    m_shownPieces = shown;
    m_progress.clear();
    auto *rows = static_cast<QVBoxLayout *>(m_pieceRows->layout());
    clearLayout(rows);
    for (auto it = m_deleteSlots.begin(); it != m_deleteSlots.end();) {
        it = it.key().startsWith(QLatin1Char('p')) ? m_deleteSlots.erase(it) : std::next(it);
    }
    for (const DictationModule::PieceInfo &piece : pieces) {
        auto *row = new QWidget(m_pieceRows);
        row->setObjectName(QStringLiteral("pieceRow"));
        auto *line = new QHBoxLayout(row);
        line->setContentsMargins(0, 4, 0, 4);
        line->setSpacing(9);
        auto *texts = new QVBoxLayout();
        texts->setSpacing(2);
        texts->addWidget(Ui::label(piece.title, "optionLabel", row));
        auto *about = Ui::caption(I18n::tr("%1 · %2 license").arg(DiskSpace::formatBytes(piece.size), piece.license), row);
        about->setWordWrap(true);
        texts->addWidget(about);
        if (piece.state == State::Error && !piece.error.isEmpty()) {
            auto *error = Ui::caption(piece.error, row);
            error->setTextFormat(Qt::PlainText);
            error->setWordWrap(true);
            Ui::setStyleProperty(error, "tone", QStringLiteral("err"));
            texts->addWidget(error);
        }
        line->addLayout(texts, 1);
        const QString id = piece.id;
        QPushButton *action = nullptr;
        switch (piece.state) {
        case State::Missing:
            action = Ui::button(I18n::tr("Download (%1)").arg(DiskSpace::formatBytes(piece.size)), QString(), QStringLiteral("sm"), row);
            if (m_interactive) connect(action, &QPushButton::clicked, this, [this, id]() { runLater([this, id]() { m_module->download(id); }); });
            break;
        case State::Error:
            action = Ui::button(I18n::tr("Try again"), QString(), QStringLiteral("sm"), row);
            if (m_interactive) connect(action, &QPushButton::clicked, this, [this, id]() { runLater([this, id]() { m_module->download(id); }); });
            break;
        case State::Downloading:
        case State::Verifying: {
            auto *bar = new UsageBar(row);
            bar->setFixedWidth(90);
            bar->setFillColor(Theme::kOk);
            bar->set(true, piece.progress / 100.0, -1.0, false);
            line->addWidget(bar, 0, Qt::AlignVCenter);
            auto *percent = Ui::label(piece.state == State::Verifying ? I18n::trc("dictation", "Checking…") : downloadingText(piece),
                                      "meta", row);
            line->addWidget(percent, 0, Qt::AlignVCenter);
            m_progress.insert(id, {bar, percent});
            if (piece.state == State::Downloading) {
                action = Ui::button(I18n::tr("Cancel"), QStringLiteral("ghost"), QStringLiteral("sm"), row);
                if (m_interactive) connect(action, &QPushButton::clicked, this, [this, id]() { runLater([this, id]() { m_module->cancelDownload(id); }); });
            }
            break;
        }
        case State::Ready: {
            // «Ready» y el tacho contra el borde derecho; «Confirm delete» en su mismo lugar.
            const DeleteSlot actions = deleteSlot(row);
            auto *ready = new Chip(actions.actions);
            ready->set(QStringLiteral("ok"), I18n::tr("Ready"));
            actions.normal->addWidget(ready, 0, Qt::AlignVCenter);
            auto *remove = iconButton(Icon::Trash, I18n::tr("Delete from disk"), actions.actions);
            CustomTooltip::instance()->setToolTip(remove, I18n::tr("Delete it from the disk. Click again to confirm."));
            actions.normal->addWidget(remove, 0, Qt::AlignVCenter);
            const QString key = QStringLiteral("p") + id;
            m_deleteSlots.insert(key, actions.slot);
            actions.slot->setCurrentIndex(m_armed == key ? 1 : 0);
            line->addWidget(actions.actions, 0, Qt::AlignVCenter);
            if (m_interactive) {
                connect(remove, &QPushButton::clicked, this, [this, key]() { armDelete(key); });
                connect(actions.confirm, &QPushButton::clicked, this, [this, id]() {
                    runLater([this, id]() {
                        m_armed.clear();
                        m_module->removePiece(id);
                    });
                });
            }
            break;
        }
        }
        if (action) {
            action->setFocusPolicy(Qt::NoFocus);
            line->addWidget(action, 0, Qt::AlignVCenter);
        }
        rows->addWidget(row);
        Ui::addDivider(rows, m_pieceRows);
    }
}

void DictationPanel::refreshRecent()
{
    const QList<DictationFlow::Entry> history = m_module->history();
    // El historial solo crece por adelante: alcanza con cuántos son y el último.
    const QString shown = history.isEmpty() ? QStringLiteral("0")
                                            : QStringLiteral("%1:%2").arg(history.size()).arg(history.first().when.toMSecsSinceEpoch());
    if (shown == m_shownRecent) {
        return;
    }
    m_shownRecent = shown;
    auto *rows = static_cast<QVBoxLayout *>(m_recentRows->layout());
    clearLayout(rows);
    if (history.isEmpty()) {
        auto *empty = Ui::caption(I18n::tr("Nothing yet. The last 10 dictations show up here until you quit the app; they're never saved to disk."),
                                  m_recentRows);
        empty->setWordWrap(true);
        rows->addWidget(empty);
        return;
    }
    for (int index = 0; index < history.size(); ++index) {
        const DictationFlow::Entry &entry = history.at(index);
        if (index > 0) Ui::addDivider(rows, m_recentRows);
        auto *row = new QWidget(m_recentRows);
        row->setObjectName(QStringLiteral("recentRow"));
        auto *line = new QHBoxLayout(row);
        line->setContentsMargins(0, 2, 0, 2);
        line->setSpacing(9);
        auto *texts = new QVBoxLayout();
        texts->setSpacing(3);
        // Lo dicho es texto del usuario: nunca se interpreta como HTML.
        auto *text = Ui::label(entry.text, "optionLabel", row);
        text->setTextFormat(Qt::PlainText);
        text->setWordWrap(true);
        texts->addWidget(text);
        auto *meta = Ui::caption(QStringLiteral("%1 · %2").arg(entry.when.toString(QStringLiteral("HH:mm")), entry.note), row);
        meta->setTextFormat(Qt::PlainText);
        meta->setWordWrap(true);
        if (!entry.typed) Ui::setStyleProperty(meta, "tone", QStringLiteral("warn"));
        texts->addWidget(meta);
        line->addLayout(texts, 1);
        auto *copy = Ui::button(I18n::tr("Copy"), QString(), QStringLiteral("sm"), row);
        copy->setFocusPolicy(Qt::NoFocus);
        line->addWidget(copy, 0, Qt::AlignTop);
        rows->addWidget(row);
        if (m_interactive) {
            // Al portapapeles SOLO por este click: el dictado en sí nunca lo toca.
            const QString value = entry.text;
            connect(copy, &QPushButton::clicked, this, [value]() { QGuiApplication::clipboard()->setText(value); });
        }
    }
}

QString DictationPanel::microphoneText() const
{
    const QString id = m_module->microphone();
    if (id.isEmpty()) {
        return I18n::tr("Default microphone");
    }
    for (const AudioCapture::Device &device : m_devices) {
        if (device.id == id) {
            // Los nombres de Windows pueden ser largos: el desplegable no crece mas que media pagina.
            return m_microphone->fontMetrics().elidedText(device.name, Qt::ElideRight, 240);
        }
    }
    return I18n::tr("Microphone not connected");
}

// ------------------------------------------------------------------ Acciones

void DictationPanel::showMenu(QPushButton *button, const QList<QPair<QString, QString>> &options, const QString &current,
                              const std::function<void(const QString &)> &choose)
{
    QMenu menu(this);
    // Como un desplegable: tan ancho como el campo, con la opción elegida marcada.
    menu.setMinimumWidth(button->width());
    for (const auto &option : options) {
        QAction *action = menu.addAction(option.second);
        action->setData(option.first);
        action->setCheckable(true);
        action->setChecked(option.first == current);
    }
    // El menú corre su propio ciclo: si la herramienta se apaga mientras está abierto, el panel ya no existe.
    QPointer<DictationPanel> safe(this);
    QAction *chosen = menu.exec(button->mapToGlobal(QPoint(0, button->height() + 2)));
    if (safe && chosen) {
        choose(chosen->data().toString());
    }
}

void DictationPanel::saveWords()
{
    const QString typed = DictationModule::vocabularyFrom(m_words->toPlainText()).join(QStringLiteral(", "));
    if (typed != m_module->customWords()) {
        m_module->setCustomWords(m_words->toPlainText());
    }
    if (m_words->toPlainText() != m_module->customWords()) m_words->setPlainText(m_module->customWords());
}

void DictationPanel::saveRoot()
{
    // Solo una ruta completa (unidad + barra o UNC): "\models" o "C:models" dependen de la carpeta actual.
    const QString typed = m_root->text().trimmed();
    const bool valid = typed.isEmpty() || DictationModule::fullPath(typed);
    if (valid && !typed.isEmpty()) {
        m_module->setModelsRoot(typed);
    }
    m_rootError->setVisible(!valid);
    m_root->setText(QDir::toNativeSeparators(m_module->modelsRoot()));
}

void DictationPanel::browseRoot()
{
    QPointer<DictationPanel> safe(this);
    const QString picked = QFileDialog::getExistingDirectory(this, I18n::tr("Choose a folder"), m_module->modelsRoot());
    if (!safe || picked.isEmpty()) {
        return;
    }
    m_module->setModelsRoot(picked);
}

void DictationPanel::submitReplacement()
{
    const QString from = m_from->text().simplified();
    const QString to = m_to->text().simplified();
    QString error;
    DictationText::Replacements list = m_module->replacements();
    if (from.isEmpty()) {
        error = I18n::tr("Write the text to replace.");
    } else {
        for (int index = 0; index < list.size(); ++index) {
            if (index != m_editing && list.at(index).first.compare(from, Qt::CaseInsensitive) == 0) {
                error = I18n::tr("That text already has a replacement.");
            }
        }
    }
    m_replacementError->setText(error);
    m_replacementError->setVisible(!error.isEmpty());
    if (!error.isEmpty()) {
        return;
    }
    if (m_editing >= 0 && m_editing < list.size()) {
        list[m_editing] = {from, to};
    } else {
        list.append({from, to});
    }
    m_from->clearFocus();
    m_to->clearFocus();
    resetReplacementEditor();
    m_module->setReplacements(list);
}

void DictationPanel::finishReplacementField(QLineEdit *field)
{
    if (!m_from->text().trimmed().isEmpty() && !m_to->text().trimmed().isEmpty()) {
        submitReplacement();
    } else {
        field->clearFocus();
    }
}

void DictationPanel::editReplacement(int index)
{
    const DictationText::Replacements list = m_module->replacements();
    if (index < 0 || index >= list.size()) {
        return;
    }
    armDelete(QString());
    m_editing = index;
    m_from->setText(list.at(index).first);
    m_to->setText(list.at(index).second);
    m_addReplacement->setText(I18n::tr("Save"));
    m_cancelReplacement->show();
    m_replacementError->hide();
}

void DictationPanel::resetReplacementEditor()
{
    m_editing = -1;
    m_from->clear();
    m_to->clear();
    m_addReplacement->setText(I18n::tr("Add"));
    m_cancelReplacement->hide();
    m_replacementError->hide();
}

void DictationPanel::showReplacementEditorForCapture()
{
    editReplacement(1);
}

void DictationPanel::armDelete(const QString &key)
{
    m_armed = key;
    for (auto it = m_deleteSlots.cbegin(); it != m_deleteSlots.cend(); ++it) {
        it.value()->setCurrentIndex(it.key() == key ? 1 : 0);
    }
}

void DictationPanel::runLater(const std::function<void()> &action)
{
    // El botón que se apretó se borra al rearmar la lista: la acción corre cuando su click ya terminó.
    QTimer::singleShot(0, this, action);
}

// ------------------------------------------------------------------ Eventos

void DictationPanel::showEvent(QShowEvent *event)
{
    // Los micrófonos y el disco se leen al mostrar la página (no se vigilan).
    if (m_interactive) {
        m_devices = m_module->devices();
        m_module->refreshModels();
        m_microphone->setText(microphoneText());
    }
    QWidget::showEvent(event);
}

void DictationPanel::hideEvent(QHideEvent *event)
{
    armDelete(QString());
    QWidget::hideEvent(event);
}

bool DictationPanel::eventFilter(QObject *watched, QEvent *event)
{
    // Escape en un campo descarta lo escrito y lo suelta (regla de la app).
    if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        if (watched == m_words) {
            m_words->setPlainText(m_module->customWords());
            m_words->clearFocus();
            return true;
        }
        if (watched == m_root) {
            m_root->setText(QDir::toNativeSeparators(m_module->modelsRoot()));
            m_rootError->hide();
            m_root->clearFocus();
            return true;
        }
        if (watched == m_from || watched == m_to) {
            resetReplacementEditor();
            static_cast<QWidget *>(watched)->clearFocus();
            return true;
        }
    }
    // Las palabras: Enter suelta el campo y al soltarlo se guardan (también con un click afuera o Tab).
    if (watched == m_words && event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent *>(event)->key();
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            m_words->clearFocus();
            return true;
        }
    }
    // Un popup (el menú del click derecho del campo) también manda FocusOut, pero el foco no se va: no se guarda.
    if (watched == m_words && event->type() == QEvent::FocusOut && m_interactive
        && static_cast<QFocusEvent *>(event)->reason() != Qt::PopupFocusReason) {
        saveWords();
    }
    if (event->type() == QEvent::MouseButtonPress) {
        // Un click fuera del campo que tiene el teclado lo suelta: las palabras y la carpeta se guardan
        // (editingFinished). Un reemplazo con los dos campos escritos se agrega (o se guarda, si se editaba);
        // a medias queda escrito sin agregarse. Pasar de un campo del reemplazo al otro no cuenta.
        QWidget *focused = QApplication::focusWidget();
        auto *target = qobject_cast<QWidget *>(watched);
        if (focused && target && focused != target && !focused->isAncestorOf(target)
            && (focused == m_words || focused == m_root || focused == m_from || focused == m_to)) {
            const bool replacement = focused == m_from || focused == m_to;
            const bool otherField = target == m_from || target == m_to || m_from->isAncestorOf(target) || m_to->isAncestorOf(target);
            const bool ownButton = target == m_addReplacement || target == m_cancelReplacement;
            if (replacement && !otherField && !ownButton && !m_from->text().trimmed().isEmpty() && !m_to->text().trimmed().isEmpty()) {
                runLater([this]() { submitReplacement(); });
            }
            focused->clearFocus();
        }
        // Un click fuera de «Confirm delete» lo desarma.
        if (!m_armed.isEmpty() && target && isAncestorOf(target) && !qobject_cast<QPushButton *>(target)) {
            armDelete(QString());
        }
    }
    return QWidget::eventFilter(watched, event);
}
