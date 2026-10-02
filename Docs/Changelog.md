# Changelog — LGA Mighty Tools

v1.11:

La ventana de Disk Space tenía el tamaño fijo del diseño y sus listas venían siempre ordenadas por peso.
Ahora se estira desde bordes y esquinas (mínimo 860×440, el ancho con el que la barra de abajo entra
entera en español) y recuerda su tamaño; en Windows lo hace el sistema, porque la ventana sin marco le
dice qué borde hay bajo el mouse. En Folders, Largest files y What changed, un click en el título de una
columna ordena por ella y otro invierte el sentido; cada lista recuerda su orden. Además, con el tamaño de
interfaz 1 o 2 los nombres y las rutas se cortaban con «…» aunque entraran: se medían con el ancho
redondeado hacia abajo y el recorte usaba el exacto. En macOS estirarla queda pendiente.
[ Disk Space - Ventana redimensionable y listas ordenables ]

v1.10:

En macOS la app no declaraba los .nk como documento: Finder no tenía ícono para ellos y macOS podía no
ofrecerla para abrirlos. El `Info.plist` ahora declara el tipo (importado, porque es de Foundry) con el
mismo ícono de documento de Nuke que en Windows, en `NukeScript.icns` dentro del bundle. Con rango
«Alternate» aparece en «Abrir con» pero no se queda con los .nk al instalarse: el doble click lo toma
solo con «Apply». Falta compilarlo y probarlo en una Mac.
[ Open in NukeX - Tipo .nk con icono en macOS ]

v1.09:

Con los .nk asociados a Mighty Tools, el Explorador los mostraba con el ícono de la app, que no se lee
como un script de Nuke. El ProgID apuntaba al primer ícono del exe. Ahora el exe trae embebido, como segundo
ícono (ID 101), el documento de Nuke que usaba Open in NukeX, y el `DefaultIcon` de los .nk lo nombra por
ese ID. No hay archivo suelto que instalar ni que borrar: se va con el ProgID al soltar o desinstalar. Una
asociación hecha con una versión anterior toma el ícono nuevo al volver a aplicarla.
[ Open in NukeX - Icono de documento de Nuke para los .nk ]

v1.08:

En una pantalla chica el tamaño de interfaz 2 dejaba la ventana con el borde de abajo cortado: con
1366×768 ni el 1 de fábrica entraba. Ahora la app lee el área útil de la pantalla principal antes de
arrancar y usa el tamaño más grande con el que la ventana más grande (960×676 lógicos) entra entera, sin
cambiar lo guardado, así que en una pantalla grande vuelve solo. En General, los tamaños que no entran
quedan apagados y una línea dice hasta cuál entra. `--ui-shot` acepta `--screen-area` para capturar ese
caso.
[ General - Tamano de la interfaz limitado por la pantalla ]

v1.07:

No había forma de agrandar la interfaz: textos e íconos se veían chicos en un monitor grande. Ahora la
página General tiene «Interface size», un switch con 0 (el tamaño del diseño), 1 y 2, que agrandan todo un
10 % y un 20 %, ventanas incluidas; de fábrica viene en 1. Usa el factor de escala global de Qt, así que el
diseño en píxeles lógicos no cambia y nada se corre; como Qt lo lee solo al arrancar, elegir otro tamaño
reinicia la app y vuelve a abrir la ventana donde estaba. El factor no pasa a lo que la app lanza (NukeX
también es de Qt). El ícono de la bandeja sale siempre del tamaño nativo. Se compararon todos los estados
en los tres tamaños y los dos idiomas.
[ General - Tamano de la interfaz: 0, 1 y 2 ]

v1.06:

Antes de borrar algo dudoso había que copiar rutas a mano para preguntarle a un asistente de IA si era
seguro. Ahora la ventana de limpieza tiene «Export for AI...»: arma un Markdown con la pregunta ya escrita
y solo lo elegido para borrar (lo tildado en Clean up, o lo seleccionado en Folders y Largest files), con
ruta, peso, archivos, último cambio y cómo se borra. De las carpetas más pesadas suma lo que tienen
adentro, y de un navegador o una app, qué carpetas de caché se vacían. Un cartel aclara que desde la app no
se envía nada y deja copiarlo o guardarlo. Sale en el idioma de la interfaz, sin el contenido de ningún
archivo. `--simulate-action diskSpace:cleanup-export` lo genera en modo de solo lectura.
[ Disk Space - Exportar lo elegido para preguntarle a una IA antes de borrar ]

v1.05:

Los tooltips de la app eran los nativos de Qt con un color encima, cuando la regla de las apps LGA pide el
tooltip propio. Venían de Nuke Shortcuts, que nunca lo tuvo. Ahora se usa `CustomTooltip`, traído de la
Base: globo con flecha, demora de 600 ms, se ubica arriba o abajo según el lugar y se oculta apenas el mouse
sale, con un click o con una tecla. Se migraron todos; los que solo repetían lo que el ícono ya dice (cerrar,
minimizar, ayuda, el lápiz del atajo) se quitaron, y un texto recortado muestra el suyo solo cuando no entra.
`tools\qa\check_tooltips.ps1` detecta cualquier tooltip nativo nuevo y `--ui-probe tooltip-hover` lo prueba
sin tocar el escritorio.
[ UI - Tooltips propios en lugar de los nativos de Qt ]

