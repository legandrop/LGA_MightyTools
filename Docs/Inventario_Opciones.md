# Inventario de opciones de las apps de origen

Todo lo que el usuario ve o puede configurar hoy en las cuatro apps que se unen en LGA Mighty Tools,
relevado del código el 2026-09-25. Es la lista de control del diseño: cada elemento tiene que aparecer en
el diseño o figurar en la sección «Descartes» con su motivo. Las rutas `archivo:línea` son de cada repo
de origen.

1. Nuke Shortcuts y Disk Space: `C:\Portable\LGA_NukeShortcuts`
2. Open in NukeX: `C:\Users\leg4-pc\.nuke\LGA_OpenInNukeX\QtClient`
3. Folder Switch: `C:\Portable\LGA_FolderSwitch`
4. Link Redirector: `C:\Portable\LGA_LinkRedirector`

## Descartes

Elementos de las apps de origen que Mighty Tools deja afuera a propósito, con el motivo. Es la propuesta
del diseño presentado el 2026-09-25; se confirma al aprobarlo (D-01) y lo que Lega decida distinto se
corrige acá.

| Elemento | App | Motivo |
|---|---|---|
| Pestañas Settings / Help | Link Redirector | La ventana única las reemplaza; la ayuda pasa a la ayuda común |
| Estilo «Liquid Glass» | Link Redirector | La estética es la de Nuke Shortcuts |
| Selector de idioma EN / ES | Open in NukeX | UI solo en inglés por ahora (D-08) |
| Coloreado de rutas por carpeta en los diálogos | Open in NukeX | Cada ruta en su línea, en un solo color (D-14) |
| Plugin dentro de Nuke (servidor TCP, `paste_clipboard`, mensajes de consola) | Open in NukeX | Es el plugin, no el módulo: sigue en su repo |
| `showWarning` sin ningún llamado | Folder Switch | Nunca se usaba |
| Migración de settings desde el registro | Folder Switch | Nadie la instaló: no hay nada que migrar |
| Cartel «No system tray available in this session.» | Link Redirector | Se espera la bandeja hasta 90 s sin cartel, como Nuke Shortcuts y Folder Switch |

Además cambian, sin descartarse: los textos que estaban en castellano o mezclados (Link Redirector,
mensajes del doble click de Open in NukeX) pasan a inglés; «Disk space» pasa a «Watched drives» y «File
Association» a «.nk files»; los atajos de Folder Switch pasan a ser editables (D-16); el aviso «NukeX
Launcher» pasa a ser una opción visible, apagada por defecto. El detalle está en la tabla de cobertura del
diseño.

## LGA Nuke Shortcuts

Inventario exhaustivo de todo lo que el usuario ve o puede configurar en la app Qt/C++ actual
(`C:\Portable\LGA_NukeShortcuts`, versión de código v2.06). No incluye la versión legado AutoHotkey
(`Legacy_AHK/`), que es un binario aparte y no forma parte del diseño nuevo.

---

### General de la app

#### Controles y opciones

| Elemento | Tipo de control | Valores / rango / default | Texto exacto en la UI | Dónde se guarda | Plataforma | Evidencia archivo:línea |
|---|---|---|---|---|---|---|
| Título de la ventana | Barra de título propia (TitleBar) | — | "LGA Nuke Shortcuts" | — | Win/mac | `src/ui/MainWindow.cpp:124`, `src/ui/TitleBar.cpp:139,157` |
| Botón Help | Botón icono (signo de pregunta) | — | (sin texto, tooltip "Help") | — | Win/mac | `src/ui/TitleBar.cpp:144,163` |
| Botón Minimize | Botón icono | — | (sin texto, tooltip "Minimize") | — | Solo Windows (en mac es el semáforo amarillo) | `src/ui/TitleBar.cpp:173-174` |
| Botón Close | Botón icono | — | (sin texto, tooltip "Close") | — | Solo Windows (en mac es el semáforo rojo) | `src/ui/TitleBar.cpp:176-177` |
| Semáforo Close (mac) | Botón circular rojo | — | tooltip "Close" | — | Solo mac | `src/ui/TitleBar.cpp:134` |
| Semáforo Minimize (mac) | Botón circular amarillo | — | tooltip "Minimize" | — | Solo mac | `src/ui/TitleBar.cpp:135` |
| Checkbox de inicio con la sesión | QCheckBox | on/off, default: se activa solo en el primer arranque de una copia instalada | Windows: "Start with Windows" · mac: "Open at login" | `HKCU\...\Run` valor `LGA_NukeShortcuts` (Win) / `SMAppService.mainApp` (mac); no en el .ini | Win/mac (texto distinto) | `src/platform/AutoStartCommon.cpp:55-62`, `src/ui/MainWindow.cpp:238` |
| Checkbox "Check for updates at startup" | QCheckBox | on/off, default ON | "Check for updates at startup" | clave `checkUpdatesAtStartup` en settings.ini | Solo Windows (oculto en mac) | `src/ui/MainWindow.cpp:242,254`, `src/core/AppState.cpp:13,37` |
| Botón "Check now" | QPushButton (variante sm) | dispara chequeo manual | "Check now" | — | Solo Windows | `src/ui/MainWindow.cpp:243` |
| Etiqueta de versión instalada | QLabel (caption) | — | "Installed version: v2.06" (versión leída de CMakeLists) | — | Win/mac | `src/ui/MainWindow.cpp:258` |
| Tarjeta de estado (arriba de todo) | QFrame + punto + título + caption + botón | 4 estados (ver "Estados visibles") | ver abajo | — | Win/mac | `src/ui/MainWindow.cpp:162-180,311-363` |
| Botón de la tarjeta de estado | QPushButton | texto y variante cambian según el estado | "Pause" / "Resume" / "Change" / "Open Settings" | — | Win/mac | `src/ui/MainWindow.cpp:325,331,345,353` |
| Icono de la bandeja / barra de menú | QSystemTrayIcon | activo (nítido) / en pausa (atenuado 40%) | — | — | Win: bandeja; mac: barra de menú | `src/tray/TrayMenu.cpp:72-94` |
| Instancia única | QLockFile | — | (sin UI; una segunda instancia se cierra sola) | archivo de lock en carpeta temp del sistema | Win/mac | `src/main.cpp:344-348` |
| Cerrar ventana | Evento de ventana | — | (no hay botón "salir" en la ventana; cerrar oculta a la bandeja) | — | Win/mac | `src/ui/MainWindow.cpp:511-517` |
| Arrastrar la ventana | Gesto de mouse en la barra de título | — | — | — | Win/mac | `src/ui/TitleBar.cpp:203-212` |
| Ancho fijo de la ventana | Layout | 440 px de ancho, alto automático según contenido | — | — | Win/mac | `src/ui/MainWindow.cpp:33,416-430` |

#### Estados visibles

Tarjeta de estado (la primera de Settings), según `MainWindow::currentStatus()`:

| Estado | Punto (dot) | Título | Texto | Botón | Tono de la tarjeta | Cuándo aparece |
|---|---|---|---|---|---|---|
| Activos | `on` | "Shortcuts are on" | "Only in Nuke. Other apps keep these keys." | "Pause" | normal | Atajos activos (Nuke al frente o no: el texto no cambia con eso) |
| En pausa | `paused` | "Shortcuts are paused" | "Nuke gets these keys as usual." | "Resume" (variante primaria) | normal | El usuario pausó desde la tarjeta o el menú |
| Atajo tomado | `error` | "%1 is off" o "Both shortcuts are off" | "Another app uses %1." o "Another app uses them." | "Change" | `err` (borde/rojo) | El sistema rechazó el registro de una o las dos combinaciones |
| Falta el permiso (mac) | `warn` | "Accessibility access needed" | "Needed to click and type in Nuke." | "Open Settings" (variante primaria) | `warn` (ámbar) | mac sin el permiso de Accesibilidad concedido |

Otros estados visuales generales:
- Icono de bandeja atenuado al 40% de opacidad cuando la app está en pausa (`src/tray/TrayMenu.cpp:85-91`).
- Ventana con esquinas redondeadas y sombra nativa de Windows 11 aplicadas por código (`applyNativeFrame`, `src/ui/MainWindow.cpp:519-536`).

#### Menú de la bandeja

Común a todas las apps LGA en Windows (bandeja) y macOS (barra de menú). Orden de arriba a abajo:

| Entrada | Texto exacto | Cuándo aparece | Qué hace |
|---|---|---|---|
| Encabezado | "Nuke Shortcuts · On" / "Nuke Shortcuts · Paused" | Siempre, deshabilitado (no clickeable) | Solo informa el estado |
| Alternar pausa | "Pause shortcuts" / "Resume shortcuts" | Siempre | Activa/desactiva los atajos globales |
| (separador de discos bajos) | — | Solo si hay al menos un disco vigilado bajo su umbral | Ver módulo Disk Space |
| (separador principal) | — | Siempre | — |
| Configuración | "Settings..." | Siempre | Abre/muestra la ventana principal |
| Calibrar | "Calibrate Dope Sheet..." | Siempre | Abre el diálogo de calibración |
| Updates | "Check for Updates..." | Solo Windows (oculto en mac) | Chequeo manual de actualizaciones |
| (separador) | — | Siempre | — |
| Salir | "Quit" | Siempre | Cierra la app completamente |

Doble click / click simple en el icono de la bandeja (Windows): abre Settings. En mac el click abre
directamente el menú nativo (no hay doble click). Clic en una notificación de la app: abre Settings.

Evidencia: `src/tray/TrayMenu.cpp:11-30`, `src/tray/TrayController.cpp:342-352,88`.

#### Diálogos, popups y ventanas secundarias

**Ayuda (HelpDialog)** — abre desde el botón "?" de la barra de título:
- Encabezado: "LGA Nuke Shortcuts" + "v2.06" (versión actual) en la misma línea.
- "Developed by Lega Pugliese"
- Link "github.com/legandrop" (subrayado, cambia de color al pasar el mouse; tooltip
  "https://github.com/legandrop"; abre el navegador al hacer click)
- Botón X para cerrar (arriba a la derecha)
- Sección "How it works", 4 pasos numerados:
  1. "Put the pointer over a knob in Nuke and press **Ctrl+Shift+D** to set a key." (la combinación
     mostrada es la actualmente configurada, resaltada en un color más claro)
  2. "Calibrate the **Dope Sheet** once: one click on an empty spot."
  3. "Press **Ctrl+Alt+Shift+D** to select every key in the Dope Sheet and frame them."
  4. "Under **Disk space**, add the drives to watch: you get a notification when one runs low."
- Nota final: "The shortcuts only work while Nuke is in front; other apps keep these keys. Closing
  the window keeps Nuke Shortcuts running in the tray." (mac: "...in the menu bar.")
- Botón "Close"
- Se muestra sobre un velo semitransparente (`Scrim`) que oscurece la ventana principal.
- Evidencia: `src/ui/HelpDialog.cpp:94-179`.

**Calibrar Dope Sheet (CalibrationDialog)** — abre desde "Calibrate..." de la tarjeta o del menú de
bandeja:
- Título: "Calibrate Dope Sheet"
- 3 pasos numerados:
  1. "Open Nuke with the **Dope Sheet** visible."
  2. "Click Start, then click an **empty spot** inside it."
  3. "The spot is saved relative to the Nuke window."
- Imagen: captura del layout de Nuke (`DopeSheetPos.png`) con un círculo violeta en el punto de
  ejemplo, 392x305 px con borde de 1 px.
- Texto auxiliar: "Esc cancels at any time."
- Botones: "Cancel" y "Start" (el que acepta, remarcado, a la derecha)
- Ventana sin marco del sistema, siempre encima (`WindowStaysOnTopHint`), esquinas redondeadas.
- Evidencia: `src/ui/CalibrationDialog.cpp:62-116`.

**Diálogo de actualización disponible (Windows)** — `createUpdateAvailableDialog`:
- Título de ventana: "Update Available"
- Texto principal: "LGA Nuke Shortcuts %1 is available." (%1 = versión nueva, ej. "2.1")
- Texto secundario: "You are running version %1. Updating closes Nuke Shortcuts and opens the
  installer." (%1 = versión instalada actual)
- Botones: "Later" (cancela) y "Update now" (por defecto, acepta y arranca la descarga)
- Evidencia: `src/updates/UpdateDialog.cpp:11-52`.

**Diálogo de progreso de descarga (Windows)** — QProgressDialog:
- Título de ventana: "Downloading Update"
- Texto: "Downloading %1 %2..." (nombre de la app + versión)
- Botón: "Cancel"
- Evidencia: `src/updates/UpdateService.cpp:408-415`.

**Cuadros de mensaje del updater (QMessageBox, todos Windows, todos modales)**:
| Título | Texto | Cuándo |
|---|---|---|
| "Updates" | "An update operation is already in progress." | Chequeo manual mientras ya hay uno en curso |
| "Update Check Failed" | "Could not check for updates. Please try again later.\n\nhttpStatus=%1\nurl=%2\nnetworkError=%3\nbody=%4" | Error de red al chequear (chequeo manual) |
| "Update Check Failed" | Igual formato, con status HTTP inválido | Respuesta HTTP distinta de 200 |
| "Updates" | "No installable update was found." | Manifiesto sin release para esta app (chequeo manual) |
| "Updates" | "Release %1 does not contain an installable asset." | Release sin el instalador esperado |
| "Updates" | "You are running the latest version." | Ya está al día (chequeo manual) |
| "Update Check Failed" | "Release %1 does not include a valid integrity digest. Refusing to download an unverifiable installer." | El manifiesto no trae SHA-256 válido |
| "Update Failed" | "The update cache directory could not be created." | No se pudo crear la carpeta temporal |
| "Update Failed" | "The update installer could not be saved.\n%1" | No se pudo abrir el archivo destino |
| "Update Failed" | "The update installer could not be written to disk." | Falla de escritura (disco lleno, permisos) |
| "Update Failed" | "The update installer could not be downloaded.\n\nhttpStatus=%1\nnetworkError=%2 (%3)" | Error de red durante la descarga |
| "Update Failed" | "Internal error: missing integrity digest for the downloaded update." | Bug interno (guard de seguridad) |
| "Update Failed" | "The downloaded update failed integrity verification.\n\nesperado=%1\nobtenido=%2" | El SHA-256 no coincide |
| "Update Failed" | "The update installer could not be saved.\n\n%1" | Falla al confirmar el archivo (`commit()`) |

Evidencia: `src/updates/UpdateService.cpp:207-591`.

#### Notificaciones y avisos

- Ninguna notificación general de bandeja fuera de las de los dos módulos (ver Nuke Shortcuts y Disk
  Space). Clic en cualquier notificación de la app abre Settings (`src/tray/TrayController.cpp:88`).

#### Atajos de teclado y gestos

- Ninguno propio de "General de la app" además de: arrastrar la ventana desde la barra de título, y
  los botones de la barra (Help, Minimize, Close) que son solo mouse.

#### Textos de ayuda y tooltips

| Control | Tooltip / texto |
|---|---|
| Botón Help | "Help" |
| Botón Minimize | "Minimize" |
| Botón Close | "Close" |
| Semáforo rojo (mac) | "Close" |
| Semáforo amarillo (mac) | "Minimize" |
| Checkbox de inicio con la sesión, disponible (Windows) | "Start LGA Nuke Shortcuts when you sign in to Windows" |
| Checkbox de inicio con la sesión, NO disponible (Windows, corre desde build/deploy) | "Registers THIS development copy to start when you sign in" |
| Checkbox de inicio con la sesión, disponible (mac) | "Open LGA Nuke Shortcuts when you log in" |
| Checkbox de inicio con la sesión, NO disponible (mac) | "Registers THIS development copy to open when you log in" |
| Link de GitHub (Help) | "https://github.com/legandrop" |

Evidencia: `src/ui/MainWindow.cpp:438-447`, `src/ui/HelpDialog.cpp:45`.

#### Comportamientos configurables sin control visible

- `firstRunCompleted` (settings.ini): marca interna que consume el primer arranque de una copia
  instalada (activa sola el inicio con la sesión y abre Settings una única vez). No hay control para
  resetearla desde la UI. `src/tray/TrayController.cpp:134-153`.
- `config/debug_flags.txt` (clave=valor, `#` comenta), fuera del .ini de settings:
  - `log=true` → escribe `debug.log` (ruta de `AppPaths::logFile()`). Sin esto la app no deja rastro
    en archivo (los `qDebug`/`qInfo` se pierden, no hay consola).
  - `dryRunInput=true` → las dos acciones sobre Nuke (y el registro de atajos) NO mueven el mouse ni
    aprietan teclas de verdad: solo loguean los pasos. Equivalente al flag de arranque
    `--dry-run-input`.
  - `src/core/DebugFlags.cpp`, `src/main.cpp:356`.
- Marcador de versión embebido en el binario (`LGA_NUKESHORTCUTS_BUILD_VERSION=...`), lo usa un guard
  del instalador; invisible para el usuario. `src/main.cpp:35-42`.
- Reintento de bandeja al arrancar: hasta 90 s, sondeando cada 500 ms si el sistema ya tiene la
  bandeja lista (útil en el arranque de sesión de Windows). Sin UI. `src/main.cpp:361-384`.
- Registro en `LgaRegistry` (registro compartido entre apps LGA instaladas): no visible al usuario,
  solo corre en copias instaladas (no en un build de desarrollo). `src/main.cpp:350-352`.
- Archivo de bloqueo de instancia única en la carpeta temporal del sistema
  (`com.lga.nukeshortcuts.singleton.lock`). `src/main.cpp:344`.
