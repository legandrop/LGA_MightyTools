#ifndef MIGHTYTOOLS_KEYFRAMEPLUGIN_H
#define MIGHTYTOOLS_KEYFRAMEPLUGIN_H

#include <QDateTime>
#include <QString>

// "Add keyframe" resuelto DENTRO de Nuke (nuke_plugin/LGA_OpenInNukeX/LGA_KeyframeToggle.py, bridge
// 1.84+). El plugin registra el atajo en el propio Nuke y, mientras esta activo, deja una marca
// `<carpeta de la app>/nuke_set_key/<pid>`. Si el Nuke del frente tiene su marca, esta app no
// registra el atajo global para el: lo atiende Nuke y no se manda ninguna tecla. Sin marca (sin
// plugin, o uno anterior a la 1.84), sigue la secuencia de ActionRunner::runAddKeyframe().
namespace KeyframePlugin {

// `<AppDataLocation>/nuke_set_key`: la carpeta real de la app, aunque el settings sea otro (QA).
QString markerDir();

// Si existe la marca de `pid` y es posterior al arranque del proceso: un pid reciclado por otro Nuke
// no hereda la marca que dejo uno que se cerro sin borrarla.
bool handles(const QString &markerDir, qint64 pid, const QDateTime &processStarted);

} // namespace KeyframePlugin

#endif // MIGHTYTOOLS_KEYFRAMEPLUGIN_H
