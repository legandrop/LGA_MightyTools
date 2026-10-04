#include "app/GeneralPage.h"

#include "app/ModuleHost.h"
#include "app/WindowParts.h"
#include "core/I18n.h"
#include "core/UiScale.h"
#include "platform/AutoStart.h"
#include "ui/CustomTooltip.h"
#include "ui/Theme.h"
#include "ui/UiWidgets.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QGuiApplication>
#include <QScreen>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

// Sangria de lo que va debajo de un checkbox: indicador (16) + spacing (10).
constexpr int kCheckIndent = 26;

// Encabezado de tarjeta (`.head`): titulo de 22 de alto, lo de la derecha y 8 de aire abajo.
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

QString countWord(int count)
{
    // Una clave por palabra (contexto "count"): el espanol concuerda con "herramientas" (Dos, Tres...).
    switch (count) {
    case 2: return I18n::trc("count", "Two");
    case 3: return I18n::trc("count", "Three");
    case 4: return I18n::trc("count", "Four");
    case 5: return I18n::trc("count", "Five");
    case 6: return I18n::trc("count", "Six");
    case 7: return I18n::trc("count", "Seven");
    case 8: return I18n::trc("count", "Eight");
    case 9: return I18n::trc("count", "Nine");
    default: return QString::number(count);
    }
}

} // namespace

GeneralPage::GeneralPage(ModuleHost *host, QWidget *parent)
    : QWidget(parent)
    , m_host(host)
{
    setObjectName(QStringLiteral("generalPage"));
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(10);

    const QString description = I18n::tr("Settings for the whole app and an overview of your tools.");
    m_header = new ModuleHeader(
        QStringLiteral("General"), QString(), description,
        [](QPainter &p, const QRectF &r, const QColor &c) { Icons::paint(p, Icon::General, r, c); }, false, this);
    column->addWidget(m_header);

    buildWelcome();
    column->addWidget(m_welcome);
    column->addWidget(buildAppCard());
    m_toolsCard = buildToolsCard();
    column->addWidget(m_toolsCard);
    m_aboutCard = buildAboutCard();
    column->addWidget(m_aboutCard);

    setFirstRun(false);
    refreshTools();
    setUpdateState(UpdateRowState());
}

void GeneralPage::buildWelcome()
{
    // Canvas, seccion 7: la bienvenida con una tarjeta por herramienta, todas apagadas.
    m_welcome = Ui::card(this);
    auto *layout = new QVBoxLayout(m_welcome);
    layout->setContentsMargins(18, 18, 18, 16);
    layout->setSpacing(8);
    layout->addWidget(Ui::label(QStringLiteral("LGA Mighty Tools"), "welcomeTitle", m_welcome));
    const int count = int(m_host->descriptors().size());
    // Dos claves completas (una por forma): el espanol concuerda numero y genero con "herramientas".
    const QString intro = count == 1
        ? I18n::tr("One small tool in one app. Turn on the ones you want: a tool that is off isn't loaded and uses no memory or CPU.")
        : I18n::tr("%1 small tools in one app. Turn on the ones you want: a tool that is off isn't loaded and uses no memory or CPU.")
              .arg(countWord(count));
    layout->addWidget(Ui::caption(intro, m_welcome));
    layout->addSpacing(8);
    auto *grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(8);
    int index = 0;
    for (const ModuleDescriptor &d : m_host->descriptors()) {
        QFrame *mini = new QFrame(m_welcome);
        mini->setObjectName(QStringLiteral("miniCard"));
        auto *miniLayout = new QVBoxLayout(mini);
        miniLayout->setContentsMargins(12, 10, 12, 10);
        miniLayout->setSpacing(4);
        auto *row = new QHBoxLayout();
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(8);
        auto *rowStrut = new QWidget(mini); // `.row` de 22 de alto
        rowStrut->setFixedSize(0, 22);
        row->addWidget(rowStrut);
        auto *icon = new ToolIcon(d.paintIcon, 16, mini);
        icon->setColor(Theme::color(Theme::kTextMuted));
        row->addWidget(icon, 0, Qt::AlignVCenter);
        row->addWidget(Ui::label(d.title, "miniTitle", mini), 1, Qt::AlignVCenter);
        auto *toggle = new ToggleSwitch(mini);
        toggle->setProperty("moduleId", d.id);
        row->addWidget(toggle, 0, Qt::AlignVCenter);
        const QString id = d.id;
        connect(toggle, &QAbstractButton::clicked, this, [this, id](bool on) { emit toolToggleRequested(id, on); });
        m_welcomeSwitches.append(toggle);
        miniLayout->addLayout(row);
        miniLayout->addWidget(Ui::caption(d.description, mini));
        miniLayout->addStretch(1);
        grid->addWidget(mini, index / 2, index % 2);
        ++index;
    }
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    layout->addLayout(grid);
}

