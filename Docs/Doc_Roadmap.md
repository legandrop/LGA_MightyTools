# Roadmap

Lo que falta, por importancia. Las fases están en `Docs/Plan_MightyTools.md`, sección 6.

1. **Prueba de Lega de las fases 2 a 6** en su máquina (atajos, calibrador, discos, cambio de carpeta en
   diálogos reales, links, doble click en `.nk`, Apply de la asociación, instalación del Nuke Bridge),
   con las apps viejas cerradas y fuera del inicio con Windows.
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
6. **Fase 8:** macOS. Los avisos siguen por `osascript`, sin click ni desplegable: el «Remind me again
   in» de Disk Space pide `UNUserNotificationCenter` con acciones (macOS las agrupa en «Options»).
7. **Otros repos, con pedido de Lega:** PipeSync (la tarjeta de LGA Mighty Tools en lugar de la de
   LGA OpenInNukeX, sin la app ni el plugin viejos en el catálogo; D-21), LGA_Updates y el sitio.
