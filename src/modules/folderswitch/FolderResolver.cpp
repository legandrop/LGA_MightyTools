#include "modules/folderswitch/FolderResolver.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QUrl>

#include <exdisp.h>
#include <shlobj.h>

// Copia de LGA_FolderSwitch (src/core/FolderResolver.cpp), sin cambios de logica.

namespace FolderResolver {

QString resolveExplorerPath(HWND hwnd)
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
