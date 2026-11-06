#include "app/ModuleRegistry.h"
#include "modules/diskspace/DiskSpaceModule.h"
#include "modules/linkredirector/LinkRedirectorDescriptor.h"
#include "modules/nukeshortcuts/NukeShortcutsModule.h"
#include "modules/windowlayouts/WindowLayoutsModule.h"
#include "modules/apptoggle/AppToggleModule.h"
#include "modules/meetframe/MeetFrameModule.h"

#include "modules/openinnukex/OpenInNukeXDescriptor.h"

#include <QtGlobal>

#include <algorithm>

#if defined(Q_OS_WIN)
#include "modules/desktopicons/DesktopIconsModule.h" // solo Windows
#include "modules/dictation/DictationModule.h"
#include "modules/folderswitch/FolderSwitchModule.h"
#include "modules/mousebuttons/MouseButtonsModule.h"
#elif defined(Q_OS_MACOS)
#include "modules/folderswitch/mac/FolderSwitchMacModule.h" // D-40: su propia implementacion de mac
#endif

// Una linea por herramienta (all() las devuelve en orden alfabetico). Cada modulo declara su
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
    list << windowLayoutsDescriptor();
    list << appToggleDescriptor();
    list << meetFrameDescriptor();
#if defined(Q_OS_WIN)
    list << mouseButtonsDescriptor();
    list << dictationDescriptor();
    list << desktopIconsDescriptor();
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
    // Orden alfabetico por el nombre visible, sin distinguir mayusculas (D-73): lo siguen el indice, la
    // bandeja, General y el arranque. Los rangos de ids de los atajos salen de esta posicion, pero no se
    // guardan en ningun lado.
    std::stable_sort(available.begin(), available.end(), [](const ModuleDescriptor &a, const ModuleDescriptor &b) {
        return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
    });
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
    providers.insert(QStringLiteral("windowLayouts"), &windowLayoutsHelp);
    providers.insert(QStringLiteral("appToggle"), &appToggleHelp);
    providers.insert(QStringLiteral("meetFrame"), &meetFrameHelp);
#if defined(Q_OS_WIN)
    providers.insert(QStringLiteral("mouseButtons"), &mouseButtonsHelp);
    providers.insert(QStringLiteral("dictation"), &dictationHelp);
    providers.insert(QStringLiteral("desktopIcons"), &desktopIconsHelp);
#endif
    return providers;
}

} // namespace ModuleRegistry
