# Roadmap

Lo que falta, por importancia. Las fases están en `Docs/Plan_MightyTools.md`, sección 6.

1. **Herramientas nuevas (Lega, 2026-11-02), en este orden:**
   1. **App Toggle** (Windows y macOS). Un atajo por app con tres casos: si la app no está abierta, la
      abre; si está abierta sin foco, la trae al frente (y la restaura si estaba minimizada); si ya tiene
      el foco, la minimiza. Sin opción de maximizar. Guarda exe, argumentos y directorio de trabajo, no un
      `.lnk` (el instalador de OBS 33 le cambió el nombre al acceso y el AHK dejó de abrirlo). Entradas de
      Lega: XYplorer (Win+E, hoy en `C:\Leg_AHKs\ahk_lega_shortcuts.ahk`, que se saca de ahí para que no se
      pisen), OBS y UAD Console (hoy botones «Abrir» de Stream Deck que lanzan `ahk-open_OBS.ahk` y
      `ahk-open_UAD.ahk`; pasan a botones de tipo atajo con la combinación de App Toggle).
   2. **Mouse Buttons** (Windows). Reemplaza X-Mouse Button Control, que no se reinstaló al formatear. Lo
      único que Lega usaba era un perfil para `photoshop.exe`: mientras se mantiene apretado el botón del
      medio, simula Espacio + clic izquierdo (mover la imagen con el botón del medio). Reglas «app → botón →
      teclas y clic mientras está apretado», con un gancho de mouse de bajo nivel que solo existe mientras
      una app con regla está al frente.
   3. **Desktop Icons** (Windows). Guarda y restaura la posición de los íconos del escritorio: snapshots
      manuales y uno automático por día con la PC despierta hace al menos una hora; quedan los últimos 5
      automáticos más los manuales. Diseño y decisiones en `Docs/Roadmap_Desktop_Icons.md`.
2. **Limpieza de Disk Space, lo que sigue:** lo que pide administrador (restos de Windows Update, volcado de
   memoria) con elevación (D-27); huérfanos de `C:\Windows\Installer`; reglas que quedaron afuera
   (DaVinci Resolve, NuGet, conda, miniaturas); medir el primer escaneo con el disco frío; aceptar o
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
      - **Respaldar el certificado «LGA Code Signing»** (el `.p12` del llavero de la Mac de Lega): si se
        pierde, la versión siguiente sale con otra identidad, el actualizador la rechaza y hay que
        reinstalar a mano y volver a dar los permisos.
      - `LGA_OpenInNukeX` puede archivarse y pasar a privado cuando Lega quiera (D-10): en mac no hay
        mudanza que esperar (D-50) y PipeSync ya ofrece Mighty Tools en las dos plataformas.
      - La app no está notarizada: la primera apertura pide el `xattr -dr com.apple.quarantine` del
        `README.txt` del DMG.
   5. **Pendientes chicos:** el ícono, las viñetas y los atajos del descriptor de Folder Switch están
      copiados entre `FolderSwitchModule.cpp` y `mac/FolderSwitchMacModule.cpp` (unificar la próxima vez
      que se compile en Windows).
