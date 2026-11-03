#include "platform/AppToggleSystem.h"

#include "core/AutomatedRun.h"
#include "core/I18n.h"
#include "platform/WindowActivation.h"
#include "platform/win/RegistryHelper.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QPixmap>
#include <QRegularExpression>
#include <QVarLengthArray>

#include <algorithm>

#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shobjidl.h>
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
    bool folderWindowsOnly = false; ///< el Explorador: ni el escritorio ni la barra de tareas
};

// Las ventanas de carpetas del Explorador (CabinetWClass; ExploreWClass en Windows viejos).
bool folderWindowClass(const wchar_t *className)
{
    return wcscmp(className, L"CabinetWClass") == 0 || wcscmp(className, L"ExploreWClass") == 0;
}

// La primera ruta que existe.
QString firstExisting(const QStringList &candidates)
{
    for (const QString &candidate : candidates) {
        const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(candidate));
        if (!candidate.isEmpty() && QFileInfo(clean).isFile()) return QDir::toNativeSeparators(clean);
    }
    return {};
}

QString programFiles()
{
    return qEnvironmentVariable("ProgramW6432", qEnvironmentVariable("ProgramFiles", QStringLiteral("C:/Program Files")));
}

// XYplorer: su desinstalador anota la carpeta (XYplorer64 o XYplorer, en la maquina o en el usuario); si no,
// las carpetas de siempre. Una copia portable en otro lado no se encuentra. Su instalador es de 32 bits: en la
// maquina la clave queda bajo WOW6432Node, que esta app (64 bits) no ve sin pedirla.
QString xyplorerPath()
{
    QStringList candidates;
    const QString uninstall = QStringLiteral("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\");
    const QString uninstall32 = QStringLiteral("SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\");
    const QList<QPair<HKEY, QString>> places = {{HKEY_CURRENT_USER, uninstall}, {HKEY_LOCAL_MACHINE, uninstall},
                                                {HKEY_LOCAL_MACHINE, uninstall32}};
    for (const auto &place : places) {
        for (const char *key : {"XYplorer64", "XYplorer"}) {
            const QString folder = RegistryHelper::readString(place.first, place.second + QLatin1String(key),
                                                              QStringLiteral("InstallLocation"));
            if (!folder.isEmpty()) candidates << folder + QStringLiteral("/XYplorer.exe");
        }
    }
    candidates << programFiles() + QStringLiteral("/XYplorer/XYplorer.exe")
               << qEnvironmentVariable("ProgramFiles(x86)") + QStringLiteral("/XYplorer/XYplorer.exe");
    return firstExisting(candidates);
}

// OBS anota su carpeta en HKLM\SOFTWARE\OBS Studio. Si no esta instalado, la ruta de siempre: es el ejemplo.
QString obsPath()
{
    const QString folder = RegistryHelper::readString(HKEY_LOCAL_MACHINE, QStringLiteral("SOFTWARE\\OBS Studio"));
    const QString standard = programFiles() + QStringLiteral("/obs-studio/bin/64bit/obs64.exe");
    const QString found = firstExisting({folder.isEmpty() ? QString() : folder + QStringLiteral("/bin/64bit/obs64.exe"), standard});
    return found.isEmpty() ? QDir::toNativeSeparators(standard) : found;
}

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
    if (collected->folderWindowsOnly && !folderWindowClass(className)) {
        return TRUE;
    }
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

// El bitmap del shell (IShellItemImageFactory) a QImage. QImage::fromHBITMAP lo da vuelta y pierde el
// alfa: se pide con GetDIBits de arriba hacia abajo, que es alfa premultiplicado. Un icono viejo sin canal
// alfa llega con alfa 0 en todo: ese se toma opaco.
QImage imageFromShellBitmap(HBITMAP bitmap)
{
    BITMAP info{};
    if (!GetObjectW(bitmap, sizeof(info), &info) || info.bmWidth <= 0 || info.bmHeight == 0) {
        return {};
    }
    const int width = info.bmWidth;
    const int height = qAbs(info.bmHeight);
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    BITMAPINFO header{};
    header.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    header.bmiHeader.biWidth = width;
    header.bmiHeader.biHeight = -height;
    header.bmiHeader.biPlanes = 1;
    header.bmiHeader.biBitCount = 32;
    header.bmiHeader.biCompression = BI_RGB;
    HDC screen = GetDC(nullptr);
    const int lines = GetDIBits(screen, bitmap, 0, UINT(height), image.bits(), &header, DIB_RGB_COLORS);
    ReleaseDC(nullptr, screen);
    if (lines != height) {
        return {};
    }
    // Premultiplicado, ningun canal pasa al alfa. Si alguno pasa, el alfa viene comun (sin premultiplicar):
    // tomarlo como premultiplicado deja puntos de color saturado en los bordes.
    bool anyAlpha = false;
    bool straight = false;
    for (int y = 0; y < height; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < width; ++x) {
            const int alpha = qAlpha(line[x]);
            anyAlpha = anyAlpha || alpha != 0;
            straight = straight || qRed(line[x]) > alpha || qGreen(line[x]) > alpha || qBlue(line[x]) > alpha;
        }
    }
    if (!anyAlpha) {
        image.reinterpretAsFormat(QImage::Format_RGB32);
    } else if (straight) {
        image.reinterpretAsFormat(QImage::Format_ARGB32);
    }
    return image;
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
    snapshot.shell = samePath(appPath, explorerPath());
    collected.folderWindowsOnly = snapshot.shell;
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
    // Segunda barrera (la primera es el modulo): una corrida automatizada nunca abre, activa ni minimiza.
    if (AutomatedRun::active()) {
        qInfo().noquote() << "[appToggle] Corrida automatizada, no se ejecuta:" << describe(decision);
        return true;
    }
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
        // Las apps que al minimizarse se van a la bandeja (OBS con esa opcion) esconden la ventana: tambien vale.
        if (!IsIconic(window) && IsWindowVisible(window)) {
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
    // Cada tamano pedido al shell, que elige el cuadro del .ico que mejor le queda: la baldosa (20 px) sale
    // nitida al 100, 150, 200 y 300 % sin estirar un icono de 32. COM ya esta iniciado en main.
    IShellItemImageFactory *factory = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(file.c_str(), nullptr, IID_PPV_ARGS(&factory)))) {
        for (const int size : {16, 20, 24, 30, 32, 40, 48, 60, 64, 80, 96, 128}) {
            HBITMAP bitmap = nullptr;
            if (SUCCEEDED(factory->GetImage(SIZE{size, size}, SIIGBF_ICONONLY, &bitmap)) && bitmap) {
                const QImage image = imageFromShellBitmap(bitmap);
                DeleteObject(bitmap);
                if (!image.isNull()) result.addPixmap(QPixmap::fromImage(image));
            }
        }
        factory->Release();
    }
    if (!result.isNull()) {
        return result;
    }
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

QString explorerPath()
{
    wchar_t folder[MAX_PATH] = {};
    const UINT length = GetWindowsDirectoryW(folder, MAX_PATH);
    const QString windows = length && length < MAX_PATH ? QString::fromWCharArray(folder, int(length)) : QStringLiteral("C:\\Windows");
    return QDir::toNativeSeparators(windows + QStringLiteral("/explorer.exe"));
}

QString photoshopPath()
{
    // La del ano mas nuevo que tenga el exe: "Adobe Photoshop 2026" antes que "Adobe Photoshop CC 2019".
    const QDir adobe(programFiles() + QStringLiteral("/Adobe"));
    QStringList folders = adobe.entryList({QStringLiteral("Adobe Photoshop*")}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    const auto year = [](const QString &name) {
        const QRegularExpressionMatch match = QRegularExpression(QStringLiteral("(\\d{4})")).match(name);
        return match.hasMatch() ? match.captured(1).toInt() : 0;
    };
    std::stable_sort(folders.begin(), folders.end(), [&](const QString &a, const QString &b) { return year(a) > year(b); });
    QStringList candidates;
    for (const QString &folder : folders) candidates << adobe.filePath(folder + QStringLiteral("/Photoshop.exe"));
    return firstExisting(candidates);
}

QList<Suggestion> suggestedApps()
{
    const QString xyplorer = xyplorerPath();
    const QString winE = QStringLiteral("Meta+E");
    // El nombre es un dato que el usuario puede cambiar: se siembra en el idioma que ya eligio.
    return {xyplorer.isEmpty() ? Suggestion{I18n::tr("File Explorer"), explorerPath(), winE}
                               : Suggestion{QStringLiteral("XYplorer"), xyplorer, winE},
            Suggestion{QStringLiteral("OBS Studio"), obsPath(), QStringLiteral("Meta+Ctrl+Alt+Shift+1")}};
}

} // namespace AppToggleSystem
