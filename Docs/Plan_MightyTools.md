# LGA Mighty Tools: plan

Plan de arranque de LGA Mighty Tools, escrito el 2026-09-25 antes de crear el repo. Es la
especificación de partida: las fases, la arquitectura y las decisiones abiertas de acá mandan hasta que
el diseño aprobado y `Docs/Doc_Decisiones.md` las reemplacen. Lo que se cierra se pasa a esos documentos
y se borra de acá.

## 1. Qué es

Una sola app de bandeja para Windows y macOS que reúne varias herramientas chicas de LGA, al estilo de
Microsoft PowerToys: una ventana donde cada herramienta aparece con su interruptor de encendido, su estado
y su panel de configuración. Una herramienta apagada no corre nada (ni atajos, ni hooks, ni timers).

Reemplaza a cuatro apps sueltas. Solo Open in NukeX tiene usuarios (pocos y conocidos): desinstalan la
vieja e instalan esta (sección 9). Nuke Shortcuts (v2.03) y Folder Switch (v0.10) tienen un release
publicado en GitHub y en el manifiesto de updates, pero nadie las instaló. Link Redirector nunca se
publicó y su repo es privado (D-11).

| Módulo (nombre en la UI) | Qué hace | Plataformas | Origen |
|---|---|---|---|
| Nuke Shortcuts | Add keyframe y Frame Dope Sheet con atajos, solo con Nuke al frente; calibrador del Dope Sheet | Windows, macOS | `C:\Portable\LGA_NukeShortcuts` (v2.06) |
| Disk Space | Vigila discos locales y avisa cuando uno baja de su umbral (GB o %) | Windows, macOS | `C:\Portable\LGA_NukeShortcuts` (v2.06) |
| Open in NukeX | Doble click en un `.nk`: lo abre en el NukeX que ya está abierto, o lanza NukeX; instala el Nuke Bridge | Windows, macOS | `~/.nuke/LGA_OpenInNukeX/QtClient` (v1.83) |
| Folder Switch | Lleva la carpeta del Explorador o de XYplorer al diálogo Abrir/Guardar; menú de carpetas recientes | Windows | `C:\Portable\LGA_FolderSwitch` (v0.10) |
| Link Redirector | Se registra como navegador y abre cada link en el navegador que corresponde según palabras clave | Windows, macOS | `C:\Portable\LGA_LinkRedirector` (v0.173) |

Nombres de producto: visible **LGA Mighty Tools**; archivos sin espacios (`LGA_MightyTools.exe`,
`LGA_MightyTools_Setup_v<versión>.exe`); repo `legandrop/LGA_MightyTools` (público); bundle de mac
`LGA Mighty Tools.app` con id `com.lga.mightytools`. El nombre no dispara la detección de Nuke por nombre
de ejecutable (el patrón exige que empiece con `nuke` seguido de un número).

## 2. Punto de partida de cada origen

Hechos relevados en los repos el 2026-09-24 y 25, con lo que cada uno aporta y lo que hay que resolver.

**Nuke Shortcuts** (Qt 6.5 / C++17, ~7.300 líneas). Es la base: su arquitectura se copia entera.
- Capa de plataforma con interfaces y una implementación por sistema (`src/platform/{win,mac}`):
  `HotkeyService`, `NukeWatcher`, `InputInjector`, `SystemInput`, `AutoStart`, `LocalDrives`.
- `AppState` como única fuente de verdad, `Theme` por tokens, `UiWidgets`, `TitleBar`, `HelpDialog`,
  `TrayController`/`TrayMenu`, `UpdateService` (manifiesto de LGA_Updates, SHA-256 obligatorio),
  `qa/UiShot` (capturas sin escritorio), `--self-test`, `--simulate-action`, `--ui-probe`.
- Todas las piezas de UI están más avanzadas acá que en FolderSwitch, de donde salieron.
- Hay un release v2.03 en GitHub y una entrada en LGA_Updates y en el sitio, pero nadie la instaló.

**Disk Space** vive hoy dentro de Nuke Shortcuts (`core/DiskSpace`, `core/DiskMonitor`,
`platform/LocalDrives`, `ui/DiskCard`) y ya está separado como pieza propia: pasa a ser un módulo sin
cambios de lógica.

**Open in NukeX** (cliente Qt 6 / C++ de ~6.100 líneas más un plugin Python dentro de Nuke).
- El cliente se lanza por doble click en un `.nk`, manda el path por TCP a NukeX (`localhost:54325`) o
  lanza NukeX con `--nukex`, y sale. Sin argumento abre su ventana de configuración: asociación `.nk`,
  versión de Nuke preferida e instalación del Nuke Bridge. No es residente.
