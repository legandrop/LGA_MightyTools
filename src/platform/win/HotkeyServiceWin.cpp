#include "platform/HotkeyService.h"

#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QDebug>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QSemaphore>
#include <QSet>
#include <QThread>

#include <windows.h>

namespace {

// Los ids que ve Windows. Se corren para no chocar con otros RegisterHotKey del mismo hilo.
constexpr int kIdOffset = 100;
constexpr int kProbeId = 99;
constexpr UINT kPriorityMessage = WM_APP + 0x143;

UINT nativeModifiers(Qt::KeyboardModifiers modifiers)
{
    // MOD_NOREPEAT: dejar apretado el atajo no dispara la accion en rafaga.
    UINT mods = MOD_NOREPEAT;
    if (modifiers & Qt::ControlModifier) mods |= MOD_CONTROL;
    if (modifiers & Qt::AltModifier) mods |= MOD_ALT;
    if (modifiers & Qt::ShiftModifier) mods |= MOD_SHIFT;
    if (modifiers & Qt::MetaModifier) mods |= MOD_WIN;
    return mods;
}

// Letras y digitos coinciden con su virtual-key ('A'..'Z', '0'..'9'); F1-F12 son VK_F1 + n.
UINT nativeKey(int key)
{
    if ((key >= Qt::Key_A && key <= Qt::Key_Z) || (key >= Qt::Key_0 && key <= Qt::Key_9)) {
        return static_cast<UINT>(key);
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F12) {
        return static_cast<UINT>(VK_F1 + (key - Qt::Key_F1));
    }
    if (key == Qt::Key_Left) return VK_LEFT;
    if (key == Qt::Key_Right) return VK_RIGHT;
    if (key == Qt::Key_Up) return VK_UP;
    if (key == Qt::Key_Down) return VK_DOWN;
    return 0;
}

} // namespace

// El hook vive fuera del hilo de la UI: Windows lo quita sin aviso si su callback tarda demasiado.
class PriorityHookThread : public QThread
{
public:
    struct Entry
    {
        UINT key;
        UINT modifiers;
        quint64 generation;
    };

    explicit PriorityHookThread(DWORD ownerThread) : m_ownerThread(ownerThread) {}

    bool begin()
    {
        start();
        m_ready.acquire();
        if (!m_installed) wait();
        return m_installed;
    }

    DWORD error() const { return m_error; }

    void add(int id, const Shortcut &shortcut, quint64 generation)
    {
        QMutexLocker lock(&m_mutex);
        m_entries.insert(id, {nativeKey(shortcut.key), nativeModifiers(shortcut.modifiers) & ~MOD_NOREPEAT,
                              generation});
    }

    void remove(int id)
    {
        QMutexLocker lock(&m_mutex);
        m_entries.remove(id);
    }

