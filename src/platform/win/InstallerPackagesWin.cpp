#include "platform/InstallerPackages.h"

#include <QDebug>
#include <QDir>

#include <windows.h>
#include <msi.h>
#include <msidefs.h>
#include <msiquery.h>
#include <sddl.h>

#include <string>
#include <vector>

// Solo lectura. Nada de esto escribe en el registro ni en el disco: la API de Windows Installer se usa
// para enumerar y preguntar propiedades, los paquetes se abren con MSIDBOPEN_READONLY, el registro se
// abre con KEY_READ (nunca con QSettings, que crea la clave al leer) y la carpeta se lista y se abre sin
// pedir acceso de escritura.

namespace {

using InstallerPackages::Claims;
using InstallerPackages::FileFacts;
using InstallerPackages::PackageFacts;
using InstallerPackages::Survey;
using InstallerPackages::codeKey;
using InstallerPackages::pathKey;

constexpr wchar_t kUserDataKey[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Installer\\UserData";
constexpr wchar_t kInProgressKey[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Installer\\InProgress";
constexpr wchar_t kSessionManagerKey[] = L"SYSTEM\\CurrentControlSet\\Control\\Session Manager";
constexpr wchar_t kEveryoneSid[] = L"s-1-1-0"; // "todos los usuarios" para la API de Windows Installer
constexpr qint64 kEpochDiff = 116444736000000000LL; // 1601 -> 1970, en unidades de 100 ns

qint64 toSecs(const FILETIME &time)
{
    const qint64 ticks = (qint64(time.dwHighDateTime) << 32) | time.dwLowDateTime;
    return ticks <= kEpochDiff ? 0 : (ticks - kEpochDiff) / 10000000LL;
}

QString currentUserSid()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return {};
    }
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<char> buffer(size ? size : 1);
    QString result;
    if (size && GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) {
        LPWSTR text = nullptr;
        if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(buffer.data())->User.Sid, &text)) {
            result = QString::fromWCharArray(text);
            LocalFree(text);
        }
    }
    CloseHandle(token);
    return result;
}

QString installerDirectory()
{
    std::wstring windows(MAX_PATH + 1, L'\0');
    UINT length = GetSystemWindowsDirectoryW(windows.data(), UINT(windows.size()));
    if (length >= windows.size()) {
        windows.resize(length + 1);
        length = GetSystemWindowsDirectoryW(windows.data(), UINT(windows.size()));
    }
    if (length == 0 || length >= windows.size()) {
        return {};
    }
    const QString written = QString::fromWCharArray(windows.data(), int(length)) + QStringLiteral("\\Installer");
    // La ruta real, y solo si es una carpeta de verdad: un enlace a otro lado no se mira.
    const QString real = FileSystemOps::canonicalPath(written);
    if (real.isEmpty() || FileSystemOps::kind(real) != FileSystemOps::Kind::Dir) {
        return {};
    }
    return real;
}

// Una copia que Windows Installer dice usar: por su ruta escrita, por su ruta real y por su identidad.
void addClaim(Claims &claims, const QString &package)
{
    const QString trimmed = package.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    claims.paths.insert(pathKey(trimmed));
    const QString real = FileSystemOps::canonicalPath(trimmed);
    if (!real.isEmpty()) {
        claims.paths.insert(pathKey(real));
    }
    const FileSystemOps::Identity id = FileSystemOps::identity(trimmed);
    if (id.valid && !claims.ids.contains(id)) {
        claims.ids.append(id);
    }
}

void failEnumeration(Survey &survey, const QString &what, quint32 code)
{
    if (survey.enumerationError.isEmpty()) {
        survey.enumerationError = QStringLiteral("%1: %2").arg(what).arg(code);
    }
    survey.enumerated = false;
}

// Una cadena de la API con buffer que se agranda: ERROR_SUCCESS con el valor, o el codigo de la API.
template <typename Query>
UINT readText(Query query, QString *value)
{
    std::wstring buffer(MAX_PATH + 1, L'\0');
    DWORD length = DWORD(buffer.size());
    UINT code = query(buffer.data(), &length);
    if (code == ERROR_MORE_DATA) {
        buffer.assign(size_t(length) + 2, L'\0');
        length = DWORD(buffer.size());
        code = query(buffer.data(), &length);
    }
    if (code == ERROR_SUCCESS) {
        *value = QString::fromWCharArray(buffer.data(), int(qMin<size_t>(length, buffer.size())));
    }
    return code;
}

