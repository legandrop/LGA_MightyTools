#include "updates/VersionCompare.h"

#include <QVersionNumber>

namespace VersionCompare {

namespace {

// Saca el prefijo "v" y los espacios. "v0.10" y " 0.10 " son la misma version.
QString stripped(const QString &rawVersion)
{
    QString candidate = rawVersion.trimmed();
    if (candidate.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) {
        candidate = candidate.mid(1).trimmed();
    }
    return candidate;
}

// Cada segmento se lee como entero: "0.10" da [0,10] y "0.9" da [0,9]. Se exige que el texto
// entero se consuma, para que un sufijo ("0.10-beta") no pase por una version valida.
QVersionNumber parse(const QString &rawVersion, bool *okOut)
{
    const QString candidate = stripped(rawVersion);
    qsizetype parsePosition = -1;
    const QVersionNumber version = QVersionNumber::fromString(candidate, &parsePosition);
    *okOut = !candidate.isEmpty() && !version.isNull() && parsePosition == candidate.size();
    return version;
}

} // namespace

bool isNewer(const QString &candidateVersion, const QString &referenceVersion)
{
    bool candidateOk = false;
    bool referenceOk = false;
    const QVersionNumber candidate = parse(candidateVersion, &candidateOk);
    const QVersionNumber reference = parse(referenceVersion, &referenceOk);
    if (!candidateOk || !referenceOk) {
        return false;
    }
    return QVersionNumber::compare(candidate, reference) > 0;
}

} // namespace VersionCompare