    void stop()
    {
        quit();
        wait();
    }

protected:
    void run() override
    {
        s_current = this;
        HHOOK hook = SetWindowsHookExW(WH_KEYBOARD_LL, &PriorityHookThread::onKey,
                                       GetModuleHandleW(nullptr), 0);
        m_installed = hook != nullptr;
        m_error = m_installed ? 0 : GetLastError();
        m_ready.release();
        if (hook) {
            exec();
            UnhookWindowsHookEx(hook);
        }
        s_current = nullptr;
    }

private:
    static LRESULT CALLBACK onKey(int code, WPARAM message, LPARAM data)
    {
        PriorityHookThread *self = s_current;
        if (!self || code != HC_ACTION) return CallNextHookEx(nullptr, code, message, data);
        const auto *key = reinterpret_cast<const KBDLLHOOKSTRUCT *>(data);
        if (key->flags & LLKHF_INJECTED) return CallNextHookEx(nullptr, code, message, data);
        const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
        const bool up = message == WM_KEYUP || message == WM_SYSKEYUP;
        if (!down && !up) return CallNextHookEx(nullptr, code, message, data);

        QMutexLocker lock(&self->m_mutex);
        if (up && self->m_swallowed.remove(key->vkCode)) return 1;
        if (!down) {
            lock.unlock();
            return CallNextHookEx(nullptr, code, message, data);
        }
        if (self->m_swallowed.contains(key->vkCode)) return 1;

        UINT modifiers = 0;
        if (GetAsyncKeyState(VK_CONTROL) & 0x8000) modifiers |= MOD_CONTROL;
        if ((GetAsyncKeyState(VK_MENU) & 0x8000) || (key->flags & LLKHF_ALTDOWN)) modifiers |= MOD_ALT;
        if (GetAsyncKeyState(VK_SHIFT) & 0x8000) modifiers |= MOD_SHIFT;
        if ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) modifiers |= MOD_WIN;
        for (auto it = self->m_entries.cbegin(); it != self->m_entries.cend(); ++it) {
            if (it->key != key->vkCode || it->modifiers != modifiers) continue;
            if (PostThreadMessageW(self->m_ownerThread, kPriorityMessage,
                                   static_cast<WPARAM>(it.key() + kIdOffset),
                                   static_cast<LPARAM>(it->generation))) {
                self->m_swallowed.insert(key->vkCode);
                return 1;
            }
            break;
        }
        lock.unlock();
        return CallNextHookEx(nullptr, code, message, data);
    }

    DWORD m_ownerThread;
    QMutex m_mutex;
    QHash<int, Entry> m_entries;
    QSet<DWORD> m_swallowed;
    QSemaphore m_ready;
    bool m_installed = false;
    DWORD m_error = 0;
    static thread_local PriorityHookThread *s_current;
};

thread_local PriorityHookThread *PriorityHookThread::s_current = nullptr;

struct HotkeyService::Private : public QAbstractNativeEventFilter
{
    HotkeyService *q = nullptr;
    QHash<int, Shortcut> registered;
    QHash<int, quint64> priorityGeneration;
    quint64 nextGeneration = 0;
    std::unique_ptr<PriorityHookThread> priorityHook;

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr * /*result*/) override
    {
        if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG") {
            return false;
        }
        const MSG *msg = static_cast<const MSG *>(message);
        if (msg->message == kPriorityMessage) {
            const int id = static_cast<int>(msg->wParam) - kIdOffset;
            if (priorityGeneration.value(id) == static_cast<quint64>(msg->lParam)
                && priorityGeneration.contains(id)) emit q->activated(id);
            return true;
        }
        if (msg->message != WM_HOTKEY) {
            return false;
        }
        const int id = static_cast<int>(msg->wParam) - kIdOffset;
        if (!registered.contains(id)) {
            return false;
        }
        emit q->activated(id);
        return true;
    }
};

HotkeyService::HotkeyService(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->q = this;
    QCoreApplication::instance()->installNativeEventFilter(d.get());
}

HotkeyService::~HotkeyService()
{
    unregisterAll();
    if (QCoreApplication::instance()) {
        QCoreApplication::instance()->removeNativeEventFilter(d.get());
    }
}

bool HotkeyService::registerHotkey(int id, const Shortcut &shortcut)
{
    unregisterHotkey(id);
    const UINT vk = nativeKey(shortcut.key);
    if (vk == 0) {
        qWarning() << "[HotkeyService] Tecla no soportada:" << shortcut.toPortableString();
        return false;
    }
    if (!RegisterHotKey(nullptr, id + kIdOffset, nativeModifiers(shortcut.modifiers), vk)) {
        qWarning() << "[HotkeyService] Windows rechazo" << shortcut.toPortableString()
                   << "(ya la tiene otra app?) error:" << GetLastError();
        return false;
    }
    d->registered.insert(id, shortcut);
    qDebug() << "[HotkeyService] Registrado" << shortcut.toPortableString() << "id" << id;
    return true;
}

