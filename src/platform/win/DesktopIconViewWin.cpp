#include "platform/DesktopIconView.h"

#include "core/AutomatedRun.h"

#include <QDebug>
#include <QHash>

#include <windows.h>
#include <exdisp.h>
#include <servprov.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <vector>

using Microsoft::WRL::ComPtr;

// La tecnica del Explorador: la ventana del escritorio es una "shell window" mas (SWC_DESKTOP); su
// navegador da la vista activa y la vista es un IFolderView2 con las posiciones de cada icono. Las
// llamadas cruzan al proceso del Explorador.

namespace {

// SID_STopLevelBrowser (shlguid.h), definido aca para no depender de INITGUID.
const GUID kTopLevelBrowser = {0x4c96be40, 0x915c, 0x11cf, {0x99, 0xd3, 0x00, 0xaa, 0x00, 0x4a, 0xe8, 0x37}};

// Como en FolderResolver: cada proxy al Explorador se acota (RPC_C_BINDING_MIN_TIMEOUT, una escala, no
// milisegundos) para que un Explorador trabado no deje colgada la ventana de la app.
void limitTimeout(IUnknown *proxy)
{
    ComPtr<IRpcOptions> options;
    if (proxy && SUCCEEDED(proxy->QueryInterface(IID_PPV_ARGS(&options)))) {
        options->Set(proxy, COMBND_RPCTIMEOUT, RPC_C_BINDING_MIN_TIMEOUT);
    }
}

// Pixeles fisicos aunque el proceso no haya declarado DPI (--simulate-action corre sin QGuiApplication):
// la pantalla tiene que medirse en la misma escala que las posiciones que da el Explorador.
class PhysicalPixels
{
public:
    PhysicalPixels() : m_previous(SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {}
    ~PhysicalPixels()
    {
        if (m_previous) SetThreadDpiAwarenessContext(m_previous);
    }

private:
    DPI_AWARENESS_CONTEXT m_previous;
};

QString hrText(const char *what, HRESULT hr)
{
    return QStringLiteral("%1 (0x%2)").arg(QLatin1String(what)).arg(quint32(hr), 8, 16, QLatin1Char('0'));
}

bool desktopView(ComPtr<IFolderView2> *view, HWND *window, QString *error)
{
    ComPtr<IShellWindows> windows;
    HRESULT hr = CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&windows));
    if (FAILED(hr)) {
        *error = hrText("ShellWindows", hr);
        return false;
    }
    limitTimeout(windows.Get());
    VARIANT location;
    VariantInit(&location);
    location.vt = VT_I4;
    location.lVal = CSIDL_DESKTOP;
    VARIANT empty;
    VariantInit(&empty);
    long hwnd = 0;
    ComPtr<IDispatch> dispatch;
    hr = windows->FindWindowSW(&location, &empty, SWC_DESKTOP, &hwnd, SWFO_NEEDDISPATCH, &dispatch);
    if (hr != S_OK || !dispatch) {
        *error = hrText("FindWindowSW", hr == S_OK ? E_FAIL : hr);
        return false;
    }
    limitTimeout(dispatch.Get());
    ComPtr<IServiceProvider> provider;
    ComPtr<IShellBrowser> browser;
    ComPtr<IShellView> shellView;
    if (FAILED(hr = dispatch.As(&provider)) || FAILED(hr = provider->QueryService(kTopLevelBrowser, IID_PPV_ARGS(&browser)))) {
        *error = hrText("QueryService", hr);
        return false;
    }
    limitTimeout(browser.Get());
    if (FAILED(hr = browser->QueryActiveShellView(&shellView)) || !shellView) {
        *error = hrText("QueryActiveShellView", FAILED(hr) ? hr : E_FAIL);
        return false;
    }
    limitTimeout(shellView.Get());
    if (FAILED(hr = shellView.As(view))) {
        *error = hrText("IFolderView2", hr);
        return false;
    }
    limitTimeout(view->Get());
    if (window) *window = reinterpret_cast<HWND>(static_cast<LONG_PTR>(hwnd));
    return true;
}

QString shellName(PCUITEMID_CHILD child, SIGDN kind)
{
    // El escritorio es la raiz del espacio de nombres: el hijo de su vista ya es un ID absoluto.
    PWSTR text = nullptr;
    if (FAILED(SHGetNameFromIDList(reinterpret_cast<PCIDLIST_ABSOLUTE>(child), kind, &text)) || !text) {
        return QString();
    }
    const QString name = QString::fromWCharArray(text);
    CoTaskMemFree(text);
    return name;
}

// Los iconos de la vista con su ID (el llamador libera cada uno con CoTaskMemFree).
struct Item
{
    PITEMID_CHILD id = nullptr;
    DesktopIconView::Icon icon;
};

bool enumerate(IFolderView2 *view, QList<Item> *items, QString *error)
{
    ComPtr<IEnumIDList> list;
    HRESULT hr = view->Items(SVGIO_ALLVIEW, IID_PPV_ARGS(&list));
    if (FAILED(hr) || !list) {
        *error = hrText("Items", FAILED(hr) ? hr : E_FAIL);
        return false;
    }
    limitTimeout(list.Get());
    PITEMID_CHILD child = nullptr;
    while (list->Next(1, &child, nullptr) == S_OK) {
        Item item;
        item.id = child;
        item.icon.path = shellName(child, SIGDN_DESKTOPABSOLUTEPARSING);
        item.icon.name = shellName(child, SIGDN_NORMALDISPLAY);
        POINT point{};
        if (item.icon.path.isEmpty() || FAILED(view->GetItemPosition(child, &point))) {
            CoTaskMemFree(child);
            continue;
        }
        item.icon.position = QPoint(point.x, point.y);
        items->append(item);
    }
    return true;
}