- El plugin (`init.py`) corre dentro de NukeX y escucha el puerto. También lo usa el Review Panel de
  Hiero (`paste_clipboard`). Vive en `~/.nuke/LGA_OpenInNukeX`, que es a la vez el clon del repo: ese
  nombre de carpeta es un contrato y no se cambia. **El plugin se muda a Mighty Tools** (D-04): viaja
  embebido en el exe, con su propia versión, y el repo `LGA_OpenInNukeX` se archiva.
- Sin updater propio: lo actualiza PipeSync (`C:\Portable\LGA_PipeSync_2`, `UpdateCatalog.cpp`), que baja
  solo el plugin; el instalador del cliente lo corre únicamente el botón manual «Install App».
- Windows: instalador Inno con permisos de administrador en `Program Files\LGA\OpenInNukeX`, AppId
  `{B8F1A2C3-4D5E-6F78-9A0B-1C2D3E4F5678}`. Asociación `.nk` en HKCU con el ProgID `LGA.NukeScript.1`,
  Capabilities en `Software\OpenInNukeX`. En Windows 11 usa un helper .NET 9 (`LGA_WinSetFTA.exe`) para el
  hash de UserChoice. La ruta de NukeX vive en `%APPDATA%\LGA\OpenInNukeX\nukeXpath.txt` (mac:
  `~/Library/Application Support/LGA/OpenInNukeX/nukeXpath.txt`), primera línea. **Es un contrato con
  PipeSync**, que la muestra en sus settings y la usa para abrir la última versión en NukeX
  (`UserAccountPanel.cpp`, `py_scr/open_nuke_latest_version.py`).
- La versión del plugin hoy sale del CMake del cliente, y el zip del release de Windows lleva adentro el
  instalador del cliente (`LGA_OpenInNukeX_Setup.exe`), que PipeSync ofrece con «Install App».
- macOS: `.app` + DMG sin instalador, bundle id `com.lga.openinnukex`.
- `NukeOpener` llama a `QApplication::quit()` en todas sus salidas: en una app residente eso la cerraría.
- UI en castellano e inglés (`i18n.cpp`).

**Folder Switch** (Qt 6.5 / C++17, ~5.600 líneas, solo Windows, sin capa de plataforma).
- `RegisterHotKey` con ids fijos 1 y 2 (`Ctrl+Alt+O`, `Ctrl+Alt+Shift+O`) y su propio filtro nativo.
- `ForegroundWatcher` con `SetWinEventHook(EVENT_SYSTEM_FOREGROUND)` y un puntero estático `s_instance`:
  rompe si se instancia dos veces. Nuke Shortcuts tiene otro hook igual dentro de `NukeWatcherWin`.
- COM en modo apartment (`CoInitializeEx(COINIT_APARTMENTTHREADED)`) para Shell COM y UI Automation.
- Popup propio de carpetas recientes (`RecentFoldersPopup`), migración de settings desde el registro.

**Link Redirector** (Qt 6.5 / C++17, ~4.750 líneas, Windows y macOS; repo privado).
- Se registra como navegador y para abrir `.htm`/`.html`: ProgID `LinkRedirectorURL`, Capabilities,
  `RegisteredApplications`, `StartMenuInternet` en HKCU (Windows); `CFBundleURLTypes` http/https y `LSRegisterURL` (mac). Ninguna
  plataforma deja forzarse como navegador por defecto: el usuario confirma en el sistema.
- Windows: cada link lanza `LinkRedirector.exe <url>`, rutea y sale, sin tocar la instancia de bandeja.
  macOS: proceso residente que recibe la URL como `QFileOpenEvent`.
- Reglas: palabras clave (substring sin mayúsculas) que mandan al navegador alternativo; el resto va al
  navegador por defecto configurado.
- Hoy loguea cada URL completa (`UrlRouter.cpp:55`).
- Estética propia («Liquid Glass» pintado a mano en `mainwindow.cpp`, 1.678 líneas): se rehace entera con
  el Theme de Mighty Tools. Sin updater.

## 3. Reglas del producto

1. **Repo nuevo y público.** Todo se copia adentro desde los repos de origen; ningún script ni build
   depende de otro repo. Los repos de origen solo se leen.
2. **Autocontenida.** Nada depende de instalaciones globales salvo Qt para compilar e Inno Setup para
   armar el instalador. Hasta que Lega decida D-03, no se agrega ninguna dependencia de .NET.