QFrame *GeneralPage::buildAppCard()
{
    QFrame *card = Ui::card(this);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(0);
    addHead(layout, QStringLiteral("App"), card);

    // El checkbox queda habilitado tambien desde una salida de desarrollo: el click EXPLICITO del
    // usuario se respeta siempre; lo prohibido es la escritura automatica (ver AutoStart.h).
    m_autoStart = new QCheckBox(AutoStart::checkboxText(), card);
    // Del ancho de su texto: asi la flecha del tooltip le apunta a la casilla y no al medio de la tarjeta.
    m_autoStart->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    layout->addWidget(m_autoStart);
    m_firstRunCaption = Ui::caption(I18n::tr("Turns on by itself with the first tool you turn on."), card);
    m_firstRunCaption->setContentsMargins(kCheckIndent, 3, 0, 0);
    layout->addWidget(m_firstRunCaption);
    connect(m_autoStart, &QCheckBox::clicked, this, &GeneralPage::autoStartToggled);

    m_checkUpdates = new QCheckBox(I18n::tr("Check for updates at startup"), card);
    m_updateResult = Ui::label(QString(), "meta", card);
    m_updateButton = Ui::button(I18n::tr("Check now"), QString(), QStringLiteral("sm"), card);
    m_updateButton->setObjectName(QStringLiteral("checkNowButton"));
    Ui::addDivider(layout, card);
    auto *row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(12);
    row->addWidget(m_checkUpdates, 1);
    row->addWidget(m_updateResult, 0, Qt::AlignVCenter);
    row->addWidget(m_updateButton, 0, Qt::AlignVCenter);
    auto *rowWidget = new QWidget(card);
    rowWidget->setLayout(row);
    rowWidget->setMinimumHeight(26);
    layout->addWidget(rowWidget);
    connect(m_checkUpdates, &QCheckBox::clicked, this, &GeneralPage::checkUpdatesAtStartupToggled);
    connect(m_updateButton, &QPushButton::clicked, this, [this]() {
        if (m_updateState.kind == UpdateRowState::Kind::Available) {
            emit updateRequested();
        } else {
            emit checkNowRequested();
        }
    });

    QLabel *version = Ui::caption(I18n::tr("Installed version: v%1").arg(QStringLiteral(MIGHTYTOOLS_VERSION)), card);
    version->setContentsMargins(kCheckIndent, 3, 0, 0);
    layout->addWidget(version);

    // Idioma y tamano de la interfaz: el mismo desplegable que "Remind me every" de Disk Space, en una
    // grilla para que los dos desplegables arranquen en la misma columna. Cada idioma con su nombre en
    // su idioma. Elegir no cambia nada aca: lo aplica quien arma la ventana.
    Ui::addDivider(layout, card);
    auto *optionsBox = new QWidget(card);
    auto *options = new QGridLayout(optionsBox);
    options->setContentsMargins(0, 0, 0, 0);
    options->setHorizontalSpacing(8);
    options->setVerticalSpacing(4);
    options->setColumnStretch(2, 1);
    options->setRowMinimumHeight(0, 26);
    options->setRowMinimumHeight(1, 26);
    options->addWidget(Ui::label(I18n::tr("Language"), "optionLabel", optionsBox), 0, 0, Qt::AlignVCenter);
    m_languageButton = Ui::button(I18n::nativeName(I18n::language()), QString(), QString(), optionsBox);
    m_languageButton->setObjectName(QStringLiteral("fieldButton"));
    Ui::setDropdownArrow(m_languageButton);
    // AlignAbsolute: el desplegable va en RightToLeft (la flecha a la derecha) y un AlignLeft comun lo
    // daria vuelta, pegandolo al borde derecho de la columna.
    options->addWidget(m_languageButton, 0, 1, Qt::AlignVCenter | Qt::AlignLeft | Qt::AlignAbsolute);
    // Tamano de la interfaz (core/UiScale.h): 0, 1 y 2, como los piensa Lega, en un switch segmentado.
    // Muestra el de esta sesion.
    QStringList levels;
    for (int level = 0; level <= UiScale::kMaxLevel; ++level) {
        levels.append(QString::number(level));
    }
    m_uiSizeSwitch = new SegmentedSwitch(levels, UiScale::sessionLevel(), optionsBox);
    if (!UiScale::supported()) {
        // macOS (D-45): sin tamano de interfaz. El switch existe (lo usan las senales) pero no se muestra.
        m_uiSizeSwitch->hide();
        options->setRowMinimumHeight(1, 0);
        layout->addWidget(optionsBox);
    } else {
        options->addWidget(Ui::label(I18n::tr("Interface size"), "optionLabel", optionsBox), 1, 0, Qt::AlignVCenter);
        options->addWidget(m_uiSizeSwitch, 1, 1, Qt::AlignVCenter | Qt::AlignLeft);
        layout->addWidget(optionsBox);
        // Limite por pantalla (D-35): lo que no entra en la pantalla principal queda apagado. Qt da el area en
        // pixeles logicos de esta sesion; por el factor vuelve a la escala del sistema, la misma que usa main.
        // En una corrida automatizada, la pantalla que eligio la prueba (UiScale::overrideScreenArea).
        QSize unscaledArea;
        if (UiScale::screenAreaOverridden()) {
            unscaledArea = UiScale::overriddenScreenArea();
        } else if (const QScreen *screen = QGuiApplication::primaryScreen()) {
            const qreal f = UiScale::factor(UiScale::sessionLevel());
            const QSize area = screen->availableGeometry().size();
            unscaledArea = QSize(qRound(area.width() * f), qRound(area.height() * f));
        }
        const int maxLevel = UiScale::maxFittingLevel(unscaledArea);
        for (int level = 0; level <= UiScale::kMaxLevel; ++level) {
            m_uiSizeSwitch->setSegmentEnabled(level, level <= maxLevel);
        }
        QLabel *uiSizeCaption = Ui::caption(I18n::tr("1 is the default size. Changing it restarts the app."), card);
        uiSizeCaption->setContentsMargins(0, 3, 0, 0);
        layout->addWidget(uiSizeCaption);
        // Una linea aparte y corta, solo cuando la pantalla limita: dice hasta cual entra.
        if (maxLevel < UiScale::kMaxLevel) {
            const QString limit = maxLevel == 0 ? I18n::tr("Only 0 fits on this screen.")
                                                : I18n::tr("Up to %1 fits on this screen.").arg(maxLevel);
            layout->addWidget(Ui::caption(limit, card));
        }
    }
#ifdef Q_OS_MACOS
    // macOS: el click en la barra de menu abre esta ventana (sin menu, pedido de Lega 2026-10-02), asi que
    // salir de la app vive aca. En Windows sigue en el menu de la bandeja.
    Ui::addDivider(layout, card);
    auto *quitRow = new QHBoxLayout();
    quitRow->setContentsMargins(0, 0, 0, 0);
    quitRow->setSpacing(12);
    auto *quitText = new QVBoxLayout();
    quitText->setContentsMargins(0, 0, 0, 0);
    quitText->setSpacing(2);
    quitText->addWidget(Ui::label(I18n::tr("Quit LGA Mighty Tools"), "optionLabel", card));
    quitText->addWidget(Ui::caption(I18n::tr("Every tool stops until you open the app again."), card));
    quitRow->addLayout(quitText, 1);
    QPushButton *quitButton = Ui::button(I18n::tr("Quit"), QString(), QStringLiteral("sm"), card);
    quitButton->setObjectName(QStringLiteral("quitButton"));
    quitRow->addWidget(quitButton, 0, Qt::AlignVCenter);
    layout->addLayout(quitRow);
    connect(quitButton, &QPushButton::clicked, this, &GeneralPage::quitRequested);
#endif
    connect(m_uiSizeSwitch, &SegmentedSwitch::currentChanged, this, &GeneralPage::uiSizeChangeRequested);
    connect(m_languageButton, &QPushButton::clicked, this, [this]() {
        QMenu menu(this);
        for (const I18n::Language option : {I18n::Language::English, I18n::Language::Spanish}) {
            QAction *action = menu.addAction(I18n::nativeName(option));
            action->setData(I18n::code(option));
            action->setCheckable(true);
            action->setChecked(option == I18n::language());
        }
        QAction *chosen = menu.exec(m_languageButton->mapToGlobal(QPoint(0, m_languageButton->height() + 2)));
        if (chosen && I18n::fromCode(chosen->data().toString()) != I18n::language()) {
            emit languageChangeRequested(chosen->data().toString());
        }
    });
    return card;
}

