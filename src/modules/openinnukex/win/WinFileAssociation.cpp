#include "modules/openinnukex/win/WinFileAssociation.h"
#include "modules/openinnukex/win/UserChoiceLatest.h"

#include "platform/win/RegistryHelper.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QThread>
#include <QtGlobal>

#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>

namespace {

constexpr wchar_t kProgIdW[] = L"LGA.NukeScript.1";
constexpr wchar_t kExtensionW[] = L".nk";
// Nombre de la app en el registro (Capabilities / RegisteredApplications), distinto del
// "OpenInNukeX" del cliente viejo: esta es LGA Mighty Tools.
constexpr wchar_t kAppRegKey[] = L"LGA_MightyTools";
constexpr wchar_t kCapabilitiesPath[] = L"Software\\LGA_MightyTools\\Capabilities";
constexpr wchar_t kRegisteredAppsPath[] = L"Software\\RegisteredApplications";

// Clave de desinstalacion del cliente viejo (Inno Setup, AppId del plan seccion 2/9).
constexpr wchar_t kOldClientUninstallKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\"
    L"{B8F1A2C3-4D5E-6F78-9A0B-1C2D3E4F5678}_is1";

std::wstring toW(const QString &value)
{
    return value.toStdWString();
}

void notifyAssociationChanged()
{
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

// ─── Registro de la app ──────────────────────────────────────────────────────

bool registerProgId()
{
    const QString exePath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    const QString progIdValue = QString::fromWCharArray(kProgIdW);

    bool ok = true;
    ok &= RegistryHelper::writeString(HKEY_CURRENT_USER, QStringLiteral("Software\\Classes\\%1").arg(progIdValue),
                                      QString(), QStringLiteral("Nuke Script File"));
    ok &= RegistryHelper::writeString(HKEY_CURRENT_USER,
                                      QStringLiteral("Software\\Classes\\%1\\shell\\open\\command").arg(progIdValue),
                                      QString(), QStringLiteral("\"%1\" \"%2\"").arg(exePath, QStringLiteral("%1")));
    return ok;
}

bool registerExtensionClass(const QString &extension, const QString &progIdValue)
{
    return RegistryHelper::writeString(HKEY_CURRENT_USER, QStringLiteral("Software\\Classes\\%1").arg(extension),
                                       QString(), progIdValue);
}

bool registerDefaultAppCapabilities()
{
    const QString exePath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    const QString iconRef = QStringLiteral("\"%1\",0").arg(exePath);
    const QString progIdValue = QString::fromWCharArray(kProgIdW);
    const QString appKey = QString::fromWCharArray(kAppRegKey);
    const QString capsPath = QString::fromWCharArray(kCapabilitiesPath);

    bool ok = true;
    ok &= RegistryHelper::writeString(HKEY_CURRENT_USER, capsPath, QStringLiteral("ApplicationName"),
                                      QStringLiteral("LGA Mighty Tools"));
    ok &= RegistryHelper::writeString(HKEY_CURRENT_USER, capsPath, QStringLiteral("ApplicationDescription"),
                                      QStringLiteral("Opens Nuke scripts (.nk) with your preferred NukeX."));
    ok &= RegistryHelper::writeString(HKEY_CURRENT_USER, capsPath, QStringLiteral("ApplicationIcon"), iconRef);
    ok &= RegistryHelper::writeString(HKEY_CURRENT_USER, capsPath + QStringLiteral("\\FileAssociations"),
                                      QString::fromWCharArray(kExtensionW), progIdValue);
    ok &= RegistryHelper::writeString(HKEY_CURRENT_USER, QString::fromWCharArray(kRegisteredAppsPath), appKey, capsPath);
    return ok;
}

/// Borra un VALOR de una clave sin borrar la clave entera (RegistryHelper solo tiene deleteTree,
/// que se lleva puesta toda la clave — RegisteredApplications es compartida por todo Windows).
bool deleteValue(HKEY root, const QString &subKey, const QString &valueName)
{
    const std::wstring sub = toW(subKey);
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, sub.c_str(), 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
        return true; // la clave no existe: nada que borrar, no es un error
    }
    const std::wstring val = toW(valueName);
    const LONG rc = RegDeleteValueW(key, val.c_str());
    RegCloseKey(key);
    return rc == ERROR_SUCCESS || rc == ERROR_FILE_NOT_FOUND;
}

bool cleanConflictingKeys()
{
    const QStringList keys = {
        QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\.nk\\UserChoice"),
        QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\.nk\\UserChoiceLatest"),
    };
    bool ok = true;
    for (const QString &key : keys) {
        ok &= RegistryHelper::deleteTree(HKEY_CURRENT_USER, key);
    }
    return ok;
}

// ─── Fallbacks de UI del sistema ───────────────────────────────────────────

// IOpenWithLauncher: el mismo IID a mano que usaba el cliente v1.83 (MinGW ignora __declspec(uuid)).
struct IOpenWithLauncher : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE Launch(HWND hWndParent, const wchar_t *lpszPath, int flags) = 0;
};

