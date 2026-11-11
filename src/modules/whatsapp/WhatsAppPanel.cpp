#include "modules/whatsapp/WhatsAppPanel.h"

#include "core/I18n.h"
#include "modules/whatsapp/WhatsAppModule.h"
#include "ui/ShortcutRow.h"
#include "ui/Theme.h"
#include "ui/UiWidgets.h"

#include <QDate>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QFrame *titledCard(QWidget *parent, const QString &title, QVBoxLayout **column, int spacing = 6)
{
    QFrame *card = Ui::card(parent);
    *column = new QVBoxLayout(card);
    (*column)->setContentsMargins(18, 15, 18, 16);
    (*column)->setSpacing(spacing);
    (*column)->addWidget(Ui::label(title, "cardTitle", card));
    return card;
}

// El click de un boton del panel puede apagar la herramienta (y borrar el panel): se hace afuera del click.
template <class F>
void runLater(QObject *context, F f)
{
    QTimer::singleShot(0, context, f);
}

const QStringList kModes{QStringLiteral("auto"), QStringLiteral("app"), QStringLiteral("webapp"), QStringLiteral("browser")};

QString noteText(const QString &note, const QDateTime &when)
{
    if (note.isEmpty()) return {};
    const QString time = when.date() == QDate::currentDate() ? when.toString(QStringLiteral("HH:mm"))
                                                             : when.toString(QStringLiteral("dd/MM HH:mm"));
    return I18n::tr("Last try, %1: %2").arg(time, note);
}

} // namespace

WhatsAppPanel::WhatsAppPanel(WhatsAppModule *module, bool interactive, QWidget *parent)
    : QWidget(parent), m_module(module), m_interactive(interactive)
{
    setObjectName(QStringLiteral("whatsAppPanel"));
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(10);
    m_status = new StatusCard(this);
    m_status->setLive(true); // sube al encabezado de la hoja (D-73)
    column->addWidget(m_status);
    column->addWidget(buildShortcutCard());
    column->addWidget(buildNumbersCard());
    column->addWidget(buildOpenCard());
    column->addStretch();
    if (m_interactive) {
        connect(m_status->button(), &QPushButton::clicked, this, [this]() { runLater(this, [this]() { statusAction(); }); });
    }
    refresh();
}

QWidget *WhatsAppPanel::buildShortcutCard()
{
    QVBoxLayout *column = nullptr;
    QFrame *card = titledCard(this, I18n::tr("Shortcut"), &column);
    m_shortcut = new ShortcutRow(I18n::tr("Open the copied number"), I18n::tr("Works from any app, without opening this window."), card);
    m_shortcut->editButton()->setAccessibleName(I18n::tr("Record the shortcut to open the copied number"));
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

QWidget *WhatsAppPanel::buildNumbersCard()
{
    QVBoxLayout *column = nullptr;
    QFrame *card = titledCard(this, I18n::tr("Numbers"), &column, 4);
    auto *row = new QHBoxLayout();
    row->setContentsMargins(0, 4, 0, 0);
    row->setSpacing(12);
    row->addWidget(Ui::label(I18n::tr("Default country"), "optionLabel", card), 0, Qt::AlignVCenter);
    m_country = Ui::button(QString(), QString(), QString(), card);
    m_country->setObjectName(QStringLiteral("fieldButton"));
    m_country->setStyleSheet(QStringLiteral("text-align: left;"));
    m_country->setFocusPolicy(Qt::NoFocus);
    Ui::setDropdownArrow(m_country);
    // El ancho del nombre mas largo, para que ninguno se corte ni el boton cambie de tamano.
    m_country->ensurePolished();
    int width = 0;
    for (const PhoneNumber::Country &c : PhoneNumber::countries()) {
        m_country->setText(WhatsAppModule::countryName(c.id));
        width = qMax(width, m_country->sizeHint().width());
    }
    m_country->setFixedWidth(width);
    row->addWidget(m_country, 0, Qt::AlignVCenter | Qt::AlignLeft | Qt::AlignAbsolute);
    row->addStretch(1);
    column->addLayout(row);
    m_countryHint = Ui::caption(QString(), card);
    m_countryHint->setWordWrap(true);
    column->addSpacing(4);
    column->addWidget(m_countryHint);
    if (m_interactive) {
        connect(m_country, &QPushButton::clicked, this, &WhatsAppPanel::showCountryMenu);
    }
    return card;
}

QWidget *WhatsAppPanel::buildOpenCard()
{
    QVBoxLayout *column = nullptr;
    QFrame *card = titledCard(this, I18n::tr("Open chats in"), &column, 4);
    auto *row = new QHBoxLayout();
    row->setContentsMargins(0, 4, 0, 0);
    row->setSpacing(8);
    m_openIn = new SegmentedSwitch({I18n::tr("Automatic"), I18n::tr("App"), I18n::tr("Web app"), I18n::tr("Browser")}, 0, card);
    row->addWidget(m_openIn, 0, Qt::AlignVCenter);
    row->addStretch(1);
    column->addLayout(row);
    m_found = Ui::caption(QString(), card);
    m_found->setWordWrap(true);
    column->addSpacing(4);
    column->addWidget(m_found);
    if (m_interactive) {
        connect(m_openIn, &SegmentedSwitch::currentChanged, this, [this](int index) { m_module->setOpenIn(kModes.value(index)); });
    }
    return card;
}

void WhatsAppPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // Lo instalado se mira despues de dibujar la pagina: cambiar de pagina no espera al disco (D-77).
    if (m_interactive) {
        QPointer<WhatsAppPanel> safe(this);
        QTimer::singleShot(0, this, [safe]() {
            if (safe) safe->m_module->refreshTargets();
        });
    }
}

