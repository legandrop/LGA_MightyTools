---
product: LGA Mighty Tools
release_repo: legandrop/LGA_MightyTools
tech_changelog: Changelog.md
version_heading: "v{v}:"
platforms: [win, mac]
languages: [es, en]
en_from: "1.53"
---
# What's new in LGA Mighty Tools
<!-- Editable while a version is unpublished. NOT append-only. Published versions are frozen. -->

## Unreleased

## v1.55
- [improved][mac] El DMG viene firmado y verificado por Apple: la app se abre sin pasar por la Terminal. Con la firma nueva, macOS vuelve a pedir una vez los permisos que ya habías dado (Accesibilidad, micrófono y control del Finder).
  en: The DMG is now signed and verified by Apple: the app opens without going through Terminal. With the new signature, macOS asks once more for the permissions you had already granted (Accessibility, microphone and Finder control).

## v1.54
- [fixed] Dictation ya no escribe en inglés lo que dictaste en castellano cuando la ventanita lo mostraba bien.
  en: Dictation no longer types in English what you dictated in Spanish when the small window showed it correctly.
- [new] Dictation tiene la opción «Only closing marks» en Voice: con ella prendida, las preguntas y exclamaciones se escriben sin los ¿ y ¡ de apertura.
  en: Dictation has the «Only closing marks» option under Voice: when it is on, questions and exclamations are typed without the opening ¿ and ¡.
- [fixed][mac] El primer tooltip después de abrir Mighty Tools ya no aparece encima del puntero tapando el botón: sale debajo, como los siguientes.
  en: The first tooltip after opening Mighty Tools no longer appears over the pointer covering the button: it shows below, like the following ones.
- [new][mac] Dictation escribe lo que decís en cualquier campo de texto: tocás ⌘⌥⇧H (o lo mantenés mientras hablás), ves el texto en una ventanita mientras hablás y se escribe al terminar sin pasar por el portapapeles; la primera vez macOS te pide permiso para el micrófono y para Accesibilidad, y el modelo se descarga una sola vez con «Download» desde su página.
  en: Dictation types what you say into any text field: press ⌘⌥⇧H (or hold it while you speak), see the text in a small window as you talk, and it is typed when you finish without going through the clipboard; the first time, macOS asks for Microphone and Accessibility permission, and the model is downloaded only once with «Download» from its page.

## v1.53
- [improved] Mighty Tools es más liviana y rápida: la ventana abre y cambia de página al instante, y cerrada en la bandeja ocupa menos de la mitad de memoria, sin perder nada de lo que estabas editando.
  en: Mighty Tools is lighter and faster: the window opens and switches pages instantly, and closed in the tray it uses less than half the memory, without losing anything you were editing.
- [new] En General, Colors deja elegir entre seis paletas de color: Violet, High contrast, Graphite | amber, Mist, Warm teal y Magenta; se aplican en el acto y, con el menú abierto, las flechas del teclado las muestran antes de elegir.
  en: In General, Colors lets you choose one of six color palettes: Violet, High contrast, Graphite | amber, Mist, Warm teal and Magenta; they apply at once and, with the menu open, the arrow keys preview them before you choose.
- [new] El cartel de actualización muestra las novedades de la versión nueva, en el idioma que elegiste para Mighty Tools.
  en: The update window shows what's new in the new version, in the language you chose for Mighty Tools.
- [new] Escape cierra la ventana de Mighty Tools y la deja en la bandeja (en Mac, en la barra de menú), y Ctrl+Alt con las flechas izquierda y derecha (⌘⌥ en Mac) pasa a la herramienta anterior o siguiente de la lista.
  en: Escape closes the Mighty Tools window and leaves it in the tray (on Mac, in the menu bar), and Ctrl+Alt with the left and right arrows (⌘⌥ on Mac) goes to the previous or next tool in the list.