v1.04:

Disk Space avisaba que un disco estaba lleno, pero no decía qué lo ocupaba ni dejaba liberar nada. Ahora
el aviso de Windows trae «Free up space», y la fila de cada disco, un botón para explorarlo (y el mismo
link cuando está bajo). Abren una ventana que escanea el volumen entero sin administrador, con un motor
propio en paralelo (4,7 M de archivos en unos 7 s), y lo muestra en cuatro pestañas: Clean up, Folders,
Largest files y What changed. Clean up agrupa las cachés conocidas, ya tildadas, y las cosas del usuario,
sin tildar; cada categoría se abre para elegir renglón por renglón y se pueden agregar reglas de carpetas
propias. Todo borrado pasa por guardas sobre la ruta real: no sigue enlaces ni toca Windows, los
programas, el perfil o lo que está en uso. En macOS queda deshabilitado.
[ Disk Space - Ventana de limpieza: escaneo del disco, caches y carpetas por peso ]

v1.03:

En Disk Space el espacio libre no se destacaba y el límite de aviso solo se cambiaba escribiendo el número.
Ahora «29 GB free» va en negrita en la fila de cada disco, en gris o en ámbar. En el aviso de Windows, cuyo
cuerpo no admite negrita, la cantidad libre pasa al título, que Windows muestra en negrita. La marca del
límite en la barra se agarra y se arrastra: el número (GB o %) cambia en vivo y se guarda una sola vez al
soltar. `--ui-probe threshold-drag` lo prueba sin tocar el escritorio.
[ Disk Space - Espacio libre en negrita y limite arrastrable en la barra ]

v1.02:

El encabezado de cada página tenía el cuadro del ícono con 34 px fijos: terminaba a mitad de la primera
línea de la descripción, y el dibujo quedaba siempre en 16 px porque `ToolIcon` solo respetaba el ancho
pedido. Ahora el cuadro mide lo que suman el título y la primera línea, con las fuentes reales, y el
dibujo acompaña. Las descripciones de General, Open in NukeX, Folder Switch (con sus dos atajos) y Link
Redirector pasan a los textos del sitio, en inglés y en español. La flecha de los desplegables («Language»,
«Remind me every») ya no queda pegada al texto.
[ UI - Encabezados alineados, descripciones nuevas y flecha de desplegable con aire ]

v1.01:

La app estaba solo en inglés. Ahora General > App > Language permite elegir español y la interfaz cambia
en el acto, sin reiniciar: la ventana se rearma y el menú de la bandeja, la ayuda y los avisos salen en el
idioma elegido. Cada texto visible pasa por `I18n::tr`, con la tabla en `src/core/I18nSpanish.cpp`; lo que
falta queda en inglés, y `tools\qa\check_i18n.ps1` y el self-test lo controlan. Los controles propios de
Qt usan su traducción oficial. De paso se corrigió lo que traducir dejaba a la vista: Apply decidía
comparando el texto de un chip, había plurales rotos («1 Nuke versions found»), atajos escritos a mano en
los mensajes y etiquetas en castellano en un error del updater. Apply y la desinstalación del cliente viejo
siguen en curso aunque se cambie el idioma.
[ Idioma - Interfaz en español opcional, elegible desde General ]

v1.00:

Primera versión publicada. Disk Space avisaba según un intervalo de chequeo configurable, así que un disco
lleno podía pasar horas sin detectarse y el aviso no se podía posponer. Ahora el chequeo es fijo cada 15
minutos y «Check every» pasa a ser «Remind me every» (cada cuánto se repite el aviso de un disco que sigue
lleno). El toast trae un desplegable «Remind me again in» que pospone solo ese disco, y un click en el
cuerpo vuelve a la app, también desde el Centro de notificaciones y con la app cerrada. Para eso el primer
aviso registra el AUMID y el activador COM de la app, que `--uninstall-cleanup` borra al desinstalar. Open in NukeX pasa a ser la primera herramienta de la lista.
[ Release 1.00 - Recordatorio de Disk Space con posponer desde el aviso ]

v0.08:

Apply de Open in NukeX se negaba a asociar los `.nk` cuando la app corría desde una carpeta de build,
también en Windows. El cliente original solo frenaba en mac, donde se asocia la ruta del `.app` y el
próximo build la borra; en Windows el exe se reemplaza en el mismo lugar y la asociación sigue andando.
Ahora el aviso queda solo para mac: en Windows, Apply es una acción explícita del usuario y se hace desde
cualquier copia, como pide el contrato de las herramientas.
[ Open in NukeX - Apply desde una copia de build solo se frena en mac ]

v0.07:

