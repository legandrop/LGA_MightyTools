#include "platform/MouseButtonsSystem.h"

#include "core/AutomatedRun.h"
#include "platform/win/InputOrigin.h"

#include <QDebug>
#include <QSemaphore>
#include <QThread>
#include <QVector>

#include <windows.h>

#include <atomic>

namespace MouseButtonsSystem {

namespace {

// Tecla virtual sin asignar (la misma de HotkeyServiceWin e InputInjectorWin): apretarla antes de soltar Win
// o Alt evita el menu Inicio y la barra de menu de la ventana.
constexpr WORD kMaskKey = 0xE8;
constexpr UINT kWatchdogMs = 200;

std::atomic<int> g_liveHooks{0};

WORD virtualKey(int key, bool *extended)
{
    *extended = false;
    if ((key >= Qt::Key_A && key <= Qt::Key_Z) || (key >= Qt::Key_0 && key <= Qt::Key_9)) return WORD(key);
    if (key >= Qt::Key_F1 && key <= Qt::Key_F12) return WORD(VK_F1 + (key - Qt::Key_F1));
    switch (key) {
    case Qt::Key_Space: return VK_SPACE;
    case Qt::Key_Control: return VK_CONTROL;
    case Qt::Key_Alt: return VK_MENU;
    case Qt::Key_Shift: return VK_SHIFT;
    case Qt::Key_Meta: *extended = true; return VK_LWIN;
    case Qt::Key_Left: *extended = true; return VK_LEFT;
    case Qt::Key_Right: *extended = true; return VK_RIGHT;
    case Qt::Key_Up: *extended = true; return VK_UP;
    case Qt::Key_Down: *extended = true; return VK_DOWN;
    default: return 0;
    }
}

INPUT keyInput(WORD vk, bool extended, bool up)
{
    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.wScan = WORD(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    input.ki.dwFlags = (up ? KEYEVENTF_KEYUP : 0) | (extended ? KEYEVENTF_EXTENDEDKEY : 0);
    input.ki.dwExtraInfo = kMightyToolsInputTag;
    return input;
}

QVector<INPUT> toInputs(const QList<Step> &steps)
{
    // Con los botones intercambiados (zurdos), el boton izquierdo de la app es el derecho fisico.
    const bool swapped = GetSystemMetrics(SM_SWAPBUTTON) != 0;
    QVector<INPUT> inputs;
    for (const Step &step : steps) {
        if (step.kind == Step::LeftDown || step.kind == Step::LeftUp) {
            INPUT input = {};
            input.type = INPUT_MOUSE;
            const bool down = step.kind == Step::LeftDown;
            input.mi.dwFlags = swapped ? (down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP)
                                       : (down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP);
            input.mi.dwExtraInfo = kMightyToolsInputTag;
            inputs.append(input);
            continue;
        }
        bool extended = false;
        const WORD vk = virtualKey(step.key, &extended);
        if (!vk) continue;
        const bool up = step.kind == Step::KeyUp;
        if (up && (vk == VK_LWIN || vk == VK_MENU)) {
            inputs.append(keyInput(kMaskKey, false, false));
            inputs.append(keyInput(kMaskKey, false, true));
        }
        inputs.append(keyInput(vk, extended, up));
    }
    return inputs;
}

bool sendInputs(const QList<Step> &steps)
{
    QVector<INPUT> inputs = toInputs(steps);
    if (inputs.isEmpty()) return true;
    return SendInput(UINT(inputs.size()), inputs.data(), sizeof(INPUT)) == UINT(inputs.size());
}

bool mouseButton(WPARAM message, DWORD mouseData, Button *button, bool *down)
{
    switch (message) {
    case WM_MBUTTONDOWN: *button = Button::Middle; *down = true; return true;
    case WM_MBUTTONUP: *button = Button::Middle; *down = false; return true;
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
        *down = message == WM_XBUTTONDOWN;
        if (HIWORD(mouseData) == XBUTTON1) *button = Button::Back;
        else if (HIWORD(mouseData) == XBUTTON2) *button = Button::Forward;
        else return false;
        return true;
    default:
        return false;
    }
}

DWORD processOf(HWND window)
{
    DWORD pid = 0;
    if (window) GetWindowThreadProcessId(window, &pid);
    return pid;
}

bool elevated(HANDLE process, bool *known)
{
    *known = false;
    HANDLE token = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elevation = {};
    DWORD size = 0;
    *known = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) != 0;
    CloseHandle(token);
    return *known && elevation.TokenIsElevated != 0;
}

// El hilo del gancho. Un bucle de mensajes propio (GetMessage), no el de Qt: el gancho, el reloj de
// seguridad y la orden de salir son mensajes de Windows de este hilo.
class HookThread : public QThread
{
public:
    HookThread(const QList<Rule> &rules, quint32 pid) : m_machine(rules), m_pid(pid) {}

