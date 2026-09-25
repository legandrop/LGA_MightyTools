#ifndef MIGHTYTOOLS_FOLDERSWITCH_WINDOWUTILS_H
#define MIGHTYTOOLS_FOLDERSWITCH_WINDOWUTILS_H

#include <windows.h>
#include <QString>

// Helpers de clasificacion de ventanas Win32: Explorer, XYplorer y file dialogs comunes (#32770).
// Copia de LGA_FolderSwitch (src/core/WindowUtils), sin cambios de logica: el modulo los usa para
// decidir cuando ofrecer/inyectar un path.
namespace WindowUtils {

// Explorer: clase "CabinetWClass".
bool isExplorerWindow(HWND hwnd);

// XYplorer: clase "ThunderRT6FormDC" O el exe del proceso es xyplorer.exe (case-insensitive). El
// chequeo por exe es el definitivo.
bool isXYplorerWindow(HWND hwnd);

// True si hwnd es un manager de archivos que sabemos resolver (Explorer o XYplorer).
bool isFileManagerWindow(HWND hwnd);

// File dialog comun de Windows: clase "#32770" y tiene un hijo/descendiente de clase
// "DUIViewWndClassName" o "SHELLDLL_DefView", o un hijo directo "ComboBoxEx32".
bool isFileDialogWindow(HWND hwnd);

// File dialog Qt puro (p.ej. Nuke): class name empieza con "Qt", tiene owner (GW_OWNER), y por UI
// Automation expone al menos un Edit y un boton de aceptar (Open/Save/Choose/Select/Abrir/Guardar).
// Sin hijos Win32 -> EnumChildWindows no sirve aca, por eso el chequeo real es via UIA (costoso, se
// cachea por HWND).
bool isQtFileDialog(HWND hwnd);

// Nombre de archivo (sin path) del ejecutable dueno de hwnd, en minusculas. QString vacio si no se
// pudo obtener.
QString processExeName(HWND hwnd);

} // namespace WindowUtils

#endif // MIGHTYTOOLS_FOLDERSWITCH_WINDOWUTILS_H
