#include "modules/openinnukex/win/WinFileAssociation.h"

#include "platform/win/RegistryHelper.h"

#include <QCoreApplication>
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

bool launchOpenWithPicker()
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
            const HRESULT hr = launcher->Launch(nullptr, kExtensionW, 0x2004);
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
    const QString latestPath = QStringLiteral(
        "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\.nk\\UserChoiceLatest");
    const QString latestProgId = RegistryHelper::readString(HKEY_CURRENT_USER, latestPath, QStringLiteral("ProgId"));
    if (!latestProgId.isEmpty()) {
        return latestProgId;
    }

    const QString legacyPath = QStringLiteral(
        "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\.nk\\UserChoice");
    return RegistryHelper::readString(HKEY_CURRENT_USER, legacyPath, QStringLiteral("ProgId"));
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
    Q_UNUSED(extension);
    Q_UNUSED(progIdValue);
    // ---- COSTURA D-03 --------------------------------------------------------------------
    // La escritura de UserChoice/UserChoiceLatest (el hash legado Y el de Windows 11) la entrega
    // OTRO ejecutor en src/modules/openinnukex/win/UserChoiceLatest.{h,cpp}
    // (API: applyAssociation(ext, progId, ...) -> ApplyResult con ok/motivo/avisos, e
    // isLatestHashActive()). Todavia no existe en este build: el supervisor conecta esta funcion
    // reemplazando el cuerpo por la llamada real. Hasta entonces, apply() SIEMPRE cae al selector
    // nativo "Abrir con" (o a ms-settings:defaultapps).
    if (reason) {
        *reason = QStringLiteral("no disponible todavia");
    }
    qInfo("[openInNukeX] writeUserChoice: no disponible todavia en este build "
          "(ver src/modules/openinnukex/win/UserChoiceLatest.h, D-03). Se cae al selector nativo.");
    return false;
}

bool isOldClientInstalled()
{
    return RegistryHelper::keyExists(HKEY_LOCAL_MACHINE, QString::fromWCharArray(kOldClientUninstallKey))
        || RegistryHelper::keyExists(HKEY_LOCAL_MACHINE,
                                     QStringLiteral("Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\"
                                                    "Uninstall\\{B8F1A2C3-4D5E-6F78-9A0B-1C2D3E4F5678}_is1"))
        || RegistryHelper::keyExists(HKEY_CURRENT_USER, QString::fromWCharArray(kOldClientUninstallKey));
}

ApplyOutcome apply(bool reapply)
{
    ApplyOutcome outcome;

    if (reapply) {
        if (!cleanConflictingKeys()) {
            outcome.errors << QStringLiteral("Error al limpiar el registro");
        }
        QThread::msleep(500);
    }

    if (!registerProgId()) {
        outcome.errors << QStringLiteral("Error al registrar el ProgID");
    }
    if (!registerDefaultAppCapabilities()) {
        outcome.errors << QStringLiteral("Error al registrar la app en Apps predeterminadas");
    }

    const QString ext = QString::fromWCharArray(kExtensionW);
    const QString pid = progId();
    if (!registerExtensionClass(ext, pid)) {
        outcome.errors << QStringLiteral("Error al registrar la extension");
    }

    QString reason;
    const bool associationWritten = writeUserChoice(ext, pid, &reason);
    if (!associationWritten) {
        outcome.errors << QStringLiteral("No se pudo escribir la asociacion de .nk (%1)").arg(reason);
    }

    notifyAssociationChanged();

    if (associationWritten && isNkAssociatedWithUs()) {
        outcome.result = ApplyResult::Success;
        return outcome;
    }

    const bool pickerLaunched = launchOpenWithPicker();
    QThread::msleep(800);
    notifyAssociationChanged();

    if (isNkAssociatedWithUs()) {
        outcome.result = ApplyResult::Success;
        return outcome;
    }

    if (!openDefaultAppsSettings()) {
        outcome.errors << QStringLiteral("No se pudo abrir Apps predeterminadas de Windows");
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
        *error = QStringLiteral("No se pudieron borrar todas las claves de la asociacion");
    }
    return ok;
}

} // namespace WinFileAssociation
