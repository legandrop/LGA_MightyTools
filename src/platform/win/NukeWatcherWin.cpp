#include "platform/NukeWatcher.h"

#include <QDebug>
#include <QFileInfo>
#include <QTimeZone>

#include <iterator>

#include <windows.h>
#include <dwmapi.h>

namespace {

QString processFileName(HWND hwnd)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) {
        return QString();
    }
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) {
        return QString();
    }
    wchar_t buffer[MAX_PATH * 2] = {};
    DWORD size = static_cast<DWORD>(std::size(buffer));
    const bool ok = QueryFullProcessImageNameW(process, 0, buffer, &size);
    CloseHandle(process);
    return ok ? QFileInfo(QString::fromWCharArray(buffer, static_cast<int>(size))).fileName() : QString();
}

// La ventana principal de la que cuelga `hwnd`. Un panel flotante de Nuke (un Dope Sheet suelto,
// el panel de propiedades) es una ventana propia OWNED por la principal: GA_ROOTOWNER sube hasta
// ella, asi el punto calibrado siempre se mide contra el mismo marco.
HWND mainWindowOf(HWND hwnd)
{
    return hwnd ? GetAncestor(hwnd, GA_ROOTOWNER) : nullptr;
}

// Marco VISIBLE de la ventana, en pixeles fisicos. GetWindowRect incluye los bordes invisibles de
// redimensionar (unos 7 px por lado en Windows 10/11) y cambian entre maximizada y no maximizada;
// el marco de DWM es lo que el usuario ve.
QRect frameOf(HWND hwnd)
{
    RECT rect{};
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect)))) {
        if (!GetWindowRect(hwnd, &rect)) {
            return QRect();
        }
    }
    return QRect(QPoint(rect.left, rect.top), QPoint(rect.right - 1, rect.bottom - 1));
}

bool isNukeWindow(HWND hwnd)
{
    return hwnd && NukeWatcher::isNukeExecutable(processFileName(hwnd));
}

} // namespace

bool NukeWatcher::isNukeInFrontNow() const
{
    return isNukeWindow(GetForegroundWindow());
}

NukeWatcher::FrontProcess NukeWatcher::frontNukeProcess() const
{
    FrontProcess result;
    const HWND foreground = GetForegroundWindow();
    if (!isNukeWindow(foreground)) {
        return result;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(foreground, &pid);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) {
        return result;
    }
    FILETIME created{}, exited{}, kernel{}, user{};
    if (GetProcessTimes(process, &created, &exited, &kernel, &user)) {
        // FILETIME: intervalos de 100 ns desde 1601-01-01 UTC.
        const qint64 ticks = (static_cast<qint64>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
        constexpr qint64 kEpochDiff = 116444736000000000LL;
        result.pid = pid;
        result.started = QDateTime::fromMSecsSinceEpoch((ticks - kEpochDiff) / 10000, QTimeZone::UTC);
    }
    CloseHandle(process);
    return result;
}

QRect NukeWatcher::frontNukeFrame() const
{
    const HWND foreground = GetForegroundWindow();
    if (!isNukeWindow(foreground)) {
        return QRect();
    }
    return frameOf(mainWindowOf(foreground));
}

QRect NukeWatcher::nukeFrameAt(const QPoint &nativePoint) const
{
    const HWND hwnd = WindowFromPoint(POINT{nativePoint.x(), nativePoint.y()});
    if (!isNukeWindow(hwnd)) {
        return QRect();
    }
    return frameOf(mainWindowOf(hwnd));
}
