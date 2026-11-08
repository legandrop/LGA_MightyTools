#include "dev/PaletteEditor.h"

#include "dev/LgaColorPickerDialog.h"
#include "modules/diskspace/DiskReminderToast.h"
#include "ui/CustomTooltip.h"
#include "ui/Palette.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QStandardPaths>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <functional>

namespace {

// Colores fijos del editor (no pasan por la paleta).
const char *kEditorSheet = R"(
QWidget#paletteEditor { background: #141417; }
QWidget#paletteEditor QLabel { color: #C9C8CE; font-size: 12px; background: transparent; }
QLabel[r="head"] { color: #F0EEE8; font-size: 15px; font-weight: 700; }
QLabel[r="role"] { color: #E6E4DE; font-size: 13px; }
QLabel[r="hex"] { color: #8E8D95; font-size: 12px; font-family: 'Consolas'; }
QLabel[r="member"] { color: #A9A8AF; font-size: 12px; font-family: 'Consolas'; }
QLabel[r="status"] { color: #8E8D95; font-size: 12px; }
QLabel[r="status"][dirty="1"] { color: #F2CB3F; }
QFrame[r="row"] { background: #1C1C20; border: 1px solid #28282E; border-radius: 6px; }
QWidget#paletteEditor QPushButton { background: #2A2A31; border: 1px solid #3A3A43; border-radius: 5px; color: #E6E4DE;
    padding: 0 10px; min-height: 24px; max-height: 24px; font-size: 12px; font-weight: 400; }
QWidget#paletteEditor QPushButton:hover { background: #34343C; }
QWidget#paletteEditor QPushButton[k="flat"] { background: transparent; border: 0; color: #9D8FE0; padding: 0 4px; }
QWidget#paletteEditor QPushButton[k="flat"]:hover { background: transparent; color: #C5BBF4; }
QWidget#paletteEditor QLineEdit, QWidget#paletteEditor QComboBox { background: #101013; border: 1px solid #34343C;
    border-radius: 5px; color: #E6E4DE; padding: 0 8px; min-height: 24px; font-size: 12px; }
QWidget#paletteEditor QComboBox QAbstractItemView { background: #1C1C20; color: #E6E4DE; selection-background-color: #2E2A50; }
QWidget#paletteEditor QCheckBox { color: #E6E4DE; font-size: 12px; spacing: 6px; }
QWidget#paletteEditor QCheckBox::indicator { width: 14px; height: 14px; border: 1px solid #55555F; border-radius: 3px;
    background: #101013; }
QWidget#paletteEditor QCheckBox::indicator:checked { background: #9D8FE0; border: 1px solid #C5BBF4; }
QWidget#paletteEditor QScrollArea { border: 0; background: transparent; }
QWidget#paletteEditor QScrollArea > QWidget > QWidget { background: transparent; }
QWidget#paletteEditor QScrollBar:vertical { width: 8px; background: transparent; }
QWidget#paletteEditor QScrollBar::handle:vertical { background: #3A3A43; border-radius: 3px; min-height: 24px; }
QWidget#paletteEditor QScrollBar::add-line:vertical, QWidget#paletteEditor QScrollBar::sub-line:vertical { height: 0; }
)";

// La rueda no cambia el desplegable (regla de las apps LGA).
class NoWheelCombo final : public QComboBox
{
public:
    using QComboBox::QComboBox;

protected:
    void wheelEvent(QWheelEvent *event) override { event->ignore(); }
};

// Muestra de color: pinta lo que devuelve «color» en cada repintado, asi sigue a la paleta sin avisarle.
class Swatch final : public QWidget
{
public:
    Swatch(std::function<QColor()> color, QSize size, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_color(std::move(color))
    {
        setFixedSize(size);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        connect(Palette::Notifier::instance(), &Palette::Notifier::changed, this, qOverload<>(&QWidget::update));
    }
    std::function<void()> onClick;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor(underMouse() ? "#8E8D95" : "#45454E"), 1));
        painter.setBrush(m_color());
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
    }
    void enterEvent(QEnterEvent *) override { update(); }
    void leaveEvent(QEvent *) override { update(); }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()) && onClick) {
            onClick();
        }
    }

private:
    std::function<QColor()> m_color;
};

QString upperHex(const QColor &color)
{
    return color.name(QColor::HexRgb).toUpper();
}

QString slug(const QString &name)
{
    const QString decomposed = name.normalized(QString::NormalizationForm_D).toLower();
    QString out;
    for (const QChar ch : decomposed) {
        if (ch.category() == QChar::Mark_NonSpacing) {
            continue;
        }
        if ((ch >= QLatin1Char('a') && ch <= QLatin1Char('z')) || (ch >= QLatin1Char('0') && ch <= QLatin1Char('9'))) {
            out += ch;
        } else if (!out.endsWith(QLatin1Char('-'))) {
            out += QLatin1Char('-');
        }
    }
    while (out.endsWith(QLatin1Char('-'))) {
        out.chop(1);
    }
    while (out.startsWith(QLatin1Char('-'))) {
        out.remove(0, 1);
    }
    return out.isEmpty() ? QStringLiteral("paleta") : out;
}

QStringList membersOf(const QString &roleId)
{
    const Palette::Role *role = Palette::role(roleId);
    return role ? role->members : QStringList();
}

QPushButton *flatButton(const QString &text)
{
    auto *button = new QPushButton(text);
    button->setProperty("k", "flat");
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

} // namespace

QString PaletteEditor::folderFromEnvironment()
{
    const QString env = qEnvironmentVariable("MIGHTYTOOLS_PALETTE_DIR");
    if (!env.isEmpty()) {
        return QDir::fromNativeSeparators(env);
    }
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/palettes");
}

PaletteEditor::PaletteEditor(const QString &folder, QWidget *parent)
    : QWidget(parent)
    , m_folder(folder)
{
    setObjectName(QStringLiteral("paletteEditor"));
    setWindowTitle(QStringLiteral("LGA Mighty Tools · Paletas (desarrollo)"));
    setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_StyledBackground);
    setStyleSheet(QString::fromLatin1(kEditorSheet));
    QDir().mkpath(m_folder);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 12);
    root->setSpacing(8);
    auto *head = new QLabel(QStringLiteral("Paletas de color"));
    head->setProperty("r", "head");
    root->addWidget(head);

    auto *pickRow = new QHBoxLayout;
    pickRow->setSpacing(6);
    m_list = new NoWheelCombo;
    m_list->setMinimumWidth(220);
    m_list->setFocusPolicy(Qt::ClickFocus);
    pickRow->addWidget(new QLabel(QStringLiteral("Paleta")));
    pickRow->addWidget(m_list, 1);
    auto *folderButton = new QPushButton(QStringLiteral("Carpeta"));
    CustomTooltip::instance()->setToolTip(folderButton, QStringLiteral("Abre la carpeta donde se guardan los .json"));
    pickRow->addWidget(folderButton);
    root->addLayout(pickRow);

    auto *saveRow = new QHBoxLayout;
    saveRow->setSpacing(6);
    m_name = new QLineEdit;
    m_name->setPlaceholderText(QStringLiteral("Nombre de la paleta"));
    m_liked = new QCheckBox(QStringLiteral("Me gusta"));
    auto *saveButton = new QPushButton(QStringLiteral("Guardar"));
    CustomTooltip::instance()->setToolTip(saveButton,
                                          QStringLiteral("Guarda con este nombre.<br>Un nombre que ya existe se reemplaza."));
    saveRow->addWidget(new QLabel(QStringLiteral("Nombre")));
    saveRow->addWidget(m_name, 1);
    saveRow->addWidget(m_liked);
    saveRow->addWidget(saveButton);
    root->addLayout(saveRow);

    auto *toolRow = new QHBoxLayout;
    toolRow->setSpacing(6);
    m_status = new QLabel;
    m_status->setProperty("r", "status");
    toolRow->addWidget(m_status, 1);
    auto *revert = new QPushButton(QStringLiteral("Descartar cambios"));
    auto *sample = new QPushButton(QStringLiteral("Aviso de ejemplo"));
    CustomTooltip::instance()->setToolTip(sample,
                                          QStringLiteral("Muestra el aviso de disco lleno para ver sus colores.<br>No guarda nada."));
    toolRow->addWidget(revert);
    toolRow->addWidget(sample);
    root->addLayout(toolRow);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *inner = new QWidget;
    m_rows = new QVBoxLayout(inner);
    m_rows->setContentsMargins(0, 2, 4, 2);
    m_rows->setSpacing(5);
    scroll->setWidget(inner);
    root->addWidget(scroll, 1);
    buildRows();

    connect(folderButton, &QPushButton::clicked, this, [this]() { QDesktopServices::openUrl(QUrl::fromLocalFile(m_folder)); });
    connect(saveButton, &QPushButton::clicked, this, &PaletteEditor::save);
    connect(m_name, &QLineEdit::returnPressed, this, &PaletteEditor::save);
    connect(revert, &QPushButton::clicked, this, [this]() { loadIndex(m_list->currentIndex()); });
    connect(sample, &QPushButton::clicked, this, &PaletteEditor::showSampleNotice);
    connect(m_list, &QComboBox::activated, this, &PaletteEditor::loadIndex);
    connect(m_liked, &QCheckBox::toggled, this, [this]() {
        if (!m_loading) {
            markDirty(true);
        }
    });
    connect(Palette::Notifier::instance(), &Palette::Notifier::changed, this, [this]() {
        if (!m_loading) {
            markDirty(true);
        }
    });

    refreshList(lastPalette());
    loadIndex(m_list->currentIndex());
    resize(470, 760);
    if (const QScreen *screen = QGuiApplication::primaryScreen()) {
        const QRect area = screen->availableGeometry();
        move(area.right() - width() - 24, area.top() + 40);
    }
}

