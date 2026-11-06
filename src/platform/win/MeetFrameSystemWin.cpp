#include "platform/MeetFrameSystem.h"

#include "platform/AppToggleSystem.h"
#include "platform/OverlayWindow.h"

#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHash>
#include <QPainter>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QWidget>

#include <windows.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <dxgi1_2.h>

#include <algorithm>
#include <cstring>

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

// Meet Frame en Windows. Tres piezas:
//  - La ventana compartida: una ventana Win32 propia («Meet Frame», en la barra de tareas para que la reunion la
//    liste) pegada al fondo del orden Z. Las reuniones capturan una ventana aunque este tapada, asi que no hace
//    falta que el usuario la vea. Nunca se activa ni se minimiza.
//  - La captura: DXGI Desktop Duplication del monitor donde esta la region (en la GPU, solo entrega lo que
//    cambio). Si no se puede (sesion remota, monitor rotado, acceso perdido), GDI (BitBlt) como respaldo.
//  - Lo que se tapa: las ventanas de nivel superior en orden Z, con MeetFrameRules::computeMask. Se pinta liso
//    encima de la imagen antes de mostrarla: lo tapado nunca llega a la ventana compartida.

namespace MeetFrameSystem {

namespace {

const wchar_t kSharedClass[] = L"LGA_MeetFrame_Shared";

template <class T>
class Com
{
public:
    Com() = default;
    Com(const Com &) = delete;
    Com &operator=(const Com &) = delete;
    ~Com() { reset(); }
    void reset()
    {
        if (m_p) m_p->Release();
        m_p = nullptr;
    }
    T *get() const { return m_p; }
    T **put()
    {
        reset();
        return &m_p;
    }
    T *operator->() const { return m_p; }
    explicit operator bool() const { return m_p != nullptr; }

private:
    T *m_p = nullptr;
};

QRect toQRect(const RECT &r)
{
    return QRect(r.left, r.top, r.right - r.left, r.bottom - r.top);
}

QString classNameOf(HWND hwnd)
{
    wchar_t buffer[128] = {};
    const int n = GetClassNameW(hwnd, buffer, 128);
    return n > 0 ? QString::fromWCharArray(buffer, n) : QString();
}

QString titleOf(HWND hwnd)
{
    wchar_t buffer[512] = {};
    const int n = GetWindowTextW(hwnd, buffer, 512);
    return n > 0 ? QString::fromWCharArray(buffer, n) : QString();
}

QString processPath(DWORD pid)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) {
        return {};
    }
    wchar_t buffer[MAX_PATH * 2] = {};
    DWORD size = MAX_PATH * 2;
    QString path;
    if (QueryFullProcessImageNameW(process, 0, buffer, &size)) {
        path = QString::fromWCharArray(buffer, int(size));
    }
    CloseHandle(process);
    return path;
}

// Nombre del exe por proceso, con cache: se lee una vez por pid. La cache se vacia cada tanto (un pid se
// puede reusar).
class ProcessNames
{
public:
    QString exe(DWORD pid)
    {
        if (m_age.isValid() && m_age.elapsed() > 5000) {
            m_names.clear();
            m_age.restart();
        }
        if (!m_age.isValid()) m_age.start();
        const auto it = m_names.constFind(pid);
        if (it != m_names.constEnd()) return it.value();
        const QString path = processPath(pid);
        const QString name = path.isEmpty() ? QString() : QFileInfo(path).fileName().toLower();
        m_names.insert(pid, name);
        return name;
    }

private:
    QHash<DWORD, QString> m_names;
    QElapsedTimer m_age;
};

struct EnumContext
{
    QRect region;
    HWND shared = nullptr;
    QSet<HWND> ignored;
    ProcessNames *names = nullptr;
    const QSet<QString> *browsers = nullptr; ///< nullptr: leer el titulo de todas (diagnostico)
    bool wantTitles = false;
    QList<MeetFrameRules::WindowInfo> windows;
};

bool cloaked(HWND hwnd)
{
    DWORD value = 0;
    return SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &value, sizeof(value))) && value != 0;
}

QRect frameRect(HWND hwnd)
{
    RECT r{};
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r))) && r.right > r.left) {
        return toQRect(r);
    }
    if (GetWindowRect(hwnd, &r)) {
        return toQRect(r);
    }
    return {};
}

