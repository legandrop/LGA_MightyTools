#ifndef MIGHTYTOOLS_FOLDERSWITCH_UIASWITCHER_H
#define MIGHTYTOOLS_FOLDERSWITCH_UIASWITCHER_H

#include <QString>

#include <windows.h>

// Inyecta un path en dialogos de archivo Qt puro (p.ej. Nuke), que no tienen hijos Win32 y por lo
// tanto son invisibles para DialogSwitcher. Usa UI Automation (IUIAutomation) para encontrar el Edit
// de path y el boton de aceptar, y ValuePattern::SetValue / una tecla real para operarlos. Copia de
// LGA_FolderSwitch (src/core/UiaSwitcher), sin cambios de logica.
namespace UiaSwitcher {

// Requiere que COM ya este inicializado (CoInitializeEx) en el hilo actual. Devuelve false solo si
// no se pudo crear IUIAutomation, no se encontro el elemento del dialogo, o no se encontro ningun
// Edit. Si se escribio el valor pero no se encontro boton para invocar, igual devuelve true
// (best-effort).
bool switchQtDialog(HWND dlg, const QString &folder);

} // namespace UiaSwitcher

#endif // MIGHTYTOOLS_FOLDERSWITCH_UIASWITCHER_H
