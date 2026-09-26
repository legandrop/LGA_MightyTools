#ifndef MIGHTYTOOLS_OPENINNUKEX_WINFILEASSOCIATION_H
#define MIGHTYTOOLS_OPENINNUKEX_WINFILEASSOCIATION_H

#include <QString>
#include <QStringList>

// HWND sin incluir windows.h: la UI (OpenInNukeXPanel.cpp) no lleva headers de Win32 (regla de la
// app). Mismo truco que usaba winfileassociation.h del cliente v1.83: `struct HWND__` es el mismo
// tipo incompleto que winnt.h declara antes de definirlo, asi que si este header y windows.h
// conviven en la misma unidad de traduccion (WinFileAssociation.cpp), el alias es identico y no
// hay redefinicion en conflicto.
struct HWND__;
using HWND = HWND__ *;

// Asociacion de `.nk` en Windows, portada de
// `~/.nuke/LGA_OpenInNukeX/QtClient/src/winfileassociation.{h,cpp}` (v1.83).
//
// Diferencias con el origen (D-03, D-04, D-09, plan 4.6):
//  - El ProgID `LGA.NukeScript.1` SE REUTILIZA (para que la eleccion que ya hizo un usuario del
//    cliente viejo siga valiendo), pero `shell\open\command` apunta a ESTE exe.
//  - Capabilities y RegisteredApplications van con el nombre "LGA Mighty Tools", no "OpenInNukeX".
//  - Sin el helper .NET `LGA_WinSetFTA.exe` (D-03: cero dependencias de .NET). TODA la escritura
//    de UserChoice/UserChoiceLatest (el hash legado pre-Windows 11 Y el de Windows 11) la porta
//    OTRO ejecutor a `src/modules/openinnukex/win/UserChoiceLatest.{h,cpp}` (API:
//    `applyAssociation(ext, progId, ...)` -> resultado con ok/motivo/avisos, e
//    `isLatestHashActive()`). El hash legado del cliente v1.83 tenia tres bugs confirmados
//    (corrimiento aritmetico en vez de logico, hora local sin pasar a UTC, y `HashVersion` leido
//    como string cuando es REG_DWORD) que el otro ejecutor corrige de cero: por eso NO se porto
//    aca. Mientras esos archivos no existen, `writeUserChoice()` (mas abajo) es la UNICA
//    costura — devuelve false y loguea, y `apply()` cae al selector "Abrir con" o a
//    `ms-settings:defaultapps`, igual que si el hash no alcanzara.
//  - `cleanConflictingKeys()` corre SOLO en re-apply (releaseAssociation()/Re-apply explicito),
//    nunca como primer paso silencioso de apply(): un usuario ya "Associated" no deberia perder
//    su UserChoice por abrir el panel.
//  - `releaseAssociation()` es nuevo (D-09): suelta el ProgID, las Capabilities y
//    RegisteredApplications, y limpia UserChoice/UserChoiceLatest de `.nk` SOLO si hoy apuntan a
//    nuestro ProgID (nunca el de otra app).
namespace WinFileAssociation {

enum class ApplyResult { Success, NeedsUserConfirmation, Failed };

struct ApplyOutcome
{
    ApplyResult result = ApplyResult::Failed;
    QStringList errors;
};

/// El ProgID que reutilizamos. Estable para siempre: cambiar de ProgID perderia la eleccion de
/// todos los usuarios del cliente viejo.
QString progId();

/// Lee el ProgId actual de `.nk` desde UserChoiceLatest (si esta activo) o UserChoice.
QString currentNkProgId();

/// True si el ProgId actual de `.nk` es el nuestro Y el comando de ese ProgID apunta a ESTE exe
/// (plan 4.6: "el estado asociado se verifica con el ProgID MAS la ruta del comando").
bool isNkAssociatedWithUs();

/// Registra ProgID + Capabilities + RegisteredApplications y escribe la asociacion de `.nk`.
/// `reapply=true` corre primero `cleanConflictingKeys()` (borra UserChoice/UserChoiceLatest antes
/// de reintentar): es el camino de "Re-apply" para quien no aparece como Associated, nunca el
/// primer paso (comentario del header).
///
/// `parentHwnd`: la ventana dueña del selector nativo "Abrir con" si el hash silencioso no
/// alcanza (`IOpenWithLauncher::Launch`). El cliente v1.83 pasaba `winId()` de su ventana; en
/// esta app se habia perdido (regresion detectada en la auditoria), y el picker aparecia sin
/// dueño (puede salir detras de la ventana o sin foco). `nullptr` sigue siendo valido: el picker
/// aparece sin ventana dueña, como si no se pasara ninguna.
ApplyOutcome apply(bool reapply, HWND parentHwnd = nullptr);

/// D-09: suelta lo que apply() escribio. No toca UserChoice/UserChoiceLatest si apuntan a OTRA
/// app (nunca le saca la asociacion a alguien mas).
bool releaseAssociation(QString *error);

/// True si el cliente viejo (LGA OpenInNukeX, instalador Inno con permisos de administrador)
/// sigue instalado, detectado por su clave de desinstalacion
/// `{B8F1A2C3-4D5E-6F78-9A0B-1C2D3E4F5678}_is1` (plan 4.6, seccion 9).
bool isOldClientInstalled();

/// Costura de D-03 (ver comentario de arriba del archivo). Devuelve false con `*reason` en
/// "no disponible todavia" hasta que el supervisor conecte `win/UserChoiceLatest.{h,cpp}`.
bool writeUserChoice(const QString &extension, const QString &progId, QString *reason);

} // namespace WinFileAssociation

#endif // MIGHTYTOOLS_OPENINNUKEX_WINFILEASSOCIATION_H
