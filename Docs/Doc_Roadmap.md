# Roadmap

Lo que falta, por importancia. Las fases están en `Docs/Plan_MightyTools.md`, sección 6.

1. **Herramientas nuevas (Lega, 2026-11-02), en este orden:**
   1. **Dictation** (Windows, D-72): en main y probada por Lega con su voz (2026-11-03). Falta probar de
      verdad el botón Download con una carpeta de modelos vacía (en la PC de Lega el motor se copió a mano).
      **macOS (D-75):** hecha en la rama `dictado-mac` (2026-11-07), sin merge a main. Falta la prueba de
      Lega: Download con la carpeta vacía (baja motor y modelo), el permiso de micrófono la primera vez, el
      de Accesibilidad, dictar en Notas, Safari, la Terminal y una app Electron (tocar y mantener ⌘⌥⇧H, Esc a
      la mitad), en Nuke (Qt: algunas apps no Cocoa toman solo la primera letra de cada pedazo de 16),
      con unos AirPods (cambian de perfil al abrir el micrófono) y la velocidad con Metal. Pendientes posibles: mostrar en la página si falta el permiso
      de Accesibilidad o de micrófono (hoy se entera al dictar); probar en una Mac Intel (solo CPU).
      Descartado por Lega: Parakeet y Voxtral como modelos opcionales, y el ayudante elevado para dictar
      con una ventana de administrador al frente.
   2. **App Toggle** (Windows y macOS): hecha en la v1.53 (D-67). Falta la prueba real de Lega (Win+E con
      XYplorer; OBS y UAD Console desde el Stream Deck con botones de tipo atajo) y probarla en la Mac
      (el CI ya la compila). Pendientes posibles: con una ventana de administrador al frente el atajo no llega
      (sumar App Toggle al ayudante elevado de D-61); una app viva sin ventana (solo en la bandeja, como
      UAD Console a veces) no se muestra, igual que el AHK (opción: volver a ejecutar el exe).
   3. **Mouse Buttons** (Windows): hecha en la v1.53 (D-68). Falta la prueba real de Lega con Photoshop
      (mover la imagen con el botón del medio; en otra app el botón del medio hace lo de siempre).
      Pendientes posibles: macOS; dos reglas que comparten una tecla y se aprietan a la vez (soltar una
      suelta la tecla de las dos).
   4. **Desktop Icons** (Windows): hecha en la v1.53 (D-66, `Docs/Roadmap_Desktop_Icons.md`). Falta la
      prueba real de Lega: restaurar un snapshot (las pruebas automáticas nunca mueven íconos), también con
      «Organizar automáticamente» prendido, y Undo.
   5. **Meet Frame** (Windows y macOS, D-74): hecha en la v1.53. Falta la prueba real de Lega: compartir
      la ventana «Meet Frame» en Teams, Meet y Zoom (también con la ventana debajo del escritorio), que un
      aviso de Windows y WhatsApp/Telegram salgan tapados, el consumo con 30 y 60 fps, y probarla en la
      Mac (el CI la compila; el permiso de Grabación de pantalla y el filtro de ScreenCaptureKit no se
      probaron). Pendientes posibles: «ver a través» de lo tapado en lugar del gris liso; motor de
      miniaturas de DWM si P1 sale bien (más liviano); atajo para encajar la ventana activa en el marco.
   6. **WhatsApp Chat** (D-78): se sacó entera antes de publicarse (D-79).
2. **Limpieza de Disk Space, lo que sigue:** lo que pide administrador (restos de Windows Update, volcado de
   memoria) con elevación (D-27); probar de verdad los instaladores huérfanos (D-69: el UAC, el paso
   elevado y la Papelera; en la PC de Lega hoy no aparece ninguno porque las 6 copias sueltas son de
   programas instalados); probar las cachés de programas audiovisuales (D-70) con los programas
   instalados y en la Mac (sin compilar en local, sin Adobe/Resolve/Unreal reales para confirmar
   formatos: la base `.mcdb` de Adobe, el `Preferences.cfg` de Live 12, los nombres de archivo del
   timeline de Nuke); reglas que quedaron afuera (preferencias de Resolve y de Redshift, Zen de Unreal
   5.4+, Topaz y Unreal en mac, NuGet, conda, miniaturas); medir el primer escaneo con el disco frío; aceptar o
   corregir los desvíos del diseño (D-31).
   - **Observaciones de la auditoría (no frenan):** un programa extraído en la carpeta temporal hace más de
     7 días y todavía corriendo puede perder sus archivos de datos (la sonda de «en uso» no ve un exe
     cargado): saltear todo hijo que contenga la imagen de un proceso vivo. Al terminar el escaneo, el
     resumen y la medición corren en el hilo de la ventana (del orden de 1 s). El listado de carpetas no
     abre con «no seguir enlaces», así que un programa que cambie una carpeta por un enlace justo durante
     el borrado podría desviarlo. El self-test no cubre nombres cortos, archivos de solo lectura, cancelar
     a mitad ni los flujos de la ventana.
