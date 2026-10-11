#include "modules/diskspace/DiskReminderToast.h"

#include "core/I18n.h"
#include "platform/SystemPaths.h"
#include "ui/CustomTooltip.h"
#include "ui/Theme.h"
#include "ui/UiWidgets.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <utility>

namespace {
class ReminderComboBox : public QComboBox
{
public:
    using QComboBox::QComboBox;
protected:
    void paintEvent(QPaintEvent *event) override
    {
        QComboBox::paintEvent(event);
        QPainter painter(this);
        Icons::paint(painter, Icon::ChevronDown, QRectF(width() - 24, 10, 12, 12),
                     Theme::color(Theme::kReminderUnit));
    }
};

class ReminderMenuButton : public QPushButton
{
public:
    using QPushButton::QPushButton;
protected:
    void paintEvent(QPaintEvent *event) override
    {
        QPushButton::paintEvent(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Theme::color(Theme::kReminderHeaderIcon));
        for (int x : {7, 12, 17}) {
            painter.drawEllipse(QPointF(x, 12), 1, 1);
        }
    }
};
} // namespace

DiskReminderToast::DiskReminderToast(DriveInfo drive, DiskWatch watch, int minutes, QWidget *parent)
    : QWidget(parent), m_watch(std::move(watch))
{
    if (!parent) {
        setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_DeleteOnClose);
#ifdef Q_OS_MACOS
        // La app suele estar en bandeja mientras se trabaja en otra ventana.
        setAttribute(Qt::WA_MacAlwaysShowToolWindow);
#endif
    }
    setAttribute(Qt::WA_TranslucentBackground);
    setObjectName(QStringLiteral("diskReminderToast"));
    setFixedWidth(364);
    setFocusPolicy(Qt::NoFocus);
    setFont(Theme::uiFont(14));
#ifdef Q_OS_WIN
    // Offscreen no enumera las fuentes de Windows: usa el mismo archivo que el escritorio real.
    if (QGuiApplication::platformName() == QLatin1String("offscreen")) {
        static const int fontId = QFontDatabase::addApplicationFont(
            QStandardPaths::locate(QStandardPaths::FontsLocation, QStringLiteral("segoeui.ttf")));
        Q_UNUSED(fontId);
    }
    const QString family = QStringLiteral("Segoe UI");
#else
    const QString family = QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
