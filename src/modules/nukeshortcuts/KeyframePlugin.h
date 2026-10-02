#ifndef MIGHTYTOOLS_KEYFRAMEPLUGIN_H
#define MIGHTYTOOLS_KEYFRAMEPLUGIN_H

#include "core/NukePlugin.h"

#include <QDateTime>
#include <QString>

// "Add keyframe" resuelto DENTRO de Nuke: el plugin de Nuke Shortcuts (nuke_plugin/LGA_NukeShortcuts/,
// que se instala en `<.nuke>/LGA_NukeShortcuts/` desde el panel, D-41; en la 1.84 del bridge de Open in
// NukeX vivia adentro de LGA_OpenInNukeX). El plugin registra el atajo en el propio Nuke y, mientras esta
// activo, deja una marca
// `<carpeta de la app>/nuke_set_key/<pid>`. Si el Nuke del frente tiene su marca, esta app no
// registra el atajo global para el: lo atiende Nuke y no se manda ninguna tecla. Sin marca (sin
// plugin), sigue la secuencia de ActionRunner::runAddKeyframe().
namespace KeyframePlugin {

/// La carpeta `LGA_NukeShortcuts` y su payload `:/nukeshortcuts/*`, para core/NukePlugin.
const NukePlugin::Spec &plugin();

// `<AppDataLocation>/nuke_set_key`: la carpeta real de la app, aunque el settings sea otro (QA).
QString markerDir();

// Si existe la marca de `pid` y es posterior al arranque del proceso: un pid reciclado por otro Nuke
// no hereda la marca que dejo uno que se cerro sin borrarla.
bool handles(const QString &markerDir, qint64 pid, const QDateTime &processStarted);

} // namespace KeyframePlugin

#endif // MIGHTYTOOLS_KEYFRAMEPLUGIN_H