PaletteEditor::~PaletteEditor()
{
    delete m_notice.data();
}

QWidget *PaletteEditor::sampleNotice() const
{
    return m_notice.data();
}

void PaletteEditor::closeEvent(QCloseEvent *event)
{
    if (m_picker) {
        m_picker->close();
    }
    if (m_notice) {
        m_notice->close();
    }
    QWidget::closeEvent(event);
    if (onClosed) {
        onClosed();
    }
}

QString PaletteEditor::lastPalette() const
{
    return QSettings(QDir(m_folder).filePath(QStringLiteral("_editor.ini")), QSettings::IniFormat)
        .value(QStringLiteral("editor/palette"))
        .toString();
}

void PaletteEditor::rememberPalette(const QString &path) const
{
    QSettings(QDir(m_folder).filePath(QStringLiteral("_editor.ini")), QSettings::IniFormat)
        .setValue(QStringLiteral("editor/palette"), path);
}

void PaletteEditor::buildRows()
{
    for (const auto &role : Palette::roles()) {
        const QString id = QString::fromLatin1(role.id);
        auto *row = new QFrame;
        row->setProperty("r", "row");
        auto *rowLayout = new QVBoxLayout(row);
        rowLayout->setContentsMargins(8, 6, 8, 6);
        rowLayout->setSpacing(4);
        auto *line = new QHBoxLayout;
        line->setSpacing(8);
        auto *swatch = new Swatch([id]() { return Palette::base(id); }, QSize(34, 22));
        swatch->onClick = [this, id]() { pick(id, {}); };
        auto *label = new QLabel(QString::fromUtf8(role.label));
        label->setProperty("r", "role");
        label->setWordWrap(true);
        auto *hex = new QLabel;
        hex->setProperty("r", "hex");
        const auto updateHex = [hex, id]() { hex->setText(upperHex(Palette::base(id))); };
        updateHex();
        connect(Palette::Notifier::instance(), &Palette::Notifier::changed, hex, updateHex);
        line->addWidget(swatch);
        line->addWidget(label, 1);
        line->addWidget(hex);
        rowLayout->addLayout(line);

        const QStringList members = membersOf(id);
        if (members.size() > 1 || (members.size() == 1 && members.first().compare(QLatin1String(role.base), Qt::CaseInsensitive) != 0)) {
            QPushButton *toggle = flatButton(QStringLiteral("Tonos (%1)").arg(members.size()));
            line->addWidget(toggle);
            auto *box = new QWidget;
            auto *boxLayout = new QVBoxLayout(box);
            boxLayout->setContentsMargins(42, 2, 0, 2);
            boxLayout->setSpacing(3);
            for (const QString &member : members) {
                auto *memberLine = new QHBoxLayout;
                memberLine->setSpacing(8);
                auto *memberSwatch = new Swatch([id, member]() { return Palette::mapIn(id, member); }, QSize(26, 16));
                memberSwatch->onClick = [this, id, member]() { pick(id, member); };
                auto *memberText = new QLabel;
                memberText->setProperty("r", "member");
                QPushButton *reset = flatButton(QStringLiteral("Quitar ajuste"));
                const QString key = id + QLatin1Char('/') + member.toUpper();
                const auto update = [memberText, reset, id, member, key]() {
                    const bool fixed = Palette::current().overrides.contains(key);
                    memberText->setText(QStringLiteral("%1  →  %2%3").arg(member.toUpper(), upperHex(Palette::mapIn(id, member)),
                                                                        fixed ? QStringLiteral("  (ajustado)") : QString()));
                    reset->setVisible(fixed);
                };
                update();
                connect(Palette::Notifier::instance(), &Palette::Notifier::changed, memberText, update);
                connect(reset, &QPushButton::clicked, this, [id, member]() { Palette::setOverride(id, member, QColor()); });
                memberLine->addWidget(memberSwatch);
                memberLine->addWidget(memberText, 1);
                memberLine->addWidget(reset);
                boxLayout->addLayout(memberLine);
            }
            box->hide();
            rowLayout->addWidget(box);
            connect(toggle, &QPushButton::clicked, box, [box]() { box->setVisible(!box->isVisible()); });
        }
        m_rows->addWidget(row);
    }
    m_rows->addStretch();
}

