#include "modules/diskspace/cleanup/CleanupDialogs.h"

#include "core/I18n.h"
#include "ui/UiWidgets.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {

// La caja del canvas (`.dialog`): 20 / 24 de relleno y 12 entre bloques.
QVBoxLayout *dialogLayout(QDialog *dialog, const QString &title, int width)
{
    dialog->setObjectName(QStringLiteral("updateDialog"));
    dialog->setModal(true);
    dialog->setFixedWidth(width);
    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(12);
    auto *titleLabel = new QLabel(title, dialog);
    titleLabel->setObjectName(QStringLiteral("dialogTitle"));
    titleLabel->setWordWrap(true);
    layout->addWidget(titleLabel);
    return layout;
}

// Una linea "nombre ........ peso".
void addLine(QVBoxLayout *layout, QWidget *parent, const QString &name, const QString &size, bool path)
{
    auto *row = new QHBoxLayout();
    row->setSpacing(12);
    if (path) {
        auto *label = new ElidedLabel(parent);
        label->setObjectName(QStringLiteral("dialogPath"));
        label->setElideMode(Qt::ElideMiddle);
        label->setText(name);
        row->addWidget(label, 1);
    } else {
        row->addWidget(Ui::label(name, "dialogLine", parent), 1);
    }
    QLabel *value = Ui::label(size, "dialogValue", parent);
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    row->addWidget(value, 0);
    layout->addLayout(row);
}

} // namespace

