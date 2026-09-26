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

} // namespace Measure

#endif // MIGHTYTOOLS_MEASURE_H
