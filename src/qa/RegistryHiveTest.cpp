#include "qa/RegistryHiveTest.h"

#include "app/ModuleRegistry.h"
#include "app/SettingsStore.h"
#include "app/UninstallCleanup.h"
#include "core/AppSettings.h"
#include "modules/linkredirector/BrowserRegistration.h"
#include "modules/openinnukex/win/OldClientMigration.h"
#include "modules/openinnukex/win/WinFileAssociation.h"
#include "platform/AutoStart.h"
#include "platform/ToastActivation.h"
#include "platform/win/RegistryHelper.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QMap>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>

#include <cstdio>
#include <vector>

#include <windows.h>

namespace {

using Check = std::function<void(bool ok, const QString &what)>;
using Snapshot = QMap<QString, QByteArray>;

const QString kHivePrefix = QStringLiteral("LGA_MightyTools_selftest_hive_");
// El hive que hace de HKLM: otro prefijo, asi abrir uno nunca intenta borrar los archivos del otro.
const QString kHklmHivePrefix = QStringLiteral("LGA_MightyTools_selftest_hklm_");

// Rutas relativas a la raiz del hive (lo que la app ve como HKCU).
const QString kRun = QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\Run");
const QString kStartupApproved = QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run");
const QString kRegApps = QStringLiteral("Software\\RegisteredApplications");
const QString kStartMenuOwn = QStringLiteral("Software\\Clients\\StartMenuInternet\\LGA_MightyTools");
const QString kStartMenuCaps = kStartMenuOwn + QStringLiteral("\\Capabilities");
const QString kUrlProgId = QStringLiteral("Software\\Classes\\LGA.MightyTools.URL");
const QString kNkProgId = QStringLiteral("Software\\Classes\\LGA.NukeScript.1");
const QString kNkClass = QStringLiteral("Software\\Classes\\.nk");
const QString kNkCaps = QStringLiteral("Software\\LGA_MightyTools\\Capabilities");
const QString kAppRoot = QStringLiteral("Software\\LGA_MightyTools");
const QString kFileExtsNk = QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\.nk");
const QString kMailtoChoice = QStringLiteral("Software\\Microsoft\\Windows\\Shell\\Associations\\UrlAssociations\\mailto\\UserChoice");
const QString kOldCaps = QStringLiteral("Software\\OpenInNukeX\\Capabilities");
const QString kOldRoot = QStringLiteral("Software\\OpenInNukeX");
const QString kNkProgIdName = QStringLiteral("LGA.NukeScript.1");
// Anotacion de los avisos (ToastActivation).
const QString kAumidParent = QStringLiteral("Software\\Classes\\AppUserModelId");
const QString kAumidOwn = kAumidParent + QStringLiteral("\\LGA_MightyTools");
const QString kAumidForeign = kAumidParent + QStringLiteral("\\SomeVendor.SomeApp");
const QString kToastClsid = QStringLiteral("Software\\Classes\\CLSID\\{3AB03416-FB8F-436A-872F-5FB73C97F32C}");
const QString kToastServer = kToastClsid + QStringLiteral("\\LocalServer32");
// Cliente viejo (LGA OpenInNukeX v1.83): su clave de desinstalacion en cada vista.
const QString kOldUninstallTail =
    QStringLiteral("Microsoft\\Windows\\CurrentVersion\\Uninstall\\{B8F1A2C3-4D5E-6F78-9A0B-1C2D3E4F5678}_is1");
const QString kOldUninstall64 = QStringLiteral("Software\\") + kOldUninstallTail;
const QString kOldUninstallWow = QStringLiteral("Software\\WOW6432Node\\") + kOldUninstallTail;

std::wstring ws(const QString &s)
{
    return s.toStdWString();
}

QString quoted(const QString &path)
{
    return QLatin1Char('"') + path + QLatin1Char('"');
}

// ---- Escritura directa por el handle del hive: lo ajeno sembrado nunca pasa por HKEY_CURRENT_USER.

bool put(HKEY root, const QString &sub, const QString &name, DWORD type, const void *data, DWORD size)
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(root, ws(sub).c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &key, nullptr)
        != ERROR_SUCCESS) {
        return false;
    }
    const std::wstring n = ws(name);
    const LONG rc = RegSetValueExW(key, name.isEmpty() ? nullptr : n.c_str(), 0, type, static_cast<const BYTE *>(data), size);
    RegCloseKey(key);
    return rc == ERROR_SUCCESS;
}

bool putSz(HKEY root, const QString &sub, const QString &name, const QString &value)
{
    const std::wstring w = ws(value);
    return put(root, sub, name, REG_SZ, w.c_str(), DWORD((w.size() + 1) * sizeof(wchar_t)));
}

bool putDword(HKEY root, const QString &sub, const QString &name, DWORD value)
{
    return put(root, sub, name, REG_DWORD, &value, sizeof(value));
}

bool putBinary(HKEY root, const QString &sub, const QString &name, const QByteArray &value)
{
    return put(root, sub, name, REG_BINARY, value.constData(), DWORD(value.size()));
}

bool putNone(HKEY root, const QString &sub, const QString &name)
{
    return put(root, sub, name, REG_NONE, nullptr, 0);
}

bool putMulti(HKEY root, const QString &sub, const QString &name, const QStringList &items)
{
    std::wstring buffer;
    for (const QString &item : items) {
        buffer += ws(item);
        buffer.push_back(L'\0');
    }
    buffer.push_back(L'\0');
    return put(root, sub, name, REG_MULTI_SZ, buffer.data(), DWORD(buffer.size() * sizeof(wchar_t)));
}

bool putKey(HKEY root, const QString &sub)
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(root, ws(sub).c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &key, nullptr)
        != ERROR_SUCCESS) {
        return false;
    }
    RegCloseKey(key);
    return true;
}

bool valueExists(HKEY root, const QString &sub, const QString &name)
{
    const std::wstring s = ws(sub);
    const std::wstring n = ws(name);
    return RegGetValueW(root, sub.isEmpty() ? nullptr : s.c_str(), name.isEmpty() ? nullptr : n.c_str(), RRF_RT_ANY,
                        nullptr, nullptr, nullptr)
           == ERROR_SUCCESS;
}

QString readSz(HKEY root, const QString &sub, const QString &name = QString())
{
    return RegistryHelper::readString(root, sub, name);
}

bool deleteRootValue(HKEY root, const QString &name)
{
    const std::wstring n = ws(name);
    const LONG rc = RegDeleteValueW(root, n.c_str());
    return rc == ERROR_SUCCESS || rc == ERROR_FILE_NOT_FOUND;
}

// ---- Foto del hive: TODAS las claves (tambien las vacias) y cada valor con su tipo y sus bytes.

void snapKey(HKEY key, const QString &path, Snapshot *out)
{
    out->insert(path + QStringLiteral("\\"), QByteArrayLiteral("<clave>"));
    DWORD subCount = 0;
    DWORD maxSubName = 0;
    DWORD valueCount = 0;
    DWORD maxValueName = 0;
    DWORD maxData = 0;
    if (RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, &subCount, &maxSubName, nullptr, &valueCount, &maxValueName,
                         &maxData, nullptr, nullptr)
        != ERROR_SUCCESS) {
        out->insert(path + QStringLiteral("|<ilegible>"), QByteArray());
        return;
    }
    std::vector<wchar_t> name(maxValueName + 2);
    std::vector<BYTE> data(maxData + 2);
    for (DWORD i = 0; i < valueCount; ++i) {
        DWORD nameLen = DWORD(name.size());
        DWORD dataLen = DWORD(data.size());
        DWORD type = 0;
        if (RegEnumValueW(key, i, name.data(), &nameLen, nullptr, &type, data.data(), &dataLen) != ERROR_SUCCESS) {
            out->insert(path + QStringLiteral("|<valor ilegible %1>").arg(i), QByteArray());
            continue;
        }
        QByteArray blob = QByteArray::number(qulonglong(type)) + ':';
        blob.append(reinterpret_cast<const char *>(data.data()), int(dataLen));
        out->insert(path + QLatin1Char('|') + QString::fromWCharArray(name.data(), int(nameLen)), blob);
    }
    std::vector<wchar_t> subName(maxSubName + 2);
    for (DWORD i = 0; i < subCount; ++i) {
        DWORD len = DWORD(subName.size());
        if (RegEnumKeyExW(key, i, subName.data(), &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
            continue;
        }
        const QString child = QString::fromWCharArray(subName.data(), int(len));
        HKEY childKey = nullptr;
        if (RegOpenKeyExW(key, ws(child).c_str(), 0, KEY_READ, &childKey) == ERROR_SUCCESS) {
            snapKey(childKey, path + QLatin1Char('\\') + child, out);
            RegCloseKey(childKey);
        }
    }
}

Snapshot snapshot(HKEY hive)
{
    Snapshot s;
    snapKey(hive, QString(), &s);
    return s;
}

