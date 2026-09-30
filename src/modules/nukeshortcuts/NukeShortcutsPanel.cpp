#include "modules/nukeshortcuts/NukeShortcutsPanel.h"
#include "core/I18n.h"

#include "ui/ShortcutRow.h"
#include "ui/Theme.h"
#include "ui/UiWidgets.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QVBoxLayout>

// Miniatura del layout de Nuke (la captura DopeSheetPos de la version AutoHotkey) con un punto
// violeta donde quedo guardado el click. Sin punto guardado, la captura va atenuada y con borde
// punteado.
class SpotThumbnail : public QWidget
{
public:
    explicit SpotThumbnail(QWidget *parent)
        : QWidget(parent)
        , m_image(QStringLiteral(":/images/DopeSheetPos.png"))
    {
        setFixedSize(72, 56);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    void setSpot(bool hasSpot, const QPointF &spot)
    {
        m_hasSpot = hasSpot;
        m_spot = spot;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        const QRectF inner = QRectF(rect()).adjusted(1, 1, -1, -1);
        painter.setOpacity(m_hasSpot ? 0.85 : 0.35);
        painter.drawPixmap(inner.toRect(), m_image.pixmap(inner.size().toSize(), devicePixelRatioF()));
        painter.setOpacity(1.0);
        QPen border(Theme::color(m_hasSpot ? Theme::kFieldBorder : Theme::kBorderStrong), 1.0, m_hasSpot ? Qt::SolidLine : Qt::DashLine);
        painter.setPen(border);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
        if (m_hasSpot) {
            const QPointF center(inner.left() + m_spot.x() * inner.width(), inner.top() + m_spot.y() * inner.height());
            painter.setPen(QPen(Theme::color(Theme::kTextOnAccent), 1.0));
            painter.setBrush(Theme::color(Theme::kAccent));
            painter.drawEllipse(center, 4.0, 4.0);
        }
    }

private:
    QIcon m_image;
    bool m_hasSpot = false;
    QPointF m_spot;
};

NukeShortcutsPanel::NukeShortcutsPanel(NukeShortcutsState *state, bool interactive, bool showPermission, QWidget *parent)
    : QWidget(parent)
    , m_state(state)
    , m_showPermission(showPermission)
{
    setObjectName(QStringLiteral("nukeShortcutsPanel"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    // ---------- Tarjeta 1: estado ----------
    m_statusCard = new StatusCard(this);
    layout->addWidget(m_statusCard);

    // ---------- Tarjeta 2: atajos ----------
    QFrame *shortcutsCard = Ui::card(this);
    auto *shortcuts = new QVBoxLayout(shortcutsCard);
    shortcuts->setContentsMargins(14, 12, 14, 12);
    shortcuts->setSpacing(0);
    QLabel *shortcutsTitle = Ui::label(I18n::tr("Shortcuts"), "cardTitle", shortcutsCard);
    shortcutsTitle->setMinimumHeight(22);
    shortcuts->addWidget(shortcutsTitle);
    shortcuts->addSpacing(8);
    m_addKeyframeRow = new ShortcutRow(NukeShortcutsState::actionTitle(ShortcutAction::AddKeyframe),
                                       I18n::tr("Sets a key on the knob under the pointer."), shortcutsCard);
    shortcuts->addWidget(m_addKeyframeRow);
    Ui::addDivider(shortcuts, shortcutsCard);
    m_frameRow = new ShortcutRow(NukeShortcutsState::actionTitle(ShortcutAction::FrameDopeSheet),
                                 I18n::tr("Selects every key in the Dope Sheet and frames them."), shortcutsCard);
    shortcuts->addWidget(m_frameRow);
    layout->addWidget(shortcutsCard);

    // ---------- Tarjeta 3: punto del Dope Sheet ----------
    QFrame *spotCard = Ui::card(this);
    auto *spot = new QVBoxLayout(spotCard);
    spot->setContentsMargins(14, 12, 14, 12);
    spot->setSpacing(8);
    auto *spotHead = new QHBoxLayout();
    spotHead->setSpacing(6);
    QLabel *spotTitle = Ui::label(I18n::tr("Dope Sheet position"), "cardTitle", spotCard);
    spotTitle->setMinimumHeight(22);
    spotHead->addWidget(spotTitle, 1);
    m_spotChip = new Chip(spotCard);
    spotHead->addWidget(m_spotChip, 0, Qt::AlignVCenter);
    spot->addLayout(spotHead);
    auto *spotRow = new QHBoxLayout();
    spotRow->setSpacing(12);
    m_spotThumb = new SpotThumbnail(spotCard);
    spotRow->addWidget(m_spotThumb, 0, Qt::AlignVCenter);
    auto *spotTexts = new QVBoxLayout();
    spotTexts->setSpacing(2);
    m_spotValue = Ui::label(QString(), "spotValue", spotCard);
    m_spotCaption = Ui::caption(QString(), spotCard);
    spotTexts->addWidget(m_spotValue);
    spotTexts->addWidget(m_spotCaption);
    spotRow->addLayout(spotTexts, 1);
    m_calibrateButton = Ui::button(I18n::tr("Calibrate..."), QString(), QStringLiteral("sm"), spotCard);
    m_calibrateButton->setObjectName(QStringLiteral("calibrateButton"));
    spotRow->addWidget(m_calibrateButton, 0, Qt::AlignVCenter);
    spot->addLayout(spotRow);
    layout->addWidget(spotCard);

    if (interactive) {
        connect(m_state, &NukeShortcutsState::changed, this, &NukeShortcutsPanel::refresh);
        connect(m_statusCard->button(), &QPushButton::clicked, this, &NukeShortcutsPanel::onStatusButtonClicked);
        connect(m_addKeyframeRow, &ShortcutRow::shortcutRecorded, this,
                [this](const Shortcut &shortcut) { m_state->setShortcut(ShortcutAction::AddKeyframe, shortcut); });
        connect(m_frameRow, &ShortcutRow::shortcutRecorded, this,
                [this](const Shortcut &shortcut) { m_state->setShortcut(ShortcutAction::FrameDopeSheet, shortcut); });
        connect(m_calibrateButton, &QPushButton::clicked, this, &NukeShortcutsPanel::calibrateRequested);
        // Si la ventana pierde el frente mientras una fila graba, la grabacion se corta: si no, la
        // siguiente combinacion que el usuario apriete en OTRA app terminaria guardada como atajo.
        if (QWidget *top = window()) {
            top->installEventFilter(this);
        }
    }
    refresh();
}

ShortcutRow *NukeShortcutsPanel::shortcutRow(ShortcutAction action) const
{
    return action == ShortcutAction::AddKeyframe ? m_addKeyframeRow : m_frameRow;
}

void NukeShortcutsPanel::setValidator(Validator validator)
{
    for (const ShortcutAction action : {ShortcutAction::AddKeyframe, ShortcutAction::FrameDopeSheet}) {
        shortcutRow(action)->setValidator([validator, action](const Shortcut &shortcut) {
            return validator ? validator(action, shortcut) : QString();
        });
    }
}

bool NukeShortcutsPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::ActivationChange && watched->isWidgetType()
        && !static_cast<QWidget *>(watched)->isActiveWindow()) {
        cancelRecordings();
    }
    return QWidget::eventFilter(watched, event);
}

void NukeShortcutsPanel::hideEvent(QHideEvent *event)
{
    cancelRecordings();
    QWidget::hideEvent(event);
}

void NukeShortcutsPanel::cancelRecordings()
{
    m_addKeyframeRow->cancelRecording();
    m_frameRow->cancelRecording();
}

NukeShortcutsPanel::Status NukeShortcutsPanel::currentStatus() const
{
    if (m_showPermission && !m_state->accessibilityGranted()) {
        return Status::NeedsPermission;
    }
    if (m_state->paused()) {
        return Status::Paused;
    }
    if (m_state->registration(ShortcutAction::AddKeyframe) == NukeShortcutsState::Registration::Failed
        || m_state->registration(ShortcutAction::FrameDopeSheet) == NukeShortcutsState::Registration::Failed) {
        return Status::ShortcutTaken;
    }
    // Activos es activos, este Nuke al frente o no: mientras la ventana esta abierta la que esta al
    // frente es ella, asi que un "esperando a Nuke" se veria siempre (Lega, 2026-09-24).
    return Status::On;
}

void NukeShortcutsPanel::refresh()
{
    switch (currentStatus()) {
    case Status::On:
        m_statusCard->set(QStringLiteral("on"), I18n::tr("Shortcuts are on"),
                          I18n::tr("Only in Nuke. Other apps keep these keys."), I18n::tr("Pause"), QString(),
                          QString());
        break;
    case Status::Paused:
        m_statusCard->set(QStringLiteral("paused"), I18n::tr("Shortcuts are paused"),
                          I18n::tr("Nuke gets these keys as usual."), I18n::tr("Resume"),
                          QStringLiteral("primary"), QString());
        break;
    case Status::ShortcutTaken: {
        const bool addFailed = m_state->registration(ShortcutAction::AddKeyframe) == NukeShortcutsState::Registration::Failed;
        const bool frameFailed =
            m_state->registration(ShortcutAction::FrameDopeSheet) == NukeShortcutsState::Registration::Failed;
        const ShortcutAction failed = addFailed ? ShortcutAction::AddKeyframe : ShortcutAction::FrameDopeSheet;
        QString text;
        if (addFailed && frameFailed) {
            text = I18n::tr("Another app uses them.");
        } else if (!m_state->conflictWith(failed).isEmpty()) {
            // La tomo otra herramienta de Mighty Tools, no otra app.
            text = I18n::tr("Already used by %1.").arg(m_state->conflictWith(failed));
        } else {
            text = I18n::tr("Another app uses %1.").arg(m_state->shortcut(failed).displayText());
        }
        m_statusCard->set(QStringLiteral("error"),
                          addFailed && frameFailed ? I18n::tr("Both shortcuts are off")
                                                   : I18n::trc("shortcut", "%1 is off").arg(NukeShortcutsState::actionTitle(failed)),
                          text, I18n::tr("Change"), QString(), QStringLiteral("err"));
        break;
    }
    case Status::NeedsPermission:
        m_statusCard->set(QStringLiteral("warn"), I18n::tr("Accessibility access needed"),
                          I18n::tr("Needed to click and type in Nuke."), I18n::tr("Open Settings"),
                          QStringLiteral("primary"), QStringLiteral("warn"));
        break;
    }

    // Las filas que estan grabando no se tocan: pisarlas cortaria la grabacion a mitad.
    for (const ShortcutAction action : {ShortcutAction::AddKeyframe, ShortcutAction::FrameDopeSheet}) {
        ShortcutRow *row = shortcutRow(action);
        if (!row->isRecording()) {
            row->setShortcut(m_state->shortcut(action));
        }
    }

    if (m_state->hasDopeSheetSpot()) {
        const QPointF spot = m_state->dopeSheetSpot();
        m_spotChip->set(QStringLiteral("ok"), I18n::tr("Calibrated"));
        m_spotValue->setText(I18n::tr("%1% across · %2% down").arg(qRound(spot.x() * 100)).arg(qRound(spot.y() * 100)));
        m_spotCaption->setText(I18n::tr("Of the Nuke window, so it follows moves and resizes."));
    } else {
        m_spotChip->set(QStringLiteral("warn"), I18n::tr("Not calibrated"));
        m_spotValue->setText(I18n::tr("No spot saved yet"));
        m_spotCaption->setText(I18n::tr("Frame Dope Sheet needs it. Takes one click."));
    }
    m_spotThumb->setSpot(m_state->hasDopeSheetSpot(), m_state->dopeSheetSpot());
}

void NukeShortcutsPanel::onStatusButtonClicked()
{
    switch (currentStatus()) {
    case Status::On:
        m_state->setPaused(true);
        break;
    case Status::Paused:
        m_state->setPaused(false);
        break;
    case Status::ShortcutTaken:
        shortcutRow(m_state->registration(ShortcutAction::AddKeyframe) == NukeShortcutsState::Registration::Failed
                        ? ShortcutAction::AddKeyframe
                        : ShortcutAction::FrameDopeSheet)
            ->startRecording();
        break;
    case Status::NeedsPermission:
        emit accessibilityRequested();
        break;
    }
}
