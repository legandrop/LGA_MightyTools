#include "modules/folderswitch/UiaTimeouts.h"

#include <QDebug>

#include <uiautomation.h>

namespace UiaTimeouts {

HRESULT createAutomation(IUIAutomation **outAutomation)
{
    if (!outAutomation) {
        return E_POINTER;
    }
    *outAutomation = nullptr;

    // CLSID_CUIAutomation8 es el que expone IUIAutomation2..6 por QueryInterface (Windows 8+); el
    // viejo CLSID_CUIAutomation nunca los da, sea cual sea la version de Windows.
    HRESULT hr = CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER, IID_IUIAutomation,
                                  reinterpret_cast<void **>(outAutomation));
    if (SUCCEEDED(hr) && *outAutomation) {
        return hr;
    }

    qWarning() << "[folderSwitch] CLSID_CUIAutomation8 no disponible (hr=" << hr
               << "), cae a CLSID_CUIAutomation: sin ConnectionTimeout/TransactionTimeout configurables";
    return CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_IUIAutomation,
                            reinterpret_cast<void **>(outAutomation));
}

void apply(IUIAutomation *automation)
{
    if (!automation) {
        return;
    }
    IUIAutomation2 *automation2 = nullptr;
    if (FAILED(automation->QueryInterface(IID_IUIAutomation2, reinterpret_cast<void **>(&automation2)))
        || !automation2) {
        // La instancia vino de CLSID_CUIAutomation (fallback de createAutomation) o de un Windows
        // anterior a la 8: IUIAutomation2 no existe. Queda sin acotar, es el riesgo pendiente
        // documentado en el informe (no hay forma de fijar esto sin esa interfaz).
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
