#include "ui/MainWindow.h"

#include "platform/AutoStart.h"
#include "platform/SystemInput.h"
#include "ui/DiskCard.h"
#include "ui/Theme.h"
#include "ui/TitleBar.h"
#include "ui/UiWidgets.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QDebug>
#include <QEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLayout>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QShowEvent>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif

namespace {

constexpr int kWindowWidth = 440;
// Sangria de lo que va debajo de un checkbox: indicador (16) + spacing (10).
constexpr int kCheckIndent = 26;

QLabel *label(const QString &text, const char *name, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setObjectName(QLatin1String(name));
    return l;
}

QFrame *card(QWidget *parent)
{
    auto *frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("card"));
    return frame;
}

QLabel *caption(const QString &text, QWidget *parent)
{
    auto *l = label(text, "caption", parent);
    l->setWordWrap(true);
    return l;
}

// Linea divisoria entre filas de una tarjeta, con el mismo aire arriba y abajo.
void addDivider(QVBoxLayout *layout, QWidget *parent)
{
    layout->addSpacing(8);
    auto *divider = new QFrame(parent);
    divider->setObjectName(QStringLiteral("divider"));
    layout->addWidget(divider);
    layout->addSpacing(8);
}

} // namespace

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
        QPen border(QColor(m_hasSpot ? "#2f2f2f" : "#3a3a3a"), 1.0, m_hasSpot ? Qt::SolidLine : Qt::DashLine);
        painter.setPen(border);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
        if (m_hasSpot) {
            const QPointF center(inner.left() + m_spot.x() * inner.width(), inner.top() + m_spot.y() * inner.height());
            painter.setPen(QPen(QColor("#DDDBEE"), 1.0));
            painter.setBrush(Theme::color(Theme::kAccent));
            painter.drawEllipse(center, 4.0, 4.0);
        }
    }

private:
    QIcon m_image;
    bool m_hasSpot = false;
    QPointF m_spot;
};

MainWindow::MainWindow(AppState *state, Mode mode, QWidget *parent)
    : QMainWindow(parent)
    , m_state(state)
    , m_mode(mode)
{
    setWindowTitle(QStringLiteral("LGA Mighty Tools"));
    // La barra de titulo la dibuja la app (TitleBar). En Windows, applyNativeFrame() le devuelve a
    // la ventana la sombra y las esquinas del sistema.
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    buildUi();
    if (m_mode == Mode::Normal) {
        m_autoStartCheck->setChecked(AutoStart::isEnabled());
        setAutoStartTooltip(AutoStart::availability().available);
        connectWrites();
    }
    refresh();
}

MainWindow::~MainWindow() = default;

