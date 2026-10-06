#ifndef MIGHTYTOOLS_OPENINNUKEX_OLDCLIENTMIGRATION_H
#define MIGHTYTOOLS_OPENINNUKEX_OLDCLIENTMIGRATION_H

#include <QString>
#include <QStringList>

class SettingsStore;

// Mudanza de los usuarios del cliente viejo (LGA OpenInNukeX v1.83) a Open in NukeX (plan seccion 9).
// Solo Windows. La usan tres caminos, con la MISMA logica:
//  - `--migrate-openinnukex`: modo corto de main (sin ventanas), lo llama el instalador en
//    CurStepChanged(ssPostInstall).
//  - `--remove-old-client`: modo corto de main, lo llama la casilla "Remove the old LGA OpenInNukeX"
//    del instalador.
//  - El boton "Uninstall old app" del panel, en un hilo propio (removeOldClient()).
//
// Deteccion: SOLO por el DisplayName de la clave de desinstalacion del cliente viejo
// (`{B8F1A2C3-4D5E-6F78-9A0B-1C2D3E4F5678}_is1` en HKLM 64, HKLM WOW6432Node o HKCU) o por el ProgID
// compartido `LGA.NukeScript.1` con el comando apuntando a `LGA_OpenInNukeX.exe`. nukeXpath.txt no
// cuenta: tambien lo escribe esta app.
//
// Con rastro:
//  - Prende Open in NukeX en settings.ini solo si `modules/openInNukeX/enabled` todavia no existe.
//    No toca el inicio con Windows (se activa recien si el usuario prende otra herramienta).
//  - Toma los .nk solo con el modulo prendido, si la eleccion efectiva de .nk es nuestro ProgID (o no
//    hay ninguna) y el comando del ProgID apunta a `LGA_OpenInNukeX.exe`, a un exe que ya no existe
//    en una unidad local fija, o ya a ESTE exe. NUNCA si apunta a otra copia de LGA Mighty Tools.
//    registerClasses() completo, sin UserChoice, y aviso al shell. Desde un arbol de build, solo se
//    loguea.
//  - Limpia los restos del cliente viejo en HKCU (A4) solo si ya no esta instalado.
// Siempre (con o sin rastro) guarda la marca `migration/openInNukeX` con el resultado.
//
// Despues de desinstalar el viejo ya no queda rastro que detectar: la primera pasada reescribio el
// ProgID a este exe y el desinstalador borro su clave. Por eso, SOLO para tomar los .nk y limpiar los
// restos, tambien cuentan como rastro la marca `migrated` (en cualquier pasada: cubre tambien a quien
// lo desinstalo desde Ajustes de Windows) y, en la pasada de removeOldClient(), lo detectado antes de
// desinstalar. Prender el modulo sigue exigiendo el rastro de verdad.
namespace OldClientMigration {

/// Una entrada de desinstalacion del cliente viejo (una por vista del registro).
struct UninstallEntry
{
    QString where;           ///< "HKLM", "HKLM WOW6432Node" o "HKCU" (para el log)
    QString displayName;     ///< vacio = no cuenta como instalado (Inno puede dejar la clave vacia)
    QString uninstallString; ///< comando del desinstalador tal cual
    QString oldExe;          ///< el exe del cliente viejo (DisplayIcon o InstallLocation), si se sabe
};

struct Detection
{
    QList<UninstallEntry> installed; ///< entradas con DisplayName
    QString progIdCommand;           ///< shell\open\command de LGA.NukeScript.1 en HKCU
    bool progIdPointsToOldExe = false;

    bool isInstalled() const { return !installed.isEmpty(); }
    bool hasTrace() const { return isInstalled() || progIdPointsToOldExe; }
};

Detection detect();

struct Options
{
    bool dryRun = false;        ///< nada se escribe (ni registro ni settings): solo log
    bool buildTree = false;     ///< arbol de build: el registro solo se loguea
    bool launchAllowed = true;  ///< false: el desinstalador del cliente viejo nunca se lanza (solo log)
    int removeTimeoutMs = 120000;
};

struct Report
{
    int failures = 0;
    QStringList lines;
    bool moduleEnabledNow = false; ///< la migracion prendio el modulo en esta corrida
    bool nkTaken = false;          ///< se escribio (o se volvio a escribir) la asociacion propia
    bool launched = false;         ///< removeOldClient(): se lanzo el desinstalador
    bool stillInstalled = false;   ///< removeOldClient(): el cliente viejo sigue instalado al final
};

/// La migracion (plan seccion 9). `store` nulo: sin pasos de settings (el panel: el modulo ya esta
/// prendido). `knownOldClient`: el cliente viejo estaba antes de desinstalarlo (la pasada de
/// removeOldClient()); junto con la marca `migrated`, habilita la toma de .nk y la limpieza de restos
/// aunque ya no quede rastro. Nunca prende el modulo.
Report migrate(SettingsStore *store, const Options &options, bool knownOldClient = false);

/// Quitar el cliente viejo: lanza su desinstalador en silencio (eleva solo), espera el proceso y
/// sondea hasta que desaparezcan el DisplayName y el exe viejo (tope Options::removeTimeoutMs) y
/// despues rehace migrate() con lo detectado antes. Con `launchAllowed` false no lanza nada: loguea lo
/// que haria y rehace migrate().
Report removeOldClient(SettingsStore *store, const Options &options);

/// El exe y los argumentos de un UninstallString (`"C:\x\unins000.exe" /X` -> exe, "/X"). Vacio si no
/// se puede leer. Pura: la usa el self-test.
bool splitUninstallString(const QString &uninstallString, QString *exe, QString *arguments);

/// True si el proceso corre en el escritorio aparte del arnes de QA (run_headless.ps1, el de
/// ..\LGA_Base_QT_C_Py\tools\qa): entonces nunca se lanza ni se escribe nada.
bool runningOnQaDesktop();

/// Entradas de main. Imprimen cada linea en stdout (y en el debug.log con log=true) y devuelven el
/// codigo de salida: 0 bien, 1 algo fallo (o el cliente viejo sigue instalado).
/// En el escritorio del arnes de QA o con `--dry-run`: solo log (no escriben ni lanzan nada).
/// `--remove-old-client` desde un arbol de build tampoco lanza el desinstalador.
int runMigrateFromCommandLine(const QStringList &arguments);
int runRemoveFromCommandLine(const QStringList &arguments);

} // namespace OldClientMigration

#endif // MIGHTYTOOLS_OPENINNUKEX_OLDCLIENTMIGRATION_H