3. **Un proceso, un ícono de bandeja, un updater, una entrada de inicio con el sistema, un
   `settings.ini`, un instalador.** Nada de un proceso por módulo.
4. **Estética de Nuke Shortcuts**: tokens de `Theme`, tarjetas, keycaps, chips, barra de título propia,
   Inter embebida, Fusion. Las demás estéticas (Liquid Glass de Link Redirector, la ventana de Open in
   NukeX) se reemplazan.
5. **UI en inglés**; comentarios de código, logs de debug, docs y changelog en castellano.
6. **Nada toma foco de teclado** salvo los campos donde se escribe (grabador de atajos, umbral de disco,
   palabras clave de Link Redirector), que lo toman solo por click y lo sueltan con Enter, Escape o un
   click afuera.
7. **Deriva de `LGA_Base_QT_C_Py`**: sus docs valen como referencia (instaladores, autostart, registro LGA,
   checklist de app nueva).
8. **Lo apagado no consume nada** (pedido de Lega, regla dura). Detalle y medición en 4.3.

## 4. Arquitectura

### 4.1 Estructura de carpetas

```
src/
  main.cpp              arranque, modos cortos (.nk, URL), instancia única, arneses de QA
  app/                  TrayController, ModuleHost, MainWindow (la ventana), Onboarding si hace falta
  core/                 AppSettings, AppState general, LgaRegistry, BuildTree, DebugFlags, AppPaths
  platform/             interfaces compartidas + win/ y mac/
  modules/
    nukeshortcuts/      lógica, ActionRunner, calibrador, panel
    diskspace/          DiskSpace, DiskMonitor, panel
    openinnukex/        NukeOpener, asociación .nk, Nuke Bridge, panel
    folderswitch/       DialogSwitcher, UiaSwitcher, FolderResolver, WindowUtils, popup de recientes, panel
    linkredirector/     UrlRouter, registro de navegador, panel
  ui/                   Theme, UiWidgets, TitleBar, HelpDialog, piezas comunes de paneles
  updates/              UpdateService, UpdateDialog, VersionCompare
  qa/                   UiShot, UiProbe
```

### 4.2 Contrato de un módulo

Cada módulo implementa una interfaz `Module` y se registra en `ModuleHost`:

- `id()` estable (`nukeShortcuts`, `diskSpace`, `openInNukeX`, `folderSwitch`, `linkRedirector`): es la
  clave de su sección en `settings.ini` y no se cambia nunca.
- `title()`, `description()`, `icon()` (vectorial con `QPainter`), `platforms()`.
- `start()` y `stop()`, idempotentes. `stop()` suelta todo lo que el módulo tomó: atajos, hooks, timers,
  sockets. `ModuleHost` los llama según el interruptor.
- `status()`: tono (apagado, activo, atención, error) y un texto corto para el chip de su fila y para el
  menú de la bandeja.
- `createPanel()`: el panel de configuración que muestra la ventana.
- `trayActions()`: entradas propias para el menú de la bandeja (por ejemplo «Pause shortcuts» o las
  líneas de disco bajo).
- `handleExternal()` opcional: recibe un `.nk` o una URL (sección 4.5).

Un módulo no toca a otro. Lo que comparten pasa por servicios del host.

### 4.3 Lo apagado no consume nada

Un módulo apagado no existe en memoria más allá de su descriptor estático (id, título, descripción,
ícono, plataformas), que es lo único que necesita la lista para dibujar su fila y su interruptor.

- `ModuleHost` construye el objeto del módulo al prenderlo y lo destruye al apagarlo. Con él se crean y se
  destruyen sus hooks, atajos, timers, sockets, objetos COM, cachés y su panel de configuración.
- El panel de un módulo se construye recién cuando el usuario lo abre, y se destruye al apagar el módulo.
  Un módulo apagado muestra solo su descripción y el interruptor, dibujados por el host.
- Los servicios compartidos también son perezosos: el hook de ventana al frente se instala solo si algún
  módulo prendido lo pidió y se saca cuando ninguno lo usa; `HotkeyService` no registra nada sin módulos;
  COM se inicializa en `main` porque es barato y obligatorio antes de cualquier módulo que lo use.
- Nada corre en reposo: sin timers de sondeo con todo apagado. Los chequeos periódicos (Disk Space, permiso
  de Accesibilidad en mac) viven dentro de su módulo.