ShortcutRow *MainWindow::shortcutRow(ShortcutAction action) const
{
    return action == ShortcutAction::AddKeyframe ? m_addKeyframeRow : m_frameRow;
}

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("central"));
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_titleBar = new TitleBar(central);
    root->addWidget(m_titleBar);

    auto *content = new QWidget(central);
    content->setObjectName(QStringLiteral("content"));
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(14, 12, 14, 14);
    layout->setSpacing(10);
    root->addWidget(content, 1);

    // ---------- Tarjeta 1: estado ----------
    m_statusCard = card(content);
    auto *statusRow = new QHBoxLayout(m_statusCard);
    statusRow->setContentsMargins(14, 12, 14, 12);
    statusRow->setSpacing(12);
    m_statusDot = label(QString(), "statusDot", m_statusCard);
    statusRow->addWidget(m_statusDot, 0, Qt::AlignVCenter);
    auto *statusTexts = new QVBoxLayout();
    statusTexts->setSpacing(2);
    m_statusTitle = label(QString(), "cardTitle", m_statusCard);
    m_statusCaption = caption(QString(), m_statusCard);
    statusTexts->addWidget(m_statusTitle);
    statusTexts->addWidget(m_statusCaption);
    statusRow->addLayout(statusTexts, 1);
    m_statusButton = Ui::button(QString(), QString(), QString(), m_statusCard);
    m_statusButton->setObjectName(QStringLiteral("statusButton"));
    m_statusButton->setMinimumWidth(78);
    statusRow->addWidget(m_statusButton, 0, Qt::AlignVCenter);
    layout->addWidget(m_statusCard);

    // ---------- Tarjeta 2: atajos ----------
    auto *shortcutsCard = card(content);
    auto *shortcuts = new QVBoxLayout(shortcutsCard);
    shortcuts->setContentsMargins(14, 12, 14, 12);
    shortcuts->setSpacing(0);
    shortcuts->addWidget(label(QStringLiteral("Shortcuts"), "cardTitle", shortcutsCard));
    shortcuts->addSpacing(8);
    m_addKeyframeRow = new ShortcutRow(AppState::actionTitle(ShortcutAction::AddKeyframe),
                                       QStringLiteral("Sets a key on the knob under the pointer."), shortcutsCard);
    shortcuts->addWidget(m_addKeyframeRow);
    addDivider(shortcuts, shortcutsCard);
    m_frameRow = new ShortcutRow(AppState::actionTitle(ShortcutAction::FrameDopeSheet),
                                 QStringLiteral("Selects every key in the Dope Sheet and frames them."), shortcutsCard);
    shortcuts->addWidget(m_frameRow);
    layout->addWidget(shortcutsCard);

    // ---------- Tarjeta 3: punto del Dope Sheet ----------
    auto *spotCard = card(content);
    auto *spot = new QVBoxLayout(spotCard);
    spot->setContentsMargins(14, 12, 14, 12);
    spot->setSpacing(8);
    auto *spotHead = new QHBoxLayout();
    spotHead->setSpacing(6);
    spotHead->addWidget(label(QStringLiteral("Dope Sheet position"), "cardTitle", spotCard), 1);
    m_spotChip = new Chip(spotCard);
    spotHead->addWidget(m_spotChip, 0, Qt::AlignVCenter);
    spot->addLayout(spotHead);
    auto *spotRow = new QHBoxLayout();
    spotRow->setSpacing(12);
    m_spotThumb = new SpotThumbnail(spotCard);
    spotRow->addWidget(m_spotThumb, 0, Qt::AlignVCenter);
    auto *spotTexts = new QVBoxLayout();
    spotTexts->setSpacing(2);
    m_spotValue = label(QString(), "spotValue", spotCard);
    m_spotCaption = caption(QString(), spotCard);
    spotTexts->addWidget(m_spotValue);
    spotTexts->addWidget(m_spotCaption);
    spotRow->addLayout(spotTexts, 1);
    m_calibrateButton = Ui::button(QStringLiteral("Calibrate..."), QString(), QStringLiteral("sm"), spotCard);
    m_calibrateButton->setObjectName(QStringLiteral("calibrateButton"));
    spotRow->addWidget(m_calibrateButton, 0, Qt::AlignVCenter);
    spot->addLayout(spotRow);
    layout->addWidget(spotCard);

    // ---------- Tarjeta 4: espacio en disco ----------
    m_diskCard = new DiskCard(m_state, m_mode == Mode::Normal, content);
    layout->addWidget(m_diskCard);

    // ---------- Tarjeta 5: la app ----------
    auto *appCard = card(content);
    auto *appOptions = new QVBoxLayout(appCard);
    appOptions->setContentsMargins(14, 12, 14, 12);
    appOptions->setSpacing(4);
    // El checkbox queda habilitado tambien desde una salida de desarrollo: el click EXPLICITO del
    // usuario se respeta siempre; lo prohibido es la escritura automatica (primer arranque), que es
    // la que puede pisar o borrar sola la entrada. Ver AutoStart.h.
    m_autoStartCheck = new QCheckBox(AutoStart::checkboxText(), appCard);
    appOptions->addWidget(m_autoStartCheck);

    // Updates: solo en Windows por ahora (el updater baja y lanza el instalador de Inno).
    m_updatesCheck = new QCheckBox(QStringLiteral("Check for updates at startup"), appCard);
    m_checkNowButton = Ui::button(QStringLiteral("Check now"), QString(), QStringLiteral("sm"), appCard);
    m_checkNowButton->setObjectName(QStringLiteral("checkNowButton"));
#ifdef Q_OS_WIN
    addDivider(appOptions, appCard);
    auto *updatesRow = new QHBoxLayout();
    updatesRow->setContentsMargins(0, 0, 0, 0);
    updatesRow->setSpacing(12);
    updatesRow->addWidget(m_updatesCheck, 1);
    updatesRow->addWidget(m_checkNowButton, 0, Qt::AlignVCenter);
    appOptions->addLayout(updatesRow);
