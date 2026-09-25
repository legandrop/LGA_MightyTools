<#
.SYNOPSIS
    Corre un ejecutable de prueba en un escritorio de Windows aparte, dentro de un Job object,
    para que ninguna ventana ni cartel del proceso aparezca en el escritorio del usuario.

.DESCRIPTION
    Uso tipico:

      powershell -NoProfile -ExecutionPolicy Bypass -File tools\qa\run_headless.ps1 `
          -Exe C:\ruta\app.exe -Arguments '--ui-shot empty "C:\tmp\con espacios\out.png"' [-TimeoutSec 90] `
          [-QtRoot C:\Qt\6.5.3\mingw_64] [-Platform offscreen|windows] [-OutDir C:\tmp\corrida] `
          [-SetEnv NOMBRE=valor] [-PreflightOnly] [-NoQt] [-KeepRuns 20]

      powershell -NoProfile -ExecutionPolicy Bypass -File ...\run_headless.ps1 -SelfTest

    -Arguments va TAL CUAL a la linea de comandos del programa: una ruta con espacios lleva
    comillas DOBLES adentro del texto (las simples solo delimitan el parametro en PowerShell).
    Desde bash: -Arguments "--ui-shot empty \"C:\con espacios\out.png\"". Desde PowerShell 5.1,
    `& powershell -File ...` PIERDE las comillas dobles internas al pasarlas a otro proceso: en ese
    caso escribir los argumentos en un archivo de texto y pasar -ArgumentsFile <ruta> (se usa la
    primera linea, tal cual).
    -TimeoutSec por defecto 90 s: el runner tiene que poder matar, esperar hasta -KillWaitSec (10 s)
    e imprimir su informe antes de que venza el limite de la terminal que lo invoca (120 s por
    defecto en las corridas automatizadas). Para corridas mas largas, subir las dos cosas.
    -NoQt: no toca el entorno de Qt ni exige plugin (para un exe que no es de Qt).
    -PreflightOnly: valida sin crear ningun proceso. Con ctest el preflight cubre solo ctest.exe:
    correr antes -PreflightOnly sobre cada exe de test.
    Sin -OutDir, la salida va a %TEMP%\lga_headless\<fecha>_<id> y se conservan las ultimas
    -KeepRuns corridas; las mas viejas se borran.

    QUE CUBRE
      - Todo lo que el proceso y sus descendientes dibujen con ventanas propias: el MessageBox de
        Qt "no Qt platform plugin could be initialized", un QMessageBox, una ventana olvidada con
        show(). Queda en un escritorio sin entrada, con nombre GUID, que nadie ve.
      - Muerte temprana: si aparece un dialogo del sistema (#32770) en ese escritorio, copia su
        texto, mata el grupo y sale sin esperar al timeout.
      - Cero huerfanos: el proceso se crea suspendido, entra al Job (KILL_ON_JOB_CLOSE) y recien
        despues se reanuda; sus hijos y nietos quedan en el mismo Job. Nunca mata por nombre.

    QUE NO CUBRE (leer antes de confiar en el resultado)
      - Los HARD ERRORS del sistema -DLL faltante, punto de entrada no encontrado, imagen
        invalida, "Unsupported 16-Bit Application"- NO se ocultan: los dibuja csrss en el
        escritorio ACTIVO, no en el del proceso. La defensa es no provocarlos: el preflight valida
        la cabecera PE y la arquitectura del exe y resuelve cada DLL importada (y cada funcion
        importada de las DLL que no son del sistema) con el mismo orden de busqueda del loader.
        El modo de error heredado (SEM_FAILCRITICALERRORS) ayuda, pero no esta verificado que
        los suprima todos. Si aparece una ventana nueva de csrss o WerFault durante la corrida,
        la linea final la cuenta en sys_new.
      - Una tool rota que lance la app bajo prueba (un nieto con cabecera invalida) sigue pudiendo
        abrir el cartel de 16 bits: nunca poner binarios falsos o rotos en rutas que usa una app.
      - Ventanas que abre OTRO proceso, fuera del grupo: abrir una URL (QDesktopServices::openUrl)
        la abre el navegador del usuario; abrir una carpeta, su Explorer; tambien notificaciones
        y servidores COM fuera de proceso. Aparecen en el escritorio del usuario.
      - Una consola nueva que pida un descendiente con CREATE_NEW_CONSOLE: si la terminal
        predeterminada es Windows Terminal, puede abrirse como pestana en el escritorio del usuario.
      - El sonido: el MessageBox oculto igual suena.
      - Elevacion (UAC) desde un descendiente: si un proceso del grupo usa ShellExecute con runas
        (o lanza asi un exe que pide admin), el proceso elevado lo crea el servicio AppInfo FUERA
        del Job y del escritorio aparte, y el consentimiento sale en el escritorio seguro, a la
        vista del usuario. Con CreateProcess directo no hay cartel (error 740), asi que el exe
        raiz no es el problema: lo son sus descendientes.
      - No aisla red, archivos, settings ni portapapeles. Un exe fuera de las rutas aprobadas en el
        firewall por aplicacion de la maquina que abre un socket queda retenido y no muere: el
        runner lo informa como unkillable=<pids> y sale; no reintenta ni mata por nombre.
      - OpenGL en un escritorio sin entrada puede no crear contexto.

    VISIBLE_JOB
      Cuenta las ventanas visibles en el escritorio del usuario de los procesos del grupo. Un proceso
      del grupo se identifica por PID Y hora de creacion, anotados mientras esta en el Job: cuando uno
      muere, Windows puede darle su PID a otro proceso cualquiera, y con el PID solo una ventana ajena
      (otra app, otra sesion) contaba como del grupo.

    EXES DE 32 BITS (i386)
      Se aceptan con el grafo entero de 32 bits: las DLL del sistema se buscan en SysWOW64 (el hijo
      corre bajo WoW64 y su System32 es SysWOW64), y una DLL de la otra arquitectura primero en el
      camino se rechaza (lanzada daria 0xC000007B). Corren sin entorno de Qt; un exe de 32 bits que
      importe Qt6Core se rechaza.

    INSTALADORES Y DESINSTALADORES (de cualquier tipo)
      Ninguno se ejecuta en una corrida automatizada: el real instala sobre la instalacion del
      usuario, pisa su registro y crea accesos directos. Se rechazan como exe objetivo y tambien
      cuando aparecen en -Arguments (un cmd /c "setup.exe /S"): todo unins*.exe sea o no de Inno,
      msiexec y paquetes .msi, un setup de Inno (marca "built with Inno Setup" en el recurso de
      version o firma "Inno Setup Setup Data"), NSIS (firma "NullsoftInst") y los paquetes de WiX
      (seccion ".wixburn"). Un instalador que no tenga ninguna de esas marcas no se detecta: lo
      frena la regla. En -Arguments se miran las rutas .exe que existen; lo que un script lance
      desde su propio codigo no se ve. ISCC.exe si se puede correr, siempre con /O<carpeta de
      prueba>. La forma aprobada de probar un [Code] de Inno (driver .ps1 con un .iss minimo de
      prueba) esta en docs/Doc_Instaladores_Inno.md seccion 8.

    CODIGOS DE SALIDA
      0 ok (el proceso salio con 0 y no se vio nada)
      1 el proceso salio con codigo distinto de 0
      2 rechazado por el preflight: NO se lanzo
      3 aparecio un dialogo en el escritorio aparte: se copio el texto y se mato el grupo
      4 timeout: se mato el grupo
      5 quedaron procesos vivos tras matar el grupo (unkillable)
      6 se vio una ventana del grupo en el escritorio del usuario, o una ventana nueva de
        csrss/WerFault durante la corrida
      7 error interno del runner
      8 el proceso termino pero algun control no estuvo disponible (dialog_check o sys_check=no):
        no hay garantia de que no se haya visto nada
#>
[CmdletBinding()]
param(
    [string]$Exe = '',
    [string]$Arguments = '',
    [string]$ArgumentsFile = '',
    [int]$TimeoutSec = 90,
    [string]$QtRoot = '',
    [string]$Platform = 'offscreen',
    [string]$OutDir = '',
    [string]$WorkDir = '',
    [string[]]$SetEnv = @(),
    [string[]]$NetAllowed = @(),
    [int]$KillWaitSec = 10,
    [switch]$PreflightOnly,
    [switch]$NoQt,
    [int]$KeepRuns = 20,
    [string[]]$InnoSamples = @(),
    [string]$InnoWatchDir = '',
    [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

$script:DefaultQtRoot = 'C:\Qt\6.5.3\mingw_64'
$script:HardErrorNote = 'nota: los hard errors del sistema (DLL faltante, punto de entrada, imagen invalida, 16 bits) los dibuja csrss en el escritorio ACTIVO y este runner NO los oculta; los evita el preflight. sys_new cuenta ventanas nuevas de csrss/WerFault vistas durante la corrida.'

if (-not ('LgaQa.Runner' -as [type])) {
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

namespace LgaQa {

public class PeImport {
    public string Dll;
    public List<string> Names = new List<string>();
    public List<uint> Ordinals = new List<uint>();
}

public class PeImage {
    public string Path;
    public ushort Machine;
    public ushort Magic;
    public ushort Characteristics;
    public ushort Subsystem;
    public List<PeImport> Imports = new List<PeImport>();
    public List<string> DelayImports = new List<string>();
    public HashSet<string> ExportNames = new HashSet<string>(StringComparer.Ordinal);
    public uint OrdinalBase;
    public uint NumberOfFunctions;
    public bool IsDll { get { return (Characteristics & 0x2000) != 0; } }
    public bool IsExecutable { get { return (Characteristics & 0x0002) != 0; } }
}

// Lectura minima de PE: cabeceras, importaciones, exportaciones. Solo lee el archivo.
public static class Pe {
    static ushort U16(byte[] d, long o) { if (o < 0 || o + 2 > d.Length) throw new InvalidDataException("fuera de rango"); return BitConverter.ToUInt16(d, (int)o); }
    static uint U32(byte[] d, long o) { if (o < 0 || o + 4 > d.Length) throw new InvalidDataException("fuera de rango"); return BitConverter.ToUInt32(d, (int)o); }
    static ulong U64(byte[] d, long o) { if (o < 0 || o + 8 > d.Length) throw new InvalidDataException("fuera de rango"); return BitConverter.ToUInt64(d, (int)o); }

    static string CStr(byte[] d, long o) {
        if (o < 0 || o >= d.Length) throw new InvalidDataException("cadena fuera de rango");
        long e = o;
        while (e < d.Length && d[e] != 0 && e - o < 4096) e++;
        return Encoding.ASCII.GetString(d, (int)o, (int)(e - o));
    }

    public static string Magic(string path) {
        // Solo cabecera: sirve para rechazar un .exe que no es PE antes de cualquier otra cosa.
        try {
            using (FileStream fs = File.OpenRead(path)) {
                byte[] b = new byte[4096];
                int n = fs.Read(b, 0, b.Length);
                if (n < 64) return "archivo de " + n + " bytes: no es un ejecutable PE";
                if (b[0] != (byte)'M' || b[1] != (byte)'Z') return "sin firma MZ: no es un ejecutable PE";
                int lfanew = BitConverter.ToInt32(b, 0x3C);
                if (lfanew < 0 || (long)lfanew + 24 > fs.Length) return "e_lfanew fuera del archivo: cabecera PE invalida (DOS/16 bits o truncado)";
                return null;
            }
        } catch (Exception ex) { return "no se pudo leer: " + ex.Message; }
    }

    // Busca una cadena ASCII en el archivo, por bloques (los setups pesan decenas de MB).
    public static bool ContainsAscii(string path, string text) {
        return ContainsAnyAscii(path, new string[] { text }) != null;
    }

    // Igual, pero con varias cadenas en una sola lectura: devuelve la primera que aparece, o null.
    public static string ContainsAnyAscii(string path, string[] texts) {
        byte[][] pats = new byte[texts.Length][];
        int maxLen = 1;
        for (int k = 0; k < texts.Length; k++) { pats[k] = Encoding.ASCII.GetBytes(texts[k]); maxLen = Math.Max(maxLen, pats[k].Length); }
        byte[] buf = new byte[1 << 20];
        int keep = maxLen - 1, have = 0;
        try {
            using (FileStream fs = File.OpenRead(path)) {
                while (true) {
                    int n = fs.Read(buf, have, buf.Length - have);
                    if (n <= 0) return null;
                    int total = have + n;
                    for (int k = 0; k < pats.Length; k++) {
                        byte[] pat = pats[k];
                        for (int i = 0; i + pat.Length <= total; i++) {
                            int j = 0;
                            while (j < pat.Length && buf[i + j] == pat[j]) j++;
                            if (j == pat.Length) return texts[k];
                        }
                    }
                    have = Math.Min(keep, total);
                    Array.Copy(buf, total - have, buf, 0, have);
                }
            }
        } catch { return null; }
    }

    public static PeImage Read(string path, bool tables, out string error) {
        error = null;
        byte[] d;
        try { d = File.ReadAllBytes(path); } catch (Exception ex) { error = "no se pudo leer: " + ex.Message; return null; }
        try {
            if (d.Length < 64 || d[0] != (byte)'M' || d[1] != (byte)'Z') { error = "sin firma MZ"; return null; }
            long pe = BitConverter.ToInt32(d, 0x3C);
            if (pe < 0 || pe + 24 > d.Length) { error = "e_lfanew fuera del archivo"; return null; }
            if (U32(d, pe) != 0x00004550) { error = "sin firma PE (imagen DOS o de 16 bits)"; return null; }
            PeImage img = new PeImage();
            img.Path = path;
            img.Machine = U16(d, pe + 4);
            ushort nsec = U16(d, pe + 6);
            ushort optSize = U16(d, pe + 20);
            img.Characteristics = U16(d, pe + 22);
            long opt = pe + 24;
            img.Magic = U16(d, opt);
            long dirs; uint ndirs;
            if (img.Magic == 0x20B) { ndirs = U32(d, opt + 108); dirs = opt + 112; }
            else if (img.Magic == 0x10B) { ndirs = U32(d, opt + 92); dirs = opt + 96; }
            else { error = "optional header desconocido 0x" + img.Magic.ToString("X"); return null; }
            img.Subsystem = U16(d, opt + 68);
            uint sizeHeaders = U32(d, opt + 60);
            uint sizeImage = U32(d, opt + 56);
            long len = d.Length;
            long sec = opt + optSize;
            // Un archivo truncado o recortado no se carga: el loader devuelve 0xC000007B y csrss
            // dibuja el cartel en el escritorio activo. Todo lo que la cabecera ubica por offset
            // de archivo tiene que estar adentro del archivo.
            if (sec + 40L * nsec > len || sizeHeaders > len) { error = "cabeceras fuera del archivo (truncado)"; return null; }
            List<uint[]> sections = new List<uint[]>();
            for (int i = 0; i < nsec; i++) {
                long s = sec + 40L * i;
                uint vsize = U32(d, s + 8), va = U32(d, s + 12), rawSize = U32(d, s + 16), rawPtr = U32(d, s + 20);
                string sname = Encoding.ASCII.GetString(d, (int)s, 8).TrimEnd('\0');
                if (rawSize > 0 && (long)rawPtr + rawSize > len) {
                    error = "seccion " + sname + " fuera del archivo: termina en " + ((long)rawPtr + rawSize) + " y el archivo mide " + len + " (truncado)";
                    return null;
                }
                if ((long)va + Math.Max(vsize, rawSize) > (long)sizeImage + 0x10000) {
                    error = "seccion " + sname + " fuera de SizeOfImage (cabecera inconsistente)";
                    return null;
                }
                sections.Add(new uint[] { va, Math.Max(vsize, rawSize), rawPtr });
            }
            // Tabla de simbolos COFF (la dejan los builds de MinGW al final del archivo). El loader
            // no la mapea, pero si falta un pedazo el archivo esta recortado: se rechaza igual.
            uint symPtr = U32(d, pe + 12), nsym = U32(d, pe + 16);
            if (symPtr != 0) {
                long strTab = (long)symPtr + 18L * nsym;
                if (strTab + 4 > len) { error = "tabla de simbolos COFF fuera del archivo (truncado)"; return null; }
                uint strSize = U32(d, strTab);
                if (strSize >= 4 && strTab + strSize > len) { error = "tabla de cadenas COFF fuera del archivo (truncado)"; return null; }
            }
            // Firma Authenticode: el directorio 4 es un offset de archivo, no una RVA.
            if (ndirs > 4) {
                uint certOff = U32(d, dirs + 32), certSize = U32(d, dirs + 36);
                if (certSize > 0 && (long)certOff + certSize > len) { error = "firma digital fuera del archivo (truncado)"; return null; }
            }
            if (!tables) return img;

            Func<uint, long> rva = delegate(uint r) {
                if (r < sizeHeaders) return r;
                foreach (uint[] s in sections) {
                    if (r >= s[0] && r < (long)s[0] + s[1]) return (long)r - s[0] + s[2];
                }
                throw new InvalidDataException("RVA 0x" + r.ToString("X") + " fuera de secciones");
            };
            bool is64 = img.Magic == 0x20B;

            if (ndirs > 0) {
                uint expRva = U32(d, dirs), expSize = U32(d, dirs + 4);
                if (expRva != 0 && expSize != 0) {
                    long e = rva(expRva);
                    img.OrdinalBase = U32(d, e + 16);
                    img.NumberOfFunctions = U32(d, e + 20);
                    uint nnames = U32(d, e + 24);
                    long names = rva(U32(d, e + 32));
                    for (uint i = 0; i < nnames; i++) img.ExportNames.Add(CStr(d, rva(U32(d, names + 4L * i))));
                }
            }
            if (ndirs > 1) {
                uint impRva = U32(d, dirs + 8);
                if (impRva != 0) {
                    long o = rva(impRva);
                    for (int guard = 0; guard < 4096; guard++, o += 20) {
                        uint oft = U32(d, o), nameRva = U32(d, o + 12), ft = U32(d, o + 16);
                        if (oft == 0 && nameRva == 0 && ft == 0) break;
                        PeImport imp = new PeImport();
                        imp.Dll = CStr(d, rva(nameRva));
                        long t = rva(oft != 0 ? oft : ft);
                        for (int k = 0; k < 65536; k++) {
                            ulong v = is64 ? U64(d, t) : U32(d, t);
                            if (v == 0) break;
                            bool byOrd = is64 ? (v & 0x8000000000000000UL) != 0 : (v & 0x80000000UL) != 0;
                            if (byOrd) imp.Ordinals.Add((uint)(v & 0xFFFF));
                            else imp.Names.Add(CStr(d, rva((uint)(v & 0x7FFFFFFF)) + 2));
                            t += is64 ? 8 : 4;
                        }
                        img.Imports.Add(imp);
                    }
                }
            }
            if (ndirs > 13) {
                uint dlRva = U32(d, dirs + 13 * 8);
                if (dlRva != 0) {
                    long o = rva(dlRva);
                    for (int guard = 0; guard < 1024; guard++, o += 32) {
                        uint nameRva = U32(d, o + 4);
                        if (nameRva == 0) break;
                        img.DelayImports.Add(CStr(d, rva(nameRva)));
                    }
                }
            }
            return img;
        } catch (Exception ex) {
            error = "tablas PE ilegibles: " + ex.Message;
            return null;
        }
    }
}

// Resuelve una DLL con el orden de busqueda estandar de una app de escritorio (SafeDllSearchMode):
// KnownDLLs, carpeta del exe, System32, System, Windows, carpeta de trabajo, PATH del hijo.
public class Resolver {
    string exeDir, workDir, sys32, sysDir, winDir;
    List<string> pathDirs = new List<string>();
    HashSet<string> known = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

    public Resolver(string exeDir, string workDir, string childPath) : this(exeDir, workDir, childPath, 0x8664) { }

    // Para un exe de 32 bits (i386) el hijo corre bajo WoW64: cuando pide System32 el sistema le
    // da SysWOW64 (redireccion del sistema de archivos). El runner corre en 64 bits y ve el
    // System32 real, asi que la traduccion se hace aca: KnownDLLs, System32 y las entradas del
    // PATH que apunten a System32.
    public Resolver(string exeDir, string workDir, string childPath, ushort machine) {
        this.exeDir = exeDir;
        this.workDir = workDir;
        winDir = Environment.GetEnvironmentVariable("SystemRoot") ?? @"C:\Windows";
        string realSys32 = System.IO.Path.Combine(winDir, "System32");
        bool wow = machine == 0x14C;
        sys32 = wow ? System.IO.Path.Combine(winDir, "SysWOW64") : realSys32;
        sysDir = System.IO.Path.Combine(winDir, "System");
        foreach (string p in (childPath ?? "").Split(';')) {
            string t = p.Trim().Trim('"');
            if (t.Length == 0) continue;
            if (wow) {
                string full = t.TrimEnd('\\');
                if (full.Equals(realSys32, StringComparison.OrdinalIgnoreCase)) t = sys32;
                else if (full.StartsWith(realSys32 + "\\", StringComparison.OrdinalIgnoreCase)) t = sys32 + full.Substring(realSys32.Length);
            }
            pathDirs.Add(t);
        }
        try {
            using (Microsoft.Win32.RegistryKey k = Microsoft.Win32.Registry.LocalMachine.OpenSubKey(@"SYSTEM\CurrentControlSet\Control\Session Manager\KnownDLLs")) {
                if (k != null) foreach (string n in k.GetValueNames()) { object v = k.GetValue(n); if (v is string) known.Add((string)v); }
            }
        } catch { }
    }

    public string WinDir { get { return winDir; } }

    public bool IsSystem(string path) {
        return path.StartsWith(winDir.TrimEnd('\\') + "\\", StringComparison.OrdinalIgnoreCase);
    }

    static readonly HashSet<string> sxs = new HashSet<string>(StringComparer.OrdinalIgnoreCase) {
        "comctl32.dll", "gdiplus.dll",
        "msvcr80.dll", "msvcp80.dll", "msvcm80.dll", "atl80.dll", "mfc80.dll", "mfc80u.dll",
        "msvcr90.dll", "msvcp90.dll", "msvcm90.dll", "atl90.dll", "mfc90.dll", "mfc90u.dll"
    };
    public static bool IsSideBySide(string dll) { return sxs.Contains(dll); }

    public static bool IsApiSet(string dll) {
        string l = dll.ToLowerInvariant();
        return l.StartsWith("api-ms-") || l.StartsWith("ext-ms-");
    }

    public string Resolve(string dll) {
        if (known.Contains(dll)) {
            string k = System.IO.Path.Combine(sys32, dll);
            if (File.Exists(k)) return k;
        }
        List<string> dirs = new List<string>();
        dirs.Add(exeDir); dirs.Add(sys32); dirs.Add(sysDir); dirs.Add(winDir);
        if (!string.IsNullOrEmpty(workDir)) dirs.Add(workDir);
        dirs.AddRange(pathDirs);
        foreach (string dir in dirs) {
            try {
                string c = System.IO.Path.Combine(dir, dll);
                if (File.Exists(c)) return System.IO.Path.GetFullPath(c);
            } catch { }
        }
        return null;
    }
}

public class DepReport {
    public List<string> Problems = new List<string>();
    public Dictionary<string, string> Resolved = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
    public int Checked;
}

public static class Deps {
    // Recorre las importaciones estaticas: toda DLL tiene que resolverse y estar entera, y toda
    // funcion importada tiene que existir en ella (eso es el "entry point not found"), tambien en
    // las DLL del sistema. Excepciones: los api-sets (api-ms-win-*, los resuelve el loader por
    // esquema) y las DLL side-by-side (comctl32 v6, gdiplus, CRT 8/9), que Windows redirige a
    // WinSxS segun el manifiesto. Un nombre reexportado (forwarder) figura en la tabla de nombres
    // como cualquier otro, asi que alcanza con buscarlo ahi.
    public static void Check(string rootPath, ushort machine, Resolver r, DepReport rep) {
        Dictionary<string, PeImage> cache = new Dictionary<string, PeImage>(StringComparer.OrdinalIgnoreCase);
        Queue<string> q = new Queue<string>();
        HashSet<string> seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        q.Enqueue(rootPath); seen.Add(rootPath);
        while (q.Count > 0) {
            string cur = q.Dequeue();
            string err;
            PeImage img;
            if (!cache.TryGetValue(cur, out img)) {
                img = Pe.Read(cur, true, out err);
                if (img == null) { rep.Problems.Add("imagen invalida: " + cur + " (" + err + ")"); continue; }
                cache[cur] = img;
            }
            rep.Checked++;
            foreach (PeImport imp in img.Imports) {
                if (Resolver.IsApiSet(imp.Dll)) continue;
                string path = r.Resolve(imp.Dll);
                if (path == null) { rep.Problems.Add("falta " + imp.Dll + " (la importa " + System.IO.Path.GetFileName(cur) + ")"); continue; }
                if (!rep.Resolved.ContainsKey(imp.Dll)) rep.Resolved[imp.Dll] = path;
                bool sys = r.IsSystem(path);
                // Estas las redirige Windows por version (side-by-side, segun el manifiesto del
                // exe): la copia de System32 no es la que se carga, sus exportaciones no dicen nada.
                if (sys && Resolver.IsSideBySide(imp.Dll)) continue;
                PeImage dep;
                if (!cache.TryGetValue(path, out dep)) {
                    dep = Pe.Read(path, true, out err);
                    if (dep == null) { rep.Problems.Add("imagen invalida: " + path + " (" + err + ")"); continue; }
                    cache[path] = dep;
                }
                if (dep.Machine != machine) { rep.Problems.Add("arquitectura distinta: " + path + " (0x" + dep.Machine.ToString("X") + ")"); continue; }
                int missing = 0; List<string> sample = new List<string>();
                foreach (string n in imp.Names) if (!dep.ExportNames.Contains(n)) { missing++; if (sample.Count < 4) sample.Add(n); }
                foreach (uint o in imp.Ordinals) if (o < dep.OrdinalBase || o >= dep.OrdinalBase + dep.NumberOfFunctions) { missing++; if (sample.Count < 4) sample.Add("#" + o); }
                if (missing > 0) rep.Problems.Add("punto de entrada no encontrado: " + missing + " funciones de " + path + " que pide " + System.IO.Path.GetFileName(cur) + " (p. ej. " + string.Join(", ", sample.ToArray()) + ")");
                // Las DLL del sistema se comprueban (existen, estan enteras, tienen las funciones
                // pedidas) pero no se recorren: su propio grafo lo mantiene Windows.
                if (!sys && seen.Add(path)) q.Enqueue(path);
            }
        }
    }
}

public class RunResult {
    public bool Launched;
    public string Desktop;
    public uint Pid;
    public string Reason = "";           // exit, timeout, dialog, error
    public bool RootExited;
    public long ExitCode = -1;
    public bool KilledByRunner;
    public double ElapsedSec;
    public List<uint> JobPids = new List<uint>();
    public List<uint> Unkillable = new List<uint>();
    public List<uint> Leftover = new List<uint>();
    public List<string> Dialogs = new List<string>();
    public int HiddenWindows;
    public List<string> VisibleJob = new List<string>();
    public List<string> SysNew = new List<string>();
    public bool SysCheckAvailable = true;
    public bool DialogCheckAvailable = true;
    public string Error = "";
    public List<string> Log = new List<string>();
}

public static class Runner {
    const uint CREATE_SUSPENDED = 0x00000004;
    const uint CREATE_UNICODE_ENVIRONMENT = 0x00000400;
    const uint EXTENDED_STARTUPINFO_PRESENT = 0x00080000;
    const uint CREATE_NO_WINDOW = 0x08000000;
    const int STARTF_USESTDHANDLES = 0x00000100;
    const uint SEM_FAILCRITICALERRORS = 0x0001;
    const uint SEM_NOGPFAULTERRORBOX = 0x0002;
    const uint JOB_KILL_ON_JOB_CLOSE = 0x00002000;
    const uint JOB_DIE_ON_UNHANDLED_EXCEPTION = 0x00000400;
    const uint KILL_EXIT_CODE = 0xDEAD;

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct STARTUPINFO {
        public int cb; public string lpReserved; public string lpDesktop; public string lpTitle;
        public int dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
        public short wShowWindow, cbReserved2;
        public IntPtr lpReserved2, hStdInput, hStdOutput, hStdError;
    }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct STARTUPINFOEX { public STARTUPINFO StartupInfo; public IntPtr lpAttributeList; }
    [StructLayout(LayoutKind.Sequential)]
    struct PROCESS_INFORMATION { public IntPtr hProcess, hThread; public uint dwProcessId, dwThreadId; }
    [StructLayout(LayoutKind.Sequential)]
    struct SECURITY_ATTRIBUTES { public int nLength; public IntPtr lpSecurityDescriptor; public bool bInheritHandle; }
    [StructLayout(LayoutKind.Sequential)]
    struct JOBOBJECT_BASIC_LIMIT_INFORMATION {
        public long PerProcessUserTimeLimit, PerJobUserTimeLimit; public uint LimitFlags;
        public UIntPtr MinimumWorkingSetSize, MaximumWorkingSetSize; public uint ActiveProcessLimit;
        public UIntPtr Affinity; public uint PriorityClass, SchedulingClass;
    }
    [StructLayout(LayoutKind.Sequential)]
    struct IO_COUNTERS { public ulong a, b, c, d, e, f; }
    [StructLayout(LayoutKind.Sequential)]
    struct JOBOBJECT_EXTENDED_LIMIT_INFORMATION {
        public JOBOBJECT_BASIC_LIMIT_INFORMATION Basic; public IO_COUNTERS Io;
        public UIntPtr ProcessMemoryLimit, JobMemoryLimit, PeakProcessMemoryUsed, PeakJobMemoryUsed;
    }

    delegate bool EnumProc(IntPtr hwnd, IntPtr lParam);

    [DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Unicode)] static extern IntPtr CreateDesktopW(string name, IntPtr dev, IntPtr mode, uint flags, uint access, IntPtr sa);
    [DllImport("user32.dll", SetLastError = true)] static extern bool CloseDesktop(IntPtr h);
    [DllImport("user32.dll", SetLastError = true)] static extern bool SetThreadDesktop(IntPtr h);
    [DllImport("user32.dll", SetLastError = true)] static extern IntPtr OpenInputDesktop(uint flags, bool inherit, uint access);
    [DllImport("user32.dll", SetLastError = true)] static extern IntPtr GetProcessWindowStation();
    [DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Unicode)] static extern bool GetUserObjectInformationW(IntPtr h, int index, StringBuilder buf, int len, out int needed);
    [DllImport("user32.dll", SetLastError = true)] static extern bool EnumDesktopWindows(IntPtr desk, EnumProc proc, IntPtr lParam);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr parent, EnumProc proc, IntPtr lParam);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassNameW(IntPtr hwnd, StringBuilder buf, int max);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int InternalGetWindowText(IntPtr hwnd, StringBuilder buf, int max);
    [DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Unicode)] static extern IntPtr CreateWindowExW(uint ex, string cls, string name, uint style, int x, int y, int w, int h, IntPtr parent, IntPtr menu, IntPtr inst, IntPtr param);
    [DllImport("user32.dll")] static extern bool DestroyWindow(IntPtr hwnd);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)] static extern bool CreateProcessW(string app, StringBuilder cmd, IntPtr pa, IntPtr ta, bool inherit, uint flags, IntPtr env, string cwd, ref STARTUPINFOEX si, out PROCESS_INFORMATION pi);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool InitializeProcThreadAttributeList(IntPtr list, int count, int flags, ref IntPtr size);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool UpdateProcThreadAttribute(IntPtr list, uint flags, IntPtr attr, IntPtr value, IntPtr size, IntPtr prev, IntPtr retSize);
    [DllImport("kernel32.dll")] static extern void DeleteProcThreadAttributeList(IntPtr list);
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)] static extern IntPtr CreateFileW(string name, uint access, uint share, ref SECURITY_ATTRIBUTES sa, uint disp, uint flags, IntPtr tmpl);
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)] static extern IntPtr CreateJobObjectW(IntPtr sa, string name);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool SetInformationJobObject(IntPtr job, int cls, ref JOBOBJECT_EXTENDED_LIMIT_INFORMATION info, uint len);
    [DllImport("kernel32.dll", SetLastError = true, EntryPoint = "SetInformationJobObject")] static extern bool SetJobUiRestrictions(IntPtr job, int cls, ref uint uiClass, uint len);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool QueryInformationJobObject(IntPtr job, int cls, IntPtr info, uint len, out uint ret);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool AssignProcessToJobObject(IntPtr job, IntPtr proc);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool TerminateJobObject(IntPtr job, uint code);
    [DllImport("kernel32.dll", SetLastError = true)] static extern uint ResumeThread(IntPtr thread);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool TerminateProcess(IntPtr proc, uint code);
    [DllImport("kernel32.dll", SetLastError = true)] static extern uint WaitForSingleObject(IntPtr h, uint ms);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool GetExitCodeProcess(IntPtr h, out uint code);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll", SetLastError = true)] static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool GetProcessTimes(IntPtr h, out long creation, out long exit, out long kernel, out long user);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool IsProcessInJob(IntPtr proc, IntPtr job, out bool result);
    [DllImport("kernel32.dll")] static extern uint SetErrorMode(uint mode);
    [DllImport("kernel32.dll")] static extern uint GetErrorMode();
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] static extern IntPtr GetModuleHandleW(string name);

    static readonly IntPtr INVALID = new IntPtr(-1);

    static string ClassOf(IntPtr h) { StringBuilder b = new StringBuilder(256); GetClassNameW(h, b, b.Capacity); return b.ToString(); }
    static string TextOf(IntPtr h) { StringBuilder b = new StringBuilder(1024); InternalGetWindowText(h, b, b.Capacity); return b.ToString(); }

    // Texto de un dialogo: titulo + texto de sus controles hijos. InternalGetWindowText no manda
    // mensajes al proceso dueno: no puede bloquearse ni activar nada.
    static string DialogText(IntPtr h) {
        List<string> parts = new List<string>();
        parts.Add(TextOf(h));
        EnumChildWindows(h, delegate(IntPtr c, IntPtr l) {
            string t = TextOf(c);
            if (t.Length > 0) parts.Add(t);
            return true;
        }, IntPtr.Zero);
        return string.Join(" | ", parts.ToArray()).Replace("\r", " ").Replace("\n", " ");
    }

    public static string WindowStationName() {
        StringBuilder b = new StringBuilder(256); int n;
        IntPtr ws = GetProcessWindowStation();
        if (ws != IntPtr.Zero && GetUserObjectInformationW(ws, 2, b, b.Capacity * 2, out n)) return b.ToString();
        return "WinSta0";
    }

    static IntPtr NewDesktop(out string name) {
        name = "LGA_QA_" + Guid.NewGuid().ToString("N");
        // GENERIC_ALL sobre el escritorio nuevo: crear, enumerar y cerrar.
        return CreateDesktopW(name, IntPtr.Zero, IntPtr.Zero, 0, 0x10000000, IntPtr.Zero);
    }

    // Escaneo del escritorio aparte, desde un hilo asociado a ese escritorio.
    class HiddenScan {
        public IntPtr Desk;
        public bool RequireVisible = true;
        public volatile bool Stop;
        public volatile bool DialogFound;
        public volatile bool Failed;
        public string FailText = "";
        public readonly object Lock = new object();
        public Dictionary<IntPtr, string> Dialogs = new Dictionary<IntPtr, string>();
        public HashSet<IntPtr> Windows = new HashSet<IntPtr>();

        public void ScanOnce() {
            EnumDesktopWindows(Desk, delegate(IntPtr h, IntPtr l) {
                bool vis = IsWindowVisible(h);
                if (RequireVisible && !vis) return true;
                lock (Lock) {
                    Windows.Add(h);
                    if (ClassOf(h) == "#32770" && !Dialogs.ContainsKey(h)) {
                        uint pid; GetWindowThreadProcessId(h, out pid);
                        Dialogs[h] = "pid=" + pid + " texto=\"" + DialogText(h) + "\"";
                        DialogFound = true;
                    }
                }
                return true;
            }, IntPtr.Zero);
        }

        public void Loop() {
            if (!SetThreadDesktop(Desk)) { FailText = "SetThreadDesktop fallo: " + Marshal.GetLastWin32Error(); Failed = true; return; }
            while (!Stop) {
                try { ScanOnce(); } catch (Exception ex) { FailText = ex.Message; }
                Thread.Sleep(200);
            }
        }
    }

    static List<uint> JobPids(IntPtr job) {
        List<uint> res = new List<uint>();
        int max = 4096;
        int size = 8 + IntPtr.Size * max;
        IntPtr buf = Marshal.AllocHGlobal(size);
        try {
            uint ret;
            // JobObjectBasicProcessIdList = 3
            if (QueryInformationJobObject(job, 3, buf, (uint)size, out ret) || Marshal.GetLastWin32Error() == 234) {
                int n = Marshal.ReadInt32(buf, 4);
                for (int i = 0; i < n && i < max; i++) res.Add((uint)Marshal.ReadIntPtr(buf, 8 + IntPtr.Size * i).ToInt64());
            }
        } finally { Marshal.FreeHGlobal(buf); }
        return res;
    }

    static bool Alive(uint pid) {
        // SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION
        IntPtr h = OpenProcess(0x00100000 | 0x1000, false, pid);
        if (h == IntPtr.Zero) return false;
        try { return WaitForSingleObject(h, 0) != 0; } finally { CloseHandle(h); }
    }

    // Identidad de un proceso del grupo: PID + hora de creacion. El PID solo NO alcanza: cuando un
    // proceso del Job muere, Windows puede darle su numero a otro proceso cualquiera (de otra app,
    // de otra sesion), y una ventana de ese proceso ajeno contaba como "visible_job". Paso en una
    // auditoria: una ventana de otra app abierta desde otra sesion dio visible_job=1.
    static long CreationOf(IntPtr h) {
        long c, e, k, u;
        if (GetProcessTimes(h, out c, out e, out k, out u)) return c;
        return -1;
    }

    public static long CreationTime(uint pid) {
        IntPtr h = OpenProcess(0x1000, false, pid); // PROCESS_QUERY_LIMITED_INFORMATION
        if (h == IntPtr.Zero) return -1;
        try { return CreationOf(h); } finally { CloseHandle(h); }
    }

    // Anota un PID del grupo solo si, con el MISMO handle, el proceso esta en el Job: asi la hora de
    // creacion que se guarda es la del proceso del grupo y no la de uno que recibio el PID reciclado
    // entre la consulta al Job y esta llamada. Si el PID ya estaba anotado y ahora es otro proceso
    // del Job, se actualiza.
    public static void TrackJobMember(IntPtr job, uint pid, Dictionary<uint, long> own) {
        IntPtr h = OpenProcess(0x1000, false, pid);
        if (h == IntPtr.Zero) return;
        try {
            bool inJob;
            if (!IsProcessInJob(h, job, out inJob) || !inJob) return;
            long c = CreationOf(h);
            if (c > 0) own[pid] = c;
        } finally { CloseHandle(h); }
    }

    // Una ventana es del grupo si su PID esta anotado Y el proceso que hoy tiene ese PID nacio a la
    // misma hora. Si no se puede abrir el proceso (otra sesion, sin permiso), no es del grupo.
    public static bool IsOwn(uint pid, Dictionary<uint, long> own) {
        long c;
        if (!own.TryGetValue(pid, out c)) return false;
        return CreationTime(pid) == c;
    }

    static string ProcName(uint pid, Dictionary<uint, string> cache) {
        string n;
        if (cache.TryGetValue(pid, out n)) return n;
        try { n = Process.GetProcessById((int)pid).ProcessName.ToLowerInvariant(); } catch { n = ""; }
        cache[pid] = n;
        return n;
    }

    static bool IsSysDialogProc(string name) {
        return name == "csrss" || name == "werfault" || name == "werfaultsecure";
    }

    // Foto del escritorio activo: ventanas visibles de csrss/WerFault (para la linea base) y,
    // si se pasan los procesos del grupo (PID + hora de creacion), ventanas visibles de esos procesos.
    static bool SampleInput(HashSet<IntPtr> baseline, Dictionary<uint, long> own, Dictionary<uint, string> names,
                            Dictionary<IntPtr, string> sysNew, Dictionary<IntPtr, string> visibleJob, bool isBaseline) {
        IntPtr d = OpenInputDesktop(0, false, 0x0041); // DESKTOP_READOBJECTS | DESKTOP_ENUMERATE
        if (d == IntPtr.Zero) return false;
        Dictionary<uint, bool> ownCache = new Dictionary<uint, bool>();
        try {
            EnumDesktopWindows(d, delegate(IntPtr h, IntPtr l) {
                if (!IsWindowVisible(h)) return true;
                uint pid; GetWindowThreadProcessId(h, out pid);
                if (pid == 0) return true;
                bool mine = false;
                if (own != null && !ownCache.TryGetValue(pid, out mine)) { mine = IsOwn(pid, own); ownCache[pid] = mine; }
                if (mine) {
                    if (!visibleJob.ContainsKey(h)) visibleJob[h] = "pid=" + pid + " clase=" + ClassOf(h) + " titulo=\"" + TextOf(h) + "\"";
                    return true;
                }
                if (!IsSysDialogProc(ProcName(pid, names))) return true;
                if (isBaseline) { baseline.Add(h); return true; }
                if (!baseline.Contains(h) && !sysNew.ContainsKey(h)) sysNew[h] = ProcName(pid, names) + " pid=" + pid + " texto=\"" + DialogText(h) + "\"";
                return true;
            }, IntPtr.Zero);
        } finally { CloseDesktop(d); }
        return true;
    }

    public static RunResult Run(string exe, string args, string workDir, string[] env, string stdoutPath, string stderrPath, int timeoutSec, int killWaitSec) {
        RunResult r = new RunResult();
        Stopwatch sw = new Stopwatch();
        IntPtr desk = IntPtr.Zero, job = IntPtr.Zero, attrList = IntPtr.Zero, handleArray = IntPtr.Zero, envBlock = IntPtr.Zero;
        IntPtr hOut = INVALID, hErr = INVALID, hIn = INVALID;
        PROCESS_INFORMATION pi = new PROCESS_INFORMATION();
        HiddenScan scan = null; Thread scanThread = null;
        uint oldMode = GetErrorMode();
        HashSet<IntPtr> baseline = new HashSet<IntPtr>();
        Dictionary<uint, string> names = new Dictionary<uint, string>();
        Dictionary<IntPtr, string> sysNew = new Dictionary<IntPtr, string>();
        Dictionary<IntPtr, string> visibleJob = new Dictionary<IntPtr, string>();
        HashSet<uint> seenPids = new HashSet<uint>();
        Dictionary<uint, long> own = new Dictionary<uint, long>();
        try {
            // 1. Modo de error del runner, heredable (sin CREATE_DEFAULT_ERROR_MODE en el hijo).
            SetErrorMode(oldMode | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);

            // 2. Escritorio aparte con nombre unico.
            string deskName;
            desk = NewDesktop(out deskName);
            if (desk == IntPtr.Zero) { r.Reason = "error"; r.Error = "CreateDesktop fallo: " + Marshal.GetLastWin32Error(); return r; }
            r.Desktop = deskName;

            // 3. Job: al cerrarse el handle muere todo el grupo; excepcion no manejada = muerte sin WER.
            job = CreateJobObjectW(IntPtr.Zero, null);
            if (job == IntPtr.Zero) { r.Reason = "error"; r.Error = "CreateJobObject fallo: " + Marshal.GetLastWin32Error(); return r; }
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION li = new JOBOBJECT_EXTENDED_LIMIT_INFORMATION();
            li.Basic.LimitFlags = JOB_KILL_ON_JOB_CLOSE | JOB_DIE_ON_UNHANDLED_EXCEPTION;
            if (!SetInformationJobObject(job, 9, ref li, (uint)Marshal.SizeOf(typeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION)))) {
                r.Reason = "error"; r.Error = "SetInformationJobObject fallo: " + Marshal.GetLastWin32Error(); return r;
            }
            // Sin cambiar de escritorio, cerrar la sesion, tocar la pantalla ni parametros del sistema.
            // UILIMIT_SYSTEMPARAMETERS 0x08 | DISPLAYSETTINGS 0x10 | DESKTOP 0x40 | EXITWINDOWS 0x80
            uint ui = 0x08 | 0x10 | 0x40 | 0x80;
            if (!SetJobUiRestrictions(job, 4, ref ui, 4)) {
                r.Reason = "error"; r.Error = "restricciones de UI del Job fallaron: " + Marshal.GetLastWin32Error(); return r;
            }

            // 4. Salidas a archivo; el hijo hereda SOLO estos tres handles (lista explicita).
            SECURITY_ATTRIBUTES sa = new SECURITY_ATTRIBUTES();
            sa.nLength = Marshal.SizeOf(typeof(SECURITY_ATTRIBUTES)); sa.bInheritHandle = true;
            hOut = CreateFileW(stdoutPath, 0x40000000, 0x1 | 0x2, ref sa, 2, 0x80, IntPtr.Zero);
            hErr = CreateFileW(stderrPath, 0x40000000, 0x1 | 0x2, ref sa, 2, 0x80, IntPtr.Zero);
            hIn = CreateFileW("NUL", 0x80000000, 0x1 | 0x2, ref sa, 3, 0x80, IntPtr.Zero);
            if (hOut == INVALID || hErr == INVALID || hIn == INVALID) { r.Reason = "error"; r.Error = "no se pudieron abrir stdout/stderr/NUL: " + Marshal.GetLastWin32Error(); return r; }

            IntPtr attrSize = IntPtr.Zero;
            InitializeProcThreadAttributeList(IntPtr.Zero, 1, 0, ref attrSize);
            attrList = Marshal.AllocHGlobal(attrSize);
            if (!InitializeProcThreadAttributeList(attrList, 1, 0, ref attrSize)) { r.Reason = "error"; r.Error = "InitializeProcThreadAttributeList fallo: " + Marshal.GetLastWin32Error(); Marshal.FreeHGlobal(attrList); attrList = IntPtr.Zero; return r; }
            handleArray = Marshal.AllocHGlobal(IntPtr.Size * 3);
            Marshal.WriteIntPtr(handleArray, 0, hIn);
            Marshal.WriteIntPtr(handleArray, IntPtr.Size, hOut);
            Marshal.WriteIntPtr(handleArray, IntPtr.Size * 2, hErr);
            // PROC_THREAD_ATTRIBUTE_HANDLE_LIST = 0x20002
            if (!UpdateProcThreadAttribute(attrList, 0, new IntPtr(0x20002), handleArray, new IntPtr(IntPtr.Size * 3), IntPtr.Zero, IntPtr.Zero)) {
                r.Reason = "error"; r.Error = "UpdateProcThreadAttribute fallo: " + Marshal.GetLastWin32Error(); return r;
            }

            STARTUPINFOEX si = new STARTUPINFOEX();
            si.StartupInfo.cb = Marshal.SizeOf(typeof(STARTUPINFOEX));
            si.StartupInfo.lpDesktop = WindowStationName() + "\\" + deskName;
            si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
            si.StartupInfo.hStdInput = hIn; si.StartupInfo.hStdOutput = hOut; si.StartupInfo.hStdError = hErr;
            si.lpAttributeList = attrList;

            StringBuilder sb = new StringBuilder();
            foreach (string kv in env) { sb.Append(kv); sb.Append('\0'); }
            sb.Append('\0');
            envBlock = Marshal.StringToHGlobalUni(sb.ToString());

            // Linea base del escritorio activo antes de lanzar.
            if (!SampleInput(baseline, null, names, sysNew, visibleJob, true)) { r.SysCheckAvailable = false; r.Log.Add("OpenInputDesktop fallo: sin control de csrss/WerFault"); }

            // 5. Monitor del escritorio aparte (hilo propio asociado a ese escritorio).
            scan = new HiddenScan(); scan.Desk = desk;
            scanThread = new Thread(scan.Loop); scanThread.IsBackground = true; scanThread.Start();

            // 6. Crear suspendido, sin ventana de consola y SIN CREATE_NEW_CONSOLE ni CREATE_DEFAULT_ERROR_MODE.
            StringBuilder cmd = new StringBuilder("\"" + exe + "\"" + (string.IsNullOrEmpty(args) ? "" : " " + args));
            uint flags = CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT;
            if (!CreateProcessW(exe, cmd, IntPtr.Zero, IntPtr.Zero, true, flags, envBlock, workDir, ref si, out pi)) {
                r.Reason = "error"; r.Error = "CreateProcess fallo: " + Marshal.GetLastWin32Error(); return r;
            }
            r.Launched = true; r.Pid = pi.dwProcessId;

            // 7. Al Job ANTES de reanudar: ningun hijo puede nacer fuera del grupo.
            if (!AssignProcessToJobObject(job, pi.hProcess)) {
                int e = Marshal.GetLastWin32Error();
                TerminateProcess(pi.hProcess, KILL_EXIT_CODE);
                WaitForSingleObject(pi.hProcess, 5000);
                r.KilledByRunner = true; r.Reason = "error"; r.Error = "AssignProcessToJobObject fallo: " + e + " (se termino el proceso suspendido)";
                return r;
            }
            // La raiz: su handle queda abierto hasta el final, asi que su PID no se recicla.
            long rootCreation = CreationOf(pi.hProcess);
            if (rootCreation > 0) own[pi.dwProcessId] = rootCreation;
            sw.Start();
            if (ResumeThread(pi.hThread) == 0xFFFFFFFF) {
                int e = Marshal.GetLastWin32Error();
                TerminateJobObject(job, KILL_EXIT_CODE);
                r.KilledByRunner = true; r.Reason = "error"; r.Error = "ResumeThread fallo: " + e + " (se termino el grupo)";
                return r;
            }

            // 8. Espera con sondeo: salida, dialogo, timeout.
            long limitMs = (long)timeoutSec * 1000;
            while (true) {
                uint w = WaitForSingleObject(pi.hProcess, 200);
                foreach (uint p in JobPids(job)) { seenPids.Add(p); TrackJobMember(job, p, own); }
                seenPids.Add(pi.dwProcessId);
                if (r.SysCheckAvailable) SampleInput(baseline, own, names, sysNew, visibleJob, false);
                if (scan.DialogFound) { r.Reason = "dialog"; break; }
                if (w == 0) { r.Reason = "exit"; r.RootExited = true; break; }
                if (sw.ElapsedMilliseconds > limitMs) { r.Reason = "timeout"; break; }
            }
            if (!scan.DialogFound) { Thread.Sleep(250); }
            if (scan.DialogFound && r.Reason == "exit") r.Reason = "dialog";

            uint code;
            if (r.RootExited && GetExitCodeProcess(pi.hProcess, out code)) r.ExitCode = code;

            // 9. Terminar lo que quede del grupo (dialogo, timeout o descendientes sobrevivientes).
            List<uint> live = new List<uint>();
            foreach (uint p in JobPids(job)) { seenPids.Add(p); TrackJobMember(job, p, own); if (Alive(p)) live.Add(p); }
            if (r.RootExited && live.Count > 0) r.Leftover.AddRange(live);
            if (!r.RootExited || live.Count > 0) {
                TerminateJobObject(job, KILL_EXIT_CODE);
                r.KilledByRunner = true;
                Stopwatch kw = Stopwatch.StartNew();
                while (true) {
                    live.Clear();
                    foreach (uint p in JobPids(job)) if (Alive(p)) live.Add(p);
                    if (live.Count == 0 || kw.ElapsedMilliseconds > (long)killWaitSec * 1000) break;
                    Thread.Sleep(200);
                }
                // Sin reintentos ni matar por nombre: lo que no murio se informa y se sigue.
                r.Unkillable.AddRange(live);
                if (!r.RootExited && GetExitCodeProcess(pi.hProcess, out code) && code != 259) r.ExitCode = code;
            }
            sw.Stop();
            r.ElapsedSec = sw.Elapsed.TotalSeconds;

            // Ultima foto: un hard error puede aparecer justo cuando el proceso muere.
            Thread.Sleep(500);
            if (r.SysCheckAvailable) SampleInput(baseline, own, names, sysNew, visibleJob, false);
        } catch (Exception ex) {
            r.Reason = "error"; r.Error = "excepcion: " + ex.Message;
            if (job != IntPtr.Zero && r.Launched) { TerminateJobObject(job, KILL_EXIT_CODE); r.KilledByRunner = true; }
        } finally {
            if (scan != null) {
                scan.Stop = true;
                if (scanThread != null) scanThread.Join(3000);
                r.DialogCheckAvailable = !scan.Failed;
                if (scan.Failed) r.Log.Add("monitor del escritorio aparte no disponible: " + scan.FailText);
                lock (scan.Lock) { r.HiddenWindows = scan.Windows.Count; foreach (string s in scan.Dialogs.Values) r.Dialogs.Add(s); }
            }
            foreach (string s in sysNew.Values) r.SysNew.Add(s);
            foreach (string s in visibleJob.Values) r.VisibleJob.Add(s);
            r.JobPids.AddRange(seenPids);
            if (pi.hThread != IntPtr.Zero) CloseHandle(pi.hThread);
            if (pi.hProcess != IntPtr.Zero) CloseHandle(pi.hProcess);
            // Cerrar el Job con KILL_ON_JOB_CLOSE: ultima garantia de que no queda nada propio.
            if (job != IntPtr.Zero) CloseHandle(job);
            if (attrList != IntPtr.Zero) { DeleteProcThreadAttributeList(attrList); Marshal.FreeHGlobal(attrList); }
            if (handleArray != IntPtr.Zero) Marshal.FreeHGlobal(handleArray);
            if (envBlock != IntPtr.Zero) Marshal.FreeHGlobal(envBlock);
            if (hOut != INVALID) CloseHandle(hOut);
            if (hErr != INVALID) CloseHandle(hErr);
            if (hIn != INVALID) CloseHandle(hIn);
            // El escritorio se destruye cuando se cierra el ultimo handle y no le quedan hilos.
            if (desk != IntPtr.Zero && !CloseDesktop(desk)) r.Log.Add("CloseDesktop fallo: " + Marshal.GetLastWin32Error());
            SetErrorMode(oldMode);
        }
        return r;
    }

    // Prueba del detector sin ningun proceso externo: un hilo del runner, asociado a un escritorio
    // aparte, crea un dialogo #32770 con un texto hijo SIN mostrarlo (sin WS_VISIBLE). Nada de esto
    // puede verse: no esta visible y ademas vive en un escritorio sin entrada.
    public static string DetectorProbe() {
        string name; IntPtr desk = NewDesktop(out name);
        if (desk == IntPtr.Zero) return "FAIL CreateDesktop " + Marshal.GetLastWin32Error();
        string result = "FAIL sin resultado";
        Thread t = new Thread(delegate() {
            if (!SetThreadDesktop(desk)) { result = "FAIL SetThreadDesktop " + Marshal.GetLastWin32Error(); return; }
            IntPtr inst = GetModuleHandleW(null);
            // WS_POPUP, sin WS_VISIBLE
            IntPtr dlg = CreateWindowExW(0, "#32770", "LGA detector", 0x80000000, 0, 0, 200, 100, IntPtr.Zero, IntPtr.Zero, inst, IntPtr.Zero);
            if (dlg == IntPtr.Zero) { result = "FAIL CreateWindowEx " + Marshal.GetLastWin32Error(); return; }
            // WS_CHILD, sin WS_VISIBLE
            IntPtr st = CreateWindowExW(0, "Static", "texto de prueba del detector", 0x40000000, 0, 0, 150, 20, dlg, IntPtr.Zero, inst, IntPtr.Zero);
            HiddenScan s = new HiddenScan(); s.Desk = desk; s.RequireVisible = false;
            s.ScanOnce();
            string found = "";
            foreach (string v in s.Dialogs.Values) found += v;
            bool visible = IsWindowVisible(dlg);
            if (st != IntPtr.Zero) DestroyWindow(st);
            DestroyWindow(dlg);
            if (visible) result = "FAIL la ventana de prueba quedo visible";
            else if (found.Contains("LGA detector") && found.Contains("texto de prueba del detector")) result = "OK " + found;
            else result = "FAIL no detectado (" + found + ")";
        });
        t.Start(); t.Join(10000);
        CloseDesktop(desk);
        return result;
    }

    // Prueba de la identidad PID + hora de creacion, sin crear procesos ni ventanas: usa el propio
    // proceso del runner. Un PID anotado con otra hora de creacion es exactamente un PID reciclado
    // (mismo numero, otro proceso) y no tiene que contar como del grupo.
    public static string PidReuseProbe() {
        uint me = (uint)Process.GetCurrentProcess().Id;
        long c = CreationTime(me);
        if (c <= 0) return "FAIL no se pudo leer la hora de creacion del runner";
        Dictionary<uint, long> own = new Dictionary<uint, long>();
        own[me] = c;
        bool same = IsOwn(me, own);
        own[me] = c - 10000000; // un segundo antes: el mismo PID, otro proceso
        bool reused = IsOwn(me, own);
        bool unknown = IsOwn(me, new Dictionary<uint, long>());
        // Un Job vacio: el runner no esta adentro, asi que no se anota aunque el PID exista.
        IntPtr job = CreateJobObjectW(IntPtr.Zero, null);
        if (job == IntPtr.Zero) return "FAIL CreateJobObject " + Marshal.GetLastWin32Error();
        Dictionary<uint, long> outside = new Dictionary<uint, long>();
        try { TrackJobMember(job, me, outside); } finally { CloseHandle(job); }
        string detail = "mismo_proceso=" + same + " pid_reciclado=" + reused + " pid_no_anotado=" + unknown + " fuera_del_job_anotado=" + outside.ContainsKey(me);
        if (same && !reused && !unknown && !outside.ContainsKey(me)) return "OK " + detail;
        return "FAIL " + detail;
    }

    public static string CurrentDesktopName() {
        StringBuilder b = new StringBuilder(256); int n;
        IntPtr d = OpenInputDesktop(0, false, 0x0041);
        if (d == IntPtr.Zero) return "";
        try { GetUserObjectInformationW(d, 2, b, b.Capacity * 2, out n); } finally { CloseDesktop(d); }
        return b.ToString();
    }
}
}
'@
}

# ---------------------------------------------------------------------------------------------
# Utilidades
# ---------------------------------------------------------------------------------------------

function Clear-LgaOldRuns([int]$keep) {
    # Borra corridas viejas de %TEMP%\lga_headless, solo carpetas con el nombre que genera este
    # script. Si una tiene un enlace (junction o symlink) adentro no se toca: borrar recursivo a
    # traves de un enlace borra el destino.
    if ($keep -lt 1) { return }
    $root = Join-Path $env:TEMP 'lga_headless'
    if (-not (Test-Path -LiteralPath $root -PathType Container)) { return }
    $rootFull = (Resolve-Path -LiteralPath $root).Path.TrimEnd('\') + '\'
    $runs = @(Get-ChildItem -LiteralPath $root -Directory -Force |
        Where-Object { $_.Name -match '^(\d{8}_\d{6}_[0-9a-f]{8}|selftest_\d{8}_\d{6})$' -and -not ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) } |
        Sort-Object LastWriteTime -Descending)
    foreach ($d in ($runs | Select-Object -Skip $keep)) {
        try {
            if (-not $d.FullName.StartsWith($rootFull, [StringComparison]::OrdinalIgnoreCase)) { continue }
            $items = @(Get-ChildItem -LiteralPath $d.FullName -Recurse -Force -Attributes ReparsePoint -ErrorAction Stop)
            if ($items.Count -gt 0) { continue }
            Remove-Item -LiteralPath $d.FullName -Recurse -Force -ErrorAction Stop
        } catch { }
    }
}

function Test-InstallerName([string]$name) {
    # Por nombre, sin leer el archivo: todo unins*.exe (sea o no de Inno) y el Windows Installer.
    if ($name -match '^unins.*\.exe$') { return "desinstalador por nombre: $name" }
    if ($name -match '^msiexec(\.exe)?$') { return "Windows Installer (msiexec): $name" }
    if ($name -match '\.msi$') { return "paquete MSI: $name" }
    return $null
}

function Test-InstallerBinary([string]$path) {
    # Ningun instalador ni desinstalador, de ningun tipo, se ejecuta en corridas automatizadas:
    # instalan sobre la instalacion real, pisan su entrada de desinstalacion y el registro, crean
    # accesos directos y pueden lanzar la app. Se reconocen por el nombre (todo unins*.exe,
    # msiexec), por el comentario de version que Inno pone a todo lo que compila y por la firma de
    # sus datos: Inno, NSIS y los paquetes de WiX. ISCC.exe no tiene ninguna. Lo que no se reconoce
    # (otro empaquetador, un setup.exe casero) lo frena la regla, no este chequeo.
    $name = [IO.Path]::GetFileName($path)
    $byName = Test-InstallerName $name
    if ($byName) { return $byName }
    try {
        $v = [Diagnostics.FileVersionInfo]::GetVersionInfo($path)
        if ($v.Comments -and $v.Comments -match 'built with Inno Setup') { return "Inno, recurso de version: '$($v.Comments.Trim())'" }
    } catch { }
    $sig = [LgaQa.Pe]::ContainsAnyAscii($path, [string[]]@('Inno Setup Setup Data', 'NullsoftInst', '.wixburn'))
    if ($sig) { return "firma '$sig' en el archivo" }
    return $null
}

function Get-ArgumentInstallers([string]$arguments, [string]$workDir) {
    # Lo mismo para lo que el exe objetivo recibe en -Arguments: un cmd.exe /c "setup.exe /S" o un
    # powershell que llama a msiexec lanzarian el instalador como hijo. Se revisan las rutas .exe
    # que existen y los nombres de instalador aunque no existan. Un driver .ps1 que lanza un setup
    # de prueba desde su propio codigo (seccion 8 de Doc_Instaladores_Inno.md) no pasa rutas por aca.
    $found = New-Object System.Collections.Generic.List[string]
    if (-not $arguments) { return ,$found }
    $cands = New-Object System.Collections.Generic.List[string]
    # Segmentos entre comillas: una ruta con espacios va desde el principio del segmento hasta .exe.
    # Tambien entre comillas simples: un powershell -Command "& 'C:\...\Setup.exe' /VERYSILENT"
    # lleva la ruta entre simples adentro de las dobles, y el segmento doble empieza con "& '". Por
    # eso las dos pasadas son independientes, cada una sobre el texto entero.
    foreach ($pat in '"([^"]*)"', '''([^'']*)''') {
        foreach ($m in [regex]::Matches($arguments, $pat)) {
            $m2 = [regex]::Match($m.Groups[1].Value, '(?i)^\s*(.+?\.(exe|msi))(?=\s|$)')
            if ($m2.Success) { $cands.Add($m2.Groups[1].Value) }
        }
    }
    # Palabras sueltas, y lo que viene despues de un '=' (/DIR=..., --tool=...). Se sacan las comillas
    # dobles, y de las simples solo las de las puntas: 'C:\setup.exe' queda como C:\setup.exe, y una
    # ruta sin comillas con un apostrofe adentro (C:\O'Brien\tool.exe) no se parte.
    foreach ($t0 in (($arguments -replace '"', ' ') -split '\s+')) {
        $t = $t0.Trim("'")
        if (-not $t) { continue }
        $v = if ($t -match '^[^=]*=(.+)$') { $Matches[1].Trim("'") } else { $t }
        if ($v -match '(?i)\.(exe|msi)$' -or $v -match '(?i)^msiexec$') { $cands.Add($v) }
    }
    foreach ($c in $cands) {
        try {
            $why = Test-InstallerName ([IO.Path]::GetFileName($c))
            if (-not $why -and $c -match '(?i)\.exe$') {
                $p = if ([IO.Path]::IsPathRooted($c)) { $c } elseif ($workDir) { Join-Path $workDir $c } else { $null }
                if ($p -and (Test-Path -LiteralPath $p -PathType Leaf)) { $why = Test-InstallerBinary $p }
            }
            if ($why -and -not $found.Contains("$c ($why)")) { $found.Add("$c ($why)") }
        } catch { }
    }
    return ,$found
}

function Get-QtRootFromCMakeCache([string]$exeDir) {
    # Un exe de un arbol de CMake dice contra que Qt se compilo. Se busca en la carpeta del exe y
    # dos niveles arriba.
    $dir = $exeDir
    for ($i = 0; $i -lt 3 -and $dir; $i++) {
        $cache = Join-Path $dir 'CMakeCache.txt'
        if (Test-Path -LiteralPath $cache) {
            $line = Select-String -LiteralPath $cache -Pattern '^Qt6_DIR:PATH=(.+)$' | Select-Object -First 1
            if ($line) {
                $qtDir = $line.Matches[0].Groups[1].Value.Trim() -replace '/', '\'
                $root = $qtDir -replace '\\lib\\cmake\\Qt6\\?$', ''
                return @{ Root = $root; Cache = $cache }
            }
        }
        $dir = Split-Path -Parent $dir
    }
    return $null
}

function Get-NetAllowedPatterns([string[]]$fromParam) {
    $list = New-Object System.Collections.Generic.List[string]
    foreach ($p in $fromParam) { if ($p) { $list.Add($p) } }
    if ($env:LGA_QA_NET_ALLOWED) { foreach ($p in ($env:LGA_QA_NET_ALLOWED -split ';')) { if ($p.Trim()) { $list.Add($p.Trim()) } } }
    $file = Join-Path $PSScriptRoot 'net_allowed_paths.txt'
    if (Test-Path -LiteralPath $file) {
        foreach ($l in (Get-Content -LiteralPath $file)) {
            $t = $l.Trim()
            if ($t -and -not $t.StartsWith('#')) { $list.Add($t) }
        }
    }
    return ,$list
}

function Test-NetAllowed([string]$exePath, $patterns) {
    foreach ($p in $patterns) {
        $x = [Environment]::ExpandEnvironmentVariables($p) -replace '/', '\'
        $x = $x -replace '\\\*\*$', ''
        $x = $x.TrimEnd('\')
        if ($exePath.StartsWith($x + '\', [StringComparison]::OrdinalIgnoreCase)) { return $p }
    }
    return $null
}

function Format-ExitCode([long]$code) {
    if ($code -lt 0) { return '-' }
    $hex = '{0:X8}' -f $code
    # Claves como texto: en PowerShell 5 un literal 0xC0000602 es un Int32 negativo.
    $known = @{
        'C0000602' = 'qFatal / fail-fast (tipicamente Qt sin plugin de plataforma)'
        'C0000135' = 'DLL no encontrada'
        'C0000139' = 'punto de entrada no encontrado (Qt o runtime equivocado)'
        'C0000142' = 'fallo al inicializar una DLL'
        'C000007B' = 'imagen invalida o de otra arquitectura'
        'C0000005' = 'violacion de acceso'
        'C0000409' = 'fail-fast / desborde de pila'
        '0000DEAD' = 'terminado por el runner'
    }
    if ($known.ContainsKey($hex)) { return "0x$hex ($($known[$hex]))" }
    return "0x$hex ($code)"
}

# ---------------------------------------------------------------------------------------------
# Preflight + corrida
# ---------------------------------------------------------------------------------------------

function Invoke-LgaHeadless {
    param(
        [string]$Exe, [string]$Arguments = '', [int]$TimeoutSec = 90, [string]$QtRoot = '',
        [string]$Platform = 'offscreen', [string]$OutDir = '', [string]$WorkDir = '',
        [string[]]$SetEnv = @(), [string[]]$NetAllowed = @(), [int]$KillWaitSec = 10,
        [switch]$PreflightOnly, [switch]$NoQt, [switch]$Quiet, [int]$KeepRuns = 20
    )
    $res = [ordered]@{
        exe = $Exe; arch = '-'; launched = $false; result = ''; runner_exit = 7; exit = -1; timeout = $false
        dialogs = @(); hidden_windows = 0; visible_job = @(); sys_new = @(); sys_check = 'si'; dialog_check = 'si'
        unkillable = @(); leftover = @(); job_pids = @(); net = ''; elapsed = 0.0; desktop = ''; out = ''
        problems = @(); notes = @(); qt = ''
    }
    $problems = New-Object System.Collections.Generic.List[string]
    $notes = New-Object System.Collections.Generic.List[string]

    if (-not [Environment]::Is64BitProcess) { $problems.Add('el runner tiene que correr en PowerShell de 64 bits') }

    # --- Carpeta de salida ---
    if (-not $OutDir) {
        Clear-LgaOldRuns $KeepRuns
        $OutDir = Join-Path $env:TEMP ('lga_headless\' + (Get-Date -Format 'yyyyMMdd_HHmmss') + '_' + ([guid]::NewGuid().ToString('N').Substring(0, 8)))
    }
    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
    $OutDir = (Resolve-Path -LiteralPath $OutDir).Path
    $res.out = $OutDir

    # --- Ejecutable: existe, es PE, x64 o i386, no DLL, no instalador de Inno ---
    $exePath = $null
    $machine = [uint16]0x8664
    if (-not $Exe) { $problems.Add('falta -Exe') }
    elseif (-not (Test-Path -LiteralPath $Exe -PathType Leaf)) { $problems.Add("no existe: $Exe") }
    else {
        $exePath = (Resolve-Path -LiteralPath $Exe).Path
        $res.exe = $exePath
        $magicErr = [LgaQa.Pe]::Magic($exePath)
        if ($magicErr) { $problems.Add("cabecera invalida: $magicErr. No se lanza (evita el cartel de Windows de aplicacion de 16 bits o imagen invalida).") }
        else {
            $peErr = $null
            $img = [LgaQa.Pe]::Read($exePath, $false, [ref]$peErr)
            if (-not $img) { $problems.Add("cabecera PE invalida: $peErr. No se lanza.") }
            elseif (-not (($img.Machine -eq 0x8664 -and $img.Magic -eq 0x20B) -or ($img.Machine -eq 0x14C -and $img.Magic -eq 0x10B))) {
                $problems.Add(('arquitectura no soportada: machine=0x{0:X} magic=0x{1:X} (se acepta x64 o i386)' -f $img.Machine, $img.Magic))
            }
            elseif ($img.IsDll -or -not $img.IsExecutable) { $problems.Add('no es un ejecutable (es una DLL o no tiene la marca de imagen ejecutable)') }
            else {
                $machine = [uint16]$img.Machine
                $res.arch = if ($machine -eq 0x14C) { 'i386' } else { 'x64' }
                $inst = Test-InstallerBinary $exePath
                if ($inst) { $problems.Add("instalador o desinstalador: no se ejecuta en corridas automatizadas ($inst). Toca la instalacion real, el registro y el escritorio del usuario.") }
            }
        }
    }
    if ($machine -eq 0x14C) {
        # Un exe de 32 bits no puede cargar el Qt de 64: sin entorno de Qt, para que ninguna DLL de
        # Qt de 64 bits quede primero en su busqueda.
        $res.arch = 'i386'
        if (-not $NoQt) { $notes.Add('exe de 32 bits: se corre sin entorno de Qt') }
        $NoQt = $true
    }

    $exeDir = if ($exePath) { Split-Path -Parent $exePath } else { '' }
    if (-not $WorkDir) { $WorkDir = $exeDir }

    # --- Argumentos: ningun instalador lanzado como hijo por la linea de comandos ---
    foreach ($a in (Get-ArgumentInstallers $Arguments $WorkDir)) {
        $problems.Add("instalador o desinstalador en -Arguments: no se ejecuta en corridas automatizadas: $a")
    }

    # --- Entorno del hijo ---
    $envMap = New-Object 'System.Collections.Generic.SortedDictionary[string,string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach ($e in [Environment]::GetEnvironmentVariables().GetEnumerator()) { $envMap[[string]$e.Key] = [string]$e.Value }
    # Una ruta de plugins heredada del que llama pisaria la eleccion del runner.
    if ($envMap.ContainsKey('QT_QPA_PLATFORM_PLUGIN_PATH')) { $envMap.Remove('QT_QPA_PLATFORM_PLUGIN_PATH') | Out-Null; $notes.Add('se quito QT_QPA_PLATFORM_PLUGIN_PATH heredado') }

    $qtRootSource = 'parametro'
    $cmakeQt = if ($exeDir) { Get-QtRootFromCMakeCache $exeDir } else { $null }
    if (-not $QtRoot) {
        if ($cmakeQt) { $QtRoot = $cmakeQt.Root; $qtRootSource = "CMakeCache ($($cmakeQt.Cache))" }
        else { $QtRoot = $script:DefaultQtRoot; $qtRootSource = 'por defecto' }
    } elseif ($cmakeQt -and ($cmakeQt.Root.TrimEnd('\') -ne $QtRoot.TrimEnd('\'))) {
        $problems.Add("-QtRoot $QtRoot no coincide con el Qt del build ($($cmakeQt.Root), segun $($cmakeQt.Cache)): cargaria otras DLL de Qt")
    }
    $QtRoot = $QtRoot.TrimEnd('\')
    $qtBin = Join-Path $QtRoot 'bin'
    if (-not $NoQt) {
        if (Test-Path -LiteralPath $qtBin) { $envMap['PATH'] = $qtBin + ';' + $envMap['PATH'] }
        else { $notes.Add("no existe ${qtBin}: no se agrega al PATH") }
        # Plugins del mismo Qt por defecto; tambien sirve para lo que lance el exe (ctest y sus
        # tests). Si el exe trae su Qt desplegada, mas abajo se cambia por su carpeta.
        $qtPlugins = Join-Path $QtRoot 'plugins'
        if (Test-Path -LiteralPath $qtPlugins) { $envMap['QT_PLUGIN_PATH'] = $qtPlugins }
        if ($Platform) { $envMap['QT_QPA_PLATFORM'] = $Platform }
        # Solo para poder leer el error de Qt en stderr. NO evita ningun cartel.
        $envMap['QT_FORCE_STDERR_LOGGING'] = '1'
    }
    foreach ($kv in $SetEnv) {
        $i = $kv.IndexOf('=')
        if ($i -lt 1) { $problems.Add("-SetEnv invalido: $kv (se espera NOMBRE=valor)") } else { $envMap[$kv.Substring(0, $i)] = $kv.Substring($i + 1) }
    }

    # --- Dependencias: cada DLL y cada funcion importada tiene que resolverse ---
    $resolver = $null
    if ($exePath -and $problems.Count -eq 0) {
        $resolver = New-Object LgaQa.Resolver($exeDir, $WorkDir, $envMap['PATH'], $machine)
        $rep = New-Object LgaQa.DepReport
        # Todo el grafo tiene que ser de la arquitectura del exe: una DLL de otra que el loader
        # encuentre primero da 0xC000007B (hard error de csrss).
        [LgaQa.Deps]::Check($exePath, $machine, $resolver, $rep)
        foreach ($p in $rep.Problems) { $problems.Add($p) }
        if ($machine -eq 0x14C -and $rep.Resolved.ContainsKey('Qt6Core.dll')) {
            $problems.Add('exe de 32 bits que importa Qt6Core.dll: no hay Qt de 32 bits de referencia en la maquina')
        }

        # --- Qt: de donde se cargaria Qt6Core.dll y que plugin de plataforma encontraria ---
        if ($rep.Resolved.ContainsKey('Qt6Core.dll') -and -not $NoQt) {
            $core = $rep.Resolved['Qt6Core.dll']
            $coreDir = Split-Path -Parent $core
            $res.qt = "Qt6Core=$core (QtRoot=$QtRoot, $qtRootSource)"
            $pluginsDir = $null
            if ($coreDir.TrimEnd('\') -ieq $qtBin.TrimEnd('\')) { $pluginsDir = Join-Path $QtRoot 'plugins' }
            elseif ($coreDir.TrimEnd('\') -ieq $exeDir.TrimEnd('\')) { $pluginsDir = $exeDir; $notes.Add('Qt desplegada junto al exe: se usan sus plugins') }
            else { $problems.Add("Qt6Core.dll se cargaria de $coreDir, que no es $qtBin ni la carpeta del exe: Qt mezclada") }
            if ($pluginsDir -and $Platform) {
                $plugin = Join-Path $pluginsDir ("platforms\q$Platform.dll")
                if (-not (Test-Path -LiteralPath $plugin)) { $problems.Add("falta el plugin de plataforma ${plugin}: Qt abriria su cartel fatal") }
                else {
                    $envMap['QT_PLUGIN_PATH'] = $pluginsDir
                    $prep = New-Object LgaQa.DepReport
                    [LgaQa.Deps]::Check($plugin, 0x8664, $resolver, $prep)
                    foreach ($p in $prep.Problems) { $problems.Add("plugin: $p") }
                }
            }
        } elseif (-not $NoQt) {
            $res.qt = 'el exe no importa Qt6Core.dll (no se exige plugin)'
        }
    }

    # --- Red: rutas aprobadas en el firewall por aplicacion ---
    if ($exePath) {
        $pat = Get-NetAllowedPatterns $NetAllowed
        $hit = Test-NetAllowed $exePath $pat
        if ($hit) { $res.net = 'aprobada' } else {
            $res.net = 'fuera-de-rutas-aprobadas'
            $notes.Add('AVISO: el exe esta fuera de las rutas aprobadas en el firewall por aplicacion. Si se conecta a internet queda retenido y no se puede matar: correrlo solo sin red.')
        }
    }

    $res.problems = @($problems)
    $res.notes = @($notes)
    if ($problems.Count -gt 0) {
        $res.result = 'rechazado'; $res.runner_exit = 2
        Write-LgaResult $res $OutDir $Quiet
        return $res
    }
    if ($PreflightOnly) {
        $res.result = 'preflight-ok'; $res.runner_exit = 0
        Write-LgaResult $res $OutDir $Quiet
        return $res
    }

    # --- Corrida ---
    $envList = New-Object System.Collections.Generic.List[string]
    foreach ($k in $envMap.Keys) { if ($k -and -not $k.StartsWith('=')) { $envList.Add("$k=$($envMap[$k])") } }
    $stdout = Join-Path $OutDir 'stdout.txt'
    $stderr = Join-Path $OutDir 'stderr.txt'
    $r = [LgaQa.Runner]::Run($exePath, $Arguments, $WorkDir, $envList.ToArray(), $stdout, $stderr, $TimeoutSec, $KillWaitSec)

    $res.launched = $r.Launched
    $res.desktop = $r.Desktop
    $res.exit = $r.ExitCode
    $res.timeout = ($r.Reason -eq 'timeout')
    $res.dialogs = @($r.Dialogs)
    $res.hidden_windows = $r.HiddenWindows
    $res.visible_job = @($r.VisibleJob)
    $res.sys_new = @($r.SysNew)
    $res.sys_check = if ($r.SysCheckAvailable) { 'si' } else { 'no-disponible' }
    $res.dialog_check = if ($r.DialogCheckAvailable) { 'si' } else { 'no-disponible' }
    $res.unkillable = @($r.Unkillable)
    $res.leftover = @($r.Leftover)
    $res.job_pids = @($r.JobPids)
    $res.elapsed = [math]::Round($r.ElapsedSec, 2)
    foreach ($l in $r.Log) { $notes.Add($l) }
    if ($r.Error) { $notes.Add("error: $($r.Error)") }
    $res.notes = @($notes)

    if ($r.Unkillable.Count -gt 0) { $res.result = 'unkillable'; $res.runner_exit = 5 }
    elseif ($r.VisibleJob.Count -gt 0 -or $r.SysNew.Count -gt 0) { $res.result = 'visible'; $res.runner_exit = 6 }
    elseif ($r.Reason -eq 'error') { $res.result = 'error'; $res.runner_exit = 7 }
    elseif ($r.Reason -eq 'dialog') { $res.result = 'dialogo'; $res.runner_exit = 3 }
    elseif ($r.Reason -eq 'timeout') { $res.result = 'timeout'; $res.runner_exit = 4 }
    elseif (-not $r.SysCheckAvailable -or -not $r.DialogCheckAvailable) { $res.result = 'sin-controles'; $res.runner_exit = 8 }
    elseif ($r.ExitCode -ne 0) { $res.result = 'exit-no-cero'; $res.runner_exit = 1 }
    else { $res.result = 'ok'; $res.runner_exit = 0 }

    Write-LgaResult $res $OutDir $Quiet
    return $res
}

function Write-LgaResult($res, [string]$outDir, [bool]$quiet) {
    try { ($res | ConvertTo-Json -Depth 4) | Out-File -LiteralPath (Join-Path $outDir 'result.json') -Encoding utf8 } catch { }
    if ($quiet) { return }
    foreach ($p in $res.problems) { Write-Output "preflight: $p" }
    foreach ($n in $res.notes) { Write-Output "nota: $n" }
    if ($res.qt) { Write-Output "qt: $($res.qt)" }
    foreach ($d in $res.dialogs) { Write-Output "dialogo: $d" }
    foreach ($v in $res.visible_job) { Write-Output "visible en el escritorio del usuario: $v" }
    foreach ($s in $res.sys_new) { Write-Output "ventana nueva de sistema: $s" }
    if ($res.launched) {
        foreach ($f in 'stdout.txt', 'stderr.txt') {
            $p = Join-Path $outDir $f
            if ((Test-Path -LiteralPath $p) -and (Get-Item -LiteralPath $p).Length -gt 0) {
                Write-Output "--- $f (ultimas 15 lineas) ---"
                Get-Content -LiteralPath $p -Tail 15 | ForEach-Object { Write-Output "  $_" }
            }
        }
    }
    Write-Output $script:HardErrorNote
    if (@($res.leftover).Count -gt 0) { Write-Output "nota: descendientes vivos al salir la raiz, terminados por el runner: $(@($res.leftover) -join ',')" }
    $unk = if (@($res.unkillable).Count -gt 0) { (@($res.unkillable) -join ',') } else { '-' }
    $timeoutTxt = if ($res.timeout) { 'si' } else { 'no' }
    $launchedTxt = if ($res.launched) { 'si' } else { 'no' }
    $sysChk = if ($res.launched) { $res.sys_check } else { '-' }
    $dlgChk = if ($res.launched) { $res.dialog_check } else { '-' }
    Write-Output ("headless-run result={0} launched={1} exit={2} timeout={3} dialogs={4} visible_job={5} sys_new={6} dialog_check={7} sys_check={8} unkillable={9} net={10} elapsed={11}s runner_exit={12} out={13}" -f `
        $res.result, $launchedTxt, (Format-ExitCode $res.exit), $timeoutTxt, @($res.dialogs).Count, @($res.visible_job).Count, @($res.sys_new).Count, $dlgChk, $sysChk, $unk, $res.net, $res.elapsed, $res.runner_exit, $res.out)
}