- **Open in NukeX y Link Redirector** no tienen nada residente en Windows: el sistema lanza la app con un
  `.nk` o una URL por sus registros. Apagados, el host no los construye ni reescribe sus registros. Si el
  sistema igual lanza la app, el paso directo lo hace una función estática del host, sin construir el
  módulo: la URL va al navegador configurado (nunca al propio exe: `LGA.MightyTools.URL` se excluye al
  detectar navegadores, para no entrar en un bucle) y el `.nk` va a NukeX con la ruta de
  `nukeXpath.txt`. Los ProgIDs se reescriben solo con su módulo prendido. Soltar los registros al apagar
  es D-09.
- El código de los módulos va compilado en el mismo exe. Windows y macOS cargan las páginas del ejecutable
  a demanda, así que el código de un módulo que nunca se construye no ocupa memoria residente. Separar
  cada módulo en una DLL que se carga y descarga suma complejidad de despliegue y de descarga segura sin un
  ahorro medible: no se hace salvo que la medición diga lo contrario.

Medición (criterio de aceptación desde la fase 2, cada número anotado en el changelog de la fase):
1. Self-test: después de `stop()` el puntero al módulo queda nulo; la cantidad de `QTimer` y `QThread`
   descendientes de `qApp` vuelve a la inicial; atajos y hooks registrados en el host, en 0.
2. Veinte ciclos de prender y apagar por módulo: handles, hilos y objetos GDI/USER del proceso vuelven a
   ±2 de los iniciales.
3. Arranque con todo apagado y la ventana cerrada, 10 minutos en reposo contados después del chequeo de
   updates: el tiempo de CPU del proceso sube menos de 50 ms.
4. Memoria privada con todo apagado: a lo sumo 3 MB más que el esqueleto de la fase 2, medida con la misma
   herramienta. No se compara después de prender y apagar: el heap de Windows no devuelve toda la memoria
   y daría falsos rechazos; para eso está el punto 2.

### 4.4 Servicios compartidos (uno por proceso)

- **HotkeyService**: un solo dueño de `RegisterHotKey` (Windows) y `RegisterEventHotKey` (mac). Asigna ids
  por módulo, detecta choques entre módulos y con otras apps, y el panel muestra «In use by <módulo>».
  Reemplaza al `HotkeyFilter` de Folder Switch.
- **ForegroundWatcher**: un solo hook de ventana al frente (Windows) u observador de app activa (mac), con
  señal. Lo usan Nuke Shortcuts (Nuke al frente) y Folder Switch (diálogos y managers). Sin punteros
  estáticos por instancia.
- **COM**: `CoInitializeEx` en modo apartment una sola vez, en `main`, antes de crear módulos.
- **InputInjector** con modo solo loguear, **Notifier** (notificaciones del sistema; el click abre la
  ventana en el panel del módulo que avisó), **LgaRegistry**, **AppSettings**.

### 4.5 Entradas desde el sistema (.nk y links)

- **Windows**: el sistema lanza `LGA_MightyTools.exe <archivo.nk>`, `<url>` o `<archivo.htm/.html>`.
  `main` resuelve esos modos ANTES de la instancia única: lee la sección del módulo en `settings.ini` (y
  `nukeXpath.txt`), hace su trabajo y sale, sin ventana principal ni bandeja y sin tocar la copia
  residente. Sus avisos propios (el de NukeX abriéndose, los errores) sí se muestran. Es el modelo que ya usan Open in NukeX y Link
  Redirector y el que mantiene baja la latencia.
- **Módulo apagado con una entrada del sistema**: nunca se pierde lo que el usuario abrió (paso directo de
  4.3). Un link se abre en el navegador por defecto configurado en el módulo (y si no hay, en el primer
  navegador detectado que no sea esta app); un `.nk` se abre lanzando NukeX. Detalle en D-05.
- **Privacidad**: el modo corto de links loguea solo el host; la URL completa, solo con un flag de debug.
- **Segunda copia sin argumentos** (el usuario abre la app desde el menú Inicio con la residente
  corriendo): le pide a la residente que muestre la ventana por un `QLocalSocket` y sale. Hoy las cuatro
  apps salen mudas en ese caso.
- **macOS**: los `.nk` y los links llegan como `QFileOpenEvent` a la app residente, que los despacha al
  módulo sin cerrarse (sacar los `quit()` de `NukeOpener`). Si la app no estaba corriendo, el sistema la
  arranca: el primer arranque, el pedido de Accesibilidad y el onboarding no pueden saltar en medio de
  abrir un link o un `.nk`.

### 4.6 Registros en el sistema

