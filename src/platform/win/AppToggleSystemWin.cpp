#include "platform/AppToggleSystem.h"

#include "core/AutomatedRun.h"
#include "core/I18n.h"
#include "platform/WindowActivation.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QPixmap>
#include <QVarLengthArray>

#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <tlhelp32.h>

namespace {

using AppToggleSystem::Window;

QString imagePath(DWORD pid)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) {
        return {};
    }
    QVarLengthArray<wchar_t, 512> buffer(32768);
    DWORD length = DWORD(buffer.size());
    const bool ok = QueryFullProcessImageNameW(process, 0, buffer.data(), &length);
    CloseHandle(process);
    return ok ? QString::fromWCharArray(buffer.data(), int(length)) : QString();
}

struct Collected
{
    QHash<DWORD, QString> processes;
    QList<Window> windows;
};

BOOL CALLBACK collectWindow(HWND hwnd, LPARAM parameter)
{
    auto *collected = reinterpret_cast<Collected *>(parameter);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    const auto process = collected->processes.constFind(pid);
    if (process == collected->processes.constEnd()) {
        return TRUE;
    }
    const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    DWORD cloaked = 0;
    wchar_t className[128] = {};
    GetClassNameW(hwnd, className, 128);
    Window window;
    window.id = quintptr(hwnd);
    window.processPath = *process;
    window.visible = IsWindowVisible(hwnd);
    window.minimized = IsIconic(hwnd);
    window.owned = GetWindow(hwnd, GW_OWNER) != nullptr;
    window.toolWindow = exStyle & WS_EX_TOOLWINDOW;
    window.appWindow = exStyle & WS_EX_APPWINDOW;
    window.cloaked = SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked;
    window.className = QString::fromWCharArray(className);
    collected->windows.append(window);
    return TRUE;
}

void fail(QString *error, const QString &message)
{
    if (error) *error = message;
}

bool launch(const AppToggleSystem::Target &target, QString *error)
{
    const QFileInfo exe(target.path);
    if (!exe.isFile()) {
        fail(error, I18n::tr("The app was not found: %1").arg(QDir::toNativeSeparators(target.path)));
        return false;
    }
    const QString file = QDir::toNativeSeparators(exe.absoluteFilePath());
    const QString directory = QDir::toNativeSeparators(target.workingDir.trimmed().isEmpty() ? exe.absolutePath()
                                                                                             : target.workingDir.trimmed());
    const QString arguments = target.arguments.trimmed();
    // Desde el gancho de teclado este proceso no puede pasar el frente: se lo toma y se lo cede a la app
    // que se abre, para que su ventana salga adelante y no titilando en la barra de tareas.
    WindowActivation::claimForegroundRight();
    WindowActivation::allowAnyProcessToActivate();
    // ShellExecuteEx como el Run del AHK: sin elevar desde aca; si el exe pide administrador en su
    // manifiesto, Windows muestra su cartel. Sin carteles de error propios de Windows (FLAG_NO_UI).
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
    info.lpVerb = L"open";
    info.lpFile = reinterpret_cast<LPCWSTR>(file.utf16());
    info.lpParameters = arguments.isEmpty() ? nullptr : reinterpret_cast<LPCWSTR>(arguments.utf16());
    info.lpDirectory = reinterpret_cast<LPCWSTR>(directory.utf16());
    info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info)) {
        const DWORD code = GetLastError();
        fail(error, code == ERROR_CANCELLED ? I18n::tr("The app was not opened.")
                    : code == ERROR_DIRECTORY || code == ERROR_PATH_NOT_FOUND
                        ? I18n::tr("The start folder was not found: %1").arg(directory)
                        : I18n::tr("Could not open the app (Windows error %1).").arg(code));
        return false;
    }
    return true;
}

} // namespace