    bool begin()
    {
        start();
        m_ready.acquire();
        if (!m_installed) wait();
        return m_installed;
    }

    void stop()
    {
        PostThreadMessageW(m_threadId, WM_QUIT, 0, 0);
        wait();
    }

    DWORD error() const { return m_error; }
    bool holding() const { return m_holding; }

protected:
    void run() override
    {
        s_current = this;
        MSG msg;
        PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE); // crea la cola antes de recibir ordenes
        m_threadId = GetCurrentThreadId();
        HHOOK hook = SetWindowsHookExW(WH_MOUSE_LL, &HookThread::onMouse, GetModuleHandleW(nullptr), 0);
        m_installed = hook != nullptr;
        m_error = m_installed ? 0 : GetLastError();
        if (hook) ++g_liveHooks;
        m_ready.release();
        if (hook) {
            while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
                if (msg.message == WM_TIMER && !msg.hwnd && msg.wParam == m_watchdog) {
                    watchdog();
                    continue;
                }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            UnhookWindowsHookEx(hook);
            // Nunca queda nada apretado: lo que se inyecto se suelta al irse el gancho.
            release();
            --g_liveHooks;
        }
        s_current = nullptr;
    }

private:
    // Rapido a proposito (Windows saca el gancho si tarda: LowLevelHooksTimeout): sin mensajes a otros
    // procesos (WindowFromPoint solo consulta ventanas del propio hilo) y la inyeccion aca mismo. SendInput
    // solo encola y vuelve; postearla para despues dejaria que la proxima soltada fisica, que llega como
    // mensaje enviado y se atiende antes que los posteados, se procese antes que las teclas apretadas.
    static LRESULT CALLBACK onMouse(int code, WPARAM message, LPARAM data)
    {
        HookThread *self = s_current;
        if (!self || code != HC_ACTION) return CallNextHookEx(nullptr, code, message, data);
        const auto *info = reinterpret_cast<const MSLLHOOKSTRUCT *>(data);
        Event event;
        if (!mouseButton(message, info->mouseData, &event.button, &event.down)) {
            return CallNextHookEx(nullptr, code, message, data);
        }
        event.ownInjected = (info->flags & LLMHF_INJECTED) && info->dwExtraInfo == kMightyToolsInputTag;
        if (event.down && !event.ownInjected) event.overTarget = self->overTarget(info->pt);
        const Result result = self->m_machine.handle(event);
        if (!result.steps.isEmpty()) {
            if (!sendInputs(result.steps) && event.down) {
                // Windows no las tomo (UIPI, escritorio seguro): se suelta lo que haya entrado y el clic sigue.
                sendInputs(self->m_machine.releaseAll());
                self->syncWatchdog();
                return CallNextHookEx(nullptr, code, message, data);
            }
            self->syncWatchdog();
        }
        return result.swallow ? 1 : CallNextHookEx(nullptr, code, message, data);
    }

    bool overTarget(POINT point) const
    {
        const HWND under = WindowFromPoint(point);
        return processOf(GetForegroundWindow()) == m_pid && under && processOf(GetAncestor(under, GA_ROOT)) == m_pid;
    }

