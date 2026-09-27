# Changelog — LGA Mighty Tools

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
