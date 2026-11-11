#include "platform/WhatsAppSystem.h"

#include "core/AutomatedRun.h"
#include "core/I18n.h"

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QPair>
#include <QSet>

#include <iterator>

#include <windows.h>
#include <propsys.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <tlhelp32.h>

// Windows:
//  - la app: la de la Store registra el protocolo `whatsapp:`; se pregunta quien lo atiende ahora
//    (IApplicationAssociationRegistration) y, si nadie, AssocQueryString. Abierta = corre WhatsApp.exe o
//    WhatsApp.Root.exe;
//  - las apps web: los accesos directos del menu Inicio y del escritorio con WhatsApp en el nombre que
//    lanzan `<navegador>_proxy.exe ... --app-id=<id>`. Abierta = una ventana cuyo AppUserModelID lleva ese
//    id (Chromium le pone uno propio a cada app web).

namespace {

// PKEY_AppUserModel_ID, escrita aca para no depender de la libreria de propkey.
const PROPERTYKEY kAppUserModelId = {{0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}}, 5};

QString knownFolder(REFKNOWNFOLDERID id)
{
    PWSTR raw = nullptr;
    QString path;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &raw)) && raw) {
        path = QDir::fromNativeSeparators(QString::fromWCharArray(raw));
    }
    CoTaskMemFree(raw);
    return path;
}

bool protocolRegistered(QString *name)
{
    bool found = false;
    IApplicationAssociationRegistration *registration = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_ApplicationAssociationRegistration, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&registration)))) {
        LPWSTR progId = nullptr;
        if (SUCCEEDED(registration->QueryCurrentDefault(L"whatsapp", AT_URLPROTOCOL, AL_EFFECTIVE, &progId)) && progId) {
            found = progId[0] != 0;
            CoTaskMemFree(progId);
        }
        registration->Release();
    }
    wchar_t buffer[512] = {};
    DWORD length = DWORD(std::size(buffer));
    if (SUCCEEDED(AssocQueryStringW(ASSOCF_IS_PROTOCOL, ASSOCSTR_FRIENDLYAPPNAME, L"whatsapp", nullptr, buffer, &length))) {
        found = true;
        if (name) *name = QString::fromWCharArray(buffer);
    }
    return found;
}

QSet<QString> runningExeNames()
{
    QSet<QString> names;
    HANDLE processes = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (processes == INVALID_HANDLE_VALUE) return names;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Process32FirstW(processes, &entry); more; more = Process32NextW(processes, &entry)) {
        names.insert(QString::fromWCharArray(entry.szExeFile).toLower());
    }
    CloseHandle(processes);
    return names;
}

struct WindowIds
{
    QStringList aumids;
};

BOOL CALLBACK collectAumid(HWND hwnd, LPARAM param)
{
    if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) return TRUE;
    IPropertyStore *store = nullptr;
    if (SUCCEEDED(SHGetPropertyStoreForWindow(hwnd, IID_PPV_ARGS(&store)))) {
        PROPVARIANT value;
        PropVariantInit(&value);
        if (SUCCEEDED(store->GetValue(kAppUserModelId, &value)) && value.vt == VT_LPWSTR && value.pwszVal) {
            reinterpret_cast<WindowIds *>(param)->aumids.append(QString::fromWCharArray(value.pwszVal));
        }
        PropVariantClear(&value);
        store->Release();
    }
    return TRUE;
}

QString browserFor(const QString &exe)
{
    const QString name = QFileInfo(exe).completeBaseName().toLower();
    if (name.startsWith(QLatin1String("brave"))) return QStringLiteral("Brave");
    if (name.startsWith(QLatin1String("msedge"))) return QStringLiteral("Edge");
    if (name.startsWith(QLatin1String("chrome"))) return QStringLiteral("Chrome");
    if (name.startsWith(QLatin1String("vivaldi"))) return QStringLiteral("Vivaldi");
    return QFileInfo(exe).completeBaseName();
}

QString argument(const QString &arguments, const QString &name)
{
    const QRegularExpression pattern(QStringLiteral("--%1=(\"[^\"]*\"|\\S+)").arg(QRegularExpression::escape(name)));
    QString value = pattern.match(arguments).captured(1);
    if (value.startsWith(QLatin1Char('"'))) value = value.mid(1).chopped(1);
    return value;
}

