#ifndef MIGHTYTOOLS_FOLDERSWITCH_LOGIC_H
#define MIGHTYTOOLS_FOLDERSWITCH_LOGIC_H

#include <QHash>
#include <QString>
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

// ------------------------------------------------------------------------------------------------
// Medicion de llamadas sincronas a COM/UI Automation (plan, seccion 11: "COM y UI Automation son
// sincronas en el hilo de UI... si alguna tarda, van con timeout o a un hilo"). Ahora que Folder
// Switch comparte proceso con las otras herramientas, una de estas llamadas trabada congela toda la
// app: se mide SIEMPRE (no solo con un flag de debug), para que quede en el log de Lega en uso real.
inline constexpr qint64 kSlowCallMs = 150; // aviso con qWarning a partir de aca

// Deja en el log "[folderSwitch] <operacion> <ms> ms", y ademas un qWarning si supero kSlowCallMs.
// Vive aca (no en WindowUtils/UiaSwitcher/FolderResolver) para que las cuatro llamadas que pide el
// encargo (WindowUtils, UiaSwitcher, FolderResolver, DialogSwitcher) logueen exactamente igual.
void logCallTiming(const char *operation, qint64 elapsedMs);

// ------------------------------------------------------------------------------------------------
// Cache por HWND de si una ventana es un dialogo Qt puro (WindowUtils::isQtFileDialog): el recorrido
// UIA (uiaLooksLikeFileDialog) no es gratis, y en el camino caliente de "cambio de ventana al
// frente" se pregunta por la MISMA ventana varias veces seguidas (alt-tab de ida y vuelta). Logica
// pura (sin HWND real, sin windows.h) para poder probarla en --self-test: la ventana la identifica
// una clave opaca (el HWND del llamador convertido a quintptr) y el reloj lo inyecta quien la usa.
class QtDialogCache
{
public:
    // ttlMs: cuanto tiempo se reutiliza un resultado sin volver a correr el FindAll descendente.
    // "Corto" a proposito: es solo para no repetir la MISMA pregunta en la MISMA rafaga de cambios
    // de foreground (alt-tab), no para asumir que un HWND nunca cambia de naturaleza.
    explicit QtDialogCache(qint64 ttlMs);

    // True si hay un valor vigente en `now` para `key`; lo deja en *outValue. No confundir con
    // "la ventana sigue existiendo": eso lo decide quien llama (IsWindow), invalidando con
    // invalidate() si el HWND se reciclo.
    bool lookup(quintptr key, qint64 now, bool *outValue) const;
    void store(quintptr key, bool value, qint64 now);
    void invalidate(quintptr key);
    void clear();
    int size() const { return m_entries.size(); }

private:
    struct Entry
    {
        bool value = false;
        qint64 timestampMs = 0;
    };
    QHash<quintptr, Entry> m_entries;
    qint64 m_ttlMs;
};

} // namespace FolderSwitchLogic

#endif // MIGHTYTOOLS_FOLDERSWITCH_LOGIC_H
