#ifndef MIGHTYTOOLS_REGISTRYHIVETEST_H
#define MIGHTYTOOLS_REGISTRYHIVETEST_H

#include <QString>

#include <functional>

// Self-test del registro y de --uninstall-cleanup (solo Windows) sobre un HIVE PRIVADO, nunca sobre
// el HKCU real (unica excepcion auditada a "nunca registrar desde una prueba automatizada"):
//  1. RegLoadAppKey sobre un archivo de nombre unico en %TEMP% (y se borran los viejos).
//  2. RegOverridePredefKey(HKEY_CURRENT_USER, hive). ANTES de escribir nada por HKEY_CURRENT_USER
//     se prueba el aislamiento: una marca escrita por el handle del hive se tiene que leer por
//     HKEY_CURRENT_USER (lectura, sin riesgo); recien entonces un valor escrito por
//     HKEY_CURRENT_USER tiene que aparecer en el hive y NO en el HKCU real (RegOpenCurrentUser). Si
//     algo falla, se corta toda la prueba sin escribir nada mas.
//  3. La redireccion se deshace con una guarda de alcance; sin SHChangeNotify (supresion de
//     RegistryHelper), sin selector ni Ajustes; al final se borran el archivo y sus .LOG1/.LOG2.
// Siembra lo propio con las funciones REALES de registro y lo ajeno por el handle del hive, corre la
// limpieza REAL (UninstallCleanup::run) y compara el hive byte a byte contra la foto previa.
namespace RegistryHiveTest {
void run(const std::function<void(bool ok, const QString &what)> &check);
} // namespace RegistryHiveTest

#endif // MIGHTYTOOLS_REGISTRYHIVETEST_H
