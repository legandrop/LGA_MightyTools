#include "app/GeneralPage.h"

#include "app/ModuleHost.h"
#include "app/WindowParts.h"
#include "platform/AutoStart.h"
#include "ui/Theme.h"
#include "ui/UiWidgets.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
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
    static const char *const words[] = {"No", "One", "Two", "Three", "Four", "Five", "Six", "Seven", "Eight", "Nine"};
    return count >= 0 && count < 10 ? QString::fromLatin1(words[count]) : QString::number(count);
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

#ifdef Q_OS_MACOS
    const QString description = QStringLiteral("Open at login, updates and version.");
#else
    const QString description = QStringLiteral("Start with Windows, updates and version.");
#endif
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
    layout->addWidget(Ui::label(QStringLiteral("Welcome to LGA Mighty Tools"), "welcomeTitle", m_welcome));
    const int count = int(m_host->descriptors().size());
    const QString tools = count == 1 ? QStringLiteral("One small tool in one app.")
                                     : QStringLiteral("%1 small tools in one app.").arg(countWord(count));
    layout->addWidget(Ui::caption(tools + QStringLiteral(" Turn on the ones you want: a tool that is off isn't loaded "
                                                         "and uses no memory or CPU."),
                                  m_welcome));
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
    layout->addWidget(m_autoStart);
    m_firstRunCaption = Ui::caption(QStringLiteral("Turns on by itself with the first tool you turn on."), card);
    m_firstRunCaption->setContentsMargins(kCheckIndent, 3, 0, 0);
    layout->addWidget(m_firstRunCaption);
    connect(m_autoStart, &QCheckBox::clicked, this, &GeneralPage::autoStartToggled);

    m_checkUpdates = new QCheckBox(QStringLiteral("Check for updates at startup"), card);
    m_updateResult = Ui::label(QString(), "meta", card);
    m_updateButton = Ui::button(QStringLiteral("Check now"), QString(), QStringLiteral("sm"), card);
    m_updateButton->setObjectName(QStringLiteral("checkNowButton"));
#ifdef Q_OS_WIN
    // Updates: solo en Windows por ahora (el updater baja y lanza el instalador de Inno).
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
#else
    m_checkUpdates->hide();
    m_updateResult->hide();
    m_updateButton->hide();
#endif
    connect(m_checkUpdates, &QCheckBox::clicked, this, &GeneralPage::checkUpdatesAtStartupToggled);
    connect(m_updateButton, &QPushButton::clicked, this, [this]() {
        if (m_updateState.kind == UpdateRowState::Kind::Available) {
            emit updateRequested();
        } else {
            emit checkNowRequested();
        }
    });

    QLabel *version = Ui::caption(QStringLiteral("Installed version: v" MIGHTYTOOLS_VERSION), card);
    version->setContentsMargins(kCheckIndent, 3, 0, 0);
    layout->addWidget(version);
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
    QPushButton *help = Ui::button(QStringLiteral("Help"), QString(), QStringLiteral("sm"), card);
    help->setObjectName(QStringLiteral("aboutHelp"));
    row->addWidget(help, 0, Qt::AlignVCenter);
    layout->addLayout(row);
    layout->addWidget(Ui::caption(QStringLiteral("Developed by Lega Pugliese"), card));
    layout->addWidget(new LinkLabel(QStringLiteral("github.com/legandrop"), QStringLiteral("https://github.com/legandrop"), card));
    connect(help, &QPushButton::clicked, this, &GeneralPage::helpRequested);
    return card;
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
                     QStringLiteral("%1 of %2 on").arg(running.size()).arg(m_host->descriptors().size()));
    m_running->setText(QStringLiteral("Running: %1").arg(running.isEmpty() ? QStringLiteral("none") : running.join(QStringLiteral(", "))));
    m_off->setText(QStringLiteral("Off, not loaded: %1").arg(off.isEmpty() ? QStringLiteral("none") : off.join(QStringLiteral(", "))));
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
        text = QStringLiteral("Checking…");
        break;
    case UpdateRowState::Kind::Latest:
        text = QStringLiteral("v%1 is the latest version").arg(state.version);
        tone = QStringLiteral("ok");
        break;
    case UpdateRowState::Kind::Available:
        text = QStringLiteral("v%1 is available").arg(state.version);
        tone = QStringLiteral("warn");
        break;
    }
    m_updateResult->setText(text);
    Ui::setStyleProperty(m_updateResult, "tone", tone);
#ifdef Q_OS_WIN
    m_updateResult->setVisible(!text.isEmpty());
#endif
    const bool available = state.kind == UpdateRowState::Kind::Available;
    // Mientras dice "Checking…" no se puede pedir otro chequeo.
    m_updateButton->setEnabled(state.kind != UpdateRowState::Kind::Checking);
    m_updateButton->setText(available ? QStringLiteral("Update") : QStringLiteral("Check now"));
    Ui::setStyleProperty(m_updateButton, "variant", available ? QStringLiteral("primary") : QString());
}

void GeneralPage::setAutoStart(bool enabled, bool installedCopy)
{
    // Reflejar no es escribir: con las senales bloqueadas no se dispara autoStartToggled.
    m_autoStart->blockSignals(true);
    m_autoStart->setChecked(enabled);
    m_autoStart->blockSignals(false);
#ifdef Q_OS_MACOS
    m_autoStart->setToolTip(installedCopy ? QStringLiteral("Open LGA Mighty Tools when you log in")
                                          : QStringLiteral("Registers THIS development copy to open when you log in"));
#else
    m_autoStart->setToolTip(installedCopy ? QStringLiteral("Start LGA Mighty Tools when you sign in to Windows")
                                          : QStringLiteral("Registers THIS development copy to start when you sign in"));
#endif
}

void GeneralPage::setCheckUpdatesAtStartup(bool check)
{
    m_checkUpdates->blockSignals(true);
    m_checkUpdates->setChecked(check);
    m_checkUpdates->blockSignals(false);
}
