#ifndef MIGHTYTOOLS_SCREENINFO_H
#define MIGHTYTOOLS_SCREENINFO_H

#include <QSize>

// La pantalla principal ANTES de que exista la QApplication (Qt todavia no sabe nada de pantallas): la
// usa el tamano de la interfaz (core/UiScale.h) para no elegir uno con el que la ventana no entre.
//  - Windows (platform/win/ScreenInfoWin.cpp): area de trabajo (SPI_GETWORKAREA) pasada a pixeles
//    logicos con el DPI de la pantalla. Antes de la app el proceso todavia no declaro su DPI: Windows
//    devuelve las dos cosas en la misma escala y la cuenta da lo mismo que veria Qt con factor 1.
//  - macOS (platform/mac/ScreenInfoMac.mm): visibleFrame de la primera pantalla, ya en puntos.
namespace ScreenInfo {

// Area util de la pantalla principal (sin barra de tareas, menu ni Dock) en pixeles logicos del
// sistema, sin el tamano de interfaz de la app. QSize() si no se pudo leer.
QSize primaryAvailableSize();

} // namespace ScreenInfo

#endif // MIGHTYTOOLS_SCREENINFO_H
