#include "modules/folderswitch/WindowUtils.h"

#include "modules/folderswitch/FolderSwitchLogic.h"
#include "modules/folderswitch/UiaTimeouts.h"

#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QHash>
#include <QStringList>

#include <psapi.h>
#include <uiautomation.h>
#include <objbase.h>

// Copia de LGA_FolderSwitch (src/core/WindowUtils.cpp). Unico cambio de logica respecto del origen:
// el cache de isQtFileDialog ahora vence (FolderSwitchLogic::QtDialogCache) y uiaLooksLikeFileDialog
// se mide y se acota (plan, seccion 11 y auditoria de la etapa 2).

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

// TTL corto (ver FolderSwitchLogic::QtDialogCache): alcanza para no repetir el FindAll descendente
// en la misma rafaga de alt-tab, sin pretender que un HWND nunca cambia de naturaleza.
constexpr qint64 kQtDialogCacheTtlMs = 3000;

FolderSwitchLogic::QtDialogCache &qtFileDialogCache()
{
    static FolderSwitchLogic::QtDialogCache cache(kQtDialogCacheTtlMs);
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
    QElapsedTimer timer;
    timer.start();
    // Guarda de un solo punto de salida: cualquier `return` de abajo pasa antes por aca y deja la
    // medicion en el log (plan, seccion 11 y auditoria de la etapa 2: esta llamada corre en el hilo
    // de UI compartido con las otras cuatro herramientas).
    struct TimingGuard {
        QElapsedTimer &timer;
        ~TimingGuard() { FolderSwitchLogic::logCallTiming("uiaLooksLikeFileDialog", timer.elapsed()); }
    } guard{timer};

    IUIAutomation *automation = nullptr;
    HRESULT hr = UiaTimeouts::createAutomation(&automation);
    if (FAILED(hr) || !automation) {
        return false;
    }
    UiaTimeouts::apply(automation);

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

    FolderSwitchLogic::QtDialogCache &cache = qtFileDialogCache();
    const quintptr key = reinterpret_cast<quintptr>(hwnd);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    // Windows recicla HWNDs: si la ventana cacheada ya murio, la entrada puede pertenecer a otra
    // ventana distinta. Se invalida sin importar el TTL.
    if (!IsWindow(hwnd)) {
        cache.invalidate(key);
    } else {
        bool cached = false;
        if (cache.lookup(key, now, &cached)) {
            // Se evita otro FindAll descendente: uiaLooksLikeFileDialog mide unos pocos ms a decenas
            // de ms segun la ventana (ver el log "[folderSwitch] uiaLooksLikeFileDialog <ms> ms");
            // esta linea es lo que se ahorra CADA VEZ que el mismo HWND vuelve a preguntarse dentro
            // del TTL (alt-tab de ida y vuelta al mismo dialogo).
            qDebug().noquote() << QStringLiteral("[folderSwitch] isQtFileDialog cache hit hwnd=0x%1 (evita otro "
                                                 "uiaLooksLikeFileDialog)")
                                       .arg(key, 0, 16);
            return cached;
        }
    }

    // Prefiltro barato: class name "Qt..." y tiene owner (los dialogos tienen owner, la ventana
    // principal de la app no).
    if (!classNameOf(hwnd).startsWith(QStringLiteral("Qt"))) {
        cache.store(key, false, now);
        return false;
    }
    if (GetWindow(hwnd, GW_OWNER) == nullptr) {
        cache.store(key, false, now);
        return false;
    }

    const bool result = uiaLooksLikeFileDialog(hwnd);
    cache.store(key, result, now);
    return result;
}

} // namespace WindowUtils
