#include "platform/TextTyper.h"

#include "core/AutomatedRun.h"
#include "platform/InputInjector.h"
#include "platform/MouseButtonsSystem.h"
#include "platform/win/InputOrigin.h"

#include <QDebug>
#include <QTimer>
#include <QVector>

#include <windows.h>

#include <vector>

namespace {

constexpr int kTickMs = 10;
constexpr int kModifierWaitMs = 1000;
constexpr int kOpenWaitMs = 500;     // otra app puede tener el portapapeles abierto un momento
constexpr int kRenderWaitMs = 3000;  // lo que puede tardar la ventana en pedir el texto después de Ctrl+V
constexpr int kAfterRenderMs = 400;  // margen para que termine de leerlo antes de devolver lo que había
constexpr int kMinAfterKeysMs = 1000; // nunca antes de esto desde el Ctrl+V, aunque alguien lo haya pedido antes
constexpr int kRestoreWaitMs = 5000; // reintentos para devolver lo de antes si otra app tiene el portapapeles

// Marcas de Windows para lo que la app pone en el portapapeles: no entra al historial de Win+V, no sube a la
// nube y los programas que vigilan el portapapeles no lo guardan.
const wchar_t *const kNoHistory = L"CanIncludeInClipboardHistory";
const wchar_t *const kNoCloud = L"CanUploadToCloudClipboard";
const wchar_t *const kNoMonitor = L"ExcludeClipboardContentFromMonitorProcessing";

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

QVector<INPUT> pasteKeys()
{
    return {keyInput(VK_CONTROL, false), keyInput('V', false), keyInput('V', true), keyInput(VK_CONTROL, true)};
}

// El texto como se pega: saltos de línea \r\n (pegados no mandan el mensaje de un chat, a diferencia de Enter)
// y sin otros caracteres de control salvo el tabulador.
QString prepared(const QString &text)
{
    QString out;
    out.reserve(text.size() + 8);
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('\r') || c == QLatin1Char('\n')) {
            if (c == QLatin1Char('\r') && i + 1 < text.size() && text.at(i + 1) == QLatin1Char('\n')) ++i;
            out += QLatin1String("\r\n");
        } else if (c.unicode() >= 0x20 || c == QLatin1Char('\t')) {
            out += c;
        }
    }
    return out;
}

bool anyModifierDown()
{
    for (const int vk : {VK_LCONTROL, VK_RCONTROL, VK_LMENU, VK_RMENU, VK_LSHIFT, VK_RSHIFT, VK_LWIN, VK_RWIN}) {
        if (GetAsyncKeyState(vk) & 0x8000) return true;
    }
    return false;
}

HGLOBAL globalCopy(const void *data, SIZE_T size)
{
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, size ? size : 1);
    if (!handle) return nullptr;
    if (void *to = GlobalLock(handle)) {
        if (size) memcpy(to, data, size);
        GlobalUnlock(handle);
        return handle;
    }
    GlobalFree(handle);
    return nullptr;
}

void setMarker(const wchar_t *name)
{
    const UINT format = RegisterClipboardFormatW(name);
    const DWORD zero = 0;
    if (HGLOBAL handle = format ? globalCopy(&zero, sizeof(zero)) : nullptr) {
        if (!SetClipboardData(format, handle)) GlobalFree(handle);
    }
}

// Formatos que no son memoria global copiable: manijas de GDI, datos privados de la app que los puso o referencias
// de OLE a esa app (copiados como bytes quedarían huérfanos). Los dibujos se conservan igual por CF_DIB /
// CF_DIBV5, que Windows ofrece junto con CF_BITMAP, y el contenido por sus formatos reales.
bool skippedFormat(UINT format)
{
    if (format == CF_BITMAP || format == CF_PALETTE || format == CF_METAFILEPICT || format == CF_DSPBITMAP
        || format == CF_DSPMETAFILEPICT || format == CF_OWNERDISPLAY
        || (format >= CF_PRIVATEFIRST && format <= CF_PRIVATELAST)
        || (format >= CF_GDIOBJFIRST && format <= CF_GDIOBJLAST)) {
        return true;
    }
    wchar_t name[64] = {};
    if (format < 0xC000 || GetClipboardFormatNameW(format, name, 64) <= 0) return false;
    const QString registered = QString::fromWCharArray(name);
    return registered == QLatin1String("DataObject") || registered == QLatin1String("Ole Private Data")
        || registered == QLatin1String("OlePrivateData");
}

