#include "platform/NukeWatcher.h"

#include <QRegularExpression>

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
