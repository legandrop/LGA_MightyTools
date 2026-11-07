#ifndef MIGHTYTOOLS_OVERLAYWINDOW_H
#define MIGHTYTOOLS_OVERLAYWINDOW_H

#include <QtGlobal>

class QScreen;
class QWidget;

// Ventanitas flotantes que nunca se activan (la del dictado). Una implementación por plataforma:
//  - Windows (platform/win/OverlayWindowWin.cpp): WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW. Ni mostrarla ni un click
//    la activan ni le sacan el foco a la app donde se escribe, y no aparece en la barra de tareas ni en Alt+Tab.
//  - macOS (platform/mac/OverlayWindowMac.mm): panel que no activa la app, que no se esconde cuando la app no
//    está activa y que se ve en todos los escritorios. El «nativeWindow» de mac es el PID de la app del frente
//    (abajo) y el número de su ventana de más arriba (arriba), como lo arma TextTyper::foregroundWindow.
namespace OverlayWindow {

// Con la ventana nativa ya creada (winId()) y antes de mostrarla. Fuera de la plataforma «windows»
// (las capturas offscreen) no hace nada.
void makeNonActivating(QWidget *window);

// La pantalla de Qt donde está una ventana nativa de otra app (la mayor parte de ella); nullptr si no se sabe.
QScreen *screenOf(quintptr nativeWindow);

} // namespace OverlayWindow

#endif // MIGHTYTOOLS_OVERLAYWINDOW_H
