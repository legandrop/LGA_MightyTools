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
//  - macOS (platform/mac/WindowFrameMac.mm): la ventana sin marco es una NSWindow «borderless», con
//    sombra pero con las esquinas RECTAS. prepare() la vuelve translucida y apply() recorta la capa
//    de su vista con el radio de las ventanas del sistema (16 en macOS 26, 10 antes); la sombra sigue
//    al recorte. La que se estira suma NSWindowStyleMaskResizable y macOS hace el resto (bordes,
//    esquinas y cursores).
namespace WindowFrame {

// Se llama en el constructor, despues de setWindowFlags y ANTES de que exista la ventana nativa: en
// mac el formato con alfa (WA_TranslucentBackground) se fija al crearla, y una ventana translucida
// deja de pintar el fondo del QSS de su raiz (WA_NoSystemBackground): lo que no tape un hijo con
// fondo propio lo tiene que pintar ella. Tambien instala, una sola vez, el monitor de mouseDown que
// usa startDrag(). Solo con la plataforma cocoa: las capturas offscreen quedan como estaban.
void prepare(QWidget *window);

// Arrastre por la barra de titulo propia, llamado desde TitleBar::mousePressEvent. En macOS Qt entrega
// el click DIFERIDO y startSystemMove() usa el evento actual de AppKit, que a veces ya no era el
// mouseDown: la ventana no se movia. Aca se arrastra con el ultimo mouseDown real, que guarda un
// monitor de la app (sin tragarse nada: Qt ve el click como siempre). True si se ocupo; false en
// Windows y si no hay mouseDown de esa ventana, y ahi va startSystemMove().
bool startDrag(QWidget *window);

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