QFrame *GeneralPage::buildToolsCard()
{
    QFrame *card = Ui::card(this);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(0);
    QHBoxLayout *head = addHead(layout, QStringLiteral("Tools"), card);
    m_toolsChip = new Chip(card);
    head->addWidget(m_toolsChip, 0, Qt::AlignVCenter);

    // Una linea con lo cargado y otra con lo que no: la regla de consumo, a la vista.
    auto addLine = [card, layout](const QString &tone, QLabel *text, int topMargin) {
        auto *row = new QHBoxLayout();
        row->setContentsMargins(0, topMargin, 0, 0);
        row->setSpacing(5);
        auto *dot = new StatusDot(7, card);
        dot->setTone(tone);
        auto *dotBox = new QVBoxLayout();
        dotBox->setContentsMargins(0, 6, 0, 0);
        dotBox->addWidget(dot);
        dotBox->addStretch(1);
        row->addLayout(dotBox);
        text->setWordWrap(true);
        row->addWidget(text, 1, Qt::AlignTop);
        // `.row` de 22 de alto del canvas, con el texto arriba.
        auto *strut = new QWidget(card);
        strut->setFixedSize(0, 22);
        row->addWidget(strut);
        layout->addLayout(row);
    };
    m_running = Ui::label(QString(), "runningText", card);
    m_off = Ui::caption(QString(), card);
    addLine(QStringLiteral("ok"), m_running, 0);
    addLine(QStringLiteral("off"), m_off, 4);
    return card;
}

