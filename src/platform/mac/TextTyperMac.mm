#include "platform/TextTyper.h"

#include "core/AutomatedRun.h"
#include "platform/SystemInput.h"

#include <QDebug>
#include <QTimer>

#import <AppKit/AppKit.h>
#include <ApplicationServices/ApplicationServices.h>
#include <Carbon/Carbon.h>

#include <cerrno>
#include <csignal>

// Escritura del dictado en macOS: eventos de teclado con el texto adentro (CGEventKeyboardSetUnicodeString),
// de a pedazos cortos, a la app que está al frente. NO pasa por el portapapeles: en mac no hace falta
// (regla de Lega: el dictado nunca deja rastro en el portapapeles). El salto de línea va como la tecla
// Return y el tabulador como Tab; lo demás, tal cual, sin depender de la distribución del teclado.
//
// Antes de escribir: la app de destino (su PID, tomado al empezar a grabar) tiene que seguir al frente, y se
// espera hasta 1 s a que se suelten los modificadores del atajo. Los eventos salen de una fuente privada y con
// los modificadores en cero, así que aunque el usuario siga apretando ⌘ o ⌥ no se convierten en atajos. Entre
// pedazo y pedazo se vuelve a mirar el frente: si cambió, se corta ahí (FocusChanged con lo escrito).
//
// Mandar teclas a otra app pide el permiso de Accesibilidad. Sin él macOS descarta los eventos sin avisar: se
// mira antes, se le pide a macOS que muestre su cartel y el texto queda en Recent (NoPermission).

namespace {

constexpr int kTickMs = 8;        // entre pedazo y pedazo
constexpr int kWaitModifiersMs = 1000;
constexpr int kChunk = 16;        // unidades UTF-16 por evento (CGEventKeyboardSetUnicodeString acepta ~20)
constexpr int64_t kMightyToolsInputTag = 0x4c47414d; // igual que en Windows (InputOrigin.h)

QString prepared(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('\r') || c == QLatin1Char('\n')) {
            if (c == QLatin1Char('\r') && i + 1 < text.size() && text.at(i + 1) == QLatin1Char('\n')) ++i;
            out += QLatin1Char('\n');
        } else if (c.unicode() >= 0x20 || c == QLatin1Char('\t')) {
            out += c;
        }
    }
    return out;
}

// El próximo evento a partir de `pos`: una tecla (Return, Tab) o un pedazo de texto sin cortar un par
// sustituto (un emoji queda entero).
struct Piece
{
    int length = 0;
    int key = -1; // kVK_Return / kVK_Tab, o -1 para texto
};

Piece nextPiece(const QString &text, int pos)
{
    const QChar first = text.at(pos);
    if (first == QLatin1Char('\n')) return {1, kVK_Return};
    if (first == QLatin1Char('\t')) return {1, kVK_Tab};
    int length = 0;
    while (pos + length < text.size() && length < kChunk) {
        const QChar c = text.at(pos + length);
        if (c == QLatin1Char('\n') || c == QLatin1Char('\t')) break;
        if (c.isHighSurrogate()) {
            if (length + 2 > kChunk) break;
            length += (pos + length + 1 < text.size()) ? 2 : 1;
        } else {
            ++length;
        }
    }
    return {qMax(1, length), -1};
}

bool modifiersDown()
{
    const CGEventFlags flags = CGEventSourceFlagsState(kCGEventSourceStateHIDSystemState);
    return flags & (kCGEventFlagMaskCommand | kCGEventFlagMaskControl | kCGEventFlagMaskAlternate | kCGEventFlagMaskShift
                    | kCGEventFlagMaskSecondaryFn);
}

pid_t frontmostPid()
{
    NSRunningApplication *front = [[NSWorkspace sharedWorkspace] frontmostApplication];
    return front ? front.processIdentifier : 0;
}

// El número de la ventana de más arriba (capa normal) de una app, o 0 si no tiene ninguna a la vista. No pide
// el permiso de Grabación de pantalla: el dueño y el número no son contenido.
quint32 frontWindowOf(pid_t pid)
{
    CFArrayRef windows = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
                                                    kCGNullWindowID);
    if (!windows) return 0;
    quint32 number = 0;
    for (CFIndex i = 0; i < CFArrayGetCount(windows) && number == 0; ++i) {
        NSDictionary *info = (__bridge NSDictionary *)CFArrayGetValueAtIndex(windows, i);
        if ([info[(__bridge NSString *)kCGWindowOwnerPID] intValue] == pid
            && [info[(__bridge NSString *)kCGWindowLayer] intValue] == 0) {
            number = quint32([info[(__bridge NSString *)kCGWindowNumber] unsignedIntValue]);
        }
    }
    CFRelease(windows);
    return number;
}

// El destino va en un quintptr: el PID abajo y el número de la ventana del frente arriba.
pid_t targetPid(quintptr target) { return pid_t(target & 0xffffffffu); }
quint32 targetWindow(quintptr target) { return quint32(quint64(target) >> 32); }

// La app de destino sigue al frente y, si al empezar tenía una ventana, es la misma (otra ventana de la misma
// app, como en Windows con otro HWND, cuenta como que cambió).
bool targetInFront(quintptr target)
{
    const pid_t pid = targetPid(target);
    if (frontmostPid() != pid) return false;
    const quint32 window = targetWindow(target);
    return window == 0 || frontWindowOf(pid) == window;
}

