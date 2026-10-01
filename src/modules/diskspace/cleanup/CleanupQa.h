#ifndef MIGHTYTOOLS_CLEANUPQA_H
#define MIGHTYTOOLS_CLEANUPQA_H

#include <QString>
#include <QStringList>

#include <functional>

// QA de la limpieza de discos: el self-test (sin pantalla, sobre carpetas de prueba propias) y las
// acciones de --simulate-action, que leen el disco real pero NUNCA borran nada.
namespace CleanupQa {

// --self-test: aritmetica del arbol, motor, guardas del borrado, reglas y resumenes.
void selfTest(const std::function<void(bool ok, const QString &what)> &check);
// La parte que BORRA, sobre una carpeta de pruebas propia (CleanupSelfTest.cpp). La llama selfTest.
void selfTestSandbox(const std::function<void(bool ok, const QString &what)> &check);

// --simulate-action diskSpace:<accion> [args]. Devuelve 2 si no conoce la accion.
//   scan <raiz>          escanea el volumen e imprime tiempos, totales y lo mas pesado
//   cleanup-plan <raiz>  imprime que limpiaria cada regla en ese volumen, con su peso (solo lectura)
//   cleanup-export <raiz> <salida.md>  escribe en un archivo NUEVO lo que "Export for AI..." daria por lo
//                        tildado en ese volumen (solo lee el disco; lo unico que escribe es esa salida)
int simulate(const QString &action, const QStringList &args);

} // namespace CleanupQa

#endif // MIGHTYTOOLS_CLEANUPQA_H
