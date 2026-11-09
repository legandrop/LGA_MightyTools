# Decisiones

Lo que solo Lega decide, con la numeración del plan (`Docs/Plan_MightyTools.md`, sección 8). Las
abiertas siguen con la opción reversible indicada hasta que Lega diga otra cosa.

## Tomadas

- **D-77 · App liviana: la ventana se arma al abrirla y se suelta en la bandeja** (2026-11-08, Lega: «cambiar
  de tab tarda; la app tiene que quedar súper liviana, en procesamiento y en uso», y después «sin perder nada
  de UX, incluso en computadoras muy malas»). La ventana principal se arma la primera vez que se abre
  (bandeja, aviso, otra copia), cada página de herramienta recién cuando se elige, y la ventana se destruye a
  los 5 minutos de estar escondida; al volver se rearma igual: la página, el scroll de cada página ya vista y la
  ayuda abierta (~50 ms). Medido en Windows con la configuración de Lega: con la ventana abierta y todo visto,
  50 MB privados; soltada, 24 MB (antes de D-77 nunca bajaba de 50); sin abrirla nunca, 16,5 MB. Supuestos
  tomados por la sesión, reversibles (constantes en `src/app/AppController.cpp`):
  - **5 minutos** (`kReleaseAfterMs`): cerrar y reabrir enseguida no rearma nada.
  - **Nunca se suelta con algo a medio hacer:** un menú, un diálogo o el popup de Folder Switch abiertos, una
    herramienta que la escondió (el calibrador), el Apply de Open in NukeX, ni un panel que dice tener trabajo
    sin guardar (`PanelState`: un editor abierto, una ruta o carpeta elegida sin Save/Install, un reemplazo o
    un nombre a medio escribir, «Make Default» esperando a Windows hasta 15 minutos).
  - **Primera ventana nativa por adelantado** (`kWarmupDelayMs`, 4 s después del arranque; **confirmado por
    Lega el 2026-11-09**: «Dejarlo»): la primera ventana del proceso le cuesta a Windows ~105 ms una sola vez y
    se pagaba al abrir desde la bandeja. Ahora se crea y se destruye una sin mostrar; cuesta ~4 MB que igual
    quedaban al abrir la ventana por primera vez.
  - **Al soltarse se vacía la caché de fuentes de Qt** (4,4 MB; reabrir tarda ~12 ms más).
  - **La negrita del texto con formato va a 600** (SemiBold, la que trae la app), no a 700: pedir un peso o una
    familia que la app no trae («JetBrains Mono») hacía que Qt cargara todas las fuentes del sistema la
    primera vez (70 a 230 ms). Se nota un poco más liviana en las pistas de Nuke Shortcuts y Open in NukeX.

- **D-76 · Paletas de color, las de LGA GateBouncer** (2026-11-08, Lega: las mismas paletas y el mismo
  selector que GateBouncer v1.01). `src/ui/Palette.*` agrupa cada color de `Theme.h` y de la hoja en 18
  grupos con los ids de GateBouncer y deriva los tonos en HSL desde el base de cada grupo; las seis paletas
  de `resources/palettes/` son sus mismos JSON. General → App: Language, Colors, Zoom. Supuestos tomados sin
  Lega, reversibles:
  - **De fábrica, Violet** (la primera de `Palette::presetIds`), como en GateBouncer. El rosa de antes no es
    ninguna de las seis: con los colores del código (`--palette code`) la app queda idéntica a la v1.52.
  - **Las flechas cambian la paleta con el menú abierto** (en vivo; Enter la deja, Escape vuelve a la de
    antes). Con el menú cerrado no: en Mighty Tools ningún botón toma el foco del teclado.
  - **Los dos desplegables miden lo que pide el nombre más largo** (English, Español y las seis paletas en el
    idioma de la interfaz), no 130 fijos: 144 en inglés, 137 en castellano.
  - **El link lega.com.ar sigue con el color de los links**, no el de los rótulos del índice: con los
    colores del código tiene que seguir rosa.
  - **La base de «elegido» es `#682344`**, una referencia (no se usa): deja a sus tonos a la misma distancia
    que en GateBouncer, así el segmento elegido sale igual de claro en cada paleta.
    (2026-11-08, superado: la base de referencia no alcanzaba. Los tonos del acento y de «elegido» se calculan
    desde su equivalente de GateBouncer, `Role::gbTones`, y dan los mismos colores que allá en las seis paletas.)
  - **El menú resalta con «fila elegida»** (`@listSelected`, el violeta tenue), no con el color del botón
    principal.
  - La elegida va en `settings.ini` (`app/palette`). Editor de desarrollo: `MIGHTYTOOLS_PALETTE_EDITOR=1`.