bool processAlive(pid_t pid)
{
    return pid > 0 && (::kill(pid, 0) == 0 || errno == EPERM);
}

} // namespace

struct TextTyper::Private
{
    enum class Phase { Idle, Waiting, Typing };

    QTimer timer;
    Phase phase = Phase::Idle;
    quintptr target = 0; // PID y ventana del frente (targetPid / targetWindow)
    QString text;
    int pos = 0;
    int waitedMs = 0;
    CGEventSourceRef source = nullptr;

    ~Private()
    {
        if (source) CFRelease(source);
    }

    bool post(const Piece &piece, const QString &chunk)
    {
        if (!source) {
            source = CGEventSourceCreate(kCGEventSourceStatePrivate);
        }
        const CGKeyCode code = piece.key >= 0 ? CGKeyCode(piece.key) : CGKeyCode(0);
        for (const bool down : {true, false}) {
            CGEventRef event = CGEventCreateKeyboardEvent(source, code, down);
            if (!event) return false;
            CGEventSetFlags(event, CGEventFlags(0));
            CGEventSetIntegerValueField(event, kCGEventSourceUserData, kMightyToolsInputTag);
            if (piece.key < 0) {
                CGEventKeyboardSetUnicodeString(event, UniCharCount(chunk.size()),
                                                reinterpret_cast<const UniChar *>(chunk.utf16()));
            }
            CGEventPost(kCGHIDEventTap, event);
            CFRelease(event);
        }
        return true;
    }
};

TextTyper::TextTyper(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->timer.setInterval(kTickMs);
    connect(&d->timer, &QTimer::timeout, this, [this]() {
        const auto done = [this](Result result) {
            d->timer.stop();
            const int typed = d->pos;
            d->phase = Private::Phase::Idle;
            d->text.clear();
            d->pos = 0;
            qInfo() << "[dictation] Escritura:" << result << typed << "caracteres";
            emit finished(result, typed);
        };
        if (d->phase == Private::Phase::Waiting) {
            if (!SystemInput::accessibilityTrusted(false)) {
                // Que macOS muestre su cartel (una sola vez por arranque lo muestra el sistema).
                SystemInput::accessibilityTrusted(true);
                done(Result::NoPermission);
                return;
            }
            if (!processAlive(targetPid(d->target))) {
                done(Result::NoTarget);
                return;
            }
            if (!targetInFront(d->target)) {
                done(Result::FocusChanged);
                return;
            }
            if (modifiersDown() && d->waitedMs < kWaitModifiersMs) {
                d->waitedMs += kTickMs;
                return;
            }
            d->phase = Private::Phase::Typing;
        }
        if (d->phase != Private::Phase::Typing) {
            return;
        }
        if (d->pos >= d->text.size()) {
            done(Result::Typed);
            return;
        }
        if (!targetInFront(d->target)) {
            done(Result::FocusChanged);
            return;
        }
        const Piece piece = nextPiece(d->text, d->pos);
        if (!d->post(piece, d->text.mid(d->pos, piece.length))) {
            done(Result::Failed);
            return;
        }
        d->pos += piece.length;
    });
}

TextTyper::~TextTyper() = default;

quintptr TextTyper::foregroundWindow()
{
    // En mac el destino es la app del frente (su PID, abajo) y su ventana de más arriba (su número, arriba): las
    // teclas van a la ventana activa de esa app, y si al escribir es otra ventana no se escribe.
    const pid_t pid = frontmostPid();
    if (pid <= 0) return 0;
    return quintptr((quint64(frontWindowOf(pid)) << 32) | quint64(quint32(pid)));
}

bool TextTyper::type(quintptr target, const QString &text)
{
    if (isBusy()) {
        return false;
    }
    d->target = target;
    d->text = prepared(text);
    d->pos = 0;
    d->waitedMs = 0;
    d->phase = Private::Phase::Waiting;
    if (AutomatedRun::active() || d->text.isEmpty()) {
        const Result result = d->text.isEmpty() ? Result::Typed : Result::LoggedOnly;
        qInfo() << "[dictation] (automatizada o vacio) no se escribe nada:" << d->text.size() << "caracteres";
        d->phase = Private::Phase::Idle;
        d->text.clear();
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
    // En mac se escribe de a pedazos: Esc corta también a la mitad (lo ya escrito queda).
    d->timer.stop();
    const int typed = d->pos;
    qInfo() << "[dictation] Escritura detenida con Esc despues de" << typed << "caracteres";
    d->phase = Private::Phase::Idle;
    d->text.clear();
    d->pos = 0;
    QTimer::singleShot(0, this, [this, typed]() { emit finished(Result::Stopped, typed); });
}

QStringList TextTyper::describe(const QString &text)
{
    const QString keys = prepared(text);
    if (keys.isEmpty()) return {};
    QString shown = keys;
    shown.replace(QLatin1String("\n"), QLatin1String("\\n"));
    QStringList lines{QStringLiteral("texto %1").arg(shown), QStringLiteral("modo teclas CGEvent, sin portapapeles")};
    for (int pos = 0; pos < keys.size();) {
        const Piece piece = nextPiece(keys, pos);
        if (piece.key == kVK_Return) {
            lines << QStringLiteral("tecla Return");
        } else if (piece.key == kVK_Tab) {
            lines << QStringLiteral("tecla Tab");
        } else {
            lines << QStringLiteral("unicode %1").arg(keys.mid(pos, piece.length));
        }
        pos += piece.length;
    }
    return lines;
}
