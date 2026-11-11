#include "modules/whatsapp/WhatsAppModule.h"

#include "app/ModuleContext.h"
#include "core/I18n.h"
#include "modules/whatsapp/WhatsAppPanel.h"

#include <QClipboard>
#include <QDebug>
#include <QGuiApplication>
#include <QKeySequence>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QSet>
#include <QWidget>

#include <cstdio>

namespace {

const char *kCountryKey = "country";
const char *kOpenInKey = "openIn";
// La deteccion se reusa unos segundos (abrir el panel y pegar enseguida no mira dos veces).
constexpr qint64 kTargetsFreshMs = 3000;

// Un globo de chat con una flecha hacia adentro: el numero que entra a una conversacion. Propio, no el logo
// de WhatsApp.
void paintIcon(QPainter &painter, const QRectF &rect, const QColor &color)
{
    const qreal scale = qMin(rect.width(), rect.height()) / 16.0;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.translate(rect.center().x() - 8 * scale, rect.center().y() - 8 * scale);
    painter.scale(scale, scale);
    painter.setPen(QPen(color, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    QPainterPath bubble;
    bubble.moveTo(4.2, 2.2);
    bubble.lineTo(12.8, 2.2);
    bubble.quadTo(14.6, 2.2, 14.6, 4.0);
    bubble.lineTo(14.6, 9.6);
    bubble.quadTo(14.6, 11.4, 12.8, 11.4);
    bubble.lineTo(7.0, 11.4);
    bubble.lineTo(3.6, 14.2);
    bubble.lineTo(3.9, 11.4);
    bubble.quadTo(2.4, 11.2, 2.4, 9.6);
    bubble.lineTo(2.4, 4.0);
    bubble.quadTo(2.4, 2.2, 4.2, 2.2);
    painter.drawPath(bubble);
    QPainterPath arrow;
    arrow.moveTo(5.6, 6.8);
    arrow.lineTo(11.0, 6.8);
    arrow.moveTo(8.8, 4.8);
    arrow.lineTo(11.0, 6.8);
    arrow.lineTo(8.8, 8.8);
    painter.drawPath(arrow);
    painter.restore();
}

QString pasteKeys()
{
    return QKeySequence(QKeySequence::Paste).toString(QKeySequence::NativeText);
}

// ---- QA: atajos y contexto de mentira (nunca tocan el sistema)

class FakeHotkeys : public ModuleHotkeys
{
public:
    int registered = 0;
    bool registerHotkey(int, const Shortcut &) override { ++registered; return true; }
    void unregisterHotkey(int) override {}
    void unregisterAll() override {}
    bool isRegistered(int) const override { return false; }
    bool probe(const Shortcut &) override { return true; }
    void passThrough(const Shortcut &) override {}
    QString declare(int, const Shortcut &) override { return {}; }
    QString declaredByOtherModule(const Shortcut &) const override { return {}; }
};

class FakeContext : public ModuleContext
{
public:
    QString moduleId() const override { return QStringLiteral("whatsApp"); }
    QString moduleTitle() const override { return QStringLiteral("WhatsApp Chat"); }
    QVariant value(const QString &key, const QVariant &fallback = {}) const override { return m_values.value(key, fallback); }
    void setValue(const QString &key, const QVariant &value) override { m_values.insert(key, value); }
    void removeValue(const QString &key) override { m_values.remove(key); }
    bool captureMode() const override { return false; }
    bool dryRunInput() const override { return true; }
    bool automatedRun() const override { return true; }
    bool persistentRegistrationAllowed() const override { return false; }
    ModuleHotkeys *hotkeys() override { return &fake; }
    InputInjector *injector() override { return nullptr; }
    ForegroundWatcher *foreground() override { return nullptr; }
    void notify(const QString &, const QString &, NoticeIcon, int) override { ++notices; }
    void showPanel() override { ++panels; }
    void hideWindowTemporarily() override {}
    void restoreWindow() override {}
    QWidget *window() const override { return nullptr; }

    FakeHotkeys fake;
    int notices = 0;
    int panels = 0;

private:
    QHash<QString, QVariant> m_values;
};

void selfTest(const std::function<void(bool, const QString &)> &check)
{
    using PhoneNumber::Status;
    const auto digits = [](const QString &text, const QString &country = QStringLiteral("AR")) {
        const PhoneNumber::Result r = PhoneNumber::parse(text, country);
        return r.status == Status::Ok ? r.digits : QStringLiteral("<%1>").arg(int(r.status));
    };
    const auto expect = [&](const QString &text, const QString &wanted, const QString &country = QStringLiteral("AR")) {
        const QString got = digits(text, country);
        check(got == wanted, QStringLiteral("numero: «%1» (%2) -> %3, esperado %4").arg(text, country, got, wanted));
    };
    const QString ba = QStringLiteral("5491145678901");
    // ---- Argentina: todas las formas de escribir un celular.
    expect(QStringLiteral("011 15-4567-8901"), ba);
    expect(QStringLiteral("(011) 15 4567 8901"), ba);
    expect(QStringLiteral("11 15 4567 8901"), ba);
    expect(QStringLiteral("11-4567-8901"), ba);
    expect(QStringLiteral("1145678901"), ba);
    expect(QStringLiteral("+54 9 11 4567-8901"), ba);
    expect(QStringLiteral("+54 11 4567-8901"), ba);
    expect(QStringLiteral("+54 9 11 15 4567 8901"), ba);
    expect(QStringLiteral("+54 (011) 15 4567-8901"), ba);
    expect(QStringLiteral("0054 9 11 4567 8901"), ba);
    expect(QStringLiteral("5491145678901"), ba);
    expect(QStringLiteral("Cel: 011 15-4567-8901 (Juan)"), ba);
    expect(QStringLiteral("Juan Pérez\nTel.: +54 9 11 4567-8901\nmail: juan@example.com"), ba);
    expect(QStringLiteral("(0351) 15-123-4567"), QStringLiteral("5493511234567"));
    expect(QStringLiteral("0351 153 123456"), QStringLiteral("5493513123456"));
    expect(QStringLiteral("02966 15-123456"), QStringLiteral("5492966123456"));
    expect(QStringLiteral("0294 15 4123456"), QStringLiteral("5492944123456"));
    expect(QStringLiteral("+54 341 155 123456"), QStringLiteral("5493415123456"));
    expect(QStringLiteral("el mismo +54 9 11 4567-8901 dos veces: 011 15 4567 8901"), ba);
    // ---- Links de WhatsApp: mandan sobre lo demas.
    expect(QStringLiteral("https://wa.me/5491145678901?text=hola"), ba);
    expect(QStringLiteral("https://api.whatsapp.com/send/?phone=541145678901&text=x"), QStringLiteral("541145678901"));
    // ---- Lo que encontro la auditoria: pedazos del tramo, facturas, (0), CUIT solo en Argentina.
    expect(QStringLiteral("Juan 11 4567 8901 24hs"), ba);
    expect(QStringLiteral("1) 11 4567-8901"), ba);
    expect(QStringLiteral("DNI 30123456 11 4567 8901"), ba);
    expect(QStringLiteral("+44 (0) 20 7946 0958"), QStringLiteral("442079460958"));
    expect(QStringLiteral("(24) 98765-4321"), QStringLiteral("5524987654321"), QStringLiteral("BR"));
    expect(QStringLiteral("34612345679"), QStringLiteral("34612345679"), QStringLiteral("ES"));
    expect(QStringLiteral("whatsapp://send?phone=34612345678"), QStringLiteral("34612345678"));
    // ---- Otros paises con +, y con su pais por defecto.
    expect(QStringLiteral("+34 612 34 56 78"), QStringLiteral("34612345678"));
    expect(QStringLiteral("+1 (212) 555-0147"), QStringLiteral("12125550147"));
    expect(QStringLiteral("099 123 456"), QStringLiteral("59899123456"), QStringLiteral("UY"));
    expect(QStringLiteral("9 8765 4321"), QStringLiteral("56987654321"), QStringLiteral("CL"));
    expect(QStringLiteral("044 55 1234 5678"), QStringLiteral("525512345678"), QStringLiteral("MX"));
    expect(QStringLiteral("(212) 555-0147"), QStringLiteral("12125550147"), QStringLiteral("US"));
    expect(QStringLiteral("612 34 56 78"), QStringLiteral("34612345678"), QStringLiteral("ES"));
    // ---- Lo que no es un numero de telefono.
    const auto status = [](const QString &text, const QString &country = QStringLiteral("AR")) {
        return PhoneNumber::parse(text, country).status;
    };
    check(status(QStringLiteral("hola, nos vemos el 11/11/2026 a las 14:30")) == Status::None, QStringLiteral("numero: fecha y hora no son telefonos"));
    check(status(QStringLiteral("CUIT 20-12345678-6")) == Status::None && status(QStringLiteral("20123456786")) == Status::None,
          QStringLiteral("numero: un CUIT (con y sin guiones) no es un telefono"));
    check(status(QStringLiteral("DNI 12.345.678")) == Status::None, QStringLiteral("numero: un DNI con puntos no es un telefono"));
    check(status(QStringLiteral("servidor 192.168.100.200")) == Status::None, QStringLiteral("numero: una IP no es un telefono"));
    check(status(QStringLiteral("tarjeta 4111 1111 1111 1111")) == Status::None, QStringLiteral("numero: 16 digitos no son un telefono"));
    check(status(QStringLiteral("https://example.com/item/11456789012")) == Status::None
              && status(QStringLiteral("pedido ABC1145678901")) == Status::None && status(QStringLiteral("2026-11-11")) == Status::None,
          QStringLiteral("numero: digitos dentro de una URL, pegados a letras o una fecha ISO no cuentan"));
    check(status(QStringLiteral("Factura A 0001-00012345")) != Status::Ok && status(QStringLiteral("https://wa.me/1234567890123456")) == Status::None,
          QStringLiteral("numero: una factura con 00 adelante y un link con 16 digitos no son telefonos"));
    check(status(QStringLiteral("")) == Status::None && status(QStringLiteral("sin numeros")) == Status::None,
          QStringLiteral("numero: texto vacio o sin digitos"));
    const PhoneNumber::Result local = PhoneNumber::parse(QStringLiteral("4567-8901"), QStringLiteral("AR"));
    check(local.status == Status::Incomplete && local.problem == QLatin1String("area"),
          QStringLiteral("numero: un local sin area pide el area"));
    const PhoneNumber::Result noCountry = PhoneNumber::parse(QStringLiteral("11 4567 8901"), QStringLiteral("none"));
    check(noCountry.status == Status::Incomplete && noCountry.problem == QLatin1String("country-code"),
          QStringLiteral("numero: sin pais por defecto, sin + pide el codigo de pais"));
    check(digits(QStringLiteral("+54 9 11 4567-8901"), QStringLiteral("none")) == ba, QStringLiteral("numero: con + no hace falta pais"));
    const PhoneNumber::Result two = PhoneNumber::parse(QStringLiteral("Juan 11 4567 8901, Ana 0351 15 123 4567"), QStringLiteral("AR"));
    check(two.status == Status::Several && two.found.size() == 2, QStringLiteral("numero: dos numeros distintos no eligen"));
    check(PhoneNumber::parse(QStringLiteral("DNI 12345678, cel 11 4567 8901"), QStringLiteral("AR")).digits == ba,
          QStringLiteral("numero: un numero completo gana sobre uno incompleto"));
    check(PhoneNumber::format(ba) == QLatin1String("+54 9 11 4567-8901")
              && PhoneNumber::format(QStringLiteral("5493511234567")) == QLatin1String("+54 9 351123-4567")
              && PhoneNumber::format(QStringLiteral("34612345678")) == QLatin1String("+34 612345678"),
          QStringLiteral("numero: formato para mostrar"));
    check(!PhoneNumber::logText(ba).contains(QStringLiteral("45678901")), QStringLiteral("numero: el log no lleva el numero"));
    for (const PhoneNumber::Country &c : PhoneNumber::countries()) {
        check(!WhatsAppModule::countryName(c.id).isEmpty(), QStringLiteral("paises: %1 tiene nombre").arg(c.id));
    }

    // ---- La eleccion del destino.
    using WhatsAppSystem::Kind;
    using WhatsAppSystem::Target;
    Target app;
    app.kind = Kind::App;
    app.name = QStringLiteral("WhatsApp");
    Target web;
    web.kind = Kind::WebApp;
    web.name = QStringLiteral("WhatsApp Web");
    web.browser = QStringLiteral("Brave");
    Target out;
    QString problem;
    check(WhatsAppModule::choose({}, QStringLiteral("auto"), &out, &problem) && out.kind == Kind::Browser,
          QStringLiteral("destino: sin nada instalado, el navegador"));
    check(WhatsAppModule::choose({app, web}, QStringLiteral("auto"), &out, &problem) && out.kind == Kind::App,
          QStringLiteral("destino: con las dos cerradas, la app"));
    web.running = true;
    check(WhatsAppModule::choose({app, web}, QStringLiteral("auto"), &out, &problem) && out.kind == Kind::WebApp,
          QStringLiteral("destino: la abierta gana (app web abierta, app cerrada)"));
    app.running = true;
    check(WhatsAppModule::choose({app, web}, QStringLiteral("auto"), &out, &problem) && out.kind == Kind::App,
          QStringLiteral("destino: con las dos abiertas, la app"));
    check(!WhatsAppModule::choose({web}, QStringLiteral("app"), &out, &problem) && !problem.isEmpty(),
          QStringLiteral("destino: forzar la app sin la app avisa"));
    check(!WhatsAppModule::choose({app}, QStringLiteral("webapp"), &out, &problem) && !problem.isEmpty(),
          QStringLiteral("destino: forzar la app web sin la app web avisa"));
    check(WhatsAppModule::choose({app, web}, QStringLiteral("browser"), &out, &problem) && out.kind == Kind::Browser,
          QStringLiteral("destino: forzar el navegador"));
    check(WhatsAppSystem::webUrl(ba) == QLatin1String("https://web.whatsapp.com/send?phone=5491145678901")
              && WhatsAppSystem::appUrl(ba) == QLatin1String("whatsapp://send?phone=5491145678901"),
          QStringLiteral("destino: URLs sin wa.me"));

    // ---- El modulo en una corrida automatizada: nunca abre nada.
    FakeContext context;
    {
        WhatsAppModule module(context);
        module.start();
        check(module.handleWindowPaste(QStringLiteral("011 15-4567-8901")) && module.attemptNote().isEmpty()
                  && !module.lastOpened().isEmpty() && context.notices == 0,
              QStringLiteral("modulo: pegar un numero lo «abre» (solo loguea) sin nota"));
        check(module.handleWindowPaste(QStringLiteral("nada que ver")) && !module.attemptNote().isEmpty() && context.panels == 1
                  && module.status().tone != ModuleTone::Error,
              QStringLiteral("modulo: pegar algo sin numero deja la nota, muestra el panel y no pasa a rojo"));
        module.dismissAttempt();
        check(module.attemptNote().isEmpty(), QStringLiteral("modulo: Dismiss borra la nota"));
        check(!module.openText(QStringLiteral("x"), WhatsAppModule::Source::Button) && context.panels == 1 && context.notices == 0,
              QStringLiteral("modulo: el boton sin numero deja la nota sin notificacion ni cambio de pagina"));
        module.setCountry(QStringLiteral("ZZ"));
        check(module.countryId() == PhoneNumber::defaultCountry(), QStringLiteral("modulo: un pais desconocido no se guarda"));
        module.setOpenIn(QStringLiteral("cualquiera"));
        check(module.openIn() == QLatin1String("auto"), QStringLiteral("modulo: un destino desconocido queda en automatico"));
        module.stop();
        check(context.fake.registered == 0, QStringLiteral("modulo: no registra ningun atajo (solo pegar en la ventana)"));
    }
}

int simulateAction(const QString &action, const QStringList &args)
{
    if (action == QLatin1String("parse")) {
        const QString text = args.value(0);
        const QString country = args.value(1, PhoneNumber::defaultCountry());
        const PhoneNumber::Result r = PhoneNumber::parse(text, country);
        const char *names[] = {"ok", "ninguno", "varios", "incompleto"};
        std::printf("estado: %s\n", names[int(r.status)]);
        if (r.status == PhoneNumber::Status::Ok) {
            std::printf("numero: %s (%s)%s\n", qPrintable(r.digits), qPrintable(r.display), r.fromLink ? " de un link" : "");
            std::printf("app: %s\nweb: %s\n", qPrintable(WhatsAppSystem::appUrl(r.digits)), qPrintable(WhatsAppSystem::webUrl(r.digits)));
        }
        for (const QString &found : r.found) std::printf("encontrado: %s\n", qPrintable(found));
        if (!r.problem.isEmpty()) std::printf("motivo: %s\n", qPrintable(r.problem));
        return r.status == PhoneNumber::Status::Ok ? 0 : 3;
    }
    if (action == QLatin1String("targets")) {
        // Solo lectura: lo instalado y lo abierto. No se abre nada.
        const QList<WhatsAppSystem::Target> targets = WhatsAppSystem::detect();
        for (const WhatsAppSystem::Target &t : targets) {
            std::printf("%s: %s%s%s%s\n", t.kind == WhatsAppSystem::Kind::App ? "app" : "app web", qPrintable(WhatsAppSystem::describe(t)),
                        t.running ? " (abierta)" : "", t.appId.isEmpty() ? "" : qPrintable(QStringLiteral(" id=") + t.appId),
                        t.path.isEmpty() ? "" : qPrintable(QStringLiteral(" en ") + t.path));
        }
        if (targets.isEmpty()) std::printf("nada instalado: se abriria el navegador\n");
        const QString mode = args.value(0, QStringLiteral("auto"));
        WhatsAppSystem::Target chosen;
        QString problem;
        if (WhatsAppModule::choose(targets, mode, &chosen, &problem)) {
            std::printf("con «%s» abriria: %s\n", qPrintable(mode), qPrintable(WhatsAppSystem::describe(chosen)));
        } else {
            std::printf("con «%s» no se puede: %s\n", qPrintable(mode), qPrintable(problem));
        }
        return 0;
    }
    return 2;
}

} // namespace

WhatsAppModule::WhatsAppModule(ModuleContext &context) : Module(context)
{
}

WhatsAppModule::~WhatsAppModule()
{
    stop();
}

void WhatsAppModule::start()
{
    if (m_started) {
        return;
    }
    m_started = true;
}

void WhatsAppModule::stop()
{
    if (!m_started) {
        return;
    }
    m_started = false;
}

// ------------------------------------------------------------------ Ajustes

QString WhatsAppModule::countryId() const
{
    const QString id = context().value(QLatin1String(kCountryKey), PhoneNumber::defaultCountry()).toString();
    return PhoneNumber::country(id) ? id : PhoneNumber::defaultCountry();
}

void WhatsAppModule::setCountry(const QString &id)
{
    if (!PhoneNumber::country(id) || id == countryId()) return;
    context().setValue(QLatin1String(kCountryKey), id);
    emit changed();
}

QString WhatsAppModule::openIn() const
{
    const QString mode = context().value(QLatin1String(kOpenInKey), QStringLiteral("auto")).toString();
    static const QStringList known{QStringLiteral("auto"), QStringLiteral("app"), QStringLiteral("webapp"), QStringLiteral("browser")};
    return known.contains(mode) ? mode : QStringLiteral("auto");
}

void WhatsAppModule::setOpenIn(const QString &mode)
{
    static const QStringList known{QStringLiteral("auto"), QStringLiteral("app"), QStringLiteral("webapp"), QStringLiteral("browser")};
    if (!known.contains(mode) || mode == openIn()) return;
    context().setValue(QLatin1String(kOpenInKey), mode);
    emit statusChanged();
    emit changed();
}

// ------------------------------------------------------------------ Destinos

void WhatsAppModule::refreshTargets(bool force)
{
    if (context().captureMode()) return;
    if (!force && m_targetsKnown && m_targetsAge.isValid() && m_targetsAge.elapsed() < kTargetsFreshMs) return;
    const QList<WhatsAppSystem::Target> found = WhatsAppSystem::detect();
    const auto key = [](const QList<WhatsAppSystem::Target> &list) {
        QStringList parts;
        for (const WhatsAppSystem::Target &t : list) parts << WhatsAppSystem::describe(t) + (t.running ? QStringLiteral("*") : QString());
        return parts;
    };
    const bool same = m_targetsKnown && key(found) == key(m_targets);
    m_targets = found;
    m_targetsKnown = true;
    m_targetsAge.start();
    if (same) return;
    emit statusChanged();
    emit changed();
}

bool WhatsAppModule::choose(const QList<WhatsAppSystem::Target> &targets, const QString &mode, WhatsAppSystem::Target *out,
                            QString *problem)
{
    using WhatsAppSystem::Kind;
    const auto first = [&](Kind kind, bool runningOnly) -> const WhatsAppSystem::Target * {
        for (const WhatsAppSystem::Target &t : targets) {
            if (t.kind == kind && (!runningOnly || t.running)) return &t;
        }
        return nullptr;
    };
    const WhatsAppSystem::Target *pick = nullptr;
    if (mode == QLatin1String("app")) {
        pick = first(Kind::App, false);
        if (!pick) {
            if (problem) *problem = I18n::tr("The WhatsApp app isn't installed.");
            return false;
        }
    } else if (mode == QLatin1String("webapp")) {
        pick = first(Kind::WebApp, true);
        if (!pick) pick = first(Kind::WebApp, false);
        if (!pick) {
            if (problem) *problem = I18n::tr("WhatsApp Web isn't installed as an app.");
            return false;
        }
    } else if (mode != QLatin1String("browser")) {
        for (const auto &[kind, running] : {std::pair{Kind::App, true}, std::pair{Kind::WebApp, true}, std::pair{Kind::App, false},
                                            std::pair{Kind::WebApp, false}}) {
            pick = first(kind, running);
            if (pick) break;
        }
    }
    if (out) {
        *out = pick ? *pick : WhatsAppSystem::Target{};
    }
    return true;
}

// ------------------------------------------------------------------ La accion

QString WhatsAppModule::countryName(const QString &id)
{
    if (id == QLatin1String("AR")) return I18n::tr("Argentina");
    if (id == QLatin1String("BO")) return I18n::tr("Bolivia");
    if (id == QLatin1String("BR")) return I18n::tr("Brazil");
    if (id == QLatin1String("CL")) return I18n::tr("Chile");
    if (id == QLatin1String("CO")) return I18n::tr("Colombia");
    if (id == QLatin1String("MX")) return I18n::tr("Mexico");
    if (id == QLatin1String("PY")) return I18n::tr("Paraguay");
    if (id == QLatin1String("PE")) return I18n::tr("Peru");
    if (id == QLatin1String("ES")) return I18n::tr("Spain");
    if (id == QLatin1String("US")) return I18n::tr("United States and Canada");
    if (id == QLatin1String("UY")) return I18n::tr("Uruguay");
    if (id == QLatin1String("none")) return I18n::tr("None (always use +)");
    return {};
}

QString WhatsAppModule::resultProblem(const PhoneNumber::Result &result)
{
    switch (result.status) {
    case PhoneNumber::Status::Ok:
        return {};
    case PhoneNumber::Status::None:
        return I18n::tr("There's no phone number in what you copied.");
    case PhoneNumber::Status::Several:
        return I18n::tr("You copied %1 different numbers (%2). Copy only one.").arg(result.found.size()).arg(result.found.join(QStringLiteral(", ")));
    case PhoneNumber::Status::Incomplete:
        break;
    }
    if (result.problem == QLatin1String("area")) return I18n::tr("The number you copied has no area code. Copy it with the area code.");
    if (result.problem == QLatin1String("argentina")) return I18n::tr("The number you copied isn't a complete Argentine number.");
    if (result.problem == QLatin1String("country-code")) return I18n::tr("The number you copied has no country code. Copy it with + and the country code, or choose a default country.");
    return I18n::tr("The number you copied isn't complete.");
}

bool WhatsAppModule::openText(const QString &text, Source source)
{
    const PhoneNumber::Result result = PhoneNumber::parse(text, countryId());
    if (result.status != PhoneNumber::Status::Ok) {
        const QString problem = resultProblem(result);
        qInfo().noquote() << "[whatsApp] Sin numero claro en lo copiado:" << int(result.status) << result.problem;
        setAttempt(problem);
        if (source == Source::Paste) context().showPanel();
        return false;
    }
    refreshTargets(true);
    WhatsAppSystem::Target target;
    QString problem;
    if (!choose(m_targets, openIn(), &target, &problem)) {
        setAttempt(problem);
        if (source == Source::Paste) context().showPanel();
        return false;
    }
    m_lastOpened = WhatsAppSystem::describe(target);
    qInfo().noquote() << "[whatsApp]" << PhoneNumber::logText(result.digits) << "->" << m_lastOpened;
    if (context().automatedRun()) {
        qInfo() << "[whatsApp] Corrida automatizada: solo se loguea";
        setAttempt(QString());
        return true;
    }
    QString error;
    if (!WhatsAppSystem::open(target, result.digits, &error)) {
        setAttempt(error.isEmpty() ? I18n::tr("WhatsApp could not be opened.") : error);
        if (source == Source::Paste) context().showPanel();
        return false;
    }
    setAttempt(QString());
    // Primero se abre WhatsApp y despues se esconde la ventana: escondida antes, Windows le quitaria a la app el
    // permiso de pasar lo que abre al frente.
    QPointer<QWidget> window = context().window();
    if (window && window->isVisible()) window->close();
    return true;
}

void WhatsAppModule::openClipboard(Source source)
{
    if (!m_started) return;
    openText(QGuiApplication::clipboard()->text(), source);
}

bool WhatsAppModule::handleWindowPaste(const QString &text)
{
    if (!m_started) return false;
    openText(text, Source::Paste);
    return true;
}

void WhatsAppModule::setAttempt(const QString &note)
{
    if (note.isEmpty() && m_attemptNote.isEmpty()) return;
    m_attemptNote = note;
    m_attemptTime = note.isEmpty() ? QDateTime() : QDateTime::currentDateTime();
    emit changed();
}

void WhatsAppModule::dismissAttempt()
{
    setAttempt(QString());
}

ModuleStatus WhatsAppModule::status() const
{
    // El lugar elegido no esta (se desinstalo la app): ambar con lo ultimo detectado, sin mirar el sistema aca.
    if (m_targetsKnown && !choose(m_targets, openIn(), nullptr, nullptr)) {
        return {ModuleTone::Attention, I18n::tr("Needs attention")};
    }
    return {ModuleTone::Active, I18n::tr("Ready")};
}

QWidget *WhatsAppModule::createPanel(QWidget *parent)
{
    auto *panel = new WhatsAppPanel(this, !context().captureMode(), parent);
    connect(this, &WhatsAppModule::changed, panel, &WhatsAppPanel::refresh);
    return panel;
}

QStringList WhatsAppModule::captureStates() const
{
    return {QStringLiteral("ready"), QStringLiteral("web-app"), QStringLiteral("browser-only"), QStringLiteral("attempt"),
            QStringLiteral("several"), QStringLiteral("forced-missing")};
}

bool WhatsAppModule::applyCaptureState(const QString &state)
{
    if (!context().captureMode() || !captureStates().contains(state)) {
        return false;
    }
    using WhatsAppSystem::Kind;
    WhatsAppSystem::Target app;
    app.kind = Kind::App;
    app.name = QStringLiteral("WhatsApp");
    app.running = true;
    WhatsAppSystem::Target web;
    web.kind = Kind::WebApp;
    web.name = QStringLiteral("WhatsApp Web");
    web.browser = QStringLiteral("Brave");
    m_targets.clear();
    m_targetsKnown = true;
    m_attemptNote.clear();
    const QDateTime when(QDate(2026, 11, 11), QTime(14, 32));
    if (state == QLatin1String("ready") || state == QLatin1String("attempt") || state == QLatin1String("several")) {
        m_targets = {app, web};
    } else if (state == QLatin1String("web-app")) {
        web.running = true;
        m_targets = {web};
    } else if (state == QLatin1String("forced-missing")) {
        m_targets = {web};
        context().setValue(QLatin1String(kOpenInKey), QStringLiteral("app"));
    }
    if (state == QLatin1String("attempt")) {
        m_attemptNote = resultProblem(PhoneNumber::parse(QStringLiteral("4567-8901"), QStringLiteral("AR")));
        m_attemptTime = when;
    } else if (state == QLatin1String("several")) {
        m_attemptNote = resultProblem(PhoneNumber::parse(QStringLiteral("11 4567 8901 / 0351 15 123 4567"), QStringLiteral("AR")));
        m_attemptTime = when;
    }
    return true;
}

ModuleDescriptor whatsAppDescriptor()
{
    ModuleDescriptor d;
    d.id = QStringLiteral("whatsApp");
    d.title = QStringLiteral("WhatsApp Chat");
    d.description = I18n::tr("Paste a copied phone number and its WhatsApp chat opens.");
    d.offBullets = {I18n::tr("Paste a phone number in this window, on any page, to open its chat"),
                    I18n::tr("Uses the WhatsApp app, WhatsApp Web installed as an app, or the browser")};
    d.platforms = PlatformWindows | PlatformMac;
    d.paintIcon = &paintIcon;
    d.create = [](ModuleContext &context) -> std::unique_ptr<Module> {
        return std::make_unique<WhatsAppModule>(context);
    };
    d.selfTest = &selfTest;
    d.simulateAction = &simulateAction;
    return d;
}

HelpSection whatsAppHelp(const SettingsReader &)
{
    HelpSection help;
    help.title = QStringLiteral("WhatsApp Chat");
    help.steps = {I18n::tr("Copy a phone number from anywhere: a web page, a mail, a chat."),
                  I18n::tr("Open this window and press %1 on any page.").arg(pasteKeys()),
                  I18n::tr("The chat opens in WhatsApp, new or existing, and this window closes.")};
    help.note = I18n::tr("Numbers without + are read with the default country. Nothing is saved: the number only goes to WhatsApp.");
    return help;
}
