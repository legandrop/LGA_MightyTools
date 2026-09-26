#ifndef MIGHTYTOOLS_SELFTEST_H
#define MIGHTYTOOLS_SELFTEST_H

// --self-test: la logica que no necesita pantalla, contra el codigo de produccion (no una replica).
// Cada guarda tiene su caso negativo. Corre con QCoreApplication. Sale 0 si todo paso.
//  - El host: prender y apagar deja el modulo nulo, los QTimer/QThread de qApp vuelven a los
//    iniciales, los atajos quedan en 0; choques de atajos entre dos modulos de prueba; el contexto
//    vive durante el destructor del modulo; la ventana escondida vuelve; persistentRegistration.
//  - El modo corto (ExternalDispatch) con Done, Pending, el tope de tiempo y NotMine.
//  - El selfTest() de cada herramienta registrada.
//  - Windows: el registro y --uninstall-cleanup sobre un hive privado (qa/RegistryHiveTest.h).
namespace SelfTest {
int run();
} // namespace SelfTest

#endif // MIGHTYTOOLS_SELFTEST_H