namespace AppToggleSystem {

Snapshot snapshot(const QString &appPath)
{
    Snapshot snapshot;
    const QString fileName = QFileInfo(QDir::fromNativeSeparators(appPath.trimmed())).fileName();
    if (fileName.isEmpty()) {
        return snapshot;
    }
    // Primero por el nombre del exe (barato) y solo a esos se les pide la ruta completa.
    Collected collected;
    HANDLE processes = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (processes != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        for (BOOL more = Process32FirstW(processes, &entry); more; more = Process32NextW(processes, &entry)) {
            if (QString::fromWCharArray(entry.szExeFile).compare(fileName, Qt::CaseInsensitive) != 0) {
                continue;
            }
            const QString path = imagePath(entry.th32ProcessID);
            if (!path.isEmpty()) {
                collected.processes.insert(entry.th32ProcessID, path);
                snapshot.processPaths << path;
            }
        }
        CloseHandle(processes);
    }
    // Una corrida automatizada (--simulate-action por el runner de QA) vive en un escritorio aparte, donde
    // EnumWindows y GetForegroundWindow no ven las ventanas del usuario: ahi se leen las del escritorio que
    // recibe la entrada, solo con permiso de lectura. La app lee siempre el suyo.
    HDESK input = AutomatedRun::active() ? OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS) : nullptr;
    if (!collected.processes.isEmpty()) {
        if (input) EnumDesktopWindows(input, &collectWindow, reinterpret_cast<LPARAM>(&collected));
        else EnumWindows(&collectWindow, reinterpret_cast<LPARAM>(&collected));
    }
    snapshot.windows = collected.windows;
    HWND front = GetForegroundWindow();
    GUITHREADINFO thread{};
    thread.cbSize = sizeof(thread);
    if (!front && input && GetGUIThreadInfo(0, &thread)) front = thread.hwndActive;
    if (input) CloseDesktop(input);
    if (front) {
        HWND root = GetAncestor(front, GA_ROOTOWNER);
        if (!root) root = front;
        DWORD pid = 0;
        GetWindowThreadProcessId(root, &pid);
        snapshot.foreground = quintptr(root);
        snapshot.foregroundPath = collected.processes.value(pid);
    }
    return snapshot;
}

bool run(const Decision &decision, const Target &target, QString *error)
{
    if (error) error->clear();
    if (decision.action == Action::Launch) {
        return launch(target, error);
    }
    if (decision.action == Action::Nothing) {
        return true;
    }
    HWND window = reinterpret_cast<HWND>(decision.window);
    if (!IsWindow(window)) {
        fail(error, I18n::tr("The app's window closed before it could be shown."));
        return false;
    }
    // Una app colgada no contesta: ShowWindow esperaria y trabaria la ventana de esta app.
    if (IsHungAppWindow(window)) {
        fail(error, I18n::tr("The app is not responding."));
        return false;
    }
    if (decision.action == Action::Minimize) {
        ShowWindow(window, SW_MINIMIZE);
        if (!IsIconic(window)) {
            fail(error, I18n::tr("Could not minimize the app."));
            return false;
        }
        return true;
    }
    if (!WindowActivation::bringToFront(decision.window)) {
        fail(error, I18n::tr("Windows did not let the app come to the front."));
        return false;
    }
    return true;
}

QIcon icon(const QString &appPath)
{
    if (!QFileInfo(appPath).isFile()) {
        return {};
    }
    const std::wstring file = QDir::toNativeSeparators(appPath).toStdWString();
    QIcon result;
    for (const UINT size : {UINT(SHGFI_LARGEICON), UINT(SHGFI_SMALLICON)}) {
        SHFILEINFOW info{};
        if (SHGetFileInfoW(file.c_str(), 0, &info, sizeof(info), SHGFI_ICON | size) && info.hIcon) {
            const QImage image = QImage::fromHICON(info.hIcon);
            DestroyIcon(info.hIcon);
            if (!image.isNull()) result.addPixmap(QPixmap::fromImage(image));
        }
    }
    return result;
}

QString productName(const QString &appPath)
{
    const std::wstring file = QDir::toNativeSeparators(appPath).toStdWString();
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(file.c_str(), &handle);
    if (!size) {
        return {};
    }
    QByteArray data(int(size), Qt::Uninitialized);
    if (!GetFileVersionInfoW(file.c_str(), 0, size, data.data())) {
        return {};
    }
    struct Translation
    {
        WORD language;
        WORD codePage;
    };
    Translation *translations = nullptr;
    UINT length = 0;
    QList<QString> blocks;
    if (VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void **>(&translations), &length)) {
        for (UINT i = 0; i < length / sizeof(Translation); ++i) {
            blocks << QStringLiteral("%1%2").arg(translations[i].language, 4, 16, QLatin1Char('0'))
                                            .arg(translations[i].codePage, 4, 16, QLatin1Char('0'));
        }
    }
    blocks << QStringLiteral("040904b0") << QStringLiteral("040904e4");
    // La descripcion es el nombre que muestra el Administrador de tareas ("OBS Studio"); el de producto
    // de algunas apps de Windows es "Microsoft Windows Operating System".
    for (const char *field : {"FileDescription", "ProductName"}) {
        for (const QString &block : blocks) {
            const std::wstring query = QStringLiteral("\\StringFileInfo\\%1\\%2").arg(block, QLatin1String(field)).toStdWString();
            wchar_t *value = nullptr;
            UINT chars = 0;
            if (VerQueryValueW(data.data(), query.c_str(), reinterpret_cast<void **>(&value), &chars) && value && chars > 1) {
                const QString text = QString::fromWCharArray(value).trimmed();
                if (!text.isEmpty()) return text;
            }
        }
    }
    return {};
}

} // namespace AppToggleSystem
