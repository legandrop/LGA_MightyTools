# Roadmap

Lo que falta, por importancia. Las fases están en `Docs/Plan_MightyTools.md`, sección 6.

1. **Prueba de Lega de las fases 2 a 6** en su máquina (atajos, calibrador, discos, cambio de carpeta en
   diálogos reales, links, doble click en `.nk`, Apply de la asociación, instalación del Nuke Bridge),
   con las apps viejas cerradas y fuera del inicio con Windows.
2. **Observaciones abiertas, antes del release:**
   - Textos 4-8 % más anchos que el diseño en los tamaños fraccionarios (11,5 y 12,5 px): Qt redondea el
     tamaño de letra a píxel entero y aplica hinting. Medido con `--measure-fonts`; toca la tipografía de
     toda la app.
   - El encabezado del toast sale vacío (mecanismo de PipeSync, sin AUMID) y un click no vuelve a la
     app. Para mostrar «LGA Mighty Tools» y abrir la herramienta que avisó hay que registrar un AUMID,
     con su limpieza en el desinstalador. Solo con pedido de Lega.
   - No hay captura de QA del disco desenchufado (el keycap punteado se verificó por código).
3. **Medir en uso real** los tiempos de COM y UI Automation de Folder Switch (`log=true`) y la latencia de
   los links contra Link Redirector.
4. **Fase 7:** instalador, updater, alta en LGA_Updates y en el sitio; release 1.00 de Windows.
5. **Fase 8:** macOS.
6. **Otros repos, con pedido de Lega:** PipeSync (dejar de actualizar el plugin desde el release de
   `LGA_OpenInNukeX` y detectar el cliente por Mighty Tools), LGA_Updates y el sitio.