- **Asociación `.nk`** (Windows): se reutiliza el ProgID `LGA.NukeScript.1`, así la elección que ya hizo un
  usuario de Open in NukeX sigue valiendo (el hash de UserChoice no incluye el comando). La copia instalada
  reescribe `LGA.NukeScript.1\shell\open\command` con su propio exe, y el estado «asociado» se verifica con
  el ProgID MÁS la ruta del comando igual a su exe. Capabilities y `RegisteredApplications` con el nombre
  nuevo.
- **Navegador** (Windows): ProgID nuevo `LGA.MightyTools.URL` para http, https, `.htm` y `.html`,
  Capabilities de «LGA Mighty Tools», `StartMenuInternet\LGA_MightyTools`. El panel explica que Windows
  exige confirmar el navegador por defecto en su propia pantalla.
- **Open in NukeX viejo todavía instalado** (clave de desinstalación
  `{B8F1A2C3-4D5E-6F78-9A0B-1C2D3E4F5678}_is1`): el panel lo detecta y avisa, porque el Re-apply del viejo
  y el del nuevo se pisarían el comando del ProgID.
- **Re-apply** hoy corre `cleanConflictingKeys`, que borra UserChoice y UserChoiceLatest: en el módulo
  nuevo, Re-apply es solo el camino para quien no aparece como «Associated», nunca el primer paso.
- **mac**: un solo `Info.plist` declara el tipo de documento `.nk` y las URL http/https.
- **Desinstalar limpia todo lo que la app escribió**: ProgIDs, Capabilities, `RegisteredApplications`,
  `StartMenuInternet`, la entrada `Run` (solo si apunta a esta instalación), settings y registro LGA. Open
  in NukeX no lo hacía.

### 4.7 Settings

- `%APPDATA%\LGA\LGA_MightyTools\settings.ini` (Windows) y
  `~/Library/Application Support/LGA/LGA_MightyTools/settings.ini` (mac).
- Sección `[modules]` con `<id>/enabled`; una sección por módulo con las claves que ya tenía cada app.
- **La ruta de NukeX no se migra: sigue viviendo en `nukeXpath.txt`** (sección 2), porque PipeSync la lee
  ahí. El módulo Open in NukeX la lee y la escribe en ese archivo; no se copia a `settings.ini`. Si no
  existe, se autodetecta Nuke con el escáner y se escribe ahí, antes de tomar la asociación `.nk`.
- La carpeta `.nuke` del bridge sigue publicándose en el registro compartido LGA (`nuke.json`), que leen
  otras apps.
- Las otras apps no tienen usuarios: no se migra nada de ellas.

### 4.8 Versión, updater e instalador

- Versión continua de a centésimos, con `sync_version` como Nuke Shortcuts (D-02).
- `UpdateService` de Nuke Shortcuts con el slug `legandrop/LGA_MightyTools`; alta en
  `LGA_Updates/repos.json` y en el catálogo del sitio.
- Inno Setup sin permisos de administrador (`PrivilegesRequired=lowest`), cierre por ruta con
  `tools/close_by_path.ps1`, AppId nuevo, carpeta `C:\Portable\LGA\MightyTools` (regla de la Base,
  `Doc_Rutas_Instalacion.md`). Migra a los usuarios de Open in NukeX y desinstala el cliente viejo solo
  con el sí del usuario (sección 9).
- mac: DMG como LGA_VideoDownloader, firma ad-hoc antes de empaquetar, `.zip` con `ditto`.

## 5. Diseño (fase 1)

El diseño se aprueba antes de escribir la app, en un tablero de diseño con una pantalla por sección. Lo aprobado es
la especificación visual; cada pantalla implementada se compara contra su tablero.

Lo que el diseño tiene que resolver:

1. **La ventana.** Opciones a mostrar lado a lado:
   - **A. Barra lateral**: lista de herramientas a la izquierda (ícono, nombre, interruptor, punto de
     estado) y el panel de la elegida a la derecha, como PowerToys. Escala a más módulos.
   - **B. Tablero de tarjetas**: una grilla con una tarjeta por herramienta (interruptor, estado, resumen);
     un click abre su panel con «volver».
   - **C. Una columna**: todas las tarjetas en una sola columna que crece, como Nuke Shortcuts hoy.
   - **D. Secciones que se expanden**: una columna con una fila por herramienta; al prender su
     interruptor la sección se despliega con sus opciones, y apagada queda en una línea.
   Recomendación de partida: A, con una página «General» arriba de la lista (inicio con el sistema,
   updates, versión, ayuda). La ventana deja de tener ancho fijo de 440 px.