void WhatsAppPanel::refresh()
{
    const QString sm = QStringLiteral("sm");
    const QString note = noteText(m_module->attemptNote(), m_module->attemptTime());
    const QString button = note.isEmpty() ? I18n::tr("Open copied number") : I18n::tr("Dismiss");
    const QString variant = note.isEmpty() ? QStringLiteral("primary") : QString();
    QString unavailable;
    const bool available = !m_module->targetsKnown() || WhatsAppModule::choose(m_module->targets(), m_module->openIn(), nullptr, &unavailable);
    if (!m_module->shortcutError().isEmpty()) {
        m_status->set(QStringLiteral("error"), I18n::tr("WhatsApp Chat needs attention"),
                      m_module->shortcutError() + QLatin1Char(' ') + I18n::tr("Choose another shortcut below. Pasting in this window still works."),
                      button, variant, QStringLiteral("err"), sm);
    } else if (!available) {
        m_status->set(QStringLiteral("warn"), I18n::tr("Chats can't open where you chose"),
                      unavailable + QLatin1Char(' ') + I18n::tr("Choose Automatic or another place below."), button, variant,
                      QStringLiteral("warn"), sm);
    } else {
        m_status->set(QStringLiteral("on"), I18n::tr("Ready"),
                      I18n::tr("Copy a phone number, open this window and press %1 on any page. From any other app, press %2.")
                          .arg(QKeySequence(QKeySequence::Paste).toString(QKeySequence::NativeText), m_module->shortcut().displayText()),
                      button, variant, QString(), sm);
    }
    m_status->setNote(note);

    m_shortcut->setShortcut(m_module->shortcut());

    const QString country = m_module->countryId();
    m_country->setText(WhatsAppModule::countryName(country));
    if (country == QLatin1String("AR")) {
        m_countryHint->setText(I18n::tr("Numbers without + are read as Argentine: the 0 and the 15 are removed and the 9 is added."));
    } else if (country == QLatin1String("none")) {
        m_countryHint->setText(I18n::tr("Numbers have to start with + and the country code."));
    } else {
        m_countryHint->setText(I18n::tr("Numbers without + are read as numbers from %1.").arg(WhatsAppModule::countryName(country)));
    }

    const int mode = qMax(0, kModes.indexOf(m_module->openIn()));
    m_openIn->setCurrent(mode);
    bool hasApp = false;
    bool hasWeb = false;
    QStringList found;
    for (const WhatsAppSystem::Target &t : m_module->targets()) {
        hasApp |= t.kind == WhatsAppSystem::Kind::App;
        hasWeb |= t.kind == WhatsAppSystem::Kind::WebApp;
        found << (t.running ? I18n::tr("%1 (open)").arg(WhatsAppSystem::describe(t)) : WhatsAppSystem::describe(t));
    }
    // Lo que no esta instalado no se puede elegir (salvo que ya estuviera elegido: asi se ve por que avisa).
    const bool known = m_module->targetsKnown();
    m_openIn->setSegmentEnabled(1, !known || hasApp || mode == 1);
    m_openIn->setSegmentEnabled(2, !known || hasWeb || mode == 2);
    QString text = I18n::tr("Automatic uses the one that's open, then the app, then the web app, then the browser.");
    if (!known) {
        text += QLatin1Char(' ') + I18n::tr("Looking for WhatsApp…");
    } else if (found.isEmpty()) {
        text += QLatin1Char(' ') + I18n::tr("WhatsApp isn't installed here, so chats open in the browser.");
    } else {
        text += QLatin1Char(' ') + I18n::tr("Found: %1.").arg(found.join(QStringLiteral(" · ")));
    }
    m_found->setText(text);
}

void WhatsAppPanel::showCountryMenu()
{
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    const QString current = m_module->countryId();
    for (const PhoneNumber::Country &c : PhoneNumber::countries()) {
        if (c.id == QLatin1String("none")) menu->addSeparator();
        QAction *action = menu->addAction(WhatsAppModule::countryName(c.id));
        action->setCheckable(true);
        action->setChecked(c.id == current);
        const QString id = c.id;
        connect(action, &QAction::triggered, this, [this, id]() { m_module->setCountry(id); });
    }
    menu->popup(m_country->mapToGlobal(QPoint(0, m_country->height() + 2)));
}

void WhatsAppPanel::statusAction()
{
    if (!m_module->attemptNote().isEmpty()) {
        m_module->dismissAttempt();
        return;
    }
    m_module->openClipboard(WhatsAppModule::Source::Button);
}
