#include "modules/linkredirector/LinkRedirectorModule.h"
#include "modules/linkredirector/LinkRedirectorExternal.h"
#include "modules/linkredirector/BrowserRegistration.h"
#include "app/ModuleContext.h"

#include <QDebug>
#include <QLabel>

LinkRedirectorModule::LinkRedirectorModule(ModuleContext &context) : Module(context)
{
}

void LinkRedirectorModule::start()
{
    // Registrarse como candidata a navegador (no la vuelve default: eso lo pide el usuario en el
    // panel del sistema con "Make Default", etapa 2). Nunca en una corrida automatizada ni desde un
    // arbol de build (ModuleContext::persistentRegistrationAllowed()).
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
    if (context().captureMode()) {
        // Sin panel real todavia (etapa 2): en captura no se toca el sistema (contrato de Module.h).
        s.tone = ModuleTone::Attention;
        s.text = QStringLiteral("Not default browser");
        return s;
    }
    const bool isDefault = LinkRedirectorBrowserRegistration::isDefaultBrowser();
    s.tone = isDefault ? ModuleTone::Active : ModuleTone::Attention;
    s.text = isDefault ? QStringLiteral("Default browser") : QStringLiteral("Not default browser");
    return s;
}

QWidget *LinkRedirectorModule::createPanel(QWidget *parent)
{
    // PLACEHOLDER EXPLICITO (etapa 1): el panel de verdad (tarjeta de estado, combos de navegador,
    // Match words) es etapa 2, cuando exista ModuleHost/MainWindow para alojarlo. La logica que va a
    // usar ese panel (ruteo, deteccion, registro) ya esta completa en este modulo.
    auto *placeholder = new QLabel(QStringLiteral("Link Redirector panel (etapa 2)"), parent);
    placeholder->setAlignment(Qt::AlignCenter);
    return placeholder;
}

ExternalResult LinkRedirectorModule::handleExternal(const ExternalRequest &request)
{
    // App residente de mac con la herramienta prendida: misma regla que el modo corto de Windows y
    // que la herramienta apagada (LinkRedirectorExternal::handle es la unica implementacion).
    return LinkRedirectorExternal::handle(request);
}
