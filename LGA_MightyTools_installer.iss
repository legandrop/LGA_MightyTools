; LGA_MightyTools_installer.iss -- FUENTE ESCRITA A MANO, no generada (mismo esquema que
; LGA_FolderSwitch). instalador.bat solo le pasa la version con /DMyAppVersion (la fuente unica es
; CMakeLists.txt) y no lo reescribe.

#define MyAppName "LGA Mighty Tools"
#ifndef MyAppVersion
#define MyAppVersion "0.01"
#endif
#define MyAppPublisher "LGA"
#define MyAppExeName "LGA_MightyTools.exe"
#define MyAppOutputDir "installer"
; Primera version del exe que conoce --uninstall-cleanup (FILEVERSION "mayor,menor,0,0" del recurso
; de Windows: 0.06 -> 0,6). Un exe anterior no conoce el flag, arrancaria como app de bandeja y el
; desinstalador quedaria esperando: con uno viejo no se llama.
#define CleanupMinMajor 0
#define CleanupMinMinor 6

[Setup]
AppId={{37F7C31A-CD84-418E-8282-E30C584E0A5A}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName=C:\Portable\LGA\MightyTools
DefaultGroupName={#MyAppName}
OutputDir={#MyAppOutputDir}
OutputBaseFilename=LGA_MightyTools_Setup_v{#MyAppVersion}
SetupIconFile=resources\icons\LGA_MightyTools.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
PrivilegesRequired=lowest
UsePreviousAppDir=no
DirExistsWarning=no
Compression=lzma2
LZMANumBlockThreads=4
SolidCompression=yes
WizardStyle=modern

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "deploy\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
; Script de cierre por ruta (copia de LGA_Base_QT_C_Py). Viaja dos veces: `dontcopy` para que
; PrepareToInstall lo extraiga a {tmp} (en una actualizacion todavia no hay copia instalada), y
; en {app}\tools para que el desinstalador lo encuentre: al desinstalar no hay {tmp} del setup.
Source: "tools\close_by_path.ps1"; Flags: dontcopy
Source: "tools\close_by_path.ps1"; DestDir: "{app}\tools"; Flags: ignoreversion

; Inno solo desinstala los archivos que copio el; lo que la app escribe por su cuenta se borra
; aca para no dejar basura:
;  - {app}\debug.log (se prende con log=true en config\debug_flags.txt).
;  - La configuracion: %APPDATA%\LGA\LGA_MightyTools\settings.ini (src/core/AppSettings.cpp) y
;    el registro compartido de apps LGA de esta app (LgaRegistry). La carpeta LGA solo se borra si
;    queda vacia (la usan otras apps LGA).
;  - Los instaladores bajados por el auto-update, en %TEMP%\LGA_MightyTools_updates.
; Lo que la app escribe en el REGISTRO (asociacion .nk, navegador, Run) lo borra el propio exe con
; --uninstall-cleanup, y la entrada de inicio con Windows ademas en Pascal: CurUninstallStepChanged,
; abajo.
[UninstallDelete]
Type: files; Name: "{app}\debug.log"
Type: filesandordirs; Name: "{userappdata}\LGA\LGA_MightyTools"
Type: files; Name: "{userappdata}\LGA\LGA_MightyTools.json"
Type: dirifempty; Name: "{userappdata}\LGA"
Type: filesandordirs; Name: "{%TEMP}\LGA_MightyTools_updates"

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

; SIN seccion [Registry] a proposito. El inicio con Windows (HKCU\...\Run) lo maneja SOLO la app:
; la copia instalada lo activa sola en su primer arranque, y el checkbox de Settings lo prende y lo
; apaga. Cuando en FolderSwitch lo escribia el instalador, con el mismo nombre de valor que usa la
; app, pisaba la entrada de otra copia y la borraba al desinstalar. Ver
; LGA_Base_QT_C_Py/docs/Doc_Autostart_Windows.md.

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[Code]
// La app vive en la bandeja y una instancia activa bloquea el .exe instalado, asi que se cierra
// antes de instalar y antes de desinstalar, POR RUTA real y nunca por nombre (close_by_path.ps1).
//   - Al INSTALAR, la app es de instancia unica (QLockFile: con otra copia abierta, la nueva sale
//     en silencio): -AllInstances cierra TODAS las copias (la instalada, un build, otro checkout) y
//     la que se instala queda como la unica. No lanza auxiliares: sin -Helpers.
//   - Al DESINSTALAR, -Prefix {app} cierra solo las copias que corren desde la carpeta que se va a
//     borrar; un build o un checkout quedan vivos.
// Si PowerShell no esta, o {app} no pasa las guardas del script, no se cierra nada e Inno avisa
// "archivo en uso": es la direccion segura. Las comillas van como #34 y powershell.exe con la ruta
// de {sys}.
procedure CloseAppByPath(const ScriptPath, CloseParams: String);
var
  ResultCode: Integer;
  Params: String;
begin
  Params := '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File ' + #34 + ScriptPath + #34 +
            ' ' + CloseParams;
  if Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'), Params, '', SW_HIDE,
          ewWaitUntilTerminated, ResultCode) then
    Log('close_by_path ' + CloseParams + ': codigo ' + IntToStr(ResultCode))
  else
    Log('close_by_path no se pudo ejecutar: no se cierra nada');
end;

// PrepareToInstall corre con {app} ya elegido y antes de copiar archivos.
function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  ExtractTemporaryFile('close_by_path.ps1');
  CloseAppByPath(ExpandConstant('{tmp}\close_by_path.ps1'), '-ExeName {#MyAppExeName} -AllInstances');
  // Stop-Process es asincronico: se le da tiempo al proceso a soltar sus archivos antes de copiar
  // encima.
  Sleep(1500);
end;

// Al desinstalar {app} ya es la carpeta instalada; el script es la copia de {app}\tools. Si no
// esta (alguien la borro), no se cierra nada e Inno deja lo que este en uso.
function InitializeUninstall(): Boolean;
var
  ScriptPath: String;
begin
  Result := True;
  ScriptPath := ExpandConstant('{app}\tools\close_by_path.ps1');
  if FileExists(ScriptPath) then
  begin
    CloseAppByPath(ScriptPath, '-ExeName {#MyAppExeName} -Prefix ' + #34 + ExpandConstant('{app}') + #34);
    Sleep(1500);
  end
  else
    Log('No esta ' + ScriptPath + ': no se cierra nada');
end;

// ANTES de borrar los archivos (usUninstall): el exe instalado borra lo que escribio en HKCU y es
// SUYO por contenido (asociacion .nk, registro como navegador, RegisteredApplications, Run): modo
// --uninstall-cleanup, sin ventana ni dialogos. Solo si el exe esta y su version ya conoce el flag
// (CleanupMinMajor/Minor, arriba); si no, no se llama y quedan solo las limpiezas de Pascal.
procedure RunUninstallCleanup();
var
  ExePath, VersionText: String;
  VersionMS, VersionLS: Cardinal;
  ResultCode: Integer;
begin
  ExePath := ExpandConstant('{app}\{#MyAppExeName}');
  if not FileExists(ExePath) then
  begin
    Log('--uninstall-cleanup: no esta ' + ExePath + ', no se limpia el registro');
    exit;
  end;
  if not GetVersionNumbersString(ExePath, VersionText) then
    VersionText := '?';
  if not GetVersionNumbers(ExePath, VersionMS, VersionLS) then
  begin
    Log('--uninstall-cleanup: no se pudo leer la version de ' + ExePath + ', no se llama');
    exit;
  end;
  if VersionMS < (({#CleanupMinMajor} shl 16) or {#CleanupMinMinor}) then
  begin
    Log('--uninstall-cleanup: el exe es ' + VersionText + ', anterior a la que conoce el flag; no se llama');
    exit;
  end;
  if Exec(ExePath, '--uninstall-cleanup', '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
    Log('--uninstall-cleanup (exe ' + VersionText + '): codigo ' + IntToStr(ResultCode))
  else
    Log('--uninstall-cleanup no se pudo ejecutar (exe ' + VersionText + '): error ' + IntToStr(ResultCode));
end;

// usUninstall: la limpieza del exe (arriba). usPostUninstall, despues de borrar los archivos: el
// valor LGA_MightyTools de HKCU\...\Run (inicio con Windows), SOLO si apunta a ESTA instalacion,
// como respaldo por si el exe no pudo correr. Si apunta a otra copia (build\ de desarrollo) es de esa
// copia y no se toca.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  RunValue: String;
begin
  if CurUninstallStep = usUninstall then
  begin
    RunUninstallCleanup();
    exit;
  end;
  if CurUninstallStep <> usPostUninstall then
    exit;
  if RegQueryStringValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'LGA_MightyTools', RunValue) then
  begin
    if Pos(Lowercase(AddBackslash(ExpandConstant('{app}'))), Lowercase(RunValue)) > 0 then
    begin
      RegDeleteValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'LGA_MightyTools');
      RegDeleteValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run', 'LGA_MightyTools');
      Log('Borrado el inicio con Windows de esta instalacion: ' + RunValue);
    end
    else
      Log('El inicio con Windows apunta a otra copia, no se toca: ' + RunValue);
  end;
end;
