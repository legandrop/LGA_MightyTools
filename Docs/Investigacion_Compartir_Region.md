# Investigación: compartir una región de la pantalla (herramienta nueva)

Fecha: 2026-11-06. Estado: investigación, sin código. Las preguntas para Lega están al final.

## 1. Qué hace Region to Share y qué se puede usar

**Region to Share** (Tom Englert, `github.com/tom-englert/RegionToShare`) sirve para compartir solo una
parte de la pantalla en Teams, Meet o Webex, que solo comparten la pantalla entera o una ventana.

- **Licencia GPL-3.0.** Este repo es MIT: **no se copia ni una línea** de su código. Se toman las ideas
  (las ideas no tienen licencia) y se escribe todo de cero.
- **C# / WPF / .NET Framework**, solo Windows, último cambio en enero de 2024.
- **Cómo funciona** (leído de `RecordingWindow.xaml.cs` y `MainWindow.xaml.cs`):
  - Un marco que el usuario mueve y estira marca la región.
  - Un timer (15 fps por defecto) copia esa región de la pantalla con GDI (`Graphics.CopyFromScreen`,
    o sea `BitBlt` sobre la pantalla), crea un bitmap nuevo en cada cuadro y lo pinta en una ventana WPF.
  - **El truco:** esa ventana espejo queda **debajo de todo** (una ventana «separadora» la tapa y un
    hook de foco la mantiene al fondo). Teams captura la ventana aunque esté tapada, porque la captura de
    ventanas de Windows lee el contenido de la ventana y no lo que se ve en el monitor.
  - Extras: proporciones (4:3, 16:9…), tamaños fijos, atajo para encajar la ventana activa en la región,
    resaltado del mouse con sonido de click, cursor dibujado en la copia.
- **Lo que le falta y vamos a mejorar:**
  - Captura por CPU con GDI y un bitmap nuevo por cuadro: caro en 4K, lento, sin aceleración.
  - No esconde nada: si aparece un aviso de WhatsApp o una ventana encima, se comparte.
  - No hay pausa (congelar la imagen mientras se hace algo privado), ni Mac.

**Invisiwind** (`github.com/radiantly/Invisiwind`, MIT) resuelve otro problema: esconde ventanas de
cualquier captura inyectando una DLL en cada programa para llamar a `SetWindowDisplayAffinity`.
**Descartado como base**: los antivirus lo marcan, no esconde las ventanas que se abren después, no
puede con programas de administrador y meter código adentro de WhatsApp o Telegram es justo lo que no
queremos hacer.

## 2. La idea central: nosotros armamos la imagen, entonces decidimos qué entra

Region to Share copia píxeles del monitor: lo que esté encima, sale. Como la ventana compartida la
pintamos nosotros, podemos **armar la imagen ventana por ventana** y dejar afuera lo que no se quiere
mostrar: WhatsApp, Telegram, los avisos, el escritorio. Lo que hay debajo de una ventana escondida se ve
como si no estuviera.

Las piezas son tres, en las dos plataformas:

1. **El marco**: borde fino siempre arriba, que no toma foco ni recibe clicks (salvo sus bordes y una
   barrita con Pausa / Ocultar / Cerrar). En Windows lleva `WDA_EXCLUDEFROMCAPTURE`: no sale en nuestra
   captura ni en la de nadie.
2. **La ventana compartida** (la que se elige en Teams): al fondo de todo, como en Region to Share.
3. **El motor que arma la imagen**, distinto en cada plataforma.

## 3. Windows: dos motores posibles, el prototipo elige

### Motor A, «miniaturas de DWM» (preferido si pasa la prueba P1)

El compositor de Windows (DWM) puede dibujar **en vivo** cualquier ventana adentro de otra
(`DwmRegisterThumbnail`, lo que usan Alt+Tab y Crop and Lock de PowerToys). La ventana compartida sería:
un fondo liso + una miniatura de cada ventana permitida, en el orden real de apilado, recortada a la
región (`rcSource` / `rcDestination`).

- **A favor:** todo en la GPU, sin copias, sin permiso de captura, sin borde amarillo, sin demora.
  Esconder es simplemente no dibujar: el escritorio, los avisos y las apps elegidas quedan afuera solos.
  `dwmapi` ya está enlazado en el repo.
- **En contra:** los menús, tooltips y desplegables son ventanas aparte: hay que sumarlos al vuelo (hook
  de eventos de ventana; registrar una miniatura es instantáneo). El cursor lo dibujamos nosotros. Lo
  minimizado no se ve (está bien: no está en pantalla).
- **Duda que decide todo (P1):** si Teams, Meet en el navegador y Zoom, al capturar nuestra ventana, ven
  las miniaturas o un rectángulo negro. No hay nada publicado: se prueba.

### Motor B, «pantalla + ventanas escondidas tapadas» (respaldo)

- Captura del monitor con **DXGI Desktop Duplication** (`dxgi1_2.h` y `d3d11.h`: COM común, el MinGW del
  repo los trae; solo entrega lo que cambió, en la GPU).
- Cada ventana a esconder que cae en la región se tapa: en la v1 con un panel neutro (o la imagen borrosa
  congelada); después, «ver a través» rellenando el hueco con la ventana de abajo (`PrintWindow` con
  `PW_RENDERFULLCONTENT`, o Windows.Graphics.Capture por ventana).
- «Ocultar el escritorio» en este motor = pintar liso lo que no cubre ninguna ventana.

