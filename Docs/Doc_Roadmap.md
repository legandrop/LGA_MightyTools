# Roadmap

Lo que falta, por importancia. Las fases están en `Docs/Plan_MightyTools.md`, sección 6.

1. **Prueba de Lega de las fases 2 a 6** en su máquina (atajos, calibrador, discos, cambio de carpeta en
   diálogos reales, links, doble click en `.nk`, Apply de la asociación, instalación del Nuke Bridge),
   con las apps viejas cerradas y fuera del inicio con Windows.
   - **Ventana de limpieza de Disk Space (v1.04):** el borrado real, la Papelera, el botón «Free up
     space» del aviso y el escaneo de un disco externo los prueba Lega; las pruebas automatizadas solo
     borran dentro de una carpeta propia. Empezar por una regla de carpeta chica y por «Move to Recycle
     Bin».
   - **Ventana estirable (v1.11), en macOS:** estirarla desde los bordes está sin hacer
     (`platform/mac/WindowFrameMac.cpp`). En Windows, probada y aprobada por Lega.
   - **Limpieza, lo que sigue:** lo que pide administrador (restos de Windows
     Update, volcado de memoria) con elevación (D-27); huérfanos de `C:\Windows\Installer`; reglas que
     quedaron afuera (DaVinci Resolve, NuGet, conda, miniaturas); medir el primer escaneo con el disco
     frío; macOS (el borrado está deshabilitado hasta probarlo en una Mac); aceptar o corregir los
     desvíos del diseño (D-31).
   - **Limpieza, observaciones de la auditoría (no frenan):** un programa extraído en la carpeta
     temporal hace más de 7 días y todavía corriendo puede perder sus archivos de datos (la sonda de «en
     uso» no ve un exe cargado): saltear todo hijo que contenga la imagen de un proceso vivo. Al terminar
     el escaneo, el resumen y la medición corren en el hilo de la ventana (del orden de 1 s). El listado
     de carpetas no abre con «no seguir enlaces», así que un programa que cambie una carpeta por un
     enlace justo durante el borrado podría desviarlo. El self-test no cubre nombres cortos, archivos de
     solo lectura, cancelar a mitad ni los flujos de la ventana.
   - **Tooltips propios (v1.05) y «Export for AI...» (v1.06), para probar Lega:** que el tooltip aparezca
     y se oculte bien con el mouse de verdad (también con las dos ventanas abiertas y en el borde de una
     fila), y copiar y guardar una exportación desde las tres pestañas.
   - **Tooltips, observaciones de la auditoría:** una ruta muy larga puede dar un tooltip más ancho que
     la ventana (el tope de ancho es el de la Base, 1800 px); en macOS el reemplazo de atajos también toca
     rutas y URLs; `check_tooltips.ps1` se corre a mano, nada lo llama solo.
   - **Tamaño de la interfaz (v1.07), para probar Lega:** que arranque en 1 sin nada guardado, elegir 0 y 2 desde General (la app se reinicia
     sola y la ventana vuelve a su lugar, más grande), que NukeX abierto desde la app NO salga agrandado,
     y mirar los bordes finos en el tamaño 1 (a 110 % una línea de 1 px puede salir de 1 o de 2 px).
     Ícono de .nk en macOS (v1.10): compilar en una Mac, «Apply» y mirar los .nk en Finder (si queda el
     ícono viejo, `killall Finder`); confirmar que no se queda con los .nk sin «Apply».
     Límite por pantalla (v1.08): en macOS falta probar que `NSScreen` responda antes de la app (si no,
     no limita al arrancar; la página General igual apaga lo que no entra). Visto en las capturas, de antes: en español la fila «Buscar actualizaciones al iniciar» llena justo
     la tarjeta App y deja las tarjetas 5 px más anchas que en inglés (margen derecho de 11 px, no 16).
   - **Sitio (D-32):** el día del primer release, dar de alta el repo en `LGA_Updates` y regenerar el
     sitio para que la tarjeta pase de «Pronto» a la descarga.