// Lo que había en el portapapeles antes de pegar, para devolverlo después.
struct Saved
{
    UINT format = 0;
    QByteArray bytes;              ///< memoria global
    HENHMETAFILE metafile = nullptr; ///< CF_ENHMETAFILE / CF_DSPENHMETAFILE
};

LRESULT CALLBACK ownerProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

} // namespace

struct TextTyper::Private
{
    enum class Phase { Idle, Waiting, Pasted, Restoring };

    QTimer timer;
    HWND owner = nullptr; ///< ventana invisible (solo mensajes) dueña del portapapeles mientras se pega
    HWND target = nullptr;
    // Lo que se entrega cuando alguien lo pide (render diferido). Queda hasta el próximo type(): si no se pudo
    // devolver lo de antes, el portapapeles sigue ofreciendo el texto y no un formato vacío.
    QString text;
    Phase phase = Phase::Idle;
    TextTyper::Result result = TextTyper::Result::Failed; ///< el que se informa al terminar de devolver
    int waitedMs = 0;
    int sinceRenderMs = 0;
    bool rendered = false; ///< alguien pidió el texto después del Ctrl+V
    bool checked = false;  ///< el destino ya se revisó (administrador)
    bool released = false; ///< ya se soltaron a la fuerza los modificadores
    bool hadSnapshot = false;
    std::vector<Saved> saved;

    ~Private() { freeSaved(); }

    void freeSaved()
    {
        for (Saved &item : saved) {
            if (item.metafile) DeleteEnhMetaFile(item.metafile);
        }
        saved.clear();
    }

    bool open()
    {
        return OpenClipboard(owner) != FALSE;
    }

    // Guarda lo que hay (dentro de un OpenClipboard).
    void snapshot()
    {
        freeSaved();
        for (UINT format = EnumClipboardFormats(0); format; format = EnumClipboardFormats(format)) {
            if (skippedFormat(format)) continue;
            HANDLE handle = GetClipboardData(format);
            if (!handle) continue;
            Saved item;
            item.format = format;
            if (format == CF_ENHMETAFILE || format == CF_DSPENHMETAFILE) {
                item.metafile = CopyEnhMetaFile(static_cast<HENHMETAFILE>(handle), nullptr);
                if (!item.metafile) continue;
            } else {
                const SIZE_T size = GlobalSize(handle);
                const void *data = GlobalLock(handle);
                if (!data) continue;
                item.bytes = QByteArray(static_cast<const char *>(data), qsizetype(size));
                GlobalUnlock(handle);
            }
            saved.push_back(std::move(item));
        }
        hadSnapshot = true;
    }

    // Entrega el texto (en WM_RENDERFORMAT el portapapeles ya está abierto por quien lo pide).
    void render()
    {
        const std::wstring wide = text.toStdWString();
        if (HGLOBAL handle = globalCopy(wide.c_str(), (wide.size() + 1) * sizeof(wchar_t))) {
            if (!SetClipboardData(CF_UNICODETEXT, handle)) GlobalFree(handle);
        }
        // Puede ser un programa que vigila el portapapeles y no respeta las marcas, antes que la ventana: por
        // eso además se espera un mínimo desde el Ctrl+V antes de devolver lo de antes.
        if (phase == Phase::Pasted && !rendered) {
            rendered = true;
            sinceRenderMs = 0;
        }
    }

    void dropSnapshot(const char *why)
    {
        qInfo() << "[dictation] Pegado: no se devuelve lo anterior:" << why;
        freeSaved();
        hadSnapshot = false;
    }

