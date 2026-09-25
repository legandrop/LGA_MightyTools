#ifndef MIGHTYTOOLS_PROCESSSTATS_H
#define MIGHTYTOOLS_PROCESSSTATS_H

#include <QString>
#include <QtGlobal>

// Lo que consume el proceso, para la medicion de "lo apagado no consume nada" (plan 4.3). Una
// implementacion por plataforma:
//  - Windows (platform/win/ProcessStatsWin.cpp): GetProcessHandleCount, Toolhelp (hilos),
//    GetGuiResources (GDI y USER), GetProcessMemoryInfo (memoria privada) y GetProcessTimes (CPU).
//  - macOS (platform/mac/ProcessStatsMac.cpp): proc_pidinfo (hilos, CPU), task_info (memoria),
//    proc_pidinfo de descriptores (handles). Sin GDI/USER: quedan en -1.
struct ProcessStats
{
    qint64 handles = -1;
    qint64 threads = -1;
    qint64 gdiObjects = -1;
    qint64 userObjects = -1;
    qint64 privateBytes = -1;
    qint64 cpuMs = -1; ///< usuario + kernel desde que arranco el proceso

    static ProcessStats current();
    QString toString() const;
};

#endif // MIGHTYTOOLS_PROCESSSTATS_H
