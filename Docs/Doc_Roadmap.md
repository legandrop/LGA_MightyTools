# Roadmap

Lo que falta, por importancia. Las fases están en `Docs/Plan_MightyTools.md`, sección 6.

1. **Prueba de Lega de las fases 2 a 6** en su máquina (atajos, calibrador, discos, cambio de carpeta en
   diálogos reales, links, doble click en `.nk`, Apply de la asociación, instalación del Nuke Bridge),
   con las apps viejas cerradas y fuera del inicio con Windows.
2. **Observaciones de las auditorías, antes del release:**
   - La ayuda es más alta que una pantalla chica: Close queda afuera. Acotarla y darle scroll.
   - La segunda copia puede no traer la ventana al frente (`AllowSetForegroundWindow`) y no hace nada
     mientras la residente espera la bandeja.
   - La ventana se abre en cada arranque si no hay nada prendido; D-06 dice solo la primera vez.
   - Textos 2-4 % más anchos que el diseño en la captura offscreen; chip «Win · mac» más ancho; menú de
     la bandeja más angosto y con otro orden de secciones que el diseño; keycap punteado del disco
     desenchufado.
   - Leer settings crea la carpeta de settings vacía; `--simulate-action` lee el settings.ini real.
   - «Check now» sigue habilitado mientras chequea; el grabador de atajos no se corta con un click afuera.
   - Apagar y prender Disk Space borra su historial de avisos.
   - Comentarios del contrato que todavía dicen que Nuke Shortcuts tiene hook propio.
3. **Medir en uso real** los tiempos de COM y UI Automation de Folder Switch (`log=true`) y la latencia de
   los links contra Link Redirector.
4. **Fase 7:** instalador, updater, alta en LGA_Updates y en el sitio; release 1.00 de Windows.
5. **Fase 8:** macOS.
6. **Otros repos, con pedido de Lega:** PipeSync (dejar de actualizar el plugin desde el release de
   `LGA_OpenInNukeX` y detectar el cliente por Mighty Tools), LGA_Updates y el sitio.
