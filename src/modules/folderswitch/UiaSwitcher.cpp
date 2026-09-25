#include "modules/folderswitch/UiaSwitcher.h"

#include <QDebug>
#include <QDir>
#include <QStringList>

#include <uiautomation.h>
#include <objbase.h>

// Copia de LGA_FolderSwitch (src/core/UiaSwitcher.cpp), sin cambios de logica.

namespace {

// Nombres de boton "aceptar" aceptados, en varios idiomas/apps.
const QStringList &acceptButtonNames()
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

} // namespace

namespace UiaSwitcher {

bool switchQtDialog(HWND dlg, const QString &folder)
{
    if (!dlg || folder.isEmpty()) {
        return false;
    }

    IUIAutomation *automation = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_IUIAutomation, reinterpret_cast<void **>(&automation));
    if (FAILED(hr) || !automation) {
        qWarning() << "[UiaSwitcher] No se pudo crear IUIAutomation, hr=" << hr;
        return false;
    }

    IUIAutomationElement *element = nullptr;
    hr = automation->ElementFromHandle(dlg, &element);
    if (FAILED(hr) || !element) {
        qWarning() << "[UiaSwitcher] ElementFromHandle fallo, hr=" << hr;
        automation->Release();
        return false;
    }

    bool result = false;

    // --- Buscar todos los Edit descendientes ---
    VARIANT varEditType;
    VariantInit(&varEditType);
    varEditType.vt = VT_I4;
    varEditType.lVal = UIA_EditControlTypeId;

    IUIAutomationCondition *editCondition = nullptr;
    hr = automation->CreatePropertyCondition(UIA_ControlTypePropertyId, varEditType, &editCondition);
    VariantClear(&varEditType);

    IUIAutomationElementArray *editArray = nullptr;
    int editCount = 0;
    if (SUCCEEDED(hr) && editCondition) {
        hr = element->FindAll(TreeScope_Descendants, editCondition, &editArray);
        if (SUCCEEDED(hr) && editArray) {
            editArray->get_Length(&editCount);
        }
    }
    qDebug() << "[UiaSwitcher] Edit encontrados:" << editCount;

    if (editCount <= 0) {
        qWarning() << "[UiaSwitcher] No se encontro ningun Edit en el dialogo.";
        if (editCondition) editCondition->Release();
        if (editArray) editArray->Release();
        element->Release();
        automation->Release();
        return false;
    }

    // Elegir el edit destino: el que su valor actual contenga '/' o '\\'; si hay varios, el ultimo;
    // si ninguno, el ultimo Edit de la lista.
    IUIAutomationElement *targetEdit = nullptr;
    IUIAutomationElement *lastEdit = nullptr;
    QString targetPreviousValue;

    for (int i = 0; i < editCount; ++i) {
        IUIAutomationElement *editElem = nullptr;
        if (FAILED(editArray->GetElement(i, &editElem)) || !editElem) {
            continue;
        }

        QString currentValue;
        IUIAutomationValuePattern *valuePattern = nullptr;
        if (SUCCEEDED(editElem->GetCurrentPatternAs(UIA_ValuePatternId, IID_IUIAutomationValuePattern,
                                                      reinterpret_cast<void **>(&valuePattern))) &&
            valuePattern) {
            BSTR bstrVal = nullptr;
            if (SUCCEEDED(valuePattern->get_CurrentValue(&bstrVal))) {
                currentValue = bstrToQString(bstrVal);
                if (bstrVal) SysFreeString(bstrVal);
            }
            valuePattern->Release();
        }

        if (lastEdit) {
            lastEdit->Release();
        }
        lastEdit = editElem;
        lastEdit->AddRef();

        if (currentValue.contains(QLatin1Char('/')) || currentValue.contains(QLatin1Char('\\'))) {
            if (targetEdit) {
                targetEdit->Release();
            }
            targetEdit = editElem;
            targetEdit->AddRef();
            targetPreviousValue = currentValue;
        }

        editElem->Release();
    }

    if (!targetEdit) {
        // Ninguno tenia separador: usar el ultimo Edit de la lista.
        targetEdit = lastEdit;
        if (targetEdit) {
            targetEdit->AddRef();
        }
    }
    if (lastEdit) {
        lastEdit->Release();
        lastEdit = nullptr;
    }

    editCondition->Release();
    editArray->Release();

    if (!targetEdit) {
        qWarning() << "[UiaSwitcher] No se pudo elegir un Edit destino.";
        element->Release();
        automation->Release();
        return false;
    }

    // El path ya viene normalizado desde DialogSwitcher::switchDialog (barras invertidas y barra
    // final). No se re-normaliza aca.
    const QString &path = folder;

    // SetValue cambia el texto pero NO dispara la senal de edicion del widget, asi que el browser de
    // Nuke no se entera y no aplica nada. Una tecla real si la dispara. Por eso se escribe el path
    // menos su ultimo caracter y ese ultimo se tipea: queda el path exacto, con evento de edicion, y
    // sin Enter (cualquier "aceptar" commitearia la carpeta como si fuera el archivo).
    const bool typeLastChar = path.size() > 1;
    const QChar lastChar = typeLastChar ? path.at(path.size() - 1) : QChar();
    QString seed = path;
    if (typeLastChar) {
        seed.chop(1);
    }

    IUIAutomationValuePattern *targetValuePattern = nullptr;
    if (SUCCEEDED(targetEdit->GetCurrentPatternAs(UIA_ValuePatternId, IID_IUIAutomationValuePattern,
                                                    reinterpret_cast<void **>(&targetValuePattern))) &&
        targetValuePattern) {
        BSTR bstrPath = SysAllocString(reinterpret_cast<const wchar_t *>(seed.utf16()));
        HRESULT setHr = targetValuePattern->SetValue(bstrPath);
        if (bstrPath) SysFreeString(bstrPath);
        targetValuePattern->Release();

        if (SUCCEEDED(setHr)) {
            result = true;
            qDebug() << "[UiaSwitcher] Valor previo:" << targetPreviousValue
                      << "-> escrito:" << path;
        } else {
            qWarning() << "[UiaSwitcher] SetValue fallo, hr=" << setHr;
        }
    } else {
        qWarning() << "[UiaSwitcher] El Edit elegido no soporta ValuePattern.";
    }

    if (!result) {
        targetEdit->Release();
        element->Release();
        automation->Release();
        return false;
    }

    // Tipear el ultimo caracter con una tecla real, para que el widget emita su senal de edicion. Se
    // manda como Unicode (KEYEVENTF_UNICODE) para no depender de la distribucion de teclado: la
    // barra invertida esta en distinto lugar segun el layout.
    if (typeLastChar && SUCCEEDED(targetEdit->SetFocus())) {
        INPUT in[2] = {};
        in[0].type = INPUT_KEYBOARD;
        in[0].ki.wScan = static_cast<WORD>(lastChar.unicode());
        in[0].ki.dwFlags = KEYEVENTF_UNICODE;
        in[1] = in[0];
        in[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        const UINT sent = SendInput(2, in, sizeof(INPUT));
        qDebug() << "[UiaSwitcher] Ultimo caracter tipeado:" << lastChar
                 << (sent == 2 ? "ok" : "fallo");
    } else if (typeLastChar) {
        qWarning() << "[UiaSwitcher] No se pudo enfocar el campo; el path quedo"
                   << "escrito pero puede que el dialogo no lo aplique.";
    }

    targetEdit->Release();
    element->Release();
    automation->Release();
    return true;
}

} // namespace UiaSwitcher