2. **Observaciones abiertas, antes del release:**
   - Textos 4-8 % más anchos que el diseño en los tamaños fraccionarios (11,5 y 12,5 px): Qt redondea el
     tamaño de letra a píxel entero y aplica hinting. Medido con `--measure-fonts`; toca la tipografía de
     toda la app.
   - Avisos con AUMID propio (v0.09): falta la prueba de Lega del desplegable «Remind me again in», del
     click con la app cerrada y desde el Centro de notificaciones. Si Lega tiene la copia de `build\` y la
     instalada a la vez, el click de un aviso lo atiende la copia a la que apunta la anotación.
   - No hay captura de QA del disco desenchufado (el keycap punteado se verificó por código).
   - `--uninstall-cleanup` y el desinstalador no se probaron de punta a punta (solo sobre el hive
     privado): probarlos en Windows Sandbox antes del release.
3. **Mudanza de los usuarios de Open in NukeX** (D-21, plan sección 9). El lado de Mighty Tools está
   hecho (migración en la instalación, casilla y botón para quitar el cliente viejo). PipeSync ya muestra
   la tarjeta de Mighty Tools sola cuando el manifiesto de LGA_Updates la publica.
   - **Probado en Windows Sandbox (2026-09-30), con el instalador v0.07 y el cliente viejo 1.83:** con la
     casilla marcada, y con la casilla desmarcada más «Uninstall old app», el viejo se desinstala, sus
     restos se borran, Open in NukeX queda prendido sin inicio con Windows y el doble click en un `.nk`
     llama a NukeX. El `assoc .nk=` del desinstalador viejo no toca la asociación de HKCU.
   - **Pendiente:** cancelar el permiso de Windows (UAC) del desinstalador viejo. Windows Sandbox trae el
     control de cuentas apagado, así que va en una máquina real o virtual con UAC activo.
   - **Mejora posible:** la migración desde una copia instalada no retoma un ProgID que apunta a otra
     copia de Mighty Tools (por ejemplo la de `build\`), ni aunque ese exe ya no exista: el panel queda
     «Not associated» hasta un Apply. Chequear primero si el exe falta y retomarlo en ese caso.
   - **Hueco aceptado:** si el UserChoice de `.nk` apunta a `Applications\LGA_OpenInNukeX.exe` (el
     usuario lo eligió con «Abrir con» buscando el exe), la migración no lo toca porque no es nuestro
     ProgID; al quitar el viejo, el `.nk` queda sin abrir hasta que el usuario aprieta Apply.
4. **Medir en uso real** los tiempos de COM y UI Automation de Folder Switch (`log=true`) y la latencia de
   los links contra Link Redirector.
5. **Fase 7:** instalador, updater, alta en LGA_Updates y en el sitio; release 1.00 de Windows.
6. **Fase 8: macOS (desde la Mac de Lega, 2026-10-02).** Hecho y probado por Lega en la Mac: la app
   compila y se instala con `deploy.sh` (v1.15, D-38); andan Nuke Shortcuts (con el Nuke Bridge), Disk
   Space (vigilancia y avisos), Open in NukeX, Link Redirector y Folder Switch (v1.16, D-40). Lo viejo de
   la Mac ya se retiró (2026-10-02, a la Papelera): `LGA Link Redirector.app` con su LaunchAgent,
   `LGA OpenInNukeX.app`, su configuración vieja y los clones `~/.nuke/LGA_OpenInNukeX` y
   `~/Desktop/Codin/LGA_LinkRedirector`; links, `.html` y `.nk` los atiende Mighty Tools. Falta, en
   este orden:
   1. **Disk Space completo en mac: HECHO (v1.23 escaneo, v1.24 limpieza), falta la prueba de Lega**: el
      borrado real, la Papelera (medirla y vaciarla), «Show in Finder», estirar la ventana desde los bordes
      y la franja de acceso total al disco (que no salga NINGÚN cartel de privacidad sin ese permiso; la
      sesión que lo hizo tenía el permiso y no pudo comprobarlo). Observaciones de la auditoría que quedan:
      normalizar Unicode (NFC) en `DeleteGuard::clean`; excluir cualquier `*.photoslibrary` (hoy solo la de
      Imágenes); la Papelera de iCloud Drive no se vacía (el Finder sí); «otro volumen» se mira por
      `st_dev` solo en las reglas, no en cada `DeleteGuard::check`.
   2. **Avisos nativos.** Hoy van por `osascript` (`platform/mac/SystemNotifierMac.cpp`): sin ícono propio,
      sin click que vuelva a la app (`ToastActivationMac.cpp` es un stub) y sin el desplegable «Remind me
      again in» de Disk Space. Pasar a `UNUserNotificationCenter` con acciones (macOS las agrupa en
      «Options») y pedir el permiso de notificaciones.
   3. **Updater y release de mac.** `UpdateService` es solo Windows (`CMakeLists.txt`, `#ifdef Q_OS_WIN` en
      `AppController`), y en mac General dice «up to date» sin buscar nada. Hace falta: ZIP firmado con
      `ditto` + DMG (ver `LGA_Base_QT_C_Py/docs/Doc_Deploy_macOS.md` y `deploy.sh` de la Base), el updater
      de mac (baja el ZIP, verifica `codesign`, reemplaza el bundle), la entrada de mac en `LGA_Updates`,
      y la mudanza de los usuarios de mac del OpenInNukeX viejo. Recién ahí `LGA_OpenInNukeX` puede pasar
      a privado (la fila `lga_openinnukex` de PipeSync en mac depende de ese release).
   Pendientes chicos de mac: Folder Switch sin probar con el Guardar como hoja ni el popup con click
   afuera; Lega apaga Default Folder X y usa Folder Switch en el día a día; el ícono, las viñetas y los
   atajos del descriptor de Folder Switch están copiados entre `FolderSwitchModule.cpp` y
   `mac/FolderSwitchMacModule.cpp` (unificar la próxima vez que se compile en Windows); el CI de mac
   (`.github/workflows/build-macos.yml`) no se corrió después del cambio de orden de sus pasos.
7. **Otros repos, con pedido de Lega:** PipeSync (la tarjeta de LGA Mighty Tools en lugar de la de
   LGA OpenInNukeX, sin la app ni el plugin viejos en el catálogo; D-21), LGA_Updates y el sitio.
