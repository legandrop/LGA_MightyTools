#include "app/ModuleRegistry.h"

#include <QtGlobal>

#if defined(Q_OS_WIN)
#include "modules/folderswitch/FolderSwitchModule.h"
#endif

// Una linea por herramienta, en el orden de la lista de la ventana. Cada modulo declara su
// `<modulo>Descriptor()` en su carpeta de src/modules/.

namespace ModuleRegistry {

QList<ModuleDescriptor> all()
{
    QList<ModuleDescriptor> list;
    // Ej.: list << nukeShortcutsDescriptor();
#if defined(Q_OS_WIN)
    list << folderSwitchDescriptor();
#endif

#if defined(Q_OS_WIN)
    const int current = PlatformWindows;
#else
    const int current = PlatformMac;
#endif
    QList<ModuleDescriptor> available;
    for (const ModuleDescriptor &d : list) {
        if (d.platforms & current) {
            available << d;
        }
    }
    return available;
}

} // namespace ModuleRegistry