QFrame *GeneralPage::buildAboutCard()
{
    QFrame *card = Ui::card(this);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(0);
    addHead(layout, QStringLiteral("About"), card);
    auto *row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(5);
    QLabel *name = Ui::label(QStringLiteral("LGA Mighty Tools <span style=\"color:%1;\">v" MIGHTYTOOLS_VERSION "</span>")
                                 .arg(QLatin1String(Theme::kTextStrong)),
                             "aboutName", card);
    name->setTextFormat(Qt::RichText);
    row->addWidget(name, 1, Qt::AlignVCenter);
    QPushButton *help = Ui::button(I18n::tr("Help"), QString(), QStringLiteral("sm"), card);
    help->setObjectName(QStringLiteral("aboutHelp"));
    row->addWidget(help, 0, Qt::AlignVCenter);
    layout->addLayout(row);
    layout->addWidget(Ui::caption(I18n::tr("Developed by Lega Pugliese"), card));
    layout->addWidget(new LinkLabel(QStringLiteral("lega.com.ar"), QStringLiteral("https://lega.com.ar"), card));
    connect(help, &QPushButton::clicked, this, &GeneralPage::helpRequested);
    return card;
}

void GeneralPage::showSessionUiSize()
{
    m_uiSizeSwitch->setCurrent(UiScale::sessionLevel());
}

