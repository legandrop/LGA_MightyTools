#include "platform/WindowPlacement.h"
#include "core/I18n.h"

#include <QFileInfo>
#include <QGuiApplication>
#include <QScreen>
#include <QVarLengthArray>

#include <dwmapi.h>
#include <shellapi.h>
#include <windows.h>

#include <cstdlib>

namespace {

QRect fromNative(const RECT &rect)
{
    // RECT usa extremos exclusivos; QRect(QPoint, QPoint) perderia un pixel.
    return QRect(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
}

RECT toNative(const QRect &rect)
{
    return RECT{rect.left(), rect.top(), rect.x() + rect.width(), rect.y() + rect.height()};
}

bool visibleBounds(HWND hwnd, RECT *bounds)
{
    RECT visible{};
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &visible, sizeof(visible)))
        && visible.right > visible.left && visible.bottom > visible.top) {
        *bounds = visible;
        return true;
    }
    return GetWindowRect(hwnd, bounds) && bounds->right > bounds->left && bounds->bottom > bounds->top;
}

bool isCloaked(HWND hwnd)
{
    DWORD cloaked = 0;
    return SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked;
}

bool isOrdinaryWindow(HWND hwnd)
{
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd) || isCloaked(hwnd)) {
        return false;
    }
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if ((style & WS_CHILD) || (exStyle & WS_EX_TOOLWINDOW)
        || ((exStyle & WS_EX_TRANSPARENT) && (exStyle & WS_EX_LAYERED))) {
        return false;
    }
    wchar_t className[256] = {};
    GetClassNameW(hwnd, className, 256);
    return wcscmp(className, L"Progman") && wcscmp(className, L"WorkerW")
        && wcscmp(className, L"Shell_TrayWnd") && wcscmp(className, L"Shell_SecondaryTrayWnd");
}

QString titleOf(HWND hwnd)
{
    const int length = GetWindowTextLengthW(hwnd);
    if (length <= 0) {
        return {};
    }
    QVarLengthArray<wchar_t, 256> buffer(length + 1);
    const int copied = GetWindowTextW(hwnd, buffer.data(), length + 1);
    return copied > 0 ? QString::fromWCharArray(buffer.data(), copied).trimmed() : QString();
}

QString processName(DWORD pid)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) {
        return {};
    }
    QVarLengthArray<wchar_t, 512> buffer(32768);
    DWORD length = DWORD(buffer.size());
    const bool ok = QueryFullProcessImageNameW(process, 0, buffer.data(), &length);
    CloseHandle(process);
    return ok ? QFileInfo(QString::fromWCharArray(buffer.data(), int(length))).fileName() : QString();
}

QList<QRect> logicalPieces(const RECT &native)
{
    QList<QRect> pieces;
    // Cada trozo conserva el DPR y el origen del monitor donde se ve.
    for (const QScreen *screen : QGuiApplication::screens()) {
        const QRect logical = screen->geometry();
        const POINT origin{logical.x(), logical.y()};
        const HMONITOR handle = MonitorFromPoint(origin, MONITOR_DEFAULTTONULL);
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        const qreal dpr = screen->devicePixelRatio() > 0 ? screen->devicePixelRatio() : 1.0;
        RECT clipped{};
        if (!handle || !GetMonitorInfoW(handle, &monitor)
            || monitor.rcMonitor.left != logical.x() || monitor.rcMonitor.top != logical.y()
            || qAbs(qRound((monitor.rcMonitor.right - monitor.rcMonitor.left) / dpr) - logical.width()) > 2
            || qAbs(qRound((monitor.rcMonitor.bottom - monitor.rcMonitor.top) / dpr) - logical.height()) > 2
            || !IntersectRect(&clipped, &native, &monitor.rcMonitor)) {
            continue;
        }
        const int left = logical.x() + qRound((clipped.left - monitor.rcMonitor.left) / dpr);
        const int top = logical.y() + qRound((clipped.top - monitor.rcMonitor.top) / dpr);
        const int right = logical.x() + qRound((clipped.right - monitor.rcMonitor.left) / dpr);
        const int bottom = logical.y() + qRound((clipped.bottom - monitor.rcMonitor.top) / dpr);
        const QRect piece(left, top, right - left, bottom - top);
        if (!piece.isEmpty()) {
            pieces.append(piece);
        }
    }
    return pieces;
}

