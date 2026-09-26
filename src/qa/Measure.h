#ifndef MIGHTYTOOLS_MEASURE_H
#define MIGHTYTOOLS_MEASURE_H

#include <QStringList>

// Medicion de "lo apagado no consume nada" (plan 4.3). Corridas automatizadas: settings en memoria,
// atajos contados sin llamar al sistema, inyector en solo loguear, sin bandeja, sin updater ni red.
namespace Measure {

// --measure-cycles [N] [id]: puntos 1 y 2. Por cada herramienta (o solo `id`), N ciclos de prender,
// abrir su panel y apagar, con la ventana real armada (sin mostrarla). Imprime handles, hilos,
// GDI/USER y los QTimer/QThread de qApp antes y despues. Sale 0 si todo quedo en +-2.
int cycles(const QStringList &arguments);

// --measure-idle [segundos] [asentamiento]: puntos 3 y 4. La app entera (AppController en modo
// medicion) con todo apagado y la ventana cerrada; mide la memoria privada al arrancar y el CPU que
// gasta en reposo, contado despues de `asentamiento` segundos (60 por defecto), con una muestra por
// minuto.
int idle(const QStringList &arguments);

// --measure-fonts: el ancho que le da Qt a textos del canvas (QFontMetricsF::horizontalAdvance con
// la fuente de la hoja de estilo), al lado del ancho que midio Chrome en el canvas. Sin ventanas.
// Sirve para comparar la plataforma de Windows contra offscreen.
int fonts(const QStringList &arguments);

// --notify-preview: arma las notificaciones de la app SIN mostrarlas (SystemNotifier en modo
// automatizado: ni PowerShell ni hilo) y loguea titulo, texto y el icono elegido (PNG temporal con el
// frame mas grande del .ico y su tamano).
int notifyPreview(const QStringList &arguments);

} // namespace Measure

#endif // MIGHTYTOOLS_MEASURE_H
