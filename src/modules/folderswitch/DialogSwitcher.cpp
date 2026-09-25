#include "modules/folderswitch/DialogSwitcher.h"

#include "modules/folderswitch/WindowUtils.h"
#include "modules/folderswitch/UiaSwitcher.h"

#include <QDebug>
#include <QDir>
#include <QList>
#include <QStringList>
#include <QTimer>

// Copia de LGA_FolderSwitch (src/core/DialogSwitcher.cpp), sin cambios de logica.

namespace {

QString classOf(HWND hwnd)
{
    wchar_t buf[128] = {0};
    GetClassNameW(hwnd, buf, static_cast<int>(std::size(buf)));
    return QString::fromWCharArray(buf);
}

struct Descendant {
    HWND hwnd;
    QString cls;
    int depth;
};

// Recorre TODO el arbol, no solo los hijos directos: segun la app, el combo del nombre de archivo
// cuelga de un contenedor intermedio y no del dialogo.
void collectDescendants(HWND parent, int depth, QList<Descendant> *out)
{
    if (depth > 6) {
        return;
    }
    struct Ctx { int depth; QList<Descendant> *out; } ctx{depth, out};
    EnumChildWindows(parent, [](HWND hwnd, LPARAM lp) -> BOOL {
        auto *c = reinterpret_cast<Ctx *>(lp);
        c->out->append({hwnd, classOf(hwnd), c->depth});
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
}

bool usableEdit(HWND hwnd)
{
    return IsWindowVisible(hwnd) && IsWindowEnabled(hwnd);
}

HWND findFileNameEdit(HWND dlg)
{
    // EnumChildWindows ya es recursivo sobre todo el arbol de descendientes.
    QList<Descendant> all;
    collectDescendants(dlg, 0, &all);

    // 1) Camino tipico: ComboBoxEx32 -> ComboBox -> Edit, este donde este.
    for (const Descendant &d : all) {
        if (d.cls.compare(QStringLiteral("ComboBoxEx32"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        HWND combo = FindWindowExW(d.hwnd, nullptr, L"ComboBox", nullptr);
        HWND edit = combo ? FindWindowExW(combo, nullptr, L"Edit", nullptr) : nullptr;
        if (edit && usableEdit(edit)) {
            return edit;
        }
    }

    // 2) Algunos dialogos usan ComboBox pelado, sin el wrapper Ex32.
    for (const Descendant &d : all) {
        if (d.cls.compare(QStringLiteral("ComboBox"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        HWND edit = FindWindowExW(d.hwnd, nullptr, L"Edit", nullptr);
        if (edit && usableEdit(edit)) {
            return edit;
        }
    }

    // 3) Ultimo recurso: el primer Edit util del arbol.
    for (const Descendant &d : all) {
        if (d.cls.compare(QStringLiteral("Edit"), Qt::CaseInsensitive) == 0 && usableEdit(d.hwnd)) {
            return d.hwnd;
        }
    }

    // Sin edit: volcamos el arbol para poder diagnosticar que dialogo es.
    QStringList seen;
    for (const Descendant &d : all) {
        if (!seen.contains(d.cls)) {
            seen.append(d.cls);
        }
    }
    qWarning() << "[DialogSwitcher] Sin edit. Clases del dialogo:" << seen.join(QStringLiteral(", "));
    return nullptr;
}

QString getEditText(HWND edit)
{
    const int len = static_cast<int>(SendMessageW(edit, WM_GETTEXTLENGTH, 0, 0));
    if (len <= 0) {
        return QString();
    }
    std::wstring buf(len + 1, L'\0');
    SendMessageW(edit, WM_GETTEXT, static_cast<WPARAM>(buf.size()),
                reinterpret_cast<LPARAM>(buf.data()));
    return QString::fromStdWString(buf.c_str());
}

void setEditText(HWND edit, const QString &text)
{
    const std::wstring w = text.toStdWString();
    SendMessageW(edit, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(w.c_str()));
}

bool looksLikeAbsolutePath(const QString &s)
{
    return s.contains(QStringLiteral(":\\")) || s.startsWith(QStringLiteral("\\\\"));
}

} // namespace

namespace DialogSwitcher {

bool switchDialog(HWND dlg, const QString &folder)
{
    if (!dlg || folder.isEmpty()) {
        return false;
    }

    // Los dialogos esperan el path como lo escribe Windows: barras invertidas y barra final (sin la
    // final, algunos tratan el texto como nombre de archivo en vez de navegar a la carpeta). Se
    // normaliza una sola vez, aca, para que valga igual en el camino Win32 y en el de UI Automation.
    QString path = QDir::toNativeSeparators(folder);
    if (!path.endsWith(QChar::fromLatin1('\\'))) {
        path += QChar::fromLatin1('\\');
    }

    // Un solo punto de decision: dialogos Qt puro (sin hijos Win32, p.ej. Nuke) van por UI
    // Automation; el resto sigue el camino Win32 clasico de abajo.
    if (WindowUtils::isQtFileDialog(dlg)) {
        return UiaSwitcher::switchQtDialog(dlg, path);
    }

    HWND edit = findFileNameEdit(dlg);
    if (!edit) {
        return false;   // findFileNameEdit ya logueo el arbol de clases
    }

    const QString previousText = getEditText(edit);
    const QString &nativePath = path;

    setEditText(edit, nativePath);

    HWND okButton = GetDlgItem(dlg, IDOK);
    SendMessageW(dlg, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED),
                reinterpret_cast<LPARAM>(okButton));

    HWND editCapture = edit;
    QString restoreText = previousText;
    QTimer::singleShot(150, [editCapture, restoreText]() {
        if (!IsWindow(editCapture)) {
            return;
        }
        if (restoreText.isEmpty()) {
            setEditText(editCapture, QString());
        } else if (!looksLikeAbsolutePath(restoreText)) {
            setEditText(editCapture, restoreText);
        }
        // Si restoreText era un path absoluto, dejamos el que quedo (ya navegado).
    });

    qDebug() << "[DialogSwitcher] Inyectado:" << nativePath;
    return true;
}

} // namespace DialogSwitcher