struct WindowList
{
    QList<WindowPlacement::WindowInfo> items;
    RECT desktop{};
};

BOOL CALLBACK collectWindow(HWND hwnd, LPARAM parameter)
{
    auto *list = reinterpret_cast<WindowList *>(parameter);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid || pid == GetCurrentProcessId() || !isOrdinaryWindow(hwnd)) {
        return TRUE;
    }
    RECT bounds{}, overlap{};
    if (!visibleBounds(hwnd, &bounds) || !IntersectRect(&overlap, &bounds, &list->desktop)) {
        return TRUE;
    }
    const QList<QRect> pieces = logicalPieces(bounds);
    if (pieces.isEmpty()) {
        return TRUE;
    }
    QString title = titleOf(hwnd);
    if (title.isEmpty()) {
        title = processName(pid);
    }
    if (title.isEmpty()) {
        title = I18n::tr("Untitled window");
    }
    for (const QRect &piece : pieces) {
        list->items.append({title, piece});
    }
    return TRUE;
}

bool elevated(HANDLE process, bool *value)
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &token)) {
        return false;
    }
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    const bool ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
    CloseHandle(token);
    if (ok) {
        *value = elevation.TokenIsElevated != 0;
    }
    return ok;
}

bool targetNeedsElevation(HWND hwnd)
{
    bool own = false;
    if (!elevated(GetCurrentProcess(), &own) || own) {
        return false;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) {
        return false;
    }
    bool target = false;
    const bool known = elevated(process, &target);
    CloseHandle(process);
    return known && target;
}

QRect availableArea(const MONITORINFO &monitor)
{
    RECT work = monitor.rcWork;
    // rcWork puede reservar una franja para activar una barra autooculta. Solo se
    // recupera esa franja; una barra visible u otra appbar siguen descontadas.
    for (UINT edge : {ABE_LEFT, ABE_TOP, ABE_RIGHT, ABE_BOTTOM}) {
        APPBARDATA bar{};
        bar.cbSize = sizeof(bar);
        bar.uEdge = edge;
        bar.rc = monitor.rcMonitor;
        if (!SHAppBarMessage(ABM_GETAUTOHIDEBAREX, &bar)) {
            continue;
        }
        if (edge == ABE_LEFT && work.left >= monitor.rcMonitor.left
            && work.left - monitor.rcMonitor.left <= 8) {
            work.left = monitor.rcMonitor.left;
        } else if (edge == ABE_TOP && work.top >= monitor.rcMonitor.top
                   && work.top - monitor.rcMonitor.top <= 8) {
            work.top = monitor.rcMonitor.top;
        } else if (edge == ABE_RIGHT && work.right <= monitor.rcMonitor.right
                   && monitor.rcMonitor.right - work.right <= 8) {
            work.right = monitor.rcMonitor.right;
        } else if (edge == ABE_BOTTOM && work.bottom <= monitor.rcMonitor.bottom
                   && monitor.rcMonitor.bottom - work.bottom <= 8) {
            work.bottom = monitor.rcMonitor.bottom;
        }
    }
    return fromNative(work);
}

void fail(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

} // namespace

