#ifndef MIGHTYTOOLS_MODULEREGISTRY_H
#define MIGHTYTOOLS_MODULEREGISTRY_H

#include "app/Module.h"

#include <QList>

// Las herramientas que conoce la app, en el orden de la lista de la ventana. Cada modulo expone una
// funcion `<modulo>Descriptor()` en su carpeta de src/modules/; sumar una herramienta es sumar una
// linea en ModuleRegistry.cpp. Las que no corren en esta plataforma no se devuelven.
namespace ModuleRegistry {
QList<ModuleDescriptor> all();
} // namespace ModuleRegistry

#endif // MIGHTYTOOLS_MODULEREGISTRY_H
