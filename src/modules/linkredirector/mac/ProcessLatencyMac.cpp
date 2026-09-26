#include "modules/linkredirector/ProcessLatency.h"

#include <QDateTime>

#include <libproc.h>
#include <unistd.h>

// Se compila solo bajo APPLE (ver CMakeLists.txt); no se compilo ni se corrio en esta maquina.

namespace LinkRedirectorProcessLatency {

qint64 elapsedMsSinceStart()
{
    struct proc_bsdinfo info;
    if (proc_pidinfo(getpid(), PROC_PIDTBSDINFO, 0, &info, sizeof(info)) <= 0) {
        return -1;
    }
    const qint64 startMs = qint64(info.pbi_start_tvsec) * 1000 + qint64(info.pbi_start_tvusec) / 1000;
    return QDateTime::currentMSecsSinceEpoch() - startMs;
}

} // namespace LinkRedirectorProcessLatency