bool readShortcut(const QString &file, QString *target, QString *arguments)
{
    IShellLinkW *link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) return false;
    bool ok = false;
    IPersistFile *persist = nullptr;
    if (SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&persist)))) {
        const std::wstring path = QDir::toNativeSeparators(file).toStdWString();
        if (SUCCEEDED(persist->Load(path.c_str(), STGM_READ))) {
            wchar_t buffer[MAX_PATH * 2] = {};
            if (SUCCEEDED(link->GetPath(buffer, int(std::size(buffer)), nullptr, 0))) {
                *target = QString::fromWCharArray(buffer);
            }
            wchar_t args[4096] = {};
            if (SUCCEEDED(link->GetArguments(args, int(std::size(args))))) {
                *arguments = QString::fromWCharArray(args);
            }
            ok = !target->isEmpty();
        }
        persist->Release();
    }
    link->Release();
    return ok;
}

} // namespace

namespace WhatsAppSystem {

QList<Target> detect()
{
    QList<Target> targets;
    const QSet<QString> running = runningExeNames();
    QString appName;
    if (protocolRegistered(&appName)) {
        Target app;
        app.kind = Kind::App;
        app.name = appName.isEmpty() ? QStringLiteral("WhatsApp") : appName;
        app.running = running.contains(QStringLiteral("whatsapp.exe")) || running.contains(QStringLiteral("whatsapp.root.exe"));
        targets.append(app);
    }

    WindowIds windows;
    bool windowsRead = false;
    QSet<QString> seen;
    // El menu Inicio con sus carpetas («Chrome Apps», «Brave Apps»); el escritorio solo arriba: Chromium deja ahi el
    // acceso directo suelto, y recorrer las carpetas de trabajo del escritorio trabaria la ventana.
    const QList<QPair<QString, bool>> roots{{knownFolder(FOLDERID_Programs), true},
                                            {knownFolder(FOLDERID_CommonPrograms), true},
                                            {knownFolder(FOLDERID_Desktop), false}};
    for (const auto &[root, recursive] : roots) {
        if (root.isEmpty()) continue;
        QDirIterator it(root, {QStringLiteral("*.lnk")}, QDir::Files,
                        recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
        while (it.hasNext()) {
            const QString file = it.next();
            if (!QFileInfo(file).completeBaseName().contains(QLatin1String("whatsapp"), Qt::CaseInsensitive)) continue;
            QString exe;
            QString arguments;
            if (!readShortcut(file, &exe, &arguments)) continue;
            const QString appId = argument(arguments, QStringLiteral("app-id"));
            if (appId.isEmpty()) continue;
            const QString profile = argument(arguments, QStringLiteral("profile-directory"));
            const QString key = appId + QLatin1Char('|') + profile;
            if (seen.contains(key)) continue;
            seen.insert(key);
            Target web;
            web.kind = Kind::WebApp;
            web.name = QFileInfo(file).completeBaseName();
            web.browser = browserFor(exe);
            web.path = QDir::toNativeSeparators(file);
            web.launcher = exe;
            web.appId = appId;
            web.profile = profile;
            if (!windowsRead) {
                EnumWindows(&collectAumid, reinterpret_cast<LPARAM>(&windows));
                windowsRead = true;
            }
            for (const QString &aumid : windows.aumids) {
                if (aumid.contains(appId, Qt::CaseInsensitive)) web.running = true;
            }
            targets.append(web);
        }
    }
    return targets;
}

bool open(const Target &target, const QString &digits, QString *error)
{
    if (error) error->clear();
    // Segunda barrera (la primera es el modulo): una corrida automatizada nunca abre nada.
    if (AutomatedRun::active()) {
        qInfo().noquote() << "[whatsApp] Corrida automatizada, no se abre:" << describe(target);
        return true;
    }
    // La ventana de Mighty Tools tiene el frente: se lo cede a lo que se abre.
    AllowSetForegroundWindow(ASFW_ANY);
    if (target.kind == Kind::WebApp && !target.launcher.isEmpty() && !target.appId.isEmpty()) {
        QStringList args;
        if (!target.profile.isEmpty()) args << QStringLiteral("--profile-directory=%1").arg(target.profile);
        args << QStringLiteral("--app-id=%1").arg(target.appId)
             << QStringLiteral("--app-launch-url-for-shortcuts-menu-item=%1").arg(webUrl(digits));
        if (QProcess::startDetached(target.launcher, args)) return true;
        if (error) *error = I18n::tr("%1 could not be opened.").arg(describe(target));
        return false;
    }
    const QString url = target.kind == Kind::App ? appUrl(digits) : webUrl(digits);
    const std::wstring wide = url.toStdWString();
    // Sin carteles de Windows: un `whatsapp:` registrado que apunta a un exe borrado vuelve como error y va a la nota.
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_FLAG_NO_UI;
    info.lpVerb = L"open";
    info.lpFile = wide.c_str();
    info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info)) {
        if (error) *error = target.kind == Kind::App ? I18n::tr("WhatsApp could not be opened.") : I18n::tr("The browser could not be opened.");
        return false;
    }
    return true;
}

} // namespace WhatsAppSystem
