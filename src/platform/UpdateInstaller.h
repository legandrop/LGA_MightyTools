#ifndef MIGHTYTOOLS_UPDATEINSTALLER_H
#define MIGHTYTOOLS_UPDATEINSTALLER_H

#include <QString>

// Ultimo paso de una actualizacion: instalar el paquete que UpdateService ya bajo y verifico
// (SHA-256). Es lo unico del updater que cambia por plataforma:
//  - Windows: el paquete es el instalador de Inno; se lanza y el instalador hace el resto.
//  - macOS: el paquete es un ZIP con el bundle; un script auxiliar espera a que la app cierre,
//    reemplaza el bundle instalado y la vuelve a abrir (platform/mac/UpdateHelperMac.h).
// En los dos casos, si launch() arranco, quien llama cierra la app.
namespace UpdateInstaller {

// Por que ESTA copia no se puede actualizar sola. Se pregunta antes de bajar nada.
enum class Blocker {
    None,
    DevelopmentCopy,    // corre desde un arbol de desarrollo: no se pisa el build
    MoveToApplications, // macOS: corre desde la imagen de disco o desde una copia de solo lectura
    FolderNotWritable,  // macOS: el usuario no puede escribir en la carpeta donde esta instalada
};
Blocker blocker();

struct Result {
    bool started = false;
    // Detalle tecnico para el cartel de error (sin traducir).
    QString detail;
};

// noticeTitle / noticeBody: el aviso que muestra el script de macOS si el reemplazo falla (ya
// traducido). Windows no los usa. En una corrida automatizada no lanza nada.
Result launch(const QString &packagePath, const QString &version, const QString &noticeTitle,
              const QString &noticeBody);

} // namespace UpdateInstaller

#endif // MIGHTYTOOLS_UPDATEINSTALLER_H