    // Devuelve lo que había, solo si el portapapeles sigue siendo nuestro: si otra app (o el usuario) copió algo
    // mientras tanto, eso manda. Va también sin historial: lo de antes ya está en Win+V y no se repite. Sin
    // esperas: true si terminó (devuelto, o no correspondía); false si el portapapeles está abierto por otra app y
    // hay que reintentar (el respaldo se conserva).
    bool tryRestore()
    {
        if (!hadSnapshot) return true;
        if (GetClipboardOwner() != owner) {
            dropSnapshot("el portapapeles cambio mientras tanto");
            return true;
        }
        if (!open()) return false;
        // Otra vez, ya abierto: entre la mirada de arriba y la apertura otra app pudo copiar algo.
        if (GetClipboardOwner() != owner) {
            CloseClipboard();
            dropSnapshot("el portapapeles cambio mientras tanto");
            return true;
        }
        EmptyClipboard();
        bool noHistory = false, noCloud = false, noMonitor = false;
        const UINT historyFormat = RegisterClipboardFormatW(kNoHistory);
        const UINT cloudFormat = RegisterClipboardFormatW(kNoCloud);
        const UINT monitorFormat = RegisterClipboardFormatW(kNoMonitor);
        int restored = 0;
        for (Saved &item : saved) {
            noHistory |= item.format == historyFormat;
            noCloud |= item.format == cloudFormat;
            noMonitor |= item.format == monitorFormat;
            HANDLE handle = item.metafile ? static_cast<HANDLE>(item.metafile)
                                          : static_cast<HANDLE>(globalCopy(item.bytes.constData(), SIZE_T(item.bytes.size())));
            if (!handle) continue;
            if (SetClipboardData(item.format, handle)) {
                item.metafile = nullptr; // ahora es del portapapeles
                ++restored;
            } else if (!item.metafile) {
                GlobalFree(handle);
            }
        }
        if (!saved.empty()) {
            if (!noHistory) setMarker(kNoHistory);
            if (!noCloud) setMarker(kNoCloud);
            if (!noMonitor) setMarker(kNoMonitor);
        }
        CloseClipboard();
        qInfo() << "[dictation] Pegado: devueltos" << restored << "de" << saved.size() << "formatos";
        freeSaved();
        hadSnapshot = false;
        return true;
    }
};

namespace {

LRESULT CALLBACK ownerProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto *d = reinterpret_cast<TextTyper::Private *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (message) {
    case WM_RENDERFORMAT:
        if (d && wParam == CF_UNICODETEXT) d->render();
        return 0;
    case WM_RENDERALLFORMATS:
        // La ventana se cierra siendo dueña: se deja el texto puesto para que no quede un formato vacío.
        if (d && OpenClipboard(hwnd)) {
            if (GetClipboardOwner() == hwnd) d->render();
            CloseClipboard();
        }
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

HWND createOwner(TextTyper::Private *d)
{
    static const wchar_t *const kClass = L"LGA_MightyTools_DictationPaste";
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = ownerProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kClass;
        registered = RegisterClassW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }
    HWND hwnd = CreateWindowExW(0, kClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (hwnd) SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(d));
    return hwnd;
}

} // namespace

