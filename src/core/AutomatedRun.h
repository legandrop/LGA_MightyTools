#ifndef MIGHTYTOOLS_AUTOMATEDRUN_H
#define MIGHTYTOOLS_AUTOMATEDRUN_H

#include <QString>

// Interruptor de proceso para las corridas automatizadas (--self-test, --ui-shot, --ui-probe,
// --simulate-action, las mediciones). Lo prende main() antes que nada, y lo miran las funciones de la
// capa de plataforma que BORRAN o abren algo del sistema: asi la regla "en una corrida automatizada
// nunca se ejecuta una accion real" no depende de que cada pantalla se acuerde de preguntarlo.
//
// Unica excepcion (auditada 2026-09-30, como el hive privado del registro): el self-test de la limpieza
// de discos borra DENTRO de una carpeta de pruebas que el mismo crea en %TEMP% y registra con
// setSandbox(). Fuera de ella, en una corrida automatizada no se borra nada.
namespace AutomatedRun {

void enable();
bool active();

// Ruta canonica de la carpeta de pruebas. Vacia: en ninguna parte.
void setSandbox(const QString &canonicalDir);
QString sandbox();

// Si esa ruta se puede modificar ahora: siempre fuera de una corrida automatizada; en una, solo si
// esta DENTRO de la carpeta de pruebas (nunca la carpeta misma ni lo que la contiene).
bool mayModify(const QString &path);

} // namespace AutomatedRun

#endif // MIGHTYTOOLS_AUTOMATEDRUN_H
