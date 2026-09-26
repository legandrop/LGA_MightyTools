#include "platform/WindowActivation.h"

// En macOS no hace falta autorizar: la app residente se activa sola al mostrar su ventana.

namespace WindowActivation {

void allowAnyProcessToActivate()
{
}

} // namespace WindowActivation