#endif
    setStyleSheet(QStringLiteral(
        "#diskReminderToast QLabel, #diskReminderToast QLineEdit, #diskReminderToast QComboBox, #diskReminderToast QPushButton { background:transparent; border:0; color:%1; font-family:'%2'; font-size:14px; font-weight:400; }"
        "#diskReminderToast QPushButton { min-height:24px; max-height:24px; padding:0; }"
        "#diskReminderToast QLabel#headerTitle { font-size:13px; }"
        "#diskReminderToast QLabel#reminderBody { color:%3; }"
        "#diskReminderToast QLabel#reminderUnit { color:%4; }"
        "#diskReminderToast QLineEdit, #diskReminderToast QComboBox { background:%5; border:1px solid %6; border-radius:5px; padding:0 10px; color:white; }"
        "#diskReminderToast QLineEdit { padding-right:42px; }"
        "#diskReminderToast QLineEdit:focus { border-color:%4; }"
        "#diskReminderToast QComboBox { padding-right:32px; }"
        "#diskReminderToast QComboBox::drop-down { width:32px; border:0; }"
        "#diskReminderToast QComboBox::down-arrow { image:none; width:0; height:0; }"
        "#diskReminderToast QComboBox QAbstractItemView { background:%5; border:1px solid %6; color:%1; font-family:'%2'; font-size:14px; }"
        "#diskReminderToast QPushButton#reminderSave, #diskReminderToast QPushButton#reminderCleanup { min-height:34px; max-height:34px; background:%7; border:1px solid %8; border-radius:4px; padding:0 7px; color:white; }"
        "#diskReminderToast QPushButton:hover { background:%9; }"
        "#diskReminderToast QPushButton:disabled { color:%10; }"
        "#diskReminderToast QLabel#reminderError { color:%11; font-size:12px; }")
        .arg(QLatin1String(Theme::kReminderText), family, QLatin1String(Theme::kReminderBody),
             QLatin1String(Theme::kReminderUnit), QLatin1String(Theme::kBorder),
             QLatin1String(Theme::kReminderFieldBorder), QLatin1String(Theme::kReminderButton),
             QLatin1String(Theme::kReminderButtonBorder), QLatin1String(Theme::kRowHover))
        .arg(QLatin1String(Theme::kTextFaint), QLatin1String(Theme::kError)));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 15, 16, 16);
    layout->setSpacing(0);
    auto *header = new QHBoxLayout;
    header->setSpacing(8);
    auto *smallIcon = new QLabel(this);
    smallIcon->setFixedSize(16, 16);
    const QPixmap appIcon(QStringLiteral(":/icons/LGA_MightyTools.png"));
    const auto iconPixmap = [&appIcon](int size) {
        const qreal dpr = qMax(qreal(2), qApp->devicePixelRatio());
        QPixmap pixmap = appIcon.scaled(QSize(size, size) * dpr, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        pixmap.setDevicePixelRatio(dpr);
        return pixmap;
    };
    smallIcon->setPixmap(iconPixmap(16));
    header->addWidget(smallIcon);
    auto *name = new QLabel(I18n::tr("LGA Mighty Tools"), this);
    name->setObjectName(QStringLiteral("headerTitle"));
    header->addWidget(name, 1);
    auto *more = new ReminderMenuButton(this);
    more->setObjectName(QStringLiteral("reminderMenu"));
    more->setFixedSize(24, 24);
    more->setAccessibleName(I18n::tr("Disk Space settings"));
    CustomTooltip::instance()->setToolTip(more, I18n::tr("Disk Space settings"));
    header->addWidget(more);
    header->addSpacing(6);
    auto *closeButton = new QPushButton(this);
    closeButton->setObjectName(QStringLiteral("reminderClose"));
    closeButton->setFixedSize(18, 24);
    closeButton->setIcon(Icons::icon(Icon::X, Theme::color(Theme::kReminderHeaderIcon)));
    closeButton->setIconSize(QSize(14, 14));
    closeButton->setAccessibleName(I18n::tr("Close"));
    header->addWidget(closeButton);
    layout->addLayout(header);
    layout->addSpacing(14);
    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);
    auto *menu = new QMenu(this);
    menu->addAction(I18n::tr("Disk Space settings"), this, [this] { emit settingsRequested(); });
    connect(more, &QPushButton::clicked, this, [more, menu] {
        menu->popup(more->mapToGlobal(QPoint(0, more->height())));
    });

    auto *summary = new QHBoxLayout;
    summary->setSpacing(15);
    auto *largeIcon = new QLabel(this);
    largeIcon->setFixedSize(60, 60);
    largeIcon->setPixmap(iconPixmap(60));
    auto *iconLayout = new QVBoxLayout;
    iconLayout->setSpacing(0);
    iconLayout->addSpacing(4);
    iconLayout->addWidget(largeIcon);
    iconLayout->addStretch();
    summary->addLayout(iconLayout);
    auto *text = new QVBoxLayout;
    text->setSpacing(0);
    auto *title = new CaptionLabel(I18n::tr("%1 free on %2")
        .arg(DiskSpace::formatBytes(drive.freeBytes), drive.label), this, 20);
    title->setObjectName(QStringLiteral("reminderTitle"));
    text->addWidget(title);
    QString body;
    if (m_watch.unit == DiskWatch::Unit::Percent) {
        body = I18n::tr("Low space on %1 (%2 total). The alert is set to appear below %3 %.")
            .arg(drive.label, DiskSpace::formatBytes(drive.totalBytes), QString::number(m_watch.value));
    } else {
        body = I18n::tr("Low space on %1 (%2 total). The alert is set to appear below %3 GB.")
            .arg(drive.label, DiskSpace::formatBytes(drive.totalBytes), QString::number(m_watch.value));
    }
    auto *description = new CaptionLabel(body, this, 20);
    description->setObjectName(QStringLiteral("reminderBody"));
    text->addWidget(description);
    text->addStretch();
    summary->addLayout(text, 1);
    layout->addLayout(summary);
    layout->addSpacing(21);

    auto *fields = new QHBoxLayout;
    fields->setSpacing(12);
    auto *timeField = new QVBoxLayout;
    timeField->setSpacing(8);
    auto *timeLabel = new QLabel(I18n::tr("Remind me in"), this);
    timeLabel->setFixedHeight(19);
    timeField->addWidget(timeLabel);
    m_minutes = new ReminderComboBox(this);
    m_minutes->setObjectName(QStringLiteral("reminderTime"));
    m_minutes->setFixedSize(160, 33);
    m_minutes->setAccessibleName(timeLabel->text());
    for (int choice : DiskSpace::remindChoices()) {
        m_minutes->addItem(DiskSpace::intervalText(choice), choice);
    }
    m_minutes->setCurrentIndex(qMax(0, m_minutes->findData(minutes)));
    m_minutes->view()->setFocusPolicy(Qt::NoFocus);
    timeField->addWidget(m_minutes);
    fields->addLayout(timeField);
    auto *thresholdField = new QVBoxLayout;
    thresholdField->setSpacing(8);
    auto *thresholdLabel = new QLabel(I18n::tr("Only below"), this);
    thresholdLabel->setFixedHeight(19);
    thresholdField->addWidget(thresholdLabel);
    auto *number = new QWidget(this);
    number->setFixedSize(160, 33);
    m_value = new QLineEdit(QString::number(m_watch.value), number);
    m_value->setObjectName(QStringLiteral("reminderThreshold"));
    m_value->setGeometry(0, 0, 160, 33);
    m_value->setInputMethodHints(Qt::ImhDigitsOnly);
    m_value->setMaxLength(10);
    m_value->setAccessibleName(thresholdLabel->text());
    auto *unitLabel = new QLabel(m_watch.unit == DiskWatch::Unit::GB ? I18n::tr("GB") : I18n::tr("%"), number);
    unitLabel->setObjectName(QStringLiteral("reminderUnit"));
    unitLabel->setGeometry(118, 7, 31, 20);
    unitLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    unitLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    thresholdField->addWidget(number);
    fields->addLayout(thresholdField);
    layout->addLayout(fields);
    m_error = new QLabel(this);
    m_error->setObjectName(QStringLiteral("reminderError"));
    m_error->setWordWrap(true);
    m_error->hide();
    layout->addWidget(m_error);
    layout->addSpacing(17);

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    auto *save = new QPushButton(I18n::tr("Save and remind me"), this);
    save->setObjectName(QStringLiteral("reminderSave"));
    save->setFixedSize(162, 36);
    buttons->addWidget(save);
    auto *cleanup = new QPushButton(I18n::tr("Free up space"), this);
    cleanup->setObjectName(QStringLiteral("reminderCleanup"));
    cleanup->setFixedSize(162, 36);
    cleanup->setEnabled(SystemPaths::cleanupSupported());
    buttons->addWidget(cleanup);
    layout->addLayout(buttons);
    connect(save, &QPushButton::clicked, this, [this] {
        int value = 0;
        if (!thresholdValue(&value)) {
            updateError();
            return;
        }
        emit reminderRequested(m_minutes->currentData().toInt(), value);
    });
    connect(cleanup, &QPushButton::clicked, this, &DiskReminderToast::cleanupRequested);
    connect(m_value, &QLineEdit::textChanged, this, [this] {
        if (!m_error->isHidden()) {
            updateError();
        }
    });
    for (QWidget *widget : findChildren<QWidget *>()) {
        widget->setFocusPolicy(widget == m_value ? Qt::ClickFocus : Qt::NoFocus);
        widget->installEventFilter(this);
    }
    installEventFilter(this);
    adjustSize();
}