# ---------------------------------------------------------------------------------------------
# Self-test: casos que no pueden mostrar nada
# ---------------------------------------------------------------------------------------------

function Invoke-LgaSelfTest {
    Clear-LgaOldRuns $KeepRuns
    # Desde `powershell -File` un arreglo llega como un solo texto: se aceptan rutas separadas por ';'.
    $InnoSamples = @($InnoSamples | ForEach-Object { $_ -split ';' } | Where-Object { $_ })
    $root = Join-Path $env:TEMP ('lga_headless\selftest_' + (Get-Date -Format 'yyyyMMdd_HHmmss'))
    New-Item -ItemType Directory -Force -Path $root | Out-Null
    $ps = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $cmd = Join-Path $env:SystemRoot 'System32\cmd.exe'
    $script:stFails = 0
    $report = New-Object System.Collections.Generic.List[string]
    function Add-Case([string]$name, [bool]$ok, [string]$detail) {
        $line = ('[{0}] {1}: {2}' -f $(if ($ok) { 'OK' } else { 'FALLA' }), $name, $detail)
        $report.Add($line)
        Write-Output $line
        if (-not $ok) { $script:stFails++ }
    }

    # Script que escribe, sin crear ninguna ventana, en que escritorio corre su hilo y su PID.
    $whereScript = Join-Path $root 'donde.ps1'
    @'
param([string]$Out, [int]$SleepSec = 0)
Add-Type -Namespace LgaProbe -Name U -MemberDefinition '[DllImport("user32.dll")] public static extern System.IntPtr GetThreadDesktop(uint t); [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId(); [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern bool GetUserObjectInformation(System.IntPtr h, int i, System.Text.StringBuilder b, int l, out int n);'
$b = New-Object Text.StringBuilder 256; $n = 0
[void][LgaProbe.U]::GetUserObjectInformation([LgaProbe.U]::GetThreadDesktop([LgaProbe.U]::GetCurrentThreadId()), 2, $b, 512, [ref]$n)
Add-Type -Namespace LgaProbe -Name E -MemberDefinition '[DllImport("kernel32.dll")] public static extern uint GetErrorMode();'
"desk=$b pid=$PID errmode=0x{0:X} is64={1}" -f [LgaProbe.E]::GetErrorMode(), [Environment]::Is64BitProcess | Out-File -Encoding ascii -LiteralPath $Out
if ($SleepSec -gt 0) { Start-Sleep -Seconds $SleepSec }
'@ | Out-File -Encoding ascii -LiteralPath $whereScript

    $userDesk = [LgaQa.Runner]::CurrentDesktopName()

    # 1. Ubicacion: el hijo corre en el escritorio aparte, no en el del usuario.
    $f1 = Join-Path $root 'caso1_donde.txt'
    $r1 = @(Invoke-LgaHeadless -Exe $ps -Arguments "-NoProfile -ExecutionPolicy Bypass -File `"$whereScript`" -Out `"$f1`"" -TimeoutSec 60 -NoQt -Quiet -OutDir (Join-Path $root 'caso1'))[-1]
    $c1 = if (Test-Path -LiteralPath $f1) { (Get-Content -LiteralPath $f1 -Raw).Trim() } else { '' }
    $ok1 = ($r1.result -eq 'ok') -and ($c1 -like "desk=$($r1.desktop) *") -and ($r1.desktop -ne $userDesk) -and ($r1.desktop -like 'LGA_QA_*')
    Add-Case 'caso 1 ubicacion' $ok1 "hijo: '$c1'; escritorio del runner=$($r1.desktop); escritorio activo=$userDesk; result=$($r1.result)"
    if (-not $ok1) {
        Write-Output 'La prueba de ubicacion fallo: se aborta el self-test antes de cualquier otro caso.'
        return 1
    }

    # 1b. Detector de dialogos: ventana #32770 creada SIN mostrar, en un escritorio aparte.
    $d = [LgaQa.Runner]::DetectorProbe()
    Add-Case 'caso 1b detector de dialogos' ($d.StartsWith('OK')) $d

    # 2. Positivo: sale con 0, stdout capturado, nada visible.
    $r2 = @(Invoke-LgaHeadless -Exe $cmd -Arguments '/c echo lga-headless-ok' -TimeoutSec 30 -NoQt -Quiet -OutDir (Join-Path $root 'caso2'))[-1]
    $out2 = Get-Content -LiteralPath (Join-Path $r2.out 'stdout.txt') -Raw
    $ok2 = ($r2.result -eq 'ok') -and ($out2 -match 'lga-headless-ok') -and (@($r2.visible_job).Count -eq 0) -and (@($r2.sys_new).Count -eq 0)
    Add-Case 'caso 2 positivo' $ok2 "result=$($r2.result) exit=$($r2.exit) stdout='$($out2.Trim())' visible_job=$(@($r2.visible_job).Count) sys_new=$(@($r2.sys_new).Count)"

    # 3. Cabecera invalida: 20 bytes con extension .exe. Se rechaza SIN lanzarlo.
    $fake = Join-Path $root 'falso_20_bytes.exe'
    [IO.File]::WriteAllBytes($fake, [byte[]](1..20))
    $r3 = @(Invoke-LgaHeadless -Exe $fake -TimeoutSec 10 -Quiet -OutDir (Join-Path $root 'caso3'))[-1]
    $ok3 = ($r3.result -eq 'rechazado') -and (-not $r3.launched) -and ($r3.runner_exit -eq 2)
    Add-Case 'caso 3 cabecera PE invalida' $ok3 "result=$($r3.result) launched=$($r3.launched) motivo='$(@($r3.problems) -join '; ')'"

    # 4. Hijo que lanza un nieto: los dos en el escritorio aparte y en el Job, y los dos mueren.
    $f4 = Join-Path $root 'caso4_nieto.txt'
    $r4 = @(Invoke-LgaHeadless -Exe $cmd -Arguments "/c `"`"$ps`" -NoProfile -ExecutionPolicy Bypass -File `"$whereScript`" -Out `"$f4`" -SleepSec 120`"" -TimeoutSec 12 -NoQt -Quiet -OutDir (Join-Path $root 'caso4'))[-1]
    $c4 = if (Test-Path -LiteralPath $f4) { (Get-Content -LiteralPath $f4 -Raw).Trim() } else { '' }
    $nietoPid = 0
    if ($c4 -match 'pid=(\d+)') { $nietoPid = [int]$Matches[1] }
    $vivos = @()
    foreach ($p in @($r4.job_pids)) { if (Get-Process -Id $p -ErrorAction SilentlyContinue) { $vivos += $p } }
    $ok4 = ($r4.result -eq 'timeout') -and ($c4 -like "desk=$($r4.desktop) *") -and ($nietoPid -gt 0) -and (@($r4.job_pids) -contains [uint32]$nietoPid) -and ($vivos.Count -eq 0) -and (@($r4.unkillable).Count -eq 0)
    Add-Case 'caso 4 hijo y nieto' $ok4 "result=$($r4.result) nieto='$c4' job_pids=$(@($r4.job_pids) -join ',') vivos_despues=$($vivos -join ',')"

    # 5. Preflight contra carteles del sistema: copias rotas de un exe del sistema. NINGUNA se
    #    lanza (-PreflightOnly); cada una, lanzada, abriria un hard error que el escritorio aparte
    #    no tapa. El control positivo es la copia intacta. Se corre con el where.exe de 64 bits
    #    (System32) y con el de 32 bits (SysWOW64).
    function Set-Ascii([byte[]]$b, [string]$from, [string]$to) {
        # Reemplaza cada aparicion de from+NUL (mismo largo). Devuelve cuantas cambio.
        $f = [Text.Encoding]::ASCII.GetBytes($from + [char]0); $t = [Text.Encoding]::ASCII.GetBytes($to + [char]0); $n = 0
        for ($i = 0; $i -le $b.Length - $f.Length; $i++) {
            $ok = $true
            for ($j = 0; $j -lt $f.Length; $j++) { if ($b[$i + $j] -ne $f[$j]) { $ok = $false; break } }
            if ($ok) { [Array]::Copy($t, 0, $b, $i, $t.Length); $n++ }
        }
        return $n
    }
    function Get-Cut([byte[]]$b, [long]$n) { $x = New-Object byte[] $n; [Array]::Copy($b, $x, $n); return ,$x }
    function New-Case([string]$pfRoot, [string]$name, [byte[]]$content, [string]$file = 'muestra.exe') {
        $dir = Join-Path $pfRoot $name
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
        $f = Join-Path $dir $file
        [IO.File]::WriteAllBytes($f, $content)
        return $f
    }
    function Invoke-PreflightCases([string]$label, [string]$pfRoot, $cases) {
        # Cada caso: nombre, exe, resultado esperado y, opcionales, un texto que tiene que estar en
        # el motivo del rechazo (para que no pase por otro motivo) y los -Arguments.
        foreach ($cs in $cases) {
            $caseArgs = if ($cs.Count -gt 4) { [string]$cs[4] } else { '' }
            $rr = @(Invoke-LgaHeadless -Exe $cs[1] -Arguments $caseArgs -NoQt -PreflightOnly -Quiet -OutDir (Join-Path (Split-Path -Parent $cs[1]) 'pf'))[-1]
            $why = (@($rr.problems) | Select-Object -First 1)
            $whyOk = ($cs.Count -lt 4) -or (-not $cs[3]) -or ((@($rr.problems) -join ' ').Contains([string]$cs[3]))
            Add-Case "$label $($cs[0])" (($rr.result -eq $cs[2]) -and (-not $rr.launched) -and $whyOk) "result=$($rr.result) arch=$($rr.arch) (esperado $($cs[2])) $why"
        }
    }
    function Get-BrokenCases([string]$sysDir, [string]$pfRoot, [string]$otherArchVersionDll) {
        $sample = Join-Path $sysDir 'where.exe'
        $verDll = Join-Path $sysDir 'version.dll'
        New-Item -ItemType Directory -Force -Path $pfRoot | Out-Null
        $bytes = [IO.File]::ReadAllBytes($sample)
        $cases = New-Object System.Collections.Generic.List[object]
        $cases.Add(@('intacto (control positivo)', (New-Case $pfRoot 'intacto' $bytes), 'preflight-ok'))
        foreach ($frac in 0.5, 0.99) { $cases.Add(@("exe truncado al $([int]($frac*100)) %", (New-Case $pfRoot "exe_trunc_$frac" (Get-Cut $bytes ([long]($bytes.Length * $frac)))), 'rechazado')) }
        $cases.Add(@('exe truncado en 1 byte', (New-Case $pfRoot 'exe_trunc_1' (Get-Cut $bytes ($bytes.Length - 1))), 'rechazado'))
        $ne = New-Object byte[] 256; $ne[0] = 0x4D; $ne[1] = 0x5A; $ne[0x3C] = 0x80; $ne[0x80] = 0x4E; $ne[0x81] = 0x45
        $cases.Add(@('cabecera NE (16 bits)', (New-Case $pfRoot 'ne' $ne), 'rechazado'))
        $dos = New-Object byte[] 128; $dos[0] = 0x4D; $dos[1] = 0x5A
        $cases.Add(@('solo MZ (DOS)', (New-Case $pfRoot 'dos' $dos), 'rechazado'))
        $c = [byte[]]$bytes.Clone(); $n1 = Set-Ascii $c 'VERSION.dll' 'VERSIQN.dll'
        $cases.Add(@("DLL inexistente ($n1 parche)", (New-Case $pfRoot 'dll_falta' $c), 'rechazado'))
        $c = [byte[]]$bytes.Clone(); $n2 = Set-Ascii $c 'KERNEL32.dll' 'KERNELZZ.dll'
        $cases.Add(@("DLL del sistema inexistente ($n2 parche)", (New-Case $pfRoot 'sys_falta' $c), 'rechazado'))
        # Funcion inexistente en una DLL del sistema: la primera de KERNEL32 que where.exe importa.
        $peErr = $null
        $img = [LgaQa.Pe]::Read($sample, $true, [ref]$peErr)
        $fn = ($img.Imports | Where-Object { $_.Dll -ieq 'KERNEL32.dll' } | Select-Object -First 1).Names | Where-Object { $_.Length -gt 8 } | Select-Object -First 1
        $c = [byte[]]$bytes.Clone(); $n3 = Set-Ascii $c $fn ($fn.Substring(0, $fn.Length - 1) + 'Q')
        $cases.Add(@("funcion inexistente en KERNEL32 ($fn -> ...Q, $n3 parche)", (New-Case $pfRoot 'sysfunc_falta' $c), 'rechazado'))
        # DLL truncada junto al exe: version.dll no es KnownDLL, asi que se resuelve primero la de la
        # carpeta del exe (la copia recortada).
        $vb = [IO.File]::ReadAllBytes($verDll)
        foreach ($frac in 0.9, 0.99, 0.9999) {
            $f = New-Case $pfRoot "dll_trunc_$frac" $bytes
            [IO.File]::WriteAllBytes((Join-Path (Split-Path -Parent $f) 'version.dll'), (Get-Cut $vb ([long]($vb.Length * $frac))))
            $cases.Add(@("version.dll truncada al $($frac*100) % junto al exe", $f, 'rechazado'))
        }
        # Arquitectura cruzada: version.dll entera, pero de la otra arquitectura, junto al exe.
        if ($otherArchVersionDll) {
            $f = New-Case $pfRoot 'dll_otra_arq' $bytes
            Copy-Item -LiteralPath $otherArchVersionDll -Destination (Join-Path (Split-Path -Parent $f) 'version.dll')
            $cases.Add(@('version.dll de la otra arquitectura junto al exe', $f, 'rechazado'))
        }
        return ,$cases
    }
    $sys64 = Join-Path $env:SystemRoot 'System32'
    $sys32 = Join-Path $env:SystemRoot 'SysWOW64'
    Invoke-PreflightCases 'caso 5 preflight x64:' (Join-Path $root 'preflight') (Get-BrokenCases $sys64 (Join-Path $root 'preflight') $null)

    # 6. i386: ubicacion y modo de error heredado. Un powershell de 32 bits (SysWOW64) escribe su
    #    escritorio, su modo de error y si corre en 64 bits. Si falla, no se sigue con i386.
    $ps32 = Join-Path $sys32 'WindowsPowerShell\v1.0\powershell.exe'
    $cmd32 = Join-Path $sys32 'cmd.exe'
    $f6 = Join-Path $root 'caso6_donde32.txt'
    $r6 = @(Invoke-LgaHeadless -Exe $ps32 -Arguments "-NoProfile -ExecutionPolicy Bypass -File `"$whereScript`" -Out `"$f6`"" -TimeoutSec 60 -Quiet -OutDir (Join-Path $root 'caso6'))[-1]
    $c6 = if (Test-Path -LiteralPath $f6) { (Get-Content -LiteralPath $f6 -Raw).Trim() } else { '' }
    $em6 = 0
    if ($c6 -match 'errmode=0x([0-9A-Fa-f]+)') { $em6 = [Convert]::ToUInt32($Matches[1], 16) }
    $ok6 = ($r6.result -eq 'ok') -and ($r6.arch -eq 'i386') -and ($c6 -like "desk=$($r6.desktop) *") -and ($c6 -match 'is64=False') -and (($em6 -band 1) -eq 1)
    Add-Case 'caso 6 i386 ubicacion y modo de error' $ok6 "hijo: '$c6'; escritorio del runner=$($r6.desktop); arch=$($r6.arch); result=$($r6.result)"
    if ($ok6) {
        # 7. i386: hijo que lanza un nieto, los dos de 32 bits, en el Job y muertos al timeout.
        $f7 = Join-Path $root 'caso7_nieto32.txt'
        $r7 = @(Invoke-LgaHeadless -Exe $cmd32 -Arguments "/c `"`"$ps32`" -NoProfile -ExecutionPolicy Bypass -File `"$whereScript`" -Out `"$f7`" -SleepSec 120`"" -TimeoutSec 12 -Quiet -OutDir (Join-Path $root 'caso7'))[-1]
        $c7 = if (Test-Path -LiteralPath $f7) { (Get-Content -LiteralPath $f7 -Raw).Trim() } else { '' }
        $nieto7 = 0
        if ($c7 -match 'pid=(\d+)') { $nieto7 = [int]$Matches[1] }
        $vivos7 = @()
        foreach ($p in @($r7.job_pids)) { if (Get-Process -Id $p -ErrorAction SilentlyContinue) { $vivos7 += $p } }
        $ok7 = ($r7.result -eq 'timeout') -and ($c7 -like "desk=$($r7.desktop) *") -and ($c7 -match 'is64=False') -and ($nieto7 -gt 0) -and (@($r7.job_pids) -contains [uint32]$nieto7) -and ($vivos7.Count -eq 0) -and (@($r7.unkillable).Count -eq 0)
        Add-Case 'caso 7 i386 hijo y nieto' $ok7 "result=$($r7.result) nieto='$c7' job_pids=$(@($r7.job_pids) -join ',') vivos_despues=$($vivos7 -join ',')"

        # 8. i386: los mismos casos de preflight con el where.exe de 32 bits, mas una version.dll de
        #    64 bits junto al exe de 32. Y el cruce inverso con el where.exe de 64 bits.
        Invoke-PreflightCases 'caso 8 preflight i386:' (Join-Path $root 'preflight32') (Get-BrokenCases $sys32 (Join-Path $root 'preflight32') (Join-Path $sys64 'version.dll'))
        $fx = New-Case (Join-Path $root 'preflight') 'dll_otra_arq_inverso' ([IO.File]::ReadAllBytes((Join-Path $sys64 'where.exe')))
        Copy-Item -LiteralPath (Join-Path $sys32 'version.dll') -Destination (Join-Path (Split-Path -Parent $fx) 'version.dll')
        Invoke-PreflightCases 'caso 8 preflight x64:' $null @(,@('version.dll de 32 bits junto al exe de 64', $fx, 'rechazado'))
    } else {
        Write-Output 'La prueba de ubicacion de 32 bits fallo: no se corren los casos 7 y 8.'
    }

    # 9. Instaladores de Inno: se rechazan sin lanzarlos. Por nombre (una copia de where.exe
    #    llamada unins000.exe) siempre; y, si estan en la maquina, copias de un setup y de un
    #    desinstalador reales pasados con -InnoSamples (nunca los originales).
    $fu = New-Case (Join-Path $root 'inno') 'por_nombre' ([IO.File]::ReadAllBytes((Join-Path $sys32 'where.exe'))) 'unins000.exe'
    $innoCases = New-Object System.Collections.Generic.List[object]
    $innoCases.Add(@('unins000.exe por nombre', $fu, 'rechazado'))
    $k = 0
    foreach ($src in $InnoSamples) {
        if (-not $src -or -not (Test-Path -LiteralPath $src -PathType Leaf)) { continue }
        $k++
        $dir = Join-Path $root "inno\muestra_$k"
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
        # Copia renombrada: el rechazo tiene que venir de la marca de Inno, no del nombre.
        $dst = Join-Path $dir "copia_$k.exe"
        Copy-Item -LiteralPath $src -Destination $dst
        $innoCases.Add(@("copia de $([IO.Path]::GetFileName($src)) renombrada", $dst, 'rechazado'))
    }
    # Otros instaladores: todo unins*.exe (no solo unins000), msiexec por nombre, y NSIS por firma
    # (una copia de where.exe con la firma de NSIS agregada al final). Ninguno se lanza.
    $whereBytes = [IO.File]::ReadAllBytes((Join-Path $sys64 'where.exe'))
    $innoCases.Add(@('uninstall.exe por nombre (todo unins*.exe)', (New-Case (Join-Path $root 'inno') 'uninstall' $whereBytes 'uninstall.exe'), 'rechazado', 'desinstalador por nombre'))
    $innoCases.Add(@('msiexec.exe por nombre', (New-Case (Join-Path $root 'inno') 'msiexec' ([IO.File]::ReadAllBytes((Join-Path $sys64 'msiexec.exe'))) 'msiexec.exe'), 'rechazado', 'msiexec'))
    $nsisBytes = New-Object byte[] ($whereBytes.Length + 16)
    [Array]::Copy($whereBytes, $nsisBytes, $whereBytes.Length)
    $sigBytes = [Text.Encoding]::ASCII.GetBytes('NullsoftInst')
    [Array]::Copy($sigBytes, 0, $nsisBytes, $whereBytes.Length + 4, $sigBytes.Length)
    $fnsis = New-Case (Join-Path $root 'inno\con espacios') 'nsis' $nsisBytes 'mi setup.exe'
    $innoCases.Add(@('NSIS por firma', $fnsis, 'rechazado', 'NullsoftInst'))
    Invoke-PreflightCases 'caso 9 instaladores:' $null $innoCases

    # 10. ISCC por el runner: compila un .iss minimo a una carpeta de prueba (/O). Despues, el setup
    #     que genero tiene que rechazarse por la marca de Inno, y la carpeta -InnoWatchDir (por
    #     ejemplo installer\ de una app) no puede haber cambiado.
    $iscc = Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'
    if (Test-Path -LiteralPath $iscc) {
        $isDir = Join-Path $root 'iscc'
        $isOut = Join-Path $isDir 'salida'
        New-Item -ItemType Directory -Force -Path $isOut | Out-Null
        $iss = Join-Path $isDir 'minimo.iss'
        @(
            '[Setup]', 'AppName=LGA Runner Selftest', 'AppVersion=1.0', 'AppId=LGA_Runner_Selftest_QA',
            'DefaultDirName={localappdata}\LGA_Runner_Selftest_QA', 'PrivilegesRequired=lowest',
            'Uninstallable=no', 'CreateAppDir=no', 'OutputBaseFilename=selftest_setup', 'DisableProgramGroupPage=yes'
        ) | Out-File -Encoding ascii -LiteralPath $iss
        $before = @{}
        if ($InnoWatchDir -and (Test-Path -LiteralPath $InnoWatchDir)) {
            foreach ($f in Get-ChildItem -LiteralPath $InnoWatchDir -File -Recurse) { $before[$f.FullName] = (Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash }
        }
        $r10 = @(Invoke-LgaHeadless -Exe $iscc -Arguments "/Q `"/O$isOut`" `"$iss`"" -TimeoutSec 60 -Quiet -OutDir (Join-Path $isDir 'run'))[-1]
        $setup = Join-Path $isOut 'selftest_setup.exe'
        $after = @{}
        if ($InnoWatchDir -and (Test-Path -LiteralPath $InnoWatchDir)) {
            foreach ($f in Get-ChildItem -LiteralPath $InnoWatchDir -File -Recurse) { $after[$f.FullName] = (Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash }
        }
        $same = ($before.Count -eq $after.Count)
        foreach ($kv in $before.GetEnumerator()) { if ($after[$kv.Key] -ne $kv.Value) { $same = $false } }
        $watchTxt = if ($InnoWatchDir) { "$($before.Count) archivos de $InnoWatchDir, iguales=$same" } else { 'sin -InnoWatchDir' }
        $ok10 = ($r10.result -eq 'ok') -and ($r10.arch -eq 'i386') -and (Test-Path -LiteralPath $setup) -and $same
        Add-Case 'caso 10 ISCC por el runner con /O de prueba' $ok10 "result=$($r10.result) arch=$($r10.arch) setup=$(Test-Path -LiteralPath $setup) $watchTxt"
        if (Test-Path -LiteralPath $setup) {
            Invoke-PreflightCases 'caso 10 Inno:' $null @(,@('setup recien compilado (no se ejecuta)', $setup, 'rechazado'))
        }
    } else {
        Write-Output "caso 10 omitido: no esta $iscc"
    }

    # 11. Instaladores en -Arguments: un lanzador inocente (copia de where.exe) que recibiria un
    #     instalador para lanzarlo como hijo se rechaza. Control: la forma del driver de la
    #     seccion 8 de Doc_Instaladores_Inno.md (powershell con un .ps1) pasa.
    $lanz = New-Case (Join-Path $root 'args') 'lanzador' $whereBytes 'lanzador.exe'
    $argCases = New-Object System.Collections.Generic.List[object]
    $argCases.Add(@('unins000.exe en los argumentos', $lanz, 'rechazado', 'en -Arguments', "/c `"$fu`" /VERYSILENT"))
    $argCases.Add(@('setup NSIS con espacios en los argumentos', $lanz, 'rechazado', 'NullsoftInst', "/c `"$fnsis`" /S"))
    $argCases.Add(@('msiexec en los argumentos', $lanz, 'rechazado', 'msiexec', '/c msiexec /i paquete.msi /qn'))
    # Ruta entre comillas simples adentro de las dobles, como la escribe un powershell -Command.
    $argCases.Add(@('setup NSIS con espacios entre comillas simples (powershell -Command)', $lanz, 'rechazado', 'NullsoftInst', "-NoProfile -Command `"& '$fnsis' /S`""))
    $argCases.Add(@('unins000.exe entre comillas simples (powershell -Command)', $lanz, 'rechazado', 'en -Arguments', "-NoProfile -Command `"& '$fu' /VERYSILENT`""))
    # Ruta SIN comillas con un apostrofe adentro: no se puede partir en el apostrofe.
    $apDir = Join-Path $root "args\O'Brien"
    New-Item -ItemType Directory -Force -Path $apDir | Out-Null
    $apNsis = Join-Path $apDir 'nsis_tool.exe'; Copy-Item -LiteralPath $fnsis -Destination $apNsis -Force
    $argCases.Add(@('setup NSIS en una ruta con apostrofe, sin comillas', $lanz, 'rechazado', 'NullsoftInst', "/c $apNsis /S"))
    if ((Test-Path variable:setup) -and $setup -and (Test-Path -LiteralPath $setup)) {
        $argCases.Add(@('setup de Inno del caso 10 entre comillas simples (powershell -Command)', $lanz, 'rechazado', 'Inno', "-NoProfile -Command `"& '$setup' /VERYSILENT`""))
        $apInno = Join-Path $apDir 'tool.exe'; Copy-Item -LiteralPath $setup -Destination $apInno -Force
        $argCases.Add(@('setup de Inno del caso 10 en una ruta con apostrofe, sin comillas', $lanz, 'rechazado', 'Inno', "/c $apInno /S"))
    }
    $argCases.Add(@('driver .ps1 (control positivo)', $lanz, 'preflight-ok', '', "-NoProfile -ExecutionPolicy Bypass -File `"$whereScript`" -Out `"$root\no_se_escribe.txt`" -Tool `"$ps`""))
    Invoke-PreflightCases 'caso 11 argumentos:' $null $argCases

    # 12. Identidad de los procesos del grupo: PID + hora de creacion. Sin procesos ni ventanas.
    $pr = [LgaQa.Runner]::PidReuseProbe()
    Add-Case 'caso 12 PID reciclado no cuenta como del grupo' ($pr.StartsWith('OK')) $pr

    Write-Output ''
    Write-Output "self-test: $($report.Count) casos, $($script:stFails) fallas. Carpeta: $root"
    Write-Output 'No incluidos a proposito: el negativo de plugin de Qt y el hard error por DLL inexistente. Se corren a mano, una sola vez y con el usuario mirando.'
    if ($script:stFails -gt 0) { return 1 } else { return 0 }
}

# ---------------------------------------------------------------------------------------------
# Entrada
# ---------------------------------------------------------------------------------------------

try {
    if ($ArgumentsFile) {
        if ($Arguments) { throw 'usar -Arguments o -ArgumentsFile, no los dos' }
        $Arguments = @(Get-Content -LiteralPath $ArgumentsFile -TotalCount 1)[0]
        if ($null -eq $Arguments) { $Arguments = '' }
    }
    if ($SelfTest) {
        $code = Invoke-LgaSelfTest
        $last = @($code)[-1]
        @($code) | Select-Object -SkipLast 1 | ForEach-Object { $_ }
        exit [int]$last
    }
    $r = Invoke-LgaHeadless -Exe $Exe -Arguments $Arguments -TimeoutSec $TimeoutSec -QtRoot $QtRoot -Platform $Platform `
        -OutDir $OutDir -WorkDir $WorkDir -SetEnv $SetEnv -NetAllowed $NetAllowed -KillWaitSec $KillWaitSec -PreflightOnly:$PreflightOnly -NoQt:$NoQt -KeepRuns $KeepRuns
    $last = @($r)[-1]
    @($r) | Select-Object -SkipLast 1 | ForEach-Object { $_ }
    exit [int]$last.runner_exit
} catch {
    Write-Output "headless-run result=error runner_exit=7 detalle=$($_.Exception.Message)"
    exit 7
}
