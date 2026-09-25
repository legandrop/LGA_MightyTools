#ifndef MIGHTYTOOLS_MODULEREGISTRY_H
#define MIGHTYTOOLS_MODULEREGISTRY_H

#include "app/Module.h"
#include "ui/HelpSection.h"

#include <QHash>
#include <QList>

#include <functional>

// Las herramientas que conoce la app, en el orden de la lista de la ventana. Cada modulo expone una
// funcion `<modulo>Descriptor()` en su carpeta de src/modules/; sumar una herramienta es sumar una
// linea en ModuleRegistry.cpp. Las que no corren en esta plataforma no se devuelven.
namespace ModuleRegistry {
QList<ModuleDescriptor> all();

// Seccion de la ayuda unica de cada herramienta, por id (canvas, seccion 6). Funcion estatica del
// modulo registrada al lado de su descriptor: lee su seccion de settings.ini sin construirlo (los
// atajos configurados). Una herramienta sin entrada aca muestra su descripcion.
using HelpProvider = std::function<HelpSection(const SettingsReader &value)>;
QHash<QString, HelpProvider> helpProviders();
} // namespace ModuleRegistry

#endif // MIGHTYTOOLS_MODULEREGISTRY_H
