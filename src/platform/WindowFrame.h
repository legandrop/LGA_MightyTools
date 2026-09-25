#ifndef MIGHTYTOOLS_WINDOWFRAME_H
#define MIGHTYTOOLS_WINDOWFRAME_H

#include <QtGlobal>

class QWidget;

// Marco de la ventana principal, que dibuja su propia barra de titulo (TitleBar). Una
// implementacion por plataforma:
//  - Windows (platform/win/WindowFrameWin.cpp): una ventana Qt sin marco es un WS_POPUP, sin sombra,
//    sin esquinas redondeadas y sin minimizar desde la barra de tareas. Se le devuelven los estilos
//    de una ventana con titulo y WM_NCCALCSIZE deja el area no-cliente en cero.
//  - macOS (platform/mac/WindowFrameMac.cpp): nada; la ventana sin marco ya tiene sombra.
namespace WindowFrame {

// Se llama una vez, la primera vez que se muestra la ventana (ya existe la ventana nativa). True si
// hizo falta y quedo aplicado: desde ahi la ventana pasa sus eventos nativos por handleNativeEvent.
bool apply(QWidget *window);
// True si el evento ya quedo atendido (resultado en *result).
bool handleNativeEvent(void *message, qintptr *result);

} // namespace WindowFrame

#endif // MIGHTYTOOLS_WINDOWFRAME_H
