#ifndef MIGHTYTOOLS_FOLDERSWITCH_UIATIMEOUTS_H
#define MIGHTYTOOLS_FOLDERSWITCH_UIATIMEOUTS_H

#include <windows.h>

struct IUIAutomation;

// Crea y acota la instancia de UI Automation que usan WindowUtils::isQtFileDialog y
// UiaSwitcher::switchQtDialog, para que una llamada contra una ventana trabada (ElementFromHandle,
// FindAll, SetValue) no cuelgue el hilo de UI de toda la app (plan, seccion 11). Vive en un solo
// lugar en vez de repetirse en los dos archivos que la usan.
namespace UiaTimeouts {

// CLSID_CUIAutomation (la interfaz "vieja") NUNCA expone IUIAutomation2..6 por QueryInterface, tenga
// la version de Windows que tenga: eso solo lo da CLSID_CUIAutomation8 (Windows 8+). Por eso la
// creacion pasa por aca: pide CLSID_CUIAutomation8 primero y cae a CLSID_CUIAutomation (sin timeouts
// configurables, logueado) si esa clase no esta registrada en la maquina.
HRESULT createAutomation(IUIAutomation **outAutomation);

// Sin efecto (y logueado) si la instancia no expone IUIAutomation2 (viene de CLSID_CUIAutomation, o
// un Windows anterior a la 8): ahi la llamada queda sin acotar, es un riesgo documentado, no un
// error silencioso.
void apply(IUIAutomation *automation);

} // namespace UiaTimeouts

#endif // MIGHTYTOOLS_FOLDERSWITCH_UIATIMEOUTS_H
