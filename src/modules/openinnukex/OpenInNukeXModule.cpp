#include "modules/openinnukex/OpenInNukeXModule.h"

#include "app/ModuleContext.h"
#include "modules/openinnukex/OpenInNukeXDescriptor.h"
#include "modules/openinnukex/OpenInNukeXMessages.h"
#include "modules/openinnukex/OpenInNukeXPanel.h"

#ifdef Q_OS_WIN
#include "modules/openinnukex/win/WinFileAssociation.h"
#elif defined(Q_OS_MACOS)
#include "modules/openinnukex/mac/MacFileAssociation.h"
#endif

#include <QDialog>
#include <QWidget>

OpenInNukeXModule::OpenInNukeXModule(ModuleContext &context)
    : Module(context)
{
}

void OpenInNukeXModule::start()
{
    // Nada residente en Windows (plan 4.3): ni atajos, ni hooks, ni timers. El escaneo de
    // versiones y la lectura de estado del bridge corren recien cuando se abre el panel
    // (OpenInNukeXPanel se construye recien en createPanel()).
}

void OpenInNukeXModule::stop()
{
    // Idempotente y simetrico con start(): nada que soltar todavia.
}

ModuleStatus OpenInNukeXModule::status() const
{
    ModuleStatus result;

    // Captura (--ui-shot, --ui-probe): nunca se lee el sistema real. La fila de la barra lateral
    // tiene que coincidir con lo que applyCaptureState() le dejo al panel (mismo criterio que
    // OpenInNukeXPanel: "first-time" es la unica ficha "no asociada").
    if (context().captureMode()) {
        if (m_pendingCaptureState == QStringLiteral("old-client")) {
            result.tone = ModuleTone::Attention;
            result.text = QStringLiteral("Old client still installed");
            return result;
        }
        const bool associated = m_pendingCaptureState != QStringLiteral("first-time");
        result.tone = associated ? ModuleTone::Active : ModuleTone::Attention;
        result.text = associated ? QStringLiteral("Associated") : QStringLiteral("Not associated");
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
    auto *panel = new OpenInNukeXPanel(context(), parent);
    // En captura, applyCaptureState() se llamo ANTES de que el panel existiera (Module.h: el
    // arnes fija el estado y RECIEN DESPUES pide el panel); se aplica ahora que existe.
    if (context().captureMode() && !m_pendingCaptureState.isEmpty()
        && m_pendingCaptureState != QStringLiteral("launcher-notice")) {
        panel->applyCaptureState(m_pendingCaptureState);
    }
    m_panel = panel;
    connect(panel, &OpenInNukeXPanel::oldClientStateChanged, this, &Module::statusChanged);
    return panel;
}

ExternalResult OpenInNukeXModule::handleExternal(const ExternalRequest &request)
{
    // Misma logica que el modo corto de Windows (ver OpenInNukeXDescriptor::runExternal): la app
    // residente de mac la usa con el modulo prendido, sin salir del proceso al terminar.
    return openInNukeXRunExternal(request);
}

QStringList OpenInNukeXModule::captureStates() const
{
    // Los 11 estados del panel (OpenInNukeXPanel::captureStates()) mas "launcher-notice", que
    // arma createCaptureWidget() en vez de pasar por el panel.
    QStringList states = OpenInNukeXPanel::captureStates();
    states << QStringLiteral("launcher-notice");
    return states;
}

bool OpenInNukeXModule::applyCaptureState(const QString &state)
{
    if (!captureStates().contains(state)) {
        return false;
    }
    m_pendingCaptureState = state;
    // Si el panel YA existe (poco frecuente: normalmente esto llega antes de createPanel()),
    // se le aplica en el acto.
    if (m_panel && state != QStringLiteral("launcher-notice")) {
        return m_panel->applyCaptureState(state);
    }
    return true;
}

QWidget *OpenInNukeXModule::createCaptureWidget(const QString &state, QWidget *parent)
{
    if (state == QStringLiteral("launcher-notice")) {
        // Solo construccion (sin exec()/show(), Module.h): el mismo cartel que showLaunchNotice()
        // pero con el timer parado, para que la captura sea un instante fijo ("Closing in 3
        // seconds").
        return OpenInNukeXMessages::buildLaunchNoticeWidget(parent);
    }
    return nullptr; // el resto de los estados son del panel (createPanel()), no un widget aparte.
}
