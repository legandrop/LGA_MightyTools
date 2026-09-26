#include "modules/folderswitch/UiaTimeouts.h"

#include <QDebug>

#include <uiautomation.h>

namespace UiaTimeouts {

void apply(IUIAutomation *automation)
{
    if (!automation) {
        return;
    }
    IUIAutomation2 *automation2 = nullptr;
    if (FAILED(automation->QueryInterface(IID_IUIAutomation2, reinterpret_cast<void **>(&automation2)))
        || !automation2) {
        // Windows 7: IUIAutomation2 no existe. Queda sin acotar, es el riesgo pendiente documentado
        // en el informe (no hay forma de fijar esto sin esa interfaz).
        qWarning() << "[folderSwitch] IUIAutomation2 no disponible: ConnectionTimeout/TransactionTimeout sin fijar";
        return;
    }
    // 500 ms de conexion: una app viva responde al primer contacto UIA en unos pocos ms incluso con
    // el proveedor recien arrancado en frio; medio segundo ya distingue eso de una app realmente sin
    // responder (que puede tardar varios segundos o no volver nunca).
    // 1000 ms de transaccion: una operacion normal (FindAll, SetValue) sobre un dialogo vivo tarda
    // unos pocos ms (ver FolderSwitchLogic::kSlowCallMs = 150 ms, el umbral de aviso); un segundo da
    // margen para una maquina momentaneamente ocupada sin dejar el hilo de UI de toda la app colgado
    // el tiempo que tarda un hang real.
    automation2->put_ConnectionTimeout(500);
    automation2->put_TransactionTimeout(1000);
    automation2->Release();
}

} // namespace UiaTimeouts