// Productos de todos los contextos (maquina, usuario administrado y no administrado) del alcance pedido.
bool enumerateProducts(Survey &survey, const wchar_t *userSid)
{
    for (DWORD index = 0;; ++index) {
        wchar_t code[39] = {};
        MSIINSTALLCONTEXT context = MSIINSTALLCONTEXT_NONE;
        std::wstring sid(256, L'\0');
        DWORD sidLength = DWORD(sid.size());
        UINT result = MsiEnumProductsExW(nullptr, userSid, MSIINSTALLCONTEXT_ALL, index, code, &context, sid.data(), &sidLength);
        if (result == ERROR_MORE_DATA) {
            sid.assign(size_t(sidLength) + 2, L'\0');
            sidLength = DWORD(sid.size());
            result = MsiEnumProductsExW(nullptr, userSid, MSIINSTALLCONTEXT_ALL, index, code, &context, sid.data(), &sidLength);
        }
        if (result == ERROR_NO_MORE_ITEMS) {
            return true;
        }
        if (result != ERROR_SUCCESS) {
            failEnumeration(survey, QStringLiteral("MsiEnumProductsEx"), result);
            return false;
        }
        ++survey.products;
        survey.installedProducts.insert(codeKey(QString::fromWCharArray(code)));
        // En el contexto de la maquina el SID va nulo.
        const wchar_t *owner = context == MSIINSTALLCONTEXT_MACHINE ? nullptr : sid.c_str();
        QString package;
        const UINT info = readText(
            [&](wchar_t *buffer, DWORD *length) {
                return MsiGetProductInfoExW(code, owner, context, INSTALLPROPERTY_LOCALPACKAGE, buffer, length);
            },
            &package);
        if (info == ERROR_SUCCESS && !package.trimmed().isEmpty()) {
            addClaim(survey.claims, package);
        } else if (info == ERROR_SUCCESS || info == ERROR_UNKNOWN_PROPERTY || info == ERROR_UNKNOWN_PRODUCT) {
            // Sin copia registrada (un producto anunciado) o desinstalado en el medio: nada que reclamar.
            ++survey.withoutPackage;
        } else {
            failEnumeration(survey, QStringLiteral("MsiGetProductInfoEx"), info);
            return false;
        }
    }
}

bool enumeratePatches(Survey &survey, const wchar_t *userSid)
{
    for (DWORD index = 0;; ++index) {
        wchar_t patch[39] = {};
        wchar_t product[39] = {};
        MSIINSTALLCONTEXT context = MSIINSTALLCONTEXT_NONE;
        std::wstring sid(256, L'\0');
        DWORD sidLength = DWORD(sid.size());
        UINT result = MsiEnumPatchesExW(nullptr, userSid, MSIINSTALLCONTEXT_ALL, MSIPATCHSTATE_ALL, index, patch, product, &context,
                                        sid.data(), &sidLength);
        if (result == ERROR_MORE_DATA) {
            sid.assign(size_t(sidLength) + 2, L'\0');
            sidLength = DWORD(sid.size());
            result = MsiEnumPatchesExW(nullptr, userSid, MSIINSTALLCONTEXT_ALL, MSIPATCHSTATE_ALL, index, patch, product, &context,
                                       sid.data(), &sidLength);
        }
        if (result == ERROR_NO_MORE_ITEMS) {
            return true;
        }
        if (result != ERROR_SUCCESS) {
            failEnumeration(survey, QStringLiteral("MsiEnumPatchesEx"), result);
            return false;
        }
        ++survey.patches;
        const wchar_t *owner = context == MSIINSTALLCONTEXT_MACHINE ? nullptr : sid.c_str();
        QString package;
        const UINT info = readText(
            [&](wchar_t *buffer, DWORD *length) {
                return MsiGetPatchInfoExW(patch, product, owner, context, INSTALLPROPERTY_LOCALPACKAGE, buffer, length);
            },
            &package);
        if (info == ERROR_SUCCESS && !package.trimmed().isEmpty()) {
            addClaim(survey.claims, package);
        } else if (info == ERROR_SUCCESS || info == ERROR_UNKNOWN_PROPERTY || info == ERROR_UNKNOWN_PATCH
                   || info == ERROR_UNKNOWN_PRODUCT) {
            ++survey.withoutPackage;
        } else {
            failEnumeration(survey, QStringLiteral("MsiGetPatchInfoEx"), info);
            return false;
        }
    }
}

