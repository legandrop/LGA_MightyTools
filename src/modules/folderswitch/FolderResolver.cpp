#include "modules/folderswitch/FolderResolver.h"

#include "modules/folderswitch/FolderSwitchLogic.h"

#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QUrl>

#include <exdisp.h>
#include <objidl.h>
#include <shlobj.h>

// Copia de LGA_FolderSwitch (src/core/FolderResolver.cpp). Unico cambio de logica respecto del
// origen (plan, seccion 11 y auditoria de la etapa 2): resolveExplorerPathImpl() se mide, y su
// llamada Shell COM (cruza a Explorer.exe, un proceso ajeno) se acota con IRpcOptions.

namespace {

// Acota la llamada cruzada a Explorer.exe: IShellWindows es un proxy a un objeto QUE YA ESTA
// CORRIENDO en Explorer, asi que cada metodo es una llamada RPC entre procesos. Si Explorer esta
// trabado, sin esto el hilo de UI de toda la app queda esperando lo que tarde Explorer en
// responder (puede ser indefinido). IRpcOptions::Set con COMBND_RPCTIMEOUT es la forma
// documentada de acotar UN proxy sin tocar hilos ni el apartment de COM (a diferencia de
// CoCancelCall, que necesita un hilo aparte para cancelar el que esta bloqueado). 2000 ms: una
// llamada sana a IShellWindows/IWebBrowser2 tarda unos pocos ms; dos segundos ya distingue eso de
// un Explorer realmente trabado, sin sumar una espera larga al cambio de foreground.
constexpr ULONG kExplorerRpcTimeoutMs = 2000;

void limitShellWindowsTimeout(IShellWindows *shellWindows)
{
    IRpcOptions *rpcOptions = nullptr;
    if (FAILED(shellWindows->QueryInterface(IID_IRpcOptions, reinterpret_cast<void **>(&rpcOptions)))
        || !rpcOptions) {
        // Sin proxy RPC (por ejemplo si Explorer expusiera el objeto in-proc): sin IRpcOptions no hay
        // forma de acotar esta llamada especifica sin mover a un hilo. Riesgo documentado en el
        // informe.
        qWarning() << "[folderSwitch] IRpcOptions no disponible en IShellWindows: llamada a Explorer sin timeout";
        return;
    }
    const HRESULT hr = rpcOptions->Set(shellWindows, COMBND_RPCTIMEOUT, kExplorerRpcTimeoutMs);
    if (FAILED(hr)) {
        qWarning() << "[folderSwitch] IRpcOptions::Set(COMBND_RPCTIMEOUT) fallo, hr=" << hr;
    }
    rpcOptions->Release();
}

QString resolveExplorerPathImpl(HWND hwnd)
{
    if (!hwnd) {
        return QString();
    }

    IShellWindows *shellWindows = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL,
                                  IID_IShellWindows, reinterpret_cast<void **>(&shellWindows));
    if (FAILED(hr) || !shellWindows) {
        qWarning() << "[FolderResolver] CoCreateInstance(ShellWindows) fallo, hr=" << hr;
        return QString();
    }
    limitShellWindowsTimeout(shellWindows);

    QString result;

    long count = 0;
    shellWindows->get_Count(&count);

    for (long i = 0; i < count && result.isEmpty(); ++i) {
        VARIANT idx;
        VariantInit(&idx);
        idx.vt = VT_I4;
        idx.lVal = i;

        IDispatch *dispatch = nullptr;
        if (FAILED(shellWindows->Item(idx, &dispatch)) || !dispatch) {
            continue;
        }

        IWebBrowser2 *browser = nullptr;
        HRESULT qi = dispatch->QueryInterface(IID_IWebBrowser2, reinterpret_cast<void **>(&browser));
        dispatch->Release();
        if (FAILED(qi) || !browser) {
            continue;
        }

        SHANDLE_PTR hwndPtr = 0;
        HRESULT gotHwnd = browser->get_HWND(&hwndPtr);
        if (FAILED(gotHwnd) || reinterpret_cast<HWND>(hwndPtr) != hwnd) {
            browser->Release();
            continue;
        }

        BSTR url = nullptr;
        if (SUCCEEDED(browser->get_LocationURL(&url)) && url) {
            const QString locationUrl = QString::fromWCharArray(url, SysStringLen(url));
            SysFreeString(url);
            if (!locationUrl.isEmpty()) {
                // Carpeta virtual (Este equipo, Papelera, etc.) da toLocalFile() vacio.
                result = QUrl(locationUrl).toLocalFile();
            }
        }
        browser->Release();
    }

    shellWindows->Release();
    return result;
}

} // namespace

namespace FolderResolver {

QString resolveExplorerPath(HWND hwnd)
{
    QElapsedTimer timer;
    timer.start();
    const QString result = resolveExplorerPathImpl(hwnd);
    FolderSwitchLogic::logCallTiming("resolveExplorerPath", timer.elapsed());
    return result;
}

QString resolveXYplorerPath(HWND hwnd)
{
    if (!hwnd) {
        return QString();
    }

    wchar_t buf[1024] = {0};
    GetWindowTextW(hwnd, buf, static_cast<int>(std::size(buf)));
    QString title = QString::fromWCharArray(buf);

    const QString marker = QStringLiteral(" - XYplorer");
    const int markerIdx = title.indexOf(marker);
    QString candidate = (markerIdx >= 0) ? title.left(markerIdx) : title;
    candidate = candidate.trimmed();

    if (candidate.isEmpty()) {
        return QString();
    }

    const QFileInfo info(candidate);
    if (info.isDir()) {
        return candidate;
    }
    if (info.exists()) {
        return info.absolutePath();
    }
    return QString();
}

} // namespace FolderResolver