- [new] Meet Frame: con Alt+Shift+Win+M (en Mac, ⌘⌥⇧M) aparece un marco para compartir solo esa parte de la pantalla en Teams, Meet o Zoom; los avisos, el escritorio, WhatsApp, Telegram y las apps que elijas salen tapados para los demás, Pause congela la imagen, y el marco se estira a cualquier tamaño o, con «Lock the frame shape», mantiene la forma 16:9, 4:3, 1:1 o la que tenga.
  en: Meet Frame: Alt+Shift+Win+M (on Mac, ⌘⌥⇧M) shows a frame to share only that part of the screen in Teams, Meet or Zoom; notifications, the desktop, WhatsApp, Telegram and the apps you choose are hidden from the others, Pause freezes the image, and the frame stretches to any size or, with «Lock the frame shape», keeps the 16:9, 4:3, 1:1 or current shape.
- [fixed][mac] La ventana de Mighty Tools tiene las esquinas redondeadas como las del sistema y se mueve arrastrando desde cualquier parte libre de la barra de arriba, no solo desde algunas.
  en: The Mighty Tools window has rounded corners like the system's and moves by dragging from any free part of the top bar, not just some.
- [fixed][mac] Link Redirector ya no altera los links que reenvía al navegador: los inicios de sesión con Google y otros servicios que antes terminaban en «Error 400: invalid_request» vuelven a funcionar.
  en: Link Redirector no longer alters the links it forwards to the browser: sign-ins with Google and other services that ended in «Error 400: invalid_request» work again.
- [improved] Si falla la conexión, la búsqueda de actualizaciones, la descarga de una versión nueva y la del modelo de Dictation lo vuelven a intentar solas, cada cinco segundos y hasta tres veces por cada servidor, y avisan «Retrying (2 of 4)…» mientras esperan; una descarga cortada sigue desde donde quedó.
  en: If the connection fails, checking for updates, downloading a new version and downloading the Dictation model retry on their own, every five seconds and up to three times per server, and show «Retrying (2 of 4)…» while they wait; an interrupted download resumes where it left off.
- [improved] Mighty Tools tiene un diseño nuevo: cada herramienta es una hoja sobre fondo negro unida a su lugar en la lista de la izquierda, y los colores dicen algo: cian es lo que está funcionando, violeta lo que podés tocar y amarillo lo que pide atención; la letra es igual de grande que antes.
  en: Mighty Tools has a new design: each tool is a sheet on a black background joined to its place in the list on the left, and the colors mean something: cyan is what is running, violet what you can click and yellow what needs attention; the text is as large as before.
- [improved] Las herramientas aparecen en orden alfabético en la ventana, en el menú de la bandeja y en General.
  en: Tools appear in alphabetical order in the window, in the tray menu and in General.
- [improved] El «?» ya no abre una ayuda de todas las herramientas juntas: cada página tiene su «How it works» con los pasos de esa herramienta, cerrado al entrar, que se abre con un clic o con el «?»; la versión, el autor y la web están al pie de General.
  en: The «?» no longer opens a help page for all the tools together: each page has its own «How it works» with that tool's steps, closed when you open the page, which opens with a click or with the «?»; the version, the author and the website are at the bottom of General.
- [new][win] Dictation escribe lo que decís en cualquier campo de texto: tocás Win+H (o lo mantenés mientras hablás), ves el texto en una ventanita mientras hablás y se escribe al terminar sin tocar lo que tenías copiado ni dejar rastro en el historial de Win+V; escribe bien términos de VFX y de desarrollo (Nuke Studio, compo, DMP, comitear, pushear) y podés sumar los tuyos en «Custom words»; funciona en tu computadora y el modelo se descarga una sola vez con «Download» desde su página.
  en: Dictation types what you say into any text field: press Win+H (or hold it while you speak), see the text in a small window as you talk, and it is typed when you finish without touching what you had copied or leaving a trace in the Win+V history; it writes VFX and development terms correctly (Nuke Studio, compo, DMP, commit, push) and you can add your own in «Custom words»; it runs on your computer and the model is downloaded only once with «Download» from its page.
- [improved][win] En General, el tamaño de la interfaz ahora se llama Zoom y muestra 100 %, 110 % y 120 %.
  en: In General, the interface size is now called Zoom and shows 100 %, 110 % and 120 %.
- [new] La limpieza de Disk Space encuentra las cachés de Nuke, DaVinci Resolve, After Effects, Premiere, Ableton, Topaz Video, REAPER, Unreal y otros programas de video y audio aunque las hayas movido a otro disco, y deja para que decidas lo que cuesta rehacer, como proxies, medios optimizados o modelos.
  en: Disk Space's clean-up finds the caches of Nuke, DaVinci Resolve, After Effects, Premiere, Ableton, Topaz Video, REAPER, Unreal and other video and audio programs even if you moved them to another drive, and leaves for you to decide what is costly to rebuild, such as proxies, optimized media or models.