// Opaca: tapa lo que tiene debajo. Las transparentes al mouse, las semitransparentes, las de transparencia por
// pixel y las que se excluyen de captura (lo de abajo SI sale en la captura) no cuentan: si debajo hay algo a
// tapar, se tapa igual (de mas, nunca de menos).
bool coversBelow(HWND hwnd, LONG_PTR exStyle)
{
    if (exStyle & WS_EX_TRANSPARENT) {
        return false;
    }
    if (exStyle & WS_EX_LAYERED) {
        COLORREF key = 0;
        BYTE alpha = 0;
        DWORD flags = 0;
        if (!GetLayeredWindowAttributes(hwnd, &key, &alpha, &flags) || !(flags & LWA_ALPHA) || alpha != 255 || (flags & LWA_COLORKEY)) {
            return false;
        }
    }
    DWORD affinity = WDA_NONE;
    if (GetWindowDisplayAffinity(hwnd, &affinity) && affinity != WDA_NONE) {
        return false;
    }
    return true;
}

// Vistas del shell que muestran miniaturas en vivo de TODAS las ventanas (Alt+Tab, Vista de tareas, vistas
// previas de la barra de tareas): se tapan enteras, sea cual sea el exe.
bool thumbnailView(const QString &className)
{
    static const QStringList kClasses = {QStringLiteral("MultitaskingViewFrame"), QStringLiteral("XamlExplorerHostIslandWindow"),
                                         QStringLiteral("TaskListThumbnailWnd"), QStringLiteral("TaskSwitcherWnd"),
                                         QStringLiteral("ForegroundStaging"), QStringLiteral("TaskListOverlayWnd")};
    return kClasses.contains(className);
}

// Las apps de la Store: la ventana de nivel superior es de ApplicationFrameHost.exe y la app vive en su hija
// CoreWindow. El pid que cuenta es el de la hija.
DWORD appPid(HWND hwnd, const QString &className)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (className == QLatin1String("ApplicationFrameWindow")) {
        struct Find
        {
            DWORD frame;
            DWORD found = 0;
        } find{pid};
        EnumChildWindows(
            hwnd,
            [](HWND child, LPARAM param) -> BOOL {
                auto *f = reinterpret_cast<Find *>(param);
                if (classNameOf(child) != QLatin1String("Windows.UI.Core.CoreWindow")) return TRUE;
                DWORD childPid = 0;
                GetWindowThreadProcessId(child, &childPid);
                if (childPid && childPid != f->frame) {
                    f->found = childPid;
                    return FALSE;
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&find));
        if (find.found) pid = find.found;
    }
    return pid;
}