- **D-75 · Dictation en macOS** (2026-11-07, Lega: «arrancá» el dictado en mac). Misma herramienta que
  D-72, en la rama `dictado-mac` hasta que Lega la apruebe. Supuestos tomados sin preguntar, reversibles:
  - **Atajo de fábrica `⌘⌥⇧H`** (Lega, 2026-11-07: el que él usa; el primero fue `⌃⌥H`). No `⌥` + letra
    (desde macOS 15 Carbon rechaza los atajos con solo Option), ni `⌃H` (borra en la Terminal), y los atajos
    de la app no admiten Espacio.
  - **Escritura con eventos de teclado** con el texto adentro, de a 16 caracteres, a la app del frente:
    no pasa por el portapapeles (no hace falta el «pegado sin historial» de Windows). Los saltos de línea
    van como Return y el tabulador como Tab. Esc corta también a la mitad; si la app del frente cambia,
    se corta ahí y el texto entero queda en «Recent».
  - **Permisos de macOS:** el de Micrófono la primera vez que se dicta (ese dictado no graba: hay que
    volver a apretar el atajo) y el de Accesibilidad para escribir en otras apps (el mismo que ya usan
    Folder Switch y Nuke Shortcuts); sin él el texto queda en «Recent» y macOS muestra su cartel.
  - **Motor** transcribe.cpp 0.4.1 de mac: Metal en Apple Silicon (2,5 MB) y solo CPU en Intel (1,6 MB,
    lento con este modelo). Mismo modelo que Windows. La carpeta por defecto es
    `~/Library/Application Support/LGA/LGA_MightyTools/models` (nunca dentro del `.app`).
- **D-74 · Meet Frame** (2026-11-06, Lega: «Meet Frame se va a llamar, implementalo»). Herramienta para
  compartir una parte de la pantalla, como Region to Share (GPL-3.0: solo se tomó la idea, ninguna línea
  de código), tapando en lo compartido los avisos, el escritorio y las apps elegidas. Investigación en
  `Docs/Investigacion_Compartir_Region.md`. Windows y macOS. Supuestos tomados sin preguntar, reversibles:
  - **Motor B en Windows** (captura del monitor con DXGI Desktop Duplication, GDI de respaldo, y lo
    tapado pintado liso), no las miniaturas de DWM: es el que funciona seguro sin el prototipo P1. La
    ventana compartida queda al fondo del orden Z (si queda debajo del escritorio, mejor: nadie la ve).
  - **Atajos** `Alt+Shift+Win+M` (mostrar u ocultar el marco; Lega, 2026-11-07: el que él usa) y
    `Alt+Shift+Win+P` (pausa, que haga juego; registrado solo con el marco abierto). En mac son `⌘⌥⇧M` (Lega, 2026-11-07: `⌘⌥M` es «minimizar todas las ventanas» de
    macOS; vale de fábrica el que él usa) y `⌘⌥⇧P` (Lega, mismo día: que haga juego).
  - **Forma libre de fábrica** (Lega, 2026-11-07: «todo bloqueado no está bien»): se estira a cualquier
    tamaño; «Lock the frame shape» (panel y menú Size del marco) la fija en 16:9, 4:3, 1:1 o la actual.
  - **Vienen prendidos** los avisos, el escritorio, Mensajería y Contraseñas; Correo viene apagado (se
    suele compartir el correo a propósito). WhatsApp y Telegram en el navegador se reconocen por el
    título y se tapa la ventana entera del navegador mientras esa pestaña está al frente.
  - **Ante la duda se tapa de más:** una ventana semitransparente o excluida de captura no cuenta como que
    tapa lo de abajo. En mac, una app que se abre después se suma al filtro en el siguiente segundo, y
    hasta entonces no sale.
  - Sin «No molestar» de Windows ni Focus de mac (no tienen API pública): los avisos se tapan en lo
    compartido, que es lo que importa.
