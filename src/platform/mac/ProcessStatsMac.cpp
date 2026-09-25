#include "platform/ProcessStats.h"

#include <libproc.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <sys/proc_info.h>
#include <unistd.h>

// macOS: sin GDI ni USER (quedan en -1). Los "handles" son los descriptores de archivo abiertos.

ProcessStats ProcessStats::current()
{
    ProcessStats stats;
    const pid_t pid = getpid();

    proc_taskinfo task{};
    if (proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &task, sizeof(task)) == int(sizeof(task))) {
        stats.threads = task.pti_threadnum;
        // pti_total_user/system vienen en unidades de mach_absolute_time.
        mach_timebase_info_data_t timebase{};
        mach_timebase_info(&timebase);
        const double ns = double(task.pti_total_user + task.pti_total_system) * timebase.numer / timebase.denom;
        stats.cpuMs = qint64(ns / 1e6);
    }

    const int bytes = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, nullptr, 0);
    if (bytes > 0) {
        stats.handles = bytes / int(sizeof(proc_fdinfo));
    }

    task_vm_info_data_t vm{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&vm), &count) == KERN_SUCCESS) {
        stats.privateBytes = qint64(vm.phys_footprint);
    }
    return stats;
}

QString ProcessStats::toString() const
{
    return QStringLiteral("handles=%1 threads=%2 gdi=%3 user=%4 private=%5KB cpu=%6ms")
        .arg(handles)
        .arg(threads)
        .arg(gdiObjects)
        .arg(userObjects)
        .arg(privateBytes / 1024)
        .arg(cpuMs);
}