namespace WindowPlacement {

QList<WindowInfo> selectableWindows(QString *error)
{
    if (error) error->clear();
    WindowList list;
    list.desktop = RECT{GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                        GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
                        GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
    if (!EnumWindows(&collectWindow, reinterpret_cast<LPARAM>(&list))) {
        fail(error, I18n::tr("Could not list open windows."));
    } else if (list.items.isEmpty()) {
        fail(error, I18n::tr("No selectable windows were found."));
    }
    return list.items;
}

QRect pickerWorkAreaAt(const QPoint &globalLogical)
{
    QScreen *screen = QGuiApplication::screenAt(globalLogical);
    if (!screen) {
        return {};
    }
    const QRect logicalScreen = screen->geometry();
    const POINT origin{logicalScreen.x(), logicalScreen.y()};
    const HMONITOR handle = MonitorFromPoint(origin, MONITOR_DEFAULTTONULL);
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    const qreal dpr = screen->devicePixelRatio() > 0 ? screen->devicePixelRatio() : 1.0;
    if (!handle || !GetMonitorInfoW(handle, &monitor)
        || monitor.rcMonitor.left != logicalScreen.x()
        || monitor.rcMonitor.top != logicalScreen.y()
        || qAbs(qRound((monitor.rcMonitor.right - monitor.rcMonitor.left) / dpr) - logicalScreen.width()) > 2
        || qAbs(qRound((monitor.rcMonitor.bottom - monitor.rcMonitor.top) / dpr) - logicalScreen.height()) > 2) {
        // Si Qt y Win32 no concuerdan sobre el origen del monitor, no se
        // transforma una region nativa con el DPR de otra pantalla.
        return screen->availableGeometry();
    }
    const QRect native = availableArea(monitor);
    const int left = logicalScreen.x() + qRound((native.left() - monitor.rcMonitor.left) / dpr);
    const int top = logicalScreen.y() + qRound((native.top() - monitor.rcMonitor.top) / dpr);
    const int right = logicalScreen.x() + qRound((native.x() + native.width() - monitor.rcMonitor.left) / dpr);
    const int bottom = logicalScreen.y() + qRound((native.y() + native.height() - monitor.rcMonitor.top) / dpr);
    return QRect(left, top, right - left, bottom - top);
}

bool moveForeground(const QRectF &normalizedArea, QString *error, bool dryRun)
{
    if (error) error->clear();
    if (resolve(QRect(0, 0, 10000, 10000), normalizedArea).isEmpty()) {
        fail(error, I18n::tr("Choose a valid area."));
        return false;
    }
    if (dryRun) {
        return true;
    }

    HWND hwnd = GetForegroundWindow();
    DWORD pid = 0;
    if (hwnd) GetWindowThreadProcessId(hwnd, &pid);
    if (!hwnd || pid == GetCurrentProcessId() || !isOrdinaryWindow(hwnd)) {
        fail(error, I18n::tr("Switch to a movable app window and try again."));
        return false;
    }
    if (targetNeedsElevation(hwnd)) {
        fail(error, I18n::tr("Windows does not allow moving an administrator window from this app."));
        return false;
    }
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) {
        fail(error, I18n::tr("Could not read the window's monitor."));
        return false;
    }
    const QRect desired = resolve(availableArea(monitor), normalizedArea);
    if (desired.isEmpty()) {
        fail(error, I18n::tr("The selected area is too small for this monitor."));
        return false;
    }
    const bool wasMaximized = IsZoomed(hwnd);
    RECT original{};
    if (!GetWindowRect(hwnd, &original)) {
        fail(error, I18n::tr("Could not read the active window."));
        return false;
    }
    if (wasMaximized) {
        ShowWindow(hwnd, SW_RESTORE);
    }
    RECT outer{}, visible{};
    if (!GetWindowRect(hwnd, &outer) || !visibleBounds(hwnd, &visible)) {
        if (wasMaximized) ShowWindow(hwnd, SW_MAXIMIZE);
        fail(error, I18n::tr("Could not read the active window."));
        return false;
    }
    const int extraLeft = visible.left - outer.left;
    const int extraTop = visible.top - outer.top;
    const int extraRight = outer.right - visible.right;
    const int extraBottom = outer.bottom - visible.bottom;
    const RECT destination = toNative(desired);
    if (!SetWindowPos(hwnd, nullptr, destination.left - extraLeft, destination.top - extraTop,
                      desired.width() + extraLeft + extraRight, desired.height() + extraTop + extraBottom,
                      SWP_NOZORDER | SWP_NOACTIVATE)) {
        const DWORD code = GetLastError();
        if (wasMaximized) ShowWindow(hwnd, SW_MAXIMIZE);
        fail(error, code == ERROR_ACCESS_DENIED
                        ? I18n::tr("Windows denied access to this window.")
                        : I18n::tr("This window could not be moved or resized."));
        return false;
    }
    RECT actual{};
    if (!visibleBounds(hwnd, &actual) || std::abs(actual.left - destination.left) > 2
        || std::abs(actual.top - destination.top) > 2 || std::abs(actual.right - destination.right) > 2
        || std::abs(actual.bottom - destination.bottom) > 2) {
        if (wasMaximized) {
            ShowWindow(hwnd, SW_MAXIMIZE);
        } else {
            SetWindowPos(hwnd, nullptr, original.left, original.top,
                         original.right - original.left, original.bottom - original.top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        fail(error, I18n::tr("This app limits the window's position or minimum size."));
        return false;
    }
    return true;
}

} // namespace WindowPlacement
