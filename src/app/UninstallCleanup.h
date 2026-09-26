#ifndef MIGHTYTOOLS_UNINSTALLCLEANUP_H
#define MIGHTYTOOLS_UNINSTALLCLEANUP_H

#include "app/Module.h"

#include <QList>
#include <QStringList>

// --uninstall-cleanup: lo que la app escribio en el sistema y es SUYO se borra antes de que el
// desinstalador borre los archivos (lo llama el .iss desde CurUninstallStepChanged(usUninstall)).
//
// Modo corto de main: QCoreApplication, sin bandeja, sin ventana, sin instancia unica, sin
// registrarse en el registro LGA y sin dialogos. Cada herramienta aporta su limpieza con
// releaseSystem() de su DESCRIPTOR (funcion estatica: lo apagado no consume; ningun modulo se
// construye), y la entrada de inicio con el sistema la limpia AutoStart::removeIfOwned().
//
// Solo se borra lo que es de ESTE exe por CONTENIDO (el comando apunta a este exe, el valor apunta a
// una clave propia). Lo de otra copia, del cliente viejo o de otra app queda intacto. No toca
// UserChoice de http/https (UCPD lo protege; Windows cae solo al siguiente navegador) ni
// ApplicationAssociationToasts ni HKCU\Software\QtProject (lo comparten todas las apps Qt).
namespace UninstallCleanup {

struct Report
{
    int failures = 0;  ///< pasos que no pudieron borrar algo propio
    QStringList lines; ///< una linea por paso, para stdout y el log
};

// La limpieza real, sobre HKEY_CURRENT_USER (en el self-test, el hive privado redirigido).
Report run(const QList<ModuleDescriptor> &descriptors);

// La entrada de main: corre run() sobre ModuleRegistry::all(), imprime cada linea en stdout y
// devuelve el codigo de salida (0 todo ok, 1 algun paso fallo).
int runFromCommandLine();

} // namespace UninstallCleanup

#endif // MIGHTYTOOLS_UNINSTALLCLEANUP_H