bool HotkeyService::registerPriorityHotkey(int id, const Shortcut &shortcut)
{
    unregisterHotkey(id);
    if (nativeKey(shortcut.key) == 0) return false;
    if (!d->priorityHook) {
        d->priorityHook = std::make_unique<PriorityHookThread>(GetCurrentThreadId());
        if (!d->priorityHook->begin()) {
            qWarning() << "[HotkeyService] No se pudo instalar el hook de prioridad:"
                       << d->priorityHook->error();
            d->priorityHook.reset();
            return false;
        }
    }
    const quint64 generation = ++d->nextGeneration;
    d->priorityHook->add(id, shortcut, generation);
    d->priorityGeneration.insert(id, generation);
    d->registered.insert(id, shortcut);
    qInfo() << "[HotkeyService] Prioridad solicitada para" << shortcut.toPortableString() << "id" << id;
    return true;
}

void HotkeyService::unregisterHotkey(int id)
{
    if (!d->registered.contains(id)) {
        return;
    }
    if (d->priorityGeneration.remove(id)) {
        d->priorityHook->remove(id);
        if (d->priorityGeneration.isEmpty()) {
            d->priorityHook->stop();
            d->priorityHook.reset();
        }
    } else {
        UnregisterHotKey(nullptr, id + kIdOffset);
    }
    qDebug() << "[HotkeyService] Liberado" << d->registered.value(id).toPortableString() << "id" << id;
    d->registered.remove(id);
}

void HotkeyService::unregisterAll()
{
    const QList<int> ids = d->registered.keys();
    for (const int id : ids) {
        unregisterHotkey(id);
    }
}

bool HotkeyService::isRegistered(int id) const
{
    return d->registered.contains(id);
}

void HotkeyService::passThrough(const Shortcut &shortcut)
{
    const UINT vk = nativeKey(shortcut.key);
    if (vk == 0) {
        return;
    }
    // Los modificadores del atajo que el usuario ya no tiene apretados se aprietan y se sueltan
    // alrededor de la tecla; los que sigue apretando se dejan como estan.
    struct Modifier
    {
        Qt::KeyboardModifier qt;
        WORD vk;
    };
    const Modifier modifiers[] = {{Qt::ControlModifier, VK_CONTROL},
                                  {Qt::AltModifier, VK_MENU},
                                  {Qt::ShiftModifier, VK_SHIFT},
                                  {Qt::MetaModifier, VK_LWIN}};
    INPUT inputs[10] = {};
    int count = 0;
    WORD pressed[4] = {};
    int pressedCount = 0;
    const auto key = [&inputs, &count](WORD code, bool up) {
        INPUT &input = inputs[count++];
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = code;
        input.ki.dwFlags = (up ? KEYEVENTF_KEYUP : 0) | (code == VK_LWIN ? KEYEVENTF_EXTENDEDKEY : 0);
    };
    for (const Modifier &modifier : modifiers) {
        if ((shortcut.modifiers & modifier.qt) && !(GetAsyncKeyState(modifier.vk) & 0x8000)) {
            key(modifier.vk, false);
            pressed[pressedCount++] = modifier.vk;
        }
    }
    key(static_cast<WORD>(vk), false);
    key(static_cast<WORD>(vk), true);
    for (int i = pressedCount - 1; i >= 0; --i) {
        key(pressed[i], true);
    }
    SendInput(static_cast<UINT>(count), inputs, sizeof(INPUT));
    qInfo() << "[HotkeyService] Combinacion devuelta a la app del frente:" << shortcut.toPortableString();
}

bool HotkeyService::probe(const Shortcut &shortcut)
{
    const UINT vk = nativeKey(shortcut.key);
    if (vk == 0) {
        return false;
    }
    if (!RegisterHotKey(nullptr, kProbeId + kIdOffset, nativeModifiers(shortcut.modifiers), vk)) {
        return false;
    }
    UnregisterHotKey(nullptr, kProbeId + kIdOffset);
    return true;
}