2. **Todas las opciones de todas las apps, sin excepción.** El diseño se arma contra un inventario de cada
   control, estado, menú, diálogo y aviso de las cuatro apps de origen (`Docs/Inventario_Opciones.md`), y
   se revisa contra ese inventario antes de presentarlo. Lo que se descarta a propósito queda anotado ahí
   con su motivo.
3. **El panel de cada módulo**, con sus estados: los de Nuke Shortcuts y Disk Space ya diseñados, más
   Open in NukeX (asociación, versión de Nuke, Nuke Bridge), Folder Switch (atajos, última carpeta,
   recientes) y Link Redirector (navegadores, palabras clave, estado de navegador por defecto).
4. **Estados de un módulo** en la lista y en su panel: apagado, activo, necesita atención (sin calibrar,
   sin permiso de Accesibilidad, disco bajo, no es el navegador por defecto, `.nk` sin asociar), error
   (atajo tomado).
5. **Menú de la bandeja**: encabezado con el estado general, entradas por módulo activo, Settings...,
   Check for Updates..., Quit. En mac, nativo.
6. **Primer arranque**: qué ve alguien que instala la app por primera vez y cómo prende cada herramienta.
7. **Ícono de la app y de la bandeja**, con el sistema de íconos de LGA (desregistro CMY).

## 6. Fases

Cada fase termina con la app compilando, el self-test en verde, las capturas de sus estados y la prueba de
Lega en su máquina. Recién ahí se commitea.

| Fase | Qué | Criterio de aceptación |
|---|---|---|
| 0 | Repo: estructura, reglas locales fuera del control de versiones, `.gitignore`, `.gitattributes` (LF salvo scripts de Windows), licencia MIT con aviso LGPL de Qt, README en inglés, `CMakeLists.txt`, `VERSION`, `Docs/Changelog.md` (v0.01), `Docs/Doc_Roadmap.md`, `Docs/Doc_Decisiones.md` (con las decisiones de la sección 8), `compilar.bat`, `sync_version`, `run_headless.ps1`, `close_by_path.ps1` | Compila un exe vacío; `git ls-files` sin archivos de reglas |
| 1 | Diseño (sección 5) | Lega lo aprueba |
| 2 | Esqueleto: `ModuleHost`, servicios compartidos, ventana, bandeja, settings, updater, autostart, `UiShot`, self-test; cero módulos | La app arranca, muestra la ventana aprobada vacía y sale limpia; línea base de 4.3 medida y anotada |
| 3 | Módulos Nuke Shortcuts y Disk Space, portados de Nuke Shortcuts | Todo lo que hace Nuke Shortcuts v2.06, con el interruptor apagando y prendiendo cada uno |
| 4 | Folder Switch sobre los servicios compartidos (hotkeys, foreground, COM) | Los dos atajos y el cambio automático en Explorer, XYplorer, diálogos Win32 y diálogos Qt |
| 5 | Link Redirector con el modo corto de Windows y el panel rehecho | Un link abre el navegador correcto; con el módulo apagado igual se abre; del inicio del proceso al lanzamiento del navegador tarda como Link Redirector más 50 ms a lo sumo, medido en el log |
| 6 | Open in NukeX con las condiciones de la sección 9 | Doble click en `.nk` con NukeX abierto y cerrado; el Nuke Bridge se instala; PipeSync muestra la ruta de NukeX y abre NukeX |
| 7 | Instalador, updater, LGA_Updates, sitio, PipeSync; release 1.00 de Windows | Lega instala, actualiza y desinstala en una máquina limpia sin dejar basura (las pruebas automáticas nunca ejecutan un instalador) |
| 8 | macOS completo | Lo mismo en mac, con Accesibilidad y la barra de menú nativa |
| 9 | Retirar los orígenes (sección 10) | Ningún catálogo ofrece las apps viejas |

Orden: primero lo que ya es de la base (fase 3), después los módulos que exigen servicios compartidos nuevos
(4 y 5), y al final Open in NukeX, que es el único con usuarios y el que más condiciones tiene. D-03 se
resuelve con una prueba corta antes de la fase 6.

