#ifndef MIGHTYTOOLS_WINDOWFRAME_H
#define MIGHTYTOOLS_WINDOWFRAME_H

#include <QtGlobal>

class QWidget;

// Marco de la ventana principal, que dibuja su propia barra de titulo (TitleBar). Una
// implementacion por plataforma:
//  - Windows (platform/win/WindowFrameWin.cpp): una ventana Qt sin marco es un WS_POPUP, sin sombra,
//    sin esquinas redondeadas y sin minimizar desde la barra de tareas. Se le devuelven los estilos
//    de una ventana con titulo y WM_NCCALCSIZE deja el area no-cliente en cero. La que se estira
//    (`resizable`) responde WM_NCHITTEST con los bordes y las esquinas de una franja interior.
//  - macOS (platform/mac/WindowFrameMac.mm): la ventana sin marco ya tiene sombra; la que se estira
//    suma NSWindowStyleMaskResizable y macOS hace el resto (bordes, esquinas y cursores).
namespace WindowFrame {

// Franja interior que estira una ventana `resizable`, en pixeles a 96 ppp (toda la ventana es cliente,
// asi que la franja se superpone al contenido). A la derecha es de 2: justo el margen libre que deja la
// barra de desplazamiento vertical del QSS (`QScrollBar:vertical`, margen derecho 2), que va contra ese
// borde. Las esquinas toman un tramo mas largo de cada borde, para agarrarlas sin punteria.
constexpr int kResizeBorder = 5;
constexpr int kResizeBorderRight = 2;
constexpr int kResizeCorner = 14;

// Se llama una vez, la primera vez que se muestra la ventana (ya existe la ventana nativa). True si
// hizo falta y quedo aplicado: desde ahi la ventana pasa sus eventos nativos por handleNativeEvent.
bool apply(QWidget *window, bool resizable = false);
// True si el evento ya quedo atendido (resultado en *result).
bool handleNativeEvent(void *message, qintptr *result);

} // namespace WindowFrame

#endif // MIGHTYTOOLS_WINDOWFRAME_H
