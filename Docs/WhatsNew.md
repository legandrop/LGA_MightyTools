---
product: LGA Mighty Tools
release_repo: legandrop/LGA_MightyTools
tech_changelog: Changelog.md
version_heading: "v{v}:"
platforms: [win, mac]
---
# What's new in LGA Mighty Tools
<!-- Editable while a version is unpublished. NOT append-only. Published versions are frozen. -->

## Unreleased

## v1.53
- [new] Meet Frame: con Ctrl+Alt+M aparece un marco para compartir solo esa parte de la pantalla en Teams, Meet o Zoom; los avisos, el escritorio, WhatsApp, Telegram y las apps que elijas salen tapados para los demás, y Pause congela la imagen.
- [fixed][mac] Link Redirector ya no altera los links que reenvía al navegador: los inicios de sesión con Google y otros servicios que antes terminaban en «Error 400: invalid_request» vuelven a funcionar.
- [improved] Si falla la conexión, la búsqueda de actualizaciones, la descarga de una versión nueva y la del modelo de Dictation lo vuelven a intentar solas, cada cinco segundos y hasta tres veces por cada servidor, y avisan «Retrying (2 of 4)…» mientras esperan; una descarga cortada sigue desde donde quedó.
- [improved] Mighty Tools tiene un diseño nuevo: cada herramienta es una hoja sobre fondo negro unida a su lugar en la lista de la izquierda, y los colores dicen algo: cian es lo que está funcionando, magenta lo que podés tocar y amarillo lo que pide atención; la letra es igual de grande que antes.
- [improved] Las herramientas aparecen en orden alfabético en la ventana, en el menú de la bandeja y en General.
- [improved] El «?» ya no abre una ayuda de todas las herramientas juntas: cada página tiene su «How it works» con los pasos de esa herramienta, cerrado al entrar, que se abre con un clic o con el «?»; la versión, el autor y la web están al pie de General.
- [new][win] Dictation escribe lo que decís en cualquier campo de texto: tocás Win+H (o lo mantenés mientras hablás), ves el texto en una ventanita mientras hablás y se escribe al terminar sin tocar lo que tenías copiado ni dejar rastro en el historial de Win+V; escribe bien términos de VFX y de desarrollo (Nuke Studio, compo, DMP, comitear, pushear) y podés sumar los tuyos en «Custom words»; funciona en tu computadora y el modelo se descarga una sola vez con «Download» desde su página.
- [improved][win] En General, el tamaño de la interfaz ahora se llama Zoom y muestra 100 %, 110 % y 120 %.
- [new] La limpieza de Disk Space encuentra las cachés de Nuke, DaVinci Resolve, After Effects, Premiere, Ableton, Topaz Video, REAPER, Unreal y otros programas de video y audio aunque las hayas movido a otro disco, y deja para que decidas lo que cuesta rehacer, como proxies, medios optimizados o modelos.
- [new][win] La limpieza de Disk Space encuentra copias de instaladores que Windows guarda de programas que ya no están instalados y las manda a la Papelera con permiso de administrador; las de programas instalados nunca aparecen.
- [new][win] Mouse Buttons hace que un botón del mouse mantenga teclas y el botón izquierdo solo en las apps que elijas, y trae de ejemplo, apagada, la regla que mueve la imagen de Photoshop con el botón del medio.
- [new] App Toggle abre una app con un atajo, la trae al frente si ya está abierta o la minimiza si ya la estás usando (en Mac la oculta), y trae de ejemplo, apagado, OBS y, en Windows, Win+E para XYplorer o, si no lo tenés, para el Explorador sin abrir una ventana nueva cada vez.
- [improved][win] Al soltar las teclas de un atajo de Window Layouts o App Toggle ya no se abre el menú Inicio ni la app de Office (Ctrl+Alt+Shift+Win, la combinación que mandan el Stream Deck y otros teclados programables), y Win más una letra (como Win+E) se puede grabar para reemplazar el atajo de Windows.
- [new][win] Desktop Icons guarda dónde están los íconos del escritorio, una vez por día o cuando quieras con un nombre, y los vuelve a su lugar con un clic cuando Windows los desordena, con «Undo» para deshacer.
- [improved] La ventana es un poco más alta y ninguna página corta textos ni botones: si algo no entra, aparece la barra de scroll.
- [improved] La barra de scroll es más finita.
- [improved] Todas las opciones de encender y apagar usan el mismo switch, en lugar de casillas, y el switch se desliza suave al tocarlo.
- [new][win] Window Layouts ahora puede mover también el Administrador de tareas y las apps abiertas como administrador: activar «Also move administrator windows» arriba de su panel.
- [fixed][win] Apply de Open in NukeX ya no cierra la app cuando Windows tiene que preguntar con qué programa abrir los `.nk`.

## v1.52
- [fixed][win] Window Layouts shortcuts sent by Stream Deck apply the layout instead of reaching Windows or other apps.
- [improved] Disk Space's Clean up shows how much each group and row will actually free next to its total, and how many of its folders are held by open apps.
- [improved] Disk Space's What changed groups the folders that grew by area, lists each one only once so the numbers add up, and shows how the growth splits between areas.

## v1.47
- [fixed] Scrolling over the reminder time in Disk Space no longer changes your choice; choose a time from the menu to change it.

## v1.43
- [new] Window Layouts lets you add window shortcuts to move or minimize the active window: combine horizontal and vertical halves, thirds or quarters, draw an area, or copy an open window's bounds while keeping clear of visible system bars.
- [new][win] Window Layouts tries to take priority for active shortcuts, marks detected clashes with a yellow dot inside its panel, and lets you turn each layout on or off separately.

## v1.37
- [improved] Disk Space reminders let you edit the minimum free space next to the reminder time, using the drive's GB or percentage setting, in a smaller alert with aligned buttons and a clearer "Remind me again" action.

## v1.33
- [fixed][mac] Checking for updates works: version 1.31 could not connect, so download this version once from the download page.

## v1.31
- [improved] The app waits to offer or install an update while Disk Space is deleting files.

## v1.30
- [new][mac] LGA Mighty Tools is now available for macOS, on Apple Silicon and Intel, with its five tools: Open in NukeX, Nuke Shortcuts, Disk Space, Folder Switch and Link Redirector.
- [new][mac] The app checks for new versions and installs them for you in one click.
- [fixed][win] Updating from inside the app works now: earlier versions stop with an integrity error, so install this one once with the installer from the download page.

## v1.27
- [improved] The app checks for a new version every few hours while it stays open, not only at startup, and Later postpones the notice for a day.