bool launchOpenWithPicker(HWND parentHwnd)
{
    const HRESULT comRc = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comOwned = SUCCEEDED(comRc);

    bool launched = false;
    CLSID clsid{};
    const IID kIID_IOpenWithLauncher = {
        0x6a283fe2, 0xecfa, 0x4599, {0x91, 0xc4, 0xe8, 0x09, 0x57, 0x13, 0x7b, 0x26}};
    if (SUCCEEDED(CLSIDFromString(L"{e44e9428-bdbc-4987-a099-40dc8fd255e7}", &clsid))) {
        IOpenWithLauncher *launcher = nullptr;
        if (SUCCEEDED(CoCreateInstance(clsid, nullptr, CLSCTX_LOCAL_SERVER, kIID_IOpenWithLauncher,
                                       reinterpret_cast<void **>(&launcher)))) {
            CoAllowSetForegroundWindow(launcher, nullptr);
            // v1.83 pasaba winId() de su ventana; se lo devolvimos (auditoria) para que el picker
            // salga dueño de la ventana de LGA Mighty Tools, no huerfano.
            const HRESULT hr = launcher->Launch(parentHwnd, kExtensionW, 0x2004);
            launched = SUCCEEDED(hr) || hr == HRESULT_FROM_WIN32(ERROR_CANCELLED);
            launcher->Release();
        }
    }
    if (comOwned) {
        CoUninitialize();
    }
    return launched;
}

bool openDefaultAppsSettings()
{
    const QString deepLink = QStringLiteral("ms-settings:defaultapps?registeredAppUser=%1")
                                  .arg(QString::fromWCharArray(kAppRegKey));
    const std::wstring url = deepLink.toStdWString();
    const HINSTANCE rc = ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<intptr_t>(rc) > 32) {
        return true;
    }
    const HINSTANCE fallback = ShellExecuteW(nullptr, L"open", L"ms-settings:defaultapps", nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<intptr_t>(fallback) > 32;
}

} // namespace