- **D-73 · Rediseño «Pliego», Help por pantalla y herramientas en orden alfabético** (2026-11-03, Lega
  eligió entre cinco rediseños de la app entera). Cada herramienta es una hoja más clara sobre la mesa
  negra; la ficha elegida del índice lateral se une a la hoja; el índice va en orden alfabético y cada
  fila conserva ícono, estado y switch. Los colores tienen significado: cian =
  funciona, magenta = lo que hace el usuario (botón principal, links, ayuda abierta), amarillo = pide
  atención, rojo = error. Switch con perilla circular, teclas con borde inferior grueso, títulos en
  Bricolage Grotesque y el resto en Inter. Pedidos de Lega sobre el mockup: los títulos van en texto
  liso, **sin** el desregistro de colores (el logo de la barra de título sí lo conserva), y **los
  tamaños de letra no bajan** respecto de los de hoy con la UI al 100 % (como mucho medio punto). La
  ayuda deja de ser un cuadro con las diez herramientas: cada página trae su franja «How it works»
  plegable (opción B de la ronda 1), que el «?» abre o cierra; la
  versión, el autor y la web pasan al pie de General. Canvas con las opciones descartadas: link en
  `LGA_RepoTools/RepoRules_Shared/repos/LGA_MightyTools/DISENO.md`. **Ajustes de Lega del 2026-11-04,
  sobre la app ya armada:** la perilla del switch y todas las marcas vuelven a ser círculos (nada de
  rombos); sin la columna de iniciales en el índice; la ficha de arriba queda pareja con el borde de la
  hoja (una diferencia chica se ve como un error: o ninguna o una bien notoria); «How it works» arranca
  cerrada cada vez que se entra a una herramienta y no se recuerda.
- **D-72 · Dictation** (2026-11-03, Lega: dictado por voz propio en lugar del de Windows). Herramienta
  propia con su página. Motor transcribe.cpp 0.4.1 (CPU + Vulkan) y modelo Whisper large-v3 turbo Q8_0,
  los dos con licencia MIT: los baja el usuario con un botón que muestra antes tamaño y licencia, se
  verifican por SHA-256 y nunca viajan en el instalador; quien no dicta no baja nada. La carpeta por
  defecto es `<app>\models` (se borra al desinstalar) y se puede cambiar. El motor corre en un proceso
  aparte, verifica sus archivos antes de cargarse, se carga al prender la herramienta o con el primer
  dictado (nunca al arrancar la app: el audio espera mientras carga, ~1 s) y se descarga tras N minutos
  sin uso (10 por defecto). El texto se ve en vivo en una ventanita sin foco y se escribe al terminar en
  la ventana que estaba al frente con el «pegado sin historial» (render diferido, marcas que lo sacan del
  historial de Win+V y de la nube, Ctrl+V y lo copiado devuelto; tipear con `SendInput` Unicode se
  descartó porque el Bloc de notas cambiaba letras); sin voz no se escribe nada, y si la ventana cambió no se escribe y queda en «Recent». Con una ventana de
  administrador al frente el atajo no llega (Windows no le pasa las teclas a una app sin administrador)
  y se abre el dictado de Windows. Win+H por defecto (reemplaza el dictado de Windows), tocar o mantener,
  Esc cancela (también mientras escribe). Palabras propias para el modelo y reemplazos «X → Y» después.
  El historial vive solo en memoria. Solo Windows por ahora. Lega la aprobó y está en main (2026-11-03).
  Las palabras de fábrica (VFX y desarrollo: Nuke Studio, compo, DMP, comitear, pushear…) vienen puestas
  para todos, en un campo de varias líneas. Descartado por Lega: Parakeet y Voxtral como modelos
  opcionales, y dictar con una ventana de administrador al frente (sin ayudante elevado). macOS: D-75.