// Diferencias legibles para el mensaje de una falla: "-" solo antes, "+" solo despues, "~" cambio.
QString describeDiff(const Snapshot &before, const Snapshot &after)
{
    QStringList lines;
    for (auto it = before.cbegin(); it != before.cend(); ++it) {
        if (!after.contains(it.key())) {
            lines << QStringLiteral("-") + it.key();
        } else if (after.value(it.key()) != it.value()) {
            lines << QStringLiteral("~") + it.key();
        }
    }
    for (auto it = after.cbegin(); it != after.cend(); ++it) {
        if (!before.contains(it.key())) {
            lines << QStringLiteral("+") + it.key();
        }
    }
    if (lines.isEmpty()) {
        return QString();
    }
    const int total = int(lines.size());
    if (total > 8) {
        lines = lines.mid(0, 8);
        lines << QStringLiteral("... (%1 en total)").arg(total);
    }
    return QStringLiteral(" | diferencias: ") + lines.join(QStringLiteral(", "));
}

// Lo AJENO que tiene que sobrevivir intacto: parecido a lo que hay en un HKCU real alrededor de lo
// nuestro (otro navegador, otras apps, OpenWithProgids, la eleccion de http protegida por UCPD, la
// configuracion de QFileDialog que comparten todas las apps Qt) y una clave cualquiera con todos los
// tipos de valor.
bool seedForeign(HKEY hive)
{
    bool ok = true;
    const QString foreignBrowser = QStringLiteral("Software\\Clients\\StartMenuInternet\\ForeignBrowser");
    ok &= putSz(hive, foreignBrowser, QString(), QStringLiteral("Foreign Browser"));
    ok &= putSz(hive, foreignBrowser + QStringLiteral("\\shell\\open\\command"), QString(),
                QStringLiteral("\"C:\\Foreign\\browser.exe\" \"%1\""));
    ok &= putSz(hive, foreignBrowser + QStringLiteral("\\Capabilities"), QStringLiteral("ApplicationName"),
                QStringLiteral("Foreign Browser"));
    ok &= putSz(hive, QStringLiteral("Software\\Classes\\Foreign.Handler.1\\shell\\open\\command"), QString(),
                QStringLiteral("\"C:\\Foreign\\app.exe\" \"%1\""));
    ok &= putNone(hive, kNkClass + QStringLiteral("\\OpenWithProgids"), QStringLiteral("Foreign.Nk.1"));
    ok &= putSz(hive, kRegApps, QStringLiteral("ForeignApp"), QStringLiteral("Software\\ForeignVendor\\Capabilities"));
    ok &= putSz(hive, QStringLiteral("Software\\ForeignVendor\\Capabilities"), QStringLiteral("ApplicationName"),
                QStringLiteral("Foreign App"));
    ok &= putSz(hive, kRun, QStringLiteral("ForeignTray"), QStringLiteral("\"C:\\Foreign\\tray.exe\""));
    ok &= putBinary(hive, kStartupApproved, QStringLiteral("ForeignTray"), QByteArray("\x02\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 12));
    ok &= putNone(hive, kFileExtsNk + QStringLiteral("\\OpenWithProgids"), QStringLiteral("Foreign.Nk.1"));
    ok &= putDword(hive, QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\ApplicationAssociationToasts"),
                   QStringLiteral("Foreign.Nk.1_.nk"), 0);
    // La eleccion de http que apunta a NOSOTROS no se puede sembrar: UCPD (el driver que protege
    // UserChoice de http/https) bloquea la escritura por la RUTA, tambien dentro de un hive privado
    // (medido). La limpieza no la toca: ninguna ruta de UrlAssociations aparece en su codigo. Se
    // siembra la de mailto, que UCPD no protege, como testigo del mismo arbol.
    ok &= putSz(hive, kMailtoChoice, QStringLiteral("ProgId"), QStringLiteral("LGA.MightyTools.URL"));
    ok &= putSz(hive, kMailtoChoice, QStringLiteral("Hash"), QStringLiteral("abcdefgh="));
    ok &= putSz(hive, QStringLiteral("Software\\QtProject\\OrganizationDefaults\\FileDialog"), QStringLiteral("lastVisited"),
                QStringLiteral("file:///C:/Foreign"));
    const QString vendor = QStringLiteral("Software\\SomeVendor\\SomeApp");
    ok &= putDword(hive, vendor, QStringLiteral("Count"), 42);
    ok &= putMulti(hive, vendor, QStringLiteral("Recent"), {QStringLiteral("a.nk"), QStringLiteral("b.nk")});
    ok &= putBinary(hive, vendor, QStringLiteral("Blob"), QByteArray("\x00\x01\xfe\xff", 4));
    const std::wstring expand = L"%TEMP%\\x";
    ok &= put(hive, vendor, QStringLiteral("Expand"), REG_EXPAND_SZ, expand.c_str(), DWORD((expand.size() + 1) * sizeof(wchar_t)));
    ok &= putSz(hive, vendor, QString(), QStringLiteral("default of some app"));
    ok &= putKey(hive, vendor + QStringLiteral("\\EmptyChild"));
    return ok;
}

bool wipe(HKEY hive)
{
    return RegDeleteTreeW(hive, nullptr) == ERROR_SUCCESS;
}

const ModuleDescriptor *descriptorById(const QList<ModuleDescriptor> &all, const QString &id)
{
    for (const ModuleDescriptor &d : all) {
        if (d.id == id) {
            return &d;
        }
    }
    return nullptr;
}

void removeFilesStartingWith(const QString &baseName)
{
    const QDir temp(QDir::tempPath());
    const QStringList files = temp.entryList({baseName + QLatin1Char('*')}, QDir::Files | QDir::Hidden | QDir::System);
    for (const QString &file : files) {
        const QString path = temp.filePath(file);
        for (int attempt = 0; attempt < 20 && QFile::exists(path); ++attempt) {
            QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
            if (!QFile::remove(path)) {
                QThread::msleep(50);
            }
        }
    }
}

// La sesion del hive: carga, aislamiento, redireccion y su deshecho con guarda de alcance.
// `predef` es la clave que se redirige: HKEY_CURRENT_USER (hive = raiz del HKCU) o HKEY_LOCAL_MACHINE
// (hive = raiz del HKLM). La prueba de aislamiento del HKLM usa `Software` como ancla: el HKLM real
// se abre ANTES de redirigir (un handle abierto no sigue la redireccion) y la marca y la sonda van
// como valores de `Software` (en el real, escribir ahi pide administrador).
class HiveSession
{
public:
    explicit HiveSession(HKEY predef = HKEY_CURRENT_USER)
        : m_predef(predef)
        , m_isMachine(predef == HKEY_LOCAL_MACHINE)
    {
    }
    HiveSession(const HiveSession &) = delete;
    HiveSession &operator=(const HiveSession &) = delete;

    ~HiveSession()
    {
        restore();
        if (m_real) {
            RegCloseKey(m_real);
        }
        if (m_hive) {
            RegCloseKey(m_hive);
        }
        if (!m_baseName.isEmpty()) {
            removeFilesStartingWith(m_baseName);
        }
    }

    HKEY hive() const { return m_hive; }
    QString path() const { return m_path; }

    bool open(const Check &check)
    {
        const QString name = m_isMachine ? QStringLiteral("HKLM") : QStringLiteral("HKCU");
        const QString prefix = m_isMachine ? kHklmHivePrefix : kHivePrefix;
        // Los de corridas anteriores que hayan quedado (un corte a mitad de prueba).
        removeFilesStartingWith(prefix);

        const QString unique = QStringLiteral("%1_%2").arg(QCoreApplication::applicationPid()).arg(QDateTime::currentMSecsSinceEpoch());
        m_baseName = prefix + unique;
        m_path = QDir::toNativeSeparators(QDir(QDir::tempPath()).filePath(m_baseName + QStringLiteral(".dat")));
        m_marker = QStringLiteral("LGA_MightyTools_HiveMarker_") + unique;

        LONG rc = RegLoadAppKeyW(ws(m_path).c_str(), &m_hive, KEY_ALL_ACCESS, REG_PROCESS_APPKEY, 0);
        if (rc != ERROR_SUCCESS) {
            m_hive = nullptr;
            check(false, QStringLiteral("hive %1: RegLoadAppKey no cargo %2 (rc=%3): la prueba se corta").arg(name, m_path).arg(rc));
            return false;
        }
        // El real, abierto ANTES de redirigir.
        rc = m_isMachine ? RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE", 0, KEY_READ, &m_real) : RegOpenCurrentUser(KEY_READ, &m_real);
        if (rc != ERROR_SUCCESS) {
            m_real = nullptr;
            check(false, QStringLiteral("hive %1: no se pudo abrir el %1 real (rc=%2): sin forma de probar el aislamiento, la prueba se "
                                        "corta")
                             .arg(name)
                             .arg(rc));
            return false;
        }
        // Marca escrita por el handle del hive (nunca por la clave predefinida).
        if (!putSz(m_hive, anchor(), m_marker, QStringLiteral("hive")) || valueExists(m_real, QString(), m_marker)) {
            check(false, QStringLiteral("hive %1: la marca no se pudo escribir en el hive o ya estaba en el %1 real: la prueba se corta")
                             .arg(name));
            return false;
        }

        rc = RegOverridePredefKey(m_predef, m_hive);
        if (rc != ERROR_SUCCESS) {
            check(false, QStringLiteral("hive %1: RegOverridePredefKey no acepta el handle de RegLoadAppKey (rc=%2): no se escribe "
                                        "nada por la clave predefinida, la prueba se corta")
                             .arg(name)
                             .arg(rc));
            return false;
        }
        m_overridden = true;

        // 1) Lectura (sin riesgo): la clave predefinida tiene que ver la marca del hive.
        if (readSz(m_predef, anchor(), m_marker) != QLatin1String("hive")) {
            check(false, QStringLiteral("aislamiento %1: la clave predefinida no ve el hive despues de RegOverridePredefKey: la prueba "
                                        "se corta sin escribir nada")
                             .arg(name));
            return false;
        }
        // 2) Escritura por la clave predefinida: tiene que aparecer en el hive y NO en el real.
        const QString probe = QStringLiteral("LGA_MightyTools_IsolationProbe_") + unique;
        const bool written = putSz(m_predef, anchor(), probe, QStringLiteral("probe"));
        const bool inHive = valueExists(m_hive, anchor(), probe);
        const bool inReal = valueExists(m_real, QString(), probe);
        if (inReal) {
            // No deberia pasar nunca (la lectura de arriba ya lo descarto): se borra lo que se filtro.
            HKEY realWrite = nullptr;
            const LONG openRc = m_isMachine ? RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE", 0, KEY_SET_VALUE, &realWrite)
                                            : RegOpenCurrentUser(KEY_SET_VALUE, &realWrite);
            if (openRc == ERROR_SUCCESS) {
                deleteRootValue(realWrite, probe);
                RegCloseKey(realWrite);
            }
        }
        check(written && inHive && !inReal,
              QStringLiteral("aislamiento %1: un valor escrito por la clave predefinida aparece en el hive (%2) y NO en el %1 real (%3)")
                  .arg(name, inHive ? QStringLiteral("si") : QStringLiteral("no"), inReal ? QStringLiteral("si") : QStringLiteral("no")));
        if (!written || !inHive || inReal) {
            return false;
        }
        RegistryHelper::deleteValue(m_hive, anchor(), probe);
        return true;
    }

    // Deshace la redireccion. true si la clave predefinida ya no ve la marca del hive.
    bool restore()
    {
        if (!m_overridden) {
            return true;
        }
        RegOverridePredefKey(m_predef, nullptr);
        m_overridden = false;
        return !valueExists(m_predef, anchor(), m_marker);
    }

    // Deja el hive como recien abierto (solo la marca).
    bool reset()
    {
        return wipe(m_hive) && putSz(m_hive, anchor(), m_marker, QStringLiteral("hive"));
    }

private:
    QString anchor() const { return m_isMachine ? QStringLiteral("Software") : QString(); }

    HKEY m_predef = HKEY_CURRENT_USER;
    bool m_isMachine = false;
    QString m_path;
    QString m_baseName;
    QString m_marker;
    HKEY m_hive = nullptr;
    HKEY m_real = nullptr;
    bool m_overridden = false;
};

// ---- Escenarios

// 1. Una instalacion con todo prendido: navegador, .nk asociado y Run, con lo ajeno alrededor.
void scenarioOwnInstall(HiveSession &session, const Check &check)
{
    const HKEY hive = session.hive();
    const QString own = RegistryHelper::ownExePath();
    check(session.reset() && seedForeign(hive), QStringLiteral("1 siembra: lo ajeno sembrado por el handle del hive"));
    const Snapshot before = snapshot(hive);

    QString error;
    check(LinkRedirectorBrowserRegistration::registerAsBrowser(&error),
          QStringLiteral("1 siembra: registro REAL del navegador (por HKEY_CURRENT_USER redirigido) %1").arg(error));
    QList<ApplyIssue> errors;
    check(WinFileAssociation::registerClasses(&errors),
          QStringLiteral("1 siembra: registro REAL de .nk (ProgID, Capabilities, RegisteredApplications, Classes\\.nk) %1")
              .arg(applyIssuesForLog(errors)));
    // UserChoice/UserChoiceLatest como los deja Windows (el hash no importa: no se valida aca).
    putSz(hive, kFileExtsNk + QStringLiteral("\\UserChoice"), QStringLiteral("ProgId"), kNkProgIdName);
    putSz(hive, kFileExtsNk + QStringLiteral("\\UserChoice"), QStringLiteral("Hash"), QStringLiteral("legacy="));
    putSz(hive, kFileExtsNk + QStringLiteral("\\UserChoiceLatest\\ProgId"), QStringLiteral("ProgId"), kNkProgIdName);
    putSz(hive, kFileExtsNk + QStringLiteral("\\UserChoiceLatest"), QStringLiteral("Hash"), QStringLiteral("latest="));
    check(AutoStart::setEnabled(true), QStringLiteral("1 siembra: inicio con Windows REAL (Run)"));
    QString toastDetail;
    const bool toastRegistered = ToastActivation::ensureRegistered(QStringLiteral("C:\\x\\notification_icon.png"), &toastDetail);
    check(toastRegistered && ToastActivation::registeredForThisExe()
              && RegistryHelper::commandPointsTo(readSz(hive, kToastServer), own)
              && readSz(hive, kAumidOwn, QStringLiteral("DisplayName")) == QLatin1String("LGA Mighty Tools"),
          QStringLiteral("1 siembra: anotacion REAL de los avisos (AUMID + activador COM) %1").arg(toastDetail));
    const bool toastAgain = ToastActivation::ensureRegistered(QStringLiteral("C:\\x\\notification_icon.png"), &toastDetail);
    check(toastAgain && toastDetail == QLatin1String("ya estaba"),
          QStringLiteral("1 siembra: anotar de nuevo no escribe nada (%1)").arg(toastDetail));
    // Task Manager lo tenia deshabilitado.
    putBinary(hive, kStartupApproved, QStringLiteral("LGA_MightyTools"), QByteArray("\x03\x00\x00\x00\x01\x02\x03\x04\x05\x06\x07\x08", 12));

    check(RegistryHelper::keyExists(hive, kStartMenuOwn) && RegistryHelper::keyExists(hive, kUrlProgId)
              && RegistryHelper::keyExists(hive, kNkProgId) && RegistryHelper::keyExists(hive, kNkCaps)
              && readSz(hive, kRegApps, QStringLiteral("LGA_MightyTools")) == kStartMenuCaps
              && readSz(hive, kRegApps, WinFileAssociation::registeredApplicationValue()) == kNkCaps
              && readSz(hive, kNkClass) == kNkProgIdName
              && RegistryHelper::commandPointsTo(readSz(hive, kRun, QStringLiteral("LGA_MightyTools")), own),
          QStringLiteral("1 siembra: lo propio quedo en el hive con dos valores de RegisteredApplications (A1)"));
    check(WinFileAssociation::isNkAssociatedWithUs(), QStringLiteral("1 siembra: .nk asociado con este exe segun el hive"));
    check(readSz(hive, kNkProgId + QStringLiteral("\\DefaultIcon")) == WinFileAssociation::nukeScriptIcon(),
          QStringLiteral("1 siembra: el ProgID trae DefaultIcon con el documento de Nuke embebido en este exe (-101)"));

    const UninstallCleanup::Report report = UninstallCleanup::run(ModuleRegistry::all());
    check(report.failures == 0,
          QStringLiteral("1 limpieza real (--uninstall-cleanup): 0 fallas [%1]").arg(report.lines.join(QStringLiteral(" / "))));
    const Snapshot after = snapshot(hive);
    check(after == before, QStringLiteral("1 limpieza: lo propio desaparece y lo ajeno queda intacto byte a byte (%1 entradas)%2")
                               .arg(before.size())
                               .arg(describeDiff(before, after)));
    check(!RegistryHelper::keyExists(hive, kAppRoot), QStringLiteral("1 limpieza: Software\\LGA_MightyTools vacia se borra"));
    check(readSz(hive, kMailtoChoice, QStringLiteral("ProgId")) == QLatin1String("LGA.MightyTools.URL"),
          QStringLiteral("1 limpieza: un UserChoice de UrlAssociations que apunta a nosotros NO se toca"));
    check(RegistryHelper::keyExists(hive, QStringLiteral("Software\\QtProject\\OrganizationDefaults\\FileDialog")),
          QStringLiteral("1 limpieza: HKCU\\Software\\QtProject (compartida por las apps Qt) queda"));
    // Negativo de la foto: un solo byte cambiado en lo ajeno tiene que verse.
    putDword(hive, QStringLiteral("Software\\SomeVendor\\SomeApp"), QStringLiteral("Count"), 43);
    check(snapshot(hive) != before, QStringLiteral("1 negativo: la foto detecta un byte cambiado en lo ajeno"));
}

// 2. Los MISMOS nombres pero de otro: ProgID compartido con el cliente viejo, un build de
//    desarrollo, un RegisteredApplications que apunta a otras Capabilities y un comando con un
//    prefijo de este exe. Nada se borra.
void scenarioOthersOwnOurNames(HiveSession &session, const Check &check)
{
    const HKEY hive = session.hive();
    const QString own = RegistryHelper::ownExePath();
    const QString dev = QDir::toNativeSeparators(QFileInfo(own).absolutePath() + QStringLiteral("/dev_copy/LGA_MightyTools.exe"));
    const QString oldClient = QStringLiteral("C:\\Program Files\\LGA_OpenInNukeX\\OpenInNukeX.exe");
    check(!RegistryHelper::samePath(dev, own) && !RegistryHelper::commandPointsTo(quoted(own + QStringLiteral(".old")), own),
          QStringLiteral("2 rutas: el build de desarrollo y '<exe>.old' no son este exe"));

    bool ok = session.reset() && seedForeign(hive);
    ok &= putSz(hive, kNkProgId, QString(), QStringLiteral("Nuke Script File"));
    ok &= putSz(hive, kNkProgId + QStringLiteral("\\shell\\open\\command"), QString(), quoted(oldClient) + QStringLiteral(" \"%1\""));
    ok &= putSz(hive, kNkClass, QString(), kNkProgIdName);
    ok &= putSz(hive, kFileExtsNk + QStringLiteral("\\UserChoice"), QStringLiteral("ProgId"), kNkProgIdName);
    ok &= putSz(hive, kFileExtsNk + QStringLiteral("\\UserChoiceLatest\\ProgId"), QStringLiteral("ProgId"), kNkProgIdName);
    ok &= putSz(hive, kNkCaps, QStringLiteral("ApplicationIcon"), quoted(dev) + QStringLiteral(",0"));
    ok &= putSz(hive, kNkCaps + QStringLiteral("\\FileAssociations"), QStringLiteral(".nk"), kNkProgIdName);
    ok &= putSz(hive, kRegApps, WinFileAssociation::registeredApplicationValue(), QStringLiteral("Software\\OtherVendor\\Capabilities"));
    ok &= putSz(hive, kRegApps, QStringLiteral("LGA_MightyTools"), kStartMenuCaps);
    ok &= putSz(hive, kStartMenuOwn + QStringLiteral("\\shell\\open\\command"), QString(), quoted(dev) + QStringLiteral(" \"%1\""));
    ok &= putSz(hive, kStartMenuCaps, QStringLiteral("ApplicationName"), QStringLiteral("LGA Mighty Tools"));
    ok &= putSz(hive, kUrlProgId + QStringLiteral("\\shell\\open\\command"), QString(),
                quoted(own + QStringLiteral(".old")) + QStringLiteral(" \"%1\""));
    ok &= putSz(hive, kRun, QStringLiteral("LGA_MightyTools"), quoted(dev));
    ok &= putBinary(hive, kStartupApproved, QStringLiteral("LGA_MightyTools"), QByteArray("\x03\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 12));
    check(ok, QStringLiteral("2 siembra: lo de otros con nuestros nombres"));
    const Snapshot before = snapshot(hive);

    check(!WinFileAssociation::isNkAssociatedWithUs(),
          QStringLiteral("2 ProgID compartido con comando al exe viejo: no cuenta como asociado con este exe"));
    const UninstallCleanup::Report report = UninstallCleanup::run(ModuleRegistry::all());
    check(report.failures == 0, QStringLiteral("2 limpieza real: 0 fallas [%1]").arg(report.lines.join(QStringLiteral(" / "))));
    const Snapshot after = snapshot(hive);
    check(after == before,
          QStringLiteral("2 negativos: ProgID del exe viejo, Classes\\.nk y UserChoice que apuntan a el, Capabilities y Run de un build de "
                         "desarrollo, RegisteredApplications a otra Capabilities y comando '<exe>.old' sobreviven intactos%1")
              .arg(describeDiff(before, after)));

    // La casilla de Ajustes: apagar el inicio con Windows desde ESTE exe no borra el Run (ni la marca
    // de Task Manager) de otra copia.
    const bool offOk = AutoStart::setEnabled(false);
    const Snapshot afterOff = snapshot(hive);
    check(offOk && afterOff == before,
          QStringLiteral("2 AutoStart::setEnabled(false) no borra el Run ni StartupApproved de otra copia%1").arg(describeDiff(before, afterOff)));
}

// 3. A1: la migracion del valor viejo y que soltar uno nunca rompa al otro; valores colgados.
void scenarioMigrationAndCoexistence(HiveSession &session, const Check &check)
{
    const HKEY hive = session.hive();
    const QString own = RegistryHelper::ownExePath();
    const QList<ModuleDescriptor> all = ModuleRegistry::all();
    const ModuleDescriptor *onx = descriptorById(all, QStringLiteral("openInNukeX"));
    const ModuleDescriptor *lr = descriptorById(all, QStringLiteral("linkRedirector"));
    check(onx && onx->releaseSystem && lr && lr->releaseSystem, QStringLiteral("3 descriptores de Open in NukeX y Link Redirector con releaseSystem"));
    if (!onx || !onx->releaseSystem || !lr || !lr->releaseSystem) {
        return;
    }
    check(session.reset() && seedForeign(hive), QStringLiteral("3 siembra: lo ajeno"));
    const Snapshot base = snapshot(hive);

    // Lo que dejaba la version anterior: el .nk en el valor LGA_MightyTools.
    putSz(hive, kRegApps, QStringLiteral("LGA_MightyTools"), kNkCaps);
    putSz(hive, kNkCaps, QStringLiteral("ApplicationIcon"), quoted(own) + QStringLiteral(",0"));
    QList<ApplyIssue> errors;
    WinFileAssociation::registerClasses(&errors);
    check(readSz(hive, kRegApps, QStringLiteral("LGA_MightyTools")).isEmpty()
              && readSz(hive, kRegApps, WinFileAssociation::registeredApplicationValue()) == kNkCaps
              && readSz(hive, kNkCaps, QStringLiteral("ApplicationName")) == QLatin1String("Open in NukeX"),
          QStringLiteral("3 A1: registrar .nk migra LGA_MightyTools -> LGA_MightyTools_NukeScripts ('Open in NukeX')"));

    LinkRedirectorBrowserRegistration::registerAsBrowser();
    WinFileAssociation::registerClasses(&errors);
    check(readSz(hive, kRegApps, QStringLiteral("LGA_MightyTools")) == kStartMenuCaps,
          QStringLiteral("3 A1: volver a registrar .nk no toca el valor del navegador"));

    QString error;
    onx->releaseSystem(&error);
    check(readSz(hive, kRegApps, QStringLiteral("LGA_MightyTools")) == kStartMenuCaps && RegistryHelper::keyExists(hive, kStartMenuOwn)
              && RegistryHelper::keyExists(hive, kUrlProgId)
              && readSz(hive, kRegApps, WinFileAssociation::registeredApplicationValue()).isEmpty()
              && !RegistryHelper::keyExists(hive, kAppRoot) && !RegistryHelper::keyExists(hive, kNkProgId),
          QStringLiteral("3 A1/A3: soltar .nk no rompe el navegador y se lleva lo suyo"));
    lr->releaseSystem(&error);
    check(readSz(hive, kRegApps, QStringLiteral("LGA_MightyTools")).isEmpty() && !RegistryHelper::keyExists(hive, kStartMenuOwn)
              && !RegistryHelper::keyExists(hive, kUrlProgId),
          QStringLiteral("3 soltar el navegador se lleva lo suyo"));
    Snapshot after = snapshot(hive);
    check(after == base, QStringLiteral("3 los dos soltados: el hive vuelve a lo ajeno, byte a byte%1").arg(describeDiff(base, after)));

    // Valores colgados: apuntan a nuestras rutas y la clave ya no existe.
    putSz(hive, kRegApps, WinFileAssociation::registeredApplicationValue(), kNkCaps);
    putSz(hive, kRegApps, QStringLiteral("LGA_MightyTools"), kStartMenuCaps);
    const UninstallCleanup::Report report = UninstallCleanup::run(all);
    after = snapshot(hive);
    check(report.failures == 0 && after == base,
          QStringLiteral("3 valores colgados de RegisteredApplications se borran%1").arg(describeDiff(base, after)));
}

// 4. A4: restos del cliente viejo, solo si no esta instalado y su exe no existe en una unidad local
//    fija presente. Classes\LGA.NukeScript.1 nunca se toca.
// 6. La anotacion de los avisos de otro exe: una copia que existe no se toca; una de
//    LGA_MightyTools.exe que ya no existe (un build borrado) se limpia; un AUMID ajeno queda.
void scenarioNoticeRegistration(HiveSession &session, const Check &check)
{
    const HKEY hive = session.hive();
    check(session.reset() && seedForeign(hive), QStringLiteral("6 siembra: lo ajeno sembrado por el handle del hive"));
    putSz(hive, kAumidForeign, QStringLiteral("DisplayName"), QStringLiteral("Some App"));
    const auto seed = [&](const QString &exe) {
        putSz(hive, kToastServer, QString(), QStringLiteral("\"%1\" --toast-activated").arg(exe));
        putSz(hive, kAumidOwn, QStringLiteral("CustomActivator"), QStringLiteral("{3AB03416-FB8F-436A-872F-5FB73C97F32C}"));
        putSz(hive, kAumidOwn, QStringLiteral("DisplayName"), QStringLiteral("LGA Mighty Tools"));
    };
    const QString systemExe = QDir::toNativeSeparators(QStandardPaths::findExecutable(QStringLiteral("notepad.exe")));
    QString detail;

    seed(systemExe);
    const Snapshot before = snapshot(hive);
    check(!ToastActivation::registeredForThisExe(), QStringLiteral("6 de otro exe: no cuenta como anotada para este"));
    bool removed = ToastActivation::removeIfOwned(&detail);
    check(removed && snapshot(hive) == before,
          QStringLiteral("6 de otro exe que existe (%1): no se toca [%2]").arg(systemExe, detail));

    seed(QStringLiteral("C:\\LGA_selftest_missing\\LGA_MightyTools.exe"));
    removed = ToastActivation::removeIfOwned(&detail);
    check(removed && !RegistryHelper::keyExists(hive, kToastClsid)
              && !RegistryHelper::keyExists(hive, kAumidOwn) && RegistryHelper::keyExists(hive, kAumidForeign),
          QStringLiteral("6 de un LGA_MightyTools.exe que ya no existe: se borra y el AUMID ajeno queda [%1]").arg(detail));

    seed(QStringLiteral("C:\\LGA_selftest_missing\\Other.exe"));
    removed = ToastActivation::removeIfOwned(&detail);
    check(removed && RegistryHelper::keyExists(hive, kToastClsid),
          QStringLiteral("6 de otro nombre de exe que no existe: no se toca [%1]").arg(detail));
}

void scenarioOldClientLeftovers(HiveSession &session, const Check &check)
{
    const HKEY hive = session.hive();
    const QString own = RegistryHelper::ownExePath();
    const QString missing = QDir::toNativeSeparators(
        QDir::tempPath() + QStringLiteral("/LGA_MightyTools_selftest_missing_%1/OpenInNukeX.exe").arg(QCoreApplication::applicationPid()));
    const std::wstring tempRoot = ws(missing.left(3));
    const bool tempIsFixed = GetDriveTypeW(tempRoot.c_str()) == DRIVE_FIXED;

    check(session.reset() && seedForeign(hive)
              && putSz(hive, kNkProgId + QStringLiteral("\\shell\\open\\command"), QString(), quoted(missing) + QStringLiteral(" \"%1\"")),
          QStringLiteral("4 siembra: lo ajeno y el ProgID compartido"));
    const Snapshot base = snapshot(hive);
    // `icon` es el valor ApplicationIcon tal cual (el cliente viejo lo escribia `"<exe>",0`).
    const auto seedOld = [&](const QString &icon) {
        bool ok = putSz(hive, kOldCaps, QStringLiteral("ApplicationName"), QStringLiteral("LGA OpenInNukeX"));
        ok &= putSz(hive, kOldCaps, QStringLiteral("ApplicationIcon"), icon);
        ok &= putSz(hive, kOldCaps + QStringLiteral("\\FileAssociations"), QStringLiteral(".nk"), kNkProgIdName);
        ok &= putSz(hive, kRegApps, QStringLiteral("OpenInNukeX"), kOldCaps);
        return ok;
    };
    const auto dropOld = [&]() {
        RegDeleteTreeW(hive, ws(kOldRoot).c_str());
        RegistryHelper::deleteValue(hive, kRegApps, QStringLiteral("OpenInNukeX"));
    };

    if (!tempIsFixed) {
        std::printf("info A4: %%TEMP%% no esta en una unidad fija: el caso positivo no se puede probar aca\n");
    } else {
        check(seedOld(quoted(missing) + QStringLiteral(",0")), QStringLiteral("4 siembra: restos del cliente viejo con el exe ausente"));
        const Snapshot withOld = snapshot(hive);
        const QStringList kept = WinFileAssociation::removeOldClientLeftovers(/*oldClientInstalled=*/true);
        const Snapshot afterKept = snapshot(hive);
        check(kept.isEmpty() && afterKept == withOld,
              QStringLiteral("4 A4: con el cliente viejo instalado no se toca nada%1").arg(describeDiff(withOld, afterKept)));
        const QStringList removed = WinFileAssociation::removeOldClientLeftovers(/*oldClientInstalled=*/false);
        const Snapshot afterRemoved = snapshot(hive);
        check(removed.size() == 3 && afterRemoved == base,
              QStringLiteral("4 A4: sin el cliente y con su exe ausente se borran sus Capabilities, su RegisteredApplications y "
                             "Software\\OpenInNukeX; Classes\\LGA.NukeScript.1 queda (%1)%2")
                  .arg(removed.join(QStringLiteral(", ")), describeDiff(base, afterRemoved)));
        dropOld();
        if (!missing.contains(QLatin1Char(' '))) {
            // Sin comillas pero sin espacios: la ruta se lee entera y el exe falta: se borra.
            seedOld(missing + QStringLiteral(",0"));
            const QStringList unquoted = WinFileAssociation::removeOldClientLeftovers(/*oldClientInstalled=*/false);
            const Snapshot afterUnquoted = snapshot(hive);
            check(unquoted.size() == 3 && afterUnquoted == base,
                  QStringLiteral("4 A4: icono sin comillas y sin espacios con el exe ausente se borra%1")
                      .arg(describeDiff(base, afterUnquoted)));
            dropOld();
        }
    }

    // Negativos: el exe existe, esta en la red, en una unidad que no esta, el icono no es un exe, o
    // viene sin comillas y con espacios (se cortaria en "C:\Program", que parece ausente).
    QString noExe = missing;
    noExe.chop(4);
    QStringList negatives = {
        quoted(own) + QStringLiteral(",0"),
        quoted(QStringLiteral("\\\\server\\share\\LGA_OpenInNukeX\\OpenInNukeX.exe")) + QStringLiteral(",0"),
        quoted(noExe + QStringLiteral(".ico")) + QStringLiteral(",0"),
        QStringLiteral("C:\\Program Files\\LGA_OpenInNukeX_selftest_%1\\OpenInNukeX.exe,0").arg(QCoreApplication::applicationPid()),
    };
    const DWORD drives = GetLogicalDrives();
    for (wchar_t letter = L'Z'; letter >= L'D'; --letter) {
        if (!(drives & (1u << (letter - L'A')))) {
            negatives << quoted(QStringLiteral("%1:\\LGA_OpenInNukeX\\OpenInNukeX.exe").arg(QChar(letter))) + QStringLiteral(",0");
            break;
        }
    }
    for (const QString &icon : negatives) {
        seedOld(icon);
        const Snapshot withOld = snapshot(hive);
        const QStringList removed = WinFileAssociation::removeOldClientLeftovers(/*oldClientInstalled=*/false);
        const Snapshot after = snapshot(hive);
        check(removed.isEmpty() && after == withOld,
              QStringLiteral("4 A4 negativo: ApplicationIcon %1 (existe, red, unidad ausente, no es exe o sin comillas con "
                             "espacios) no borra nada%2")
                  .arg(icon, describeDiff(withOld, after)));
        dropOld();
    }
    const Snapshot end = snapshot(hive);
    check(end == base, QStringLiteral("4 el hive vuelve a lo sembrado%1").arg(describeDiff(base, end)));
}

// ---- 5. Mudanza del cliente viejo (--migrate-openinnukex / --remove-old-client), con HKCU y HKLM
//         redirigidos a hives privados y el settings.ini en una carpeta temporal.

QByteArray fileDigest(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArrayLiteral("<no existe>");
    }
    return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex();
}

class MigrationFixture
{
public:
    MigrationFixture(HiveSession &cu, HiveSession &lm, const QString &settingsFile)
        : m_cu(cu)
        , m_lm(lm)
        , m_settingsFile(settingsFile)
    {
    }

    HKEY cu() const { return m_cu.hive(); }
    HKEY lm() const { return m_lm.hive(); }
    QString settingsFile() const { return m_settingsFile; }

    // Hives como recien abiertos, con lo ajeno alrededor, y sin settings.ini.
    bool reset()
    {
        QFile::remove(m_settingsFile);
        bool ok = m_cu.reset() && seedForeign(cu()) && m_lm.reset();
        ok &= putSz(lm(), QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ForeignApp"),
                    QStringLiteral("DisplayName"), QStringLiteral("Foreign App"));
        ok &= putSz(lm(), QStringLiteral("Software\\Classes\\.nk"), QString(), QStringLiteral("Foreign.Machine.Nk"));
        return ok;
    }

    // La clave de desinstalacion del cliente viejo en la vista `where` ("HKLM", "WOW", "HKCU").
    bool seedInstalled(const QString &where, const QString &displayName = QStringLiteral("LGA OpenInNukeX version 1.83"),
                       const QString &uninstallString = QStringLiteral("\"C:\\Program Files\\LGA\\OpenInNukeX\\unins000.exe\""))
    {
        const HKEY root = where == QLatin1String("HKCU") ? cu() : lm();
        const QString key = where == QLatin1String("WOW") ? kOldUninstallWow : kOldUninstall64;
        bool ok = putSz(root, key, QStringLiteral("DisplayName"), displayName);
        ok &= putSz(root, key, QStringLiteral("UninstallString"), uninstallString);
        ok &= putSz(root, key, QStringLiteral("DisplayIcon"), QStringLiteral("C:\\Program Files\\LGA\\OpenInNukeX\\LGA_OpenInNukeX.exe"));
        ok &= putSz(root, key, QStringLiteral("InstallLocation"), QStringLiteral("C:\\Program Files\\LGA\\OpenInNukeX\\"));
        return ok;
    }

    // Lo que dejaba el cliente viejo en HKCU al asociar: el ProgID compartido con SU comando, la
    // clase de .nk y la eleccion del usuario (UserChoice) apuntando al ProgID.
    bool seedProgId(const QString &exe, const QString &userChoiceProgId = kNkProgIdName)
    {
        bool ok = putSz(cu(), kNkProgId, QString(), QStringLiteral("Nuke Script File"));
        ok &= putSz(cu(), kNkProgId + QStringLiteral("\\shell\\open\\command"), QString(), quoted(exe) + QStringLiteral(" \"%1\""));
        ok &= putSz(cu(), kNkProgId + QStringLiteral("\\DefaultIcon"), QString(),
                    QStringLiteral("\"C:\\Program Files\\LGA\\OpenInNukeX\\app_icon.ico\",0"));
        ok &= putSz(cu(), kNkClass, QString(), kNkProgIdName);
        ok &= putSz(cu(), kFileExtsNk + QStringLiteral("\\UserChoice"), QStringLiteral("ProgId"), userChoiceProgId);
        ok &= putSz(cu(), kFileExtsNk + QStringLiteral("\\UserChoice"), QStringLiteral("Hash"), QStringLiteral("legacy="));
        return ok;
    }

    // Restos del cliente viejo en HKCU (sus Capabilities con el icono al exe `exe`).
    bool seedOldCaps(const QString &exe)
    {
        bool ok = putSz(cu(), kOldCaps, QStringLiteral("ApplicationName"), QStringLiteral("LGA OpenInNukeX"));
        ok &= putSz(cu(), kOldCaps, QStringLiteral("ApplicationIcon"), quoted(exe) + QStringLiteral(",0"));
        ok &= putSz(cu(), kRegApps, QStringLiteral("OpenInNukeX"), kOldCaps);
        return ok;
    }

    QVariant setting(const QString &key) const
    {
        FileSettingsStore store;
        return store.value(key);
    }

    void setSetting(const QString &key, const QVariant &value)
    {
        FileSettingsStore store;
        store.setValue(key, value);
    }

private:
    HiveSession &m_cu;
    HiveSession &m_lm;
    QString m_settingsFile;
};

bool linesContain(const OldClientMigration::Report &report, const QString &text)
{
    for (const QString &line : report.lines) {
        if (line.contains(text)) {
            return true;
        }
    }
    return false;
}

void scenarioOldClientMigration(HiveSession &cuSession, HiveSession &lmSession, const QString &settingsFile, const Check &check)
{
    using namespace OldClientMigration;
    MigrationFixture f(cuSession, lmSession, settingsFile);
    const QString own = RegistryHelper::ownExePath();
    const QString oldExe = QStringLiteral("C:\\Program Files\\LGA\\OpenInNukeX\\LGA_OpenInNukeX.exe");
    const QString missingOld = QDir::toNativeSeparators(
        QDir::tempPath() + QStringLiteral("/LGA_MightyTools_selftest_missing_%1/LGA_OpenInNukeX.exe").arg(QCoreApplication::applicationPid()));
    const QString otherCopy =
        QDir::toNativeSeparators(QFileInfo(own).absolutePath() + QStringLiteral("/other_copy/LGA_MightyTools.exe"));
    const QString enabledKey = QStringLiteral("modules/openInNukeX/enabled");
    const QString markKey = QStringLiteral("migration/openInNukeX");
    const QString nkCommand = kNkProgId + QStringLiteral("\\shell\\open\\command");
    const Options real; // registro y settings de verdad (en los hives y la carpeta temporal)

    // Pura: el UninstallString.
    QString exe;
    QString args;
    check(splitUninstallString(QStringLiteral("\"C:\\Program Files\\LGA\\OpenInNukeX\\unins000.exe\" /LOG"), &exe, &args)
              && exe == QLatin1String("C:\\Program Files\\LGA\\OpenInNukeX\\unins000.exe") && args == QLatin1String("/LOG"),
          QStringLiteral("5 UninstallString con comillas: exe y argumentos"));
    check(splitUninstallString(QStringLiteral("C:\\Program Files\\LGA\\OpenInNukeX\\unins000.exe"), &exe, &args)
              && exe == QLatin1String("C:\\Program Files\\LGA\\OpenInNukeX\\unins000.exe") && args.isEmpty(),
          QStringLiteral("5 UninstallString sin comillas y con espacios: se corta en el .exe"));
    check(!splitUninstallString(QStringLiteral("unins000.exe"), &exe, &args) && !splitUninstallString(QStringLiteral("\"C:\\x\\a.bat\""), &exe, &args)
              && !splitUninstallString(QString(), &exe, &args),
          QStringLiteral("5 UninstallString relativo, que no es .exe o vacio: no se usa"));

    // 5a. Instalado en HKLM 64 + ProgID del viejo + UserChoice al ProgID. Instalado: sus restos quedan.
    check(f.reset() && f.seedInstalled(QStringLiteral("HKLM")) && f.seedProgId(oldExe) && f.seedOldCaps(missingOld),
          QStringLiteral("5a siembra: cliente viejo instalado (HKLM), ProgID a su exe, UserChoice y sus Capabilities"));
    const Detection d = detect();
    check(d.isInstalled() && d.installed.first().where == QLatin1String("HKLM") && d.progIdPointsToOldExe
              && d.installed.first().oldExe == oldExe,
          QStringLiteral("5a deteccion: HKLM, ProgID al exe viejo y su exe por DisplayIcon"));
    const QString userChoiceHashBefore = readSz(f.cu(), kFileExtsNk + QStringLiteral("\\UserChoice"), QStringLiteral("Hash"));
    const Snapshot lmBefore = snapshot(f.lm());
    {
        FileSettingsStore store;
        const Report r = migrate(&store, real);
        check(r.failures == 0 && r.moduleEnabledNow && r.nkTaken, QStringLiteral("5a migracion: 0 fallas, modulo prendido y .nk tomados [%1]")
                                                                      .arg(r.lines.join(QStringLiteral(" / "))));
    }
    check(f.setting(enabledKey).toBool() && f.setting(markKey).toString() == QLatin1String("migrated"),
          QStringLiteral("5a settings: modules/openInNukeX/enabled=true y la marca migration/openInNukeX=migrated"));
    check(!f.setting(QStringLiteral("app/autoStartDecided")).isValid() && readSz(f.cu(), kRun, QStringLiteral("LGA_MightyTools")).isEmpty(),
          QStringLiteral("5a sin tocar el inicio con Windows (ni Run ni app/autoStartDecided)"));
    check(RegistryHelper::commandPointsTo(readSz(f.cu(), nkCommand), own)
              && readSz(f.cu(), kNkProgId + QStringLiteral("\\DefaultIcon")) == WinFileAssociation::nukeScriptIcon()
              && readSz(f.cu(), kNkClass) == kNkProgIdName
              && readSz(f.cu(), kNkCaps, QStringLiteral("ApplicationName")) == QLatin1String("Open in NukeX")
              && readSz(f.cu(), kRegApps, WinFileAssociation::registeredApplicationValue()) == kNkCaps,
          QStringLiteral("5a toma de .nk: registerClasses completo (comando, DefaultIcon, Classes\\.nk, Capabilities, RegisteredApplications)"));
    check(readSz(f.cu(), kFileExtsNk + QStringLiteral("\\UserChoice"), QStringLiteral("ProgId")) == kNkProgIdName
              && readSz(f.cu(), kFileExtsNk + QStringLiteral("\\UserChoice"), QStringLiteral("Hash")) == userChoiceHashBefore,
          QStringLiteral("5a UserChoice intacto (no se escribe hash)"));
    check(RegistryHelper::keyExists(f.cu(), kOldCaps) && readSz(f.cu(), kRegApps, QStringLiteral("OpenInNukeX")) == kOldCaps,
          QStringLiteral("5a con el cliente viejo instalado sus Capabilities quedan"));
    const Snapshot lmAfter = snapshot(f.lm());
    check(lmAfter == lmBefore, QStringLiteral("5a el HKLM no se toca%1").arg(describeDiff(lmBefore, lmAfter)));

    // Idempotencia: segunda corrida, nada cambia (registro ni settings.ini).
    {
        const Snapshot cuFirst = snapshot(f.cu());
        const QByteArray settingsFirst = fileDigest(f.settingsFile());
        FileSettingsStore store;
        const Report r = migrate(&store, real);
        const Snapshot cuSecond = snapshot(f.cu());
        check(r.failures == 0 && !r.moduleEnabledNow && cuSecond == cuFirst && fileDigest(f.settingsFile()) == settingsFirst,
              QStringLiteral("5a idempotente: la segunda corrida no cambia el registro ni el settings.ini%1")
                  .arg(describeDiff(cuFirst, cuSecond)));
    }

    // 5b. Deteccion en cada vista (sin ProgID: se prende el modulo y el registro no cambia).
    const QStringList views = {QStringLiteral("HKLM"), QStringLiteral("WOW"), QStringLiteral("HKCU")};
    for (const QString &view : views) {
        f.reset();
        f.seedInstalled(view);
        const Snapshot cuBefore = snapshot(f.cu());
        const Snapshot lmB = snapshot(f.lm());
        const Detection dv = detect();
        const QString expected = view == QLatin1String("WOW") ? QStringLiteral("HKLM WOW6432Node") : view;
        FileSettingsStore store;
        const Report r = migrate(&store, real);
        const Snapshot cuAfter = snapshot(f.cu());
        const Snapshot lmA = snapshot(f.lm());
        check(dv.installed.size() == 1 && dv.installed.first().where == expected && f.setting(enabledKey).toBool() && !r.nkTaken
                  && cuAfter == cuBefore && lmA == lmB,
              QStringLiteral("5b instalado en %1: detectado, modulo prendido, sin ProgID no se toca el registro%2")
                  .arg(expected, describeDiff(cuBefore, cuAfter) + describeDiff(lmB, lmA)));
    }
    // DisplayName vacio (Inno puede dejar la clave vacia): no cuenta.
    f.reset();
    f.seedInstalled(QStringLiteral("HKLM"), QString());
    check(!detect().hasTrace(), QStringLiteral("5b clave de desinstalacion sin DisplayName: sin rastro"));

    // 5c. Ya desinstalado: ProgID al exe viejo que no existe y sus restos -> se toman los .nk y se
    //     borran los restos.
    f.reset();
    f.seedProgId(missingOld);
    f.seedOldCaps(missingOld);
    {
        FileSettingsStore store;
        const Report r = migrate(&store, real);
        check(r.failures == 0 && r.nkTaken && f.setting(enabledKey).toBool() && RegistryHelper::commandPointsTo(readSz(f.cu(), nkCommand), own)
                  && !RegistryHelper::keyExists(f.cu(), kOldRoot) && readSz(f.cu(), kRegApps, QStringLiteral("OpenInNukeX")).isEmpty(),
              QStringLiteral("5c desinstalado (ProgID al exe viejo ausente): .nk tomados y restos borrados [%1]")
                  .arg(r.lines.join(QStringLiteral(" / "))));
    }

    // 5d. ProgID de OTRA copia de LGA Mighty Tools (aunque no exista): nunca se toca.
    f.reset();
    f.seedInstalled(QStringLiteral("HKLM"));
    f.seedProgId(otherCopy);
    {
        const Snapshot before = snapshot(f.cu());
        FileSettingsStore store;
        const Report r = migrate(&store, real);
        const Snapshot after = snapshot(f.cu());
        check(!r.nkTaken && after == before && f.setting(enabledKey).toBool(),
              QStringLiteral("5d ProgID de otra copia de LGA Mighty Tools: intacto (el modulo igual se prende)%1").arg(describeDiff(before, after)));
    }

    // 5e. Usuario nuevo sin rastro: nada cambia y el modulo no se prende; la marca queda.
    for (const QString &command : {QString(), QStringLiteral("C:\\Program Files\\SomethingElse_selftest\\Other.exe")}) {
        f.reset();
        if (!command.isEmpty()) {
            f.seedProgId(command); // otro programa (ausente) con nuestro ProgID: no es rastro del viejo
        }
        const Snapshot cuBefore = snapshot(f.cu());
        const Snapshot lmB = snapshot(f.lm());
        FileSettingsStore store;
        const Report r = migrate(&store, real);
        const Snapshot cuAfter = snapshot(f.cu());
        const Snapshot lmA = snapshot(f.lm());
        check(r.failures == 0 && !r.moduleEnabledNow && !f.setting(enabledKey).isValid()
                  && f.setting(markKey).toString() == QLatin1String("no-trace") && cuAfter == cuBefore && lmA == lmB,
              QStringLiteral("5e sin rastro%1: nada cambia, modulo sin prender, marca no-trace%2")
                  .arg(command.isEmpty() ? QString() : QStringLiteral(" (ProgID a otro programa)"),
                       describeDiff(cuBefore, cuAfter) + describeDiff(lmB, lmA)));
    }

    // 5f. El usuario apago Open in NukeX: no se vuelve a prender y los .nk no se tocan.
    f.reset();
    f.seedInstalled(QStringLiteral("HKLM"));
    f.seedProgId(oldExe);
    f.setSetting(enabledKey, false);
    {
        const Snapshot before = snapshot(f.cu());
        FileSettingsStore store;
        const Report r = migrate(&store, real);
        const Snapshot after = snapshot(f.cu());
        check(!r.moduleEnabledNow && f.setting(enabledKey).isValid() && !f.setting(enabledKey).toBool() && !r.nkTaken && after == before
                  && f.setting(markKey).toString() == QLatin1String("migrated"),
              QStringLiteral("5f Open in NukeX apagado por el usuario: sigue apagado y el registro no cambia%1").arg(describeDiff(before, after)));
    }

    // 5g. Eleccion de .nk AJENA (UserChoice o UserChoiceLatest de otra app): no se toca nada.
    for (int latest = 0; latest < 2; ++latest) {
        f.reset();
        f.seedInstalled(QStringLiteral("HKLM"));
        f.seedProgId(oldExe, latest ? kNkProgIdName : QStringLiteral("Foreign.Nk.1"));
        if (latest) {
            putSz(f.cu(), kFileExtsNk + QStringLiteral("\\UserChoiceLatest\\ProgId"), QStringLiteral("ProgId"), QStringLiteral("Foreign.Nk.1"));
            putSz(f.cu(), kFileExtsNk + QStringLiteral("\\UserChoiceLatest"), QStringLiteral("Hash"), QStringLiteral("latest="));
        }
        const Snapshot before = snapshot(f.cu());
        FileSettingsStore store;
        const Report r = migrate(&store, real);
        const Snapshot after = snapshot(f.cu());
        check(!r.nkTaken && after == before && f.setting(enabledKey).toBool(),
              QStringLiteral("5g %1 de otra app: intacto, ProgID y Classes\\.nk sin tocar (queda Apply)%2")
                  .arg(latest ? QStringLiteral("UserChoiceLatest") : QStringLiteral("UserChoice"), describeDiff(before, after)));
    }

    // 5h. Arbol de build: el registro solo se loguea.
    f.reset();
    f.seedInstalled(QStringLiteral("HKLM"));
    f.seedProgId(missingOld);
    f.seedOldCaps(missingOld);
    {
        Options build;
        build.buildTree = true;
        const Snapshot before = snapshot(f.cu());
        const Snapshot lmB = snapshot(f.lm());
        FileSettingsStore store;
        const Report r = migrate(&store, build);
        const Snapshot after = snapshot(f.cu());
        check(!r.nkTaken && after == before && snapshot(f.lm()) == lmB && linesContain(r, QStringLiteral("arbol de build")),
              QStringLiteral("5h arbol de build: el registro no cambia, solo se loguea%1").arg(describeDiff(before, after)));
    }

    // 5i. Solo log (--dry-run o el escritorio de QA): nada se escribe, tampoco el settings.ini.
    f.reset();
    f.seedInstalled(QStringLiteral("HKLM"));
    f.seedProgId(oldExe);
    {
        Options dry;
        dry.dryRun = true;
        const Snapshot before = snapshot(f.cu());
        FileSettingsStore store;
        const Report r = migrate(&store, dry);
        const Snapshot after = snapshot(f.cu());
        check(!r.nkTaken && !r.moduleEnabledNow && after == before && !QFile::exists(f.settingsFile()),
              QStringLiteral("5i solo log: ni registro ni settings.ini%1").arg(describeDiff(before, after)));
    }

    // 5j. --remove-old-client sin permiso para lanzar (como en toda corrida automatizada): no lanza
    //     nada, deja la clave de desinstalacion y rehace la toma de .nk.
    f.reset();
    f.seedInstalled(QStringLiteral("HKLM"));
    f.seedProgId(oldExe);
    {
        Options noLaunch;
        noLaunch.launchAllowed = false;
        const Snapshot lmB = snapshot(f.lm());
        FileSettingsStore store;
        const Report r = removeOldClient(&store, noLaunch);
        check(!r.launched && r.stillInstalled && snapshot(f.lm()) == lmB && r.nkTaken && linesContain(r, QStringLiteral("(solo log) lanzaria"))
                  && RegistryHelper::commandPointsTo(readSz(f.cu(), nkCommand), own),
              QStringLiteral("5j remove-old-client sin lanzar: no lanza, el HKLM queda y los .nk se retoman [%1]")
                  .arg(r.lines.join(QStringLiteral(" / "))));
    }
    // Un UninstallString que no es un desinstalador de Inno: nunca se usa.
    f.reset();
    f.seedInstalled(QStringLiteral("HKLM"), QStringLiteral("LGA OpenInNukeX version 1.83"),
                    QStringLiteral("\"C:\\Windows\\System32\\cmd.exe\" /c echo"));
    {
        Options noLaunch;
        noLaunch.launchAllowed = false;
        FileSettingsStore store;
        const Report r = removeOldClient(&store, noLaunch);
        check(!r.launched && r.failures >= 1 && linesContain(r, QStringLiteral("no utilizable")),
              QStringLiteral("5j UninstallString que no es unins*.exe: rechazado sin lanzar nada"));
    }
    // 5k. Migrado y despues desinstalado: sin clave de desinstalacion, el ProgID ya es de este exe y
    //     quedan las Capabilities del viejo (icono a un exe que ya no existe) y su RegisteredApplications.
    //     La pasada posterior a quitarlo tiene que borrar esos restos y dejar los .nk hacia este exe.
    const auto seedMigratedThenRemoved = [&]() {
        bool ok = f.reset();
        QList<ApplyIssue> errors;
        ok &= WinFileAssociation::registerClasses(&errors); // lo que dejo la primera pasada
        ok &= putSz(f.cu(), kFileExtsNk + QStringLiteral("\\UserChoice"), QStringLiteral("ProgId"), kNkProgIdName);
        ok &= f.seedOldCaps(missingOld);
        return ok;
    };
    {
        check(seedMigratedThenRemoved(), QStringLiteral("5k siembra: migrado y desinstalado, restos del viejo en HKCU"));
        check(!detect().hasTrace(), QStringLiteral("5k sin rastro detectable (por eso la pasada necesita la marca o lo de antes)"));
        const Snapshot before = snapshot(f.cu());
        FileSettingsStore store;
        const Report plain = migrate(&store, real);
        const Snapshot afterPlain = snapshot(f.cu());
        check(!plain.nkTaken && afterPlain == before,
              QStringLiteral("5k sin marca ni deteccion previa: la migracion comun no toca nada%1").arg(describeDiff(before, afterPlain)));
    }
    // Con la marca migrated (el instalador: --remove-old-client despues de --migrate-openinnukex).
    {
        seedMigratedThenRemoved();
        f.setSetting(enabledKey, true);
        f.setSetting(markKey, QStringLiteral("migrated"));
        Options noLaunch;
        noLaunch.launchAllowed = false;
        FileSettingsStore store;
        const Report r = removeOldClient(&store, noLaunch);
        check(r.failures == 0 && !r.launched && r.nkTaken && !RegistryHelper::keyExists(f.cu(), kOldRoot)
                  && readSz(f.cu(), kRegApps, QStringLiteral("OpenInNukeX")).isEmpty()
                  && RegistryHelper::commandPointsTo(readSz(f.cu(), nkCommand), own)
                  && readSz(f.cu(), kNkClass) == kNkProgIdName
                  && readSz(f.cu(), kFileExtsNk + QStringLiteral("\\UserChoice"), QStringLiteral("ProgId")) == kNkProgIdName,
              QStringLiteral("5k con la marca migrated: restos borrados y .nk intactos hacia este exe [%1]")
                  .arg(r.lines.join(QStringLiteral(" / "))));
    }
    // Sin settings, con lo detectado antes de desinstalar (el boton del panel).
    {
        seedMigratedThenRemoved();
        const Report r = migrate(nullptr, real, /*knownOldClient=*/true);
        check(r.failures == 0 && r.nkTaken && !RegistryHelper::keyExists(f.cu(), kOldRoot)
                  && readSz(f.cu(), kRegApps, QStringLiteral("OpenInNukeX")).isEmpty()
                  && RegistryHelper::commandPointsTo(readSz(f.cu(), nkCommand), own),
              QStringLiteral("5k con lo detectado antes de desinstalar (panel): restos borrados y .nk hacia este exe"));
    }
    // La marca sola nunca prende el modulo.
    {
        seedMigratedThenRemoved();
        f.setSetting(markKey, QStringLiteral("migrated"));
        FileSettingsStore store;
        const Report r = migrate(&store, real);
        check(!r.moduleEnabledNow && !f.setting(enabledKey).isValid() && !r.nkTaken,
              QStringLiteral("5k la marca migrated sin rastro no prende el modulo (y apagado no toma los .nk)"));
    }

    std::printf("info 5: escritorio del arnes de QA detectado: %s\n", runningOnQaDesktop() ? "si" : "no");
    f.reset();
}

} // namespace

namespace RegistryHiveTest {

void run(const Check &check)
{
    // Ningun SHChangeNotify durante la prueba: el shell releeria el registro REAL.
    const RegistryHelper::ShellNotifySuppression quiet;
    const int suppressed0 = RegistryHelper::ShellNotifySuppression::suppressedCount();
    QString path;
    QString machinePath;
    bool isolated = false;
    {
        HiveSession session;
        isolated = session.open(check);
        path = session.path();
        if (isolated) {
            scenarioOwnInstall(session, check);
            scenarioOthersOwnOurNames(session, check);
            scenarioMigrationAndCoexistence(session, check);
            scenarioOldClientLeftovers(session, check);
            scenarioNoticeRegistration(session, check);

            // 5. La mudanza lee el HKLM (clave de desinstalacion del cliente viejo) y escribe el
            // settings.ini: HKLM a otro hive privado y settings.ini (y %APPDATA%) a una carpeta
            // temporal. El settings.ini real se compara byte a byte antes y despues.
            const QString realSettings =
                QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("settings.ini"));
            const QByteArray realSettingsBefore = fileDigest(realSettings);
            QTemporaryDir appData;
            const QByteArray oldAppData = qgetenv("APPDATA");
            if (appData.isValid()) {
                qputenv("APPDATA", QDir::toNativeSeparators(appData.path()).toLocal8Bit());
                const QString tempSettings = QDir(appData.path()).filePath(QStringLiteral("LGA/LGA_MightyTools/settings.ini"));
                AppSettings::useFile(tempSettings);
                {
                    HiveSession machine(HKEY_LOCAL_MACHINE);
                    const bool machineIsolated = machine.open(check);
                    machinePath = machine.path();
                    if (machineIsolated) {
                        scenarioOldClientMigration(session, machine, tempSettings, check);
                        check(machine.restore(), QStringLiteral("hive HKLM: redireccion deshecha (HKEY_LOCAL_MACHINE ya no ve el hive)"));
                    }
                } // la guarda del HKLM deshace su redireccion, cierra el hive y borra sus archivos
                AppSettings::useMemoryOnly();
                qputenv("APPDATA", oldAppData);
            } else {
                check(false, QStringLiteral("5 carpeta temporal para %APPDATA%: no se pudo crear"));
            }
            check(fileDigest(realSettings) == realSettingsBefore,
                  QStringLiteral("5 el settings.ini real no cambio (%1)").arg(realSettings));
            check(session.restore(), QStringLiteral("hive: redireccion deshecha (HKEY_CURRENT_USER ya no ve el hive)"));
        }
    } // la guarda deshace la redireccion (si quedo), cierra el hive y borra sus archivos
    for (const QString &hivePath : {path, machinePath}) {
        if (hivePath.isEmpty()) {
            continue;
        }
        check(!QFile::exists(hivePath) && !QFile::exists(hivePath + QStringLiteral(".LOG1"))
                  && !QFile::exists(hivePath + QStringLiteral(".LOG2")),
              QStringLiteral("hive: el archivo y sus .LOG1/.LOG2 se borraron (%1)").arg(hivePath));
    }
    if (!isolated) {
        return; // la falla ya quedo reportada; no se probo nada mas
    }
    check(RegistryHelper::ShellNotifySuppression::suppressedCount() > suppressed0,
          QStringLiteral("sin SHChangeNotify: los %1 avisos al shell de la limpieza real se callaron")
              .arg(RegistryHelper::ShellNotifySuppression::suppressedCount() - suppressed0));
}

} // namespace RegistryHiveTest
