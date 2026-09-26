#include "modules/linkredirector/ProcessLatency.h"

#include <windows.h>

namespace {

quint64 toU64(const FILETIME &ft)
{
    return (static_cast<quint64>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

} // namespace

namespace LinkRedirectorProcessLatency {

qint64 elapsedMsSinceStart()
{
    FILETIME creation, exitTime, kernelTime, userTime;
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exitTime, &kernelTime, &userTime)) {
        return -1;
    }
    FILETIME now;
    GetSystemTimePreciseAsFileTime(&now); // Windows 8+: la app apunta a Windows 10/11.
    const quint64 diff100ns = toU64(now) - toU64(creation);
    return static_cast<qint64>(diff100ns / 10000); // 100 ns -> ms
}

} // namespace LinkRedirectorProcessLatency
