#ifndef MIGHTYTOOLS_LINKREDIRECTOR_MODULE_H
#define MIGHTYTOOLS_LINKREDIRECTOR_MODULE_H

#include "app/Module.h"
#include "modules/linkredirector/LinkRedirectorTypes.h"

#include <QList>
#include <QString>

// Modulo Link Redirector. En Windows no tiene nada residente: cada link llega como un proceso corto
// que el host resuelve con ModuleDescriptor::runExternal, sin construir este objeto (plan 4.3, D-05).
// Este modulo solo existe mientras la herramienta esta PRENDIDA, para:
//  - registrarse como candidata a navegador al arrancar (start(), guardado por
//    ModuleContext::persistentRegistrationAllowed());
//  - en mac, la app residente lo usa para despachar los links que llegan como QFileOpenEvent
//    (handleExternal(), misma logica que runExternal via LinkRedirectorExternal::handle);
//  - mostrar en su fila si Windows/mac ya la tiene como navegador por defecto;
//  - el panel de verdad (etapa 2): tarjeta de estado, combos de navegador, Match words.
//
// stop() NO desregistra (D-09): sacar el registro de navegador es una accion explicita ("Remove as
// browser" del aviso de apagado, o --uninstall-cleanup), nunca un efecto secundario de apagar la
// herramienta. Si stop() desregistrara, el aviso "Windows still sends links here" nunca podria pasar.
class LinkRedirectorModule : public Module
{
    Q_OBJECT

public:
    explicit LinkRedirectorModule(ModuleContext &context);

    void start() override;
    void stop() override;
    ModuleStatus status() const override;
    QWidget *createPanel(QWidget *parent) override;
    ExternalResult handleExternal(const ExternalRequest &request) override;

    // QA (--ui-shot <id>:<estado>): "default"/"not-default" (tarjeta de estado), "combo-open" (la
    // lista del combo "Default browser" desplegada, como widget de captura), "custom-browser" (un
    // navegador configurado que la deteccion no lista) y "empty-words" (Match words vacio, con su
    // placeholder). Ninguno toca el sistema: fijan valores de prueba en memoria (ModuleContext, en
    // captura, nunca escribe settings.ini) y una lista de navegadores detectados de mentira.
    QStringList captureStates() const override;
    bool applyCaptureState(const QString &state) override;
    QWidget *createCaptureWidget(const QString &state, QWidget *parent) override;

private:
    // Fixture de captura (nunca se usa fuera de captureMode()).
    bool m_captureIsDefault = false;
    QList<DetectedBrowser> m_captureDetected;
};

#endif // MIGHTYTOOLS_LINKREDIRECTOR_MODULE_H
