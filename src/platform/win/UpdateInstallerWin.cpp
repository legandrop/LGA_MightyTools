#include "platform/UpdateInstaller.h"

#include "core/AutomatedRun.h"

#include <QProcess>
#include <QStringList>

namespace UpdateInstaller {

Blocker blocker()
{
    // El instalador de Inno se encarga de todo: cierra la copia en curso por ruta y escribe en
    // {app}, que no necesita administrador.
    return Blocker::None;
}

Result launch(const QString &packagePath, const QString &version, const QString &noticeTitle,
              const QString &noticeBody)
{
    Q_UNUSED(version);
    Q_UNUSED(noticeTitle);
    Q_UNUSED(noticeBody);
    Result result;
    if (AutomatedRun::active()) {
        result.failure = Failure::AutomatedRun;
        return result;
    }
    result.started = QProcess::startDetached(packagePath, QStringList());
    if (!result.started) {
        result.failure = Failure::StartInstaller;
    }
    return result;
}

} // namespace UpdateInstaller
