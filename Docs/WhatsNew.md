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
- [new] La limpieza de Disk Space encuentra las cachés de Nuke, DaVinci Resolve, After Effects, Premiere, Ableton, Topaz Video, REAPER, Unreal y otros programas de video y audio aunque las hayas movido a otro disco, y deja para que decidas lo que cuesta rehacer, como proxies, medios optimizados o modelos.
- [new][win] La limpieza de Disk Space encuentra copias de instaladores que Windows guarda de programas que ya no están instalados y las manda a la Papelera con permiso de administrador; las de programas instalados nunca aparecen.
- [new][win] Mouse Buttons hace que un botón del mouse mantenga teclas y el botón izquierdo solo en las apps que elijas: por ejemplo, mover la imagen de Photoshop con el botón del medio.
- [new] App Toggle abre una app con un atajo, la trae al frente si ya está abierta o la minimiza si ya la estás usando (en Mac la oculta).
- [improved][win] Al soltar Win después de un atajo de Window Layouts o App Toggle ya no se abre el menú Inicio, y Win más una letra (como Win+E) se puede grabar para reemplazar el atajo de Windows.
- [new][win] Desktop Icons guarda dónde están los íconos del escritorio, una vez por día o cuando quieras con un nombre, y los vuelve a su lugar con un clic cuando Windows los desordena, con «Undo» para deshacer.
- [improved] La ventana es un poco más alta y ninguna página corta textos ni botones: si algo no entra, aparece la barra de scroll.
- [improved] La barra de scroll es más finita y General ya no tiene la sección «About».
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
