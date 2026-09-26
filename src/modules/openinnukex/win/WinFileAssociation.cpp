#include "modules/openinnukex/win/WinFileAssociation.h"
#include "modules/openinnukex/win/UserChoiceLatest.h"

#include "platform/win/RegistryHelper.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <QtGlobal>

#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>

namespace {

constexpr wchar_t kProgIdW[] = L"LGA.NukeScript.1";
constexpr wchar_t kExtensionW[] = L".nk";

// A1: el `.nk` tiene su PROPIO valor en RegisteredApplications. `LGA_MightyTools` es el del
// navegador (Link Redirector, StartMenuInternet\LGA_MightyTools\Capabilities); si los dos usaran el
// mismo, ganaba el ultimo y soltar uno borraba el del otro.
constexpr wchar_t kRegisteredAppValue[] = L"LGA_MightyTools_NukeScripts";
// El valor que usaban las versiones anteriores para el `.nk`. Se migra (se borra) solo si todavia
// apunta a NUESTRAS Capabilities: si apunta a las del navegador, es de Link Redirector.
constexpr wchar_t kLegacyRegisteredAppValue[] = L"LGA_MightyTools";
// Nombre visible en Apps predeterminadas. Provisorio: lo confirma Lega.
constexpr wchar_t kApplicationName[] = L"LGA Mighty Tools (Nuke scripts)";
constexpr wchar_t kAppRootPath[] = L"Software\\LGA_MightyTools";
constexpr wchar_t kCapabilitiesPath[] = L"Software\\LGA_MightyTools\\Capabilities";
constexpr wchar_t kRegisteredAppsPath[] = L"Software\\RegisteredApplications";
constexpr wchar_t kFileExtsNkPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\.nk";

// Cliente viejo (LGA OpenInNukeX v1.83): lo que dejaba en HKCU ademas del ProgID compartido.
constexpr wchar_t kOldAppRootPath[] = L"Software\\OpenInNukeX";
constexpr wchar_t kOldCapabilitiesPath[] = L"Software\\OpenInNukeX\\Capabilities";
constexpr wchar_t kOldRegisteredAppValue[] = L"OpenInNukeX";

// Clave de desinstalacion del cliente viejo (Inno Setup, AppId del plan seccion 2/9).
constexpr wchar_t kOldClientUninstallKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\"
    L"{B8F1A2C3-4D5E-6F78-9A0B-1C2D3E4F5678}_is1";

QString w(const wchar_t *text)
{
    return QString::fromWCharArray(text);
}

QString progIdKey()
{
    return QStringLiteral("Software\\Classes\\%1").arg(w(kProgIdW));
}

QString progIdCommand()
{
    return RegistryHelper::readString(HKEY_CURRENT_USER, progIdKey() + QStringLiteral("\\shell\\open\\command"));
}

QString userChoicePath()
{
    return w(kFileExtsNkPath) + QStringLiteral("\\UserChoice");
}

QString userChoiceLatestPath()
{
    return w(kFileExtsNkPath) + QStringLiteral("\\UserChoiceLatest");
}

QString userChoiceProgId()
{
    return RegistryHelper::readString(HKEY_CURRENT_USER, userChoicePath(), QStringLiteral("ProgId"));
}

// Windows guarda el ProgId de UserChoiceLatest en la SUBCLAVE UserChoiceLatest\ProgId (valor
// ProgId), no en UserChoiceLatest directamente.
QString userChoiceLatestProgId()
{
    return RegistryHelper::readString(HKEY_CURRENT_USER, userChoiceLatestPath() + QStringLiteral("\\ProgId"),
                                      QStringLiteral("ProgId"));
}

bool isOurProgIdName(const QString &value)
{
    return value.compare(w(kProgIdW), Qt::CaseInsensitive) == 0;
}

// ─── Registro de la app ──────────────────────────────────────────────────────

bool registerProgId()
{
    const QString exePath = RegistryHelper::ownExePath();

    bool ok = true;
    ok &= RegistryHelper::writeString(HKEY_CURRENT_USER, progIdKey(), QString(), QStringLiteral("Nuke Script File"));
    ok &= RegistryHelper::writeString(HKEY_CURRENT_USER, progIdKey() + QStringLiteral("\\shell\\open\\command"),
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
    const QString exePath = RegistryHelper::ownExePath();
    const QString iconRef = QStringLiteral("\"%1\",0").arg(exePath);
    const QString capsPath = w(kCapabilitiesPath);
    const QString registeredApps = w(kRegisteredAppsPath);

    bool ok = true;
    ok &= RegistryHelper::writeString(HKEY_CURRENT_USER, capsPath, QStringLiteral("ApplicationName"), w(kApplicationName));
    ok &= RegistryHelper::writeString(HKEY_CURRENT_USER, capsPath, QStringLiteral("ApplicationDescription"),
                                      QStringLiteral("Opens Nuke scripts (.nk) with your preferred NukeX."));
    ok &= RegistryHelper::writeString(HKEY_CURRENT_USER, capsPath, QStringLiteral("ApplicationIcon"), iconRef);
    ok &= RegistryHelper::writeString(HKEY_CURRENT_USER, capsPath + QStringLiteral("\\FileAssociations"), w(kExtensionW),
                                      w(kProgIdW));
    ok &= RegistryHelper::writeString(HKEY_CURRENT_USER, registeredApps, w(kRegisteredAppValue), capsPath);

    // Migracion (A1): las versiones anteriores escribian el `.nk` en el valor LGA_MightyTools. Se
    // borra SOLO si todavia apunta a estas Capabilities; si apunta a las del navegador es de Link
    // Redirector y queda.
    const QString legacy = RegistryHelper::readString(HKEY_CURRENT_USER, registeredApps, w(kLegacyRegisteredAppValue));
    if (legacy.compare(capsPath, Qt::CaseInsensitive) == 0) {
        ok &= RegistryHelper::deleteValue(HKEY_CURRENT_USER, registeredApps, w(kLegacyRegisteredAppValue));
        qInfo() << "[openInNukeX] migrado RegisteredApplications:" << w(kLegacyRegisteredAppValue) << "->"
                << w(kRegisteredAppValue);
    }
    return ok;
}

bool cleanConflictingKeys()
{
    bool ok = true;
    ok &= RegistryHelper::deleteTree(HKEY_CURRENT_USER, userChoicePath());
    ok &= RegistryHelper::deleteTree(HKEY_CURRENT_USER, userChoiceLatestPath());
    return ok;
}

// Lo que es NUESTRO de la asociacion, decidido por el CONTENIDO (regla "Registro limpio") y
// evaluado entero ANTES de borrar nada: borrar el ProgID primero haria imposible saber despues si
// UserChoice o `.nk` apuntaban a un ProgID nuestro.
struct Ownership
{
    bool progIdOwned = false;           ///< Classes\LGA.NukeScript.1: su comando apunta a este exe
    bool extensionClassOwned = false;   ///< Classes\.nk (valor por defecto) = nuestro ProgID propio
    bool userChoiceOwned = false;       ///< FileExts\.nk\UserChoice: ProgId nuestro Y ProgID propio
    bool userChoiceLatestOwned = false; ///< idem UserChoiceLatest
    bool capabilitiesExists = false;
    bool capabilitiesOwned = false;     ///< Software\LGA_MightyTools\Capabilities: su icono es este exe
    bool registeredOwned = false;       ///< RegisteredApplications\LGA_MightyTools_NukeScripts
    bool legacyRegisteredOwned = false; ///< RegisteredApplications\LGA_MightyTools (version anterior)
};

Ownership evaluateOwnership()
{
    const QString exe = RegistryHelper::ownExePath();
    const QString capsPath = w(kCapabilitiesPath);
    Ownership o;
    // LGA.NukeScript.1 es COMPARTIDO con el cliente viejo de Open in NukeX: es nuestro solo si su
    // comando apunta a este exe.
    o.progIdOwned = RegistryHelper::commandPointsTo(progIdCommand(), exe);
    o.extensionClassOwned =
        o.progIdOwned
        && isOurProgIdName(RegistryHelper::readString(HKEY_CURRENT_USER, QStringLiteral("Software\\Classes\\.nk")));
    o.userChoiceOwned = o.progIdOwned && isOurProgIdName(userChoiceProgId());
    o.userChoiceLatestOwned = o.progIdOwned && isOurProgIdName(userChoiceLatestProgId());
    o.capabilitiesExists = RegistryHelper::keyExists(HKEY_CURRENT_USER, capsPath);
    o.capabilitiesOwned = RegistryHelper::commandPointsTo(
        RegistryHelper::readString(HKEY_CURRENT_USER, capsPath, QStringLiteral("ApplicationIcon")), exe);
    // Un valor de RegisteredApplications se decide por la ruta a la que apunta: la nuestra, y que
    // esa clave sea nuestra o ya no exista (colgado no le sirve a nadie).
    const auto pointsToOurCaps = [&](const wchar_t *valueName) {
        const QString target = RegistryHelper::readString(HKEY_CURRENT_USER, w(kRegisteredAppsPath), w(valueName));
        return target.compare(capsPath, Qt::CaseInsensitive) == 0 && (o.capabilitiesOwned || !o.capabilitiesExists);
    };
    o.registeredOwned = pointsToOurCaps(kRegisteredAppValue);
    o.legacyRegisteredOwned = pointsToOurCaps(kLegacyRegisteredAppValue);
    return o;
}

// true si `exePath` es una ruta de una unidad LOCAL FIJA presente y el archivo no esta. Una ruta de
// red, de una unidad desconectada o removible (otro pendrive con la misma letra) da false: ahi no se
// puede saber si el exe existe.
bool missingOnPresentLocalDrive(const QString &exePath)
{
    const QString native = QDir::toNativeSeparators(exePath.trimmed());
    if (native.size() < 4 || !native.at(0).isLetter() || native.at(1) != QLatin1Char(':')
        || native.at(2) != QLatin1Char('\\')) {
        return false; // vacia, relativa o UNC
    }
    const std::wstring root = native.left(3).toStdWString();
    if (GetDriveTypeW(root.c_str()) != DRIVE_FIXED) {
        return false;
    }
    return !QFileInfo::exists(native);
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
    // El deep link usa el nombre del valor de RegisteredApplications: el del `.nk` (A1).
    const QString deepLink = QStringLiteral("ms-settings:defaultapps?registeredAppUser=%1").arg(w(kRegisteredAppValue));
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
    return w(kProgIdW);
}

QString registeredApplicationValue()
{
    return w(kRegisteredAppValue);
}

QString currentNkProgId()
{
    // UserChoiceLatest manda si tiene un ProgId propio; si no, se cae al UserChoice legado. Sin
    // depender de si el hash "esta activo": esa lectura (HashVersion) vive del lado de
    // UserChoiceLatest.h (ver trySetUserChoiceLatestHash), no aca.
    const QString latestProgId = userChoiceLatestProgId();
    if (!latestProgId.isEmpty()) {
        return latestProgId;
    }
    const QString legacyProgId = userChoiceProgId();
    if (!legacyProgId.isEmpty()) {
        return legacyProgId;
    }
    // Sin ninguna eleccion del usuario, el Explorador usa la clase de la extension: el doble click
    // ya abre con ese ProgId.
    return RegistryHelper::readString(HKEY_CURRENT_USER, QStringLiteral("Software\\Classes\\.nk"), QString());
}

bool isNkAssociatedWithUs()
{
    if (!isOurProgIdName(currentNkProgId())) {
        return false;
    }
    // Plan 4.6: "el ProgID MAS la ruta del comando igual a este exe" — un ProgID compartido con
    // una copia instalada en otro lado (o con el cliente viejo, que tambien podria reescribirlo)
    // no cuenta como "asociado con nosotros" si el comando no apunta a ESTE binario.
    return RegistryHelper::commandPointsTo(progIdCommand(), RegistryHelper::ownExePath());
}

bool registerClasses(QStringList *errors)
{
    bool ok = true;
    if (!registerProgId()) {
        ok = false;
        if (errors) {
            *errors << QStringLiteral("Could not register the ProgID.");
        }
    }
    if (!registerDefaultAppCapabilities()) {
        ok = false;
        if (errors) {
            *errors << QStringLiteral("Could not register the app in Default apps.");
        }
    }
    if (!registerExtensionClass(w(kExtensionW), progId())) {
        ok = false;
        if (errors) {
            *errors << QStringLiteral("Could not register the .nk extension.");
        }
    }
    return ok;
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
    return listed(HKEY_LOCAL_MACHINE, w(kOldClientUninstallKey))
        || listed(HKEY_LOCAL_MACHINE, QStringLiteral("Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\"
                                                     "Uninstall\\{B8F1A2C3-4D5E-6F78-9A0B-1C2D3E4F5678}_is1"))
        || listed(HKEY_CURRENT_USER, w(kOldClientUninstallKey));
}

QStringList removeOldClientLeftovers(bool oldClientInstalled)
{
    QStringList removed;
    if (oldClientInstalled) {
        return removed; // sigue instalado: lo suyo es suyo
    }
    const QString capsPath = w(kOldCapabilitiesPath);
    if (!RegistryHelper::keyExists(HKEY_CURRENT_USER, capsPath)) {
        return removed;
    }
    const QString icon = RegistryHelper::readString(HKEY_CURRENT_USER, capsPath, QStringLiteral("ApplicationIcon"));
    const QString oldExe = RegistryHelper::commandExecutable(icon);
    if (!missingOnPresentLocalDrive(oldExe)) {
        qInfo() << "[openInNukeX] restos del cliente viejo: se dejan (el exe existe o no se puede saber):" << oldExe;
        return removed;
    }

    // Nunca Classes\LGA.NukeScript.1: es compartido y ahora lo usa esta app.
    const QString registeredApps = w(kRegisteredAppsPath);
    const QString target = RegistryHelper::readString(HKEY_CURRENT_USER, registeredApps, w(kOldRegisteredAppValue));
    if (target.compare(capsPath, Qt::CaseInsensitive) == 0
        && RegistryHelper::deleteValue(HKEY_CURRENT_USER, registeredApps, w(kOldRegisteredAppValue))) {
        removed << QStringLiteral("HKCU\\%1\\%2").arg(registeredApps, w(kOldRegisteredAppValue));
    }
    if (RegistryHelper::deleteTree(HKEY_CURRENT_USER, capsPath)) {
        removed << QStringLiteral("HKCU\\%1").arg(capsPath);
    }
    if (RegistryHelper::keyExists(HKEY_CURRENT_USER, w(kOldAppRootPath))) {
        RegistryHelper::deleteKeyIfEmpty(HKEY_CURRENT_USER, w(kOldAppRootPath));
        if (!RegistryHelper::keyExists(HKEY_CURRENT_USER, w(kOldAppRootPath))) {
            removed << QStringLiteral("HKCU\\%1").arg(w(kOldAppRootPath));
        }
    }
    for (const QString &entry : removed) {
        qInfo().noquote() << "[openInNukeX] borrado resto del cliente viejo (exe ausente:" << oldExe << "):" << entry;
    }
    return removed;
}

ApplyOutcome apply(bool reapply, HWND parentHwnd)
{
    ApplyOutcome outcome;

    // A4: restos del cliente viejo de Open in NukeX, solo aca (el usuario apreto Apply).
    removeOldClientLeftovers(isOldClientInstalled());

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

    registerClasses(&outcome.errors);

    QString reason;
    const bool associationWritten = writeUserChoice(w(kExtensionW), progId(), &reason);
    if (!associationWritten) {
        // `reason` (el motivo tecnico de UserChoiceLatest, en castellano) ya quedo en el log
        // dentro de writeUserChoice(): no se le agrega crudo al mensaje que ve el usuario.
        outcome.errors << QStringLiteral("Could not write the .nk association.");
    }

    RegistryHelper::notifyAssociationsChanged();

    if (associationWritten && isNkAssociatedWithUs()) {
        outcome.result = ApplyResult::Success;
        return outcome;
    }

    const bool pickerLaunched = launchOpenWithPicker(parentHwnd);
    QThread::msleep(800);
    RegistryHelper::notifyAssociationsChanged();

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
    // Todo se evalua ANTES de borrar (ver Ownership). Lo que no es de este exe queda como esta.
    const Ownership o = evaluateOwnership();
    const QString registeredApps = w(kRegisteredAppsPath);
    bool ok = true;

    // UserChoice/UserChoiceLatest solo si HOY apuntan a nuestro ProgID y ese ProgID es de este exe:
    // nunca robarle la asociacion a otra app que el usuario eligio despues, ni al cliente viejo.
    if (o.userChoiceOwned) {
        ok &= RegistryHelper::deleteTree(HKEY_CURRENT_USER, userChoicePath());
    }
    if (o.userChoiceLatestOwned) {
        ok &= RegistryHelper::deleteTree(HKEY_CURRENT_USER, userChoiceLatestPath());
    }
    // Classes\.nk: solo el valor por defecto; la clave, solo si queda vacia (puede tener
    // OpenWithProgids de otras apps).
    if (o.extensionClassOwned) {
        ok &= RegistryHelper::deleteValue(HKEY_CURRENT_USER, QStringLiteral("Software\\Classes\\.nk"), QString());
        ok &= RegistryHelper::deleteKeyIfEmpty(HKEY_CURRENT_USER, QStringLiteral("Software\\Classes\\.nk"));
    }
    if (o.progIdOwned) {
        ok &= RegistryHelper::deleteTree(HKEY_CURRENT_USER, progIdKey());
    }
    // RegisteredApplications es una clave compartida por todo Windows: solo nuestros valores.
    if (o.registeredOwned) {
        ok &= RegistryHelper::deleteValue(HKEY_CURRENT_USER, registeredApps, w(kRegisteredAppValue));
    }
    if (o.legacyRegisteredOwned) {
        ok &= RegistryHelper::deleteValue(HKEY_CURRENT_USER, registeredApps, w(kLegacyRegisteredAppValue));
    }
    if (o.capabilitiesOwned) {
        ok &= RegistryHelper::deleteTree(HKEY_CURRENT_USER, w(kCapabilitiesPath));
    }
    ok &= RegistryHelper::deleteKeyIfEmpty(HKEY_CURRENT_USER, w(kAppRootPath));

    RegistryHelper::notifyAssociationsChanged();

    qInfo() << "[openInNukeX] asociacion .nk soltada. ProgID:" << (o.progIdOwned ? "borrado" : "no era de este exe")
            << "| UserChoice:" << o.userChoiceOwned << "| UserChoiceLatest:" << o.userChoiceLatestOwned
            << "| Classes\\.nk:" << o.extensionClassOwned << "| Capabilities:" << o.capabilitiesOwned
            << "| RegisteredApplications:" << o.registeredOwned << "/" << o.legacyRegisteredOwned << "| ok:" << ok;

    if (!ok && error) {
        *error = QStringLiteral("Could not remove all the association keys.");
    }
    return ok;
}

} // namespace WinFileAssociation