- Manifiesto de actualizaciones: URL fija `https://legandrop.github.io/LGA_Updates/versions.json`,
  sin forma de apuntar a otro. Verificación SHA-256 obligatoria (fail-closed): sin digest válido, no
  se descarga nada. `src/updates/UpdateService.cpp:35-39,330-341`.
- Demora del chequeo automático de updates al arrancar: 15 s fijos
  (`kAutomaticCheckDelayMs`), sin control de usuario. Timeout de chequeo 15 s, de descarga 300 s
  (por inactividad, no total). `src/updates/UpdateService.cpp:44-46`.

---

### Nuke Shortcuts

Módulo de los dos atajos sobre Nuke (Add keyframe, Frame Dope Sheet), su calibrador y (en mac) el
permiso de Accesibilidad.

#### Controles y opciones

| Elemento | Tipo de control | Valores / rango / default | Texto exacto en la UI | Dónde se guarda | Plataforma | Evidencia archivo:línea |
|---|---|---|---|---|---|---|
| Fila "Add keyframe" | ShortcutRow (nombre + cajas de teclas + lápiz + descripción) | Combinación de teclas | Nombre: "Add keyframe" · Descripción: "Sets a key on the knob under the pointer." | `shortcuts/addKeyframe` (texto portable, ej. "Ctrl+Shift+D") | Win/mac | `src/ui/MainWindow.cpp:189-190`, `src/core/AppState.cpp:14` |
| Fila "Frame Dope Sheet" | ShortcutRow | Combinación de teclas | Nombre: "Frame Dope Sheet" · Descripción: "Selects every key in the Dope Sheet and frames them." | `shortcuts/frameDopeSheet` | Win/mac | `src/ui/MainWindow.cpp:193-194` |
| Atajo por defecto: Add keyframe | — | Windows: Ctrl+Shift+D · mac: Cmd+Shift+D (mismo valor de .ini, distinto glifo) | cajas "Ctrl" "Shift" "D" (mac: "⌘" "⇧" "D") | — | Win/mac | `src/core/Shortcut.cpp:83-89` |
| Atajo por defecto: Frame Dope Sheet | — | Windows: Ctrl+Alt+Shift+D · mac: Cmd+Opt+Shift+D | cajas "Ctrl" "Alt" "Shift" "D" (mac: "⌃" "⌥" "⇧" "⌘" según corresponda) | — | Win/mac | `src/core/Shortcut.cpp:91-96` |
| Botón lápiz (cambiar atajo) | Botón icono | inicia/cancela grabación | tooltip "Change shortcut" | — | Win/mac | `src/ui/ShortcutRow.cpp:78-79` |
| Campo de grabación | Reemplaza las cajas mientras se graba | teclas válidas: letras, dígitos, F1-F12, con al menos un modificador (Ctrl/Alt/Meta; en mac ⌘/⌥/⌃) | placeholder "Press a shortcut..."; con un modificador apretado, "Ctrl + Shift + ..." (mac: "⌃⌥⇧ ...") | — | Win/mac (formato distinto) | `src/ui/ShortcutRow.cpp:117-132` |
| Botón "Calibrate..." | QPushButton (sm) | abre el diálogo de calibración | "Calibrate..." | — | Win/mac | `src/ui/MainWindow.cpp:220` |
| Miniatura del layout de Nuke | Widget pintado (SpotThumbnail) | con/sin punto guardado | — (imagen) | — | Win/mac | `src/ui/MainWindow.cpp:73-117` |
| Permiso de Accesibilidad (mac) | Fila de estado especial + botón | concedido / no concedido | ver tarjeta de estado, botón "Open Settings" | — (se consulta en vivo al sistema, sondeado cada 2000 ms) | Solo mac | `src/tray/TrayController.cpp:39,314-317`, `src/platform/mac/SystemInputMac.cpp:31-33` |

#### Estados visibles

- Tarjeta de estado (compartida con "General"): además de On/Paused, dos estados propios de este
  módulo:
  - "%1 is off" / "Both shortcuts are off" cuando el sistema rechaza el registro de una o las dos
    combinaciones (otra app ya las usa).
  - "Accessibility access needed" en mac sin el permiso concedido.
- Tarjeta "Dope Sheet position": Chip "Calibrated" (tono `ok`) o "Not calibrated" (tono `warn`).
  - Calibrado: valor "%1% across · %2% down" (porcentaje del ancho y alto de la ventana de Nuke) +
    caption "Of the Nuke window, so it follows moves and resizes."
  - Sin calibrar: "No spot saved yet" + caption "Frame Dope Sheet needs it. Takes one click."
- Miniatura (SpotThumbnail): sin punto, imagen atenuada (opacidad 0.35) con borde punteado gris;
  con punto, imagen más visible (opacidad 0.85), borde sólido y un círculo violeta en la posición
  guardada.
- Fila de atajo grabando: las cajas de teclas se reemplazan por un recuadro con borde de acento y un
  punto (`recorderDot`); la descripción cambia a "Press the new combination. Esc cancels."
- Fila de atajo rechazado: la descripción se pinta en rojo con el motivo + "Kept %1." (el atajo
  anterior que se conservó).
- Burbuja de calibración (sigue al puntero):
  - Sobre Nuke: "Click inside the Dope Sheet" + "%1% · %2% of Nuke    Esc cancels"
  - Fuera de Nuke: "Move over the Nuke window" + "Esc cancels"
  - Después de un click que no cayó en Nuke: "That's not Nuke. Try again." (en rojo) + "Esc cancels"

Evidencia: `src/ui/MainWindow.cpp:373-385`, `src/ui/ShortcutRow.cpp:134-143,173-177`,
`src/ui/CalibrationSession.cpp:51-65`.

#### Menú de la bandeja

Entradas ya listadas en "General de la app" que pertenecen a este módulo: encabezado
"Nuke Shortcuts · On/Paused", "Pause shortcuts"/"Resume shortcuts", "Calibrate Dope Sheet...".

#### Diálogos, popups y ventanas secundarias

- **CalibrationDialog** ("Calibrate Dope Sheet"): descrito en "General" (es compartido visualmente,
  pero funcionalmente es 100% de este módulo). Al aceptar ("Start") arranca la `CalibrationSession`:
  la ventana de Settings se oculta (para no tapar Nuke) mientras se espera el click.
- **Burbuja de calibración** (`CalibrationBubble`): ventana flotante sin marco, siempre encima,
  transparente a los clicks (`WindowTransparentForInput`), no roba foco; sigue al cursor con offset
  de 20 px, reacomodándose si no entra en la pantalla.
- **HelpDialog**: los pasos 1 a 3 documentan este módulo (ver "General").

#### Notificaciones y avisos

| Texto | Cuándo |
|---|---|
| Título "Frame Dope Sheet", cuerpo "Calibrate the Dope Sheet first: one click, from the tray menu." (ícono Info, dura 6000 ms) | El usuario aprieta el atajo Frame Dope Sheet sin haber calibrado nunca un punto |

Evidencia: `src/tray/TrayController.cpp:272-277`.

#### Atajos de teclado y gestos

- **Add keyframe** (default Ctrl+Shift+D / mac Cmd+Shift+D): click derecho en la posición actual del
  puntero, flecha abajo, Enter. Temporizado: 15 ms tras soltar modificadores, 50 ms para que abra el
  menú contextual, 30 ms antes de Enter.
- **Frame Dope Sheet** (default Ctrl+Alt+Shift+D / mac Cmd+Opt+Shift+D): click izquierdo en el punto
  calibrado del Dope Sheet, Ctrl+A (Cmd+A en mac), F, y el puntero vuelve a su posición original.
  Temporizado: 15 ms tras soltar modificadores, 30 ms tras el click, 10 ms entre Ctrl+A y F, 15 ms
  antes de restaurar el puntero.
- Ambos atajos solo están activos con Nuke al frente y (si Nuke deja de estarlo antes de procesarse)
  la combinación se le "devuelve" a la app que quedó al frente, como si esta app no existiera.
- Grabar un atajo nuevo: cualquier letra/dígito/F1-F12 con al menos un modificador no-Shift
  (Ctrl/Alt/Meta en Windows; ⌘/⌥/⌃ en mac). Esc cancela la grabación en cualquier momento.
- Calibración: NO se captura el click con Qt; se sondea el estado físico del botón principal del
  mouse cada 16 ms, así el click le sigue llegando a Nuke. Esc cancela en cualquier momento
  (sondeado igual, cada 16 ms).

Evidencia: `src/actions/ActionRunner.cpp:11-24,47-89`, `src/ui/CalibrationSession.cpp:20-27,135-161`.

#### Textos de ayuda y tooltips

| Control | Texto |
|---|---|
| Botón lápiz de cada fila de atajo | "Change shortcut" |
| Mensaje de error: tecla no soportada | "Use a letter, a number or F1-F12." |
| Mensaje de error: falta modificador (Windows) | "Add Ctrl or Alt so it doesn't take a plain key." |
| Mensaje de error: falta modificador (mac) | "Add ⌘, ⌥ or ⌃ so it doesn't take a plain key." |
| Mensaje de error: ya usado por la otra acción | "Already used by %1." |
| Mensaje de error: lo tiene otra app | "%1 is taken by another app." |
| Formato de rechazo completo | "%1 Kept %2." (motivo + atajo anterior conservado) |
| Descripción fija fila Add keyframe | "Sets a key on the knob under the pointer." |
| Descripción fija fila Frame Dope Sheet | "Selects every key in the Dope Sheet and frames them." |
| CalibrationDialog, texto auxiliar | "Esc cancels at any time." |

Evidencia: `src/ui/ShortcutRow.cpp:209-224`, `src/tray/TrayController.cpp:239-245`.

#### Comportamientos configurables sin control visible

- `enabled` (settings.ini): pausa/reanuda TODOS los atajos a la vez; no hay pausa por atajo
  individual.
- `dopeSheet/x`, `dopeSheet/y` (settings.ini): el punto calibrado, como fracción 0..1 del ancho y
  alto de la ventana principal de Nuke (nunca coordenadas absolutas de pantalla). Un valor fuera de
  rango en el .ini se descarta silenciosamente (queda "sin calibrar").
- Un atajo ilegible en el .ini (editado a mano o de una versión futura) vuelve solo al valor de
  fábrica en lugar de dejar la acción sin atajo.
- Estado de registro (`Registration`: Idle/Registered/Failed) es de sesión, no se persiste.
- `accessibilityGranted` (mac) es de sesión, se vuelve a consultar al sistema en cada arranque; se
  sondea cada 2000 ms mientras no esté concedido.
- Constantes de tiempo de las secuencias de acción (ms), fijas en código, sin UI para cambiarlas:
  15 (tras soltar modificadores), 50 (apertura de menú), 30 (antes de Enter / tras click de foco),
  10 (entre Ctrl+A y F), 15 (antes de restaurar el puntero).
- IDs internos de los atajos globales (1 = Add keyframe, 2 = Frame Dope Sheet), sin relevancia para
  el usuario.
- `--dry-run-input` / `dryRunInput=true`: ninguna acción real se ejecuta, solo se loguean los pasos
  (usado en QA, nunca en uso normal).

#### Diferencias por plataforma

- **Detección de Nuke**: por el nombre del ejecutable del proceso en primer plano (`Nuke*.exe` en
  Windows; nombre del bundle en mac), nunca por clase de ventana.
- **Registro de atajos globales**: `RegisterHotKey` (Win32) vs `RegisterEventHotKey` (Carbon, mac).
- **Inyección de input**: `SendInput`/`SetCursorPos` (Win32) vs `CGEventPost` (mac).
- **Permiso de Accesibilidad**: solo existe en mac (`SystemInput::needsAccessibilityPermission()`
  devuelve `true` en mac y `false` en Windows). Sin él, `CGEventPost` no puede mandar clicks ni
  teclas a Nuke. El botón "Open Settings" abre el panel de Privacidad y Accesibilidad del sistema
  (`x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility`).
- **Mapeo de modificadores**: en el .ini, "Ctrl" significa literalmente Ctrl en Windows y Cmd en mac
  (mismo texto portable, Nuke usa el mismo mapeo). Las cajas de teclas muestran "Ctrl"/"Alt"/"Shift"
  (Win) o los glifos ⌃⌥⇧⌘ en el orden de Apple (mac).
- **Ctrl+A del Dope Sheet**: se manda como Cmd+A en mac.
- Nota de HelpDialog: "in the tray" (Windows) vs "in the menu bar" (mac).

---

### Disk Space

Módulo de monitoreo de espacio libre en disco: tarjeta, discos vigilados, avisos.

#### Controles y opciones

| Elemento | Tipo de control | Valores / rango / default | Texto exacto en la UI | Dónde se guarda | Plataforma | Evidencia archivo:línea |
|---|---|---|---|---|---|---|
| Título de la tarjeta | QLabel | — | "Disk space" | — | Win/mac | `src/ui/DiskCard.cpp:287` |
| Chip de estado de la tarjeta | Chip | "Off" (sin discos) / "All good" / "%1 is low" / "%1 drives are low" | ver texto | — | Win/mac | `src/ui/DiskCard.cpp:441-453` |
| Caption sin discos vigilados | QLabel | — | "Get a notification when a drive runs low. Nothing is watched yet." | — | Win/mac | `src/ui/DiskCard.cpp:294` |
| "Check every" + botón de intervalo | Label + botón desplegable (QMenu) | 1 / 5 / 15 / 30 / 60 / 360 min, default 15 min | "Check every" + texto del intervalo elegido: "1 min", "5 min", "15 min", "30 min", "1 hour", "6 hours" | `disks/checkMinutes` | Win/mac | `src/ui/DiskCard.cpp:308-315`, `src/core/DiskSpace.cpp:23-41` |
| "Last check" | QLabel (meta) | hora de la última lectura | "Last check %1" (formato HH:mm) | — (se recalcula, no se guarda) | Win/mac | `src/ui/DiskCard.cpp:435-436` |
| Keycap del disco | Chip | letra de unidad (Win) / nombre de volumen (mac) | ej. "C:" | — | Win/mac (contenido distinto) | `src/ui/DiskCard.cpp:235-236`, `src/core/DiskSpace.cpp:104-121` |
| Nombre del disco | ElidedLabel (recorta con "...", tooltip = texto completo) | — | nombre del volumen (ej. "Cache") | `disks/watched[i]/name` | Win/mac | `src/ui/DiskCard.cpp:117-120,237-238` |
| Etiqueta "Not connected" | QLabel (meta) | visible solo si el disco no está enchufado | "Not connected" | — | Win/mac | `src/ui/DiskCard.cpp:121,240` |
| Campo de umbral | QSpinBox, sin flechas, alineado a la derecha, 52x24 px | 1-100000 (GB) o 1-99 (%), default 50 GB al agregar el primer disco (o el valor/unidad del último agregado) | número | `disks/watched[i]/value` | Win/mac | `src/ui/DiskCard.cpp:131-138,244-247`, `src/core/DiskSpace.cpp:49-52` |
| Botones de unidad GB / % | QPushButton checkable, segmentados | GB o % | "GB" / "%" | `disks/watched[i]/unit` | Win/mac | `src/ui/DiskCard.cpp:143-153` |
| Botón "dejar de vigilar" (X) | Botón icono | — | tooltip/accessible name "Stop watching" | quita la entrada de `disks/watched` | Win/mac | `src/ui/DiskCard.cpp:156-160` |
| Barra de uso (UsageBar) | Widget pintado | fracción ocupada del disco + marca del umbral | — (visual) | — | Win/mac | `src/ui/DiskCard.cpp:52-97` |
| Texto de espacio libre | QLabel (caption) | — | "%1 free of %2" (+ " · under %1" si está bajo) | — | Win/mac | `src/ui/DiskCard.cpp:262-268` |
| Texto de umbral | QLabel (meta) | oculto cuando el disco está bajo (el texto pasa al de espacio libre) | "warn under %1" | — | Win/mac | `src/ui/DiskCard.cpp:255,269-270` |
| Botón "Add drive..." | QPushButton tipo link, icono + | abre menú de discos locales no vigilados | "Add drive..." | agrega entrada a `disks/watched` | Win/mac | `src/ui/DiskCard.cpp:329-331` |
| Menú "Add drive..." | QMenu | un ítem por disco local sin vigilar | encabezado "Local drives" (o "Every local drive is already watched" si no queda ninguno); cada ítem: "<letra/nombre>  <nombre>  ·  <libre> free" | — | Win/mac | `src/ui/DiskCard.cpp:458-480` |

#### Estados visibles

- Chip de la tarjeta: `src` "Off" (sin discos vigilados), `ok` "All good" (todos por encima de su
  umbral), `warn` "%1 is low" (un disco) o "%1 drives are low" (varios). El borde de toda la tarjeta
  toma tono ámbar (`tone=warn`) si hay al menos un disco bajo.
- Fila de disco conectado: keycap tono `key`, barra de uso normal (ámbar si está bajo el umbral),
  texto de libre/total.
- Fila de disco desconectado: keycap tono `src`, etiqueta "Not connected", barra atenuada/sin
  relleno, caption "Skipped until it is plugged in." en lugar del texto de espacio libre; el umbral
  configurado se sigue mostrando ("warn under %1").
- Barra de uso: riel gris, relleno según ocupación, marca vertical en el punto del umbral; pasado el
  umbral el relleno se pinta ámbar.

Evidencia: `src/ui/DiskCard.cpp:229-271,419-456`.

#### Menú de la bandeja

Además del encabezado y "Pause/Resume" (compartidos), este módulo agrega:

| Entrada | Texto exacto | Cuándo aparece | Qué hace |
|---|---|---|---|
| Aviso de disco bajo | "%1 is low · %2 free" (una línea por disco vigilado, conectado y bajo su umbral) | Solo mientras haya al menos un disco así | Click abre Settings |
| Separador de avisos de disco | — | Se muestra solo si hay al menos un aviso | separa los avisos del resto del menú |

Evidencia: `src/tray/TrayMenu.cpp:38-70`.

Tooltip de la bandeja/menú (no es una entrada de menú, es el tooltip del icono): además del nombre de
la app ("LGA Nuke Shortcuts" o "...— paused"), una línea por disco bajo: "%1 %2 free" (label + libre).
`src/tray/TrayController.cpp:170-180`.

#### Diálogos, popups y ventanas secundarias

- **Menú "Add drive..."** (QMenu, no modal): ver tabla de controles arriba.
- **Menú del intervalo** (QMenu, no modal): un ítem checkable por cada intervalo disponible (1, 5,
  15, 30, 60, 360 min), con el actual marcado. `src/ui/DiskCard.cpp:493-506`.

#### Notificaciones y avisos

| Título | Cuerpo | Icono / duración | Cuándo |
|---|---|---|---|
| "%1 is running low" (label del disco) | "%1 free of %2. You asked to be warned under %3." | Warning, 10000 ms | Un disco vigilado y conectado cruza su umbral hacia abajo |

Reglas de cuándo avisar (no configurables desde la UI):
- El primer chequeo que puede avisar ocurre 20 s después de arrancar la app (para no sumar un aviso
  al iniciar sesión en Windows); el primer chequeo (inmediato, al iniciar) solo actualiza la lectura,
  sin avisar.
- Mientras el disco sigue bajo, el aviso se repite cada 6 horas (no antes).
- Si el disco sube por encima del umbral y vuelve a bajar, se avisa de nuevo al cruzar.
- Un disco desconectado nunca cuenta como "bajo" ni dispara aviso; al reconectarse retoma su
  historial de avisos previo (no repite el aviso apenas se conecta con el mismo valor bajo, solo si
  vuelve a cruzar).

Evidencia: `src/tray/TrayController.cpp:43-44,192-199`, `src/core/DiskSpace.cpp:123-132`,
`src/core/DiskMonitor.cpp:24-30,57-91`.

#### Atajos de teclado y gestos

- Ninguno específico. El campo de umbral acepta Enter (confirma y suelta el foco), Escape (descarta
  lo escrito y vuelve al valor guardado) y rueda del mouse (no verificado en el código leído, es un
  QSpinBox estándar sin botones visibles). Un click fuera del campo también confirma el valor
  escrito.

Evidencia: `src/ui/DiskCard.cpp:183-215`.

#### Textos de ayuda y tooltips

| Control | Tooltip |
|---|---|
| Campo de umbral | "Warn when free space is under this" |
| Botón "GB" | "Warn under an amount of free space" |
| Botón "%" | "Warn under a share of the drive" |
| Botón de quitar (X) | "Stop watching" |
| Botón de intervalo | "How often the drives are checked" |
| Nombre del disco (si se recorta) | el nombre completo del volumen |

Evidencia: `src/ui/DiskCard.cpp:137,152-153,159,314`.

#### Comportamientos configurables sin control visible

- `disks/checkMinutes` (.ini): intervalo del chequeo periódico, único para todos los discos.
- `disks/watched` (.ini, array): por disco vigilado — `root`, `value` (umbral numérico), `unit`
  ("GB" o "%"), `name` (último nombre visto, se usa para mostrarlo desconectado).
- Constantes fijas sin UI: umbral por defecto al agregar el primer disco = 50 GB; al pasar a "%" el
  valor arranca en 10%; máximos 100000 GB / 99%; el "GB" mostrado son GiB (1024³, como el Explorador
  de Windows, no GB decimal); repetición de aviso cada 6 horas exactas; primer chequeo que puede
  avisar a los 20 s de arrancar; duración de la notificación de bandeja 10 s.
- Detección de "disco local": Windows excluye red, CD/DVD y RAM disk (`GetDriveType`); mac excluye
  volúmenes de red y de solo lectura (una imagen .dmg montada no cuenta).
- Un disco vigilado con entrada ilegible en el .ini (editada a mano) se descarta solo, sin avisar en
  la UI (se loguea con `qWarning`).
- Recalcular el listado completo de discos locales ocurre solo al abrir el menú "Add drive..." (no
  en cada chequeo periódico, que solo relee los ya vigilados).

#### Diferencias por plataforma

- **Keycap e identidad del disco**: Windows muestra la letra de unidad ("C:"); mac muestra el nombre
  del volumen. Al estar desconectado, Windows sigue mostrando la letra tal cual; mac reconstruye un
  nombre a partir del último segmento de la ruta guardada si no hay nombre.
- **Qué cuenta como "disco local"**: reglas distintas de exclusión (ver arriba), una implementación
  por plataforma (`platform/win/LocalDrivesWin.cpp`, `platform/mac/LocalDrivesMac.cpp`) sobre una
  lectura común con `QStorageInfo`.

---

### Diferencias por plataforma (resumen general)

| Aspecto | Windows | macOS |
|---|---|---|
| Icono de la app | Bandeja (system tray) | Barra de menú (`LSUIElement`, sin ícono en el Dock) |
| Barra de título | Icono de app + título + Help + separador + Minimize + Close (todos a la derecha) | Semáforos Close/Minimize a la izquierda (sin semáforo verde: la ventana no se puede agrandar), sin icono de app, Help a la derecha |
| Inicio con la sesión | `HKCU\...\Run`, checkbox "Start with Windows" | `SMAppService.mainApp` (macOS 13+), checkbox "Open at login" |
| Updates | Sí (checkbox, botón, menú, todos los diálogos) | No existe (oculto en todos lados) |
| Permiso de Accesibilidad | No aplica (`needsAccessibilityPermission()` = false) | Obligatorio para `CGEventPost`; fila/estado especial y botón "Open Settings" |
| Detección de Nuke al frente | `SetWinEventHook(EVENT_SYSTEM_FOREGROUND)` + nombre del exe | `NSWorkspace` + API de Accesibilidad |
| Registro de atajo global | `RegisterHotKey` | `RegisterEventHotKey` (Carbon) |
| Inyección de clicks/teclas | `SendInput` / `SetCursorPos` | `CGEventPost` |
| Glifos de teclas | "Ctrl", "Alt", "Shift", "Win" | ⌃ ⌥ ⇧ ⌘ (orden Apple) |
| Discos: identidad | Letra de unidad | Nombre de volumen |
| Texto "en la bandeja/barra de menú" (Help) | "in the tray" | "in the menu bar" |

---

### Totales

**Nuke Shortcuts:**
- Controles/opciones: 7 filas de tabla (2 filas de atajo, 2 valores por defecto, botón lápiz, campo
  de grabación, botón Calibrate, miniatura, fila de permiso mac) — 9 elementos distintos.
- Estados visibles: 2 estados propios de tarjeta de estado + 2 chips de calibración + 2 estados de
  miniatura + 2 estados de fila (grabando/rechazada) + 3 estados de burbuja de calibración = 11.
- Entradas de menú de bandeja propias: 1 ("Calibrate Dope Sheet...", más el encabezado y
  pause/resume que son compartidos con General).
- Diálogos/ventanas secundarias: 2 (CalibrationDialog, CalibrationBubble; HelpDialog es compartido).
- Notificaciones: 1 ("Frame Dope Sheet" sin calibrar).
- Atajos de teclado/gestos: 2 acciones globales + reglas de grabación + reglas de calibración.

**Disk Space:**
- Controles/opciones: 15 elementos de tabla.
- Estados visibles: 3 chips de tarjeta + 2 estados de fila (conectado/desconectado) + estado de
  barra ámbar = 6.
- Entradas de menú de bandeja propias: 1 tipo (aviso por disco, dinámico) + 1 separador.
- Diálogos/ventanas secundarias: 2 menús (Add drive, intervalo).
- Notificaciones: 1 tipo ("X is running low").
- Atajos de teclado/gestos: 0 propios (Enter/Escape del campo de umbral).

**General de la app:**
- Controles/opciones: 13 elementos de tabla.
- Estados visibles: 4 estados de la tarjeta de estado + estado de icono de bandeja (nítido/atenuado).
- Entradas de menú de bandeja: 6 (encabezado, pause/resume, settings, calibrate, updates, quit) más
  separadores.
- Diálogos/ventanas secundarias: HelpDialog, UpdateDialog, QProgressDialog de descarga, 13 variantes
  de QMessageBox del updater = 16 ventanas/mensajes distintos.
- Notificaciones: 0 propias (las notificaciones de tray son de los módulos).
- Atajos de teclado/gestos: 0 (solo arrastrar la ventana).
- Tooltips: 6.

### Dudas