namespace WinFileAssociation {

QString progId()
{
    return QString::fromWCharArray(kProgIdW);
}

QString currentNkProgId()
{
    // UserChoiceLatest manda si tiene un ProgId propio; si no, se cae al UserChoice legado. Sin
    // depender de si el hash "esta activo": esa lectura (HashVersion) vive del lado de
    // UserChoiceLatest.h (ver trySetUserChoiceLatestHash), no aca.
    // Windows guarda el ProgId de UserChoiceLatest en la SUBCLAVE UserChoiceLatest\ProgId (valor
    // ProgId), no en UserChoiceLatest directamente.
    const QString latestPath = QStringLiteral(
        "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\.nk\\UserChoiceLatest\\ProgId");
    const QString latestProgId = RegistryHelper::readString(HKEY_CURRENT_USER, latestPath, QStringLiteral("ProgId"));
    if (!latestProgId.isEmpty()) {
        return latestProgId;
    }

    const QString legacyPath = QStringLiteral(
        "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\.nk\\UserChoice");
    const QString legacyProgId = RegistryHelper::readString(HKEY_CURRENT_USER, legacyPath, QStringLiteral("ProgId"));
    if (!legacyProgId.isEmpty()) {
        return legacyProgId;
    }

    // Sin ninguna eleccion del usuario, el Explorador usa la clase de la extension: el doble click
    // ya abre con ese ProgId.
    return RegistryHelper::readString(HKEY_CURRENT_USER, QStringLiteral("Software\\Classes\\.nk"), QString());
}

bool isNkAssociatedWithUs()
{
    if (currentNkProgId().compare(progId(), Qt::CaseInsensitive) != 0) {
        return false;
    }
    // Plan 4.6: "el ProgID MAS la ruta del comando igual a este exe" — un ProgID compartido con
    // una copia instalada en otro lado (o con el cliente viejo, que tambien podria reescribirlo)
    // no cuenta como "asociado con nosotros" si el comando no apunta a ESTE binario.
    const QString command = RegistryHelper::readString(
        HKEY_CURRENT_USER, QStringLiteral("Software\\Classes\\%1\\shell\\open\\command").arg(progId()));
    const QString exePath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    return command.contains(exePath, Qt::CaseInsensitive);
}

bool writeUserChoice(const QString &extension, const QString &progIdValue, QString *reason)
{
    // D-03: UserChoice y UserChoiceLatest con el hash calculado en C++ (UserChoiceLatest.h),
    // verificado contra los hashes que escribe Windows. Sin helper .NET.
    const UserChoiceLatest::ApplyResult r = UserChoiceLatest::applyAssociation(extension, progIdValue);
    for (const QString &warning : r.warnings) {
        qWarning().noquote() << "[openInNukeX] asociacion:" << warning;
    }
    qInfo().noquote() << "[openInNukeX] asociacion" << extension << "->" << progIdValue
                      << (r.ok ? "escrita" : "no escrita") << "| OpenWithHost" << r.dllVersion
                      << "| intentos" << r.latestAttempts;
    if (!r.ok && reason) {
        *reason = r.reason;
    }
    return r.ok;
}

bool isOldClientInstalled()
{
    // Instalado = la entrada de desinstalacion tiene nombre, como la lista de Apps de Windows. El
    // desinstalador de Inno puede dejar la clave vacia (medido en la maquina de Lega, en HKCU): la
    // clave sola no alcanza.
    const auto listed = [](HKEY root, const QString &key) {
        return !RegistryHelper::readString(root, key, QStringLiteral("DisplayName")).isEmpty();
    };
    return listed(HKEY_LOCAL_MACHINE, QString::fromWCharArray(kOldClientUninstallKey))
        || listed(HKEY_LOCAL_MACHINE, QStringLiteral("Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\"
                                                     "Uninstall\\{B8F1A2C3-4D5E-6F78-9A0B-1C2D3E4F5678}_is1"))
        || listed(HKEY_CURRENT_USER, QString::fromWCharArray(kOldClientUninstallKey));
}

ApplyOutcome apply(bool reapply, HWND parentHwnd)
{
    ApplyOutcome outcome;

    // outcome.errors es lo que el usuario ve en "Association finished with warnings"
    // (OpenInNukeXMessages::associationFinishedWithWarnings): va en INGLES. El detalle tecnico en
    // castellano (rc de la API, el motivo exacto de UserChoiceLatest) queda solo en el log, via
    // qWarning/qInfo de mas abajo y de writeUserChoice().
    if (reapply) {
        if (!cleanConflictingKeys()) {
            outcome.errors << QStringLiteral("Could not clean up the registry.");
        }
        QThread::msleep(500);
    }

    if (!registerProgId()) {
        outcome.errors << QStringLiteral("Could not register the ProgID.");
    }
    if (!registerDefaultAppCapabilities()) {
        outcome.errors << QStringLiteral("Could not register the app in Default apps.");
    }

    const QString ext = QString::fromWCharArray(kExtensionW);
    const QString pid = progId();
    if (!registerExtensionClass(ext, pid)) {
        outcome.errors << QStringLiteral("Could not register the .nk extension.");
    }

    QString reason;
    const bool associationWritten = writeUserChoice(ext, pid, &reason);
    if (!associationWritten) {
        // `reason` (el motivo tecnico de UserChoiceLatest, en castellano) ya quedo en el log
        // dentro de writeUserChoice(): no se le agrega crudo al mensaje que ve el usuario.
        outcome.errors << QStringLiteral("Could not write the .nk association.");
    }

    notifyAssociationChanged();

    if (associationWritten && isNkAssociatedWithUs()) {
        outcome.result = ApplyResult::Success;
        return outcome;
    }

    const bool pickerLaunched = launchOpenWithPicker(parentHwnd);
    QThread::msleep(800);
    notifyAssociationChanged();

    if (isNkAssociatedWithUs()) {
        outcome.result = ApplyResult::Success;
        return outcome;
    }

    if (!openDefaultAppsSettings()) {
        outcome.errors << QStringLiteral("Could not open Windows Default apps.");
    }

    outcome.result = (!outcome.errors.isEmpty() && !pickerLaunched) ? ApplyResult::Failed
                                                                     : ApplyResult::NeedsUserConfirmation;
    return outcome;
}

bool releaseAssociation(QString *error)
{
    bool ok = true;
    const QString pid = progId();

    // Solo tocar UserChoice/UserChoiceLatest si HOY apuntan a nosotros: nunca robarle la
    // asociacion a otra app que el usuario haya elegido despues.
    if (currentNkProgId().compare(pid, Qt::CaseInsensitive) == 0) {
        ok &= cleanConflictingKeys();
    }

    ok &= RegistryHelper::deleteTree(HKEY_CURRENT_USER, QStringLiteral("Software\\Classes\\%1").arg(pid));
    ok &= RegistryHelper::deleteTree(HKEY_CURRENT_USER, QString::fromWCharArray(kCapabilitiesPath));
    // RegisteredApplications es un VALOR dentro de una clave compartida por todo Windows: se
    // borra solo nuestra entrada, nunca la clave entera.
    ok &= deleteValue(HKEY_CURRENT_USER, QString::fromWCharArray(kRegisteredAppsPath),
                      QString::fromWCharArray(kAppRegKey));

    notifyAssociationChanged();

    if (!ok && error) {
        *error = QStringLiteral("Could not remove all the association keys.");
    }
    return ok;
}

} // namespace WinFileAssociation