bool DiskReminderToast::thresholdValue(int *value) const
{
    static const QRegularExpression digits(QStringLiteral("^[0-9]+$"));
    bool ok = false;
    const int entered = m_value->text().toInt(&ok);
    const int maximum = m_watch.unit == DiskWatch::Unit::GB ? DiskSpace::kMaxGb : DiskSpace::kMaxPercent;
    if (!ok || !digits.match(m_value->text()).hasMatch() || entered < 1 || entered > maximum) {
        return false;
    }
    *value = entered;
    return true;
}

void DiskReminderToast::updateError()
{
    int value = 0;
    const bool valid = thresholdValue(&value);
    const int maximum = m_watch.unit == DiskWatch::Unit::GB ? DiskSpace::kMaxGb : DiskSpace::kMaxPercent;
    m_error->setText(I18n::tr("Enter a whole number between 1 and %1.").arg(maximum));
    m_error->setVisible(!valid);
    adjustSize();
}

bool DiskReminderToast::eventFilter(QObject *object, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress && object != m_value) {
        m_value->clearFocus();
    }
    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab || key->key() == Qt::Key_Return
            || key->key() == Qt::Key_Enter || key->key() == Qt::Key_Escape) {
            if (object == m_value && key->key() == Qt::Key_Escape) {
                m_value->setText(QString::number(m_watch.value));
            }
            m_value->clearFocus();
            return true;
        }
    }
    return QWidget::eventFilter(object, event);
}

void DiskReminderToast::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QLinearGradient gradient(0, 0, width(), height());
    gradient.setColorAt(0, Theme::color(Theme::kReminderGradientStart));
    gradient.setColorAt(1, Theme::color(Theme::kReminderGradientEnd));
    painter.setBrush(gradient);
    painter.setPen(Theme::color(Theme::kReminderBorder));
    painter.drawRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5), 8, 8);
}

void DiskReminderToast::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // El error crece hacia arriba: los botones siguen dentro del area util de la pantalla.
    if (isWindow() && isVisible() && event->oldSize().isValid()) {
        move(x(), y() + event->oldSize().height() - event->size().height());
    }
}
