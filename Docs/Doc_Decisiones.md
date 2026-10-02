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
- **D-21 · Mudanza de los usuarios de Open in NukeX por PipeSync** (2026-09-26). PipeSync reemplaza la
  tarjeta de LGA OpenInNukeX por la de LGA Mighty Tools (badge NEW). La migración la hace la instalación
  de Mighty Tools: a quien ya tenía el cliente viejo, Open in NukeX le queda prendido y tomando los `.nk`,
  con su configuración, sin activar el inicio con Windows (se activa si prende otra herramienta). El
  plugin de Nuke lo instala y actualiza el Nuke Bridge embebido (D-04), no PipeSync. `LGA_OpenInNukeX`
  pasa a privado después de un período de espera. Detalle en el plan, sección 9.
- **D-22 · Recordatorio de Disk Space** (2026-09-30). El chequeo de los discos es fijo, cada 15 min. Lo
  que se configura es «Remind me every» (15 min a 6 h, 15 min por defecto): cada cuánto se repite el
  aviso mientras el disco sigue bajo. El aviso de Windows trae «Remind me again in» con ese valor
  preseleccionado. Elegir otro valor pospone solo el próximo recordatorio de ese disco. El aviso queda en
  pantalla hasta que se elige algo.
- **D-23 · Los avisos se anotan ante Windows también desde un build** (2026-09-30). El AUMID y el
  activador COM de HKCU los escribe el primer aviso, también en una copia de `build\`. Es la única
  excepción a «desde un build no se escribe nada solo». `--uninstall-cleanup` los borra si apuntan a
  este exe o a un `LGA_MightyTools.exe` que ya no existe.
- **D-08 · Idioma: inglés por defecto, español opcional** (2026-09-30). Se elige en General > App >
  Language y la interfaz cambia en el acto. El español va en infinitivo o impersonal, nunca vos ni tú.
  Quedan en inglés los nombres de las herramientas y de la app, Tools, App, About, knob, key, keyframe,
  Dope Sheet y Explorer; las pantallas de Windows y macOS se citan con su nombre real en español. Los
  tamaños siguen con punto decimal. El título de la bienvenida es «LGA Mighty Tools» en los dos idiomas.
  El instalador, lo que se escribe en el registro y el plugin de Nuke quedan en inglés.
- **D-24 · Limpieza de discos: motor propio, sin administrador, sin gráficos** (2026-09-30). Disk Space
  abre una ventana aparte por disco, desde el aviso de disco bajo y desde su panel. El escaneo es propio
  (lista carpeta por carpeta en paralelo, sin leer la MFT), así no pide administrador. Nada de mapas de
  bloques: listas ordenadas por peso, en pestañas (Clean up, Folders, Largest files, What changed). La
  variante de dos columnas quedó descartada.
- **D-25 · Qué borra «Clean up»** (2026-09-30). Exactamente lo tildado, de forma definitiva, con una
  confirmación que lo lista. «Safe to delete» (cachés que se regeneran, Papelera incluida) arranca
  tildado; «Yours to decide» (cosas del usuario) arranca destildado. Cada categoría se abre y se elige
  renglón por renglón; la casilla de la categoría tiene tres estados.
- **D-26 · Borrado a mano** (2026-09-30). En Folders y Largest files, lo elegido va a la Papelera por
  defecto; «Delete permanently» es un botón aparte. Si la Papelera no lo acepta, no se borra.
- **D-27 · Lo que pide administrador, en una segunda etapa** (2026-09-30). Por ahora la ventana informa
  cuánto pesa y ofrece abrir la limpieza de Windows; no eleva permisos.
- **D-28 · Reglas de carpetas del usuario** (2026-09-30). «Add a folder rule» declara descartable una
  carpeta, o toda carpeta con cierto nombre dentro de otra (los `build` de `C:\Portable`). Aparecen en
  «Yours to decide», sin tildar.
- **D-29 · «What changed»** (2026-09-30). Cada escaneo completo guarda un resumen (carpetas de 100 MB o
  más) y la pestaña muestra qué creció o se achicó desde el anterior.
- **D-30 · Las reglas de limpieza nombran la app que limpian** (2026-09-30). Decir de qué programa es una
  caché es parte del producto; no tiene que ver con la regla de no mencionar herramientas de desarrollo.
- **D-32 · En el sitio, una sola tarjeta** (2026-09-30). Las tarjetas de Nuke Shortcuts, OpenInNukeX,
  FolderSwitch y Link Redirector se reemplazan por la de LGA Mighty Tools, que reutiliza la de Nuke
  Shortcuts (la app heredó su ícono). Se publica antes del release: muestra «Pronto» hasta que exista la
  descarga.
- **D-33 · Tooltips propios** (2026-10-01). Ningún tooltip nativo de Qt: todos pasan por `CustomTooltip`,
  el mismo de las demás apps LGA. Siempre prendidos y con demora; no se suman opciones a General. Única
  excepción: el ícono de la bandeja, cuyo tooltip lo dibuja el sistema.
- **D-34 · «Export for AI...»** (2026-10-01). La ventana de limpieza exporta lo elegido para borrar, y
  solo eso, para preguntarle a un asistente de IA si es seguro antes de hacerlo. Formato Markdown, con la
  pregunta ya escrita, en el idioma de la interfaz. La app no envía nada: el usuario copia el texto o
  guarda el archivo. La interfaz dice «your AI assistant», sin nombrar marcas. El aviso de privacidad
  (lleva nombres de carpetas y de archivos, no su contenido) va en la letra chica del cartel, sin
  casilla ni opción de ocultar nombres.
- **D-35 · Tamaño de la interfaz** (2026-10-01, pedido de Lega). Opción general «Interface size» con 0,
  1 y 2 (factor 1,0, 1,1 y 1,2), en un switch segmentado con el mismo campo que el desplegable de Idioma (borde, radio, alto y letra; el elegido con los tokens de «Elegido»), y **1 de
  fábrica**. Las capturas de QA siguen en 0 (el tamaño del diseño aprobado) salvo `--ui-scale`. Aplicada con el factor de escala global de Qt (`QT_SCALE_FACTOR`, fijado
  antes de crear la app y borrado enseguida para que no lo herede NukeX). No se reescalan las medidas a
  mano: con el factor de Qt todo crece parejo y el layout no cambia. Elegir otro tamaño reinicia la app.
  **Límite por pantalla** (Lega, mismo día): un tamaño vale solo si la ventana más grande (960×676
  lógicos) entra entera en el área útil de la pantalla principal; si no, se usa el mayor que entra, sin
  tocar lo guardado (en una pantalla grande vuelve solo), y General apaga los que no entran. En 1366×768
  solo entra el 0.
- **D-36 · Ventana de limpieza estirable y listas ordenables** (2026-10-01, pedido de Lega: «todo está
  muy rígido»). La ventana deja el tamaño fijo del canvas (960×620 sigue siendo el de fábrica): se estira
  desde bordes y esquinas, con mínimo 860×440, y recuerda su tamaño. Las columnas NO se ensanchan a mano
  (Lega lo descartó); el nombre toma lo que sobra. Folders, Largest files y What changed se ordenan con
  un click en el título de cualquier columna; el segundo click invierte. Una columna nueva arranca por lo
  más grande (o lo más nuevo, o lo que más creció) y el nombre de la A a la Z. Cada lista recuerda su
  orden.
- **D-37 · «Add keyframe» dentro de Nuke** (2026-10-02, elegido y probado por Lega). El plugin de Nuke
  (bridge 1.84, `LGA_KeyframeToggle.py`) registra el atajo dentro de Nuke y pone o borra la key del
  knob bajo el mouse, o del que tiene el foco; con el knob abierto en varios campos, solo la del canal
  del campo. El atajo y el prendido salen del settings de la app. La app no toma el atajo en un Nuke que
  tenga el plugin activo (marca `nuke_set_key/<pid>`); en uno sin plugin sigue el clic derecho + Enter.

## Abiertas

- **D-31 · Desvíos de la ventana de limpieza respecto del diseño aprobado.** Mientras tanto, lo
  implementado:
  - El grupo de administrador solo informa (D-27).
  - Ventana de tamaño fijo, 960×620 (el diseño la dibuja de 577 de alto).
  - Una categoría de más de 12 renglones muestra «Show N more»; en Folders se listan todas las
    subcarpetas, sin el renglón «smaller items».
  - Después de limpiar, lo limpiado desaparece y queda el cartel verde, sin etiquetas «Cleaned».
  - Las reglas de carpetas se titulan «Folders named build» (no «Build folders»), y cada una tiene una
    «×» para quitarla, que el diseño no dibuja. `C:\temp` no viene de fábrica: se agrega como regla.
  - Las previews de Adobe Bridge están en «Yours to decide» porque purgarlas puede perder etiquetas.
  - No hay reglas para DaVinci Resolve, NuGet, conda, las miniaturas ni los logs de Windows. La
    categoría es «Shader caches and crash dumps».
  - Categoría nueva «Folders marked as cache» (carpetas que su programa marca con `CACHEDIR.TAG`).
  - Una caché de uv que no está en su lugar de fábrica ni en `UV_CACHE_DIR` se muestra con las de
    Python pero arranca destildada (el diseño la trae tildada): puede ser la caché privada del runtime
    de otra herramienta.
  - Las imágenes de máquina virtual avisan que se pierde lo que las sesiones guardaron adentro.
  - Los temporales se limpian a los 7 días sin cambios y los volcados a los 30; un renglón cuyo
    programa está abierto queda bloqueado.
  - Las leyendas de Codex, de los parches de Windows Installer y del volcado de memoria dicen lo que la
    app hace hoy, no lo del diseño.

- **D-02 · Numeración.** Versión continua de a centésimos desde 0.01, con 1.00 como primer release
  público. Mientras tanto: esa.
- **D-05 · Open in NukeX y Link Redirector apagados** con entradas del sistema. Mientras tanto: paso
  directo del host, sin cargar el módulo; nunca se pierde lo que el usuario abrió.
- **D-07 · Identidad en mac.** Mientras tanto: bundle id nuevo `com.lga.mightytools`.
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
