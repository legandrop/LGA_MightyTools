#include "platform/ForegroundWatcher.h"

#include <QDebug>
#include <QFileInfo>
#include <QHash>
#include <QMetaObject>

#include <iterator>

#include <windows.h>

namespace {

// Registro compartido hook -> instancia, en vez de un puntero estatico unico por instancia (el
// `g_instance` que tenia NukeWatcherWin se pisaba si se instanciaba dos veces). El host tiene una sola
// instancia, pero el mapa admite varias sin que se pisen. El callback de Windows llega con SU PROPIO
// HWINEVENTHOOK, asi que nunca hace falta recorrer el mapa entero.
QHash<HWINEVENTHOOK, ForegroundWatcher *> &registry()
{
    static QHash<HWINEVENTHOOK, ForegroundWatcher *> map;
    return map;
}

QString exeNameOfWindow(HWND hwnd, DWORD *outPid)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (outPid) {
        *outPid = pid;
    }
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

} // namespace

// Nested a proposito: desde C++11 una clase anidada tiene acceso a los miembros privados de la
// clase que la contiene, y por eso el callback (que Windows exige como funcion C, sin "this") puede
// llamar al setter privado `ForegroundWatcher::setForeground` sin exponerlo en el header publico.
struct ForegroundWatcher::Private
{
    HWINEVENTHOOK hook = nullptr;

    static void CALLBACK onForeground(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG,
                                       DWORD, DWORD)
    {
        if (event != EVENT_SYSTEM_FOREGROUND || idObject != OBJID_WINDOW || !hwnd) {
            return;
        }
        ForegroundWatcher *watcher = registry().value(hook, nullptr);
        if (!watcher) {
            return;
        }
        DWORD pid = 0;
        const QString exeName = exeNameOfWindow(hwnd, &pid);
        const quintptr hwndValue = reinterpret_cast<quintptr>(hwnd);
        const quint32 pidValue = static_cast<quint32>(pid);
        QMetaObject::invokeMethod(watcher, [watcher, hwndValue, pidValue, exeName]() {
            watcher->setForeground(hwndValue, pidValue, exeName);
        }, Qt::QueuedConnection);
    }
};

ForegroundWatcher::ForegroundWatcher(bool active, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    if (!active) {
        // Corrida automatizada (--self-test, --ui-shot, --simulate-action, medicion de consumo): ni
        // hook ni consulta al sistema. El objeto existe solo para ejercitar el ciclo de vida.
        return;
    }

    d->hook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
                              &Private::onForeground, 0, 0, WINEVENT_OUTOFCONTEXT);
    if (d->hook) {
        registry().insert(d->hook, this);
    } else {
        qWarning() << "[ForegroundWatcher] SetWinEventHook fallo:" << GetLastError();
    }

    if (HWND foreground = GetForegroundWindow()) {
        DWORD pid = 0;
        m_exeName = exeNameOfWindow(foreground, &pid);
        m_hwnd = reinterpret_cast<quintptr>(foreground);
        m_pid = static_cast<quint32>(pid);
    }
}

ForegroundWatcher::~ForegroundWatcher()
{
    if (d->hook) {
        registry().remove(d->hook);
        UnhookWinEvent(d->hook);
    }
}

int ForegroundWatcher::installedHooks()
{
    return int(registry().size());
}

void ForegroundWatcher::setForeground(quintptr hwnd, quint32 pid, const QString &exeName)
{
    if (m_hwnd == hwnd) {
        return;
    }
    m_hwnd = hwnd;
    m_pid = pid;
    m_exeName = exeName;
    emit foregroundChanged(hwnd, pid, exeName);
}
