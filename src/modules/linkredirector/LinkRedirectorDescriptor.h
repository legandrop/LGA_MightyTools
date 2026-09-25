#ifndef MIGHTYTOOLS_LINKREDIRECTOR_DESCRIPTOR_H
#define MIGHTYTOOLS_LINKREDIRECTOR_DESCRIPTOR_H

#include "app/Module.h"
#include "ui/HelpSection.h"

// Descriptor de Link Redirector (id "linkRedirector"). Sumado a ModuleRegistry::all().
ModuleDescriptor linkRedirectorDescriptor();

// Seccion de la ayuda unica (canvas, seccion 6). Sumada a ModuleRegistry::helpProviders().
HelpSection linkRedirectorHelp(const SettingsReader &value);

#endif // MIGHTYTOOLS_LINKREDIRECTOR_DESCRIPTOR_H
