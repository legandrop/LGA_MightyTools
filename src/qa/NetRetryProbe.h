#ifndef MIGHTYTOOLS_NETRETRYPROBE_H
#define MIGHTYTOOLS_NETRETRYPROBE_H

#include <QStringList>

// --ui-probe net-retry [--work <carpeta>]: los reintentos de red (core/LgaNetRetry.h) del updater
// (chequeo del manifiesto y descarga del instalador) y de la descarga del dictado (solo Windows), contra
// un servidor HTTP propio en 127.0.0.1 que corta, contesta 404 o 503, o acepta Range segun el caso (y uno
// segundo en ::1, el loopback de IPv6, al que redirige para probar la cuenta por servidor). Nunca
// sale a la red, nunca ejecuta un instalador (AutomatedRun) y escribe solo en su carpeta: un QTemporaryDir
// o, con --work, una subcarpeta nueva por corrida. Imprime una linea por chequeo y sale 0 si todo paso.
int runNetRetryProbe(const QStringList &args);

#endif // MIGHTYTOOLS_NETRETRYPROBE_H