namespace CleanupDialogs {

QDialog *confirmCleanup(QWidget *parent, const QString &driveLabel, const QString &total, const QList<QPair<QString, QString>> &lines,
                        const QList<QPair<QString, QString>> &rest, int moreCount, const QString &moreSize, const QString &freeBefore,
                        const QString &freeAfter)
{
    auto *dialog = new QDialog(parent);
    dialog->setWindowTitle(I18n::tr("Clean up"));
    QVBoxLayout *layout = dialogLayout(dialog, I18n::tr("Clean up %1 on %2?").arg(total, driveLabel), 440);
    QLabel *intro = Ui::label(I18n::tr("These are deleted for good, not moved to the Recycle Bin:"), "dialogLine", dialog);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto *list = new QVBoxLayout();
    list->setSpacing(4);
    for (const auto &line : lines) {
        addLine(list, dialog, line.first, line.second, false);
    }
    if (moreCount > 0) {
        // "27 more" es un link: despliega el resto de la lista en el mismo cartel.
        auto *moreRow = new QWidget(dialog);
        auto *moreLayout = new QHBoxLayout(moreRow);
        moreLayout->setContentsMargins(0, 0, 0, 0);
        moreLayout->setSpacing(12);
        auto *link = Ui::button(moreCount == 1 ? I18n::tr("1 more") : I18n::tr("%1 more").arg(moreCount), QString(), QString(), moreRow);
        link->setObjectName(QStringLiteral("linkButton"));
        link->setCursor(Qt::PointingHandCursor);
        moreLayout->addWidget(link, 0);
        moreLayout->addStretch(1);
        QLabel *value = Ui::label(moreSize, "dialogValue", moreRow);
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        moreLayout->addWidget(value, 0);
        list->addWidget(moreRow);
        if (!rest.isEmpty()) {
            QObject::connect(link, &QPushButton::clicked, dialog, [dialog, list, moreRow, rest]() {
                moreRow->hide();
                auto *area = new QScrollArea(dialog);
                area->setObjectName(QStringLiteral("dialogScroll"));
                area->setFrameShape(QFrame::NoFrame);
                area->setWidgetResizable(true);
                area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
                area->setFocusPolicy(Qt::NoFocus);
                auto *inner = new QWidget(area);
                auto *innerLayout = new QVBoxLayout(inner);
                // Lugar para la barra de scroll cuando la lista no entra.
                const bool scrolls = rest.size() > 8;
                innerLayout->setContentsMargins(0, 0, scrolls ? 10 : 0, 0);
                innerLayout->setSpacing(4);
                for (const auto &line : rest) {
                    addLine(innerLayout, inner, line.first, line.second, false);
                }
                area->setWidget(inner);
                // Ocho renglones enteros a la vista; el resto, con la rueda.
                const int pitch = (inner->sizeHint().height() + innerLayout->spacing()) / int(rest.size());
                area->setFixedHeight(int(qMin<qsizetype>(rest.size(), 8)) * pitch - innerLayout->spacing());
                list->addWidget(area);
                dialog->adjustSize();
            });
        }
    }
    layout->addLayout(list);
    auto *note = new CaptionLabel(I18n::tr("Files in use are skipped. %1 goes from %2 to %3 free.").arg(driveLabel, freeBefore, freeAfter),
                                  dialog);
    layout->addWidget(note);

    auto *buttons = new QHBoxLayout();
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(8);
    buttons->addStretch(1);
    auto *cancel = Ui::button(I18n::tr("Cancel"), QString(), QString(), dialog);
    auto *clean = Ui::button(I18n::tr("Clean up"), QStringLiteral("primary"), QString(), dialog);
    buttons->addWidget(cancel);
    buttons->addWidget(clean);
    layout->addLayout(buttons);
    QObject::connect(cancel, &QPushButton::clicked, dialog, &QDialog::reject);
    QObject::connect(clean, &QPushButton::clicked, dialog, &QDialog::accept);
    return dialog;
}

QDialog *confirmDelete(QWidget *parent, const QList<QPair<QString, QString>> &paths, int moreCount, qint64 fileCount,
                       bool offerRecycleBin, DeleteKind kind)
{
    auto *dialog = new QDialog(parent);
    dialog->setWindowTitle(I18n::tr("Delete permanently"));
    const int count = int(paths.size()) + moreCount;
    QString title;
    switch (kind) {
    case DeleteKind::Folders:
        title = count == 1 ? I18n::tr("Delete 1 folder permanently?") : I18n::tr("Delete %1 folders permanently?").arg(count);
        break;
    case DeleteKind::Files:
        title = count == 1 ? I18n::tr("Delete 1 file permanently?") : I18n::tr("Delete %1 files permanently?").arg(count);
        break;
    case DeleteKind::Items:
        title = count == 1 ? I18n::tr("Delete 1 item permanently?") : I18n::tr("Delete %1 items permanently?").arg(count);
        break;
    }
    QVBoxLayout *layout = dialogLayout(dialog, title, 440);
    auto *list = new QVBoxLayout();
    list->setSpacing(4);
    for (const auto &path : paths) {
        addLine(list, dialog, path.first, path.second, true);
    }
    if (moreCount > 0) {
        addLine(list, dialog, moreCount == 1 ? I18n::tr("1 more") : I18n::tr("%1 more").arg(moreCount), QString(), false);
    }
    layout->addLayout(list);
    const QString files = fileCount == 1 ? I18n::tr("1 file.") : I18n::tr("%1 files.").arg(QLocale(QLocale::English).toString(fileCount));
    layout->addWidget(new CaptionLabel(files + QLatin1Char(' ') + I18n::tr("This cannot be undone."), dialog));

    auto *buttons = new QHBoxLayout();
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(8);
    buttons->addStretch(1);
    auto *cancel = Ui::button(I18n::tr("Cancel"), QString(), QString(), dialog);
    buttons->addWidget(cancel);
    QObject::connect(cancel, &QPushButton::clicked, dialog, [dialog]() { dialog->done(Cancel); });
    if (offerRecycleBin) {
        auto *bin = Ui::button(I18n::tr("Move to Recycle Bin"), QString(), QString(), dialog);
        buttons->addWidget(bin);
        QObject::connect(bin, &QPushButton::clicked, dialog, [dialog]() { dialog->done(ToRecycleBin); });
    }
    auto *remove = Ui::button(I18n::trc("button", "Delete"), QStringLiteral("danger"), QString(), dialog);
    buttons->addWidget(remove);
    QObject::connect(remove, &QPushButton::clicked, dialog, [dialog]() { dialog->done(Delete); });
    layout->addLayout(buttons);
    return dialog;
}

QDialog *exportForAi(QWidget *parent, int count, const QString &size)
{
    auto *dialog = new QDialog(parent);
    dialog->setWindowTitle(I18n::tr("Export for AI"));
    QVBoxLayout *layout = dialogLayout(dialog, I18n::tr("Ask an AI before deleting"), 440);
    QLabel *text = Ui::label(
        count == 1 ? I18n::tr("Exports the selected item (%1) with its path, size and dates, and a question ready to send. "
                              "Nothing is sent from here: attach it or paste it into your AI assistant.")
                         .arg(size)
                   : I18n::tr("Exports the %1 selected items (%2) with their paths, sizes and dates, and a question ready to "
                              "send. Nothing is sent from here: attach it or paste it into your AI assistant.")
                         .arg(count)
                         .arg(size),
        "dialogLine", dialog);
    text->setWordWrap(true);
    layout->addWidget(text);
    layout->addWidget(new CaptionLabel(I18n::tr("It includes folder and file names, not their contents."), dialog));

    auto *buttons = new QHBoxLayout();
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(8);
    buttons->addStretch(1);
    auto *cancel = Ui::button(I18n::tr("Cancel"), QString(), QString(), dialog);
    auto *copy = Ui::button(I18n::tr("Copy"), QString(), QString(), dialog);
    auto *save = Ui::button(I18n::tr("Save file..."), QStringLiteral("primary"), QString(), dialog);
    buttons->addWidget(cancel);
    buttons->addWidget(copy);
    buttons->addWidget(save);
    layout->addLayout(buttons);
    QObject::connect(cancel, &QPushButton::clicked, dialog, [dialog]() { dialog->done(ExportCancel); });
    QObject::connect(copy, &QPushButton::clicked, dialog, [dialog]() { dialog->done(ExportCopy); });
    QObject::connect(save, &QPushButton::clicked, dialog, [dialog]() { dialog->done(ExportSave); });
    return dialog;
}

} // namespace CleanupDialogs