    // Mientras hay algo apretado, un reloj del hilo mira la ventana del frente: si dejo de ser la app (otra
    // app, la pantalla de bloqueo o un aviso de UAC, donde el gancho no ve la soltada), se suelta todo.
    void syncWatchdog()
    {
        m_holding = m_machine.holding();
        if (m_holding && !m_watchdog) {
            m_watchdog = SetTimer(nullptr, 0, kWatchdogMs, nullptr);
        } else if (!m_holding && m_watchdog) {
            KillTimer(nullptr, m_watchdog);
            m_watchdog = 0;
        }
    }

    void watchdog()
    {
        if (processOf(GetForegroundWindow()) != m_pid) release();
    }

    // Sin logs desde este hilo: el manejador de logs de la app escribe el archivo sin cerrojo.
    void release()
    {
        sendInputs(m_machine.releaseAll());
        syncWatchdog();
    }

    Machine m_machine;
    const quint32 m_pid;
    DWORD m_threadId = 0;
    UINT_PTR m_watchdog = 0;
    std::atomic<bool> m_holding{false};
    QSemaphore m_ready;
    bool m_installed = false;
    DWORD m_error = 0;
    static thread_local HookThread *s_current;
};

thread_local HookThread *HookThread::s_current = nullptr;

} // namespace

bool injectionBlocked(quint32 pid)
{
    bool known = false;
    const bool self = elevated(GetCurrentProcess(), &known);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return !self;
    const bool target = elevated(process, &known);
    CloseHandle(process);
    // Sin poder leer su token (otro usuario o administrador), se la da por bloqueada salvo que esta app sea admin.
    return known ? (target && !self) : !self;
}

QStringList describeInputs(const QList<Step> &steps)
{
    QStringList lines;
    for (const INPUT &input : toInputs(steps)) {
        if (input.type == INPUT_MOUSE) {
            const DWORD flags = input.mi.dwFlags;
            lines << QStringLiteral("mouse %1 %2 marca=0x%3")
                         .arg(flags & (MOUSEEVENTF_RIGHTDOWN | MOUSEEVENTF_RIGHTUP) ? QStringLiteral("derecho (botones intercambiados)")
                                                                                     : QStringLiteral("izquierdo"),
                              flags & (MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_RIGHTDOWN) ? QStringLiteral("abajo") : QStringLiteral("arriba"))
                         .arg(quint64(input.mi.dwExtraInfo), 0, 16);
            continue;
        }
        lines << QStringLiteral("tecla vk=0x%1 escaneo=0x%2 %3%4 marca=0x%5")
                     .arg(input.ki.wVk, 2, 16, QLatin1Char('0'))
                     .arg(input.ki.wScan, 2, 16, QLatin1Char('0'))
                     .arg(input.ki.dwFlags & KEYEVENTF_KEYUP ? QStringLiteral("arriba") : QStringLiteral("abajo"),
                          input.ki.dwFlags & KEYEVENTF_EXTENDEDKEY ? QStringLiteral(" extendida") : QString())
                     .arg(quint64(input.ki.dwExtraInfo), 0, 16);
    }
    return lines;
}

int liveHooks()
{
    return g_liveHooks;
}

struct Hook::Private
{
    std::unique_ptr<HookThread> thread;
};

Hook::Hook()
    : d(std::make_unique<Private>())
{
}

Hook::~Hook()
{
    if (!d->thread) return;
    const bool held = d->thread->holding();
    d->thread->stop();
    if (held) qInfo() << "[mouseButtons] Se solto lo que estaba apretado al retirar el gancho";
}

bool Hook::holding() const
{
    return d->thread && d->thread->holding();
}

std::unique_ptr<Hook> Hook::install(const QList<Rule> &rules, quint32 pid, QString *error)
{
    // Segundo cinturon: el modulo ya no lo pide en una corrida automatizada.
    if (AutomatedRun::active()) {
        if (error) *error = QStringLiteral("corrida automatizada");
        return nullptr;
    }
    std::unique_ptr<Hook> hook(new Hook);
    hook->d->thread = std::make_unique<HookThread>(rules, pid);
    if (!hook->d->thread->begin()) {
        if (error) *error = QString::number(hook->d->thread->error());
        hook->d->thread.reset();
        return nullptr;
    }
    return hook;
}

} // namespace MouseButtonsSystem