Los usuarios del Open in NukeX viejo no tenían cómo pasar a Mighty Tools sin perder el doble click en
`.nk`. Ahora la instalación los migra: `--migrate-openinnukex` detecta el cliente viejo por su
desinstalador o por el ProgID que las dos apps comparten, prende Open in NukeX sin inicio con Windows y
toma los `.nk` reescribiendo ese ProgID, sin tocar la elección protegida de Windows. Al final, una casilla
ofrece quitar el cliente viejo (`--remove-old-client`, que espera a que termine y retoma los `.nk`); el
panel suma el botón «Uninstall old app». A quien nunca lo tuvo no se le cambia nada. La entrada de `.nk`
en Apps predeterminadas pasa a llamarse «Open in NukeX».
[ Open in NukeX - Mudanza del cliente viejo desde la instalacion ]

v0.06:

La ventana mostraba la barra de título de Windows (el marco se aplicaba antes de atender WM_NCCALCSIZE),
Match words no tomaba el teclado (el filtro de foco pisaba el focus proxy del campo) y los estados de
Open in NukeX y Link Redirector no se releían: ahora se releen al volver a la ventana, desde
UserChoiceLatest y por el nombre del desinstalador. La app ya no deja el registro sucio: `--uninstall-
cleanup`, llamado por el desinstalador, borra solo lo que apunta a este exe; cada herramienta registra
su propia entrada en Apps predeterminadas y soltar una no rompe la otra; Apply borra los restos del
cliente viejo. Un self-test prueba todo sobre un registro privado. El ejemplo de Match words es genérico.
[ Registro - Limpieza propia al desinstalar, ventana sin barra nativa y estados que se releen ]

v0.05:

Las notificaciones salían con el ícono genérico de Windows y el nombre del archivo como encabezado.
Ahora son un toast nativo con el ícono de la app en grande, el mismo mecanismo de PipeSync, para las
cinco herramientas; PowerShell corre sin ventana y se corta si la app se cierra con un aviso en vuelo. El exe lleva recurso de versión con el nombre visible.
Se cierran también las observaciones de las auditorías: la ayuda scrollea y entra en pantallas chicas,
la segunda copia trae la ventana al frente, la ventana se abre sola solo en el primer arranque, leer
settings no crea carpetas, Check now se deshabilita mientras chequea, un click afuera corta el grabador
de atajos, Disk Space conserva su historial de avisos, y el menú de la bandeja, el keycap del disco
desenchufado y el chip de plataformas quedan como el diseño.
[ Notificaciones y ayuda - Toast con el icono de la app y correcciones de las auditorias ]

v0.04:

Las cinco herramientas funcionan dentro de la app, cada una con su interruptor. Faltaba todo lo que va
arriba del contrato: la ventana con barra lateral, la bandeja con una sección por herramienta, General,
el primer arranque con todo apagado, la ayuda única y los ports de Nuke Shortcuts, Disk Space, Folder
Switch, Link Redirector y Open in NukeX, con el plugin de Nuke adentro del repo. La asociación de `.nk`
calcula el hash de Windows 11 en C++, sin .NET, y coincide con los hashes que guardó Windows. Lo apagado
no consume: 20 ciclos por herramienta sin fugas, 16 ms de CPU en 10 minutos de reposo, +2,2 MB. Corrige
también lo que v0.03 daba por hecho y no estaba: el self-test ahora sí recorre las pruebas de cada
herramienta.
[ Herramientas - Las cinco herramientas en la ventana con barra lateral ]

v0.03:

Contrato de las herramientas, antes de escribir la ventana. Cada una se describe con datos estáticos
(lo único que existe apagada) y un objeto vivo que el host construye al prenderla y destruye al
apagarla, con su panel creado recién al abrirlo. Fija también el orden de borrado, los pasos directos
de `.nk` y links con trabajo asincrónico (el envío a NukeX espera un socket), la política de choques de
atajos entre herramientas y dos banderas: una corrida automatizada no toca nada del sistema, y una copia
de desarrollo funciona pero no deja el sistema apuntando a ella. El self-test ya recorre las pruebas de
cada herramienta.
[ Arquitectura - Contrato de las herramientas ]

v0.02:

Vuelve el workflow de GitHub que compila la versión de mac y corre el self-test en un runner (se
dispara a mano). Había quedado afuera del arranque porque la credencial de GitHub no tenía permiso para
publicar workflows; con el permiso dado, se suma con el nombre nuevo de la app y del bundle.
[ CI - Build de mac en GitHub ]

v0.01:

Arranque del repo. Mighty Tools reúne en una sola app de bandeja cinco herramientas que hoy son apps
sueltas: Nuke Shortcuts, Disk Space, Open in NukeX, Folder Switch y Link Redirector. La base es una copia
de LGA Nuke Shortcuts v2.06 (arquitectura, capa de plataforma, Theme, updater y QA) con la identidad
nueva: exe `LGA_MightyTools.exe`, bundle `com.lga.mightytools`, instalador con AppId propio y carpeta
`C:\Portable\LGA\MightyTools`, y el ícono de Nuke Shortcuts. Suman el plan, el inventario de opciones de
las cuatro apps de origen, las decisiones tomadas (ventana con barra lateral, nada prendido de fábrica,
sin .NET, el plugin de Nuke dentro del repo) y la licencia MIT con los avisos de Qt e Inter.
[ Repo - Arranque de LGA Mighty Tools ]
