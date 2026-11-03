# Desktop Icons: guardar y restaurar la posición de los íconos del escritorio

Herramienta de Mighty Tools, en desarrollo desde el 2026-11-02 (`Docs/Doc_Roadmap.md`, ítem 1.3).

## Para qué

Windows desordena los íconos del escritorio en situaciones comunes:

- al arrancar, si el escritorio se arma con una resolución chica (por ejemplo 1024×768) antes de que llegue el
  monitor bueno;
- al cambiar el monitor de entrada o de cable (de HDMI a DisplayPort Windows lo toma como otro monitor);
- al reiniciar el Explorador.

Hoy, volver al orden de antes es a mano. La herramienta guarda fotos del orden («snapshots») y deja volver a
cualquiera de ellas con un click.

## Qué hace

1. **Snapshot manual.** El usuario guarda el orden actual cuando quiere, con un nombre opcional. Estos snapshots
   no se borran solos nunca: los borra el usuario.
2. **Snapshot automático, una vez por día.** La app guarda solo un snapshot por día calendario, con una condición:
   **la computadora tiene que llevar por lo menos una hora despierta** (desde que arrancó Windows o desde que
   volvió de suspensión o hibernación). Lo que se cuenta es el tiempo desde la última de esas dos cosas. El
   motivo: los desórdenes aparecen casi siempre justo después de arrancar, y un snapshot tomado en ese momento
   guardaría el escritorio ya desordenado.
   - La app revisa la condición cada tanto (por ejemplo cada 15 minutos) y guarda la primera vez que se cumple
     en el día. Si la PC se apaga antes de cumplir la hora, ese día queda sin snapshot automático.
3. **Retención.** Se conservan los **últimos 5 snapshots automáticos** (los últimos 5 días con snapshot); al
   guardar el sexto se borra el más viejo. Los manuales no cuentan para ese límite.
4. **Restaurar.** Una lista con todos los snapshots (automáticos y manuales): fecha y hora, tipo, nombre,
   cantidad de íconos y resolución de pantalla de ese momento. El usuario elige uno y lo restaura.
   - Cada ícono se busca por la ruta de su archivo, no por su nombre visible. Los que ya no existen se informan
     y se saltean.
   - Antes de restaurar, se guarda solo un snapshot «antes de restaurar», para poder deshacer.

## Cómo (Windows)

- **Leer y mover los íconos:** la vista del escritorio con `IShellWindows::FindWindowSW(SWC_DESKTOP)` →
  `IFolderView` / `IFolderView2` (`GetItemPosition`, `SelectAndPositionItems`), como hace el Explorador. Ya
  existe un script de restauración probado con esta técnica en la PC de Lega: movió 86 de 112 íconos (los
  demás eran de apps sin instalar).
- **Qué guarda cada snapshot** (un JSON por snapshot en la carpeta de configuración de la app): por ícono, el
  nombre visible, la ruta y la posición; y de la pantalla, la resolución, el escalado (DPI), el espaciado de
  íconos y si están activos «Organizar automáticamente» y «Alinear a la cuadrícula».
- **Tiempo despierta (como quedó implementado):** «despierta desde» arranca cuando se prende la herramienta (o
  el interruptor «Daily snapshot»): si la app se reinicia a mitad del día, esperar una hora más no hace daño. Un
  timer cada 15 minutos compara cuánto avanzó el reloj de pared con cuánto avanzó `QueryUnbiasedInterruptTime`
  (que no cuenta el tiempo suspendido ni hibernado): si el de pared avanzó más de 2 minutos de más, hubo una
  suspensión y «despierta desde» vuelve a empezar. Así no hace falta una ventana para `WM_POWERBROADCAST` ni
  leer el registro de eventos. Límite conocido: adelantar el reloj a mano también cuenta como suspensión (solo
  atrasa el automático una hora).
- **No reiniciar el Explorador** para aplicar nada: en la PC de Lega dejó una vez la pantalla gris sin barra de
  tareas. El orden restaurado lo graba el propio Explorador al cerrar sesión o reiniciar.

## Decisiones de Lega (2026-11-02)

- **Escritorio igual al del snapshot automático anterior** (mismos íconos en las mismas posiciones): no se
  gasta un lugar; se actualiza la fecha de ese snapshot. Así los 5 automáticos son 5 órdenes distintos.
- **Resolución distinta de la habitual** (la que más se repite en los snapshots guardados): ese día se
  saltea el automático. Es la señal de un escritorio que arrancó mal.
- **Restaurar solo a pedido** en la primera versión. Más adelante, un aviso cuando cambia la pantalla o se
  reinicia el Explorador.
- **«Organizar automáticamente» y «Alinear a la cuadrícula»** se dejan como estaban en el snapshot al
  restaurar: si no, Windows reacomoda los íconos encima de lo restaurado.
- Solo Windows: macOS queda fuera de la primera versión.

## Cómo quedó la primera versión (2026-11-02)

- **Dónde:** un JSON por snapshot en `%APPDATA%\LGA\LGA_MightyTools\desktop_icons\`. El desinstalador borra esa
  carpeta junto con `settings.ini`. Un archivo ilegible se ignora y queda donde está.
- **Resolución:** la de la pantalla virtual (todos los monitores) en píxeles físicos, que es el área de la vista
  del escritorio. **Habitual** es la que más se repite entre los manuales y automáticos guardados; los «antes de
  restaurar» no cuentan. Sin snapshots, o con un empate en el primer lugar, cuenta como habitual.
- **Deshacer:** «Undo» restaura el «antes de restaurar», y como toda restauración guarda antes uno nuevo, apretar
  «Undo» otra vez vuelve a lo restaurado.
- **Íconos nuevos** que no estaban en el snapshot quedan donde están.
- Las filas de un snapshot con otra resolución que la actual lo avisan antes de restaurar.