- **D-71 · Ejemplos apagados en App Toggle y Mouse Buttons, y el Explorador como toggle** (2026-11-03,
  Lega: «debe venir con la app ya puesto… es como un setting que viene por defecto», «pero apagado»).
  La primera vez que se prende cada herramienta (sin la clave en settings) trae ejemplos apagados: App
  Toggle, Win+E para XYplorer si está instalado o, si no, para el Explorador, y OBS con
  Ctrl+Alt+Shift+Win+1 aunque no esté instalado (en mac solo OBS); Mouse Buttons, la regla de Photoshop
  (botón del medio = Espacio + botón izquierdo). Son datos, no texto de la interfaz: borrarlos no los
  vuelve a traer. Con el Explorador, Win+E deja de abrir una ventana nueva cada vez: sin ventana de
  carpetas abre una, con una la trae, al frente la minimiza.
- **D-70 · Cachés de programas audiovisuales: leídas de sus preferencias, borradas solo por marca**
  (2026-11-03, Lega: «buscar dónde apuntan las cachés» en las preferencias de cada programa). La
  limpieza lee dónde configuró el usuario la caché de Nuke, Adobe, After Effects, Ableton, Topaz,
  REAPER, Unreal y Redshift, y reconoce por forma las de Resolve, Premiere y Blender. Nunca se borra la
  carpeta que dice una preferencia, solo sus subcarpetas conocidas con la marca del programa; sin
  marca no hay ítem (en Topaz Video 1.6.0 una carpeta de proyectos movida terminó vaciando media
  unidad). Lo regenerable va en «Safe to delete»; lo que cuesta rehacer (timeline y copias locales de
  Nuke, proxies, medios optimizados, previews, modelos de Topaz, DDC de Unreal) en «Yours to decide».
  Quedan afuera la carpeta de localización de Nuke (no tiene marca), Houdini (su temporal guarda la
  recuperación de escenas), Final Cut Pro, Mocha, Fusion, Maya y Avid.
- **D-69 · Instaladores huérfanos: solo los de programas que ya no están, con administrador**
  (2026-11-03, Lega: «Si están instaladas, no podemos borrarlos de ninguna manera. No deberían ni
  aparecer si la app está instalada»). La ventana de limpieza lista un `.msi` o `.msp` de primer nivel de
  `C:\Windows\Installer` solo si ningún producto ni parche lo reclama y el programa que instala (por
  ProductCode o UpgradeCode, en cualquier versión) no está instalado; ante la duda, no aparece. La
  consulta es completa o nada: si no cubre a todos los usuarios, no se lista ningún archivo. Va en
  «Yours to decide», destildado; «Clean up» pide administrador una vez y un paso elevado vuelve a
  verificar cada archivo con todos los usuarios antes de mandarlo a la Papelera. Es la primera excepción
  a D-27. Las copias repetidas de paquetes de programas instalados no se muestran.
- **D-68 · Mouse Buttons** (2026-11-02, Lega). Reemplaza X-Mouse Button Control con la forma mínima:
  reglas «app → botón → teclas y botón izquierdo mientras está apretado». Solo Windows por ahora. La
  regla se guarda por el nombre del exe y actúa solo con la app al frente y bajo el puntero.
- **D-67 · App Toggle** (2026-11-02, Lega). Un atajo por app: la abre si no está abierta, la trae al
  frente si está abierta sin foco y la minimiza si ya tiene el foco, como los AutoHotkey que reemplaza.
  Sin opción de maximizar. Windows y macOS. En macOS la oculta (como Cmd+H) en lugar de minimizarla:
  no pide el permiso de Accesibilidad y vuelve con todas sus ventanas (decidido de noche, Lega puede
  cambiarlo). Win + una letra o número reemplaza a propósito el atajo de Windows: no se marca como
  compartido.
- **D-66 · Desktop Icons** (2026-11-02, Lega; detalle en `Docs/Roadmap_Desktop_Icons.md`). Solo
  Windows. Si el escritorio está igual al del automático anterior, solo se actualiza su fecha; con una
  resolución distinta de la habitual se saltea el automático; restaurar es solo a pedido; «Organizar
  automáticamente» y «Alinear a la cuadrícula» vuelven como estaban en el snapshot.
- **D-65 · Sin herramienta de imagen de disco** (2026-11-02, Lega). La herramienta «Disk Image» que
  estaba en el roadmap queda descartada del todo.
