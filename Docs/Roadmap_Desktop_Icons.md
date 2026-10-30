# Desktop Icons: guardar y restaurar la posición de los íconos del escritorio

Herramienta futura de Mighty Tools. **Todavía no se implementa**: queda anotada para hacerla más adelante.
Está en `Docs/Doc_Roadmap.md`.

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
- **Tiempo despierta:** se toma el más reciente entre el momento del arranque y el de la última vuelta de
  suspensión. Para la vuelta de suspensión: `WM_POWERBROADCAST` / `PBT_APMRESUMEAUTOMATIC` mientras la app
  corre, y si la app arrancó después, el evento de reanudación del registro de sistema. Para el arranque:
  ahora menos `GetTickCount64`, que también cuenta el tiempo suspendido, por eso no alcanza solo.
- **No reiniciar el Explorador** para aplicar nada: en la PC de Lega dejó una vez la pantalla gris sin barra de
  tareas. El orden restaurado lo graba el propio Explorador al cerrar sesión o reiniciar.

## Preguntas abiertas (las decide Lega al implementarla)

- Si el escritorio de hoy está casi igual al del snapshot anterior, ¿se guarda igual o se saltea?
- ¿Saltear también el automático si la resolución actual no es la de siempre (señal de que el escritorio arrancó
  mal)?
- ¿Ofrecer restaurar solo al detectar un desorden grande (muchos íconos movidos de golpe), o solo a pedido?
- macOS: las posiciones del escritorio del Finder se pueden leer y escribir, pero queda fuera de la primera
  versión.