void GeneralPage::setFirstRun(bool firstRun)
{
    m_firstRun = firstRun;
    m_header->setVisible(!firstRun);
    m_welcome->setVisible(firstRun);
    m_firstRunCaption->setVisible(firstRun);
    m_toolsCard->setVisible(!firstRun);
    m_aboutCard->setVisible(!firstRun);
}

void GeneralPage::refreshTools()
{
    QStringList running;
    QStringList off;
    for (const ModuleDescriptor &d : m_host->descriptors()) {
        (m_host->isRunning(d.id) ? running : off).append(d.title);
    }
    m_toolsChip->set(QStringLiteral("src"),
                     I18n::tr("%1 of %2 on").arg(running.size()).arg(m_host->descriptors().size()));
    m_running->setText(I18n::tr("Running: %1").arg(running.isEmpty() ? I18n::tr("none") : running.join(QStringLiteral(", "))));
    m_off->setText(I18n::tr("Off, not loaded: %1").arg(off.isEmpty() ? I18n::tr("none") : off.join(QStringLiteral(", "))));
    for (ToggleSwitch *toggle : m_welcomeSwitches) {
        const bool on = m_host->isRunning(toggle->property("moduleId").toString());
        if (toggle->isChecked() != on) {
            toggle->setChecked(on);
        }
    }
}

void GeneralPage::setUpdateState(const UpdateRowState &state)
{
    m_updateState = state;
    QString text;
    QString tone;
    switch (state.kind) {
    case UpdateRowState::Kind::Idle:
        break;
    case UpdateRowState::Kind::Checking:
        text = I18n::tr("Checking…");
        break;
    case UpdateRowState::Kind::Latest:
        text = I18n::tr("v%1 is the latest version").arg(state.version);
        tone = QStringLiteral("ok");
        break;
    case UpdateRowState::Kind::Available:
        text = I18n::tr("v%1 is available").arg(state.version);
        tone = QStringLiteral("warn");
        break;
    }
    m_updateResult->setText(text);
    Ui::setStyleProperty(m_updateResult, "tone", tone);
    m_updateResult->setVisible(!text.isEmpty());
    const bool available = state.kind == UpdateRowState::Kind::Available;
    // Mientras dice "Checking…" no se puede pedir otro chequeo.
    m_updateButton->setEnabled(state.kind != UpdateRowState::Kind::Checking);
    m_updateButton->setText(available ? I18n::tr("Update") : I18n::tr("Check now"));
    Ui::setStyleProperty(m_updateButton, "variant", available ? QStringLiteral("primary") : QString());
}

void GeneralPage::setAutoStart(bool enabled, bool installedCopy)
{
    // Reflejar no es escribir: con las senales bloqueadas no se dispara autoStartToggled.
    m_autoStart->blockSignals(true);
    m_autoStart->setChecked(enabled);
    m_autoStart->blockSignals(false);
#ifdef Q_OS_MACOS
    // La copia instalada no lleva tooltip: la casilla ya dice lo que hace. La de desarrollo, si.
    CustomTooltip::instance()->setToolTip(m_autoStart, installedCopy
                                                           ? QString()
                                                           : I18n::tr("Registers THIS development copy to open when you log in"));
#else
    // La copia instalada no lleva tooltip: la casilla ya dice lo que hace. La de desarrollo, si.
    CustomTooltip::instance()->setToolTip(m_autoStart, installedCopy
                                                           ? QString()
                                                           : I18n::tr("Registers THIS development copy to start when you sign in"));
#endif
}

void GeneralPage::setCheckUpdatesAtStartup(bool check)
{
    m_checkUpdates->blockSignals(true);
    m_checkUpdates->setChecked(check);
    m_checkUpdates->blockSignals(false);
}
