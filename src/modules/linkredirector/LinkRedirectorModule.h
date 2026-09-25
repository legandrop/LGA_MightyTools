#ifndef MIGHTYTOOLS_LINKREDIRECTOR_MODULE_H
#define MIGHTYTOOLS_LINKREDIRECTOR_MODULE_H

#include "app/Module.h"

// Modulo Link Redirector (etapa 1: logica sin panel). En Windows no tiene nada residente: cada link
// llega como un proceso corto que el host resuelve con ModuleDescriptor::runExternal, sin construir
// este objeto (plan 4.3, D-05). Este modulo solo existe mientras la herramienta esta PRENDIDA, para:
//  - registrarse como candidata a navegador al arrancar (start(), guardado por
//    ModuleContext::persistentRegistrationAllowed());
//  - en mac, la app residente lo usa para despachar los links que llegan como QFileOpenEvent
//    (handleExternal(), misma logica que runExternal via LinkRedirectorExternal::handle);
//  - mostrar en su fila si Windows/mac ya la tiene como navegador por defecto.
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
};

#endif // MIGHTYTOOLS_LINKREDIRECTOR_MODULE_H
