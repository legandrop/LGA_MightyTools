#include "platform/ProcessStats.h"

#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>

namespace {

qint64 threadCount()
{
    const DWORD pid = GetCurrentProcessId();
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return -1;
    }
    qint64 count = 0;
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    if (Thread32First(snapshot, &entry)) {
        do {
            if (entry.th32OwnerProcessID == pid) {
                ++count;
            }
        } while (Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return count;
}

qint64 fileTimeMs(const FILETIME &time)
{
    ULARGE_INTEGER value;
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    return qint64(value.QuadPart / 10000); // unidades de 100 ns
}

} // namespace

ProcessStats ProcessStats::current()
{
    ProcessStats stats;
    HANDLE process = GetCurrentProcess();
    DWORD handles = 0;
    if (GetProcessHandleCount(process, &handles)) {
        stats.handles = handles;
    }
    stats.threads = threadCount();
    stats.gdiObjects = GetGuiResources(process, GR_GDIOBJECTS);
    stats.userObjects = GetGuiResources(process, GR_USEROBJECTS);
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    if (GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory), sizeof(memory))) {
        stats.privateBytes = qint64(memory.PrivateUsage);
    }
    FILETIME created{}, exited{}, kernel{}, user{};
    if (GetProcessTimes(process, &created, &exited, &kernel, &user)) {
        stats.cpuMs = fileTimeMs(kernel) + fileTimeMs(user);
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
