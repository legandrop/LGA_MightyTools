#include "modules/folderswitch/WindowUtils.h"

#include <QDebug>
#include <QHash>
#include <QStringList>

#include <psapi.h>
#include <uiautomation.h>
#include <objbase.h>

// Copia de LGA_FolderSwitch (src/core/WindowUtils.cpp), sin cambios de logica.

namespace {

QString classNameOf(HWND hwnd)
{
    wchar_t buf[256] = {0};
    GetClassNameW(hwnd, buf, static_cast<int>(std::size(buf)));
    return QString::fromWCharArray(buf);
}

struct FindDescendantContext {
    QStringList classNames;
    bool found = false;
};

BOOL CALLBACK enumChildProc(HWND hwnd, LPARAM lParam)
{
    auto *ctx = reinterpret_cast<FindDescendantContext *>(lParam);
    const QString cls = classNameOf(hwnd);
    for (const QString &target : ctx->classNames) {
        if (cls.compare(target, Qt::CaseInsensitive) == 0) {
            ctx->found = true;
            return FALSE; // dejar de enumerar
        }
    }
    return TRUE;
}

bool hasDescendantOfClass(HWND hwnd, const QStringList &classNames)
{
    FindDescendantContext ctx;
    ctx.classNames = classNames;
    EnumChildWindows(hwnd, enumChildProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.found;
}

} // namespace

namespace WindowUtils {

bool isExplorerWindow(HWND hwnd)
{
    if (!hwnd) {
        return false;
    }
    return classNameOf(hwnd).compare(QStringLiteral("CabinetWClass"), Qt::CaseInsensitive) == 0;
}

QString processExeName(HWND hwnd)
{
    if (!hwnd) {
        return QString();
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) {
        return QString();
    }
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProcess) {
        return QString();
    }
    wchar_t path[MAX_PATH] = {0};
    DWORD size = static_cast<DWORD>(std::size(path));
    QString result;
    if (QueryFullProcessImageNameW(hProcess, 0, path, &size)) {
        const QString fullPath = QString::fromWCharArray(path, static_cast<int>(size));
        const int slash = fullPath.lastIndexOf(QLatin1Char('\\'));
        result = (slash >= 0) ? fullPath.mid(slash + 1) : fullPath;
    }
    CloseHandle(hProcess);
    return result;
}

bool isXYplorerWindow(HWND hwnd)
{
    if (!hwnd) {
        return false;
    }
    // El chequeo por exe es el definitivo: la clase VB6 es generica.
    const QString exeName = processExeName(hwnd);
    if (exeName.compare(QStringLiteral("xyplorer.exe"), Qt::CaseInsensitive) == 0) {
        return true;
    }
    return classNameOf(hwnd).compare(QStringLiteral("ThunderRT6FormDC"), Qt::CaseInsensitive) == 0;
}

bool isFileManagerWindow(HWND hwnd)
{
    return isExplorerWindow(hwnd) || isXYplorerWindow(hwnd);
}

bool isFileDialogWindow(HWND hwnd)
{
    if (!hwnd) {
        return false;
    }
    if (classNameOf(hwnd).compare(QStringLiteral("#32770"), Qt::CaseInsensitive) != 0) {
        return false;
    }
    if (hasDescendantOfClass(hwnd, {QStringLiteral("DUIViewWndClassName"),
                                    QStringLiteral("SHELLDLL_DefView"),
                                    QStringLiteral("ComboBoxEx32")})) {
        return true;
    }
    return false;
}

namespace {

// Cache de resultados de isQtFileDialog por HWND: el recorrido UIA no es gratis y no queremos
// repetirlo en cada cambio de foco.
QHash<HWND, bool> &qtFileDialogCache()
{
    static QHash<HWND, bool> cache;
    return cache;
}

const QStringList &qtAcceptButtonNames()
{
    static const QStringList names = {
        QStringLiteral("open"), QStringLiteral("save"), QStringLiteral("choose"),
        QStringLiteral("select"), QStringLiteral("abrir"), QStringLiteral("guardar"),
    };
    return names;
}

QString bstrToQString(BSTR bstr)
{
    if (!bstr) {
        return QString();
    }
    return QString::fromWCharArray(bstr, static_cast<int>(SysStringLen(bstr)));
}

// Recorrido UIA real: hwnd ya paso el prefiltro barato (clase "Qt..." + owner).
bool uiaLooksLikeFileDialog(HWND hwnd)
{
    IUIAutomation *automation = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_IUIAutomation, reinterpret_cast<void **>(&automation));
    if (FAILED(hr) || !automation) {
        return false;
    }