// ------------------------------------------------------------------ FolderRuleDialog

FolderRuleDialog::FolderRuleDialog(const QString &startDir, QWidget *parent)
    : QDialog(parent)
    , m_startDir(startDir)
{
    setWindowTitle(I18n::tr("Add a folder rule"));
    QVBoxLayout *layout = dialogLayout(this, I18n::tr("Add a folder rule"), 460);
    layout->addWidget(new CaptionLabel(
        I18n::tr("Tell Disk Space which folders of yours are disposable. They show up under Yours to decide, unticked; "
                 "nothing is deleted until you tick it and press Clean up."),
        this));
    layout->addWidget(Ui::label(I18n::tr("Folder"), "optionLabel", this));
    auto *folderRow = new QHBoxLayout();
    folderRow->setSpacing(8);
    m_folder = new QLineEdit(this);
    m_folder->setObjectName(QStringLiteral("ruleField"));
    // Los campos de texto toman el teclado solo por click (regla de la app).
    m_folder->setFocusPolicy(Qt::ClickFocus);
    folderRow->addWidget(m_folder, 1);
    auto *browse = Ui::button(I18n::tr("Browse..."), QString(), QStringLiteral("sm"), this);
    folderRow->addWidget(browse);
    layout->addLayout(folderRow);

    layout->addWidget(Ui::label(I18n::tr("What is disposable"), "optionLabel", this));
    auto *modeRow = new QHBoxLayout();
    modeRow->setSpacing(0);
    m_whole = Ui::button(I18n::tr("This folder"), QString(), QString(), this);
    m_named = Ui::button(I18n::tr("Folders inside it named"), QString(), QString(), this);
    for (QPushButton *button : {m_whole, m_named}) {
        button->setObjectName(QStringLiteral("segButton"));
        button->setCheckable(true);
        modeRow->addWidget(button);
    }
    m_whole->setProperty("pos", QStringLiteral("left"));
    m_named->setProperty("pos", QStringLiteral("right"));
    m_named->setChecked(true);
    modeRow->addSpacing(8);
    m_match = new QLineEdit(this);
    m_match->setObjectName(QStringLiteral("ruleField"));
    m_match->setFocusPolicy(Qt::ClickFocus);
    m_match->setPlaceholderText(QStringLiteral("build"));
    modeRow->addWidget(m_match, 1);
    layout->addLayout(modeRow);

    m_problem = Ui::label(I18n::tr("That folder cannot be a rule: it is protected, or it is not inside this drive."), "caption", this);
    Ui::setStyleProperty(m_problem, "tone", QStringLiteral("err"));
    m_problem->setWordWrap(true);
    m_problem->hide();
    layout->addWidget(m_problem);

    auto *buttons = new QHBoxLayout();
    buttons->setContentsMargins(0, 4, 0, 0);
    buttons->setSpacing(8);
    buttons->addStretch(1);
    auto *cancel = Ui::button(I18n::tr("Cancel"), QString(), QString(), this);
    m_add = Ui::button(I18n::tr("Add rule"), QStringLiteral("primary"), QString(), this);
    buttons->addWidget(cancel);
    buttons->addWidget(m_add);
    layout->addLayout(buttons);

    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_add, &QPushButton::clicked, this, &QDialog::accept);
    connect(browse, &QPushButton::clicked, this, &FolderRuleDialog::browse);
    connect(m_whole, &QPushButton::clicked, this, [this]() {
        m_whole->setChecked(true);
        m_named->setChecked(false);
        refresh();
    });
    connect(m_named, &QPushButton::clicked, this, [this]() {
        m_named->setChecked(true);
        m_whole->setChecked(false);
        refresh();
    });
    connect(m_folder, &QLineEdit::textChanged, this, &FolderRuleDialog::refresh);
    connect(m_match, &QLineEdit::textChanged, this, &FolderRuleDialog::refresh);
    // Enter en un campo acepta si la regla esta completa (Escape ya cierra el cartel).
    connect(m_folder, &QLineEdit::returnPressed, this, [this]() {
        if (m_add->isEnabled()) {
            accept();
        }
    });
    connect(m_match, &QLineEdit::returnPressed, this, [this]() {
        if (m_add->isEnabled()) {
            accept();
        }
    });
    refresh();
}