- **D-64 · Animación «Suave» del switch** (2026-10-31, Lega eligió la B entre seis del mockup). La
  perilla desliza 160 ms y frena al llegar; el color se funde en el mismo tiempo; sin rebote ni
  cambios con el mouse encima. Solo anima un click del usuario.
- **D-63 · Switches en lugar de casillas** (2026-10-31, Lega). Toda opción de encender y apagar usa el
  switch (`SwitchRow`: texto y descripción a la izquierda, switch a la derecha), también donde el canvas
  dibujaba una casilla. La animación del switch la elige Lega sobre un mockup.
- **D-62 · General sin «About», ventana de 700 de alto y barra de scroll más finita** (2026-10-31,
  Lega). La tarjeta About no sumaba nada: la versión ya está en App y la ayuda en el «?» de la barra de
  título. La ventana principal pasa de 676 a 700 de alto para que Open in NukeX entre entera con el
  tamaño de interfaz 1 en español (en 1366×768 sigue entrando el 0, D-35). La barra de scroll vertical
  pasa de 8 a 5 px de grosor visible (Lega pidió la mitad o un 60%).
- **D-61 · Ventanas de administrador con un ayudante elevado, no con la app entera elevada**
  (2026-10-31, Lega). Con UAC encendido, Window Layouts no recibía los atajos ni podía mover el
  Administrador de tareas ni otras apps elevadas. Lega comparó «Always run as administrator» (como
  PowerToys) con un ayudante y eligió el ayudante: la app sigue sin elevar, así que navegador, Nuke y
  carpetas nunca se abren como administrador. Es un switch arriba del panel de Window Layouts, apagado por defecto; el
  ayudante es el mismo exe lanzado con «runas», solo mientras la opción y algún atajo estén encendidos.
  Una herramienta futura que necesite tocar ventanas elevadas (botones del mouse, App Toggle) suma su
  acción a este ayudante.
- **D-60 · macOS: `deploy.sh` termina siempre instalada y abierta** (2026-10-15, Lega: «todos los
  deploy.sh de mac deberían instalar en applications la app y dejarla abierta al terminar»). También
  cuando empaqueta o publica: reemplaza la regla anterior de que empaquetar o publicar no tocaba
  `/Applications`. El paso lo hace `tools/macos/install_to_applications.sh`, copia idéntica de la pieza
  canónica de la Base; `--no-install` y `--no-open` siguen, y `--install` queda sin efecto.
- **D-59 · La versión se sube al publicar, no en cada cambio** (2026-10-14, Lega). Como en FileManagerS3
  y PipeSync: `CMakeLists.txt` y `VERSION` guardan la última versión publicada y el primer encabezado del
  changelog, la que está en desarrollo. Las tandas suman entradas a esa versión sin subirla; al publicar,
  Lega corre `bump_version.sh` (mac) o `bump_version.bat` (Windows), que sube los dos archivos, commitea y
  pushea, y ofrece publicar. Un solo bump para Windows y macOS: las dos plataformas publican en el mismo
  release. Reemplaza a `sync_version`. Las v1.52 y v1.53, que nunca se publicaron, quedan juntas en v1.52.

- **D-58 · Totales de Clean up y agrupación de What changed** (2026-10-14, Lega). En Clean up, el chip
  de cada tarjeta y el tamaño de cada fila muestran «lo que se va de lo que hay» cuando no se va todo; sin
  nada tildado, el total va en gris. Una categoría con carpetas retenidas por apps abiertas tiene la casilla a
  medias y cuenta cuántas están en uso. What changed agrupa por zona (la carpeta más alta que cambió), lista
  adentro solo las carpetas más profundas con su ruta relativa y una fila «Otras carpetas en …», cierra con
  lo que ninguna zona explica y lleva arriba una barra con las tres zonas que más crecieron. Opciones A y 1
  de la propuesta.

- **D-57 · Presentación del recordatorio de Disk Space** (2026-10-11, Lega). Las dos acciones inferiores
  tienen el mismo ancho, separación y padding horizontal que los campos del plazo y el mínimo, y un
  hover visible. El aviso completo es un poco más compacto y todos sus textos bajan una unidad de
  tamaño. La acción que guarda el mínimo y aplaza el próximo aviso se llama «Remind me again» en inglés
  y «Volver a recordarme» en español. Esta decisión actualiza el tamaño y el nombre de D-51 sin cambiar
  lo que se guarda.

