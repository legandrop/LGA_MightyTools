#include "platform/TextTyper.h"

#include "core/AutomatedRun.h"
#include "platform/InputInjector.h"
#include "platform/MouseButtonsSystem.h"
#include "platform/win/InputOrigin.h"

#include <QDebug>
#include <QList>
#include <QTimer>
#include <QVector>

#include <windows.h>

namespace {

constexpr int kBatchUnits = 12;      // caracteres por lote (un emoji o un salto de línea cuentan uno)
constexpr int kTickMs = 12;          // pausa entre lotes: ~1000 caracteres por segundo
constexpr int kModifierWaitMs = 1000;

struct Batch
{
    QVector<INPUT> inputs;
    int end = 0;          ///< posición en el texto después de este lote
    bool shifted = false; ///< lleva un Shift+Enter: si Windows lo corta a la mitad, se suelta Shift
};

INPUT unicodeInput(ushort unit, bool up)
{
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wScan = unit;
    input.ki.dwFlags = KEYEVENTF_UNICODE | (up ? KEYEVENTF_KEYUP : 0);
    input.ki.dwExtraInfo = kMightyToolsInputTag;
    return input;
}

INPUT keyInput(WORD vk, bool up)
{
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.wScan = WORD(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    input.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    input.ki.dwExtraInfo = kMightyToolsInputTag;
    return input;
}

QList<Batch> batchesOf(const QString &text)
{
    QList<Batch> batches;
    Batch batch;
    int units = 0;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('\r') || c == QLatin1Char('\n')) {
            if (c == QLatin1Char('\r') && i + 1 < text.size() && text.at(i + 1) == QLatin1Char('\n')) ++i;
            batch.inputs << keyInput(VK_SHIFT, false) << keyInput(VK_RETURN, false) << keyInput(VK_RETURN, true)
                         << keyInput(VK_SHIFT, true);
            batch.shifted = true;
        } else if (c.isHighSurrogate() && i + 1 < text.size() && text.at(i + 1).isLowSurrogate()) {
            for (const QChar part : {c, text.at(i + 1)}) {
                batch.inputs << unicodeInput(part.unicode(), false) << unicodeInput(part.unicode(), true);
            }
            ++i;
        } else if (c.unicode() >= 0x20 || c == QLatin1Char('\t')) {
            batch.inputs << unicodeInput(c.unicode(), false) << unicodeInput(c.unicode(), true);
        } else {
            continue; // otros caracteres de control: no se escriben
        }
        batch.end = i + 1;
        if (++units == kBatchUnits) {
            batches << batch;
            batch = Batch();
            units = 0;
        }
    }
    if (!batch.inputs.isEmpty()) {
        batches << batch;
    }
    return batches;
}

bool anyModifierDown()
{
    for (const int vk : {VK_LCONTROL, VK_RCONTROL, VK_LMENU, VK_RMENU, VK_LSHIFT, VK_RSHIFT, VK_LWIN, VK_RWIN}) {
        if (GetAsyncKeyState(vk) & 0x8000) return true;
    }
    return false;
}

bool send(QVector<INPUT> inputs)
{
    // Segundo cinturón: type() ya no llega acá en una corrida automatizada.
    if (AutomatedRun::active() || inputs.isEmpty()) return false;
    return SendInput(UINT(inputs.size()), inputs.data(), sizeof(INPUT)) == UINT(inputs.size());
}

} // namespace

struct TextTyper::Private
{
    QTimer timer;
    HWND target = nullptr;
    QList<Batch> batches;
    int next = 0;
    int waitedMs = 0;
    bool checked = false; ///< el destino ya se reviso (administrador, modificadores)
};

TextTyper::TextTyper(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->timer.setInterval(kTickMs);
    connect(&d->timer, &QTimer::timeout, this, [this]() {
        const auto finish = [this](Result result) {
            d->timer.stop();
            const int typed = d->next > 0 ? d->batches.at(d->next - 1).end : 0;
            qInfo() << "[dictation] Escritura:" << result << typed << "de" << (d->batches.isEmpty() ? 0 : d->batches.last().end);
            d->batches.clear();
            emit finished(result, typed);
        };
        if (!d->target || !IsWindow(d->target)) return finish(Result::NoTarget);
        if (GetForegroundWindow() != d->target) return finish(Result::FocusChanged);
        if (!d->checked) {
            DWORD pid = 0;
            GetWindowThreadProcessId(d->target, &pid);
            if (MouseButtonsSystem::injectionBlocked(pid)) return finish(Result::Elevated);
            if (anyModifierDown()) {
                if (d->waitedMs < kModifierWaitMs) {
                    d->waitedMs += kTickMs;
                    return;
                }
                // Siguen apretados (o Windows los da por apretados): se sueltan, con la tecla muda antes de Win o Alt.
                InputInjector(false).releaseModifiers();
            }
            d->checked = true;
        }
        const Batch &batch = d->batches.at(d->next);
        if (!send(batch.inputs)) {
            if (batch.shifted) send({keyInput(VK_SHIFT, true)}); // nunca queda Shift apretado
            return finish(Result::Failed);
        }
        if (++d->next == d->batches.size()) finish(Result::Typed);
    });
}

TextTyper::~TextTyper() = default;

quintptr TextTyper::foregroundWindow()
{
    return reinterpret_cast<quintptr>(GetForegroundWindow());
}

bool TextTyper::type(quintptr target, const QString &text)
{
    if (isBusy()) {
        return false;
    }
    d->target = reinterpret_cast<HWND>(target);
    d->batches = batchesOf(text);
    d->next = 0;
    d->waitedMs = 0;
    d->checked = false;
    if (AutomatedRun::active() || d->batches.isEmpty()) {
        const Result result = d->batches.isEmpty() ? Result::Typed : Result::LoggedOnly;
        qInfo() << "[dictation] (automatizada o vacio) no se escribe nada:" << d->batches.size() << "lotes";
        d->batches.clear();
        QTimer::singleShot(0, this, [this, result]() { emit finished(result, 0); });
        return true;
    }
    d->timer.start();
    return true;
}

bool TextTyper::isBusy() const
{
    return d->timer.isActive();
}

void TextTyper::stop()
{
    if (!d->timer.isActive()) {
        return;
    }
    d->timer.stop();
    const int typed = d->next > 0 ? d->batches.at(d->next - 1).end : 0;
    qInfo() << "[dictation] Escritura detenida con Esc:" << typed << "de" << d->batches.last().end;
    d->batches.clear();
    QTimer::singleShot(0, this, [this, typed]() { emit finished(Result::Stopped, typed); });
}

QStringList TextTyper::describe(const QString &text)
{
    QStringList lines;
    const QList<Batch> batches = batchesOf(text);
    for (const Batch &batch : batches) {
        if (!lines.isEmpty()) lines << QStringLiteral("--");
        for (const INPUT &input : batch.inputs) {
            const bool up = input.ki.dwFlags & KEYEVENTF_KEYUP;
            const QString kind = input.ki.dwFlags & KEYEVENTF_UNICODE
                                     ? QStringLiteral("unicode U+%1").arg(QString::number(input.ki.wScan, 16).toUpper().rightJustified(4, QLatin1Char('0')))
                                     : QStringLiteral("tecla vk=0x%1").arg(input.ki.wVk, 2, 16, QLatin1Char('0'));
            lines << QStringLiteral("%1 %2%3").arg(kind, up ? QStringLiteral("arriba") : QStringLiteral("abajo"),
                                                   input.ki.dwExtraInfo == kMightyToolsInputTag ? QString() : QStringLiteral(" SIN-MARCA"));
        }
    }
    return lines;
}
