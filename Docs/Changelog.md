# Changelog — LGA Mighty Tools

v1.53:

Windows: con el release existente sin el instalador de Windows y el changelog
adelante, el menú de `instalador.bat` sugería `git checkout v<versión>` para
sumarlo con el código de ese release. En Windows ese camino no llega a ningún
lado: publicar exige estar en `main`, y desde el tag el menú solo deja el
instalador local. Se sacó la nota; Windows saltea esa versión y sale en la
siguiente, con el bump.

[ Instalador - Sin la nota de git checkout en Windows ]

Windows: el menú de publicación de `instalador.bat` tenía tres detalles. Con la
versión ya publicada salía antes del menú «Para reemplazarlo, correr con
--replace» aunque lo que correspondía era el bump: ahora `--replace` se sugiere
solo cuando el changelog no abrió una versión nueva. Los commits locales sin
pushear recién se detectaban después de compilar, al ir a crear el tag: ahora se
miran antes del menú, como en la Mac, y solo se ofrece local o cancelar. Y si
había varios motivos para no publicar se mostraba uno solo: ahora se listan
todos, uno por línea, sin la línea en blanco de más.

[ Instalador - Menu de publicacion: commits sin pushear y todos los motivos ]

Windows: `instalador.bat` decide la publicación antes de compilar con el mismo
menú de una tecla que `deploy.sh`. Mira el release en GitHub y ofrece publicar
v<versión>, sumar el instalador al release que creó la Mac, o, si el changelog
va adelante, el bump (`bump_version.bat --commit --no-publish`, con commit y
push) y publicar esa; además instalador local o cancelar. Rama distinta de
`main`, árbol sucio o GitHub no listo ya no abortan: el menú dice el motivo y
ofrece solo local o cancelar. Se van las preguntas S/N de publicar y la de
seguir local; sin consola y con `--local` no cambia nada. Con `--replace` se
ofrece reemplazar el instalador ya publicado.

[ Instalador - Menu de publicacion de una tecla antes de compilar ]

Solo Mac: `./deploy.sh` sin flags ya no pregunta «¿publicar?» a ciegas ni pasa
el control a `bump_version.sh` cuando falta el bump. Llama a
`tools/macos/publish_release.sh --decide` (pieza de la Base v0.158, re-copiada),
que mira el release en GitHub antes de compilar y pregunta con una tecla, sin
Enter: publicar o sumar macOS al release; si la versión ya está publicada para
macOS o falta el bump, el bump (`BUMP_CMD` en `release.conf`, con commit y push)
y publicar esa; build local; o cancelar. Si lo elegido no pasa el preflight,
ofrece seguir local. Las preguntas S/N del script pasan a una tecla. Probado en
la Mac con un clon descartable; falta `instalador.bat`.

[ Deploy Mac - Menu de publicacion de una tecla antes de compilar ]