- [new][win] La limpieza de Disk Space encuentra copias de instaladores que Windows guarda de programas que ya no están instalados y las manda a la Papelera con permiso de administrador; las de programas instalados nunca aparecen.
  en: Disk Space's clean-up finds the installer copies Windows keeps for programs that are no longer installed and sends them to the Recycle Bin with administrator permission; those of installed programs never appear.
- [new][win] Mouse Buttons hace que un botón del mouse mantenga teclas y el botón izquierdo solo en las apps que elijas, y trae de ejemplo, apagada, la regla que mueve la imagen de Photoshop con el botón del medio.
  en: Mouse Buttons makes a mouse button hold keys and the left button only in the apps you choose, and includes, turned off, an example rule that pans the image in Photoshop with the middle button.
- [new] App Toggle abre una app con un atajo, la trae al frente si ya está abierta o la minimiza si ya la estás usando (en Mac la oculta), y trae de ejemplo, apagado, OBS y, en Windows, Win+E para XYplorer o, si no lo tenés, para el Explorador sin abrir una ventana nueva cada vez.
  en: App Toggle opens an app with a shortcut, brings it to the front if it is already open or minimizes it if you are already using it (on Mac it hides it), and includes, turned off, OBS as an example and, on Windows, Win+E for XYplorer or, if you don't have it, for File Explorer without opening a new window every time.
- [improved][win] Al soltar las teclas de un atajo de Window Layouts o App Toggle ya no se abre el menú Inicio ni la app de Office (Ctrl+Alt+Shift+Win, la combinación que mandan el Stream Deck y otros teclados programables), y Win más una letra (como Win+E) se puede grabar para reemplazar el atajo de Windows.
  en: Releasing the keys of a Window Layouts or App Toggle shortcut no longer opens the Start menu or the Office app (Ctrl+Alt+Shift+Win, the combination sent by Stream Deck and other programmable keyboards), and Win plus a letter (like Win+E) can be recorded to replace the Windows shortcut.
- [new][win] Desktop Icons guarda dónde están los íconos del escritorio, una vez por día o cuando quieras con un nombre, y los vuelve a su lugar con un clic cuando Windows los desordena, con «Undo» para deshacer.
  en: Desktop Icons saves where the desktop icons are, once a day or whenever you want with a name, and puts them back with one click when Windows scrambles them, with «Undo» to go back.
- [improved] La ventana es un poco más alta y ninguna página corta textos ni botones: si algo no entra, aparece la barra de scroll.
  en: The window is a little taller and no page cuts off text or buttons: if something doesn't fit, the scroll bar appears.
- [improved] La barra de scroll es más finita.
  en: The scroll bar is thinner.
- [improved] Todas las opciones de encender y apagar usan el mismo switch, en lugar de casillas, y el switch se desliza suave al tocarlo.
  en: All on and off options use the same switch instead of checkboxes, and the switch slides smoothly when you click it.
- [fixed] Window Layouts ya no queda en rojo con «Needs attention» cuando un atajo se aprieta sin una ventana para mover: avisa en amarillo qué había al frente (el escritorio, una ventana minimizada…) y a qué hora, y el aviso se borra con «Dismiss» o con el próximo atajo que funcione.
  en: Window Layouts no longer stays red with «Needs attention» when a shortcut is pressed with no window to move: it says in yellow what was in front (the desktop, a minimized window…) and when, and the note clears with «Dismiss» or with the next shortcut that works.
- [new][win] Window Layouts ahora puede mover también el Administrador de tareas y las apps abiertas como administrador: activar «Also move administrator windows» arriba de su panel.
  en: Window Layouts can now also move Task Manager and apps running as administrator: turn on «Also move administrator windows» at the top of its panel.
- [fixed][win] Apply de Open in NukeX ya no cierra la app cuando Windows tiene que preguntar con qué programa abrir los `.nk`.
  en: Open in NukeX's Apply no longer closes the app when Windows has to ask which program should open .nk files.

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