// ---- Registro (solo lectura)

class Key
{
public:
    Key() = default;
    Key(const Key &) = delete;
    Key &operator=(const Key &) = delete;
    ~Key()
    {
        if (m_key) RegCloseKey(m_key);
    }
    LSTATUS open(HKEY parent, const std::wstring &path)
    {
        return RegOpenKeyExW(parent, path.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &m_key);
    }
    HKEY get() const { return m_key; }

private:
    HKEY m_key = nullptr;
};

// Los nombres de las subclaves. false si la enumeracion fallo.
bool subKeys(HKEY key, QStringList *names)
{
    names->clear();
    for (DWORD index = 0;; ++index) {
        wchar_t name[256];
        DWORD length = 256;
        const LSTATUS status = RegEnumKeyExW(key, index, name, &length, nullptr, nullptr, nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS) {
            return true;
        }
        if (status != ERROR_SUCCESS) {
            return false;
        }
        names->append(QString::fromWCharArray(name, int(length)));
    }
}

// Un valor de texto; vacio si no esta. false si no se pudo leer por otra cosa.
bool readString(HKEY key, const wchar_t *subKey, const wchar_t *value, QString *text)
{
    text->clear();
    DWORD bytes = 0;
    LSTATUS status = RegGetValueW(key, subKey, value, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, nullptr, nullptr, &bytes);
    if (status == ERROR_FILE_NOT_FOUND) {
        return true;
    }
    if (status != ERROR_SUCCESS && status != ERROR_MORE_DATA) {
        return false;
    }
    std::wstring buffer(bytes / sizeof(wchar_t) + 2, L'\0');
    bytes = DWORD(buffer.size() * sizeof(wchar_t));
    status = RegGetValueW(key, subKey, value, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, nullptr, buffer.data(), &bytes);
    if (status == ERROR_FILE_NOT_FOUND) {
        return true;
    }
    if (status != ERROR_SUCCESS) {
        return false;
    }
    *text = QString::fromWCharArray(buffer.c_str());
    return true;
}

// Un valor de varias cadenas; vacio si no esta. false si no se pudo leer por otra cosa.
bool readMultiString(HKEY key, const wchar_t *subKey, const wchar_t *value, QStringList *lines)
{
    lines->clear();
    for (int attempt = 0; attempt < 3; ++attempt) {
        DWORD bytes = 0;
        LSTATUS status = RegGetValueW(key, subKey, value, RRF_RT_REG_MULTI_SZ, nullptr, nullptr, &bytes);
        if (status == ERROR_FILE_NOT_FOUND) {
            return true;
        }
        if (status != ERROR_SUCCESS && status != ERROR_MORE_DATA) {
            return false;
        }
        std::wstring buffer(bytes / sizeof(wchar_t) + 2, L'\0');
        bytes = DWORD(buffer.size() * sizeof(wchar_t));
        status = RegGetValueW(key, subKey, value, RRF_RT_REG_MULTI_SZ, nullptr, buffer.data(), &bytes);
        if (status == ERROR_MORE_DATA) {
            continue; // crecio entre las dos lecturas
        }
        if (status == ERROR_FILE_NOT_FOUND) {
            return true;
        }
        if (status != ERROR_SUCCESS) {
            return false;
        }
        const wchar_t *cursor = buffer.c_str();
        const wchar_t *end = buffer.c_str() + qMin<size_t>(bytes / sizeof(wchar_t), buffer.size());
        while (cursor < end && *cursor) {
            const std::wstring line(cursor);
            lines->append(QString::fromStdWString(line));
            cursor += line.size() + 1;
        }
        return true;
    }
    return false;
}

// Las subclaves de "<sid>\<what>" (Products o Patches) y la copia local de cada una. `inner`: la
// subclave donde vive el valor ("InstallProperties" en un producto, ninguna en un parche).
bool readUserDataItems(HKEY sidKey, const wchar_t *what, const wchar_t *inner, Claims &claims, int *count)
{
    *count = 0;
    Key list;
    const LSTATUS status = list.open(sidKey, what);
    if (status == ERROR_FILE_NOT_FOUND) {
        return true;
    }
    if (status != ERROR_SUCCESS) {
        return false;
    }
    QStringList codes;
    if (!subKeys(list.get(), &codes)) {
        return false;
    }
    *count = int(codes.size());
    for (const QString &code : codes) {
        std::wstring path = code.toStdWString();
        if (inner) {
            path += L"\\";
            path += inner;
        }
        QString package;
        // Una clave sin la subclave o sin el valor no reclama nada; un error de lectura si invalida todo.
        Key item;
        const LSTATUS opened = item.open(list.get(), path);
        if (opened == ERROR_FILE_NOT_FOUND) {
            continue;
        }
        if (opened != ERROR_SUCCESS || !readString(item.get(), nullptr, L"LocalPackage", &package)) {
            return false;
        }
        addClaim(claims, package);
    }
    return true;
}

