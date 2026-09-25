#ifndef MIGHTYTOOLS_FOLDERSWITCH_FOLDERRESOLVER_H
#define MIGHTYTOOLS_FOLDERSWITCH_FOLDERRESOLVER_H

#include <QString>

#include <windows.h>

// Resuelve la carpeta activa de una ventana de Explorer o XYplorer. Copia de LGA_FolderSwitch
// (src/core/FolderResolver), sin cambios de logica. COM (modo apartment) lo inicializa `main`, una
// sola vez para todo el proceso (plan 4.4): este modulo no lo toca.
namespace FolderResolver {

// Explorer via COM (IShellWindows/IWebBrowser2). QString() si no se pudo resolver (carpeta virtual,
// LocationURL vacio, etc.).
QString resolveExplorerPath(HWND hwnd);

// XYplorer via titulo de ventana (formato "<path> - XYplorer ...").
QString resolveXYplorerPath(HWND hwnd);

} // namespace FolderResolver

#endif // MIGHTYTOOLS_FOLDERSWITCH_FOLDERRESOLVER_H