- **D-56 · Editor y nombre de los atajos de Window Layouts** (2026-10-11, Lega). El alta se presenta
  como «Nuevo atajo de ventana» en español y «Add windows shortcut» en inglés. Todo el panel y sus
  estados llaman atajos a los elementos guardados. Al abrir el editor desde el final de una lista
  larga, el contenido ajusta su altura a la página visible y vuelve el scroll al inicio. Se elimina
  el botón superior de regreso: «Cancelar» y «Guardar atajo» resuelven la salida.

- **D-55 · Choques informativos de Window Layouts** (2026-10-11, Lega). Cuando una disposición
  activa comparte un atajo detectable con otra app o el sistema, la barra lateral conserva el
  estado normal de la herramienta: ese uso es una elección del usuario. El aviso queda solo dentro
  del panel. Su punto y los marcadores junto a los interruptores son amarillos, con más espacio entre
  marcador e interruptor. Un fallo real de activación sí conserva el estado de error y su detalle
  por fila. Esta decisión ajusta la señal visual de D-54.

- **D-54 · Control individual de Window Layouts** (2026-10-11, Lega). Cada disposición nueva queda
  activada y ofrece un interruptor propio debajo del lápiz de edición. Ya no existe la pausa global.
  Al apagar una disposición se libera solo su atajo; las demás siguen activas. En Windows todas las
  disposiciones activas intentan capturar su combinación con prioridad, sin una casilla adicional.
  Cada fallo de activación aparece en su fila. Un punto rojo marca las combinaciones cuyo uso por
  otra app o por el sistema se puede detectar; el cartel superior resume cuántas hay. No se puede
  identificar de forma fiable al dueño ni detectar todos los hooks, y la prioridad no está
  garantizada. Esta decisión reemplaza el carácter optativo y la pausa global de D-53.

- **D-53 · Prioridad de atajos de Window Layouts en Windows** (2026-10-11, Lega). Cada disposición
  puede optar por intentar responder antes que otra app que use el mismo atajo. Por defecto queda
  apagado y siguen bloqueados los choques entre herramientas y disposiciones de Mighty Tools. La
  captura de teclado se crea solo mientras haya una disposición prioritaria activa y se suelta al
  pausar o apagar. No se mata ni se modifica el otro programa; se consume la pulsación elegida.
  Windows no informa de forma fiable quién tiene el atajo ni qué hace. La prioridad puede perder
  frente a atajos del sistema o a otros capturadores posteriores; Lega prueba las teclas reales.

- **D-52 · Window Layouts** (2026-10-11, Lega). Herramienta nueva, apagada y sin atajos ni
  disposiciones al instalar. Cada disposición tiene nombre, atajo y destino: ajuste rápido, selección
  independiente de fracción y posición horizontal y vertical (entero, mitad, tercio o cuarto), área
  dibujada con arrastre o límites de una ventana abierta. Los destinos se guardan como fracciones del
  área útil y se resuelven sobre el monitor de la ventana activa al pulsar el atajo. En Windows se usa
  el área de trabajo del monitor; en macOS, el área visible de la pantalla. Taskbar, Dock y barra de
  menús visibles quedan afuera aunque estén en otro borde; las franjas de activación de taskbar y Dock
  autoocultos se recuperan. Si la app ajena no acepta el tamaño o la posición, se intenta restaurar
  la geometría anterior y se muestra el error. También se puede guardar la acción de minimizar, sin
  destino geométrico. Una instalación nueva no importa atajos AHK automáticamente: se eligen en el
  panel y se informan los choques con otra herramienta o con el sistema. Ctrl+Home queda fuera de
  Window Layouts porque no es un atajo de ventanas. En la máquina de Lega se migraron a sus settings
  las doce acciones de ventanas: cuartos, tercios, mitades, tres cuartos, OBS y minimizar. En macOS mover ventanas ajenas requiere
  Accesibilidad; elegirlas puede requerir Grabación de pantalla. El selector no hace una captura del
  escritorio: superpone un contorno y actualiza las posiciones mientras está abierto.

