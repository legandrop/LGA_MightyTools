#include "platform/HotkeyService.h"
#include "platform/win/InputOrigin.h"
#include "qa/HotkeyPriorityTest.h"

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

DWORD nativeModifier(const KBDLLHOOKSTRUCT &key)
{
    DWORD vk = key.vkCode;
    if (vk == VK_CONTROL) vk = key.flags & LLKHF_EXTENDED ? VK_RCONTROL : VK_LCONTROL;
    if (vk == VK_MENU) vk = key.flags & LLKHF_EXTENDED ? VK_RMENU : VK_LMENU;
    if (vk == VK_SHIFT) vk = key.scanCode == 0x36 ? VK_RSHIFT : VK_LSHIFT;
    return vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_LMENU || vk == VK_RMENU
        || vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_LWIN || vk == VK_RWIN ? vk : 0;
}

QSet<DWORD> modifierSnapshot()
{
    QSet<DWORD> keys;
    for (DWORD vk : {VK_LCONTROL, VK_RCONTROL, VK_LMENU, VK_RMENU,
                     VK_LSHIFT, VK_RSHIFT, VK_LWIN, VK_RWIN}) {
        if (GetAsyncKeyState(static_cast<int>(vk)) & 0x8000) keys.insert(vk);
    }
    return keys;
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

    static void selfTest(const std::function<void(bool, const QString &)> &check);

    bool begin()
    {
        start();
        m_ready.acquire();
        if (!m_installed) wait();
        return m_installed;
    }

    DWORD error() const { return m_error; }

    void setFilter(std::function<bool()> filter)
    {
        QMutexLocker lock(&m_mutex);
        m_filter = std::move(filter);
    }

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
        m_modifiersDown = modifierSnapshot();
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
        // Una tecla fisica normal resincroniza liberaciones perdidas al bloquear la sesion.
        // No consultar el estado asincrono durante una macro ni al cambiar el propio modificador.
        QSet<DWORD> physicalModifiers;
        const bool refresh = !(key->flags & LLKHF_INJECTED) && !nativeModifier(*key);
        if (refresh) physicalModifiers = modifierSnapshot();
        if (self->consumeKey(message, *key, [self](int id, quint64 generation) {
                return PostThreadMessageW(self->m_ownerThread, kPriorityMessage,
                                          static_cast<WPARAM>(id + kIdOffset),
                                          static_cast<LPARAM>(generation)) != 0;
            }, refresh ? &physicalModifiers : nullptr)) return 1;
        return CallNextHookEx(nullptr, code, message, data);
    }

    bool consumeKey(WPARAM message, const KBDLLHOOKSTRUCT &key,
                    const std::function<bool(int, quint64)> &deliver,
                    const QSet<DWORD> *physicalModifiers = nullptr)
    {
        const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
        const bool up = message == WM_KEYUP || message == WM_SYSKEYUP;
        if (!down && !up) return false;
        if (physicalModifiers) m_modifiersDown = *physicalModifiers;

        // El hook corre antes de actualizar GetAsyncKeyState: seguir la secuencia de la macro.
        const DWORD modifier = nativeModifier(key);
        if (modifier) {
            if (down) m_modifiersDown.insert(modifier);
            else m_modifiersDown.remove(modifier);
        }
        // Seguir tambien nuestras liberaciones de modificadores, pero nunca activar por inyeccion propia.
        const bool injected = key.flags & LLKHF_INJECTED;
        if (injected && key.dwExtraInfo == kMightyToolsInputTag) return false;

        QMutexLocker lock(&m_mutex);
        const quint64 keyToken = key.vkCode | (quint64(injected) << 32);
        if (up && m_swallowed.remove(keyToken)) return true;
        if (!down) return false;
        if (m_swallowed.contains(keyToken)) return true;

        UINT modifiers = 0;
        if (m_modifiersDown.contains(VK_LCONTROL) || m_modifiersDown.contains(VK_RCONTROL)) modifiers |= MOD_CONTROL;
        if (m_modifiersDown.contains(VK_LMENU) || m_modifiersDown.contains(VK_RMENU)) modifiers |= MOD_ALT;
        if (m_modifiersDown.contains(VK_LSHIFT) || m_modifiersDown.contains(VK_RSHIFT)) modifiers |= MOD_SHIFT;
        if (m_modifiersDown.contains(VK_LWIN) || m_modifiersDown.contains(VK_RWIN)) modifiers |= MOD_WIN;
        for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
            if (it->key != key.vkCode || it->modifiers != modifiers) continue;
            // Sin filtro o con filtro aceptado se come la combinacion; si no, sigue al proximo hook.
            if ((!m_filter || m_filter()) && deliver(it.key(), it->generation)) {
                m_swallowed.insert(keyToken);
                return true;
            }
            break;
        }
        return false;
    }

    DWORD m_ownerThread;
    QMutex m_mutex;
    QHash<int, Entry> m_entries;
    std::function<bool()> m_filter;
    QSet<quint64> m_swallowed;
    QSet<DWORD> m_modifiersDown;
    QSemaphore m_ready;
    bool m_installed = false;
    DWORD m_error = 0;
    static thread_local PriorityHookThread *s_current;
};

