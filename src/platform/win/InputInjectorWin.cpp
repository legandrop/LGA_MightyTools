#include "platform/InputInjector.h"

#include <windows.h>

namespace {

// Tecla virtual sin asignar. Se aprieta y suelta antes de soltar Alt o Win: si no, soltar Alt solo
// activa la barra de menu de la ventana, y soltar Win solo abre el menu Inicio.
constexpr WORD kMaskKey = 0xE8;

INPUT keyInput(WORD vk, bool up)
{
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    // Las flechas y las teclas de la derecha son "extendidas": sin la marca, Down llega como el 2
    // del teclado numerico.
    if (vk == VK_DOWN || vk == VK_UP || vk == VK_LEFT || vk == VK_RIGHT || vk == VK_RCONTROL || vk == VK_RMENU
        || vk == VK_RWIN || vk == VK_LWIN) {
        input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    }
    return input;
}

void sendKey(WORD vk, bool up)
{
    INPUT input = keyInput(vk, up);
    SendInput(1, &input, sizeof(INPUT));
}

bool isDown(int vk)
{
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

WORD virtualKey(InputInjector::Key key)
{
    switch (key) {
    case InputInjector::Key::Down:
        return VK_DOWN;
    case InputInjector::Key::Return:
        return VK_RETURN;
    case InputInjector::Key::A:
        return 'A';
    case InputInjector::Key::F:
        return 'F';
    }
    return 0;
}

} // namespace

QPoint InputInjector::cursorPos() const
{
    POINT point{};
    GetCursorPos(&point);
    return QPoint(point.x, point.y);
}

void InputInjector::moveCursor(const QPoint &nativePoint)
{
    note(QStringLiteral("move %1,%2").arg(nativePoint.x()).arg(nativePoint.y()));
    if (!m_dryRun) {
        SetCursorPos(nativePoint.x(), nativePoint.y());
    }
}

void InputInjector::clickAt(Button button, const QPoint &nativePoint)
{
    moveCursor(nativePoint);
    note(QStringLiteral("click %1").arg(button == Button::Left ? QStringLiteral("left") : QStringLiteral("right")));
    if (m_dryRun) {
        return;
    }
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_MOUSE;
    inputs[1].type = INPUT_MOUSE;
    inputs[0].mi.dwFlags = button == Button::Left ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_RIGHTDOWN;
    inputs[1].mi.dwFlags = button == Button::Left ? MOUSEEVENTF_LEFTUP : MOUSEEVENTF_RIGHTUP;
    SendInput(2, inputs, sizeof(INPUT));
}

void InputInjector::releaseModifiers()
{
    struct Modifier
    {
        int vk;
        const char *name;
    };
    static const Modifier modifiers[] = {
        {VK_LCONTROL, "LCtrl"}, {VK_RCONTROL, "RCtrl"}, {VK_LSHIFT, "LShift"}, {VK_RSHIFT, "RShift"},
        {VK_LMENU, "LAlt"},     {VK_RMENU, "RAlt"},     {VK_LWIN, "LWin"},     {VK_RWIN, "RWin"},
    };
    QStringList held;
    bool needsMask = false;
    for (const Modifier &modifier : modifiers) {
        if (isDown(modifier.vk)) {
            held << QLatin1String(modifier.name);
            needsMask = needsMask || modifier.vk == VK_LMENU || modifier.vk == VK_RMENU || modifier.vk == VK_LWIN
                        || modifier.vk == VK_RWIN;
        }
    }
    note(QStringLiteral("release modifiers [%1]").arg(held.join(QLatin1Char(' '))));
    if (m_dryRun || held.isEmpty()) {
        return;
    }
    if (needsMask) {
        sendKey(kMaskKey, false);
        sendKey(kMaskKey, true);
    }
    for (const Modifier &modifier : modifiers) {
        if (isDown(modifier.vk)) {
            sendKey(static_cast<WORD>(modifier.vk), true);
        }
    }
}

void InputInjector::tapKey(Key key, bool withPrimary)
{
    note(QStringLiteral("key %1%2").arg(withPrimary ? QStringLiteral("Ctrl+") : QString(), keyName(key)));
    if (m_dryRun) {
        return;
    }
    const WORD vk = virtualKey(key);
    INPUT inputs[4] = {};
    int count = 0;
    if (withPrimary) {
        inputs[count++] = keyInput(VK_CONTROL, false);
    }
    inputs[count++] = keyInput(vk, false);
    inputs[count++] = keyInput(vk, true);
    if (withPrimary) {
        inputs[count++] = keyInput(VK_CONTROL, true);
    }
    SendInput(static_cast<UINT>(count), inputs, sizeof(INPUT));
}