    IUIAutomationElement *element = nullptr;
    hr = automation->ElementFromHandle(hwnd, &element);
    if (FAILED(hr) || !element) {
        automation->Release();
        return false;
    }

    bool hasEdit = false;
    {
        VARIANT var;
        VariantInit(&var);
        var.vt = VT_I4;
        var.lVal = UIA_EditControlTypeId;
        IUIAutomationCondition *cond = nullptr;
        if (SUCCEEDED(automation->CreatePropertyCondition(UIA_ControlTypePropertyId, var, &cond)) && cond) {
            IUIAutomationElementArray *arr = nullptr;
            if (SUCCEEDED(element->FindAll(TreeScope_Descendants, cond, &arr)) && arr) {
                int count = 0;
                arr->get_Length(&count);
                hasEdit = count > 0;
                arr->Release();
            }
            cond->Release();
        }
        VariantClear(&var);
    }

    bool hasAcceptButton = false;
    if (hasEdit) {
        VARIANT var;
        VariantInit(&var);
        var.vt = VT_I4;
        var.lVal = UIA_ButtonControlTypeId;
        IUIAutomationCondition *cond = nullptr;
        if (SUCCEEDED(automation->CreatePropertyCondition(UIA_ControlTypePropertyId, var, &cond)) && cond) {
            IUIAutomationElementArray *arr = nullptr;
            if (SUCCEEDED(element->FindAll(TreeScope_Descendants, cond, &arr)) && arr) {
                int count = 0;
                arr->get_Length(&count);
                for (int i = 0; i < count && !hasAcceptButton; ++i) {
                    IUIAutomationElement *btn = nullptr;
                    if (FAILED(arr->GetElement(i, &btn)) || !btn) {
                        continue;
                    }
                    BSTR bstrName = nullptr;
                    QString name;
                    if (SUCCEEDED(btn->get_CurrentName(&bstrName))) {
                        name = bstrToQString(bstrName);
                        if (bstrName) SysFreeString(bstrName);
                    }
                    if (qtAcceptButtonNames().contains(name.trimmed(), Qt::CaseInsensitive)) {
                        hasAcceptButton = true;
                    }
                    btn->Release();
                }
                arr->Release();
            }
            cond->Release();
        }
        VariantClear(&var);
    }

    element->Release();
    automation->Release();
    return hasEdit && hasAcceptButton;
}

} // namespace

bool isQtFileDialog(HWND hwnd)
{
    if (!hwnd) {
        return false;
    }

    QHash<HWND, bool> &cache = qtFileDialogCache();
    auto it = cache.find(hwnd);
    if (it != cache.end()) {
        // Windows recicla HWNDs: si la ventana cacheada ya murio, la entrada puede pertenecer a otra
        // ventana distinta. Se reevalua.
        if (IsWindow(hwnd)) {
            return it.value();
        }
        cache.erase(it);
    }
    if (cache.size() > 64) {
        cache.clear();
    }

    // Prefiltro barato: class name "Qt..." y tiene owner (los dialogos tienen owner, la ventana
    // principal de la app no).
    if (!classNameOf(hwnd).startsWith(QStringLiteral("Qt"))) {
        cache.insert(hwnd, false);
        return false;
    }
    if (GetWindow(hwnd, GW_OWNER) == nullptr) {
        cache.insert(hwnd, false);
        return false;
    }

    const bool result = uiaLooksLikeFileDialog(hwnd);
    cache.insert(hwnd, result);
    return result;
}

} // namespace WindowUtils