void FolderRuleDialog::refresh()
{
    const bool named = m_named->isChecked();
    m_match->setEnabled(named);
    const QString folder = m_folder->text().trimmed();
    const QString match = m_match->text().trimmed();
    // El nombre es un nombre de carpeta, no una ruta.
    const bool matchOk = !named || (!match.isEmpty() && !match.contains(QLatin1Char('/')) && !match.contains(QLatin1Char('\\')));
    const bool exists = !folder.isEmpty() && QFileInfo(folder).isDir();
    const bool allowed = !exists || !m_validator || m_validator(rule());
    m_problem->setVisible(exists && !allowed);
    m_add->setEnabled(exists && matchOk && allowed);
    adjustSize();
}

void FolderRuleDialog::setValidator(const std::function<bool(const Cleanup::FolderRule &)> &validator)
{
    m_validator = validator;
    refresh();
}

void FolderRuleDialog::browse()
{
    const QString start = m_folder->text().trimmed().isEmpty() ? m_startDir : m_folder->text().trimmed();
    const QString picked = QFileDialog::getExistingDirectory(this, I18n::tr("Choose a folder"), start);
    if (!picked.isEmpty()) {
        m_folder->setText(QDir::toNativeSeparators(picked));
    }
}

Cleanup::FolderRule FolderRuleDialog::rule() const
{
    Cleanup::FolderRule rule;
    rule.base = QDir::toNativeSeparators(QDir::cleanPath(m_folder->text().trimmed()));
    rule.match = m_named->isChecked() ? m_match->text().trimmed() : QString();
    return rule;
}

void FolderRuleDialog::setExample(const QString &folder, const QString &match)
{
    m_folder->setText(folder);
    m_match->setText(match);
    // En la captura la carpeta de ejemplo no existe: el boton se muestra como con una regla valida.
    m_add->setEnabled(true);
}