// HKLM\...\Installer\UserData: que SIDs tienen productos o parches registrados, y las copias que
// registran (se suman a lo reclamado: la API y el registro tienen que coincidir, y si no, gana el
// lado que protege mas).
void readUserData(Survey &survey)
{
    survey.userDataRead = false;
    survey.userDataSids.clear();
    Key root;
    if (root.open(HKEY_LOCAL_MACHINE, kUserDataKey) != ERROR_SUCCESS) {
        return;
    }
    QStringList sids;
    if (!subKeys(root.get(), &sids)) {
        return;
    }
    for (const QString &sid : sids) {
        Key sidKey;
        if (sidKey.open(root.get(), sid.toStdWString()) != ERROR_SUCCESS) {
            return;
        }
        int products = 0;
        int patches = 0;
        if (!readUserDataItems(sidKey.get(), L"Products", L"InstallProperties", survey.claims, &products)
            || !readUserDataItems(sidKey.get(), L"Patches", nullptr, survey.claims, &patches)) {
            return;
        }
        if (products + patches > 0) {
            survey.userDataSids.append(sid);
        }
    }
    survey.userDataRead = true;
}

// ---- Enlaces de origen

// A donde lleva un enlace, siguiendolo. Vacio y `dangling` si el destino ya no existe; vacio sin
// `dangling` si no se pudo saber.
QString linkTarget(const QString &path, bool *dangling)
{
    *dangling = false;
    const std::wstring native = FileSystemOps::nativePath(path);
    const HANDLE handle = CreateFileW(native.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                      FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        *dangling = error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND || error == ERROR_CANT_RESOLVE_FILENAME;
        return {};
    }
    std::wstring buffer(1024, L'\0');
    DWORD length = GetFinalPathNameByHandleW(handle, buffer.data(), DWORD(buffer.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (length >= buffer.size()) {
        buffer.resize(length + 1);
        length = GetFinalPathNameByHandleW(handle, buffer.data(), DWORD(buffer.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    }
    CloseHandle(handle);
    if (length == 0 || length >= buffer.size()) {
        return {};
    }
    QString result = QString::fromWCharArray(buffer.data(), int(length));
    if (result.startsWith(QLatin1String("\\\\?\\")) && !result.startsWith(QLatin1String("\\\\?\\UNC\\"))) {
        result.remove(0, 4);
    }
    return result;
}

// %WINDIR%\Installer\DerandomizedSymbolicLinksForSourceLists: enlaces con nombre legible ("Producto.msi")
// a la copia que Windows Installer usa como origen de un producto. Lo que apuntan esta en uso.
void readSourceLinks(Survey &survey)
{
    const QString dir = survey.installerDir + QStringLiteral("\\DerandomizedSymbolicLinksForSourceLists");
    const FileSystemOps::Kind kind = FileSystemOps::kind(dir);
    if (kind == FileSystemOps::Kind::Missing) {
        return;
    }
    if (kind != FileSystemOps::Kind::Dir) {
        failEnumeration(survey, QStringLiteral("source links"), 0);
        return;
    }
    const std::wstring pattern = (dir + QStringLiteral("\\*")).toStdWString();
    WIN32_FIND_DATAW data;
    const HANDLE find = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, 0);
    if (find == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND) {
            failEnumeration(survey, QStringLiteral("source links"), error);
        }
        return;
    }
    do {
        const QString name = QString::fromWCharArray(data.cFileName);
        if (name == QLatin1String(".") || name == QLatin1String("..")) {
            continue;
        }
        bool dangling = false;
        const QString target = linkTarget(dir + QLatin1Char('\\') + name, &dangling);
        if (target.isEmpty()) {
            if (!dangling) {
                failEnumeration(survey, QStringLiteral("source link %1").arg(name), GetLastError());
            }
            continue;
        }
        addClaim(survey.claims, target);
        ++survey.sourceLinks;
    } while (FindNextFileW(find, &data));
    const DWORD error = GetLastError();
    FindClose(find);
    if (error != ERROR_NO_MORE_FILES) {
        failEnumeration(survey, QStringLiteral("source links"), error);
    }
}

// ---- Que producto es un paquete suelto

// Un producto esta instalado (en la maquina, para este usuario o para otro). `failed`: la consulta no
// se entendio (un codigo mal formado).
bool productKnown(const QString &code, bool *failed)
{
    const std::wstring wide = code.toStdWString();
    const INSTALLSTATE state = MsiQueryProductStateW(wide.c_str());
    if (state == INSTALLSTATE_INVALIDARG) {
        *failed = true;
        return false;
    }
    return state != INSTALLSTATE_UNKNOWN;
}

// Nombre y version de un producto instalado ("Topaz Video 1.7.1"); vacio si no se puede leer desde aca.
QString installedProductName(const QString &code)
{
    const std::wstring wide = code.toStdWString();
    QString name;
    QString version;
    readText([&](wchar_t *buffer, DWORD *length) { return MsiGetProductInfoW(wide.c_str(), INSTALLPROPERTY_INSTALLEDPRODUCTNAME, buffer, length); },
             &name);
    readText([&](wchar_t *buffer, DWORD *length) { return MsiGetProductInfoW(wide.c_str(), INSTALLPROPERTY_VERSIONSTRING, buffer, length); },
             &version);
    name = name.simplified();
    version = version.simplified();
    if (name.isEmpty()) {
        return {};
    }
    return version.isEmpty() || name.contains(version) ? name : QStringLiteral("%1 %2").arg(name, version);
}

QString summaryText(MSIHANDLE handle, UINT property, bool *ok)
{
    std::wstring buffer(256, L'\0');
    DWORD length = DWORD(buffer.size());
    UINT type = 0;
    INT number = 0;
    FILETIME time{};
    UINT code = MsiSummaryInfoGetPropertyW(handle, property, &type, &number, &time, buffer.data(), &length);
    if (code == ERROR_MORE_DATA) {
        buffer.assign(size_t(length) + 2, L'\0');
        length = DWORD(buffer.size());
        code = MsiSummaryInfoGetPropertyW(handle, property, &type, &number, &time, buffer.data(), &length);
    }
    if (ok) {
        *ok = code == ERROR_SUCCESS;
    }
    if (code != ERROR_SUCCESS || type != VT_LPSTR) {
        return QString();
    }
    return QString::fromWCharArray(buffer.data(), int(qMin<size_t>(length, buffer.size()))).simplified();
}

// ProductCode, UpgradeCode, ProductName y ProductVersion de la tabla Property de un .msi, en solo lectura.
bool readProperties(const QString &path, PackageFacts &package)
{
    const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    MSIHANDLE database = 0;
    if (MsiOpenDatabaseW(native.c_str(), MSIDBOPEN_READONLY, &database) != ERROR_SUCCESS) {
        return false;
    }
    bool complete = false;
    QString name;
    QString version;
    MSIHANDLE view = 0;
    if (MsiDatabaseOpenViewW(database, L"SELECT `Property`, `Value` FROM `Property`", &view) == ERROR_SUCCESS) {
        if (MsiViewExecute(view, 0) == ERROR_SUCCESS) {
            for (;;) {
                MSIHANDLE record = 0;
                const UINT fetched = MsiViewFetch(view, &record);
                if (fetched == ERROR_NO_MORE_ITEMS) {
                    complete = true;
                    break;
                }
                if (fetched != ERROR_SUCCESS) {
                    break;
                }
                QString property;
                QString value;
                const bool read =
                    readText([&](wchar_t *buffer, DWORD *length) { return MsiRecordGetStringW(record, 1, buffer, length); }, &property)
                        == ERROR_SUCCESS
                    && readText([&](wchar_t *buffer, DWORD *length) { return MsiRecordGetStringW(record, 2, buffer, length); }, &value)
                           == ERROR_SUCCESS;
                MsiCloseHandle(record);
                if (!read) {
                    break;
                }
                if (property == QLatin1String("ProductCode")) {
                    package.productCode = value.trimmed();
                } else if (property == QLatin1String("UpgradeCode")) {
                    package.upgradeCode = value.trimmed();
                } else if (property == QLatin1String("ProductName")) {
                    name = value.simplified();
                } else if (property == QLatin1String("ProductVersion")) {
                    version = value.simplified();
                }
            }
            MsiViewClose(view);
        }
        MsiCloseHandle(view);
    }
    MsiCloseHandle(database);
    package.productName = version.isEmpty() || name.contains(version) ? name : QStringLiteral("%1 %2").arg(name, version);
    return complete && !package.productCode.isEmpty();
}

// Que producto es el paquete y si ese producto, en cualquier version, sigue instalado. Cualquier cosa
// que no se pueda leer deja `read` o `lookupFailed` en el lado seguro (el paquete no se ofrece).
void readPackage(FileFacts &file)
{
    PackageFacts &package = file.package;
    package = PackageFacts();
    package.patch = file.name.endsWith(QLatin1String(".msp"), Qt::CaseInsensitive);
    QStringList candidates;
    if (package.patch) {
        // Un parche: los productos a los que se aplica estan en el resumen ("{GUID};{GUID}").
        const std::wstring native = QDir::toNativeSeparators(file.path).toStdWString();
        MSIHANDLE handle = 0;
        if (MsiGetSummaryInformationW(0, native.c_str(), 0, &handle) != ERROR_SUCCESS) {
            return;
        }
        bool ok = false;
        const QString targets = summaryText(handle, PID_TEMPLATE, &ok);
        MsiCloseHandle(handle);
        for (const QString &target : targets.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
            if (!target.trimmed().isEmpty()) {
                package.targets.append(target.trimmed());
            }
        }
        package.read = ok && !package.targets.isEmpty();
        candidates = package.targets;
    } else {
        package.read = readProperties(file.path, package);
        if (!package.read) {
            return;
        }
        candidates.append(package.productCode);
        // Cualquier version del mismo programa (mismo UpgradeCode) instalada tambien lo esconde.
        if (!package.upgradeCode.isEmpty()) {
            const std::wstring upgrade = package.upgradeCode.toStdWString();
            for (DWORD index = 0;; ++index) {
                wchar_t related[39] = {};
                const UINT result = MsiEnumRelatedProductsW(upgrade.c_str(), 0, index, related);
                if (result == ERROR_NO_MORE_ITEMS) {
                    break;
                }
                if (result != ERROR_SUCCESS) {
                    package.lookupFailed = true;
                    break;
                }
                const QString code = QString::fromWCharArray(related);
                if (!package.installed.contains(code, Qt::CaseInsensitive)) {
                    package.installed.append(code);
                }
            }
        }
    }
    for (const QString &code : std::as_const(candidates)) {
        bool failed = false;
        if (productKnown(code, &failed) && !package.installed.contains(code, Qt::CaseInsensitive)) {
            package.installed.prepend(code);
        }
        package.lookupFailed = package.lookupFailed || failed;
    }
    for (const QString &code : std::as_const(package.installed)) {
        package.installedName = installedProductName(code);
        if (!package.installedName.isEmpty()) {
            break;
        }
    }
    if (package.installedName.isEmpty() && !package.installed.isEmpty()) {
        package.installedName = package.productName.isEmpty() ? package.installed.first() : package.productName;
    }
}

// Los .msi/.msp de primer nivel y su informacion, de una carpeta ya resuelta.
QList<FileFacts> listPackages(const QString &installerDir, bool *listed)
{
    QList<FileFacts> files;
    *listed = false;
    if (installerDir.isEmpty()) {
        return files;
    }
    const std::wstring pattern = (QDir::toNativeSeparators(installerDir) + QStringLiteral("\\*")).toStdWString();
    WIN32_FIND_DATAW data;
    const HANDLE find = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (find == INVALID_HANDLE_VALUE) {
        return files;
    }
    do {
        const QString name = QString::fromWCharArray(data.cFileName);
        if (!InstallerPackages::isPackageName(name)) {
            continue;
        }
        FileFacts file;
        file.name = name;
        file.path = QDir::toNativeSeparators(installerDir) + QLatin1Char('\\') + name;
        file.isLink = (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        file.isFile = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
        file.bytes = (qint64(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
        file.created = toSecs(data.ftCreationTime);
        file.modified = toSecs(data.ftLastWriteTime);
        // Identidad y enlaces duros, sin seguir un enlace y sin pedir acceso al contenido.
        const std::wstring native = FileSystemOps::nativePath(file.path);
        const HANDLE handle = CreateFileW(native.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                          FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle != INVALID_HANDLE_VALUE) {
            BY_HANDLE_FILE_INFORMATION info;
            if (GetFileInformationByHandle(handle, &info)) {
                file.links = info.nNumberOfLinks;
                file.id.volume = info.dwVolumeSerialNumber;
                file.id.file = (quint64(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
                file.id.valid = true;
                file.isLink = file.isLink || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
                file.isFile = file.isFile && (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
            }
            CloseHandle(handle);
        }
        files.append(file);
    } while (FindNextFileW(find, &data));
    const bool ok = GetLastError() == ERROR_NO_MORE_FILES;
    FindClose(find);
    *listed = ok;
    return files;
}

} // namespace

namespace InstallerPackages {

bool installBusy(const QString &installerDir, QString *reason)
{
    QStringList why;
    // Windows Installer crea este mutex mientras ejecuta una instalacion. Si existe (aunque no se pueda
    // abrir), hay una corriendo; con cualquier otra respuesta que "no existe", se asume que si.
    const HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, L"Global\\_MSIExecute");
    if (mutex) {
        CloseHandle(mutex);
        why << QStringLiteral("mutex");
    } else if (GetLastError() != ERROR_FILE_NOT_FOUND) {
        why << QStringLiteral("mutex?");
    }
    // Una instalacion suspendida (se corto a mitad, espera reanudarse o deshacerse).
    {
        Key inProgress;
        const LSTATUS status = inProgress.open(HKEY_LOCAL_MACHINE, kInProgressKey);
        if (status == ERROR_SUCCESS) {
            why << QStringLiteral("InProgress");
        } else if (status != ERROR_FILE_NOT_FOUND) {
            why << QStringLiteral("InProgress?");
        }
    }
    // Un reemplazo de archivos que espera el reinicio y toca la carpeta de instaladores.
    for (const wchar_t *value : {L"PendingFileRenameOperations", L"PendingFileRenameOperations2"}) {
        QStringList entries;
        if (!readMultiString(HKEY_LOCAL_MACHINE, kSessionManagerKey, value, &entries)) {
            why << QStringLiteral("PendingFileRenameOperations?");
            continue;
        }
        for (const QString &entry : std::as_const(entries)) {
            if (renameTouches(entry, installerDir)) {
                why << QStringLiteral("PendingFileRenameOperations");
                break;
            }
        }
    }
    if (reason) {
        *reason = why.join(QLatin1Char(','));
    }
    return !why.isEmpty();
}

Survey survey(Scope scope)
{
    Survey result;
    result.allUsers = scope == Scope::AllUsers;
    result.currentSid = currentUserSid();
    result.installerDir = installerDirectory();
    result.busy = installBusy(result.installerDir, &result.busyReason);
    result.files = listPackages(result.installerDir, &result.listed);

    // La API: "s-1-1-0" son todos los usuarios (sin administrador falla); nulo, la maquina y el usuario
    // actual. Cualquier falla deja la consulta incompleta.
    result.enumerated = true;
    const wchar_t *who = result.allUsers ? kEveryoneSid : nullptr;
    if (enumerateProducts(result, who)) {
        enumeratePatches(result, who);
    }
    readUserData(result);
    if (!result.installerDir.isEmpty()) {
        readSourceLinks(result);
    }
    // Que producto es cada paquete que nadie reclama (los reclamados no hace falta abrirlos).
    for (FileFacts &file : result.files) {
        if (!result.claims.paths.contains(pathKey(file.path)) && !result.claims.ids.contains(file.id)) {
            readPackage(file);
        }
    }
    if (!result.enumerated) {
        qInfo().noquote() << QStringLiteral("[InstallerPackages] consulta incompleta (%1)").arg(result.enumerationError);
    }
    return result;
}

void summary(const QString &path, QString *author, QString *subject)
{
    author->clear();
    subject->clear();
    MSIHANDLE handle = 0;
    // Sin cuenta de actualizaciones: solo lectura.
    const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    if (MsiGetSummaryInformationW(0, native.c_str(), 0, &handle) != ERROR_SUCCESS) {
        return;
    }
    *author = summaryText(handle, PID_AUTHOR, nullptr);
    *subject = summaryText(handle, PID_SUBJECT, nullptr);
    MsiCloseHandle(handle);
}

} // namespace InstallerPackages
