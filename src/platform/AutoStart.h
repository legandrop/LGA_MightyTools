#ifndef MIGHTYTOOLS_AUTOSTART_H
#define MIGHTYTOOLS_AUTOSTART_H

// Inicio automatico de la app con la sesion del usuario. Una implementacion por plataforma:
//  - Windows (platform/win/AutoStartWin.cpp): HKCU\Software\Microsoft\Windows\CurrentVersion\Run,
//    con la Win32 Registry API directa. Copia del modulo de LGA_FolderSwitch, que a su vez es el de
//    LGA_FrameRev. Doc del patron: ../LGA_Base_QT_C_Py/docs/Doc_Autostart_Windows.md.
//  - macOS (platform/mac/AutoStartMac.mm): SMAppService.mainApp (macOS 13 o posterior). Aparece
//    en Ajustes > General > Items de inicio con el nombre de la app.
//
// La entrada la maneja SOLO la app: el instalador no la escribe ni la borra (salvo borrarla al
// desinstalar si apunta a esa instalacion).

#include <QString>

namespace AutoStart {

// True si la app esta registrada para iniciar con la sesion. En Windows ademas el valor tiene que
// apuntar al ejecutable ACTUAL. Siempre pregunta al sistema en vivo, no cachea nada.
bool isEnabled();

// Activa o desactiva el inicio automatico para el ejecutable actual. En Windows, desactivar borra el
// valor de Run y la marca de Task Manager (StartupApproved\Run) con el mismo nombre.
bool setEnabled(bool enabled);

// --uninstall-cleanup: borra la entrada de inicio SOLO si apunta a ESTE exe (propiedad por
// contenido); la de otra copia (un build, otra carpeta) no se toca. `detail` dice que hizo.
// true si no quedo nada propio sin borrar. En macOS no hay nada que limpiar (el item de inicio de
// SMAppService se va con el .app).
bool removeIfOwned(QString *detail = nullptr);

// Por que NO se puede activar desde este binario (ver availability()).
enum class Unavailability {
    None,            ///< se puede
    DevelopmentTree, ///< corre desde build/, deploy/ o el repo (runsFromDevelopmentTree())
    Unsupported,     ///< el sistema no lo soporta (macOS anterior a 13)
};

struct Availability
{
    bool available = false;
    Unavailability reason = Unavailability::None;
    /// Texto corto EN INGLES para la UI (tooltip del checkbox); vacio si `available`.
    QString text;
};

// Si el inicio automatico se puede activar AUTOMATICAMENTE desde ESTE binario. setEnabled() NO
// chequea esto a proposito: es la escritura pelada. La politica la aplica quien decide activar:
// el primer arranque solo lo activa en una copia instalada; el click explicito del usuario en el
// checkbox se respeta siempre (salvo Unsupported).
//
// Por que importa (medido con FolderSwitch el 2026-09-04): un valor de Run que la app reescribe en
// cada arranque desde build/ es un valor que TAMBIEN se puede pisar o borrar solo.
Availability availability();

// True si este binario corre desde una salida de desarrollo: carpeta contenedora `build*` /
// `deploy*`, o adentro del arbol de build o del repo que inyecta CMake (LGA_BUILD_TREE_DIR /
// LGA_SOURCE_TREE_DIR). Misma regla que LgaRegistry.
bool runsFromDevelopmentTree();

// Solo Windows, para el log: valor crudo guardado en Run (vacio si no existe) y si Task Manager >
// Startup lo tiene deshabilitado. En macOS devuelven vacio/false.
QString storedCommand();
bool disabledByTaskManager();

// Texto de la UI: "Start with Windows" o "Open at login".
QString checkboxText();

} // namespace AutoStart

#endif // MIGHTYTOOLS_AUTOSTART_H