- **D-51 · Editar el mínimo desde el recordatorio** (2026-10-11, Lega). Aviso propio con «Remind me
  in» y «Only below» en paralelo. El mínimo se escribe y conserva GB o porcentaje según ese disco.
  Dos botones de 36 píxeles: «Save and remind me» guarda el mínimo para los avisos siguientes y
  pospone solo el próximo; «Free up space» abre la limpieza sin guardar la edición. Sin «Dismiss»:
  la cruz cierra sin guardar. El aviso propio reemplaza al nativo de Disk Space para permitir la
  distribución aprobada; los avisos generales de la app siguen usando el sistema.

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
  cuánto pesa y ofrece abrir la limpieza de Windows; no eleva permisos. Excepción: los instaladores
  huérfanos de `C:\Windows\Installer` (D-69).
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
  **Límite por pantalla** (Lega, mismo día): un tamaño vale solo si la ventana más grande (960×700
  lógicos) entra entera en el área útil de la pantalla principal; si no, se usa el mayor que entra, sin
  tocar lo guardado (en una pantalla grande vuelve solo), y General apaga los que no entran. En 1366×768
  solo entra el 0. Desde el 2026-11-03 (Lega: «tamaño de la interfaz… es medio raro») se llama «Zoom» y
  muestra 100 %, 110 % y 120 %; se sigue guardando el nivel 0/1/2.
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

- **D-38 · macOS: se prueba instalada** (2026-10-02, Lega). En mac la app se prueba siempre desde
  `/Applications`, con `deploy.sh` (Release, Qt oficial 6.5.3, bundle autocontenido, firma «LGA Code
  Signing»): el inicio con la sesión, los `.nk`, el navegador por defecto y el permiso de Accesibilidad
  dependen de la ruta y la firma de la copia instalada.
- **D-39 · macOS: la barra de menú abre la ventana, sin menú** (2026-10-02, Lega: las opciones del menú
  «no tienen sentido» ahí). Un click en el ícono abre la ventana; salir es el botón «Quit» de la tarjeta
  App en General (y Cmd+Q con la ventana al frente). Pausar y calibrar viven en el panel de cada
  herramienta. En Windows el menú de la bandeja sigue igual.

- **D-40 · Folder Switch en macOS** (2026-10-02, Lega). Se porta completo (cambio automático, atajo
  manual y recientes) y reemplaza a Default Folder X, que Lega apaga al probarlo. Toma la carpeta solo
  del Finder. Lleva la carpeta a los diálogos nativos de macOS (Abrir/Guardar de cualquier app) y al
  navegador de archivos de Nuke (Qt).
- **D-41 · Cada herramienta, su carpeta en la `.nuke`** (2026-10-02, Lega: «deberíamos ser prolijos, como
  eran Open in NukeX y los toolpacks»). Open in NukeX instala `LGA_OpenInNukeX` (el servidor de `.nk`) y
  Nuke Shortcuts instala `LGA_NukeShortcuts` (el keyframe), cada una con su línea en el `init.py` y desde
  su propio panel. Quien usa solo Nuke Shortcuts no se lleva el servidor. El keyframe funciona en
  cualquier Nuke con interfaz, no solo en NukeX. El bridge 1.85 borra el keyframe que dejó la 1.84.
- **D-42 · «Frame Dope Sheet» dentro de Nuke** (2026-10-02, probado por Lega con una sonda: «funciona
  perfecto»). El plugin `LGA_NukeShortcuts` 1.01 registra también este atajo. Busca el Dope Sheet por su
  widget, le hace creer que el mouse entró y manda Ctrl+A y F: sin mover el puntero, sin click y sin
  calibrar. Con el Dope Sheet escondido no hace nada (opción a de Lega). Cada atajo deja su propia marca,
  así un plugin 1.00 no apaga el macro del Dope Sheet. Con el plugin 1.01 instalado no se muestra la
  calibración. Sin plugin queda el macro calibrado de respaldo.