- **`AutoStart::availability().text`** ("Not available from a development build", "Needs macOS 13 or
  later") se arma en código pero, según lo leído, solo se usa en un log (`qInfo`) al primer arranque;
  no encontré dónde se le muestre al usuario en ninguna pantalla. Podría ser un texto pensado para
  una UI futura, o quedar reservado para depuración.
- **Rueda del mouse sobre el campo de umbral**: es un `QSpinBox` sin botones visibles
  (`NoButtons`), pero no confirmé en el código si Qt sigue respondiendo a la rueda del mouse para
  incrementar/decrementar (el comportamiento por defecto de `QAbstractSpinBox` lo permite salvo que
  se filtre el evento; no vi un filtro que lo bloquee, pero tampoco una prueba que lo confirme).
- **Versión monocroma del ícono para la barra de menú de macOS** ("modo template"): el comentario en
  `src/tray/TrayMenu.cpp:77` dice que está "pendiente en el roadmap"; hoy no existe, así que en mac
  usaría el ícono a color (sin confirmar cómo se ve realmente, no compilado en mac todavía).
  Relevante para el diseño nuevo: el ícono del módulo en macOS necesita esa variante.
  - Ver también `Docs/Doc_Roadmap.md` del repo para el estado exacto de este pendiente.
- **macOS sin compilar todavía**: todo lo de `src/platform/mac/*` y las diferencias de plataforma
  documentadas acá salen de LEER el código (comentarios y lógica), no de una app corriendo en mac.
  Podría haber detalles finos (tamaños exactos, comportamiento real del semáforo, etc.) que solo se
  verifican al compilar y correr en una Mac real.
- **"Open at login" en macOS**: no hay evidencia en el código de una fila/estado propio si
  `SMAppService` requiere aprobación extra del usuario en Ajustes del Sistema (como sí pasa en otras
  apps macOS modernas); el código solo expone `isEnabled()`/`setEnabled()` sin manejar ese caso
  intermedio explícitamente.
- **Comportamiento exacto de la rueda de scroll sobre los botones GB/%** (el segmentado) no está
  documentado ni parece tener soporte: son `QPushButton` checkables normales, sin lógica de rueda.

## LGA Open in NukeX

Fuente inspeccionada: `C:\Users\leg4-pc\.nuke\LGA_OpenInNukeX` (repo del cliente Qt/C++ y del
plugin Python). Todas las rutas de evidencia son relativas a esa raíz. No se compiló ni se
ejecutó nada; todo sale de leer el código, `README.md` y `docs/`.

La app tiene una única ventana (`ConfigWindow`), sin pestañas: tres bloques apilados (File
Association, Preferred Nuke Version, Nuke Bridge) más un pie de idioma/versión. Es bilingüe
(inglés/castellano) con una tabla propia en `i18n.cpp` — no usa el mecanismo `tr()` de Qt.

Se encontró además `QtClient/UserSettingsTab.cpp` (881 líneas): **no pertenece a esta app**.
Es un archivo de PipeSync (`#include "pipesync/UserSettingsTab.h"`) que quedó copiado en el
repo pero no está en `QtClient/CMakeLists.txt` ni se compila. Se excluye del inventario.

---

### Ventana de configuración — File Association

#### Controles y opciones

| Elemento | Tipo de control | Valores / rango / default | Texto exacto en la UI | Dónde se guarda | Plataforma | Evidencia archivo:línea |
|---|---|---|---|---|---|---|
| Título de sección | QLabel (no traducible, fijo) | — | "File Association" | — | Win/Mac | `QtClient/src/configwindow.cpp:332` |
| Descripción | QLabel, wordWrap | texto bilingüe fijo | EN: "Associate .nk files with OpenInNukeX to open them directly in your preferred NukeX version." / ES: "Asocia los archivos .nk con OpenInNukeX para abrirlos directamente en tu version preferida de NukeX." | — | Win/Mac | `QtClient/src/configwindow.cpp:343`, tabla `QtClient/src/i18n.cpp:19-22` |
| Botón principal | QPushButton, clase CSS "action" | texto cambia según estado (ver Estados) | EN "APPLY"/"RE-APPLY" · ES igual (no se traduce) | — | Win/Mac | `QtClient/src/configwindow.cpp:352`, `QtClient/src/i18n.cpp:36-37` |

#### Comportamiento
El botón hace lo siguiente según plataforma:
- **Windows**: registra `ProgID LGA.NukeScript.1` en `HKCU\Software\Classes`, las `Capabilities`
  en `HKCU\Software\OpenInNukeX\Capabilities` y `RegisteredApplications`, invoca
  `LGA_WinSetFTA.exe` (helper .NET 9) para fijar `UserChoiceLatest` (Win11) o escribe el hash
  `UserChoice` legado si el helper falta y `UserChoiceLatest` no está activo. `QtClient/src/winfileassociation.cpp:543-619`.
- **macOS**: registra el bundle con `lsregister`, y llama `NSWorkspace` (vía `macintegration.mm`)
  para pedir ser el handler por defecto de `.nk`; macOS muestra su propio cartel de confirmación
  del sistema (no editable por la app). `QtClient/src/configwindow.cpp:1241-1315`.

---

### Ventana de configuración — Preferred Nuke Version

#### Controles y opciones

| Elemento | Tipo de control | Valores / rango / default | Texto exacto en la UI | Dónde se guarda | Plataforma | Evidencia archivo:línea |
|---|---|---|---|---|---|---|
| Título de sección | QLabel fijo | — | "Preferred Nuke Version" | — | Win/Mac | `configwindow.cpp:375` |
| Descripción | QLabel, wordWrap | texto bilingüe | EN: "When no NukeX session is running, .nk files will open using this NukeX version." / ES: "Cuando no hay una sesion de NukeX abierta, los .nk se abren con esta version de NukeX." | — | Win/Mac | `configwindow.cpp:380`, `i18n.cpp:23` |
| Etiqueta "elegí una versión" | QLabel | condicional al resultado del escaneo | EN "Choose one of the found versions or browse your own:" / ES "Elegi una de las versiones encontradas o busca la tuya:" | — | Win/Mac | `configwindow.cpp:401`, `i18n.cpp:61-62` |
| Botón por versión encontrada | QPushButton dinámico, clase "version", alto 35px, ancho mín. 120px | uno por cada instalación de Nuke detectada; texto = `displayName` (ej. "Nuke 16.0v9") | dinámico según escaneo | — (solo carga el path en el campo) | Win/Mac | `configwindow.cpp:1487-1526` |
| Campo de ruta a NukeX | QLineEdit | placeholder; valor = último guardado o autocompletado | EN placeholder "Path to NukeX executable" / ES "Ruta al ejecutable de NukeX" | `nukeXpath.txt` en AppData/Application Support | Win/Mac | `configwindow.cpp:440-441`, `i18n.cpp:71` |
| Botón BROWSE | QPushButton, clase "secondary", alto 40px | abre selector de archivo | "BROWSE" (igual en los dos idiomas) | — | Win/Mac | `configwindow.cpp:444`, `i18n.cpp:38` |
| Botón SAVE | QPushButton, clase "action", alto 40px, ancho mín. 70px | guarda la ruta tipeada/elegida | "SAVE" | `nukeXpath.txt` | Win/Mac | `configwindow.cpp:451-454`, `i18n.cpp:39` |

#### Comportamientos configurables sin control visible
- **Escaneo automático al abrir la ventana**: sin botón que lo dispare, arranca solo apenas se
  crea `ConfigWindow`. `configwindow.cpp:180-182`, `nukescanner.cpp:14-22`.
  - Windows: recorre `C:\Program Files`, `C:\Program Files (x86)` y
    `C:\Program Files\Foundry` buscando subcarpetas `*Nuke*` con `Nuke*.exe`/`nuke*.exe`.
    `nukescanner.cpp:60-103`.
  - macOS: recorre `/Applications` buscando bundles `Nuke*.app` (directos o en subcarpeta),
    extrae el binario de `Contents/MacOS/` y descarta `.dylib`/`.so`/`.framework`/herramientas
    auxiliares (`README_Qt.md:143-144`).
  - En macOS, si el usuario elige un `.app` con BROWSE, la app resuelve sola el binario interno
    (`resolveNukeBinaryFromBundle`), sin pedir nada más. `configwindow.cpp:1228-1239`.
- **Auto-reparación de ruta muerta** (`healStalePath`): si la ruta guardada ya no existe cuando
  termina el escaneo, la reemplaza sola por la versión más nueva encontrada y la persiste — sin
  avisar al usuario con un cartel. `configwindow.cpp:1549-1574`.
- **Orden de versiones**: se comparan por `(major, minor, build)` parseado de "16.0v9"; lo que no
  matchea ese patrón pierde contra cualquier versión bien formada. `configwindow.cpp:1528-1541`.

#### Estados visibles

| Estado | Texto exacto | Cuándo aparece |
|---|---|---|
| Escaneando | EN "Scanning for installed Nuke versions…" / ES "Buscando versiones de Nuke instaladas…" (label en azul `#4A9EFF`, cursiva) | apenas arranca el escaneo, hasta que termina |
| Escaneando con path visible | EN "Scanning: %1" / ES "Buscando: %1" (path truncado a 50 caracteres con "..." adelante) | durante el escaneo, se actualiza por cada carpeta candidata |
| Encontradas N versiones | EN "%1 Nuke versions found:" / ES "%1 versiones de Nuke encontradas:" | al terminar el escaneo, si `N > 0` |
| Ninguna encontrada | EN "No Nuke installations found in common locations" / ES "No se encontro ninguna instalacion de Nuke en las ubicaciones habituales" (label en rojo `#FF6B6B`) | al terminar el escaneo, si `N == 0` |
| Botón APPLY vs RE-APPLY | "APPLY" si esta app todavía no es la asociada a `.nk`; "RE-APPLY" si ya lo es (rehace el registro) | se recalcula al construir la ventana y cada vez que la ventana vuelve a estar activa (`changeEvent`, `ActivationChange`) — por si el usuario cambió la asociación desde el Explorer/Finder mientras la ventana estaba atrás. `configwindow.cpp:909-935` |

Evidencia de textos: `i18n.cpp:63-68` (escaneo), `configwindow.cpp:1387-1485` (lógica de estados).

---

### Ventana de configuración — Nuke Bridge

#### Qué es
Componente Python opcional que corre dentro de NukeX (servidor TCP en :54325) para que un
`.nk` se abra en una sesión ya corriendo en vez de lanzar una nueva instancia. Se instala en
`<.nuke>/LGA_OpenInNukeX/` + una línea en el `init.py` de `.nuke`.

#### Controles y opciones

| Elemento | Tipo de control | Valores / rango / default | Texto exacto en la UI | Dónde se guarda | Plataforma | Evidencia archivo:línea |
|---|---|---|---|---|---|---|
| Título de sección | QLabel fijo | — | "Nuke Bridge" | — | Win/Mac | `configwindow.cpp:527` |
| Descripción | QLabel, wordWrap | texto bilingüe | EN: "Allows OpenInNukeX to detect a running NukeX session and open .nk files directly in it." / ES: "Permite que OpenInNukeX detecte una sesion de NukeX abierta y abra los .nk directamente ahi." | — | Win/Mac | `configwindow.cpp:539`, `i18n.cpp:25-27` |
| Chip de estado | QLabel con propiedad `state` (off/on/stale) | ver tabla de Estados | ver tabla de Estados | — (calculado en vivo) | Win/Mac | `configwindow.cpp:535-537, 693-753` |
| Campo carpeta `.nuke` | QLineEdit | placeholder + valor autodetectado o registrado | EN "Path to your .nuke folder" / ES "Ruta a tu carpeta .nuke" | Registro compartido LGA (`nuke.json`), ver más abajo | Win/Mac | `configwindow.cpp:550-551`, `i18n.cpp:72` |
| Botón BROWSE (bridge) | QPushButton, clase "secondary" | abre selector de carpeta, incluye ocultas | "BROWSE" | — | Win/Mac | `configwindow.cpp:553-555, 755-777` |
| Botón INSTALL/REINSTALL | QPushButton, clase "action" o "secondary" según estado | copia payload + appendea línea a `init.py` | EN "INSTALL"/"REINSTALL" · ES "INSTALAR"/"REINSTALAR" | ver Nuke Bridge más abajo | Win/Mac | `configwindow.cpp:557-560, 797-816`, `i18n.cpp:40-41` |
| Hint de carpeta | QLabel RichText | condicional | ver tabla de Estados | — | Win/Mac | `configwindow.cpp:567-571, 740-752` |
| Link/botón "Install manually" | QPushButton estilizado como link, clase "toggle" | despliega/colapsa panel manual | EN "Install manually instead…" / "Hide manual instructions" · ES "Instalar manualmente…" / "Ocultar las instrucciones" | — (solo estado de UI en memoria) | Win/Mac | `configwindow.cpp:577, 818-848`, `i18n.cpp:51-52` |
| Panel manual — paso 1 | QLabel numerado (RichText) | texto fijo | EN "Export the bridge files with the button below." / ES "Exporta los archivos del bridge con el boton de abajo." | — | Win/Mac | `configwindow.cpp:969`, `i18n.cpp:53-54` |
| Panel manual — paso 2 | QLabel numerado (RichText) | texto fijo, con `<code>` | EN "Copy the `LGA_OpenInNukeX` folder into your `.nuke` folder." / ES "Copia la carpeta `LGA_OpenInNukeX` dentro de tu carpeta `.nuke`." | — | Win/Mac | `configwindow.cpp:970`, `i18n.cpp:55-56` |
| Panel manual — paso 3 | QLabel numerado (RichText) | texto fijo, con `<code>` | EN "Add this line to the `init.py` inside `.nuke`:" / ES "Agrega esta linea al `init.py` que esta dentro de `.nuke`:" | — | Win/Mac | `configwindow.cpp:971`, `i18n.cpp:57-58` |
| Línea de código a copiar | QLabel seleccionable con mouse | fijo: `nuke.pluginAddPath('./LGA_OpenInNukeX')` | idéntico en los dos idiomas | — | Win/Mac | `configwindow.cpp:636`, `nukebridge.cpp:228-231` |
| Botón COPY LINE | QPushButton, clase "secondary", alto 34px, ancho mín. 120px | copia la línea de arriba al portapapeles; feedback temporal 1500 ms | EN "COPY LINE" → "COPIED" · ES "COPIAR LINEA" → "COPIADO" | — | Win/Mac | `configwindow.cpp:640, 872-887`, `i18n.cpp:43-44` |
| Botón EXPORT BRIDGE FILES | QPushButton, clase "action", alto 34px | abre selector de carpeta destino y copia el payload ahí | EN "EXPORT BRIDGE FILES…" / ES "EXPORTAR ARCHIVOS…" | copia física de `.py` + `VERSION` en la carpeta elegida | Win/Mac | `configwindow.cpp:655-657, 850-870`, `i18n.cpp:42` |

#### Estados visibles (chip + hint + botón)

| Estado | Chip (texto) | Color chip | Botón instalar | Hint debajo del campo | Cuándo |
|---|---|---|---|---|---|
| Sin instalar | EN "Not installed" / ES "Sin instalar" | gris `#9D9D9D` sobre `#2a2a2a` (`state=off`) | "INSTALL"/"INSTALAR" | según exista o no la carpeta (ver abajo) | no hay `<.nuke>/LGA_OpenInNukeX/` con archivos + línea activa en `init.py` |
| Instalado al día | EN "Installed · v%1" / ES "Instalado · v%1" | verde `#7CB868` sobre `#253320` (`state=on`) | "REINSTALL"/"REINSTALAR" (clase "secondary", ya no destacado) | oculto | versión instalada == versión que trae la app |
| Hay actualización | EN "Update available · v%1" / ES "Hay actualizacion · v%1" | amarillo `#D8C43A` sobre `#35300f` (`state=stale`) | "REINSTALL"/"REINSTALAR" (clase "action", destacado) | oculto | versión instalada distinta a la del bundle |
| Instalado, versión desconocida | EN "Installed · unknown version" / ES "Instalado · version desconocida" | amarillo (`state=stale`) | "REINSTALL"/"REINSTALAR" | oculto | carpeta instalada sin `VERSION` legible (bridge viejo, instalado a mano, o sin permiso de lectura) |
| Carpeta no encontrada | (chip = "Not installed") | — | "INSTALL" | EN "No Nuke folder found. Pick the one you use before installing." / ES "No encontramos la carpeta de Nuke. Elegi la que usas antes de instalar." | el campo no apunta a un directorio existente |
| Carpeta encontrada, sin bridge | (chip = "Not installed") | — | "INSTALL" | EN "Found your Nuke folder at **&lt;ruta&gt;**. Change it if you use a different one." / ES "Encontramos tu carpeta de Nuke en **&lt;ruta&gt;**. Cambiala si usas otra." | carpeta existe pero no tiene el bridge instalado |

Evidencia: `configwindow.cpp:693-753`, `dark_theme.qss:326-374`, `i18n.cpp:30-33, 47-50`.

#### Diálogos, popups y ventanas secundarias (Nuke Bridge)

- **INSTALL exitoso** — cartel `info()` (ícono Information, un botón OK marcado):
  - Título: EN "Nuke Bridge installed" / ES "Nuke Bridge instalado"
  - Mensaje: EN "The bridge is in place. **Restart NukeX** for it to start listening.<br>&lt;ruta coloreada&gt;" / ES "El bridge quedo instalado. **Reinicia NukeX** para que empiece a escuchar.<br>&lt;ruta&gt;"
  - `configwindow.cpp:797-810`, `i18n.cpp:89-91`
- **EXPORT exitoso** — cartel `info()`:
  - Título: EN "Bridge files exported" / ES "Archivos del bridge exportados"
  - Mensaje: EN "Follow the three steps with these files:<br>&lt;ruta&gt;" / ES "Segui los tres pasos con estos archivos:<br>&lt;ruta&gt;"
  - `configwindow.cpp:850-866`, `i18n.cpp:93-95`
- **INSTALL/EXPORT fallido** — cartel `error()` (ícono Critical), título en los dos casos EN "Could not install the Nuke Bridge" / ES "No se pudo instalar el Nuke Bridge" (mismo título se reusa para export). Mensaje según la causa (ver Mensajes de error). `configwindow.cpp:779-816, 858-870`, `i18n.cpp:92`.

---

### Pie de la ventana

#### Controles y opciones

| Elemento | Tipo de control | Valores / rango / default | Texto exacto | Dónde se guarda | Plataforma | Evidencia |
|---|---|---|---|---|---|---|
| Selector de idioma EN | QPushButton, clase "lang", 24×38px | alternativo con ES; marcado con propiedad `selected` | "EN" | `settings.ini` (`ui/language`) en AppData/Application Support | Win/Mac | `configwindow.cpp:477-509`, `appsettings.cpp:23-40` |
| Selector de idioma ES | QPushButton, clase "lang", 24×38px | alternativo con EN | "ES" | ídem | Win/Mac | ídem |
| Etiqueta de versión | QLabel, alineado a la derecha | fija, sale de la versión de CMake | `"v%1 | Lega"` (ej. "v1.83 | Lega") | — (compilado, no configurable) | Win/Mac | `configwindow.cpp:489-494` |

El cambio de idioma es en caliente: no hay reinicio, `retranslateUi()` reescribe todos los
textos visibles de la ventana en el acto. `configwindow.cpp:893-978`.

---

### Diálogos, popups y ventanas secundarias (genéricos)

Sistema propio (`Dialogs::ask/info/warn/error/show`), no usa `QMessageBox` de Qt directamente.
Todo diálogo de elección tiene: navegación con flechas/Tab, un botón "marcado" (violeta, con
glow) que ejecuta Enter, atajos de una letra subrayada, y Escape siempre cancela sin disparar
ningún botón por accidente. `QtClient/src/dialogs.cpp`, `QtClient/src/dialogstyle.h`,
`QtClient/src/dialogbutton.cpp`.

- **info() / warn() / error()**: un solo botón "&OK", con ícono Information/Warning/Critical
  respectivamente. Usados en todos los mensajes de la app (ver tablas de mensajes).
  `dialogs.cpp:476-484, 626-639`.
- El motor soporta además diálogos con varios botones, bloque de detalle técnico colapsable
  ("Show &Details"/"Hide &Details", en inglés siempre — es UI), texto seleccionable y botones de
  "Acción" que no cierran el diálogo — pero en esta app **no se usa ninguna de esas variantes**:
  todo mensaje real de OpenInNukeX es un solo OK. `dialogs.cpp:131-134, 261-338`.
- **Coloreado de rutas** (`Dialogs::colorizePath` / `colorizePathPair`): toda ruta mostrada en un
  cartel va en su propia línea, con cada segmento de carpeta en un color de una paleta fija
  (violeta para lo común, luego amarillo/verde-cian/naranja/azul/celeste en rotación). No hay
  ajuste de usuario sobre esto. `dialogs.cpp:430-474, 590-624`, `dialogstyle.h:34-54`.
- **Selector nativo "Open With" de Windows**: cuando el hash silencioso de asociación no alcanza,
  la app dispara el picker nativo de Windows (`IOpenWithLauncher`) o, si falla, la página
  `ms-settings:defaultapps`. Es UI del sistema operativo, no de la app. `winfileassociation.cpp:467-509`.
- **Cartel de confirmación de macOS** al pedir ser el handler por defecto de `.nk`: lo pone el
  sistema operativo (Launch Services), la app no controla su texto ni botones.
  `configwindow.cpp:1278-1298`.

---

### Mensajes de error y avisos

Todos con el sistema `Dialogs::info/warn/error` salvo que se indique lo contrario. Texto exacto
bilingüe (EN / ES) desde `i18n.cpp`.

#### File Association (guardar ruta / aplicar asociación)

| Situación | Título | Mensaje | Ícono | Evidencia |
|---|---|---|---|---|
| Campo de ruta vacío al SAVE o APPLY | EN "Warning" / ES "Atencion" | EN "Choose a NukeX version first." / ES "Elegi primero una version de NukeX." | Warning | `configwindow.cpp:1045-1050, 1107-1111`, `i18n.cpp:80-81` |
| El archivo elegido ya no existe | EN "Error" / ES "Error" | EN "That file no longer exists." / ES "Ese archivo ya no existe." | Critical | `configwindow.cpp:1053-1057, 1113-1117`, `i18n.cpp:79, 82` |
| El nombre del archivo no contiene "nuke" | EN "Warning" / ES "Atencion" | EN "That file does not look like a Nuke executable. Saving it anyway." / ES "Ese archivo no parece un ejecutable de Nuke. Igual se guarda." | Warning | `configwindow.cpp:1059-1064`, `i18n.cpp:83-84` |
| SAVE exitoso | EN "Nuke version saved" / ES "Version de Nuke guardada" | EN ".nk files will open with this NukeX build:<br>&lt;ruta coloreada&gt;" / ES "Los .nk se van a abrir con esta version de NukeX:<br>&lt;ruta&gt;" | Information | `configwindow.cpp:1081-1083`, `i18n.cpp:85-87` |
| APPLY exitoso (Windows, silencioso) | EN "Association completed" / ES "Asociacion completada" | EN "Double-clicking a .nk file now opens it with OpenInNukeX." / ES "Ahora al hacer doble click en un .nk se abre con OpenInNukeX." | Information | `configwindow.cpp:1127-1130`, `i18n.cpp:108-110` |
| APPLY con confirmación pendiente (Windows) | EN "One more step in Windows" / ES "Un paso mas en Windows" | EN "OpenInNukeX is registered. Choose it as the app for **.nk** files in the Windows dialog or in **Default apps**, then try a .nk file." / ES "OpenInNukeX ya quedo registrada. Elegila como app para archivos **.nk** en el cartel de Windows o en **Apps predeterminadas**, y despues proba un .nk." | Information | `configwindow.cpp:1131-1137`, `i18n.cpp:123-127` |
| APPLY con errores (Windows) | EN "Association finished with warnings" / ES "La asociacion termino con advertencias" | lista de errores técnicos concatenados con `<br>`, resaltados en blanco | Warning | `configwindow.cpp:1138-1146`, `i18n.cpp:118` |
| Excepción no controlada durante APPLY | EN "Error" / ES "Error" | texto crudo de la excepción (`e.what()`), sin traducir | Critical | `configwindow.cpp:1147-1151` |
| APPLY exitoso (macOS) | EN "Association completed" / ES "Asociacion completada" | igual que el caso Windows exitoso | Information | `configwindow.cpp:1304-1307`, `i18n.cpp:108-110` |
| APPLY rechazado o fallido en el cartel de macOS | EN "Almost done" / ES "Casi listo" | EN "The app is registered with your system, but macOS did not hand it the .nk files. To finish, right-click any .nk file in Finder, choose **Get Info**, pick OpenInNukeX under **Open with** and click **Change All**." / ES "La app quedo registrada en el sistema, pero macOS no le dio los .nk. Para terminar, boton derecho sobre cualquier .nk en Finder, **Get Info**, elegi OpenInNukeX en **Open with** y toca **Change All**." | Information | `configwindow.cpp:1309-1314`, `i18n.cpp:111-117` |
| APPLY cancelado: la app corre desde una carpeta de desarrollo (macOS) | EN "Warning" / ES "Atencion" | EN "This copy is running from a build folder, so associating .nk files with it would break as soon as that folder is rebuilt. Run the installed copy of OpenInNukeX instead." / ES "Esta copia corre desde una carpeta de compilacion, asi que asociarle los .nk se rompe en cuanto esa carpeta se regenere. Abri la copia instalada de OpenInNukeX y hacelo desde ahi." | Warning | `configwindow.cpp:1252-1256`, `i18n.cpp:119-122` |

#### Nuke Bridge — errores de instalar / exportar

Un motor común (`NukeBridge::Error`) alimenta ambos flujos (INSTALL y EXPORT). Título de error
siempre EN "Could not install the Nuke Bridge" / ES "No se pudo instalar el Nuke Bridge"
(se reutiliza también para el fallo de export). `configwindow.cpp:783-793`, `i18n.cpp:92`.

| Código de error | Mensaje mostrado al usuario | Cuándo | Evidencia |
|---|---|---|---|
| `DirMissing` | EN "That folder does not exist." / ES "Esa carpeta no existe." | la carpeta `.nuke` (o destino de export) tipeada no existe | `nukebridge.cpp:289-293`, `i18n.cpp:100` |
| `SourceRepo` | EN "That folder is the OpenInNukeX source repository, not a `.nuke` folder. Installing there would overwrite the source files." / ES "Esa carpeta es el repositorio de OpenInNukeX, no una carpeta `.nuke`. Instalar ahi pisaria los archivos fuente." | la carpeta destino contiene `QtClient/CMakeLists.txt` (es el repo, no una `.nuke` instalada) | `nukebridge.cpp:296-299`, `i18n.cpp:96-99` |
| `PayloadMissing` | EN "This copy of OpenInNukeX does not carry the bridge files. The build is incomplete — download the app again." / ES "Esta copia de OpenInNukeX no trae los archivos del bridge. El artefacto quedo incompleto: volve a descargar la app." | el ejecutable no trae `init.py`/`LGA_QtAdapter_OpenInNukeX.py` embebidos (build incompleto) | `nukebridge.cpp:304-307`, `i18n.cpp:101-104` |
| `WriteFailed` | EN "Could not write to that folder. Check that you have permission on it." / ES "No se pudo escribir en esa carpeta. Fijate que tengas permiso sobre ella." | no se pudo crear la carpeta, copiar los `.py`, escribir `VERSION` o tocar `init.py` | `nukebridge.cpp:308-317`, `i18n.cpp:105-106` |

El detalle técnico (rutas exactas) de cada fallo **no se muestra al usuario**: va solo al log
con `Logger::logError`. `configwindow.cpp:812-814, 868`.

#### NukeOpener (cliente de línea de comandos / doble clic en un `.nk`)

Estos mensajes salen con `QMessageBox::information` directo (no con el sistema `Dialogs::`), y
**no están traducidos** — siempre en castellano o inglés mezclado según el caso, sin pasar por
`i18n`. Aparecen cuando el usuario ya hizo doble clic en un `.nk` y algo falla antes de que
NukeX se abra.

| Mensaje | Texto exacto | Cuándo | Evidencia |
|---|---|---|---|
| Config no encontrada | "Error: Archivo de configuración de Nuke no encontrado. Ejecuta la aplicación sin argumentos para configurar la ruta. %1" (%1 = error de Qt) | no existe `nukeXpath.txt` | `nukeopener.cpp:38-48` |
| Config vacía | "Error: El archivo de configuración está vacío. Ejecuta la aplicación sin argumentos para configurar la ruta." | `nukeXpath.txt` existe pero está vacío | `nukeopener.cpp:54-57` |
| Ejecutable de NukeX inexistente | "Error: El ejecutable de NukeX no existe: %1" | la ruta guardada ya no apunta a un archivo real | `nukeopener.cpp:69-73` |
| Fallo al lanzar NukeX (Windows) | "Error al abrir Nuke con el archivo. Código de error: %1" (código de `GetLastError`) | `CreateProcessW` falla | `nukeopener.cpp:108-112` |
| Fallo al lanzar NukeX (macOS/Unix) | "Error al abrir Nuke con el archivo: %1" (mensaje de Qt) | `startDetached` falla | `nukeopener.cpp:118-121` |
| Ruta de NukeX no configurada, tras timeout o rechazo de conexión | "Nuke path file not found or cannot be read." (en inglés, sin traducir) | no se pudo leer una ruta válida al intentar levantar una instancia nueva | `nukeopener.cpp:242-244, 356-358` |

#### Cartel "NukeX Launcher" (no modal, con cuenta regresiva)

Aparece cuando NO hay una instancia de NukeX corriendo y la app lanza una nueva. Es opcional:
solo se muestra si `ShowNewNukeMsg` está habilitado (`setShowNewNukeMsg`), que **no tiene
ningún control en la UI de esta versión** — queda en `false` por default y nadie lo cambia
desde la ventana de configuración; solo existe el setter en el código.

- Título de ventana: "NukeX Launcher"
- Ícono: Information
- Texto: "No NukeX instance found, opening a new one..." (sin traducir, siempre en inglés)
- Botón deshabilitado con cuenta regresiva: "Closing in 3 seconds" → "Closing in 2 seconds" →
  "Closing in 1 second", se autocierra a los 3 segundos
- No modal: no bloquea la apertura de NukeX
- Evidencia: `nukeopener.cpp:267-334`, `nukeopener.h:19-20, 40`

---

### Textos de ayuda y tooltips

No se encontró ningún `setToolTip()` en el cliente Qt/C++ (`configwindow.cpp`, `dialogs.cpp`,
`dialogbutton.cpp`, `nukebridge.cpp`, `winfileassociation.cpp`, `nukeopener.cpp`). La única
"ayuda" visible son los hints de texto bajo el campo de carpeta `.nuke` del Nuke Bridge (ver
tabla de Estados más arriba) y los tres pasos numerados del panel manual. No hay tooltips de
hover en botones ni campos.

---

### Comportamientos configurables sin control visible

- **Idioma persistido**: `settings.ini` → `[ui] language=en|es`, en
  `%AppData%/LGA/OpenInNukeX/` (Win) o `~/Library/Application Support/LGA/OpenInNukeX/` (Mac).
  Sin control visible más allá de los botones EN/ES del pie (no hay preferencia de "seguir el
  idioma del sistema"). `appsettings.cpp:14-21`.
- **Ruta de NukeX**: `nukeXpath.txt` en el mismo directorio. Es lo que también consulta PipeSync
  (mencionado en comentarios de `appsettings.h:14-15`).
- **Carpeta `.nuke` del Nuke Bridge**: no se guarda en el INI propio — se publica en el registro
  compartido de LGA (`~/Library/Application Support/LGA/nuke.json` en Mac,
  `%APPDATA%/LGA/nuke.json` en Windows), formato `{"nukeDir": "...", "updatedAt": "..."}`. Lo
  leen también otras apps LGA. `lgaregistry.h:17-27`, `nukebridge.cpp:245-252`.
- **Alto de la ventana**: se autoajusta al contenido (hasta el 80% del alto útil de la pantalla)
  cada vez que cambia el escaneo o se abre/cierra el panel manual, salvo que el usuario ya haya
  redimensionado la ventana a mano — desde ese momento la app deja de imponerle el alto para
  siempre en esa sesión. No hay ningún control visible para esto. `configwindow.cpp:1598-1715`.
- **Centrado de la ventana**: se recentra en pantalla cada vez que cambia el alto, salvo que el
  usuario ya la haya movido a mano. `configwindow.cpp:240-268`.
- **Barra de título oscura (Windows)**: forzada vía `DwmSetWindowAttribute`, independiente del
  tema claro/oscuro del sistema operativo. Sin control de usuario. `configwindow.cpp:72-96`.
- **Reconsulta de la asociación al volver al frente**: cada vez que la ventana recupera el foco,
  se relee si `.nk` sigue asociado a esta app (por si el usuario lo cambió desde el Explorer/
  Finder mientras tanto) y se actualiza el texto APPLY/RE-APPLY. `configwindow.cpp:927-935`.
- **Logs**: se borran y rehacen en cada arranque de la app
  (`%AppData%/LGA/OpenInNukeX/OpenInNukeX.log` o el equivalente en Mac). Sin control de usuario
  (no hay nivel de log configurable desde la UI). `main.cpp:46`, `README_Qt.md:148-153`.
- **Auto-registro en el registro compartido de LGA**: en cada arranque, sin control de usuario,
  salvo que la app corra desde una carpeta de desarrollo (`build`/`deploy`/dentro del repo), caso
  en el que se salta el registro sin avisar. `main.cpp:49-54`, `lgaregistry.h:56-66`.
- **Mensaje "NukeX Launcher" con cuenta regresiva**: existe un flag interno
  `ShowNewNukeMsg`/`setShowNewNukeMsg()` que lo habilita, pero no hay ningún checkbox ni opción
  en la ventana de configuración que lo controle; en el estado actual del código queda siempre
  en `false`. `nukeopener.h:19-20, 40`.

---

### Plugin Python dentro de Nuke (`init.py`) — **plugin, no va al módulo**

Corre como servidor TCP en el puerto 54325 **solo si la sesión es NukeX** (`nuke.env["nukex"]`
y no `studio`). No registra ningún menú, panel, ítem de toolbar ni atajo dentro de Nuke: es
puramente un servidor en segundo plano. La única salida visible desde Nuke son dos `print()`
a la consola de script de Nuke (no un `nuke.message()`):

- "OpenInNukeX active on port 54325" — al arrancar el servidor con éxito
- "OpenInNukeX inactive. This is not NukeX." — cuando la sesión no es NukeX

Comandos TCP que atiende (no son UI, pero determinan lo que el usuario ve pasar en su sesión de
Nuke):
- `ping` → responde `pong`
- `run_script||<path>` → cierra el script actual (`nuke.scriptClose()`, dispara el cartel nativo
  de Nuke de "guardar cambios" si hay algo sin guardar) y abre el nuevo con `nuke.scriptOpen()`
- `paste_clipboard` → **marcado como plugin, no va al módulo**: pega el clipboard
  (`nuke.nodePaste("%clipboard%")`), arma un Contact Sheet (`LGA_ContactSheet.nk`, buscado en
  `~/.nuke/LGA_NodePack/LGizmos/Other/LGA_ContactSheet.nk`), conecta los `Read` pegados a sus
  entradas y el primer Viewer existente. No hay UI propia: si falla (no hay Reads, falta el
  toolset del Contact Sheet, o no se creó el grupo) lanza una excepción de Python que vuelve como
  texto de error por el socket, sin ningún cartel dentro de Nuke.

Evidencia: `init.py:423-947`, en especial `init.py:552-637` (paste_clipboard) y
`init.py:894-943` (servidor).

Sin logging visible al usuario: todo el logging de `init.py` (muy detallado, con snapshots de
entorno, callbacks, memoria) va a `logs/debugPy_OpenInNukeX.log` dentro de la carpeta del
plugin, y no se expone en ninguna UI.

---

### Diferencias por plataforma

| Aspecto | Windows | macOS |
|---|---|---|
| Asociación de `.nk` | ProgID en registro + helper externo `LGA_WinSetFTA.exe` (.NET 9) para `UserChoiceLatest`; puede requerir picker nativo o `ms-settings:defaultapps` | Launch Services (`lsregister` + `NSWorkspace`); cartel de confirmación nativo del sistema, no evitable |
| Botón BROWSE (ejecutable NukeX) | filtra `*.exe`, arranca en `C:/Program Files/` | acepta `.app` o binario suelto, arranca en `/Applications`; si es `.app` resuelve el binario interno solo |
| Escaneo de versiones | `C:\Program Files`, `C:\Program Files (x86)`, `C:\Program Files\Foundry`, subcarpetas `*Nuke*` con `.exe` | `/Applications`, bundles `Nuke*.app` |
| Barra de título oscura | forzada por DWM | no aplica (tema nativo de macOS) |
| Apertura de nueva instancia | `CreateProcessW` con `CREATE_NEW_CONSOLE` | `QProcess::startDetached` |
| Mensaje de error si falla el lanzamiento | incluye código de `GetLastError` | incluye `errorString()` de Qt |
| Cancelación por app en carpeta de desarrollo | no se chequea explícitamente para APPLY (solo aplica el guard de `LgaRegistry` para el registro general) | chequeo explícito antes de asociar (`LgaRegistry::runsFromDevTree()`), con cartel propio |
| Ícono de archivo asociado | `app_icon.ico` | icono del bundle (`.icns`/`Assets.car`) |

---

### Totales

- **Controles interactivos** (botones, campos de texto, botones dinámicos de versión no
  contados individualmente): 15 controles fijos (APPLY, campo ruta NukeX, BROWSE, SAVE, campo
  carpeta `.nuke`, BROWSE bridge, INSTALL, toggle manual, COPY LINE, EXPORT, EN, ES) + 1 tipo de
  control dinámico (botón por versión de Nuke encontrada, cantidad variable).
- **Estados visibles distintos**: 10 (2 del botón APPLY/RE-APPLY, 5 del escáner de versiones,
  4 del chip del Nuke Bridge + 2 variantes de hint, contados por separado en sus tablas).
- **Diálogos/popups**: 1 sistema genérico de diálogo de elección (`Dialogs::ask`) con builder
  reutilizable, más el cartel específico "NukeX Launcher" con cuenta regresiva (`QMessageBox`
  directo), más 2 diálogos del sistema operativo (picker de Windows, confirmación de macOS) que
  la app dispara pero no controla el contenido.
- **Mensajes de error/aviso distintos** (con texto propio, contando EN+ES como un mismo mensaje):
  10 de File Association/ruta de NukeX + 4 de Nuke Bridge + 6 de NukeOpener/cliente (sin
  traducir) = **20 mensajes**.
- **Entradas de la tabla de idiomas** (`I18n::Str`): 57, cubriendo descripciones, chips, botones,
  hints, escáner, placeholders, títulos de file-dialog y todos los carteles bilingües.

### Dudas

1. **`ShowNewNukeMsg` sin control de UI**: el flag que habilita el cartel "NukeX Launcher" con
   cuenta regresiva existe en `NukeOpener` pero no hay ningún checkbox en `ConfigWindow` que lo
   toque. No se pudo confirmar si en algún momento existió ese control y se quitó, o si es un
   flag que sólo se usa para pruebas/debug. Si el diseño nuevo debe incluir un interruptor para
   este cartel, hay que decidir default y texto.
2. **Mensajes de `NukeOpener`** (`nukeopener.cpp`) no pasan por el sistema `i18n` ni por
   `Dialogs::`: usan `QMessageBox::information` directo y quedan fijos en el idioma en que se
   escribieron (mezcla de castellano e inglés). Al portar al módulo de Mighty Tools, hay que
   decidir si se traducen y homologan al mismo sistema de diálogos que el resto de la ventana.
3. **`paste_clipboard` / Contact Sheet**: no tiene ninguna superficie de configuración (ni ruta
   del `.nk` del Contact Sheet, ni on/off) — está totalmente hardcodeado en `init.py`. Se marcó
   como "plugin, no va al módulo" según el encargo, pero si en algún momento se quiere exponer
   como función del módulo, hoy no hay nada configurable que migrar más que la ruta fija del
   toolset.
4. **Instalador de Windows (`installer.iss`)**: no se inspeccionaron a fondo las pantallas del
   asistente de Inno Setup (son las por defecto del framework, sin texto custom encontrado más
   allá de un `assoc .nk=` en la desinstalación). Si el inventario tiene que cubrir también la
   experiencia de instalación/desinstalación como parte de "lo que el usuario ve", falta un
   paso de revisión específico del `.iss` y del instalador de macOS (`create_dmg.sh`), que no
   se abrieron en este relevamiento.
5. **`UserSettingsTab.cpp`**: confirmado que es un archivo de PipeSync y no de esta app (no está
   en `CMakeLists.txt`), así que se excluyó. Si aparece en otra revisión y alguien lo toma como
   parte de OpenInNukeX por error, aclarar que es un remanente de otro repo copiado por
   accidente.

## LGA FolderSwitch

Inventario exhaustivo de todo lo que el usuario ve o puede configurar, relevado leyendo el código
fuente (no el README). Versión relevada: v0.10 (`CMakeLists.txt:2`). App Qt/C++, solo Windows.

---

### Módulo Folder Switch

Cambio automático de carpeta en diálogos Abrir/Guardar, sus dos atajos y el popup de recientes.
Vive sobre todo en `src/ui/MainWindow.cpp` (tarjetas 1 y 2), `src/tray/TrayController.cpp` (la
lógica de detección/inyección) y `src/ui/RecentFoldersPopup.cpp`.

#### Controles y opciones

| Elemento | Tipo de control | Valores / rango / default | Texto exacto en la UI (literal) | Dónde se guarda (clave del .ini) | Evidencia archivo:línea |
|---|---|---|---|---|---|
| Interruptor on/pausa | Botón (toggle) | on / paused, default **on** | `Pause` (cuando está on) / `Resume` (cuando está paused) | `enabled` (bool, default `true`) | `src/ui/MainWindow.cpp:124-127,265-267,284`; `src/core/AppState.cpp:29-38` |
| Cambio automático | Checkbox | on/off, default **on** | `Switch automatically` | `autoSwitch` (bool, default `true`) | `src/ui/MainWindow.cpp:131`; `src/core/AppState.cpp:40-49` |
| Atajo manual (fila, sin control clickeable) | 3 chips de tecla + label | fijo, no configurable | `Manual shortcut` con teclas `Ctrl` `Alt` `O` | no aplica (no editable) | `src/ui/MainWindow.cpp:140-166` |
| Atajo de recientes (fila, sin control clickeable) | 3+1 chips de tecla + label | fijo, no configurable | `Recent folders` con teclas `Ctrl` `Alt` `Shift` `O` | no aplica (no editable) | `src/ui/MainWindow.cpp:168-174` |

Nota: **no hay control visible** para el número máximo de carpetas recientes, ni para el delay de
inyección, ni para qué gestores de archivos se reconocen — ver "Comportamientos configurables sin
control visible" más abajo.

#### Estados visibles

**Tarjeta "cambio de carpeta" (punto + título + subtítulo):**

| Estado | Punto (`statusDot`) | Título (`cardTitle`) | Subtítulo (`caption`) | Cuándo aparece |
|---|---|---|---|---|
| On | verde (`state="on"`, color `@ok`) | `Switching is on` | `Dialogs jump to the last folder you used.` | `enabled = true` |
| Paused, con algún atajo vivo | gris (`state` sin `"on"`, color `@textFaint`) | `Switching is paused` | `Only the shortcuts work while paused.` | `enabled = false` y (`hotkeyRegistered` o `recentHotkeyRegistered`) |
| Paused, sin atajos | gris | `Switching is paused` | `Dialogs keep their own folder.` | `enabled = false` y ningún atajo registrado |

Evidencia: `src/ui/MainWindow.cpp:279-284`.

**Tarjeta "Last folder" — chips de origen y resultado, visibles solo si hubo un cambio (`lastSwitch.isValid()`):**

| Chip / campo | Tono | Texto | Cuándo |
|---|---|---|---|
| Origen | `src` (borde gris, sin relleno) | `Explorer` / `XYplorer` / `Recent` | según de dónde vino la carpeta (Explorer, XYplorer, o elegida en el popup de recientes) |
| Resultado | `ok` (verde) | `Applied` | la inyección al diálogo funcionó |
| Resultado | `err` (rojo) | `Not applied` | la inyección falló |
| Hora | label simple | `HH:mm` | siempre que hay `lastSwitch` |
| Valor de carpeta | `ElidedLabel`, elide en el medio | la ruta completa (tooltip = ruta completa) | siempre que hay `lastSwitch`; si no, `No folder yet` (sin tooltip, propiedad `empty=true`) |
| Caption bajo el campo, tono `err` | — | `The dialog didn't take it. Retry with Ctrl+Alt+O.` (si el atajo manual sigue registrado) / `The dialog didn't take it. Pick the folder by hand.` (si no) | solo si `applied = false` |
| Caption vacío | — | `Open a folder in Explorer, then go to a file dialog.` | cuando todavía no hubo ningún switch (`No folder yet`) |

Evidencia: `src/ui/MainWindow.cpp:287-309`.

**Chips "In use" junto a cada atajo (Ctrl+Alt+O y Ctrl+Alt+Shift+O):**

| Chip | Tono | Texto | Cuándo |
|---|---|---|---|
| Atajo manual ocupado | `err` | `In use` | otra app ya registró Ctrl+Alt+O (`hotkeyRegistered = false`) |
| Atajo de recientes ocupado | `err` | `In use` | otra app ya registró Ctrl+Alt+Shift+O (`recentHotkeyRegistered = false`) |

Caption bajo cada atajo:
- Manual OK: `Press it inside a file dialog to jump right away.`
- Manual ocupado (tono `err`): `Another app took it. Automatic switching isn't affected.`
- Recientes OK: `Press it inside a file dialog to pick a recent folder.`
- Recientes ocupado (tono `err`): `Another app took it. The other shortcut isn't affected.`

Evidencia: `src/ui/MainWindow.cpp:320-327`.

**Estados de captura QA (`src/qa/UiShot.cpp:45-62`, lista `kStates`) que confirman todos los
estados anteriores existen como estados dibujables**: `on`, `empty`, `paused`, `failed`,
`hotkey-busy`, `failed-hotkey-busy` (más los de ayuda/tray/updates, listados en la sección
"General de la app").

#### Menú de la bandeja

Ver "General de la app" — el menú es compartido por toda la app, no es específico de este módulo
(aunque incluye el toggle on/pausa).

#### Diálogos, popups y ventanas secundarias

**Popup de carpetas recientes** (`src/ui/RecentFoldersPopup.cpp`), abre con Ctrl+Alt+Shift+O junto
al puntero, sin marco de Windows, con sombra propia dibujada a mano:

- Encabezado: `Recent folders` a la izquierda; a la derecha, con los números en violeta (`@accent`):
  - Con 2 o más carpetas: `Press ` + `1` (en negrita/violeta) + `–` + el número de la última carpeta (ej. `5`).
  - Con exactamente 1 carpeta: `Press ` + `1`.
  - Con 0 carpetas: no se dibuja el texto de teclas.
- Fila por carpeta (hasta 5, orden más reciente primero): badge numerado 1-5 en caja violeta
  (`#443a91`, hover `#52 43 a8`), nombre de la carpeta (elide en el medio) y, debajo, la carpeta
  contenedora (elide por la izquierda). Fila resaltada (`#2a2a2a`) al pasar el mouse o con
  teclado/flechas.
- **Estado vacío** (`m_folders.isEmpty()`): ícono de carpeta atenuado, título `No folders yet` y
  texto `Open a folder in Explorer or XYplorer, then come back to this dialog.`
- Se cierra sin elegir con Esc o con un click fuera de la tarjeta (incluida la zona de sombra).

Evidencia: `src/ui/RecentFoldersPopup.cpp:41-42,265-313`.

#### Notificaciones y avisos

- Ninguna específica de este módulo (los mensajes de "no se pudo aplicar" son captions en la
  tarjeta, no notificaciones aparte). Las notificaciones del sistema (tray balloon) están en
  "General de la app".

#### Atajos de teclado y gestos

| Atajo / gesto | Acción | Dónde funciona |
|---|---|---|
| `Ctrl+Alt+O` | Aplica la última carpeta guardada (de Explorer/XYplorer) al diálogo Abrir/Guardar en primer plano | Global, solo actúa si el foreground es un file dialog (Win32 o Qt) |
| `Ctrl+Alt+Shift+O` (con `MOD_NOREPEAT`) | Abre el popup de carpetas recientes en la posición del mouse | Global, solo actúa si el foreground es un file dialog |
| Dentro del popup: `1`–`9` | Elige la carpeta de esa fila (solo hay hasta 5 filas reales) | Popup de recientes |
| Dentro del popup: flecha ↑ / ↓ | Mueve el resaltado entre filas (circular) | Popup de recientes |
| Dentro del popup: `Enter` / `Return` | Elige la fila resaltada | Popup de recientes |
| Dentro del popup: `Esc` | Cierra sin elegir | Popup de recientes |
| Click en una fila del popup | Elige esa carpeta | Popup de recientes |
| Click fuera de la tarjeta (incluida la sombra) | Cierra sin elegir | Popup de recientes |
| Mouse sobre una fila | Resalta esa fila | Popup de recientes |

Ambos atajos funcionan **también con el switching en pausa** (no dependen de `enabled` ni de
`autoSwitch`; `Ctrl+Alt+O` solo requiere que haya un manager guardado y `Ctrl+Alt+Shift+O` no
depende de nada del estado de switching). Evidencia: `src/core/HotkeyFilter.cpp:12-27`;
`src/tray/TrayController.cpp:316-362`.

#### Textos de ayuda y tooltips (literales)

- Tooltip del valor de "Last folder": la ruta completa (solo cuando hay una carpeta; vacío en el
  estado "No folder yet"). `src/ui/MainWindow.cpp:293,302`.
- El resto de la ayuda de este módulo (los 3 pasos "How it works") está en el `HelpDialog`, ver
  "General de la app".

#### Comportamientos configurables sin control visible

| Comportamiento | Valor | Evidencia |
|---|---|---|
| Máximo de carpetas recientes guardadas | 5 (`kMaxRecentFolders`) | `src/core/AppState.h:45` |
| Delay antes de inyectar el path en el diálogo (auto-switch y atajo manual) | 200 ms (`kSwitchDelayMs`) | `src/tray/TrayController.cpp:34` |
| Frescura del "último manager visto" para que el auto-switch dispare | 60 000 ms (`kManagerFreshnessMs`) | `src/tray/TrayController.cpp:33` |
| Gestores de archivos reconocidos | Explorer (clase `CabinetWClass`) y XYplorer (clase `ThunderRT6FormDC` o proceso `xyplorer.exe`) | `src/core/WindowUtils.h:12-17` |
| Diálogos reconocidos como "file dialog" | Win32 clase `#32770` con hijo `DUIViewWndClassName`/`SHELLDLL_DefView` o `ComboBoxEx32`; o diálogo Qt puro (clase que empieza con `Qt`, con `owner`, y por UI Automation al menos un campo de texto y un botón Open/Save/Choose/Select/Abrir/Guardar) | `src/core/WindowUtils.h:22-30` |
| Deduplicado de carpetas recientes | Case-insensitive y sin importar la barra final; mover al tope si ya estaba en la lista | `src/core/AppState.cpp:80-110` |
| El último switch (`lastSwitch`) NO persiste entre reinicios | vive solo en memoria de `AppState`, se resetea al cerrar la app | `src/core/AppState.cpp:7-18` (no lee `lastSwitch` de settings) |
| El estado de "atajo registrado" NO persiste | se recalcula en cada arranque al llamar `RegisterHotKey` | `src/core/HotkeyFilter.cpp:12-27` |
| Historial de carpeta al salir de un manager | si el usuario cierra la ventana del manager con la X (no cambia a otra app), esa carpeta NO entra a recientes (limitación conocida, en el roadmap) | `Docs/Doc_Roadmap.md:16-19` |

---

### General de la app

Inicio con el sistema, actualizaciones, versión, ayuda, barra de título, bandeja. Sobre todo en
`src/ui/TitleBar.cpp`, `src/tray/TrayController.cpp`, `src/tray/TrayMenu.cpp`,
`src/updates/UpdateService.cpp`, `src/updates/UpdateDialog.cpp`, `src/ui/HelpDialog.cpp`,
`src/windows/AutoStart.cpp`.

#### Controles y opciones

| Elemento | Tipo de control | Valores / rango / default | Texto exacto en la UI (literal) | Dónde se guarda (clave del .ini) | Evidencia archivo:línea |
|---|---|---|---|---|---|
| Inicio con Windows | Checkbox | on/off; solo habilitado y con efecto real desde una copia INSTALADA (ver abajo) | `Start with Windows` | Registro `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, valor `LGA_FolderSwitch` (NO en el .ini) | `src/ui/MainWindow.cpp:238`; `src/windows/AutoStart.cpp:10-11,72-87` |
| Chequear updates al arrancar | Checkbox | on/off, default **on** | `Check for updates at startup` | `checkUpdatesAtStartup` (bool, default `true`) | `src/ui/MainWindow.cpp:247`; `src/core/AppState.cpp:51-60` |
| Chequear ahora | Botón (tamaño `sm`) | dispara un chequeo inmediato, siempre da alguna respuesta (diálogo) | `Check now` | no aplica | `src/ui/MainWindow.cpp:249`; `src/updates/UpdateService.cpp:189-215` |
| Versión instalada (texto informativo) | Caption, no editable | — | `Installed version: v0.10` (versión leída de `FOLDERSWITCH_VERSION`) | no aplica | `src/ui/MainWindow.cpp:253` |
| Botón de ayuda (barra de título) | Botón icono (`?`) | abre `HelpDialog` | tooltip `Help` | no aplica | `src/ui/TitleBar.cpp:105` |
| Botón minimizar (barra de título) | Botón icono | minimiza la ventana | tooltip `Minimize` | no aplica | `src/ui/TitleBar.cpp:115-116` |
| Botón cerrar (barra de título) | Botón icono | oculta la ventana a la bandeja (no cierra la app) | tooltip `Close` | no aplica | `src/ui/TitleBar.cpp:118-119`; `src/ui/MainWindow.cpp:434-439` |
| Arrastrar la barra de título | Gesto de mouse | mueve la ventana (`startSystemMove`) | no aplica | no aplica | `src/ui/TitleBar.cpp:134-143` |

#### Estados visibles

**Checkbox "Start with Windows" — tooltip según disponibilidad:**

| Estado | Tooltip |
|---|---|
| Copia instalada (puede activarse) | `Start LGA FolderSwitch when you sign in to Windows` |
| Copia de desarrollo (`build/`, `deploy/`, o dentro del árbol de build/fuente) | `Registers THIS development copy to start when you sign in` |

Evidencia: `src/ui/MainWindow.cpp:353-357`; `src/windows/AutoStart.cpp:89-121`.

**Ícono de la bandeja** (`src/tray/TrayMenu.cpp:29-52`):

| Estado | Apariencia |
|---|---|
| Activo (`enabled = true`) | Ícono a color (desregistro CMY de la marca), un PNG por tamaño (16/20/24/32/40/48 px), se ve igual sobre barra clara u oscura |
| Pausado (`enabled = false`) | Silueta monocroma (blanco sobre barra oscura, negro sobre barra clara) atenuada al 40% de opacidad |

Tooltip del ícono de bandeja: `LGA FolderSwitch` (activo) / `LGA FolderSwitch — paused` (pausado).
Evidencia: `src/tray/TrayController.cpp:126-138`.

**Estados de captura QA relacionados con esta sección** (`src/qa/UiShot.cpp:45-62`): `help`,
`hover-help`, `hover-close`, `help-link-hover`, `tray-menu`, `recent-menu`, `recent-menu-empty`,
`update-dialog`.

#### Menú de la bandeja

Clic derecho (o el menú contextual que Qt asocia al ícono) muestra, de arriba a abajo
(`src/tray/TrayMenu.cpp:7-27`):

1. Encabezado, deshabilitado (solo informa): `FolderSwitch · On` / `FolderSwitch · Paused`.
2. `Pause switching` (si está on) / `Resume switching` (si está pausado) — alterna `enabled`.
3. — separador —
4. `Settings...` — abre/trae al frente la ventana principal.
5. `Check for Updates...` — chequeo manual (siempre con diálogo, incluso con la ventana oculta).
6. — separador —
7. `Quit` — cierra la app de verdad (`qApp->quit()`).

Doble click o click simple (`Trigger`) sobre el ícono de la bandeja: abre/trae al frente la
ventana de Settings (`src/tray/TrayController.cpp:177-182`).

#### Diálogos, popups y ventanas secundarias

**Ventana principal (Settings)**: sin marco de Windows (barra de título propia), ancho fijo 440 px,
alto ajustado al contenido, esquinas redondeadas y sombra nativas de Windows 11, sin botón
maximizar ni bordes de estirar. Cerrar la oculta a la bandeja; solo `Quit` del tray la cierra de
verdad. `src/ui/MainWindow.cpp:28,372-393,434-439`.

**HelpDialog** (`src/ui/HelpDialog.cpp`), modal, sin marco, con velo (`Scrim`) semitransparente
(`rgba(8,8,8,184)`) detrás que cubre toda la ventana principal:
- Encabezado: `LGA FolderSwitch` + `v0.10`, botón `X` (tooltip `Close`).
- `Developed by Lega Pugliese`.
- Link `github.com/legandrop` (subrayado, cambia de color al pasar el mouse; tooltip
  `https://github.com/legandrop`; click abre esa URL en el navegador).
- Sección `How it works`, con 3 pasos numerados:
  1. `Open a folder in Explorer or XYplorer.` (Explorer y XYplorer resaltados)
  2. `Go to the Open or Save dialog of any app.` (Open y Save resaltados)
  3. `The dialog jumps to that folder.`
- Nota: `Works with Windows file dialogs and Qt ones, like Nuke's. Closing the window keeps
  FolderSwitch running in the tray.`
- Botón `Close` (además del `X` del encabezado).

**Diálogo "Update Available"** (`src/updates/UpdateDialog.cpp`), modal, con marco estándar de Qt:
- Título de ventana: `Update Available`.
- Texto: `{Nombre} {versión} is available.` (ej. `LGA FolderSwitch 0.11 is available.`).
- Subtexto: `You are running version {versión actual}. Updating closes FolderSwitch and opens the
  installer.`
- Botones: `Later` (rechaza, no vuelve a preguntar hasta el próximo arranque o chequeo manual —
  sin snooze/skip persistente) y `Update now` (default, acepta y arranca la descarga).

**Diálogo de progreso de descarga** (`QProgressDialog`, `src/updates/UpdateService.cpp:408-415`):
- Título: `Downloading Update`.
- Texto: `Downloading {Nombre} {versión}...`.
- Botón: `Cancel`.
- Barra determinada si el servidor informa tamaño total; modal a la ventana.

#### Notificaciones y avisos

Todos vía `QMessageBox` (modal), disparados por `UpdateService` — el chequeo automático al
arrancar **solo interrumpe si hay una versión nueva**; todo lo demás (ya al día, error, sin
release) queda en silencio salvo que el chequeo sea manual (`Check now` o el ítem del tray):

| Título del diálogo | Texto (literal, con placeholders) | Cuándo |
|---|---|---|
| `Updates` | `An update operation is already in progress.` | se pide un chequeo mientras ya hay uno en curso |
| `Update Check Failed` | `Could not check for updates. Please try again later.\n\nhttpStatus=%1\nurl=%2\nnetworkError=%3\nbody=%4` | error de red al chequear (solo si manual) |
| `Update Check Failed` | `Could not check for updates. Please try again later.\n\nhttpStatus=%1\nurl=%2\nbody=%3` | status HTTP distinto de 200 (solo si manual) |
| `Updates` | `No installable update was found.` | el manifiesto no tiene release para esta app (solo si manual) |
| `Updates` | `Release %1 does not contain an installable asset.` | el release no tiene el asset esperado (solo si manual) |
| `Updates` | `You are running the latest version.` | ya está actualizado (solo si manual) |
| `Update Check Failed` | `Release %1 does not include a valid integrity digest. Refusing to download an unverifiable installer.` | el manifiesto no trae SHA-256 válido (fail-closed; solo si manual) |
| `Update Failed` | `The update cache directory could not be created.` | no se pudo crear la carpeta temporal de descarga |
| `Update Failed` | `The update installer could not be saved.\n%1` | no se pudo abrir el archivo de destino para escribir |
| `Update Failed` | `The update installer could not be written to disk.` | escritura corta o fallida (disco lleno, permisos) |
| `Update Failed` | `The update installer could not be downloaded.\n\nhttpStatus=%1\nnetworkError=%2 (%3)` | error de red durante la descarga |
| `Update Failed` | `Internal error: missing integrity digest for the downloaded update.` | guarda interna (no debería poder pasar) |
| `Update Failed` | `The downloaded update failed integrity verification.\n\nesperado=%1\nobtenido=%2` | el SHA-256 descargado no coincide con el del manifiesto |
| `Update Failed` | `The update installer could not be saved.\n\n%1` | falló el commit final del archivo descargado |

Cancelar la descarga desde el botón `Cancel` del progreso **no** muestra ningún cartel (se trata
como decisión del usuario, no como error). Evidencia: `src/updates/UpdateService.cpp:199-621`.

**Notificación de la bandeja (`showMessage`)**: existe el método `TrayController::showWarning(title,
message)` que dispara un globo de la bandeja con ícono de advertencia y 10 segundos de duración
(`src/tray/TrayController.cpp:160-165`), pero en el código actual **no se encontró ningún llamado**
a este método — ver "Dudas".

#### Atajos de teclado y gestos

- Ningún atajo de teclado adicional fuera de los dos globales del módulo Folder Switch.
- Toda la app (ventana de Settings y HelpDialog) está diseñada **sin foco de teclado**: solo hover
  con mouse; Tab no recorre controles (decisión de diseño, `Docs/Doc_Decisiones.md:28`).
- Arrastrar la barra de título mueve la ventana (gesto de mouse, ver tabla de controles).

#### Textos de ayuda y tooltips (literales)

- `Help` (botón de ayuda de la barra de título).
- `Minimize` (botón minimizar).
- `Close` (botón cerrar de la barra de título, y también el botón `X` del HelpDialog).
- `https://github.com/legandrop` (tooltip del link de GitHub en el HelpDialog).
- `Start LGA FolderSwitch when you sign in to Windows` / `Registers THIS development copy to start
  when you sign in` (tooltip del checkbox de inicio con Windows, según disponibilidad).

#### Comportamientos configurables sin control visible

| Comportamiento | Valor | Evidencia |
|---|---|---|
| URL del manifiesto de updates | `https://legandrop.github.io/LGA_Updates/versions.json` | `src/updates/UpdateService.cpp:38` |
| Repo de referencia para el asset | `legandrop/LGA_FolderSwitch` | `src/updates/UpdateService.cpp:39` |
| Patrón del instalador esperado | `LGA_FolderSwitch_Setup_v<version>.exe` | `src/updates/UpdateService.cpp:52-57` |
| Delay del chequeo automático al arrancar | 15 000 ms, para no competir con el arranque del tray | `src/updates/UpdateService.cpp:44` |
| Timeout del chequeo de manifiesto | 15 000 ms | `src/updates/UpdateService.cpp:45` |
| Timeout de inactividad de la descarga | 300 000 ms (5 min), no es un timeout total | `src/updates/UpdateService.cpp:46,420-422` |
| Verificación de integridad | SHA-256 obligatorio, fail-closed (sin digest válido no se descarga nada) | `src/updates/UpdateService.cpp:326-341,561-571` |
| Snooze/skip de una versión rechazada | No existe: `Later` solo pospone hasta el próximo arranque o chequeo manual | `src/updates/UpdateService.h:21-22` |
| Carpeta de descarga del instalador | `%TEMP%\LGA_FolderSwitch_updates\` | `src/updates/UpdateService.cpp:379-381` |
| Instancia única | Lock file `com.lga.folderswitch.singleton.lock` en `%TEMP%`, intento de 100 ms; si falla, la segunda copia sale sola | `src/main.cpp:184-191` |
| Espera de la bandeja al arrancar (por si el shell no está listo) | Reintenta cada 500 ms hasta 90 000 ms; si no aparece, sale con código 1 | `src/main.cpp:207-232` |
| Migración de configuración vieja | Si `settings.ini` no existe, copia las claves desde `HKCU\Software\LGA\FolderSwitch` (registro) y borra esa clave (y su padre `Software\LGA` si queda vacío) | `src/core/AppSettings.cpp:29-66` |
| Primer arranque de una copia instalada | Si `firstRunCompleted` no está en `true`, activa "Start with Windows" una sola vez y abre la ventana de Settings — pero **solo** si la copia no corre desde un árbol de desarrollo (`build`, `deploy`, o dentro del árbol de CMake) | `src/tray/TrayController.cpp:104-124`; `src/windows/AppSettings` clave interna `firstRunCompleted` (no expuesta en la UI) |
| Detección de "copia de desarrollo" (afecta si el checkbox de autostart puede escribir de verdad) | Carpeta que contiene el exe con `build` o `deploy` en el nombre, o dentro de `LGA_BUILD_TREE_DIR` / `LGA_SOURCE_TREE_DIR` | `src/windows/AutoStart.cpp:89-121` |
| Log de depuración a archivo | Desactivado por default; se activa con `log=true` en `config/debug_flags.txt` (junto al exe, o un nivel arriba si corre desde `build/`); escribe en `debug.log` | `src/main.cpp:57-94` |
| Modo de captura QA (`--ui-shot`) | No escribe `debug.log`, no toca el registro ni el `.ini`; usa un `AppState` de solo memoria con datos de prueba fijos (incluida una ruta de ejemplo `N:\Proyectos\2026_Serie_Ficticia\...`) | `src/qa/UiShot.cpp:64-65,134-157` |
| Modo de prueba oculto `--test-switch <hwnd> <folder>` | Aplica el switch a un HWND dado y sale, sin levantar tray ni hotkeys | `src/main.cpp:170-182` |

---

### Totales

- **2** atajos de teclado globales (Ctrl+Alt+O, Ctrl+Alt+Shift+O) + 4 atajos de teclado dentro del
  popup de recientes (números 1-9, flechas, Enter, Esc).
- **6** controles interactivos en la ventana de Settings: toggle Pause/Resume, checkbox Switch
  automatically, checkbox Start with Windows, checkbox Check for updates at startup, botón Check
  now, botón de ayuda (más minimizar/cerrar en la barra de título).
- **3** tarjetas en la ventana de Settings (cambio de carpeta, última carpeta, la app).
- **1** menú de bandeja con 7 entradas (encabezado + 4 ítems + 2 separadores).
- **3** ventanas/diálogos secundarios propios: HelpDialog, diálogo "Update Available", diálogo de
  progreso de descarga (`QProgressDialog`, estándar de Qt).
- **1** popup a medida (carpetas recientes) con 2 estados (con carpetas / vacío).
- **14** textos distintos de `QMessageBox` ligados a updates.
- **4** claves persistidas en `settings.ini` visibles/derivadas de un control (`enabled`,
  `autoSwitch`, `checkUpdatesAtStartup`, `recentFolders`) + **1** clave interna no expuesta
  (`firstRunCompleted`) + **1** valor de registro fuera del `.ini` (autostart, `Run\LGA_FolderSwitch`).
- **14** estados distintos capturados por la herramienta de QA (`kStates` en `UiShot.cpp`), que
  sirven de checklist visual de todo lo anterior.

### Dudas

1. `TrayController::showWarning(title, message)` está definido y declarado pero no se encontró
   ningún punto del código que lo llame: puede ser un mecanismo de aviso pensado para un caso que
   todavía no se cableó (¿fallo persistente de un atajo?, ¿error no cubierto por updates?). Si el
   diseño nuevo tiene que preservar TODO lo que hace la app hoy, este aviso no dispara nunca en la
   práctica — conviene decidir si se lleva igual (por si se activa en el futuro) o se descarta.
2. La decisión abierta **D-01** (`Docs/Doc_Decisiones.md`) define que el resultado de "Check now"
   podría dejar de mostrarse con `QMessageBox` y pasar a un estado en la misma fila de la ventana
   (`Checking…`, `v0.10 is the latest version` en verde, `v0.11 is available` con botón
   `Update`). Hoy la app usa la opción A (diálogos) en todos los casos; el mockup de la opción B
   existe pero no está implementado. El diseño nuevo debería contemplar cuál de las dos formas
   hereda.
3. El ícono de la app en la ventana (`AppIcon`, 16px) sale de `:/icons/LGA_FolderSwitch.png`; el de
   la bandeja pausado intenta primero `:/icons/LGA_FolderSwitch_menubar.png` y si no existe cae a
   `LGA_FolderSwitch.png`. No se confirmó si ambos recursos están empaquetados o si el fallback es
   el camino real en uso — no cambia el inventario funcional, pero puede importar para migrar los
   assets al módulo nuevo.
4. El roadmap (`Docs/Doc_Roadmap.md`) lista como pendiente que la app "no escribe su
   `LGA_FolderSwitch.json`" en el registro compartido de LGA (`LgaRegistry`), por lo que hoy **no
   aparece** en el card de "LGA Updates" de otras apps LGA (como PipeSync). Es una integración que
   falta, no algo que haya que reproducir del estado actual — se anota para que no se dé por hecho
   que ya existe.

## LGA Link Redirector

### Módulo Link Redirector

#### Controles y opciones

| Elemento | Tipo de control | Valores / rango / default | Texto exacto en la UI (literal) | Dónde se guarda (clave del .ini) | Plataforma | Evidencia archivo:línea |
|---|---|---|---|---|---|---|
| Tarjeta de estado del navegador por defecto | Panel + ícono de estado (`StatusIndicator`) + texto rich-text | Dos estados: es default / no es default (ver "Estados visibles") | Ver textos en "Estados visibles" | No se guarda; se calcula en vivo contra el sistema | Win/mac | `src/ui/mainwindow/mainwindow.cpp:1583-1617` |
| Botón "Set as default" | `QPushButton` (estilizado, `GlassActionButton`) | Visible solo si NO es el default; oculto si ya es default | `Make Default` | No aplica (dispara acción) | Win/mac | `src/ui/mainwindow/mainwindow.cpp:1275`, visibilidad en `1608` y `1615` |
| Combo "Default browser" | `QComboBox` (estilizado, `GlassComboBox`) | Ítems: `-` (ninguno) + navegadores detectados + `<nombre> (custom)` si el guardado no está en la lista + separador + `Browse...` | Etiqueta: `Default browser` / subtítulo `For non-matching links` | `routing/defaultBrowser` (ruta al ejecutable, string) | Win/mac | Etiqueta `mainwindow.cpp:1292`; combo `1295-1297`; persistencia `Settings.cpp:19,57-61` |
| Combo "Alternative browser" | `QComboBox` (estilizado, `GlassComboBox`) | Mismos ítems que el combo de arriba | Etiqueta: `Alternative browser` / subtítulo `For matching links` | `routing/alternativeBrowser` (ruta al ejecutable, string) | Win/mac | Etiqueta `mainwindow.cpp:1299`; combo `1302-1304`; persistencia `Settings.cpp:20,63-67` |
| Ítem de combo "-" | Ítem de `QComboBox` | Sentinel interno `__lr_no_browser__`; limpia el rol de navegador (deja el .ini vacío) | `-` | Vacía la clave correspondiente | Win/mac | `mainwindow.cpp:80,1403,1501-1502` |
| Ítem de combo "Browse..." | Ítem de `QComboBox` que abre `QFileDialog` | Sentinel interno `__lr_browse__` | `Browse...` | Si se elige un archivo válido, guarda esa ruta como custom | Win/mac | `mainwindow.cpp:78,1419,1462-1500` |
| Diálogo "elegir ejecutable" (Windows) | `QFileDialog::getOpenFileName` | Carpeta inicial `C:/Program Files`; filtro `*.exe` | Título: `Select browser executable` · filtro: `Executables (*.exe)` | N/A | Windows | `mainwindow.cpp:1162-1164` |
| Diálogo "elegir app" (macOS) | `QFileDialog::getOpenFileName` | Carpeta inicial `/Applications`; filtro `.app` o cualquier archivo | Título: `Select browser app or executable` · filtro: `Applications (*.app);;All files (*)` | N/A | macOS | `mainwindow.cpp:1158-1160` |
| Editor "Match words" | `QTextEdit` multilínea (estilizado, `LinedTextEdit`), una palabra por línea | Lista de substrings; se limpian líneas vacías y espacios al guardar; autoguardado 500 ms después de dejar de tipear | Etiqueta: `Match words` / subtítulo `Links containing any of these open in the alternative browser`; placeholder: `netflixstudios`; texto de ayuda bajo el título: `One keyword per line`; chip: `AUTOSAVED` | `routing/matchWords` (lista de strings) | Win/mac | Etiquetas `mainwindow.cpp:1341-1360`; guardado `1529-1545,1193-1196`; persistencia `Settings.cpp:21,42-55,69-73` |
| Checkbox "Iniciar con el sistema" | `QCheckBox` | Marcado/desmarcado; refleja el estado real del sistema (Run key en Windows, LaunchAgent en macOS); si falla al activarlo, se revierte solo | Windows: `Start with Windows` · macOS: `Start with macOS` | No usa el .ini de la app: en Windows es el valor `LinkRedirector` de `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`; en macOS es el archivo `~/Library/LaunchAgents/com.lga.linkredirector.autostart.plist` | Win/mac | Etiqueta `mainwindow.cpp:1168-1175`; toggle `1547-1560`; Windows `src/windows/AutoStart.cpp:9-51`; macOS `src/macos/AutoStart.cpp:17-189` |
| Pestañas "Settings" / "Help" | `QTabWidget` (subrayado violeta en la activa) | Dos pestañas fijas | `Settings`, `Help` | N/A | Win/mac | `mainwindow.cpp:1239-1244,1383,1386` |
| Geometría de ventana | Persistencia automática (sin control visible), 400 ms después de mover/redimensionar | Tamaño de la ventana es fijo (560×650, no redimensionable); solo se guarda/restaura la posición | N/A (no es un control) | Archivo `window_geometry.ini` en `AppDataLocation`, clave `geometry` (blob `saveGeometry()`) | Win/mac | `mainwindow.cpp:1183,1052-1060,1188-1191,1619-1646` |

#### Estados visibles

- **Tarjeta de estado — Es el default:** ícono violeta con tilde (check); texto: `LinkRedirector is routing your links` (título) + `Default browser is active · matching links use your alternative browser` (subtítulo); el botón `Make Default` queda oculto. Evidencia: `mainwindow.cpp:1588-1608`.
- **Tarjeta de estado — No es el default:** ícono ámbar/dorado con una barra + punto (signo de exclamación estilizado); texto: `Make LinkRedirector your default browser` (título) + `Choose Make Default to start routing links automatically` (subtítulo); el botón `Make Default` queda visible. Evidencia: `mainwindow.cpp:1609-1616`.
- El estado se recalcula: al abrir la ventana (`showEvent`), al tocar "Make Default" (recálculo inmediato + reintentos a los 2.5 s y 6 s, porque en Windows el cambio requiere que el usuario elija en un panel del sistema y en macOS es asíncrono). Evidencia: `mainwindow.cpp:1576-1580,1648-1653`.
- **Combos de navegador — ítem seleccionado:** se marca con un tilde dibujado a mano dentro del dropdown desplegado (no es el checkmark nativo de Qt). Evidencia: `mainwindow.cpp:823-836,1076-1091`.
- **Combos de navegador — hover / foco / desplegado:** el fondo del control cambia de un gradiente "normal" a uno "hover" más brillante. Evidencia: `mainwindow.cpp:262-290`.
- **Checkbox de autostart — si falla al activarse:** el checkbox se revierte visualmente al estado real leído del sistema (no queda "tildado" si el registro/plist no se pudo escribir). Evidencia: `mainwindow.cpp:1547-1560`.

### General de la app (inicio con el sistema, versión, ayuda, bandeja)

#### Controles y opciones

| Elemento | Tipo de control | Valores / rango / default | Texto exacto en la UI (literal) | Dónde se guarda (clave del .ini) | Plataforma | Evidencia archivo:línea |
|---|---|---|---|---|---|---|
| Versión mostrada en Help | Texto (rich text), no editable | Toma `LINKREDIRECTOR_VERSION` (definido por CMake desde `project(... VERSION 0.173)`) | `LinkRedirector v0.173` (formato: nombre en violeta + `v<versión>` más chico) | N/A (se lee, no se guarda) | Win/mac | `src/ui/helptab/helptab.cpp:21-23,304-317`; `CMakeLists.txt:2,172` |
| Crédito de autor | Texto fijo | — | `Desarrollado por Lega` | N/A | Win/mac | `helptab.cpp:322-328` |
| Link a GitHub | `QLabel` clickeable con hover, abre navegador externo | Al click abre `https://github.com/legandrop` con `QDesktopServices::openUrl` | `github.com/legandrop` | N/A | Win/mac | `helptab.cpp:212-258,330` |
| Ícono de la bandeja / barra de menú | Ícono monocromo (glyph) | Windows: se tiñe de blanco o negro según el tema de la barra de tareas (clave de registro `SystemUsesLightTheme`), y se retiñe solo si el usuario cambia de tema en caliente. macOS: ícono "template" (`setIsMask(true)`), el sistema lo recolorea. | Tooltip: `LinkRedirector` | N/A | Win/mac | `src/tray/TrayController.cpp:19-51,70,78-97` |
| Menú de la bandeja | `QMenu` | Dos acciones + separador | `Open Settings`, separador, `Quit` | N/A | Win/mac | `TrayController.cpp:61-66` |

#### Estados visibles

- No hay estados adicionales fuera de la tarjeta de Settings ya cubierta arriba: el ícono de bandeja tiene exactamente dos variantes de color (claro/oscuro en Windows) y una variante "template" en macOS, sin animación ni badges.

#### Menú de la bandeja / barra de menú

- **Windows (system tray):** ícono con menú contextual: `Open Settings` (abre/trae al frente la ventana de Settings), separador, `Quit` (cierra la app). Doble click o click simple (`Trigger`/`DoubleClick`) sobre el ícono también abre Settings. Tooltip del ícono: `LinkRedirector`. Evidencia: `TrayController.cpp:61-76,117-136`.
- **macOS (menu bar):** mismo menú (`Open Settings` / `Quit`); al abrir Settings la app pasa de política de activación "Accessory" (solo barra de menú, sin ícono en el Dock) a "Regular" para poder tomar foco de teclado, y vuelve a "Accessory" al cerrar la ventana de Settings (la "X" oculta, no cierra la app). Evidencia: `TrayController.cpp:117-129`; `src/macos/MacIntegration.mm:8-20`; `mainwindow.cpp:1667-1678`.

#### Diálogos, popups y ventanas secundarias (incluye el contenido de la pestaña Help)

- **Ventana principal "Settings":** título de ventana `LinkRedirector`, tamaño fijo 560×650 (no redimensionable). El botón de cerrar (X) NO cierra la app: oculta la ventana a la bandeja; salir de verdad es solo desde `Quit` del menú de la bandeja. Evidencia: `mainwindow.cpp:1179-1183`; `include/linkredirector/mainwindow.h:17-18`; `mainwindow.cpp:1667-1678`.
- **Pestaña Help — tarjeta "About":** título `LinkRedirector v<versión>`, `Desarrollado por Lega`, link `github.com/legandrop`.
- **Pestaña Help — tarjeta de contenido, sección "What does it do?"** (con ícono de interrogación):
  > "LinkRedirector keeps your browsing organized by automatically opening links in the right browser. Links that match your keywords open in your alternative browser. Everything else opens in your default browser."
  Evidencia: `helptab.cpp:342-349`.
- **Pestaña Help — tarjeta de contenido, sección "How it works"** (con ícono de engranaje):
  > "Add keywords (one per line). If a link contains any of these words, it opens in the alternative browser. Otherwise, it opens in the default browser. If a selected browser is unavailable, LinkRedirector warns you and temporarily uses the other configured browser. Use '-' to leave either browser role unassigned; the selected item is marked with a check."
  Evidencia: `helptab.cpp:356-366`.
- **Diálogo crítico "sin bandeja disponible":** `QMessageBox::critical`, título `LinkRedirector`, mensaje `No system tray available in this session.` Se muestra solo si el sistema no tiene bandeja/área de notificación disponible al arrancar, y la app corta el arranque (return 1). Evidencia: `src/main.cpp:178-182`.
- **Diálogo de advertencia "navegador no disponible" (modo router, sin bandeja activa):** `QMessageBox::warning`, título `Configuracion de navegadores`, mensaje variable (ver "Notificaciones y avisos"). Aparece cuando la app es invocada directamente como el navegador (proceso nuevo, headless) y el navegador elegido no es válido. Evidencia: `src/main.cpp:99-101`; `src/core/UrlRouter.cpp:59-78`.
- **Selector de archivo "Browse...":** ver tabla de controles arriba (dos variantes según plataforma).
- **Panel del sistema "Apps predeterminadas" (Windows):** al tocar "Make Default", si no puede setearse por API, Windows abre su propio panel de configuración (`ms-settings:defaultapps?registeredAppUser=LinkRedirector`, con fallback a `ms-settings:defaultapps` genérico si el deep link específico falla) para que el usuario elija manualmente. No es UI de la app. Evidencia: `src/windows/DefaultBrowser.cpp:154-175`.
- **Prompt de confirmación del sistema (macOS):** al tocar "Make Default", macOS 12+ muestra su propio prompt nativo de confirmación (`NSWorkspace.setDefaultApplication`), fuera del control de la app. Evidencia: `src/macos/MacIntegration.mm:22-49`.

#### Notificaciones y avisos

- **Aviso "navegador no disponible" (dos canales según el modo de ejecución):**
  - En modo bandeja (la app corre, recibe la URL por evento — típico de macOS con `QFileOpenEvent`): notificación de bandeja (`QSystemTrayIcon::showMessage`), ícono de advertencia, se autodestruye a los 10000 ms (10 s). Evidencia: `TrayController.cpp:110-115`; `main.cpp:187-189`.
  - En modo router (proceso nuevo invocado como navegador, sin bandeja activa): `QMessageBox::warning` modal. Evidencia: `main.cpp:99-101`.
  - Título del aviso: `Configuracion de navegadores`.
  - Cuerpo del mensaje (compuesto en runtime): `<motivo>\nSe abrira este enlace temporalmente con: <nombre del ejecutable usado>`, donde `<motivo>` es uno de:
    - `El navegador default no esta configurado.` / `El navegador alternativo no esta configurado.` (si el rol correspondiente está vacío en el .ini)
    - `La ruta configurada para el navegador default no existe.` / `La ruta configurada para el navegador alternativo no existe.` (si la ruta guardada ya no existe en disco)
  - Evidencia completa: `src/core/UrlRouter.cpp:59-78`.
- No hay otras notificaciones de sistema (no hay avisos de éxito, ni de "match encontrado", ni de actualización disponible dentro de esta app).

#### Textos de ayuda y tooltips (literales)

- Subtítulo bajo "Default browser": `For non-matching links` (`mainwindow.cpp:1292`).
- Subtítulo bajo "Alternative browser": `For matching links` (`mainwindow.cpp:1299`).
- Subtítulo bajo "Match words": `Links containing any of these open in the alternative browser` (`mainwindow.cpp:1341-1343`).
- Texto de ayuda sobre el editor de match words: `One keyword per line` (`mainwindow.cpp:1353`).
- Placeholder del editor de match words (cuando está vacío): `netflixstudios` (`mainwindow.cpp:1360`).
- Chip `AUTOSAVED` junto al título de match words, indicando que no hace falta guardar a mano (`mainwindow.cpp:1349-1351`).
- Tooltip del ícono de bandeja: `LinkRedirector` (`TrayController.cpp:70`).
- Todo el contenido de la pestaña Help (ver sección de diálogos arriba) funciona como ayuda in-app permanente; no hay tooltips emergentes (`QToolTip`) adicionales en ningún control.

#### Comportamientos configurables sin control visible

- **Detección de navegadores instalados — Windows:** lee `HKEY_LOCAL_MACHINE\Software\Clients\StartMenuInternet` y `HKEY_CURRENT_USER\...` (mismo path), arma nombre + ruta del ejecutable + ProgId http a partir de `shell\open\command` y `Capabilities\URLAssociations\http`; excluye explícitamente Internet Explorer (`IEXPLORE.EXE`); deduplica por ruta de ejecutable si HKCU repite lo de HKLM. Evidencia: `src/windows/BrowserRegistry.cpp:9,32-96`.
- **Detección de navegadores instalados — macOS:** usa `LSCopyAllHandlersForURLScheme("http")` (API deprecada, silenciada con pragma) más una lista fija de bundle ids de respaldo (Safari, Chrome, Brave, Firefox, Opera, Vivaldi, Edge, Arc, etc.) para cubrir navegadores instalados pero no indexados aún; filtra por una lista de familias conocidas (chrome, chromium, firefox, brave, edgemac, opera, vivaldi, floorp, waterfox, librewolf, torbrowser, zen-browser, thebrowser) y algunos bundle ids exactos (Safari, Arc, Dia, Sidekick, Whale, Orion/Kagi, qutebrowser, DuckDuckGo, Firefox Nightly), para no listar cualquier app que declare manejar http (ej. un reproductor de video); excluye siempre el propio bundle id de LinkRedirector. Evidencia: `src/macos/BrowserRegistry.cpp:87-138,199-256`.
- **Navegador elegido no existe (en el momento de rutear un link):** si el ejecutable configurado ya no está en disco (o el rol está vacío), se prueba el otro navegador configurado como fallback temporal (con el aviso descrito arriba); si tampoco es válido, la app aborta silenciosamente esa apertura (solo queda registrado en el log, no se abre nada). Evidencia: `src/core/UrlRouter.cpp:59-83`.
- **Coincidencia de palabras clave:** substring, sin distinguir mayúsculas/minúsculas (`contains(word, Qt::CaseInsensitive)`); basta con que la URL contenga cualquiera de las palabras configuradas. Evidencia: `UrlRouter.cpp:23-43`.
- **Modo `--register`:** registra la app como navegador candidato del sistema y sale (código 0/1), sin abrir UI. En Windows escribe toda la maraña de claves de registro necesarias (`StartMenuInternet`, `Capabilities`, `URLAssociations` http/https, `FileAssociations` .htm/.html, `Classes\LinkRedirectorURL`, `RegisteredApplications`) y notifica al shell (`SHChangeNotify`). En macOS llama a `LSRegisterURL`. Pensado para el instalador/primer arranque. Evidencia: `main.cpp:107-112`; `src/windows/DefaultBrowser.cpp:44-103`; `src/macos/DefaultBrowser.cpp:47-62`.
- **Modo `--unregister`:** en Windows borra los árboles de registro creados por `--register` y el valor en `RegisteredApplications`; en macOS es un no-op (no existe API de desregistro equivalente). Pensado para el desinstalador. Evidencia: `main.cpp:113-117`; `src/windows/DefaultBrowser.cpp:105-121`; `src/macos/DefaultBrowser.cpp:70-74`.
- **Modo `--selftest`:** corre una batería de diagnóstico sin UI (lista navegadores detectados, ProgId default actual, si la app ya es default, si ya está registrada, si el autostart está activo, y hace un ciclo completo registrar→verificar→desregistrar dejando todo como estaba), todo a `SelfTest.log` (se sobrescribe en cada corrida). Evidencia: `main.cpp:120-124`; `src/core/SelfTest.cpp:11-43`.
- **Modo `--settings`:** al arrancar, además de correr en bandeja, abre la ventana de Settings de entrada (pensado para un acceso directo o el primer arranque tras instalar). Evidencia: `main.cpp:208-210`.
- **Modo router implícito:** si al invocar el ejecutable llega como argumento una URL `http(s)://` o un archivo local `.htm/.html/.xhtml` (ignorando flags que empiecen con `--`), la app rutea esa única URL y sale (código 0/1) sin mostrar ventana ni bandeja; es como Windows invoca a la app cuando está registrada como navegador. Evidencia: `main.cpp:27-45,93-105`.
- **Sincronización automática al lograr ser default:** si tras tocar "Make Default" la app detecta que ya es el navegador por defecto del sistema, y antes de eso había guardado cuál era el navegador default previo, lo asigna automáticamente como el "Default browser" interno de la app (así los links no-match siguen yendo al navegador que el usuario ya usaba) y refresca el combo. Esto ocurre sin que el usuario lo pida explícitamente. Evidencia: `mainwindow.cpp:1562-1601`.
- **Instancia única:** un lock de archivo (`QLockFile`, en la carpeta temp del sistema) evita que se abran dos bandejas a la vez; si ya hay una instancia corriendo, la nueva sale sin avisar nada visible (solo log). El lock detecta procesos muertos por PID, así un crash previo no bloquea el próximo arranque. No aplica a los modos router/register/unregister/selftest, que no compiten por la bandeja. Evidencia: `main.cpp:129-139`.
- **Logs:** `Debug.log` (modo bandeja, se borra y reescribe en cada arranque), `Router.log` (modos router/register/unregister, se agrega sin borrar porque corren muchas veces por día), `SelfTest.log` (se borra en cada corrida). Rotan/truncan solos si superan 10 MB. Ubicación: junto al ejecutable en modo build (`../logs`); instalado, `{carpeta de instalación}\logs` en Windows y `AppData/logs` (macOS, vía `QStandardPaths::AppDataLocation`). No hay control visible para verlos ni para cambiar su ubicación. Evidencia: `main.cpp:47-76,90-91,141`; `include/linkredirector/AppPathManager.h:13-38`.
- **Auto-registro en el registro compartido de apps LGA:** al arrancar en modo bandeja (no en router/selftest), la app se anota a sí misma (nombre + versión) en un registro compartido entre apps LGA, para que otras herramientas LGA sepan que está instalada y en qué versión; no escribe nada si corre desde la carpeta de build. Sin control visible. Evidencia: `main.cpp:146-153`.

#### Diferencias por plataforma

- **Ícono de bandeja/menú:** Windows tiñe manualmente un PNG monocromo según el tema claro/oscuro de la barra de tareas (lee la clave de registro `SystemUsesLightTheme`, no el tema de apps de Qt); macOS usa un ícono "template" que el sistema recolorea solo.
- **Detección de navegadores instalados:** Windows lee el registro (`StartMenuInternet`); macOS consulta LaunchServices (`LSCopyAllHandlersForURLScheme`) más una lista fija de respaldo y un filtro por familias conocidas.
- **Convertirse en navegador por defecto:** Windows no tiene API para hacerlo directo — abre el panel "Apps predeterminadas" del sistema (deep link a la página de LinkRedirector, con fallback genérico) y el usuario elige a mano; macOS 12+ lo pide por API (`NSWorkspace.setDefaultApplication`) y el sistema muestra su propio prompt de confirmación (macOS <12 usa una API más vieja sin prompt).
- **Recepción de la URL a rutear:** Windows la recibe como argumento de línea de comandos en un proceso nuevo (modo router); macOS la recibe como evento (`QFileOpenEvent`/Apple Event) dentro del mismo proceso que ya corre en la barra de menú, sin lanzar un proceso nuevo — por eso en macOS el aviso de "navegador no disponible" sale como notificación de bandeja y en Windows (proceso nuevo, sin bandeja) sale como cuadro de diálogo modal.
- **Registro/desregistro como navegador:** Windows escribe/borra varias claves de registro (`--register`/`--unregister` simétricos); macOS solo registra (`LSRegisterURL`), no existe una forma programática de desregistrarse (`unregisterAsBrowser()` es un no-op que siempre devuelve éxito).
- **Autostart:** Windows usa el valor `LinkRedirector` en `HKCU\...\Run` con la ruta del .exe entre comillas; macOS instala un LaunchAgent (`~/Library/LaunchAgents/com.lga.linkredirector.autostart.plist`, `RunAtLoad` true, `KeepAlive` false) y lo carga/descarga con `launchctl bootstrap`/`bootout` (API moderna; mezclarla con `load`/`unload` viejo rompe con error de I/O).
- **Ventana de Settings — activación:** en macOS, al abrir Settings la app pasa de "Accessory" (solo barra de menú) a "Regular" para poder tomar foco de teclado, y vuelve a "Accessory" al cerrar (ocultar) la ventana; en Windows no existe ese concepto de política de activación.
- **Ícono de la app:** en macOS no se fija el ícono de ventana en runtime (lo pone el `.icns` del bundle); en Windows/Linux sí se setea explícitamente (`app.setWindowIcon`).
- **Instalador:** solo existe instalador Inno Setup para Windows (`LinkRedirector_installer.iss`, en castellano, con tarea opcional de ícono de escritorio desmarcada por default, cierre de instancias previas vía `close_by_path.ps1`, y borrado de la carpeta `logs` al desinstalar). No hay evidencia de instalador/paquete equivalente para macOS en este repo (ni `.dmg`, ni script de firma/notarización).

### Totales

- Controles interactivos en la ventana de Settings: 7 (ícono/tarjeta de estado, botón "Make Default", combo "Default browser", combo "Alternative browser", editor "Match words", checkbox de autostart, más las 2 pestañas del `QTabWidget`).
- Claves persistidas en `settings.ini` (bajo `routing/`): 3 (`defaultBrowser`, `alternativeBrowser`, `matchWords`).
- Claves persistidas en `window_geometry.ini`: 1 (`geometry`).
- Mecanismos de autostart fuera del .ini de la app: 2 (valor de registro `Run` en Windows; LaunchAgent `.plist` en macOS).
- Ítems del menú de la bandeja/barra de menú: 2 acciones + 1 separador (`Open Settings`, `Quit`).
- Diálogos/ventanas secundarias: ventana de Settings (única ventana propia), pestaña Help (no es ventana aparte, es una pestaña), diálogo crítico de "sin bandeja", diálogo de advertencia de router, 2 variantes de selector de archivo (Win/mac), paneles nativos del sistema operativo (Apps predeterminadas en Windows, prompt de confirmación en macOS).
- Canales de aviso al usuario: 2 (notificación de bandeja `showMessage`, cuadro de diálogo `QMessageBox::warning`), ambos disparados por el mismo caso (navegador configurado inválido).
- Modos de línea de comandos sin UI: 4 (`--register`, `--unregister`, `--selftest`, y el modo router implícito al recibir una URL/HTML), más `--settings` que sí abre UI.
- Textos de ayuda in-app: la pestaña Help completa (1 tarjeta "About" + 2 secciones explicativas), sin tooltips emergentes adicionales.

### Dudas

- No se encontró en el repo evidencia de un instalador o empaquetado (.dmg, firma ad-hoc, notarización) para macOS: solo hay `.iss` de Windows. Si existe en otro lado, no fue relevado acá.
- El texto del diálogo/notificación de fallback ("Configuracion de navegadores" y sus variantes) está escrito en castellano dentro del código, aunque el resto de la UI (labels, Help) está en inglés — no quedó claro si es intencional (mezcla de idiomas visible al usuario final) o un texto pendiente de traducir; se listó tal cual aparece en el código porque es lo que el usuario ve.
- No se encontró ningún control de UI para ver o abrir la carpeta de logs, ni para forzar un re-escaneo de navegadores instalados (el refresco de los combos ocurre solo al abrir la ventana / cargar settings, no hay botón "Refresh").
- `DefaultBrowser::isRegistered()` (Windows) y el resultado de `--selftest` no tienen ninguna traducción a un control o indicador visible en la UI: solo se usan en el log de selftest. Se documentó como comportamiento interno, no como estado visible.
- No se revisó el contenido exacto de `docs/UI_STYLE_LGA_APPS.md`, `docs/Doc_LinkRedirector_Planning.md` ni `docs/Doc_LinkRedirector_Status.md` línea por línea (son documentación de proceso, no UI en sí); si contienen alguna decisión de producto sobre un control que todavía no está implementado, no quedaría reflejada en este inventario, que describe únicamente lo que el código implementa hoy.
