#include "platform/NukeWatcher.h"

#include <QDebug>
#include <QFileInfo>
#include <QMetaObject>

#include <iterator>

#include <windows.h>
#include <dwmapi.h>

namespace {

NukeWatcher *g_instance = nullptr;

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

struct NukeWatcher::Private
{
    HWINEVENTHOOK hook = nullptr;

    static void CALLBACK onForeground(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG, DWORD, DWORD)
    {
        if (event != EVENT_SYSTEM_FOREGROUND || idObject != OBJID_WINDOW || !g_instance) {
            return;
        }
        const bool nuke = isNukeWindow(hwnd);
        NukeWatcher *watcher = g_instance;
        QMetaObject::invokeMethod(watcher, [watcher, nuke]() { watcher->setNukeInFront(nuke); }, Qt::QueuedConnection);
    }
};

NukeWatcher::NukeWatcher(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    g_instance = this;
    d->hook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, &Private::onForeground, 0, 0,
                              WINEVENT_OUTOFCONTEXT);
    if (!d->hook) {
        qWarning() << "[NukeWatcher] SetWinEventHook fallo:" << GetLastError();
    }
    m_nukeInFront = isNukeWindow(GetForegroundWindow());
    qInfo() << "[NukeWatcher] Nuke al frente al arrancar:" << m_nukeInFront;
}

NukeWatcher::~NukeWatcher()
{
    if (d->hook) {
        UnhookWinEvent(d->hook);
    }
    if (g_instance == this) {
        g_instance = nullptr;
    }
}

bool NukeWatcher::isNukeInFrontNow() const
{
    return isNukeWindow(GetForegroundWindow());
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