BOOL CALLBACK enumWindow(HWND hwnd, LPARAM param)
{
    auto *context = reinterpret_cast<EnumContext *>(param);
    if (!IsWindowVisible(hwnd)) {
        return TRUE;
    }
    const QString className = classNameOf(hwnd);
    // El escritorio: Progman, o el WorkerW que tiene los iconos (fondo en presentacion, fondos animados).
    const bool desktopHost = className == QLatin1String("WorkerW") && FindWindowExW(hwnd, nullptr, L"SHELLDLL_DefView", nullptr);
    if (className == QLatin1String("Progman") || desktopHost) {
        // El escritorio: lo que esta debajo no se ve. Ahi termina el recorrido.
        MeetFrameRules::WindowInfo desktop;
        desktop.desktop = true;
        desktop.className = className;
        context->windows.append(desktop);
        return FALSE;
    }
    if (context->ignored.contains(hwnd) || IsIconic(hwnd) || cloaked(hwnd)) {
        return TRUE;
    }
    const QRect rect = frameRect(hwnd);
    if (rect.isEmpty() || !rect.intersects(context->region)) {
        return TRUE;
    }
    MeetFrameRules::WindowInfo info;
    info.rect = rect;
    info.className = className;
    const DWORD pid = appPid(hwnd, className);
    // Se tapan siempre, sin contar como que tapan lo de abajo: la ventana compartida (si se ve, se capturaria a
    // si misma), las vistas con miniaturas de todas las ventanas, y el resto de las ventanas de esta app
    // (tooltips, la ventana principal con la lista de apps a tapar): el marco y su menu ya no salen (WDA).
    if (hwnd == context->shared || thumbnailView(className) || pid == GetCurrentProcessId()) {
        info.forceMask = true;
        info.covering = false;
        context->windows.append(info);
        return TRUE;
    }
    info.app = context->names->exe(pid);
    info.covering = coversBelow(hwnd, GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
    if (context->wantTitles && (!context->browsers || context->browsers->contains(info.app))) {
        info.title = titleOf(hwnd);
    }
    context->windows.append(info);
    return TRUE;
}

QList<MeetFrameRules::WindowInfo> enumerate(EnumContext &context)
{
    context.windows.clear();
    EnumWindows(&enumWindow, reinterpret_cast<LPARAM>(&context));
    return context.windows;
}

// ---- Captura

class Capture
{
public:
    enum class Result { NewFrame, NoChange, Failed };

    ~Capture() { closeDuplication(); }

    // `frame` es la imagen de la region (RGB32, del tamaño de la region): la captura escribe ahi.
    Result grab(const QRect &region, QImage &frame)
    {
        if (!m_gdiOnly) {
            const Result result = grabDxgi(region, frame);
            if (result != Result::Failed) {
                m_engine = QStringLiteral("dxgi");
                return result;
            }
        }
        m_engine = QStringLiteral("gdi");
        return grabGdi(region, frame) ? Result::NewFrame : Result::Failed;
    }

    QString engine() const { return m_engine; }
    void forceGdi() { m_gdiOnly = true; }

private:
    void closeDuplication()
    {
        if (m_frameHeld && m_duplication) m_duplication->ReleaseFrame();
        m_frameHeld = false;
        m_staging.reset();
        m_duplication.reset();
        m_context.reset();
        m_device.reset();
        m_output = QRect();
        m_stagingSize = QSize();
        m_haveFrame = false;
    }

    // El monitor que mas region tiene; abre (o reusa) su duplicacion.
    bool openFor(const QRect &region)
    {
        Com<IDXGIFactory1> factory;
        if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(factory.put())))) {
            return false;
        }
        Com<IDXGIAdapter1> bestAdapter;
        Com<IDXGIOutput> bestOutput;
        QRect bestRect;
        int bestArea = 0;
        for (UINT a = 0;; ++a) {
            Com<IDXGIAdapter1> adapter;
            if (factory->EnumAdapters1(a, adapter.put()) == DXGI_ERROR_NOT_FOUND) break;
            for (UINT o = 0;; ++o) {
                Com<IDXGIOutput> output;
                if (adapter->EnumOutputs(o, output.put()) == DXGI_ERROR_NOT_FOUND) break;
                DXGI_OUTPUT_DESC desc{};
                if (FAILED(output->GetDesc(&desc)) || !desc.AttachedToDesktop) continue;
                if (desc.Rotation != DXGI_MODE_ROTATION_IDENTITY && desc.Rotation != DXGI_MODE_ROTATION_UNSPECIFIED) continue;
                const QRect rect = toQRect(desc.DesktopCoordinates);
                const QRect common = rect.intersected(region);
                const int area = common.width() * common.height();
                if (area > bestArea) {
                    bestArea = area;
                    bestRect = rect;
                    adapter->AddRef();
                    *bestAdapter.put() = adapter.get();
                    output->AddRef();
                    *bestOutput.put() = output.get();
                }
            }
        }
        if (!bestArea) {
            return false;
        }
        if (m_duplication && bestRect == m_output) {
            return true; // mismo monitor: se reusa
        }
        closeDuplication();
        D3D_FEATURE_LEVEL level{};
        if (FAILED(D3D11CreateDevice(bestAdapter.get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                     D3D11_SDK_VERSION, m_device.put(), &level, m_context.put()))) {
            return false;
        }
        Com<IDXGIOutput1> output1;
        if (FAILED(bestOutput->QueryInterface(__uuidof(IDXGIOutput1), reinterpret_cast<void **>(output1.put())))) {
            return false;
        }
        const HRESULT hr = output1->DuplicateOutput(m_device.get(), m_duplication.put());
        if (FAILED(hr)) {
            qInfo().noquote() << "[meetFrame] DXGI no duplica este monitor, se usa GDI. hr=" << Qt::hex << ulong(hr);
            closeDuplication();
            return false;
        }
        m_output = bestRect;
        qInfo().noquote() << "[meetFrame] Captura DXGI del monitor" << bestRect;
        return true;
    }

    Result grabDxgi(const QRect &region, QImage &frame)
    {
        if (m_retryAt.isValid() && m_retryAt.elapsed() < 1000) {
            return Result::Failed; // acceso perdido hace poco (UAC, bloqueo): GDI mientras tanto
        }
        m_retryAt.invalidate();
        // La duplicacion abierta sirve mientras la region siga entera en ese monitor; si no, se busca otro.
        if (!(m_duplication && m_output.contains(region)) && !openFor(region)) {
            m_retryAt.start();
            return Result::Failed;
        }
        if (!m_output.contains(region)) {
            return Result::Failed; // entre dos monitores: GDI captura las dos partes
        }
        const QRect source = region;
        if (m_frameHeld) {
            m_duplication->ReleaseFrame();
            m_frameHeld = false;
        }
        DXGI_OUTDUPL_FRAME_INFO info{};
        Com<IDXGIResource> resource;
        HRESULT hr = m_duplication->AcquireNextFrame(0, &info, resource.put());
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            // Nada cambio en el monitor. Si la region se movio o cambio de tamaño, hay que volver a copiar:
            // la duplicacion no repite un cuadro, asi que se cae a GDI esta vez.
            if (m_haveFrame && region == m_lastRegion) return Result::NoChange;
            return grabGdi(region, frame) ? Result::NewFrame : Result::Failed;
        }
        if (FAILED(hr)) {
            qInfo().noquote() << "[meetFrame] DXGI perdio el acceso (" << Qt::hex << ulong(hr) << "): se reabre en 1 s";
            closeDuplication();
            m_retryAt.start();
            return Result::Failed;
        }
        m_frameHeld = true;
        if (info.LastPresentTime.QuadPart == 0 && m_haveFrame && region == m_lastRegion) {
            return Result::NoChange; // solo se movio el puntero
        }
        Com<ID3D11Texture2D> texture;
        if (FAILED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void **>(texture.put())))) {
            return Result::Failed;
        }
        D3D11_TEXTURE2D_DESC textureDesc{};
        texture->GetDesc(&textureDesc);
        if (textureDesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) {
            // Un monitor HDR o de 10 bits: la copia a BGRA de 8 bits no serviria. GDI convierte solo.
            qInfo().noquote() << "[meetFrame] El monitor entrega el formato" << int(textureDesc.Format) << ": se usa GDI";
            closeDuplication();
            m_gdiOnly = true;
            return Result::Failed;
        }
        if (m_stagingSize != source.size()) {
            m_staging.reset();
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = UINT(source.width());
            desc.Height = UINT(source.height());
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_STAGING;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            if (FAILED(m_device->CreateTexture2D(&desc, nullptr, m_staging.put()))) {
                m_stagingSize = QSize();
                return Result::Failed;
            }
            m_stagingSize = source.size();
        }
        D3D11_BOX box{};
        box.left = UINT(source.left() - m_output.left());
        box.top = UINT(source.top() - m_output.top());
        box.right = box.left + UINT(source.width());
        box.bottom = box.top + UINT(source.height());
        box.front = 0;
        box.back = 1;
        m_context->CopySubresourceRegion(m_staging.get(), 0, 0, 0, 0, texture.get(), 0, &box);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(m_context->Map(m_staging.get(), 0, D3D11_MAP_READ, 0, &mapped))) {
            return Result::Failed;
        }
        const auto *src = static_cast<const uchar *>(mapped.pData);
        for (int y = 0; y < source.height(); ++y) {
            std::memcpy(frame.scanLine(y), src + size_t(y) * mapped.RowPitch, size_t(source.width()) * 4);
        }
        m_context->Unmap(m_staging.get(), 0);
        m_haveFrame = true;
        m_lastRegion = region;
        return Result::NewFrame;
    }

    bool grabGdi(const QRect &region, QImage &frame)
    {
        HDC screen = GetDC(nullptr);
        if (!screen) return false;
        HDC memory = CreateCompatibleDC(screen);
        HBITMAP bitmap = CreateCompatibleBitmap(screen, region.width(), region.height());
        bool ok = false;
        if (memory && bitmap) {
            HGDIOBJ old = SelectObject(memory, bitmap);
            ok = BitBlt(memory, 0, 0, region.width(), region.height(), screen, region.left(), region.top(), SRCCOPY) != 0;
            SelectObject(memory, old);
            if (ok) {
                BITMAPINFO bmi{};
                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bmi.bmiHeader.biWidth = region.width();
                bmi.bmiHeader.biHeight = -region.height();
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 32;
                bmi.bmiHeader.biCompression = BI_RGB;
                ok = GetDIBits(memory, bitmap, 0, UINT(region.height()), frame.bits(), &bmi, DIB_RGB_COLORS) == region.height();
            }
        }
        if (bitmap) DeleteObject(bitmap);
        if (memory) DeleteDC(memory);
        ReleaseDC(nullptr, screen);
        if (ok) {
            m_lastRegion = region;
        }
        return ok;
    }

    Com<ID3D11Device> m_device;
    Com<ID3D11DeviceContext> m_context;
    Com<IDXGIOutputDuplication> m_duplication;
    Com<ID3D11Texture2D> m_staging;
    QSize m_stagingSize;
    QRect m_output;
    QRect m_lastRegion;
    bool m_frameHeld = false;
    bool m_haveFrame = false;
    bool m_gdiOnly = false;
    QElapsedTimer m_retryAt;
    QString m_engine = QStringLiteral("dxgi");
};