- **D-43 · Disk Space en macOS: acceso total al disco** (2026-10-02, Lega). Para escanear la Mac entera,
  la ventana de limpieza pide una vez el acceso total al disco con una tarjeta y un botón que abre
  Ajustes en el lugar justo (como DaisyDisk). Sin ese permiso escanea igual lo que no está protegido, sin
  carteles carpeta por carpeta, y lo protegido queda como «sin acceso».
- **D-45 · macOS: sin «Interface size»** (2026-10-02, Lega). En la Mac la app va siempre al tamaño del
  diseño (el 0) y General no muestra el control. Qt en macOS dibuja mal el texto con una escala
  fraccionaria (el 1 de fábrica da 2,2 en una Retina: perdía la negrita y dejaba una línea de texto
  cortada debajo de lo libre en Disk Space); macOS ya tiene su escala en Ajustes > Pantallas. Windows
  sigue con 0, 1 y 2 (D-35).
- **D-46 · macOS: en el Dock mientras la ventana está abierta** (2026-10-02, Lega: «con command tab tampoco
  aparece, eso no está bien»). La app es de barra de menú (sin Dock), y al pasar a otra app su ventana
  quedaba tapada sin forma de volver salvo el ícono de la barra. Con una ventana de la app abierta
  (la principal o la de limpieza) aparece en el Dock y en Cmd+Tab, con su menú; al cerrarlas vuelve a
  ser solo de barra de menú. Al pasar a otra app queda segunda en Cmd+Tab, como cualquier app (v1.29).
- **D-47 · El release de macOS sale completo** (2026-10-04, Lega). La versión de Mac no se publica hasta
  tener las tres cosas: el ZIP, el DMG y el actualizador de macOS. No sale un DMG solo: quien lo instalara
  quedaría con una app que no busca versiones nuevas y tendría que volver a bajarlo a mano cada vez.
- **D-50 · En macOS no hay mudanza del OpenInNukeX viejo** (2026-10-04, Lega: «nadie tiene OpenInNukeX
  en mac, así que no es problema»). No se hace `--migrate-openinnukex` ni «Uninstall old app» para
  macOS, y PipeSync ofrece Mighty Tools en mac a todos, con la misma regla que en Windows.
- **D-49 · Se publica desde cualquiera de las dos máquinas** (2026-10-04, Lega: «arreglá todo lo que
  falta»). `instalador.bat` (Windows) y `./deploy.sh --publish` (macOS) publican en el mismo release
  `v<versión>` en cualquier orden: el primero crea el tag y el release, el segundo suma su paquete. Cada
  plataforma puede quedar una versión atrás de la otra: el actualizador, el sitio y PipeSync leen el
  último paquete de cada una (`assetLatest`).
- **D-48 · En el sitio va con las apps** (2026-10-04, Lega). La tarjeta pasa de «Nuke & Hiero Tools» a
  «VFX & Media Apps»: es una app de escritorio y tres de sus cinco herramientas no dependen de Nuke. La
  etiqueta «Nuke + app» de la tarjeta se queda. El sitio suma macOS recién cuando exista el release de Mac.

## Abiertas

- **D-44 · Reglas de limpieza en macOS** (2026-10-02, tomadas por la sesión y auditadas; esperan a Lega).
  - Las actualizaciones que las apps ya bajaron (`*.ShipIt`, Sparkle, `*-updater/pending`,
    `Mozilla/updates`) van en «Safe», tildadas.
  - Cada hijo de `~/Library/Caches` que ninguna regla cubre va en «Yours» («Other app caches»),
    destildado: ahí también hay datos (PipeSync, Spark, JetBrains). Nunca los de Apple ni `LGA`.
  - Sin grupo de administrador en mac. Los informes de fallos de más de 30 días van en «Safe».
  - Un archivo trabado desde el Finder («Locked») no se borra (en Windows se le saca el solo-lectura).
  - Las copias de iPhone y iPad solo se informan, con «Review»; se administran en el Finder.

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
  - Las leyendas de Codex y del volcado de memoria dicen lo que la app hace hoy, no lo del diseño. La
    línea informativa de los parches de Windows Installer la reemplazó la categoría de D-69.

- **D-02 · Numeración.** Versión continua de a centésimos desde 0.01, con 1.00 como primer release
  público. Mientras tanto: esa. Cuándo se sube: D-59.
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
