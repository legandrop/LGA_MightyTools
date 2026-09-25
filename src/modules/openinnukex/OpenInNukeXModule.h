#ifndef MIGHTYTOOLS_OPENINNUKEX_MODULE_H
#define MIGHTYTOOLS_OPENINNUKEX_MODULE_H

#include "app/Module.h"

#include <QPointer>

class OpenInNukeXPanel;

// El objeto vivo de Open in NukeX.
//
// Sin nada residente en Windows (plan 4.3): start()/stop() no registran ni un timer. El escaneo
// de versiones de Nuke (NukeScanner) lo dispara OpenInNukeXPanel al construirse, no start(): "el
// escaneo arranca al construir el panel", nunca al prender el modulo.
//
// En Windows el modo corto (main.cpp, plan 4.5) atiende el doble click en un `.nk` SIN construir
// este modulo: pasa directo por `ModuleDescriptor::runExternal`. `handleExternal()` solo importa
// para la app residente de mac (D-12 abierta: sin usuarios de mac todavia, pero el camino queda
// completo y coherente con el resto del repo).
class OpenInNukeXModule : public Module
{
    Q_OBJECT

public:
    explicit OpenInNukeXModule(ModuleContext &context);

    void start() override;
    void stop() override;

    ModuleStatus status() const override;

    QWidget *createPanel(QWidget *parent) override;

    ExternalResult handleExternal(const ExternalRequest &request) override;

    // Captura (--ui-shot <id>:<estado>): 11 estados del panel (canvas, secciones 2 y 3) mas
    // "launcher-notice", que no pasa por el panel (ver createCaptureWidget()).
    QStringList captureStates() const override;
    bool applyCaptureState(const QString &state) override;
    QWidget *createCaptureWidget(const QString &state, QWidget *parent) override;

private:
    QString m_pendingCaptureState;      ///< aplicado recien cuando createPanel() construye el panel
    QPointer<OpenInNukeXPanel> m_panel; ///< el mismo que devuelve createPanel(), sin dueño
};

#endif // MIGHTYTOOLS_OPENINNUKEX_MODULE_H
