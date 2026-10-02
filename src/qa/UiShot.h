#ifndef MIGHTYTOOLS_UISHOT_H
#define MIGHTYTOOLS_UISHOT_H

#include <QStringList>

// Captura de QA sin escritorio: --ui-shot <estado> <out.png> [--dpr N].
// Arma la ventana real con datos de prueba y la dibuja a un PNG con QWidget::render, sin
// mostrar nada, sin bandeja, sin hotkey, sin updater y sin tocar QSettings ni el registro.
// Devuelve el codigo de salida del proceso.
int runUiShot(const QStringList &args);

// Sonda de comportamiento sin escritorio: --ui-probe <caso>. Arma widgets reales en la plataforma
// offscreen de Qt, les manda teclas y clicks como eventos internos (nunca al sistema) y verifica el
// resultado. Imprime una linea por chequeo y sale 0 si todo paso.
//  - threshold-focus: el campo del umbral de un disco no toma el teclado al abrir la ventana, y lo
//    suelta con Enter (guardando), con Escape (sin guardar) y con un click afuera (guardando).
//  - cleanup-sort: la ventana de Disk Space se estira (minimo, barra de abajo entera al minimo en los dos
//    idiomas) y sus tres listas se ordenan con un click en el titulo de una columna.
int runUiProbe(const QStringList &args);

#endif // MIGHTYTOOLS_UISHOT_H
