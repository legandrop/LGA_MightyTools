#include "platform/HotkeyService.h"

#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QDebug>
#include <QHash>

#include <windows.h>

namespace {

// Los ids que ve Windows. Se corren para no chocar con otros RegisterHotKey del mismo hilo.
constexpr int kIdOffset = 100;
constexpr int kProbeId = 99;

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
    return 0;
}

} // namespace

struct HotkeyService::Private : public QAbstractNativeEventFilter
{
    HotkeyService *q = nullptr;
    QHash<int, Shortcut> registered;

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr * /*result*/) override
    {
        if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG") {
            return false;
        }
        const MSG *msg = static_cast<const MSG *>(message);
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

void HotkeyService::unregisterHotkey(int id)
{
    if (!d->registered.contains(id)) {
        return;
    }
    UnregisterHotKey(nullptr, id + kIdOffset);
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