#else
    m_updatesCheck->hide();
    m_checkNowButton->hide();
    addDivider(appOptions, appCard);
#endif
    auto *versionCaption = caption(QStringLiteral("Installed version: v" MIGHTYTOOLS_VERSION), appCard);
#ifdef Q_OS_WIN
    versionCaption->setContentsMargins(kCheckIndent, 0, 0, 0);
#endif
    appOptions->addWidget(versionCaption);
    layout->addWidget(appCard);

    setCentralWidget(central);
}

void MainWindow::setShortcutValidator(ShortcutValidator validator)
{
    for (const ShortcutAction action : {ShortcutAction::AddKeyframe, ShortcutAction::FrameDopeSheet}) {
        shortcutRow(action)->setValidator([validator, action](const Shortcut &shortcut) {
            return validator ? validator(action, shortcut) : QString();
        });
    }
}

void MainWindow::connectWrites()
{
    connect(m_titleBar, &TitleBar::helpClicked, this, &MainWindow::helpRequested);
    connect(m_state, &AppState::changed, this, &MainWindow::refresh);
    connect(m_statusButton, &QPushButton::clicked, this, &MainWindow::onStatusButtonClicked);
    connect(m_addKeyframeRow, &ShortcutRow::shortcutRecorded, this,
            [this](const Shortcut &shortcut) { m_state->setShortcut(ShortcutAction::AddKeyframe, shortcut); });
    connect(m_frameRow, &ShortcutRow::shortcutRecorded, this,
            [this](const Shortcut &shortcut) { m_state->setShortcut(ShortcutAction::FrameDopeSheet, shortcut); });
    connect(m_calibrateButton, &QPushButton::clicked, this, &MainWindow::calibrateRequested);
    connect(m_autoStartCheck, &QCheckBox::toggled, this, &MainWindow::onAutoStartToggled);
    connect(m_updatesCheck, &QCheckBox::toggled, m_state, &AppState::setCheckUpdatesAtStartup);
    connect(m_checkNowButton, &QPushButton::clicked, this, &MainWindow::checkUpdatesRequested);
    connect(m_diskCard, &DiskCard::drivesRefreshRequested, this, &MainWindow::driveListRequested);
}

MainWindow::Status MainWindow::currentStatus() const
{
    // En la captura el estado "falta el permiso" se puede dibujar en cualquier plataforma.
    if ((SystemInput::needsAccessibilityPermission() || m_mode == Mode::Capture) && !m_state->accessibilityGranted()) {
        return Status::NeedsPermission;
    }
    if (!m_state->enabled()) {
        return Status::Paused;
    }
    if (m_state->registration(ShortcutAction::AddKeyframe) == AppState::Registration::Failed
        || m_state->registration(ShortcutAction::FrameDopeSheet) == AppState::Registration::Failed) {
        return Status::ShortcutTaken;
    }
    // Activos es activos, este Nuke al frente o no: mientras la ventana esta abierta la que esta al
    // frente es ella, asi que un "esperando a Nuke" se veria siempre (Lega, 2026-09-24).
    return Status::On;
}