**Antes de probar un módulo, su app de origen se cierra y se saca del inicio con Windows** en la máquina de
Lega: hoy corren Nuke Shortcuts y Folder Switch desde `build\` y Link Redirector instalado, y ocuparían los
atajos o competirían como navegador.

**Trabajo en otros repos, cada uno con pedido de Lega** (este repo no los toca por su cuenta):
1. `LGA_OpenInNukeX`: README que explique que el cliente y el plugin pasaron a Mighty Tools; después del
   período de espera pasa a privado (D-21, sección 9).
2. `LGA_PipeSync_2`: reemplazar la tarjeta de LGA OpenInNukeX por la de LGA Mighty Tools con badge NEW y
   sacar del catálogo la app y el plugin viejos (D-21), y el tooltip «Requiere LGA_OpenInNukeX»
   (`VersionsWidget.cpp`). Por plataforma: en mac no cambia hasta que exista la fase 8.
3. `LGA_Updates` y `LGA_SiteLega`: alta de Mighty Tools y baja de las apps viejas, en la tanda del release.
4. `LGA_RepoTools`: alta del repo en RepoRules, baja de los repos retirados.

## 7. Pruebas

- Una corrida automatizada NUNCA ejecuta input real: atajos, calibrador, inyección de teclas, cambio de
  carpeta en un diálogo ni apertura de links. Los modos de QA arman estados sin disparar nada.
- `--self-test`: la lógica de cada módulo sin pantalla, con casos negativos.
- `--simulate-action <acción>`: la secuencia real con el inyector en modo solo loguear.
- `--ui-shot <estado> <png>`: una captura por pantalla y estado, offscreen.
- `--ui-probe <caso>`: comportamiento de widgets con eventos internos (foco, teclas).
- Todo exe de prueba corre con `tools\qa\run_headless.ps1`. Las pruebas con mouse, teclado, Nuke real,
  diálogos reales y navegadores reales las hace Lega.

## 8. Decisiones

Viven en `Docs/Doc_Decisiones.md`, con la numeración D-01 a D-21. Las tomadas al 2026-09-26: ventana con
barra lateral (D-01), sin .NET (D-03), el plugin de Nuke dentro de este repo (D-04), nada prendido de
fábrica (D-06), los repos viejos se archivan (D-10), Link Redirector se publica (D-11) y la mudanza de los
usuarios de Open in NukeX por PipeSync (D-21).

## 9. Transición de los usuarios de Open in NukeX

Condiciones antes del primer release con el módulo Open in NukeX:

1. Leer la ruta de NukeX de `nukeXpath.txt` (que el usuario ya tiene) o autodetectarla y escribirla ahí,
   antes de tomar la asociación `.nk`. Sin eso, el primer doble click con NukeX cerrado da error.
2. Reescribir el comando del ProgID y verificar la asociación por ProgID más ruta del comando (4.6).
3. El hash de UserChoiceLatest en C++ verificado contra el helper original (D-03).
4. El plugin embebido con su propia versión (D-04).
5. La mudanza pasa por PipeSync (D-21), solo en Windows: su tarjeta de LGA OpenInNukeX se reemplaza por
   la de LGA Mighty Tools (badge NEW, «reemplaza a OpenInNukeX»), y sale DESPUÉS del release de Mighty
   Tools con su alta en LGA_Updates (fase 7). PipeSync deja de instalar el cliente y el plugin viejos: el
   plugin lo instala y actualiza el Nuke Bridge embebido (D-04). En mac, recién con la fase 8 (D-12).
6. La segunda copia abre la ventana de la residente (4.5).
7. En mac, sin `quit()` en el camino de la app residente.
8. La migración la hace la instalación de Mighty Tools (`--migrate-openinnukex` al terminar de copiar):
   detecta el cliente viejo solo por su clave de desinstalación (HKLM, WOW6432Node o HKCU, con
   DisplayName) o por el ProgID `LGA.NukeScript.1` apuntando a `LGA_OpenInNukeX.exe`; prende Open in
   NukeX si el usuario nunca lo decidió, sin inicio con Windows (se activa si prende otra herramienta);
   toma los `.nk` solo si eran del viejo o de un exe que ya no existe (nunca de otra copia de Mighty Tools
   ni con una elección de otra app); limpia sus restos solo si ya no está instalado; siempre deja la marca
   `migration/openInNukeX`. El viejo se quita solo con el sí del usuario: la casilla «Remove the old LGA
   OpenInNukeX (recommended)» del final del instalador o el botón «Uninstall old app» del panel. Los dos
   usan `--remove-old-client`, que corre su desinstalador en silencio (pide su propio UAC), espera a que
   desaparezca y retoma los `.nk`, porque ese desinstalador corre `assoc .nk=` como administrador.

Orden de los pasos:

1. Release de Mighty Tools con su alta en LGA_Updates; después, el PipeSync nuevo con la tarjeta.
2. El usuario instala Mighty Tools desde la tarjeta de PipeSync. La instalación migra: Open in NukeX queda
   prendido con su configuración (`nukeXpath.txt` es el mismo archivo) y tomando los `.nk`.
3. En la página final deja marcada la casilla y el cliente viejo se desinstala (Windows pide permiso).
   Si la desmarca o cancela el permiso, el panel de Open in NukeX lo sigue ofreciendo con «Uninstall old
   app».
4. Si usa el Review Panel de Hiero o el Nuke Bridge, actualiza el bridge desde el panel cuando el chip
   diga «Update available».
5. Pasado un período de espera (cuando los usuarios tengan el PipeSync nuevo, que se actualiza solo),
   `LGA_OpenInNukeX` pasa a privado. Antes hay que resolver lo que todavía lo usa: el DMG de mac de ese
   repo, `repos.json`/`versions.json` de LGA_Updates, `productos.json` de LGA_SiteLega y los textos de
   PipeSync que lo nombran.

Pendiente antes del release: probar el flujo real (instalador, desinstalador viejo y `assoc .nk=`) en
Windows Sandbox, nunca en la máquina de Lega. Lo primero a medir: si con el UserChoice intacto el doble
click en un `.nk` sigue abriendo después de `assoc .nk=` como administrador.

## 10. Retiro de los orígenes

- `LGA_NukeShortcuts`, `LGA_FolderSwitch` y `LGA_LinkRedirector` se archivan en GitHub cuando su módulo
  esté en un release de Mighty Tools, con un README que apunte al repo nuevo (D-10).
- `LGA_OpenInNukeX` también se archiva (D-04, D-10): el plugin vive en Mighty Tools.
- LGA_Updates y el sitio (`LGA_SiteLega/contenido/software/productos.json`) dejan de listar las apps
  viejas y suman Mighty Tools en la misma tanda que el release.
- Las reglas locales de los repos retirados se sacan de RepoRules.

## 11. Riesgos conocidos

- **Un solo proceso**: un módulo que bloquea el hilo principal congela a todos. Nada bloqueante en el hilo
  de UI; lo lento va a un hilo o a un proceso aparte.
- **Choques de atajos entre módulos**: hoy no chocan (`Ctrl+Shift+D`, `Ctrl+Alt+Shift+D`, `Ctrl+Alt+O`,
  `Ctrl+Alt+Shift+O`), pero el usuario los puede cambiar: el `HotkeyService` los valida contra todos.
- **Latencia de los links** en Windows: el modo corto arranca Qt en cada link. No hay medición previa:
  medirla en la fase 5 contra Link Redirector y, si hace falta, reenviar a la residente.
- **Mover el exe** rompe el comando de los ProgIDs: la app los reescribe en cada arranque de la copia
  instalada, nunca desde un árbol de desarrollo.
- **Ventana alta**: la tarjeta de discos crece con cada disco; con la barra lateral el panel necesita scroll.
- **Llamadas bloqueantes de Folder Switch**: COM y UI Automation son síncronas en el hilo de UI. Se miden en
  la fase 4 y, si alguna tarda, van con timeout o a un hilo.
- **mac y el permiso de Accesibilidad**: la firma ad-hoc cambia en cada update y macOS puede volver a
  pedir el permiso. Con cinco herramientas los updates son más frecuentes: el panel lo tiene que explicar.

## 12. Referencias

1. `C:\Portable\LGA_NukeShortcuts`: la base de arquitectura, Theme y QA.
2. `~/.nuke/LGA_OpenInNukeX`: el cliente (`QtClient/src`), el plugin (`init.py`) y los contratos con
   PipeSync (`docs/Doc_Nuke_Bridge.md`).
3. `C:\Portable\LGA_FolderSwitch`: detección de diálogos y managers, `UiaSwitcher`, `RecentFoldersPopup`.
4. `C:\Portable\LGA_LinkRedirector`: `UrlRouter`, registro de navegador por plataforma.
5. `C:\Portable\LGA_PipeSync_2`: el catálogo que actualiza el plugin (`src/services/updates`) y los que
   leen `nukeXpath.txt` (`src/features/settings/components/UserAccountPanel.cpp`,
   `py_scr/open_nuke_latest_version.py`).
6. `C:\Portable\LGA_Updates`: el manifiesto de versiones.
7. `C:\Portable\LGA_Base_QT_C_Py\docs`: instaladores, autostart, registro LGA, checklist de app nueva.
