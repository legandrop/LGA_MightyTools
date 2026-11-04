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
        // Por encima del resto de la app: Windows retira en silencio el gancho que tarda (LowLevelHooksTimeout).
        start(QThread::TimeCriticalPriority);
        m_ready.acquire();
        if (!m_installed) wait();
        return m_installed;
    }

    // Retira el gancho y espera solo eso. False: el hilo sigue vivo reintentando soltar lo que el escritorio
    // seguro (UAC, pantalla de bloqueo) no dejo entrar; termina solo cuando entra.
    bool stop()
    {
        PostThreadMessageW(m_threadId, WM_QUIT, 0, 0);
        m_unhooked.acquire();
        return wait(100);
    }

    DWORD error() const { return m_error; }

    // Para el log, desde el hilo de la ventana: el hilo del gancho no escribe logs. Vacio si no paso nada.
    QString summary() const
    {
        const int taken = m_taken, outside = m_outside, refused = m_refused, restored = m_restored;
        if (!taken && !outside && !refused && !restored) return {};
        return QStringLiteral("Mientras estuvo el gancho: %1 apretada(s) tomada(s), %2 fuera de la app, "
                              "%3 que Windows no dejo inyectar, gancho repuesto %4 vez/veces")
            .arg(taken)
            .arg(outside)
            .arg(refused)
            .arg(restored);
    }

protected:
    void run() override
    {
        s_current = this;
        MSG msg;
        PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE); // crea la cola antes de recibir ordenes
        m_threadId = GetCurrentThreadId();
        m_installed = hook();
        m_error = m_installed ? 0 : GetLastError();
        m_ready.release();
        if (!m_installed) {
            s_current = nullptr;
            return;
        }
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (msg.message == WM_TIMER && !msg.hwnd && m_laterTimer && msg.wParam == m_laterTimer) {
                flushLater();
                continue;
            }
            if (msg.message == WM_TIMER && !msg.hwnd && msg.wParam == m_watchdog) {
                watchdog();
                continue;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        unhook();
        release();
        m_unhooked.release();
        // Nunca queda nada apretado: si el escritorio seguro no deja soltar, se reintenta hasta que entra.
        while (!m_pending.isEmpty()) {
            Sleep(kWatchdogMs);
            retryPending();
        }
        s_current = nullptr;
    }

