#include "modules/openinnukex/OpenInNukeXModule.h"

#include "app/ModuleContext.h"
#include "modules/openinnukex/OpenInNukeXDescriptor.h"

#ifdef Q_OS_WIN
#include "modules/openinnukex/win/WinFileAssociation.h"
#elif defined(Q_OS_MACOS)
#include "modules/openinnukex/mac/MacFileAssociation.h"
#endif

#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

OpenInNukeXModule::OpenInNukeXModule(ModuleContext &context)
    : Module(context)
{
}

void OpenInNukeXModule::start()
{
    // Nada residente en Windows (plan 4.3): ni atajos, ni hooks, ni timers. El escaneo de
    // versiones y la lectura de estado del bridge corren recien cuando se abre el panel
    // (createPanel(), etapa 2).
}

void OpenInNukeXModule::stop()
{
    // Idempotente y simetrico con start(): nada que soltar todavia.
}

ModuleStatus OpenInNukeXModule::status() const
{
    ModuleStatus result;

    // Captura (--ui-shot, --ui-probe): nunca se lee el sistema real. Sin captureStates() propios
    // todavia (etapa 2), un estado neutro alcanza.
    if (context().captureMode()) {
        result.tone = ModuleTone::Attention;
        result.text = QStringLiteral("Not associated");
        return result;
    }

#ifdef Q_OS_WIN
    if (WinFileAssociation::isOldClientInstalled()) {
        result.tone = ModuleTone::Attention;
        result.text = QStringLiteral("Old client still installed");
        return result;
    }
    const bool associated = WinFileAssociation::isNkAssociatedWithUs();
#elif defined(Q_OS_MACOS)
    const bool associated = MacFileAssociation::isDefaultNkHandler();
#else
    const bool associated = false;
#endif

    if (associated) {
        result.tone = ModuleTone::Active;
        result.text = QStringLiteral("Associated");
    } else {
        result.tone = ModuleTone::Attention;
        result.text = QStringLiteral("Not associated");
    }
    return result;
}

QWidget *OpenInNukeXModule::createPanel(QWidget *parent)
{
    // Placeholder explicito (encargo, etapa 1): el panel real (File Association, Preferred Nuke
    // Version, Nuke Bridge — inventario "LGA Open in NukeX") es etapa 2.
    auto *panel = new QWidget(parent);
    auto *layout = new QVBoxLayout(panel);
    auto *label = new QLabel(QStringLiteral("Open in NukeX panel: coming in stage 2."), panel);
    label->setWordWrap(true);
    layout->addWidget(label);
    layout->addStretch();
    return panel;
}

ExternalResult OpenInNukeXModule::handleExternal(const ExternalRequest &request)
{
    // Misma logica que el modo corto de Windows (ver OpenInNukeXDescriptor::runExternal): la app
    // residente de mac la usa con el modulo prendido, sin salir del proceso al terminar.
    return openInNukeXRunExternal(request);
}