void PaletteEditor::refreshList(const QString &select)
{
    m_list->clear();
    m_list->addItem(Palette::factory().name, QString());
    struct Entry
    {
        QString name;
        QString path;
        bool liked;
    };
    QList<Entry> entries;
    const QFileInfoList files = QDir(m_folder).entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QFileInfo &file : files) {
        Palette::Data data;
        if (!Palette::load(file.absoluteFilePath(), &data)) {
            continue;
        }
        entries.push_back({data.name.isEmpty() ? file.completeBaseName() : data.name, file.absoluteFilePath(), data.liked});
    }
    // Las que gustan primero.
    std::stable_sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) { return a.liked && !b.liked; });
    for (const auto &entry : entries) {
        m_list->addItem((entry.liked ? QStringLiteral("★ ") : QString()) + entry.name, entry.path);
    }
    const int index = select.isEmpty() ? 0 : m_list->findData(select);
    m_list->setCurrentIndex(qMax(0, index));
}

void PaletteEditor::loadIndex(int index)
{
    const QString path = m_list->itemData(index).toString();
    Palette::Data data = Palette::factory();
    if (!path.isEmpty()) {
        QString error;
        if (!Palette::load(path, &data, &error)) {
            m_status->setText(QStringLiteral("No se pudo leer: ") + error);
            return;
        }
        if (data.name.isEmpty()) {
            data.name = QFileInfo(path).completeBaseName();
        }
    }
    m_loading = true;
    Palette::apply(data);
    m_name->setText(path.isEmpty() ? QString() : data.name);
    m_liked->setChecked(data.liked);
    m_loading = false;
    rememberPalette(path);
    m_status->clear();
    markDirty(false);
}

