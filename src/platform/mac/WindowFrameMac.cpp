#include "platform/WindowFrame.h"

// En macOS la ventana sin marco ya tiene sombra y esquinas del sistema: no hay nada que ajustar.

namespace WindowFrame {

bool apply(QWidget *)
{
    return false;
}

bool handleNativeEvent(void *, qintptr *)
{
    return false;
}

} // namespace WindowFrame
