#include "platform/ProcessStats.h"

#include <windows.h>
#include <malloc.h>
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

qint64 ProcessStats::cpuMsNow()
{
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        return -1;
    }
    return fileTimeMs(kernel) + fileTimeMs(user);
}

ProcessStats ProcessStats::current()
{
    ProcessStats stats;
    // El CPU primero: la foto de hilos de abajo cuesta y no tiene que entrar en esta lectura.
    stats.cpuMs = cpuMsNow();
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
        stats.workingSetBytes = qint64(memory.WorkingSetSize);
    }
    return stats;
}

QString ProcessStats::toString() const
{
    return QStringLiteral("handles=%1 threads=%2 gdi=%3 user=%4 private=%5KB ws=%7KB cpu=%6ms")
        .arg(handles)
        .arg(threads)
        .arg(gdiObjects)
        .arg(userObjects)
        .arg(privateBytes / 1024)
        .arg(cpuMs)
        .arg(workingSetBytes / 1024);
}

void ProcessStats::releaseFreeHeap()
{
    // Solo el CRT y el heap del proceso: los heaps privados de otras DLL pueden estar creados sin serializacion y
    // compactarlos desde este hilo los romperia.
    _heapmin();
    HeapCompact(GetProcessHeap(), 0);
}

bool ProcessStats::capCpuForTest(int percentOfOneCore)
{
    if (percentOfOneCore <= 0 || percentOfOneCore > 100) {
        return false;
    }
    // Un solo nucleo y, dentro del job, un tope duro relativo a TODA la maquina: N % de un nucleo = N / nucleos.
    SetProcessAffinityMask(GetCurrentProcess(), 1);
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const DWORD cores = qMax<DWORD>(1, info.dwNumberOfProcessors);
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job) {
        return false;
    }
    JOBOBJECT_CPU_RATE_CONTROL_INFORMATION rate{};
    rate.ControlFlags = JOB_OBJECT_CPU_RATE_CONTROL_ENABLE | JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP;
    // En centesimos de por ciento del total de la maquina (10000 = 100 %).
    rate.CpuRate = qMax<DWORD>(1, DWORD(percentOfOneCore) * 100 / cores);
    if (!SetInformationJobObject(job, JobObjectCpuRateControlInformation, &rate, sizeof(rate))) {
        CloseHandle(job);
        return false;
    }
    // El job queda abierto hasta que sale el proceso (es solo para medir).
    return AssignProcessToJobObject(job, GetCurrentProcess()) != FALSE;
}