private:
    bool hook()
    {
        m_hook = SetWindowsHookExW(WH_MOUSE_LL, &HookThread::onMouse, GetModuleHandleW(nullptr), 0);
        if (m_hook) ++g_liveHooks;
        return m_hook != nullptr;
    }

    void unhook()
    {
        if (!m_hook) return;
        UnhookWindowsHookEx(m_hook); // si Windows ya lo retiro, falla sin efecto
        m_hook = nullptr;
        --g_liveHooks;
    }

    // La inyeccion va aca mismo y no posteada para despues: si Windows no la toma (UIPI), el clic tiene que
    // seguir intacto a la app, y eso se decide antes de devolver. SendInput no solo encola: pasa sincronico
    // por los ganchos de bajo nivel de todos los procesos (el clic inyectado vuelve a entrar a este mismo
    // gancho), asi que puede tardar. Por eso el hilo corre con prioridad maxima, el callback no hace nada
    // mas (sin logs ni mensajes a otros procesos: WindowFromPoint solo manda WM_NCHITTEST a ventanas de
    // este hilo, que no tiene) y el reloj de seguridad se da cuenta si Windows igual lo retiro.
    static LRESULT CALLBACK onMouse(int code, WPARAM message, LPARAM data)
    {
        HookThread *self = s_current;
        if (!self || code != HC_ACTION) return CallNextHookEx(nullptr, code, message, data);
        ++self->m_calls; // tambien los movimientos: el reloj los usa para saber si el gancho sigue
        const auto *info = reinterpret_cast<const MSLLHOOKSTRUCT *>(data);
        Event event;
        if (!mouseButton(message, info->mouseData, &event.button, &event.down)) {
            return CallNextHookEx(nullptr, code, message, data);
        }
        if (!self->m_pending.isEmpty()) self->retryPending(); // lo que no se pudo soltar va antes que lo nuevo
        event.ownInjected = (info->flags & LLMHF_INJECTED) && info->dwExtraInfo == kMightyToolsInputTag;
        if (event.down && !event.ownInjected) event.overTarget = self->overTarget(info->pt);
        const Result result = self->m_machine.handle(event);
        if (event.down && !event.ownInjected && !result.swallow && self->m_machine.hasRule(event.button)) {
            ++self->m_outside; // sobre otra ventana o con otra app al frente: pasa intacto
        }
        if (result.steps.isEmpty() && result.later.isEmpty()) {
            return result.swallow ? 1 : CallNextHookEx(nullptr, code, message, data);
        }
        // Lo demorado que no salio todavia va antes que lo nuevo (y un clic demorado que ya se suelta, no sale).
        const QList<Step> earlier = self->takeLater();
        const QList<Step> now = joinSteps(earlier, result.steps);
        const bool sent = sendInputs(now);
        self->setLater(result.later);
        if (!sent && event.down) {
            // Windows no las tomo (UIPI, escritorio seguro): se suelta lo que haya entrado y el clic sigue.
            // Las soltadas demoradas de otra regla tampoco se pierden.
            ++self->m_refused;
            self->m_pending += releasesIn(earlier);
            self->release();
            return CallNextHookEx(nullptr, code, message, data);
        }
        if (!sent) self->m_pending += releasesIn(now); // una soltada que no entro se reintenta: nada queda apretado
        if (event.down) ++self->m_taken;
        self->syncWatchdog();
        return result.swallow ? 1 : CallNextHookEx(nullptr, code, message, data);
    }

    bool overTarget(POINT point) const
    {
        const HWND under = WindowFromPoint(point);
        return processOf(GetForegroundWindow()) == m_pid && under && processOf(GetAncestor(under, GA_ROOT)) == m_pid;
    }

    // El reloj de seguridad corre mientras hay algo apretado o pendiente de soltar.
    void syncWatchdog()
    {
        const bool needed = m_machine.holding() || !m_pending.isEmpty();
        if (needed && !m_watchdog) {
            m_watchdog = SetTimer(nullptr, 0, kWatchdogMs, nullptr);
            m_watchSeen = false;
        } else if (!needed && m_watchdog) {
            KillTimer(nullptr, m_watchdog);
            m_watchdog = 0;
        }
    }

    void watchdog()
    {
        // Pendiente de soltar: se reintenta sin mirar el frente (en el escritorio seguro no se puede leer).
        if (!m_pending.isEmpty()) {
            retryPending();
            return;
        }
        if (hookLost()) {
            release();
            unhook();
            hook(); // de vuelta, para los clics que siguen
            ++m_restored;
            return;
        }
        // Otra app al frente, la pantalla de bloqueo o un aviso de UAC: el gancho no ve la soltada.
        if (processOf(GetForegroundWindow()) != m_pid) release();
    }

    // Windows retira en silencio un gancho que tardo y no avisa. GetAsyncKeyState del boton no sirve para
    // notarlo: el boton que el gancho se traga nunca cambia ese estado. Pero mientras se mantiene un boton se
    // mueve el mouse, y cada movimiento pasa por el gancho: si el puntero se movio entre dos ticks y el gancho
    // no vio nada, ya no esta.
    bool hookLost()
    {
        POINT now = {};
        if (!GetCursorPos(&now)) return false;
        const bool lost = m_watchSeen && m_watchCalls == m_calls && (now.x != m_watchPoint.x || now.y != m_watchPoint.y);
        m_watchPoint = now;
        m_watchCalls = m_calls;
        m_watchSeen = true;
        return lost;
    }

    // Suelta todo lo apretado. Sin logs desde este hilo: el manejador de logs de la app escribe sin cerrojo.
    // Un clic demorado que todavia no salio se cancela con su soltada: no le llega nada a la app.
    void release()
    {
        m_pending += releasesIn(joinSteps(takeLater(), m_machine.releaseAll()));
        retryPending();
    }

    // Lo demorado (kStepDelayMs) sale con su propio reloj, en este hilo y fuera del callback del gancho.
    QList<Step> takeLater()
    {
        if (m_laterTimer) {
            KillTimer(nullptr, m_laterTimer);
            m_laterTimer = 0;
        }
        const QList<Step> steps = m_later;
        m_later.clear();
        return steps;
    }

    void setLater(const QList<Step> &steps)
    {
        m_later = steps;
        if (!m_later.isEmpty()) m_laterTimer = SetTimer(nullptr, 0, kStepDelayMs, nullptr);
        if (!m_later.isEmpty() && !m_laterTimer) flushLater(); // sin reloj, sin pausa: mejor que no mandarlo
    }

    // Con soltadas pendientes (Windows no deja inyectar) lo demorado va detras de ellas. Un clic demorado que
    // Windows no tomo no se reintenta: entraria tarde, quiza con el boton ya suelto. Las soltadas si.
    void flushLater()
    {
        const QList<Step> steps = takeLater();
        if (!m_pending.isEmpty() || !sendInputs(steps)) m_pending += releasesIn(steps);
        retryPending();
    }

    static QList<Step> releasesIn(const QList<Step> &steps)
    {
        QList<Step> releases;
        for (const Step &step : steps) {
            if (step.kind == Step::KeyUp || step.kind == Step::LeftUp) releases.append(step);
        }
        return releases;
    }

    void retryPending()
    {
        if (!m_pending.isEmpty() && sendInputs(m_pending)) m_pending.clear();
        syncWatchdog();
    }

    Machine m_machine;
    const quint32 m_pid;
    HHOOK m_hook = nullptr;
    DWORD m_threadId = 0;
    UINT_PTR m_watchdog = 0;
    UINT_PTR m_laterTimer = 0;
    QList<Step> m_later;   ///< lo que sale kStepDelayMs despues (el clic al apretar, las teclas al soltar)
    QList<Step> m_pending; ///< soltadas que Windows no tomo todavia (nunca apretadas)
    std::atomic<int> m_taken{0};
    std::atomic<int> m_outside{0};
    std::atomic<int> m_refused{0};
    std::atomic<int> m_restored{0};
    quint64 m_calls = 0;
    quint64 m_watchCalls = 0;
    POINT m_watchPoint = {};
    bool m_watchSeen = false;
    QSemaphore m_ready;
    QSemaphore m_unhooked;
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
    const bool stopped = d->thread->stop();
    const QString summary = d->thread->summary();
    if (!summary.isEmpty()) qInfo().noquote() << "[mouseButtons]" << summary;
    if (stopped) return;
    // Sigue reintentando soltar (escritorio seguro): termina y se borra solo, sin que la ventana espere.
    qInfo() << "[mouseButtons] Gancho retirado; soltar queda pendiente hasta que Windows lo deje";
    HookThread *draining = d->thread.release();
    QObject::connect(draining, &QThread::finished, draining, &QObject::deleteLater);
    if (draining->isFinished()) draining->deleteLater();
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
