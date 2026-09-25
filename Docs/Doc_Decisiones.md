# Decisiones

Lo que solo Lega decide, con la numeración del plan (`Docs/Plan_MightyTools.md`, sección 8). Las
abiertas siguen con la opción reversible indicada hasta que Lega diga otra cosa.

## Tomadas

- **D-01 · Forma de la ventana: A, barra lateral** (2026-09-25). Lista de herramientas a la izquierda,
  con interruptor y línea de estado; el panel de la elegida a la derecha; «General» arriba de la lista.
- **D-03 · Sin .NET** (2026-09-25). El hash de UserChoiceLatest que hoy calcula el helper `LGA_WinSetFTA`
  (C#, .NET 9) se porta a C++ y se verifica contra el helper con las mismas entradas. Si la versión C++
  no coincide, Apply abre el selector «Abrir con» de Windows. Lleva el aviso de licencia MIT de PS-SFTA y
  DefaultApps.
- **D-04 · El plugin de Nuke vive en este repo** (2026-09-25). El Nuke Bridge (el plugin Python que corre
  dentro de NukeX) se copia adentro del repo y viaja embebido en el exe. El repo `LGA_OpenInNukeX` se
  archiva. El plugin conserva su propia versión, que es la que muestra el chip del bridge, y la carpeta
  instalada sigue siendo `<.nuke>/LGA_OpenInNukeX`. Se conserva la guarda: no se instala si esa carpeta
  tiene `QtClient/CMakeLists.txt` o `.git`.
- **D-06 · Nada viene prendido** (2026-09-25). El primer arranque muestra la ventana con todo apagado; el
  inicio con el sistema se activa al prender la primera herramienta.
- **D-10 · Repos viejos** (2026-09-25). `LGA_NukeShortcuts`, `LGA_FolderSwitch`, `LGA_LinkRedirector` y
  `LGA_OpenInNukeX` dejan de mantenerse; Lega los archiva y los pasa a privados cuando Mighty Tools
  funcione bien.
- **D-11 · Link Redirector se publica** en este repo (2026-09-25).

## Abiertas

- **D-02 · Numeración.** Versión continua de a centésimos desde 0.01, con 1.00 como primer release
  público. Mientras tanto: esa.
- **D-05 · Open in NukeX y Link Redirector apagados** con entradas del sistema. Mientras tanto: paso
  directo del host, sin cargar el módulo; nunca se pierde lo que el usuario abrió.
- **D-07 · Identidad en mac.** Mientras tanto: bundle id nuevo `com.lga.mightytools`.
- **D-08 · Idioma.** Mientras tanto: solo inglés.
- **D-09 · Soltar los registros al apagar.** Mientras tanto: se ofrece «Release .nk association» y
  «Remove as browser», con el paso directo como red de seguridad.
- **D-12 · Transición en mac de los usuarios de Open in NukeX.** Mientras tanto: siguen con el cliente
  viejo hasta la fase de mac.
- **D-13 · Resultado de «Check now».** Mientras tanto: en la fila; los errores siguen en cartel.
- **D-14 · Rutas en los diálogos.** Mientras tanto: un color, cada ruta en su línea.
- **D-15 · Menú de la bandeja.** Mientras tanto: con secciones por herramienta.
- **D-16 · Atajos de Folder Switch editables.** Mientras tanto: sí, con el grabador de Nuke Shortcuts.
- **D-17 a D-20 · Heredadas de Nuke Shortcuts**: cómo se guarda el punto del Dope Sheet, atajos solo con
  Nuke al frente, atajos por defecto en mac, inicio con la sesión en mac. Mientras tanto: porcentaje de
  la ventana de Nuke, atajos solo con Nuke al frente, `⌘⇧D` / `⌘⌥⇧D`, `SMAppService`.
