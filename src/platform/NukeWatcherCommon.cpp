#include "platform/NukeWatcher.h"

#include "platform/ForegroundWatcher.h"

#include <QDebug>
#include <QRegularExpression>

NukeWatcher::NukeWatcher(ForegroundWatcher *foreground, QObject *parent)
    : QObject(parent)
{
    // Sin hook propio (el `g_instance` de antes): lo decide el nombre del exe que avisa el servicio.
    connect(foreground, &ForegroundWatcher::foregroundChanged, this,
            [this](quintptr, quint32 pid, const QString &exeName) {
                const bool nuke = isNukeExecutable(exeName);
                // De un Nuke a otro no cambia nukeInFront, pero uno puede tener el plugin del
                // keyframe y el otro no.
                const bool switched = nuke && m_nukeInFront && pid != m_frontPid;
                m_frontPid = nuke ? pid : 0;
                setNukeInFront(nuke);
                if (switched) {
                    emit frontNukeSwitched();
                }
            });
    m_nukeInFront = isNukeExecutable(foreground->foregroundExeName());
    qInfo() << "[NukeWatcher] Nuke al frente al arrancar:" << m_nukeInFront;
}

bool NukeWatcher::isNukeExecutable(const QString &fileName)
{
    // "Nuke" + version opcional ("15.1", "16.0v2") + ".exe" opcional. No acepta "NukeShortcuts" ni
    // otras apps que solo empiecen con la palabra.
    static const QRegularExpression pattern(QStringLiteral("^nuke[0-9][0-9.v]*(\\.exe)?$|^nuke(\\.exe)?$"),
                                            QRegularExpression::CaseInsensitiveOption);
    return pattern.match(fileName).hasMatch();
}

void NukeWatcher::setNukeInFront(bool inFront)
{
    if (m_nukeInFront == inFront) {
        return;
    }
    m_nukeInFront = inFront;
    emit nukeInFrontChanged(inFront);
}