TextTyper::TextTyper(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->timer.setInterval(kTickMs);
    connect(&d->timer, &QTimer::timeout, this, [this]() {
        // Devuelve lo que había en el portapapeles (reintentando por el temporizador si está ocupado) y recién
        // después informa el resultado.
        const auto restoreStep = [this]() {
            if (!d->tryRestore()) {
                d->waitedMs += kTickMs;
                if (d->waitedMs < kRestoreWaitMs) return;
                qWarning() << "[dictation] Pegado: el portapapeles siguio ocupado; no se pudo devolver lo anterior";
                d->freeSaved();
                d->hadSnapshot = false;
            }
            d->timer.stop();
            const Result result = d->result;
            const int typed = result == Result::Typed ? d->text.size() : 0;
            qInfo() << "[dictation] Pegado:" << result << typed << "de" << d->text.size();
            d->phase = Private::Phase::Idle;
            emit finished(result, typed);
        };
        const auto finish = [this, restoreStep](Result result) {
            d->result = result;
            d->phase = Private::Phase::Restoring;
            d->waitedMs = 0;
            restoreStep();
        };

        if (d->phase == Private::Phase::Restoring) {
            restoreStep();
            return;
        }
        if (d->phase == Private::Phase::Pasted) {
            d->waitedMs += kTickMs;
            if (d->rendered) {
                d->sinceRenderMs += kTickMs;
                if (d->waitedMs >= kMinAfterKeysMs && d->sinceRenderMs >= kAfterRenderMs) finish(Result::Typed);
            } else if (d->waitedMs >= kRenderWaitMs) {
                // Nadie pidió el texto: la ventana no tomó el Ctrl+V (o tarda demasiado). Se devuelve lo de antes.
                finish(Result::Failed);
            }
            return;
        }

        // Phase::Waiting: todavía no se pegó nada.
        if (!d->target || !IsWindow(d->target)) return finish(Result::NoTarget);
        if (GetForegroundWindow() != d->target) return finish(Result::FocusChanged);
        if (!d->checked) {
            DWORD pid = 0;
            GetWindowThreadProcessId(d->target, &pid);
            if (MouseButtonsSystem::injectionBlocked(pid)) return finish(Result::Elevated);
            d->checked = true;
        }
        if (anyModifierDown() && !d->released) {
            if (d->waitedMs < kModifierWaitMs) {
                d->waitedMs += kTickMs;
                return;
            }
            // Siguen apretados (o Windows los da por apretados): se sueltan, con la tecla muda antes de Win o Alt.
            InputInjector(false).releaseModifiers();
            d->released = true;
        }

        if (!d->owner) d->owner = createOwner(d.get());
        if (!d->owner) return finish(Result::Failed);
        if (!d->open()) {
            // Otra app lo tiene abierto: se reintenta un rato. Nunca se pega sin haber guardado lo que había.
            d->waitedMs += kTickMs;
            if (d->waitedMs >= kModifierWaitMs + kOpenWaitMs) finish(Result::Failed);
            return;
        }
        d->snapshot();
        EmptyClipboard();
        SetClipboardData(CF_UNICODETEXT, nullptr); // render diferido: el texto se entrega cuando la ventana lo pide
        setMarker(kNoHistory);
        setMarker(kNoCloud);
        setMarker(kNoMonitor);
        CloseClipboard();
        d->phase = Private::Phase::Pasted;
        d->waitedMs = 0;
        d->rendered = false;
        QVector<INPUT> keys = pasteKeys();
        if (SendInput(UINT(keys.size()), keys.data(), sizeof(INPUT)) != UINT(keys.size())) {
            INPUT up = keyInput(VK_CONTROL, true);
            SendInput(1, &up, sizeof(INPUT)); // nunca queda Ctrl apretado
            finish(Result::Failed);
        }
    });
}

TextTyper::~TextTyper()
{
    d->timer.stop();
    // Si se cierra la app a mitad, lo que había vuelve al portapapeles (un intento, sin esperar). Si no se pudo,
    // al destruir la ventana WM_RENDERALLFORMATS deja el texto puesto.
    d->tryRestore();
    if (d->owner) DestroyWindow(d->owner);
}

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
    d->text = prepared(text);
    d->phase = Private::Phase::Waiting;
    d->waitedMs = 0;
    d->checked = false;
    d->released = false;
    if (AutomatedRun::active() || d->text.isEmpty()) {
        const Result result = d->text.isEmpty() ? Result::Typed : Result::LoggedOnly;
        qInfo() << "[dictation] (automatizada o vacio) no se pega nada:" << d->text.size() << "caracteres";
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
    // Después del Ctrl+V ya no hay nada que cortar: se espera a devolver lo que había en el portapapeles.
    if (!d->timer.isActive() || d->phase != Private::Phase::Waiting) {
        return;
    }
    d->timer.stop();
    qInfo() << "[dictation] Pegado detenido con Esc antes de pegar";
    d->phase = Private::Phase::Idle;
    d->text.clear();
    QTimer::singleShot(0, this, [this]() { emit finished(Result::Stopped, 0); });
}

QStringList TextTyper::describe(const QString &text)
{
    const QString paste = prepared(text);
    if (paste.isEmpty()) return {};
    QString shown = paste;
    shown.replace(QLatin1String("\r"), QLatin1String("\\r")).replace(QLatin1String("\n"), QLatin1String("\\n"));
    QStringList lines{QStringLiteral("texto %1").arg(shown), QStringLiteral("formato CF_UNICODETEXT diferido")};
    for (const wchar_t *marker : {kNoHistory, kNoCloud, kNoMonitor}) {
        lines << QStringLiteral("marca %1").arg(QString::fromWCharArray(marker));
    }
    for (const INPUT &input : pasteKeys()) {
        lines << QStringLiteral("tecla vk=0x%1 %2%3")
                     .arg(input.ki.wVk, 2, 16, QLatin1Char('0'))
                     .arg(input.ki.dwFlags & KEYEVENTF_KEYUP ? QStringLiteral("arriba") : QStringLiteral("abajo"),
                          input.ki.dwExtraInfo == kMightyToolsInputTag ? QString() : QStringLiteral(" SIN-MARCA"));
    }
    return lines;
}
