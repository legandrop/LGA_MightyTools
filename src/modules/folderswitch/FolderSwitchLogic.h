#ifndef MIGHTYTOOLS_FOLDERSWITCH_LOGIC_H
#define MIGHTYTOOLS_FOLDERSWITCH_LOGIC_H

#include <QtGlobal>

// Reglas de decision del auto-switch, separadas de FolderSwitchModule para poder probarlas por
// --self-test sin HWNDs ni un ModuleContext real. Mismos valores y misma logica que
// TrayController::onForegroundChanged en LGA_FolderSwitch (src/tray/TrayController.cpp).
namespace FolderSwitchLogic {

// Comportamientos configurables sin control visible (Docs/Inventario_Opciones.md, "Folder Switch"):
// cuanto dura fresco el ultimo manager visto, y el delay antes de inyectar el path.
inline constexpr qint64 kManagerFreshnessMs = 60000; // 60 s
inline constexpr int kSwitchDelayMs = 200;

// True si `nowMs - lastSeenMs` sigue dentro de la ventana de frescura.
bool isManagerFresh(qint64 lastSeenMs, qint64 nowMs);

// Decide si, al ver un file dialog en foreground, corresponde inyectarle el path del ultimo manager
// visto. Las cinco condiciones son independientes entre si (todas tienen que darse):
//  - autoSwitchOn: el checkbox "Switch automatically" (autoSwitch en el .ini).
//  - masterOn: el interruptor general del modulo (enabled).
//  - managerFresh: el ultimo manager visto sigue dentro de kManagerFreshnessMs.
//  - isPendingReturn: el usuario vino de ESTE dialogo antes de pasar por el manager (evita disparar
//    en un dialogo distinto que tambien haya quedado abierto).
//  - alreadySwitchedThisDialog: ya se le inyecto a este mismo HWND, para no repetir en cada evento de
//    foreground sobre el mismo dialogo.
bool shouldAutoSwitch(bool autoSwitchOn, bool masterOn, bool managerFresh, bool isPendingReturn,
                      bool alreadySwitchedThisDialog);

} // namespace FolderSwitchLogic

#endif // MIGHTYTOOLS_FOLDERSWITCH_LOGIC_H
