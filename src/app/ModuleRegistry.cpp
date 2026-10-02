#include "app/ModuleRegistry.h"
#include "modules/diskspace/DiskSpaceModule.h"
#include "modules/linkredirector/LinkRedirectorDescriptor.h"
#include "modules/nukeshortcuts/NukeShortcutsModule.h"

#include "modules/openinnukex/OpenInNukeXDescriptor.h"

#include <QtGlobal>

#if defined(Q_OS_WIN)
#include "modules/folderswitch/FolderSwitchModule.h"
#elif defined(Q_OS_MACOS)
#include "modules/folderswitch/mac/FolderSwitchMacModule.h" // D-40: su propia implementacion de mac
#endif

// Una linea por herramienta, en el orden de la lista de la ventana. Cada modulo declara su
// `<modulo>Descriptor()` en su carpeta de src/modules/.

namespace ModuleRegistry {

QList<ModuleDescriptor> all()
{
    QList<ModuleDescriptor> list;
    // Ej.: list << nukeShortcutsDescriptor();
    list << openInNukeXDescriptor();
    list << nukeShortcutsDescriptor();
    list << diskSpaceDescriptor();
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    list << folderSwitchDescriptor();
#endif
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
    providers.insert(QStringLiteral("nukeShortcuts"), &nukeShortcutsHelp);
    providers.insert(QStringLiteral("diskSpace"), &diskSpaceHelp);
    providers.insert(QStringLiteral("openInNukeX"), &openInNukeXHelp);
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    providers.insert(QStringLiteral("folderSwitch"), &folderSwitchHelp);
#endif
    providers.insert(QStringLiteral("linkRedirector"), &linkRedirectorHelp);
    return providers;
}

} // namespace ModuleRegistry