void PaletteEditor::save()
{
    const QString name = m_name->text().trimmed();
    if (name.isEmpty()) {
        m_status->setText(QStringLiteral("Poné un nombre para guardar."));
        return;
    }
    Palette::Data data = Palette::current();
    data.name = name;
    data.liked = m_liked->isChecked();
    const QString path = QDir(m_folder).filePath(slug(name) + QStringLiteral(".json"));
    QString error;
    if (!Palette::save(path, data, &error)) {
        m_status->setText(QStringLiteral("No se pudo guardar: ") + error);
        return;
    }
    m_loading = true;
    Palette::apply(data);
    m_loading = false;
    refreshList(path);
    rememberPalette(path);
    markDirty(false);
    m_status->setText(QStringLiteral("Guardada: ") + QDir::toNativeSeparators(path));
}

void PaletteEditor::markDirty(bool dirty)
{
    m_status->setProperty("dirty", dirty ? "1" : "0");
    if (dirty) {
        m_status->setText(QStringLiteral("• Cambios sin guardar"));
    } else if (!m_status->text().startsWith(QLatin1String("Guardada"))) {
        m_status->setText(m_list->currentText());
    }
    m_status->style()->unpolish(m_status);
    m_status->style()->polish(m_status);
}

void PaletteEditor::pick(const QString &roleId, const QString &memberHex)
{
    if (m_picker) {
        m_picker->close();
    }
    const Palette::Role *role = Palette::role(roleId);
    if (!role) {
        return;
    }
    const bool member = !memberHex.isEmpty();
    const QColor initial = member ? Palette::mapIn(roleId, memberHex) : Palette::base(roleId);
    const QColor factory = member ? QColor(memberHex) : QColor(QLatin1String(role->base));
    auto *dialog = new LgaColorPickerDialog(initial, factory, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setModal(false);
    dialog->setWindowTitle(member ? QStringLiteral("%1 · tono %2").arg(QString::fromUtf8(role->label), memberHex.toUpper())
                                  : QString::fromUtf8(role->label));
    // Presets: los colores base de la paleta actual, para emparejar grupos.
    QList<QColor> presets;
    for (const char *id : {"desk", "sheet", "card", "border", "title", "ink2", "accent", "ok"}) {
        presets << Palette::base(QString::fromLatin1(id));
    }
    dialog->setPresetColors(presets);
    dialog->setRecentColors(m_recent);
    connect(dialog, &LgaColorPickerDialog::colorChanged, this, [roleId, memberHex, member](const QColor &color) {
        // El reset del selector lleva el tono a su valor de fabrica: eso es quitar el ajuste, no fijarlo.
        if (member) {
            Palette::setOverride(roleId, memberHex, color == QColor(memberHex) ? QColor() : color);
        } else {
            Palette::setBase(roleId, color);
        }
    });
    connect(dialog, &QDialog::finished, this, [this, dialog](int) {
        const QColor color = dialog->selectedColor();
        m_recent.removeAll(color);
        m_recent.prepend(color);
        while (m_recent.size() > 8) {
            m_recent.removeLast();
        }
    });
    m_picker = dialog;
    dialog->show();
    dialog->move(geometry().left() - dialog->width() - 12, geometry().top() + 60);
}

void PaletteEditor::showSampleNotice()
{
    if (m_notice) {
        m_notice->close();
    }
    // Un disco de ejemplo, nunca uno real: el aviso no lee ni escribe nada; sus botones solo lo cierran.
    constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;
    DriveInfo drive;
    drive.root = QStringLiteral("C:/");
    drive.label = QStringLiteral("C:");
    drive.name = QStringLiteral("Windows");
    drive.totalBytes = qint64(931 * kGiB);
    drive.freeBytes = qint64(38 * kGiB);
    DiskWatch watch;
    watch.root = drive.root;
    watch.value = 50;
    watch.unit = DiskWatch::Unit::GB;
    auto *notice = new DiskReminderToast(drive, watch, 60);
    notice->setAttribute(Qt::WA_DeleteOnClose);
    connect(notice, &DiskReminderToast::reminderRequested, notice, &QWidget::close);
    connect(notice, &DiskReminderToast::cleanupRequested, notice, &QWidget::close);
    connect(notice, &DiskReminderToast::settingsRequested, notice, &QWidget::close);
    m_notice = notice;
    if (const QScreen *screen = QGuiApplication::primaryScreen()) {
        const QRect area = screen->availableGeometry();
        notice->move(area.right() - notice->width() - 15, area.bottom() - notice->height() - 15);
    }
    notice->show();
}