// ---- La sesion

class SessionWin : public Session
{
public:
    SessionWin() = default;
    ~SessionWin() override
    {
        m_timer.stop();
        if (m_hwnd) {
            SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, 0);
            DestroyWindow(m_hwnd);
        }
        releaseBuffers();
    }

    bool open(const QString &title, const QRect &region, const Options &options, QString *error)
    {
        m_options = options;
        static bool registered = false;
        HINSTANCE instance = GetModuleHandleW(nullptr);
        if (!registered) {
            WNDCLASSEXW wc{};
            wc.cbSize = sizeof(wc);
            wc.lpfnWndProc = &SessionWin::windowProc;
            wc.hInstance = instance;
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.lpszClassName = kSharedClass;
            wc.hIcon = static_cast<HICON>(LoadImageW(instance, L"IDI_ICON1", IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
            wc.hIconSm = static_cast<HICON>(LoadImageW(instance, L"IDI_ICON1", IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                                       GetSystemMetrics(SM_CYSMICON), 0));
            if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
                if (error) *error = QStringLiteral("RegisterClassEx %1").arg(GetLastError());
                return false;
            }
            registered = true;
        }
        const std::wstring wtitle = title.toStdWString();
        m_hwnd = CreateWindowExW(WS_EX_APPWINDOW, kSharedClass, wtitle.c_str(), WS_POPUP | WS_CLIPCHILDREN, 0, 0,
                                 qMax(1, region.width()), qMax(1, region.height()), nullptr, nullptr, instance, this);
        if (!m_hwnd) {
            if (error) *error = QStringLiteral("CreateWindowEx %1").arg(GetLastError());
            return false;
        }
        applyRegion(region, true);
        ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        SetWindowPos(m_hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        m_timer.setTimerType(Qt::PreciseTimer);
        QObject::connect(&m_timer, &QTimer::timeout, this, [this]() { tick(false); });
        applyTimer();
        tick(true);
        return true;
    }

    void setRegion(const QRect &region) override
    {
        if (region == m_region || region.isEmpty()) return;
        applyRegion(region, false);
        tick(true);
    }

    void setOptions(const Options &options) override
    {
        const bool rulesChanged = options.rules != m_options.rules || options.fill != m_options.fill;
        m_options = options;
        applyTimer();
        if (!options.paused) {
            tick(true);
        } else if (rulesChanged) {
            // En pausa la imagen queda congelada: se vuelve a tapar con las reglas nuevas sobre las ventanas
            // que habia cuando se tomo esa imagen.
            compose(MeetFrameRules::computeMask(m_frameWindows, m_region, m_options.rules).region | m_lastMask, m_lastCursor,
                    m_lastCursorHandle);
        }
    }

    void setIgnoredWindows(const QList<QWidget *> &windows) override
    {
        m_ignoredWidgets.clear();
        for (QWidget *widget : windows) {
            if (widget) m_ignoredWidgets << QPointer<QWidget>(widget);
        }
    }

    int hiddenWindows() const override { return m_hidden; }
    QString error() const override { return m_error; }
    QString engine() const override { return m_capture.engine(); }

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_NCCREATE) {
            const auto *create = reinterpret_cast<CREATESTRUCTW *>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        auto *self = reinterpret_cast<SessionWin *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            if (self && self->m_backDc) {
                BitBlt(dc, 0, 0, self->m_region.width(), self->m_region.height(), self->m_backDc, 0, 0, SRCCOPY);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_NCHITTEST:
            return HTCLIENT;
        case WM_WINDOWPOSCHANGING: {
            // Siempre al fondo: aunque se la elija en la barra de tareas, nunca tapa nada.
            auto *pos = reinterpret_cast<WINDOWPOS *>(lParam);
            if (!(pos->flags & SWP_NOZORDER)) pos->hwndInsertAfter = HWND_BOTTOM;
            return 0;
        }
        case WM_SYSCOMMAND:
            // Minimizada, la reunion deja de recibir imagen.
            if ((wParam & 0xFFF0) == SC_MINIMIZE || (wParam & 0xFFF0) == SC_MAXIMIZE) return 0;
            break;
        case WM_CLOSE:
            if (self) {
                QPointer<SessionWin> guard(self);
                QTimer::singleShot(0, self, [guard]() {
                    if (guard) emit guard->closeRequested();
                });
            }
            return 0;
        default:
            break;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    void releaseBuffers()
    {
        if (m_backDc) {
            SelectObject(m_backDc, m_backOld);
            DeleteDC(m_backDc);
        }
        if (m_backBitmap) DeleteObject(m_backBitmap);
        m_backDc = nullptr;
        m_backBitmap = nullptr;
        m_backOld = nullptr;
        m_back = QImage();
    }

    bool allocate(const QSize &size)
    {
        releaseBuffers();
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = size.width();
        bmi.bmiHeader.biHeight = -size.height(); // de arriba hacia abajo, como QImage
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        void *bits = nullptr;
        m_backBitmap = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!m_backBitmap || !bits) {
            m_backBitmap = nullptr;
            return false;
        }
        m_backDc = CreateCompatibleDC(nullptr);
        m_backOld = SelectObject(m_backDc, m_backBitmap);
        m_back = QImage(static_cast<uchar *>(bits), size.width(), size.height(), size.width() * 4, QImage::Format_RGB32);
        m_back.fill(m_options.fill);
        m_frame = QImage(size, QImage::Format_RGB32);
        m_frame.fill(m_options.fill);
        return true;
    }

    // Donde va la ventana compartida: un rincon de algun monitor que no pise la region (asi, si queda a la
    // vista sobre el escritorio, no se captura a si misma). Si no hay lugar, sobre la region: entonces lo que
    // se vea de ella se tapa (forceMask).
    QPoint placeShared(const QRect &region) const
    {
        struct Search
        {
            QRect avoid;
            QSize size;
            QPoint found;
            bool ok = false;
        } search{region.adjusted(-48, -48, 48, 48), region.size(), {}, false};
        EnumDisplayMonitors(
            nullptr, nullptr,
            [](HMONITOR monitor, HDC, LPRECT, LPARAM param) -> BOOL {
                auto *s = reinterpret_cast<Search *>(param);
                MONITORINFO info{};
                info.cbSize = sizeof(info);
                if (!GetMonitorInfoW(monitor, &info)) return TRUE;
                const QRect work = toQRect(info.rcWork);
                const QList<QPoint> corners = {work.topLeft(), QPoint(work.right() + 1 - s->size.width(), work.top()),
                                               QPoint(work.left(), work.bottom() + 1 - s->size.height()),
                                               QPoint(work.right() + 1 - s->size.width(), work.bottom() + 1 - s->size.height())};
                for (const QPoint &corner : corners) {
                    const QRect candidate(corner, s->size);
                    if (work.contains(candidate) && !candidate.intersects(s->avoid)) {
                        s->found = corner;
                        s->ok = true;
                        return FALSE;
                    }
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&search));
        return search.ok ? search.found : region.topLeft();
    }

    void applyRegion(const QRect &region, bool first)
    {
        const bool resized = first || region.size() != m_region.size();
        m_region = region;
        if (resized && !allocate(region.size())) {
            m_error = QStringLiteral("Could not prepare the shared window.");
        }
        const QPoint at = placeShared(region);
        SetWindowPos(m_hwnd, HWND_BOTTOM, at.x(), at.y(), region.width(), region.height(), SWP_NOACTIVATE);
        m_lastMask = QRegion();
    }

    void applyTimer()
    {
        if (m_options.paused) {
            m_timer.stop();
            return;
        }
        const int fps = qBound(5, m_options.fps, 60);
        m_timer.setInterval(1000 / fps);
        if (!m_timer.isActive()) m_timer.start();
    }

    QPoint hotspot(HCURSOR cursor)
    {
        const auto it = m_hotspots.constFind(cursor);
        if (it != m_hotspots.constEnd()) return it.value();
        QPoint spot;
        ICONINFO info{};
        if (GetIconInfo(cursor, &info)) {
            spot = QPoint(int(info.xHotspot), int(info.yHotspot));
            if (info.hbmMask) DeleteObject(info.hbmMask);
            if (info.hbmColor) DeleteObject(info.hbmColor);
        }
        if (m_hotspots.size() > 64) m_hotspots.clear();
        m_hotspots.insert(cursor, spot);
        return spot;
    }

    // La imagen que ve la reunion: lo ultimo capturado, lo tapado en liso y el puntero.
    void compose(const QRegion &paint, const QPoint &cursorAt, HCURSOR cursorHandle)
    {
        if (m_back.isNull() || m_frame.size() != m_back.size()) return;
        GdiFlush();
        std::memcpy(m_back.bits(), m_frame.constBits(), size_t(m_frame.sizeInBytes()));
        if (!paint.isEmpty()) {
            QPainter painter(&m_back);
            const QRegion local = paint.translated(-m_region.topLeft());
            for (const QRect &rect : local) painter.fillRect(rect, m_options.fill);
        }
        if (cursorHandle && m_options.showCursor && m_region.adjusted(-128, -128, 128, 128).contains(cursorAt)) {
            const QPoint spot = hotspot(cursorHandle);
            DrawIconEx(m_backDc, cursorAt.x() - spot.x() - m_region.left(), cursorAt.y() - spot.y() - m_region.top(), cursorHandle, 0, 0, 0,
                       nullptr, DI_NORMAL);
            GdiFlush();
        }
        m_lastMask = paint;
        m_lastCursor = cursorAt;
        m_lastCursorHandle = cursorHandle;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    void tick(bool force)
    {
        if (!m_hwnd || m_back.isNull() || (m_options.paused && !force)) {
            return;
        }
        if (m_options.paused) {
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return;
        }
        const Capture::Result result = m_capture.grab(m_region, m_frame);
        const QString oldError = m_error;
        m_error = result == Capture::Result::Failed ? QStringLiteral("Could not capture the screen.") : QString();

        EnumContext context;
        context.region = m_region;
        context.shared = m_hwnd;
        for (const QPointer<QWidget> &widget : m_ignoredWidgets) {
            if (widget && widget->isVisible()) context.ignored.insert(reinterpret_cast<HWND>(widget->winId()));
        }
        context.names = &m_names;
        context.browsers = &m_options.rules.browsers;
        context.wantTitles = !m_options.rules.browserTitles.isEmpty();
        const QList<MeetFrameRules::WindowInfo> windows = enumerate(context);
        const MeetFrameRules::Mask mask = MeetFrameRules::computeMask(windows, m_region, m_options.rules);
        if (result == Capture::Result::NewFrame) m_frameWindows = windows;

        CURSORINFO cursor{};
        cursor.cbSize = sizeof(cursor);
        const bool cursorShown = m_options.showCursor && GetCursorInfo(&cursor) && (cursor.flags & CURSOR_SHOWING) && cursor.hCursor;
        const QPoint cursorAt = cursorShown ? QPoint(cursor.ptScreenPos.x, cursor.ptScreenPos.y) : QPoint(-100000, -100000);
        const HCURSOR cursorHandle = cursorShown ? cursor.hCursor : nullptr;

        // La imagen puede ser de un cuadro antes que las posiciones leidas: se tapa tambien donde estaba lo tapado
        // en la pasada anterior (una ventana que se mueve no deja ver su borde de atras).
        const QRegion paint = mask.region | m_currentMask;
        const bool changedPicture = force || result == Capture::Result::NewFrame || paint != m_lastMask || cursorAt != m_lastCursor
                                    || cursorHandle != m_lastCursorHandle;
        m_currentMask = mask.region;
        if (changedPicture) {
            compose(paint, cursorAt, cursorHandle);
        }
        if (mask.hiddenWindows != m_hidden || m_error != oldError) {
            m_hidden = mask.hiddenWindows;
            emit changed();
        }
    }

    HWND m_hwnd = nullptr;
    QRect m_region;
    Options m_options;
    QTimer m_timer;
    Capture m_capture;
    ProcessNames m_names;
    QList<QPointer<QWidget>> m_ignoredWidgets;
    HDC m_backDc = nullptr;
    HBITMAP m_backBitmap = nullptr;
    HGDIOBJ m_backOld = nullptr;
    QImage m_back;  ///< lo que muestra la ventana compartida (vista de la DIB)
    QImage m_frame; ///< lo ultimo capturado, sin tapar
    QRegion m_lastMask;     ///< lo tapado en la imagen que se muestra ahora
    QRegion m_currentMask;  ///< lo que habia que tapar en la ultima pasada (se une con la siguiente)
    QList<MeetFrameRules::WindowInfo> m_frameWindows; ///< las ventanas cuando se tomo m_frame (para la pausa)
    QPoint m_lastCursor;
    HCURSOR m_lastCursorHandle = nullptr;
    QHash<HCURSOR, QPoint> m_hotspots;
    int m_hidden = 0;
    QString m_error;
};

} // namespace

std::unique_ptr<Session> start(const QString &windowTitle, const QRect &region, const Options &options, QString *error)
{
    auto session = std::make_unique<SessionWin>();
    if (!session->open(windowTitle, region, options, error)) {
        return nullptr;
    }
    qInfo().noquote() << "[meetFrame] Ventana compartida lista, region" << region << "captura" << session->engine();
    return session;
}

QRect captureRegion(QWidget *frame, const QMargins &margins)
{
    if (!frame) return {};
    HWND hwnd = reinterpret_cast<HWND>(frame->winId());
    RECT r{};
    if (!GetWindowRect(hwnd, &r)) return {};
    const UINT dpi = GetDpiForWindow(hwnd);
    const qreal scale = dpi ? dpi / 96.0 : frame->devicePixelRatioF();
    const int left = qRound(margins.left() * scale);
    const int top = qRound(margins.top() * scale);
    const int right = qRound(margins.right() * scale);
    const int bottom = qRound(margins.bottom() * scale);
    return QRect(QPoint(r.left + left, r.top + top), QPoint(r.right - right - 1, r.bottom - bottom - 1));
}

QSize pixelSize(QWidget *, const QRect &region)
{
    return region.size();
}

void prepareFrame(QWidget *frame)
{
    if (!frame) return;
    OverlayWindow::makeNonActivating(frame);
    excludeFromCapture(frame);
}

void excludeFromCapture(QWidget *window)
{
    if (!window) return;
    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (!SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE)) {
        qInfo().noquote() << "[meetFrame] SetWindowDisplayAffinity fallo:" << GetLastError();
    }
}

bool hasCapturePermission()
{
    return true;
}

void requestCapturePermission()
{
}

QList<RunningApp> runningApps()
{
    struct Collect
    {
        QSet<DWORD> pids;
    } collect;
    EnumWindows(
        [](HWND hwnd, LPARAM param) -> BOOL {
            auto *c = reinterpret_cast<Collect *>(param);
            if (!IsWindowVisible(hwnd) || cloaked(hwnd) || GetWindowTextLengthW(hwnd) == 0) return TRUE;
            const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
            const bool owned = GetWindow(hwnd, GW_OWNER) != nullptr;
            if ((ex & WS_EX_TOOLWINDOW) || (owned && !(ex & WS_EX_APPWINDOW))) return TRUE;
            const DWORD pid = appPid(hwnd, classNameOf(hwnd));
            if (pid && pid != GetCurrentProcessId()) c->pids.insert(pid);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&collect));
    QHash<QString, RunningApp> byId;
    for (DWORD pid : collect.pids) {
        const QString path = processPath(pid);
        const QString id = MeetFrameRules::appKey(path);
        if (id.isEmpty() || byId.contains(id) || id == QLatin1String("explorer.exe")) continue;
        RunningApp app;
        app.id = id;
        app.path = QDir::toNativeSeparators(path);
        app.name = AppToggleSystem::productName(path);
        if (app.name.isEmpty()) app.name = QFileInfo(path).completeBaseName();
        byId.insert(id, app);
    }
    QList<RunningApp> list = byId.values();
    std::sort(list.begin(), list.end(), [](const RunningApp &a, const RunningApp &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    return list;
}

QString appIdForPath(const QString &path)
{
    return MeetFrameRules::appKey(path);
}

QList<MeetFrameRules::WindowInfo> listWindows(const QRect &region)
{
    ProcessNames names;
    EnumContext context;
    context.region = region.isEmpty() ? QRect(GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                                              GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN))
                                        : region;
    context.names = &names;
    context.browsers = nullptr;
    context.wantTitles = true;
    return enumerate(context);
}

} // namespace MeetFrameSystem