void MainWindow::refresh()
{
    const Status status = currentStatus();
    QString dot;
    QString title;
    QString text;
    QString button;
    QString buttonVariant;
    QString cardTone;
    switch (status) {
    case Status::On:
        dot = QStringLiteral("on");
        title = QStringLiteral("Shortcuts are on");
        text = QStringLiteral("Only in Nuke. Other apps keep these keys.");
        button = QStringLiteral("Pause");
        break;
    case Status::Paused:
        dot = QStringLiteral("paused");
        title = QStringLiteral("Shortcuts are paused");
        text = QStringLiteral("Nuke gets these keys as usual.");
        button = QStringLiteral("Resume");
        buttonVariant = QStringLiteral("primary");
        break;
    case Status::ShortcutTaken: {
        const bool addFailed = m_state->registration(ShortcutAction::AddKeyframe) == AppState::Registration::Failed;
        const bool frameFailed =
            m_state->registration(ShortcutAction::FrameDopeSheet) == AppState::Registration::Failed;
        const ShortcutAction failed = addFailed ? ShortcutAction::AddKeyframe : ShortcutAction::FrameDopeSheet;
        dot = QStringLiteral("error");
        title = addFailed && frameFailed ? QStringLiteral("Both shortcuts are off")
                                         : QStringLiteral("%1 is off").arg(AppState::actionTitle(failed));
        text = addFailed && frameFailed
                   ? QStringLiteral("Another app uses them.")
                   : QStringLiteral("Another app uses %1.").arg(m_state->shortcut(failed).displayText());
        button = QStringLiteral("Change");
        cardTone = QStringLiteral("err");
        break;
    }
    case Status::NeedsPermission:
        dot = QStringLiteral("warn");
        title = QStringLiteral("Accessibility access needed");
        text = QStringLiteral("Needed to click and type in Nuke.");
        button = QStringLiteral("Open Settings");
        buttonVariant = QStringLiteral("primary");
        cardTone = QStringLiteral("warn");
        break;
    }
    Ui::setStyleProperty(m_statusDot, "state", dot);
    Ui::setStyleProperty(m_statusCard, "tone", cardTone);
    m_statusTitle->setText(title);
    m_statusCaption->setText(text);
    m_statusButton->setText(button);
    Ui::setStyleProperty(m_statusButton, "variant", buttonVariant);

    // Las filas que estan grabando no se tocan: pisarlas cortaria la grabacion a mitad.
    for (const ShortcutAction action : {ShortcutAction::AddKeyframe, ShortcutAction::FrameDopeSheet}) {
        ShortcutRow *row = shortcutRow(action);
        if (!row->isRecording()) {
            row->setShortcut(m_state->shortcut(action));
        }
    }

    if (m_state->hasDopeSheetSpot()) {
        const QPointF spot = m_state->dopeSheetSpot();
        m_spotChip->set(QStringLiteral("ok"), QStringLiteral("Calibrated"));
        m_spotValue->setText(QStringLiteral("%1% across · %2% down")
                                 .arg(qRound(spot.x() * 100))
                                 .arg(qRound(spot.y() * 100)));
        m_spotCaption->setText(QStringLiteral("Of the Nuke window, so it follows moves and resizes."));
    } else {
        m_spotChip->set(QStringLiteral("warn"), QStringLiteral("Not calibrated"));
        m_spotValue->setText(QStringLiteral("No spot saved yet"));
        m_spotCaption->setText(QStringLiteral("Frame Dope Sheet needs it. Takes one click."));
    }
    m_spotThumb->setSpot(m_state->hasDopeSheetSpot(), m_state->dopeSheetSpot());
    m_diskCard->refresh();

    // Reflejar no es escribir: sin senales, setChecked no llega a AppState.
    m_updatesCheck->blockSignals(true);
    m_updatesCheck->setChecked(m_state->checkUpdatesAtStartup());
    m_updatesCheck->blockSignals(false);
    fitHeight();
}

void MainWindow::onStatusButtonClicked()
{
    switch (currentStatus()) {
    case Status::On:
        m_state->setEnabled(false);
        break;
    case Status::Paused:
        m_state->setEnabled(true);
        break;
    case Status::ShortcutTaken:
        shortcutRow(m_state->registration(ShortcutAction::AddKeyframe) == AppState::Registration::Failed
                        ? ShortcutAction::AddKeyframe
                        : ShortcutAction::FrameDopeSheet)
            ->startRecording();
        break;
    case Status::NeedsPermission:
        emit accessibilityRequested();
        break;
    }
}

void MainWindow::fitHeight()
{
    // Ancho fijo y alto segun el contenido ya pulido: adjustSize() calcula antes de que la hoja de
    // estilo le ponga la fuente a los labels.
    ensurePolished();
    for (QWidget *child : findChildren<QWidget *>()) {
        child->ensurePolished();
    }
    QLayout *rootLayout = centralWidget()->layout();
    rootLayout->invalidate();
    rootLayout->activate();
    const int height = rootLayout->hasHeightForWidth() ? rootLayout->totalHeightForWidth(kWindowWidth)
                                                       : rootLayout->totalSizeHint().height();
    setFixedSize(kWindowWidth, height);
}

void MainWindow::applyAutoStartFixture(bool enabled, bool available)
{
    m_autoStartCheck->setChecked(enabled);
    setAutoStartTooltip(available);
}

void MainWindow::setAutoStartTooltip(bool available)
{
#ifdef Q_OS_MACOS
    m_autoStartCheck->setToolTip(available ? QStringLiteral("Open LGA Mighty Tools when you log in")
                                           : QStringLiteral("Registers THIS development copy to open when you log in"));
#else
    m_autoStartCheck->setToolTip(available ? QStringLiteral("Start LGA Mighty Tools when you sign in to Windows")
                                           : QStringLiteral("Registers THIS development copy to start when you sign in"));
#endif
}

