#ifndef MIGHTYTOOLS_OPENINNUKEX_MODULE_H
#define MIGHTYTOOLS_OPENINNUKEX_MODULE_H

#include "app/Module.h"

// El objeto vivo de Open in NukeX (etapa 1: sin panel real todavia, ver createPanel()).
//
// Sin nada residente en Windows (plan 4.3): start()/stop() no registran ni un timer. El escaneo
// de versiones de Nuke (NukeScanner) corre recien cuando el panel lo pide, y el panel en si es
// etapa 2 — createPanel() hoy devuelve un placeholder explicito.
//
// En Windows el modo corto (main.cpp, plan 4.5) atiende el doble click en un `.nk` SIN construir
// este modulo: pasa directo por `ModuleDescriptor::runExternal`. `handleExternal()` solo importa
// para la app residente de mac (D-12 abierta: sin usuarios de mac todavia, pero el camino queda
// completo y coherente con el resto del repo).
class OpenInNukeXModule : public Module
{
    Q_OBJECT

public:
    explicit OpenInNukeXModule(ModuleContext &context);

    void start() override;
    void stop() override;

    ModuleStatus status() const override;

    QWidget *createPanel(QWidget *parent) override;

    ExternalResult handleExternal(const ExternalRequest &request) override;
};

#endif // MIGHTYTOOLS_OPENINNUKEX_MODULE_H
