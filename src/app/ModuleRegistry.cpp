#include "app/ModuleRegistry.h"
#include "modules/linkredirector/LinkRedirectorDescriptor.h"

#include <QtGlobal>

// Una linea por herramienta, en el orden de la lista de la ventana. Cada modulo declara su
// `<modulo>Descriptor()` en su carpeta de src/modules/.

namespace ModuleRegistry {

QList<ModuleDescriptor> all()
{
    QList<ModuleDescriptor> list;
    // Ej.: list << nukeShortcutsDescriptor();
    list << linkRedirectorDescriptor();

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

QHash<QString, HelpProvider> helpProviders()
{
    QHash<QString, HelpProvider> providers;
    // Ej.: providers.insert(QStringLiteral("nukeShortcuts"), &nukeShortcutsHelp);
    return providers;
}

} // namespace ModuleRegistry