La publicación de macOS pasa a la pieza común de las apps LGA:
`tools/macos/publish_release.sh`, copia idéntica de la de LGA_Base_QT_C_Py, con
lo propio de esta app en `tools/macos/release.conf` (repo, versión de a
centésimos, `bump_version.sh --check`, `SHA256SUMS` fusionado con el instalador
de Windows, What's new y el certificado fijado, que `deploy.sh` ahora lee de ahí).
`deploy.sh` la llama antes de compilar y otra vez con el .zip y el .dmg armados,
y conserva lo suyo: build universal, self-test de las dos mitades, verificación
del ZIP y del certificado de la hoja. Cambia de comportamiento en dos cosas: si
la publicación falla, la app se instala igual y el script sale con error, y
suma `--no-publish` y `--yes` (sin preguntar en el control de las notas).
Nombres de archivos, tag y release, iguales.

[ Deploy Mac - Publicar con la pieza comun publish_release.sh ]

Publicar desde la pregunta de `./deploy.sh` (sin flags) ya no deja la app sin
instalar: esa rama apagaba la instalación, en contra de D-60. Ahora termina
instalada en `/Applications` y abierta, igual que con `--publish`.

[ Deploy Mac - Publicar desde la pregunta tambien instala ]

`./deploy.sh` termina siempre con la app instalada en `/Applications` y abierta,
también con `--zip`, `--dmg` o `--publish` (antes empaquetar o publicar no
instalaba nada, D-60). La instalación la hace `tools/macos/install_to_applications.sh`,
copia idéntica de la pieza canónica de LGA_Base_QT_C_Py (cierre por ruta, `ditto`,
`lsregister`, verificación de la firma y `open`), después del cierre con `--quit`
de la copia instalada y la de `build/`. `--install` queda sin efecto. Además
`compilar.sh` reconfigura si el SDK de macOS cacheado ya no es el actual: después
de actualizar Xcode el build fallaba con "'type_traits' file not found".

[ Deploy Mac - Instalar en Applications y abrir tambien al publicar ]

`./deploy.sh` sin flags e `instalador.bat` preguntan siempre si publicar en
GitHub y, si falta el bump, ofrecen correrlo; antes `deploy.sh` publicaba solo
con `--publish` e `instalador.bat` armaba lo local sin preguntar.

[ Deploy - Preguntar siempre si publicar ]

El bump de versión podía meter en su commit trabajo sin commitear: commiteaba
`CMakeLists.txt` y `VERSION` enteros aunque trajeran otros cambios, y recién
miraba la identidad de git después de haber escrito los archivos. Ahora
`bump_version` no arranca si esos dos archivos tienen cambios sin commitear o si
la identidad no es la de legandrop, avisa qué commits locales salen en el mismo
push, y `--check` exige además que el bump esté commiteado, así `deploy.sh
--publish` e `instalador.bat` no publican un bump a medias. Son los mismos
controles que ya tienen los bumps de MediaTools y ThetaExplorer.

[ Versión - Bump más estricto antes de commitear y publicar ]

v1.52:

Cada tanda subía la versión con `sync_version`: el número avanzaba aunque nada
se publicara, y entre la v1.51 y el próximo release ya iban dos versiones que
ningún usuario vio. Además, sin un paso de publicación propio, los archivos de
macOS dejaron de subirse desde la v1.33. Ahora la versión se maneja como en
FileManagerS3 y PipeSync: `CMakeLists.txt` y `VERSION` guardan la última versión
publicada y el changelog abre la siguiente, donde se juntan las entradas. Al
publicar se corre `bump_version.sh` o `bump_version.bat`, que sube esos dos
archivos, commitea y pushea solo ellos y ofrece publicar: `deploy.sh --publish`
en macOS e `instalador.bat` en Windows, sobre el mismo release. `--check` dice
si la versión está lista: sin bump, `deploy.sh --publish` no publica e
`instalador.bat` arma solo el instalador local. `sync_version` se retira y lo
de la v1.53 pasa a la v1.52.

[ Versión - Subirla solo al publicar, con bump_version ]

La ventana de Disk Space mostraba en Clean up el total de cada tarjeta y de cada
categoría, pero no cuánto se iba a borrar: con apps abiertas, «App caches»
marcaba 3.25 GB con la casilla llena y se borraban 4 MB, porque el estado de la
casilla no contaba lo bloqueado. Ahora el chip de la tarjeta y el tamaño de cada
fila dicen «lo que se va de lo que hay», la casilla queda a medias y la fila
cuenta cuántas carpetas están en uso. What changed listaba carpetas anidadas en
una lista plana (Codin incluía a PipeSync_2 y a sus builds) y la columna sumaba
más que el disco. Ahora agrupa por zona: la carpeta más alta que cambió, adentro
solo las más profundas con su ruta relativa y «Otras carpetas en …», al final lo
que ninguna zona explica, y arriba una barra que reparte el crecimiento.

[ Disk Space - Mostrar lo elegido y agrupar los cambios por zona ]

Window Layouts respondía al teclado físico, pero dejaba pasar las mismas
combinaciones enviadas por Stream Deck. El receptor de prioridad descartaba
toda pulsación simulada; por eso Ctrl+Alt+Shift+Win+O llegaba a Windows y abría
Outlook en lugar de aplicar la disposición guardada. Ahora admite las macros
externas y distingue las teclas que genera Mighty Tools mediante una marca
propia, para no volver a capturar las combinaciones que devuelve a otra app.
El estado de los modificadores sigue los eventos del teclado, incluidos ambos
lados y sus liberaciones, para reconocer secuencias rápidas sin depender del
estado asíncrono anterior. Se conserva una sola activación por pulsación y
se intercepta también su liberación. Las pruebas recorren el receptor real
con eventos en memoria, sin inyectar teclas ni mover ventanas del usuario.

[ Window Layouts - Admitir atajos enviados por Stream Deck ]

v1.51:

El recordatorio de Disk Space tenía los botones inferiores con menos padding que
los campos superiores y con una separación distinta: los bordes no coincidían.
Además, una regla de fondo más específica impedía que el hover de esos botones
se viera. Se igualan el ancho, la separación horizontal y el padding de ambas
filas, y el hover gana ahora sobre el fondo normal de cada botón. El aviso se
compacta levemente: menor ancho, icono, espaciados y un nivel menos de tamaño
en todos sus textos, incluida la lista del plazo y el mensaje de error. El botón
de confirmación se llama «Remind me again» / «Volver a recordarme» para expresar
la acción de posponer el aviso. Su funcionamiento sigue guardando el mínimo
editado y el plazo elegido para ese disco.

[ Disk Space - Alinear y compactar el recordatorio ]

v1.50:

Al elegir crear un atajo desde el final de una lista larga de Window Layouts,
el editor conservaba el desplazamiento de la lista y la altura de esa página.
Por eso podía abrirse sobre un tramo vacío y obligar a desplazarse manualmente
hacia arriba. El contenedor ahora calcula su altura según la página visible y
lleva el scroll al comienzo al entrar al editor. Se quita el botón superior
«Disposiciones guardadas», que duplicaba la salida ya ofrecida por «Cancelar»
y «Guardar». Lega eligió llamar a esta acción «Nuevo atajo de ventana» en
español y «Add windows shortcut» en inglés; el resto del panel y sus estados
usan «atajo» de forma consistente. No cambian las combinaciones, áreas ni el
registro de teclas guardadas.

[ Window Layouts - Abrir el editor arriba y nombrar atajos de ventana ]

v1.49:

La v1.48 señalaba cinco combinaciones compartidas con un estado amarillo en la
barra lateral, aunque eran atajos que el usuario había elegido mantener activos.
El panel ya explicaba esos choques como información, pero los marcadores de las
filas eran rojos y quedaban demasiado cerca de cada interruptor. Window Layouts
vuelve a mostrar en la barra lateral su cantidad normal de disposiciones listas
cuando solo hay combinaciones compartidas; los errores reales de activación
siguen requiriendo atención. Dentro del panel, los puntos de los atajos
compartidos usan ahora el mismo amarillo que el aviso superior. Se amplía de
cinco a diez píxeles la separación entre cada punto y su interruptor, sin
cambiar el registro, la prioridad ni el estado guardado de las disposiciones.

[ Window Layouts - Tratar los atajos compartidos como información ]

v1.48:

Window Layouts solo mostraba el último error de registro porque cada fallo pisaba
el anterior. El botón «Pausar» suspendía todas las disposiciones sin indicar su
alcance. Cada fila tiene ahora un interruptor individual, activado al crearla;
al apagarlo se libera únicamente ese atajo. Los fallos se guardan por disposición
y se explican en su propia fila, mientras el cartel superior muestra el total.
Las combinaciones que el sistema permite detectar como ocupadas llevan un punto
rojo junto al interruptor y un aviso informativo. En Windows, toda disposición
activa intenta responder con prioridad, sin una casilla adicional; otro hook o el
sistema todavía pueden imponerse. Se migran los ajustes anteriores conservando
las disposiciones, se elimina la pausa global y se comprueba que dos fallos
simultáneos no se oculten entre sí.

[ Window Layouts - Activar disposiciones por separado y mostrar cada choque ]

v1.47:

El editor de Window Layouts mostraba los ajustes rápidos y los ejes horizontal y
vertical con el aspecto gris nativo de `QComboBox`, distinto de los desplegables
oscuros del resto de Mighty Tools. Además, la rueda podía cambiar una opción al
pasar sobre esos campos sin una elección consciente. Los tres controles usan
ahora el mismo campo y menú de opciones marcables que ya se emplea en General y
Disk Space; la disposición solo cambia cuando se elige un elemento del menú.
Se conservan los quince ajustes rápidos y las combinaciones de mitades, tercios
y cuartos de ambos ejes. El recordatorio editable de Disk Space era el único
`QComboBox` restante: ahora ignora la rueda y conserva su plazo hasta que el
usuario elige otro. La sonda del recordatorio comprueba ese comportamiento con
un evento de rueda real.

[ Window Layouts - Unificar desplegables y bloquear cambios por rueda ]

v1.46:

La lista de Window Layouts mostraba cada atajo como una cadena de texto apagada,
con un botón «Editar» separado al final de la fila. Se veía distinta de Nuke
Shortcuts y costaba reconocer las teclas de un vistazo. Las disposiciones guardadas
usan ahora la misma fila de atajos: una cápsula por tecla y el lápiz inmediatamente
a la derecha. La miniatura del área y la descripción siguen presentes; el lápiz
abre el editor completo de esa disposición, donde también se cambia el atajo y su
prioridad. La fila compartida admite esa acción sin alterar el grabador directo que
usan Nuke Shortcuts y otras herramientas. Los nombres largos se abrevian en la lista
para que las teclas y el lápiz permanezcan visibles. Se revisó una combinación de
cinco teclas con un nombre largo en una captura aislada y se comprobó el diseño
junto a Nuke Shortcuts en el mismo tamaño de ventana.

[ Window Layouts - Unificar filas de atajos con Nuke Shortcuts ]

v1.45:

Window Layouts rechazaba las combinaciones que otra aplicación ya había registrado,
aunque la intención era reemplazar los atajos de ventanas del script anterior. El
registro normal de Windows no permite apropiarse de una combinación ocupada ni
identificar con certeza al programa o la acción que la usa. Cada disposición ofrece
ahora «Try to take priority over other apps», desactivado de fábrica. Al elegirlo,
Window Layouts captura esa combinación con un hook de teclado en un hilo dedicado,
sin alterar los atajos de otros módulos; los suelta al pausar, borrar o apagar la
herramienta. El editor conserva una combinación ocupada para poder elegir la opción,
y avisa de la acción habitual de Win+Alt+Abajo sin atribuirle falsamente el dueño
actual. Si falla la instalación del hook, el guardado se revierte. La prioridad no
puede ser absoluta frente a Windows ni a un hook instalado después. Las acciones
reales quedan para la prueba de Lega.

[ Window Layouts - Permitir prioridad voluntaria de atajos en Windows ]

v1.44:

El modo local del instalador terminaba inmediatamente después de generar el archivo
y su SHA256SUMS. Por eso `instalador_local.bat` omitía las dos preguntas que sí
aparecían en el flujo habitual: instalar ahora y mostrar el archivo en Windows
Explorer. Se mueve el salto de salida a continuación de ambas preguntas. El modo
local sigue sin consultar Git ni GitHub y nunca ofrece commit o publicación. La
protección para `--no-run` y para una ejecución sin consola interactiva conserva
el comportamiento anterior: no abre el instalador ni Explorer. Se actualizan la
ayuda del script y el README para describir el recorrido real. La compilación del
paquete y la generación del hash permanecen obligatorias antes de ofrecer
cualquiera de las dos acciones; si fallan, el script termina sin preguntas.
La prueba local detectó que `Get-FileHash` podía no resolverse al invocar
PowerShell desde `cmd`; el hash ahora se calcula con las clases .NET integradas.

[ Build - Ofrecer instalar y mostrar el paquete en el modo local ]

v1.43:

La revisión de los atajos originales mostró que Ctrl+Home no reposiciona ventanas: se
había agregado por error al editor y a los registros de teclado de Window Layouts.
Se retira ese soporte y se deja el atajo en su script original. Los doce atajos de
ventanas incluyen diez destinos de tamaño y posición, minimizar y el tamaño para OBS;
el módulo ahora permite guardar y ejecutar también la acción de minimizar. La
configuración personal de Lega recibe los doce destinos como fracciones del área útil:
los llamados «2/3» del script eran en realidad tres cuartos. El script de ventanas
queda comentado para que no vuelva a registrar atajos al iniciar sesión. La instancia
elevada anterior no puede descargarse desde un proceso normal y conservará los atajos
hasta que termine la sesión. El movimiento y la minimización reales los prueba Lega.

[ Window Layouts - Migrar los atajos de ventanas y conservar Ctrl+Home en AHK ]

v1.42:

Al revisar los atajos de los scripts que Window Layouts reemplaza, se encontró que Ctrl+Home
no podía registrarse desde el editor: la lista compartida de teclas admitía letras, números,
funciones y flechas, pero no Home. Se agrega esa tecla al formato de atajo, al registro nativo
de Windows y macOS y al mensaje de ayuda del grabador. El self-test verifica que se puedan
interpretar las tres combinaciones anteriores: Ctrl+Alt+L, Ctrl+Alt+Shift+L y Ctrl+Home.
No se asignan automáticamente al encender la herramienta; cada disposición sigue vacía
hasta que el usuario elija su destino y atajo. La prueba de movimiento real y de los
conflictos con otros programas queda para Lega.

[ Window Layouts - Admitir Ctrl+Home entre los atajos ]

v1.41:

La primera compilación de Window Layouts en macOS no terminaba: el SDK entrega los tipos
de valor de Accesibilidad como constantes enteras, mientras las funciones de lectura y escritura
de geometría exigen el tipo enumerado `AXValueType`. Se convierte cada constante al tipo que
espera la API antes de leer posición y tamaño o crear los valores nuevos. No cambia el cálculo
del área útil ni el funcionamiento de Windows. La tanda anterior ya compilaba y pasaba el
self-test en Windows; esta corrección habilita comprobar la construcción completa en macOS
sin mover ventanas ajenas. La interacción real con permisos de Accesibilidad y las posiciones
del Dock siguen reservadas para la prueba de Lega con la app instalada.

[ Window Layouts - Corregir compilacion de macOS ]

v1.40:

Los atajos de reposicionamiento seguían en scripts externos y no se podían configurar desde Mighty
Tools. Se agrega Window Layouts, apagada y sin disposiciones iniciales: cada destino permite combinar
mitades, tercios y cuartos de manera independiente en horizontal y vertical, usar un ajuste rápido,
dibujar un área o tomar los límites de una ventana abierta. Los atajos se registran con el servicio
compartido y muestran los choques; al apagar la herramienta se liberan. El destino se calcula sobre
el área útil del monitor de la ventana activa al pulsar, respetando taskbar, Dock y barra de menús
visibles, incluso en otros bordes, y recuperando la franja de activación de barras autoocultas.
Si una aplicación rechaza la geometría se informa y se intenta restaurar la anterior. El selector
actualiza los límites de ventanas abiertas y borrar una disposición exige confirmación. Se agregan
traducciones, capturas y pruebas de geometría y de rechazo de registro.

[ Window Layouts - Configurar atajos y areas por monitor ]

v1.39:

El llamador del instalador local se había creado como `instalador_loca.bat`, sin la última letra.
Se corrige el nombre a `instalador_local.bat` y se actualiza su referencia en el README. Sigue
llamando al mismo `instalador.bat --local`, conservando los argumentos y el código de salida.
No cambia el modo local ni el flujo de publicación: el paquete y SHA256SUMS se generan igual,
sin preguntas de instalación ni de GitHub. Se comprueba la llamada desde una carpeta distinta
con espacios en la ruta y la devolución del error del script principal.

[ Build - Corregir el nombre de instalador_local.bat ]
v1.38:

Generar un instalador para probar cambios locales requería pasar por el flujo de publicación:
`--no-run` evitaba ejecutarlo, pero seguía controlando Git, la autenticación de GitHub, las notas y
el estado del release. Además exigía main y un árbol limpio, aunque no se quisiera publicar nada.
Ahora `instalador.bat --local` saltea todo ese preflight y conserva únicamente la preparación Release,
el control de versión del binario, la compilación con Inno Setup y la generación de SHA256SUMS.
Termina al generar los archivos, sin ofrecer instalar, revelar, commitear ni publicar. El nuevo
`instalador_loca.bat` llama a ese modo y devuelve su código de salida. Se conservan los controles
y las preguntas del flujo habitual de publicación. El README incluye ambas formas de uso.

[ Build - Generar el instalador local sin publicacion ]

v1.37:

El aviso de Disk Space permitía posponer un recordatorio, pero para cambiar el mínimo de espacio
había que abrir el panel. Ahora usa una ventana propia, porque el aviso nativo de Windows no permite
poner dos campos en paralelo. «Remind me in» y «Only below» comparten una fila; el mínimo es editable
y conserva la unidad del disco, GB o porcentaje. «Save and remind me» guarda ese mínimo y pospone
solo el próximo aviso del mismo disco. «Free up space» abre su limpieza sin guardar la edición,
y la cruz cierra sin cambios: se elimina el botón redundante de descartar. Los dos botones miden
36 píxeles. El aviso nace solo cuando hace falta, se abre sin activar la ventana, desaparece al
apagar la herramienta y encola los otros discos. Se verifican validación, persistencia, unidades y
capturas en ambos idiomas sin tocar ajustes reales.

[ Disk Space - Cambiar el minimo desde el recordatorio ]

v1.36:

El repo llevaba una copia propia del runner que corre los exes de prueba en un escritorio aparte
(`tools\qa\run_headless.ps1`, con su lista `net_allowed_paths.txt`). Era igual a la de la plantilla
`LGA_Base_QT_C_Py` salvo una línea de ayuda, y una copia así se desactualiza sin que nadie lo note: un
arreglo del runner no llegaba acá. Se borran los dos archivos y las pruebas pasan a usar el de la
plantilla por ruta relativa, `..\LGA_Base_QT_C_Py\tools\qa\run_headless.ps1` (el repo de la plantilla va
clonado al lado de este). Ningún script del repo lo llamaba: cambian la sección de pruebas de
`Docs/Plan_MightyTools.md` y un comentario del código. Sin cambios en la app.

[ QA - El runner sin escritorio se usa desde la plantilla ]

v1.35:

En Windows, `deploy.bat` todavía abría la copia de `deploy\` con `start` y una ruta relativa: lanzada
desde la consola que corría el script, la app quedaba dentro del árbol de procesos de esa
consola y se cerraba con ella, como pasaba con `compilar.bat` antes de la v1.34. Ahora la abre
`explorer.exe` con la ruta completa, porque `explorer.exe` no usa el directorio actual. La app nace con
cwd System32 y sin el entorno del script, tampoco el `PATH` con Qt que `deploy.bat` arma para
`windeployqt`: no lo necesita, las DLL ya están en `deploy\`. `instalador.bat` no cambia: corre el
instalador en primer plano a propósito, para que las preguntas de publicación aparezcan recién cuando
el instalador termina.

[ Build - deploy.bat abre la app con explorer.exe ]

v1.34:

En Windows, `compilar.bat` lanzaba la app con `start` desde la consola que lo corría: la app quedaba
dentro del árbol de procesos de esa terminal y se cerraba con ella, sin error ni log. Ahora la abre
`explorer.exe`, fuera de ese árbol. `--sim-slow` sigue con `start`, porque `explorer.exe` no acepta
`/LOW` ni `/AFFINITY`. La app nace con cwd System32 y sin el entorno del script, igual que cuando
arranca con la sesión.

[ Build - compilar.bat abre la app con explorer.exe ]

v1.33:

En la Mac, «Check now» y el chequeo automático fallaban siempre con «Could not check for updates»
(`networkError=99`), también en la 1.31 publicada. El paquete no traía el backend de TLS de Qt:
`tools/macos/podar_bundle.py` deja solo los plugins que la app usa y, hasta la v1.30, la versión de Mac
no hablaba con la red, así que `tls/` se descartaba; sin él ninguna conexión HTTPS arranca («No functional
TLS backend was found»). Las pruebas del actualizador no lo vieron porque iban contra un servidor local
sin HTTPS. Ahora la poda conserva `libqsecuretransportbackend.dylib` y el `--self-test` comprueba que haya
TLS; como `deploy.sh` lo corre sobre el bundle armado antes de empaquetar, un paquete sin TLS ya no se
publica. Quien instaló la 1.31 en Mac tiene que bajar esta versión a mano una vez.
[ macOS - El paquete trae TLS: vuelve el chequeo de versiones ]

v1.32:

La versión de Windows quedaba sin comprobar cada vez que se trabajaba desde la Mac: el código nuevo del
actualizador no se había compilado ahí e `instalador.bat`, cambiado en la v1.31, nunca había corrido. Se
suma un build de Windows en un runner de GitHub (`.github/workflows/build-windows.yml`, a mano): compila
con el mismo Qt y MinGW, corre el `--self-test`, ejecuta `instalador.bat` en modo local (sin consola no
pregunta ni publica), instala en silencio el instalador generado, corre el self-test de la copia instalada
y la desinstala. Sobre la v1.31 dio 0 fallas, y el análisis reconoció el release creado desde la Mac. En
`instalador.bat`, si falla la creación del release y la otra plataforma ya subió sus paquetes, no se
ofrece borrar el tag (se llevaría ese release). La mudanza de mac del OpenInNukeX viejo se descarta
(D-50). Sin cambios en la app.
[ Windows - Build y prueba del instalador en un runner ]

v1.31:

La v1.30 solo podía publicar la versión de Mac sobre un release que Windows ya hubiera creado, e
`instalador.bat` fallaba si el tag existía: los dos paquetes tenían que salir en un orden fijo. Ahora los
dos scripts publican en cualquier orden: el primero crea el tag y el release, el segundo sube lo suyo,
fusiona `SHA256SUMS` sin pisar las líneas de la otra plataforma y publica las notas de `Docs/WhatsNew.md`.
`deploy.sh` corre el `--self-test` de las dos mitades (Apple Silicon e Intel) antes de empaquetar. En el
actualizador: no se ofrece ni se instala una versión mientras Disk Space está borrando, el cartel de
progreso de la descarga se ve en la Mac aunque la ventana principal esté cerrada, y los motivos por los que
una instalación no arranca salen traducidos. `instalador.bat` no se pudo ejecutar desde la Mac: queda
revisado por lectura y pendiente de su primera corrida en Windows. D-49.
[ Release - Publicación en los dos órdenes y ajustes del actualizador ]

v1.30:

La app ya funcionaba en la Mac, pero no había forma de publicarla ni de actualizarla: `deploy.sh` solo
instalaba la copia local y el actualizador se compilaba solo en Windows. Ahora `deploy.sh --zip --dmg` arma
el ZIP y el DMG universales (Apple Silicon e Intel) y `--publish` los sube, firmados con el certificado
propio, a un release que ya tenga el instalador de Windows. El actualizador corre en las dos plataformas:
elige su paquete por familia en el manifiesto y, en macOS, un script reemplaza la app instalada con dos
renombres y la vuelve a abrir; si algo falla, queda la versión anterior. La prueba de punta a punta contra
un servidor local destapó un error publicado desde la 1.00: el hash esperado se borraba antes de empezar a
bajar, y toda actualización terminaba en «missing integrity digest», también en Windows. Corregido. D-47.
[ macOS - Paquete universal, publicación y actualizador ]

v1.29:

Con la v1.28, al abrir la ventana desde el ícono de la barra y pasar a otra app, Mighty Tools quedaba
última en Cmd+Tab en vez de segunda (Lega). La app ya era la activa (como app de barra de menú) cuando
pasaba a app normal, así que activarla de nuevo no era un cambio para el selector, que la agregaba al
final. Ahora, al pasar a app normal, se le da un instante de frente al Dock y se vuelve a activar la app:
el selector registra una activación de verdad. Reproducido y verificado en la instalada con capturas del
selector (abierta desde la barra de menú, TextEdit al frente: antes última, ahora segunda).
[ macOS - Segunda en Cmd+Tab al pasar a otra app ]

v1.28:

En la Mac, al pasar a otra app la ventana de Mighty Tools «desaparecía»: no se escondía (sigue en
pantalla), quedaba tapada, y como la app es de barra de menú (`LSUIElement`) no estaba en el Dock ni en
Cmd+Tab: la única forma de volver era el ícono de la barra (Lega). Ahora, mientras haya una ventana de la
app abierta, la política de activación pasa a la de una app normal (Dock, Cmd+Tab y menú propio) y al
cerrarlas vuelve a accesorio (D-46, `WindowActivation::setDockIconVisible`). Con menú propio, «Quit»
llega como `QEvent::Quit`, que primero cierra las ventanas y la principal se esconde en vez de cerrarse:
se la esconde antes para que la salida siga. Probado en la instalada: abierta = Foreground, cerrada =
UIElement, Quit sale.
[ macOS - En el Dock mientras la ventana está abierta ]

v1.27:

El chequeo de updates corría una sola vez, 15 s después de arrancar, y la app vive en la bandeja días
enteros: no se enteraba de una versión nueva hasta reiniciar. Ahora `UpdateService` lo repite cada 3 horas
(±15 min al azar), medido contra el reloj con un tick de 5 min para que al volver de una suspensión corra
enseguida, y reintenta a los 10 min si falló por red. Cada chequeo periódico consulta la casilla «Check for
updates at startup», así que apagarla vale también para la sesión en curso. «Later» (o cerrar el cartel)
pasa a posponer el automático 1 día, guardado en `updates/snoozeUntil` de `settings.ini`, igual que
«Remind me later» en las demás apps: sin eso el periódico reabría el cartel cada 3 horas. El menú y
«Check now» lo ignoran.
[ Updates - Chequeo cada 3 horas y Later de 1 dia ]

v1.26:

En la Mac, el panel de Disk Space mostraba una línea de texto cortada debajo de «126 GB free of 926 GB»
y el «free» sin negrita (Lega). No era ningún widget: `grab()` de la tarjeta salía perfecta. Era el
tamaño de interfaz 1 (D-35), que fija `QT_SCALE_FACTOR=1.1`: en una Retina eso da una escala de 2,2 y Qt
en macOS pinta mal el texto con escalas fraccionarias. Con el 0 se ve bien (comprobado en la app
instalada). Ahora en macOS el tamaño de interfaz no se ofrece y la app va siempre al tamaño del diseño
(D-45, `UiScale::supported()`); en Windows nada cambia. También: el fixture del panel de Disk Space usa
discos con forma de mac en las capturas de mac.
[ General - Sin tamaño de interfaz en la Mac ]

v1.25:

La auditoría del resultado de la v1.24 la aprobó sin bloqueantes; se corrigieron sus observaciones antes
de la prueba de Lega. Sin acceso total al disco, armar las guardas leía el xattr de Escritorio y
Documentos y listaba `~/Library/CloudStorage`, que son carpetas privadas (podía salir un cartel): ahora no
se tocan, y lo que el escaneo no lee tampoco se puede borrar ni exportar. La biblioteca de Fotos, la de
Música y la de TV quedan protegidas. Borrar definitivo un archivo abierto ya no pasa en mac. `O_NOFOLLOW`
no hacía nada con la barra final de la ruta. La fila de lo no legible decía «sin administrador»; en mac
habla de datos del sistema. Las exclusiones de las reglas se piden en cada armado, y «en uso» no hace
`realpath` por archivo (la mitad del tiempo en una carpeta grande).
[ Disk Space - Correcciones de la auditoría de la Mac ]

v1.24:

La ventana de limpieza de Disk Space no existía en la Mac: el borrado, la Papelera, las rutas protegidas
y las reglas eran solo de Windows. Ahora anda en macOS. Borrado con `unlink`/`rmdir` y la guarda de
corrida automatizada; como en mac borrar un archivo abierto no falla, antes de vaciar una carpeta se mira
con libproc si algo de adentro está abierto y, si lo está, se saltea entera. Papelera propia (`~/.Trash`,
`.Trashes/<uid>`), solo si es una carpeta de verdad del usuario. Reglas de mac (navegadores de los dos
lados, apps Electron, actualizaciones bajadas, pip, npm, Homebrew, Xcode) con bloqueo por identificador de
app; el resto de `~/Library/Caches` va destildado en «Yours» porque ahí también hay datos. Acceso total al
disco (D-43): sin él, no se entra a lo privado y sale una franja con el botón a Ajustes. «Show in Finder»,
ventana estirable, textos de mac. Propuesta auditada (3 bloqueantes corregidos), self-test de mac.
[ Disk Space - Ventana de limpieza en la Mac ]

v1.23:

En la Mac, el escaneo de Disk Space tardaba 3,5 minutos en el disco entero y contaba los datos del
usuario dos o tres veces: entraba también por `/System/Volumes/Data` (el volumen de datos tiene el mismo
`st_dev` que `/`, así que no se lo reconocía como otro volumen) y por `/.nofollow`. Además pedía cada
archivo con su propio `fstatat`. Ahora `DirEnumeratorMac` usa `getattrlistbulk` (muchas entradas por
llamada, como `FileFullDirectoryInfo` en Windows), saltea los puntos de montaje por `ATTR_DIR_MOUNTSTATUS`
y los alias de la raíz, y marca como nube lo que no está bajado (`SF_DATALESS`). Medido en una Mac M1 Max
con 4,2 M de archivos: 19 s y 776 GB, igual que el sistema. Es el piso de APFS: solo listar los nombres
ya lleva el 40 %. También: Frame Dope Sheet probado y aprobado por Lega en la Mac (v1.22).
[ Disk Space - Escaneo rápido en la Mac ]

v1.22:

En la Mac, Frame Dope Sheet seguía con el macro calibrado: el plugin `LGA_NukeShortcuts` 1.01 no
registraba ese atajo en macOS y la app mostraba la calibración, porque el encuadre dentro de Nuke solo
se había probado en Windows. Nada del método es de Windows (widget `Dope_Sheet`, ventana OpenGL
incrustada, Enter y movimiento falsos, Ctrl+A y F por eventos de Qt, donde Ctrl es Cmd en mac), así que
ahora el plugin lo registra también en la Mac y la app esconde la calibración igual que en Windows. Se
habilita para la prueba de Lega en la Mac. El plugin sigue en 1.01: en ninguna Mac hay una 1.01
instalada.
[ Nuke Shortcuts - Frame Dope Sheet dentro de Nuke también en la Mac ]

v1.21:

Cada compilación mostraba un aviso de AutoMoc: `OpenInNukeXPanel.cpp` incluía `OpenInNukeXPanel.moc`
aunque ya no tenía ninguna clase con `Q_OBJECT` (se sacó en la tanda del idioma y la línea quedó). Se
borró el include. Sin cambios para el usuario.
[ Build - Sin el aviso de AutoMoc de Open in NukeX ]

v1.20:

Frame Dope Sheet era un macro: click en el punto calibrado del Dope Sheet, Ctrl+A y F. Fallaba si el
panel cambiaba de lugar o estaba escondido, y obligaba a calibrar. Nuke manda las teclas al panel donde
entró el mouse, no al que tiene el foco. Ahora el plugin `LGA_NukeShortcuts` 1.01 registra también este
atajo dentro de Nuke: busca el widget del Dope Sheet, le manda un Enter y un movimiento de mouse falsos y
después Ctrl+A y F, sin mover el puntero. Con el Dope Sheet escondido no hace nada. Cada atajo deja su
propia marca para la app (D-42), y con el plugin 1.01 instalado se esconde la calibración.
[ Nuke Shortcuts - Frame Dope Sheet dentro de Nuke ]

v1.19:

Las v1.17 y v1.18 probadas en la Mac: el bridge 1.85 borró el keyframe que había dejado la 1.84 en
`LGA_OpenInNukeX`, Nuke Shortcuts instaló `LGA_NukeShortcuts` con una sola línea en el `init.py`, y el
atajo, la pausa y los chips anduvieron (Lega). Lo único que fallaba era de las capturas: en mac la `.nuke`
de prueba de los paneles de Nuke Shortcuts y Open in NukeX salía con forma de Windows
(`C:/Users/you/.nuke`). Ahora cada plataforma usa la suya (`/Users/<usuario>/.nuke` en mac); la app real ya
mostraba bien la ruta.
[ Nuke Shortcuts - Capturas con la .nuke de cada plataforma ]

v1.18:

Nuke Shortcuts y Open in NukeX figuraban «Activa» y «Asociado» aunque faltara su plugin dentro de Nuke, y
sin él la herramienta no está completa (Lega). Ahora Nuke Shortcuts dice «Plugin missing» en la barra
lateral y su tarjeta de estado ofrece «Install»; Open in NukeX, con los `.nk` asociados pero sin el bridge,
dice «Bridge missing». Las dos tarjetas de plugin muestran «Not installed» en ámbar, como «Not associated»,
y Open in NukeX muestra la `.nuke` con las barras de Windows. El estado del plugin se vuelve a mirar al
traer Nuke al frente y después de instalar.
[ Nuke Shortcuts - Sin plugin la herramienta no figura activa ]

v1.17:

El keyframe de Nuke Shortcuts se instalaba dentro de la carpeta de Open in NukeX (`LGA_OpenInNukeX`, bridge
1.84): para tenerlo había que instalar Open in NukeX y llevarse su servidor de `.nk`. Ahora cada
herramienta tiene su carpeta en la `.nuke` con su línea en el `init.py` (D-41): Nuke Shortcuts suma la
tarjeta «Nuke plugin» que instala `LGA_NukeShortcuts` (versión propia 1.00), y el bridge 1.85 de Open in
NukeX vuelve a traer solo el servidor y borra el keyframe que dejó la 1.84 (solo si lo reconoce como
suyo). La lógica de instalar un plugin en la `.nuke` pasó a `core/NukePlugin`, compartida por los dos.
Además, el chip de Folder Switch dice «Win · mac».
[ Nuke Shortcuts - Plugin de Nuke en su propia carpeta ]

v1.16:

Folder Switch no existía en macOS (D-40). Ahora hace lo mismo que en Windows: al ir del diálogo al Finder
y volver, el diálogo salta a la carpeta del Finder; ⌥⌘O lo hace de inmediato y ⌥⇧⌘O abre las carpetas
recientes. Anda en los diálogos nativos de macOS (Cmd+Shift+G, la ruta y Return) y en el navegador de
Nuke (la ruta en su campo, como en Windows). La carpeta se le pregunta al Finder por Apple Events: el
permiso se pide en otro hilo, porque mandado desde la interfaz la congelaba mientras el cartel estaba
abierto. El popup de recientes en mac es una ventana de herramienta: como popup, macOS nunca le daba el
teclado. Probado por Lega con Nuke 17 y TextEdit.
[ Folder Switch - Port a macOS ]

v1.15:

En macOS la app no compilaba (el updater de Windows quedaba afuera del `#ifdef`) y, compilada, fallaba
en lo básico: Link Redirector no podía ser navegador (el bundle no declaraba http/https), Disk Space no
veía el disco del sistema (el `/` sellado es de solo lectura), abrirla desde Finder con la copia corriendo
no hacía nada, un `.nk` o un link que la lanzaba cerrada se perdía y no tenía ícono. Corregido todo, y en
mac se prueba instalada (D-38): `deploy.sh` nuevo con el Qt oficial 6.5.3, bundle autocontenido y firma
estable para que el permiso de Accesibilidad sobreviva a cada versión. El click en la barra de menú abre
la ventana y salir es «Quit» en General (D-39). «Open Settings» de Accesibilidad hace un solo paso.
[ macOS - La app compila, se instala y anda en la Mac ]

v1.14:

«Add keyframe» fallaba con el campo del knob en foco: hacía clic derecho, flecha y Enter, y con el campo
en edición el clic derecho abre el menú de texto. Ahora lo resuelve Nuke: el plugin (bridge 1.84, archivo
nuevo `LGA_KeyframeToggle.py`) registra el atajo dentro de Nuke, busca el knob bajo el mouse (o el de la
etiqueta, o el que tiene el foco) y pone la key en el frame actual, o la borra si ya hay una. Un knob con
un solo campo queda colapsado; uno abierto por canal recibe la key solo en ese canal. El atajo y el
prendido se leen del settings de la app y se aplican al cambiar. La app no toma el atajo en un Nuke con el
plugin; sin plugin sigue el macro de antes. Además, el bridge no se instalaba en una `.nuke` versionada
con git: ahora solo se frena si la carpeta del plugin es un repo o la `.nuke` es código fuente.
[ Nuke Shortcuts - Add keyframe resuelto dentro de Nuke ]

v1.13:

Al terminar, `instalador.bat` preguntaba «Desea continuar y ofrecer la publicacion de release igualmente?»
cuando no había nada nuevo para commitear, que es el caso normal: el árbol ya está commiteado y pusheado
antes de armar el instalador. La pregunta sobraba, porque la siguiente («Desea subir el instalador como
release?») ya deja decir que no. Ahora, sin cambios, el script pasa directo a esa pregunta.
[ Instalador - Sin pregunta extra cuando no hay cambios ]

v1.12:

Al ejecutar el instalador desde `instalador.bat` con la copia de `build\` abierta, quedaban dos íconos
en la bandeja: el instalador cierra las copias abiertas a la fuerza, y Windows no saca el ícono de un
proceso terminado así hasta que el mouse pasa por encima. Ahora, antes de ejecutar el instalador,
`instalador.bat` le pide a la copia abierta que salga sola, como desde «Quit», con el argumento nuevo
`--quit` (por el canal de la instancia única; espera hasta 5 s a que suelte el candado). Si la abierta es
una versión vieja que no lo entiende, se cierran todas las copias por ruta, como antes.
[ Instalador - Cerrar la copia abierta antes de instalar ]

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