**Windows.Graphics.Capture** (la API moderna) queda como opción del motor B para el relleno, no como
base: la cabecera del MinGW 13.1 del repo solo declara una parte (habría que declarar a mano la fábrica,
el frame pool y las interfaces de interop) y quitar el borde amarillo pide un permiso pensado para apps
empaquetadas (OBS dice que en Windows 11 lo saca sin preguntar: a confirmar).

### Avisos de Windows

- **Escondidos en lo compartido, siempre** (prendido por defecto): los avisos son ventanas del shell
  (`ShellExperienceHost`); el motor las deja afuera como cualquier otra. Se confirma la clase y el
  proceso en el Windows de Lega (P3).
- **Botón «No molestar mientras comparto»** (opcional): Windows **no tiene API pública** para prenderlo.
  Lo único conocido es el valor `NOC_GLOBAL_SETTING_TOASTS_ENABLED` del registro (HKCU, no documentado):
  se prueba en el prototipo y, si anda, se apaga al empezar y se restaura al terminar (también si la app
  se cierra mal). Si no anda de forma fiable, no se ofrece.

## 4. macOS: ScreenCaptureKit hace casi todo

- `SCStream` con `SCContentFilter(display:excludingApplications:exceptingWindows:)` y
  `SCStreamConfiguration.sourceRect` para la región. Excluir apps es **nativo**: la lista de apps a
  esconder, nuestra propia app (el marco y la ventana compartida) y el Centro de notificaciones.
- «Ocultar el escritorio»: armar el filtro con la lista de ventanas permitidas (el fondo y el Dock quedan
  afuera solos) o excluir las ventanas del fondo de pantalla y del escritorio del Finder.
- Pide el permiso de **Grabación de pantalla**; desde Sequoia, macOS lo vuelve a confirmar una vez por
  mes y no hay forma de evitarlo. Se avisa en la página de la herramienta.
- Código: un `.mm` en `src/platform/mac/`, como `MacFileDialogs.mm`. «No molestar» en mac tampoco tiene
  API pública: queda afuera (salvo un atajo de la app Atajos que arme el usuario).

## 5. Qué se esconde y cómo se reconoce

- **Por programa** (ejecutable en Windows, bundle id en mac), con grupos listos para tildar:
  Mensajería (WhatsApp, Telegram, Signal, Slack, Discord, Messenger), Correo (Outlook, Thunderbird,
  Mail), Contraseñas (1Password, Bitwarden, KeePass/KeePassXC), Avisos del sistema y Escritorio.
  Los nombres exactos (WhatsApp de la Store tiene uno propio) se confirman en el prototipo.
- **Por título de ventana**, para lo que vive en el navegador: «WhatsApp», «Telegram Web». Ojo: si es
  una pestaña de la misma ventana que se comparte, se esconde la ventana entera mientras esa pestaña está
  al frente.
- **Elegir una ventana abierta** de una lista («esconder esta») y una lista libre de programas.

## 6. Mejoras sobre Region to Share

- Esconder apps, avisos y escritorio (lo anterior).
- **Pausa**: congela lo último que se compartió mientras se hace algo privado, con atajo global.
- Captura en la GPU, cuadros solo cuando algo cambia.
- Atajos globales por el `HotkeyService` común: mostrar / esconder el marco, pausa, encajar la ventana
  activa en la región.
- Proporciones y tamaños fijos, resaltado del cursor, varias pantallas.
- Un indicador chico en la barra del marco con cuántas ventanas se están escondiendo ahora.
- Apagada no consume nada (regla 4.3 del plan): sin marco, sin hooks, sin timers.

## 7. Prototipo antes de escribir la herramienta

Un exe de prueba aparte, que no toca nada de la app, para contestar:

- **P1:** ¿Teams, Meet (Chrome/Edge) y Zoom muestran las miniaturas de DWM de una ventana compartida?
  Decide entre el motor A y el B.
- **P2:** ¿Comparten bien una ventana que está al fondo de todo, y a qué cuadros por segundo?
- **P3:** ¿Qué clase y proceso tienen los avisos de Windows en el 26300 de Lega? ¿El valor del registro
  de «No molestar» funciona sin reiniciar nada?
- **P4:** Costo en CPU/GPU con una región de 1920×1080 en el monitor 4K.

Estas pruebas las hace Lega (comparten pantalla de verdad y abren reuniones).

## 8. Fuentes

- Region to Share: https://github.com/tom-englert/RegionToShare (GPL-3.0)
- Invisiwind: https://github.com/radiantly/Invisiwind (MIT)
- `SetWindowDisplayAffinity`: https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-setwindowdisplayaffinity
- `GraphicsCaptureSession.IsBorderRequired`: https://learn.microsoft.com/uwp/api/windows.graphics.capture.graphicscapturesession.isborderrequired
- `GraphicsCaptureSession.IncludeSecondaryWindows` (24H2): https://learn.microsoft.com/uwp/api/windows.graphics.capture.graphicscapturesession.includesecondarywindows
- ScreenCaptureKit, WWDC22 «Meet ScreenCaptureKit»: https://developer.apple.com/videos/play/wwdc2022/10155/
- Permiso mensual de Sequoia: https://9to5mac.com/2024/08/14/macos-sequoia-screen-recording-prompt-monthly/
- Valor de registro de avisos (no documentado): https://github.com/stephenh678/TrayTrigger/wiki/Do-Not-Disturb-While-Playing

## 9. Preguntas para Lega

1. **Nombre** (ver la propuesta en la conversación).
2. **Atajo global** por defecto.
3. **Prototipo primero** (P1 a P4) o directo a la herramienta con el motor B, que es seguro pero más
   pesado.
4. **Windows y mac juntos** o Windows primero y mac después (el código común se escribe igual para las
   dos).
