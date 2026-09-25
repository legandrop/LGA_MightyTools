#include "modules/linkredirector/LinkRedirectorModule.h"
#include "modules/linkredirector/LinkRedirectorExternal.h"
#include "modules/linkredirector/LinkRedirectorPanel.h"
#include "modules/linkredirector/LinkRedirectorRouting.h"
#include "modules/linkredirector/BrowserDetection.h"
#include "modules/linkredirector/BrowserRegistration.h"
#include "app/ModuleContext.h"

#include <QDebug>

LinkRedirectorModule::LinkRedirectorModule(ModuleContext &context) : Module(context)
{
}

void LinkRedirectorModule::start()
{
    // Registrarse como candidata a navegador (no la vuelve default: eso lo pide el usuario en el
    // panel del sistema con "Make Default"). Nunca en una corrida automatizada ni desde un arbol de
    // build (ModuleContext::persistentRegistrationAllowed()).
    if (!context().persistentRegistrationAllowed()) {
        return;
    }
    QString error;
    if (!LinkRedirectorBrowserRegistration::registerAsBrowser(&error)) {
        qWarning() << "[linkRedirector] No se pudo registrar como navegador candidato:" << error;
    }
}

void LinkRedirectorModule::stop()
{
    // Nada residente que soltar: el registro de navegador queda hasta que el usuario elija "Remove
    // as browser" (D-09) o se desinstale la app. Ver comentario de la clase.
}

ModuleStatus LinkRedirectorModule::status() const
{
    ModuleStatus s;
    const bool isDefault = context().captureMode() ? m_captureIsDefault : LinkRedirectorBrowserRegistration::isDefaultBrowser();
    s.tone = isDefault ? ModuleTone::Active : ModuleTone::Attention;
    s.text = isDefault ? QStringLiteral("Default browser") : QStringLiteral("Not default browser");
    return s;
}

QWidget *LinkRedirectorModule::createPanel(QWidget *parent)
{
    LinkRedirectorPanelSources sources;
    if (context().captureMode()) {
        // Captura: nunca lee ni escribe el sistema (contrato de Module.h). Los fixtures los fija
        // applyCaptureState().
        sources.isDefaultBrowser = [this]() { return m_captureIsDefault; };
        sources.detectedBrowsers = [this]() { return m_captureDetected; };
        sources.currentDefaultHandlerId = []() { return QString(); };
        sources.exePathForHandlerId = [](const QString &) { return QString(); };
        sources.requestSetAsDefault = []() {
            qInfo() << "[linkRedirector] (captura) Make Default pedido: sin efecto.";
        };
    } else {
        sources.isDefaultBrowser = []() { return LinkRedirectorBrowserRegistration::isDefaultBrowser(); };
        sources.detectedBrowsers = []() { return LinkRedirectorBrowsers::installedBrowsers(); };
        sources.currentDefaultHandlerId = []() { return LinkRedirectorBrowserRegistration::currentDefaultHandlerId(); };
        sources.exePathForHandlerId = [](const QString &handlerId) {
            return LinkRedirectorBrowserRegistration::exePathForHandlerId(handlerId);
        };
        sources.requestSetAsDefault = []() { LinkRedirectorBrowserRegistration::requestSetAsDefault(); };
    }
    return new LinkRedirectorPanel(context(), std::move(sources), parent);
}

ExternalResult LinkRedirectorModule::handleExternal(const ExternalRequest &request)
{
    // App residente de mac con la herramienta prendida: misma regla que el modo corto de Windows y
    // que la herramienta apagada (LinkRedirectorExternal::handle es la unica implementacion). Con
    // contexto: el aviso "navegador no disponible" sale como notificacion (context().notify), no como
    // QMessageBox.
    return LinkRedirectorExternal::handle(request, &context());
}

QStringList LinkRedirectorModule::captureStates() const
{
    return {
        QStringLiteral("default"),
        QStringLiteral("not-default"),
        QStringLiteral("combo-open"),
        QStringLiteral("custom-browser"),
        QStringLiteral("empty-words"),
    };
}

bool LinkRedirectorModule::applyCaptureState(const QString &state)
{
    // Fixture de navegadores detectados, comun a todos los estados (nunca toca el registro/LaunchServices).
    m_captureDetected = {
        DetectedBrowser{QStringLiteral("Google Chrome"), QStringLiteral("C:/Program Files/Google/Chrome/Application/chrome.exe"), QStringLiteral("ChromeHTML")},
        DetectedBrowser{QStringLiteral("Microsoft Edge"), QStringLiteral("C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe"), QStringLiteral("MSEdgeHTM")},
        DetectedBrowser{QStringLiteral("Firefox"), QStringLiteral("C:/Program Files/Mozilla Firefox/firefox.exe"), QStringLiteral("FirefoxURL")},
    };
    const QString kChrome = QStringLiteral("C:/Program Files/Google/Chrome/Application/chrome.exe");
    const QString kFirefox = QStringLiteral("C:/Program Files/Mozilla Firefox/firefox.exe");
    const QStringList kWords = {QStringLiteral("netflixstudios"), QStringLiteral("frame.io"), QStringLiteral("shotgrid")};

    if (state == QLatin1String("default") || state == QLatin1String("combo-open")) {
        m_captureIsDefault = true;
        context().setValue(QStringLiteral("defaultBrowser"), kChrome);
        context().setValue(QStringLiteral("alternativeBrowser"), kFirefox);
        context().setValue(QStringLiteral("matchWords"), kWords);
        return true;
    }
    if (state == QLatin1String("not-default")) {
        m_captureIsDefault = false;
        context().setValue(QStringLiteral("defaultBrowser"), kChrome);
        context().setValue(QStringLiteral("alternativeBrowser"), kFirefox);
        context().setValue(QStringLiteral("matchWords"), kWords);
        return true;
    }
    if (state == QLatin1String("custom-browser")) {
        m_captureIsDefault = true;
        // A proposito NO esta en m_captureDetected: muestra el "(custom)" del combo cerrado.
        context().setValue(QStringLiteral("defaultBrowser"), QStringLiteral("C:/Apps/Brave/Brave.exe"));
        context().setValue(QStringLiteral("alternativeBrowser"), kFirefox);
        context().setValue(QStringLiteral("matchWords"), kWords);
        return true;
    }
    if (state == QLatin1String("empty-words")) {
        m_captureIsDefault = true;
        context().setValue(QStringLiteral("defaultBrowser"), kChrome);
        context().setValue(QStringLiteral("alternativeBrowser"), kFirefox);
        context().setValue(QStringLiteral("matchWords"), QStringList{});
        return true;
    }
    return false;
}

QWidget *LinkRedirectorModule::createCaptureWidget(const QString &state, QWidget *parent)
{
    if (state != QLatin1String("combo-open")) {
        return nullptr; // los demas estados de captura SON el panel (canvas, seccion 3 "estados").
    }
    const QString configured = context().value(QStringLiteral("defaultBrowser")).toString();
    const auto items = LinkRedirectorRouting::buildBrowserComboItems(configured, m_captureDetected);
    return LinkRedirectorPanel::buildDropdownPreview(items, parent);
}
