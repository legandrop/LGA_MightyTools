#ifndef MIGHTYTOOLS_FOLDERSWITCH_DIALOGSWITCHER_H
#define MIGHTYTOOLS_FOLDERSWITCH_DIALOGSWITCHER_H

#include <QString>

#include <windows.h>

// Inyecta un path en un file dialog comun de Windows (tecnica "QuickSwitch"). Copia de
// LGA_FolderSwitch (src/core/DialogSwitcher), sin cambios de logica.
namespace DialogSwitcher {

// Ubica el edit de nombre de archivo del dialogo, le pone el path, dispara IDOK y restaura
// (best-effort) el texto original si no era un path absoluto. Devuelve false si no se encontro el
// control de edit.
bool switchDialog(HWND dlg, const QString &folder);

} // namespace DialogSwitcher

#endif // MIGHTYTOOLS_FOLDERSWITCH_DIALOGSWITCHER_H
