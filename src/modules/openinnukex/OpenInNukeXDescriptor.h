#ifndef MIGHTYTOOLS_OPENINNUKEX_DESCRIPTOR_H
#define MIGHTYTOOLS_OPENINNUKEX_DESCRIPTOR_H

#include "app/Module.h"
#include "ui/HelpSection.h"

// El descriptor de "Open in NukeX" (plan 4.2, seccion 2; canvas de diseno, tarjeta "onx"). Se
// suma a ModuleRegistry::all() en src/app/ModuleRegistry.cpp.
ModuleDescriptor openInNukeXDescriptor();

// Seccion de la ayuda unica (canvas, seccion 6). Se suma a ModuleRegistry::helpProviders().
HelpSection openInNukeXHelp(const SettingsReader &value);

// La logica de runExternal(), compartida por dos caminos:
//  - ModuleDescriptor::runExternal: el modo corto de Windows (main.cpp resuelve esto ANTES de
//    la instancia unica, con o sin el modulo prendido, plan 4.5).
//  - OpenInNukeXModule::handleExternal: la app residente de mac con el modulo prendido.
// No depende de ningun estado del modulo vivo: solo de los campos de ExternalRequest.
ExternalResult openInNukeXRunExternal(const ExternalRequest &request);

#endif // MIGHTYTOOLS_OPENINNUKEX_DESCRIPTOR_H
