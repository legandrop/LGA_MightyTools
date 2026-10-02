#ifndef MIGHTYTOOLS_KEYFRAMEPLUGIN_H
#define MIGHTYTOOLS_KEYFRAMEPLUGIN_H

#include "core/NukePlugin.h"

#include <QDateTime>
#include <QString>

// "Add keyframe" y "Frame Dope Sheet" resueltos DENTRO de Nuke: el plugin de Nuke Shortcuts
// (nuke_plugin/LGA_NukeShortcuts/, que se instala en `<.nuke>/LGA_NukeShortcuts/` desde el panel, D-41;
// en la 1.84 del bridge de Open in NukeX vivia adentro de LGA_OpenInNukeX). El plugin registra los
// atajos en el propio Nuke y, mientras cada uno esta activo, deja una marca `<carpeta de la app>/
// <carpeta de la accion>/<pid>`. Si el Nuke del frente tiene la marca de una accion, esta app no
// registra ese atajo global para el: lo atiende Nuke y no se manda ninguna tecla. Sin marca (sin
// plugin, o uno viejo), sigue la secuencia de ActionRunner.
namespace KeyframePlugin {

/// La carpeta `LGA_NukeShortcuts` y su payload `:/nukeshortcuts/*`, para core/NukePlugin.
const NukePlugin::Spec &plugin();

// `<AppDataLocation>/nuke_set_key`: la carpeta real de la app, aunque el settings sea otro (QA).
QString markerDir();

// `<AppDataLocation>/nuke_frame_dope`: la marca de "Frame Dope Sheet" (plugin 1.01 en adelante, D-42).
// Va aparte de la del keyframe: un Nuke con el plugin 1.00 tiene la del keyframe y no sabe encuadrar.
QString frameMarkerDir();

// El plugin con esa version instalada hace "Frame Dope Sheet": la calibracion ya no hace falta.
// En macOS siempre false hasta probarlo ahi (el plugin tampoco registra ese atajo en la Mac).
bool framesDopeSheet(const QString &installedVersion);

// Si existe la marca de `pid` y es posterior al arranque del proceso: un pid reciclado por otro Nuke
// no hereda la marca que dejo uno que se cerro sin borrarla.
bool handles(const QString &markerDir, qint64 pid, const QDateTime &processStarted);

} // namespace KeyframePlugin

#endif // MIGHTYTOOLS_KEYFRAMEPLUGIN_H
