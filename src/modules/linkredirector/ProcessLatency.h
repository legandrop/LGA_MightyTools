#ifndef MIGHTYTOOLS_LINKREDIRECTOR_PROCESSLATENCY_H
#define MIGHTYTOOLS_LINKREDIRECTOR_PROCESSLATENCY_H

#include <QtGlobal>

// Plan, fase 5: "del inicio del proceso al lanzamiento del navegador tarda como Link Redirector mas
// 50 ms a lo sumo, medido en el log". Windows lanza un proceso CORTO por cada link (plan 4.5), asi
// que "el inicio del proceso" es el dato que importa, no un QElapsedTimer arrancado a mitad de main
// (que ya se perderia el costo de cargar el CRT y las DLLs de Qt). Por eso esto usa la hora de
// creacion del proceso que ya guarda el sistema operativo, sin que main.cpp tenga que arrancar nada
// antes: cero cambios en main.cpp.
//
// Por plataforma: Windows via GetProcessTimes (win/ProcessLatencyWin.cpp); mac via
// proc_pidinfo(PROC_PIDTBSDINFO) (mac/ProcessLatencyMac.cpp, sin compilar en esta maquina).
namespace LinkRedirectorProcessLatency {

// Milisegundos desde que el sistema operativo creo este proceso hasta ahora. -1 si la plataforma no
// pudo leerlo (nunca se loguea ese caso como si fuera una medicion valida).
qint64 elapsedMsSinceStart();

} // namespace LinkRedirectorProcessLatency

#endif // MIGHTYTOOLS_LINKREDIRECTOR_PROCESSLATENCY_H