thread_local PriorityHookThread *PriorityHookThread::s_current = nullptr;

void PriorityHookThread::selfTest(const std::function<void(bool, const QString &)> &check)
{
    PriorityHookThread receiver(0);
    const Shortcut obs = Shortcut::fromPortableString(QStringLiteral("Meta+Ctrl+Alt+Shift+O"));
    receiver.add(12, obs, 41);
    int activations = 0;
    int lastId = 0;
    quint64 lastGeneration = 0;
    bool deliveryAllowed = true;
    const auto deliver = [&](int id, quint64 generation) {
        if (!deliveryAllowed) return false;
        ++activations;
        lastId = id;
        lastGeneration = generation;
        return true;
    };
    const auto send = [&](DWORD vk, bool up = false, DWORD flags = LLKHF_INJECTED,
                          ULONG_PTR tag = 0, DWORD scan = 0, bool systemKey = false) {
        KBDLLHOOKSTRUCT event{};
        event.vkCode = vk;
        event.scanCode = scan;
        event.flags = flags | (up ? LLKHF_UP : 0);
        event.dwExtraInfo = tag;
        const WPARAM message = systemKey ? (up ? WM_SYSKEYUP : WM_SYSKEYDOWN) : (up ? WM_KEYUP : WM_KEYDOWN);
        return receiver.consumeKey(message, event, deliver);
    };
    const auto modifiers = [&](bool up, DWORD flags = LLKHF_INJECTED, ULONG_PTR tag = 0) {
        bool passed = true;
        for (DWORD vk : {VK_CONTROL, VK_MENU, VK_SHIFT, VK_LWIN}) passed &= !send(vk, up, flags, tag);
        return passed;
    };

    check(modifiers(false) && send('O') && activations == 1 && lastId == 12 && lastGeneration == 41,
          QStringLiteral("macro: cuatro modificadores seguidos y O activan OBS una vez"));
    check(send('O') && activations == 1, QStringLiteral("macro: la repeticion no vuelve a activar"));
    check(modifiers(true) && send('O', true) && receiver.m_swallowed.isEmpty(),
          QStringLiteral("macro: soltar modificadores antes de O conserva la liberacion interceptada"));
    check(!send('O') && !send('O', true) && activations == 1,
          QStringLiteral("macro: despues de soltar, O sola pasa sin activar"));
    check(modifiers(false) && send('O') && send('O', true) && modifiers(true) && activations == 2,
          QStringLiteral("macro: una segunda pulsacion completa vuelve a funcionar"));

    modifiers(false, 0);
    check(send('O', false, 0) && !send('O', true) && send('O', true, 0) && activations == 3,
          QStringLiteral("teclado: sigue funcionando y una liberacion inyectada no consume la fisica"));
    modifiers(true, 0);
    modifiers(false);
    check(!send('P') && !send('P', true) && activations == 3,
          QStringLiteral("macro: otra letra pasa sin interferencia"));
    check(!send('O', false, LLKHF_INJECTED, kMightyToolsInputTag)
              && !send('O', true, LLKHF_INJECTED, kMightyToolsInputTag) && activations == 3,
          QStringLiteral("inyeccion propia: devolver el mismo atajo no lo recaptura"));
    send('O');
    check(!send('O', true, LLKHF_INJECTED, kMightyToolsInputTag) && send('O', true) && activations == 4,
          QStringLiteral("inyeccion propia: su liberacion no consume la de una macro externa"));
    check(modifiers(true, LLKHF_INJECTED, kMightyToolsInputTag) && !send('O') && !send('O', true),
          QStringLiteral("inyeccion propia: liberar modificadores actualiza su estado sin activar"));

    send(VK_CONTROL, false, LLKHF_INJECTED | LLKHF_EXTENDED);
    send(VK_MENU, false, LLKHF_INJECTED | LLKHF_EXTENDED);
    send(VK_SHIFT, false, LLKHF_INJECTED, 0, 0x36);
    send(VK_RWIN);
    send(VK_LCONTROL);
    send(VK_LCONTROL, true);
    check(send('O') && send('O', true) && activations == 5,
          QStringLiteral("macro: modificadores derechos y soltar el izquierdo conservan la combinacion"));
    send(VK_CONTROL, true, LLKHF_INJECTED | LLKHF_EXTENDED);
    send(VK_MENU, true, LLKHF_INJECTED | LLKHF_EXTENDED);
    send(VK_SHIFT, true, LLKHF_INJECTED, 0, 0x36);
    send(VK_RWIN, true);
    check(receiver.m_modifiersDown.isEmpty(), QStringLiteral("macro: ambos lados quedan liberados"));

    modifiers(false, LLKHF_INJECTED | LLKHF_LOWER_IL_INJECTED);
    check(send('O', false, LLKHF_INJECTED | LLKHF_LOWER_IL_INJECTED)
              && send('O', true, LLKHF_INJECTED | LLKHF_LOWER_IL_INJECTED) && activations == 6,
          QStringLiteral("macro: se admite tambien la inyeccion externa de menor integridad"));
    deliveryAllowed = false;
    check(!send('O') && !send('O', true) && activations == 6,
          QStringLiteral("entrega fallida: no interceptar una accion que no llego a la UI"));
    deliveryAllowed = true;
    receiver.remove(12);
    check(!send('O') && !send('O', true) && activations == 6,
          QStringLiteral("disposicion apagada: el atajo deja de interceptarse"));
    receiver.add(12, obs, 42);
    check(send('O') && send('O', true) && lastGeneration == 42 && activations == 7,
          QStringLiteral("registro renovado: el receptor entrega la generacion vigente"));
    check(send('O', false, LLKHF_INJECTED | LLKHF_ALTDOWN, 0, 0, true)
              && send('O', true, LLKHF_INJECTED | LLKHF_ALTDOWN, 0, 0, true) && activations == 8,
          QStringLiteral("macro: los eventos de sistema con Alt activan e interceptan su liberacion"));
    modifiers(true);
    modifiers(false);
    KBDLLHOOKSTRUCT physicalKey{};
    physicalKey.vkCode = 'O';
    const QSet<DWORD> releasedSnapshot;
    check(!receiver.consumeKey(WM_KEYDOWN, physicalKey, deliver, &releasedSnapshot)
              && receiver.m_modifiersDown.isEmpty() && activations == 8,
          QStringLiteral("teclado: un estado fisico limpio corrige modificadores con liberaciones perdidas"));
    const QSet<DWORD> heldSnapshot{VK_RCONTROL, VK_RMENU, VK_RSHIFT, VK_RWIN};
    check(receiver.consumeKey(WM_KEYDOWN, physicalKey, deliver, &heldSnapshot)
              && send('O', true, 0) && activations == 9,
          QStringLiteral("teclado: los modificadores fisicos ya apretados tambien activan el atajo"));
    bool filterAccepts = false;
    receiver.setFilter([&filterAccepts]() { return filterAccepts; });
    check(!send('O', false, 0) && !send('O', true, 0) && activations == 9,
          QStringLiteral("filtro: si rechaza, la combinacion sigue al proximo hook sin activar"));
    filterAccepts = true;
    check(send('O', false, 0) && send('O', true, 0) && activations == 10,
          QStringLiteral("filtro: si acepta, la combinacion se intercepta y activa"));
}

void HotkeyPriorityTest::run(const std::function<void(bool, const QString &)> &check)
{
    PriorityHookThread::selfTest(check);
}

struct HotkeyService::Private : public QAbstractNativeEventFilter
{
    HotkeyService *q = nullptr;
    QHash<int, Shortcut> registered;
    QHash<int, quint64> priorityGeneration;
    quint64 nextGeneration = 0;
    std::unique_ptr<PriorityHookThread> priorityHook;
    std::function<bool()> priorityFilter;

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
        d->priorityHook->setFilter(d->priorityFilter);
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

void HotkeyService::setPriorityFilter(std::function<bool()> filter)
{
    d->priorityFilter = std::move(filter);
    if (d->priorityHook) d->priorityHook->setFilter(d->priorityFilter);
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
        input.ki.dwExtraInfo = kMightyToolsInputTag;
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
