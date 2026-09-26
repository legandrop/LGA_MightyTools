#ifndef MIGHTYTOOLS_FOLDERSWITCH_UIATIMEOUTS_H
#define MIGHTYTOOLS_FOLDERSWITCH_UIATIMEOUTS_H

struct IUIAutomation;

// Acota una instancia de IUIAutomation recien creada, para que una llamada contra una ventana
// trabada (ElementFromHandle, FindAll, SetValue) no cuelgue el hilo de UI de toda la app (plan,
// seccion 11). Usada por WindowUtils::isQtFileDialog y UiaSwitcher::switchQtDialog: las dos crean su
// propia instancia via CoCreateInstance(CLSID_CUIAutomation), asi que el ajuste vive en un solo
// lugar en vez de repetirse.
namespace UiaTimeouts {

// Sin efecto (y logueado) si el proveedor no expone IUIAutomation2 (Windows 7): ahi la llamada queda
// sin acotar, es un riesgo documentado, no un error silencioso.
void apply(IUIAutomation *automation);

} // namespace UiaTimeouts

#endif // MIGHTYTOOLS_FOLDERSWITCH_UIATIMEOUTS_H