3. **Observaciones abiertas:**
   - Textos 4-8 % más anchos que el diseño en los tamaños fraccionarios (11,5 y 12,5 px): Qt redondea el
     tamaño de letra a píxel entero y aplica hinting. Medido con `--measure-fonts`; toca la tipografía de
     toda la app.
   - Tooltips: una ruta muy larga puede dar un tooltip más ancho que la ventana (el tope de ancho es el de
     la Base, 1800 px); en macOS el reemplazo de atajos también toca rutas y URLs; `check_tooltips.ps1` se
     corre a mano, nada lo llama solo.
   - No hay captura de QA del disco desenchufado (el keycap punteado se verificó por código).
   - `--uninstall-cleanup` y el desinstalador no se probaron de punta a punta (solo sobre el hive
     privado): probarlos en Windows Sandbox.
   - En español la fila «Buscar actualizaciones al iniciar» llena justo la tarjeta App y deja las tarjetas
     5 px más anchas que en inglés (margen derecho de 11 px, no 16).
   - **Liviana, lo que queda (D-77, medido con `--measure-pages` y el runner con `-Platform windows`):**
     cambiar a una página ya vista cuesta ~9 ms (mostrar, esconder y pintar: el piso de Qt); la primera visita a
     Window Layouts con 12 atajos, ~40 ms (260 widgets: una sola tecla dibujada por fila los bajaría a la mitad);
     soltada la ventana quedan ~7 MB de Windows y Qt que no se pudieron atribuir sin un perfilador de heap;
     Open in NukeX no guarda la ruta al hacer click afuera (el `editingFinished` exige `hasFocus()`, que ya es
     false en ese momento), al revés de lo que dice su comentario.
4. **Mudanza de los usuarios de Open in NukeX** (D-21, plan sección 9): hecha y probada.
   - **Mejora posible:** la migración desde una copia instalada no retoma un ProgID que apunta a otra
     copia de Mighty Tools (por ejemplo la de `build\`), ni aunque ese exe ya no exista: el panel queda
     «Not associated» hasta un Apply. Chequear primero si el exe falta y retomarlo en ese caso.
   - **Hueco aceptado:** si el UserChoice de `.nk` apunta a `Applications\LGA_OpenInNukeX.exe` (el
     usuario lo eligió con «Abrir con» buscando el exe), la migración no lo toca porque no es nuestro
     ProgID; al quitar el viejo, el `.nk` queda sin abrir hasta que el usuario aprieta Apply.
5. **Medir en uso real** los tiempos de COM y UI Automation de Folder Switch (`log=true`) y la latencia de
   los links contra Link Redirector.
6. **macOS, lo que falta:**
   1. **Ventana de limpieza estirable:** estirarla desde los bordes está sin hacer
      (`platform/mac/WindowFrameMac.mm`).
   2. **Disk Space, observaciones de las auditorías:** normalizar Unicode (NFC) en `DeleteGuard::clean`;
      excluir cualquier `*.photoslibrary` (hoy solo la de Imágenes); la Papelera de iCloud Drive no se
      vacía (el Finder sí); «otro volumen» se mira por `st_dev` solo en las reglas, no en cada
      `DeleteGuard::check`; en los temporales, una librería mapeada sin fd abierto (un `_MEI*` de
      PyInstaller) no cuenta como «en uso»; mandar a la Papelera no mira si algo está abierto (el Finder se
      niega); el encabezado de «Other app caches» tilda todo de un click; hay una carrera (un proceso del
      usuario que cambie una carpeta ya listada por un enlace).
   3. **Avisos nativos.** Hoy van por `osascript` (`platform/mac/SystemNotifierMac.cpp`): sin ícono propio,
      sin click que vuelva a la app (`ToastActivationMac.cpp` es un stub) y sin el desplegable «Remind me
      again in» de Disk Space. Pasar a `UNUserNotificationCenter` con acciones (macOS las agrupa en
      «Options») y pedir el permiso de notificaciones.
   4. **Release de mac:** el paquete, el actualizador y la publicación están hechos (D-47, D-49, D-59). Queda:
      - Observación del runner: el `--self-test` falló una vez de ocho en la copia recién instalada
        (`[diskSpace] motor`: el escaneo de la carpeta de pruebas no terminó en los 5 s de `waitScan`,
        con el antivirus del runner encima); las siete restantes, 0 fallas.
      - **Respaldar el certificado Developer ID** (el `.p12` del llavero de la Mac de Lega; dónde y cómo,
        en `../LGA_Base_QT_C_Py/docs/Doc_Firma_Apple.md`): desde la v1.55 lo publicado se firma con él.
      - `LGA_OpenInNukeX` puede archivarse y pasar a privado cuando Lega quiera (D-10): en mac no hay
        mudanza que esperar (D-50) y PipeSync ya ofrece Mighty Tools en las dos plataformas.
      - Desde la v1.55 la app y el DMG salen firmados con Developer ID y notarizados: ya no hace falta el
        `xattr`. Cuando se publique la v1.55 notarizada, poner `REQUIRE_NOTARIZATION=true` en
        `tools/macos/release.conf`.
   5. **Pendientes chicos:** el ícono, las viñetas y los atajos del descriptor de Folder Switch están
      copiados entre `FolderSwitchModule.cpp` y `mac/FolderSwitchMacModule.cpp` (unificar la próxima vez
      que se compile en Windows).