void MainWindow::syncAutoStartCheck()
{
    if (!m_autoStartCheck || m_mode != Mode::Normal) {
        return;
    }
    // Con las senales bloqueadas: reflejar el estado no es activarlo.
    m_autoStartCheck->blockSignals(true);
    m_autoStartCheck->setChecked(AutoStart::isEnabled());
    m_autoStartCheck->blockSignals(false);
}

void MainWindow::onAutoStartToggled(bool checked)
{
    const bool ok = AutoStart::setEnabled(checked);
    // Se loguea lo que QUEDO en el sistema, no solo el resultado de la escritura.
    qInfo() << "[MainWindow] AutoStart" << (checked ? "ON" : "OFF") << "ok:" << ok << "| activo ahora:"
            << AutoStart::isEnabled() << "| valor en Run:"
            << (AutoStart::storedCommand().isEmpty() ? QStringLiteral("(ninguno)") : AutoStart::storedCommand());
    if (!ok) {
        syncAutoStartCheck(); // revertir el visual si fallo
    }
}

void MainWindow::cancelRecordings()
{
    m_addKeyframeRow->cancelRecording();
    m_frameRow->cancelRecording();
}

void MainWindow::changeEvent(QEvent *event)
{
    // Si la ventana pierde el frente mientras una fila graba, la grabacion se corta: si no, la
    // siguiente combinacion que el usuario apriete en OTRA app terminaria guardada como atajo.
    if (event->type() == QEvent::ActivationChange && !isActiveWindow()) {
        cancelRecordings();
        // El umbral de un disco a medio escribir se confirma y el campo se suelta: al volver, el
        // cursor no queda titilando adentro.
        if (QWidget *focused = focusWidget()) {
            focused->clearFocus();
        }
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::showEvent(QShowEvent *event)
{
#ifdef Q_OS_WIN
    // La primera vez que se muestra ya existe el HWND. Nunca en la captura: no hay ventana real.
    if (!m_nativeFrameApplied && m_mode == Mode::Normal && QGuiApplication::platformName() == QLatin1String("windows")) {
        m_nativeFrameApplied = true;
        applyNativeFrame();
    }
#endif
    // Estado real en cada apertura: un cambio hecho por fuera (Task Manager > Startup, Ajustes de
    // macOS, otra copia de la app) tiene que verse sin reiniciar.
    syncAutoStartCheck();
    if (m_mode == Mode::Normal) {
        emit diskReadingsRequested();
    }
    QMainWindow::showEvent(event);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // No se cierra la app: se oculta a la bandeja. Salir solo desde "Quit" del menu.
    cancelRecordings();
    hide();
    event->ignore();
}

#ifdef Q_OS_WIN
void MainWindow::applyNativeFrame()
{
    // Una ventana Qt sin marco es un WS_POPUP: Windows no le da sombra, ni esquinas redondeadas, ni
    // la minimiza desde la barra de tareas. Se le agregan los estilos de una ventana con titulo
    // (WS_CAPTION, WS_SYSMENU, WS_MINIMIZEBOX) y WM_NCCALCSIZE (nativeEvent) deja el area no-cliente
    // en cero: el titulo nativo no se dibuja, pero Windows la sigue tratando como ventana normal.
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    SetWindowLongPtrW(hwnd, GWL_STYLE, style | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX);
    // Un pixel de "marco" metido en el cliente es lo que hace que DWM pinte la sombra.
    const MARGINS margins{0, 0, 1, 0};
    DwmExtendFrameIntoClientArea(hwnd, &margins);
    // Esquinas redondeadas de Windows 11 (DWMWA_WINDOW_CORNER_PREFERENCE = 33, DWMWCP_ROUND = 2).
    const DWORD corners = 2;
    DwmSetWindowAttribute(hwnd, 33, &corners, sizeof(corners));
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
    const MSG *msg = static_cast<const MSG *>(message);
    if (m_nativeFrameApplied && msg->message == WM_NCCALCSIZE && msg->wParam == TRUE) {
        // Todo el rectangulo de la ventana es cliente: la barra de titulo es TitleBar.
        *result = 0;
        return true;
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}
#endif