void release(QList<Item> &items)
{
    for (Item &item : items) CoTaskMemFree(item.id);
    items.clear();
}

} // namespace

namespace DesktopIconView {

QSize screenSize()
{
    const PhysicalPixels physical;
    return QSize(GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN));
}

qint64 awakeClockMs()
{
    ULONGLONG hundredNs = 0;
    QueryUnbiasedInterruptTime(&hundredNs);
    return qint64(hundredNs / 10000);
}

qint64 idleMs()
{
    LASTINPUTINFO info{};
    info.cbSize = sizeof(info);
    if (!GetLastInputInfo(&info)) return 0;
    return qint64(DWORD(GetTickCount() - info.dwTime)); // DWORD: sobrevive a la vuelta de GetTickCount
}

bool read(Layout *layout, QString *error)
{
    QString detail;
    const PhysicalPixels physical;
    ComPtr<IFolderView2> view;
    HWND window = nullptr;
    QList<Item> items;
    if (!desktopView(&view, &window, &detail) || !enumerate(view.Get(), &items, &detail)) {
        if (error) *error = detail;
        qWarning() << "[desktopIcons] no se pudo leer el escritorio:" << detail;
        return false;
    }
    Layout result;
    for (const Item &item : std::as_const(items)) result.icons.append(item.icon);
    release(items);
    result.screen = QSize(GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN));
    const UINT dpi = window ? GetDpiForWindow(window) : 0;
    result.dpi = dpi ? int(dpi) : 96;
    POINT spacing{};
    if (SUCCEEDED(view->GetSpacing(&spacing))) result.spacing = QSize(spacing.x, spacing.y);
    DWORD flags = 0;
    if (SUCCEEDED(view->GetCurrentFolderFlags(&flags))) {
        result.autoArrange = flags & FWF_AUTOARRANGE;
        result.snapToGrid = flags & FWF_SNAPTOGRID;
    }
    *layout = result;
    return true;
}

bool apply(bool autoArrange, bool snapToGrid, const QList<Icon> &moves, bool dryRun, ApplyResult *result,
           QString *error)
{
    ApplyResult outcome;
    // Corrida automatizada: nunca se mueve un icono ni se cambian los flags del escritorio real.
    if (dryRun || AutomatedRun::active()) {
        outcome.dryRun = true;
        outcome.moved = moves.size();
        qInfo().noquote() << QStringLiteral("[desktopIcons] solo loguear: flags autoArrange=%1 snapToGrid=%2, %3 iconos a mover")
                                 .arg(autoArrange)
                                 .arg(snapToGrid)
                                 .arg(moves.size());
        if (result) *result = outcome;
        return true;
    }
    QString detail;
    ComPtr<IFolderView2> view;
    QList<Item> items;
    if (!desktopView(&view, nullptr, &detail) || !enumerate(view.Get(), &items, &detail)) {
        if (error) *error = detail;
        qWarning() << "[desktopIcons] no se pudo abrir el escritorio para restaurar:" << detail;
        return false;
    }
    QHash<QString, PITEMID_CHILD> byPath;
    for (const Item &item : std::as_const(items)) byPath.insert(item.icon.path, item.id);
    std::vector<PCUITEMID_CHILD> ids;
    std::vector<POINT> points;
    for (const Icon &icon : moves) {
        PITEMID_CHILD child = byPath.value(icon.path);
        if (!child) {
            ++outcome.notFound;
            continue;
        }
        ids.push_back(child);
        points.push_back(POINT{icon.position.x(), icon.position.y()});
    }
    // Primero los flags: con "Organizar automaticamente" prendido el Explorador reacomodaria encima.
    HRESULT hr = view->SetCurrentFolderFlags(FWF_AUTOARRANGE | FWF_SNAPTOGRID,
                                             (autoArrange ? FWF_AUTOARRANGE : 0) | (snapToGrid ? FWF_SNAPTOGRID : 0));
    if (FAILED(hr)) qWarning().noquote() << "[desktopIcons]" << hrText("SetCurrentFolderFlags", hr);
    bool ok = true;
    if (!ids.empty()) {
        // Una sola llamada con todos: moverlos de a uno deja que el Explorador reacomode entre llamada y llamada.
        hr = view->SelectAndPositionItems(UINT(ids.size()), ids.data(), points.data(), SVSI_POSITIONITEM);
        if (FAILED(hr)) {
            detail = hrText("SelectAndPositionItems", hr);
            ok = false;
        }
        // Se cuenta como restaurado solo lo que quedo exacto, releido despues.
        for (size_t i = 0; ok && i < ids.size(); ++i) {
            POINT now{};
            if (SUCCEEDED(view->GetItemPosition(ids[i], &now)) && now.x == points[i].x && now.y == points[i].y) {
                ++outcome.moved;
            } else {
                ++outcome.notPlaced;
            }
        }
    }
    release(items);
    if (!ok) {
        if (error) *error = detail;
        qWarning() << "[desktopIcons] no se pudieron mover los iconos:" << detail;
        return false;
    }
    qInfo().noquote() << QStringLiteral("[desktopIcons] restaurado: %1 en su lugar, %2 sin encontrar, %3 en otro lugar")
                             .arg(outcome.moved)
                             .arg(outcome.notFound)
                             .arg(outcome.notPlaced);
    if (result) *result = outcome;
    return true;
}

} // namespace DesktopIconView
