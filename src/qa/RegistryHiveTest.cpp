#include "qa/RegistryHiveTest.h"

#include "app/ModuleRegistry.h"
#include "app/UninstallCleanup.h"
#include "modules/linkredirector/BrowserRegistration.h"
#include "modules/openinnukex/win/WinFileAssociation.h"
#include "platform/AutoStart.h"
#include "platform/win/RegistryHelper.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QThread>

#include <cstdio>
#include <vector>

#include <windows.h>

namespace {

using Check = std::function<void(bool ok, const QString &what)>;
using Snapshot = QMap<QString, QByteArray>;

const QString kHivePrefix = QStringLiteral("LGA_MightyTools_selftest_hive_");

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
class HiveSession
{
public:
    HiveSession() = default;
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
        // Los de corridas anteriores que hayan quedado (un corte a mitad de prueba).
        removeFilesStartingWith(kHivePrefix);

        const QString unique = QStringLiteral("%1_%2").arg(QCoreApplication::applicationPid()).arg(QDateTime::currentMSecsSinceEpoch());
        m_baseName = kHivePrefix + unique;
        m_path = QDir::toNativeSeparators(QDir(QDir::tempPath()).filePath(m_baseName + QStringLiteral(".dat")));
        m_marker = QStringLiteral("LGA_MightyTools_HiveMarker_") + unique;

        LONG rc = RegLoadAppKeyW(ws(m_path).c_str(), &m_hive, KEY_ALL_ACCESS, REG_PROCESS_APPKEY, 0);
        if (rc != ERROR_SUCCESS) {
            m_hive = nullptr;
            check(false, QStringLiteral("hive: RegLoadAppKey no cargo %1 (rc=%2): la prueba se corta").arg(m_path).arg(rc));
            return false;
        }
        rc = RegOpenCurrentUser(KEY_READ, &m_real);
        if (rc != ERROR_SUCCESS) {
            m_real = nullptr;
            check(false, QStringLiteral("hive: RegOpenCurrentUser fallo (rc=%1): sin forma de probar el aislamiento, la prueba se corta").arg(rc));
            return false;
        }
        // Marca escrita por el handle del hive (nunca por HKEY_CURRENT_USER).
        if (!putSz(m_hive, QString(), m_marker, QStringLiteral("hive")) || valueExists(m_real, QString(), m_marker)) {
            check(false, QStringLiteral("hive: la marca no se pudo escribir en el hive o ya estaba en el HKCU real: la prueba se corta"));
            return false;
        }

        rc = RegOverridePredefKey(HKEY_CURRENT_USER, m_hive);
        if (rc != ERROR_SUCCESS) {
            check(false, QStringLiteral("hive: RegOverridePredefKey no acepta el handle de RegLoadAppKey (rc=%1): no se escribe "
                                        "nada por HKEY_CURRENT_USER, la prueba se corta")
                             .arg(rc));
            return false;
        }
        m_overridden = true;

        // 1) Lectura (sin riesgo): HKEY_CURRENT_USER tiene que ver la marca del hive.
        if (readSz(HKEY_CURRENT_USER, QString(), m_marker) != QLatin1String("hive")) {
            check(false, QStringLiteral("aislamiento: HKEY_CURRENT_USER no ve el hive despues de RegOverridePredefKey: la prueba se corta "
                                        "sin escribir nada"));
            return false;
        }
        // 2) Escritura por HKEY_CURRENT_USER: tiene que aparecer en el hive y NO en el HKCU real.
        const QString probe = QStringLiteral("LGA_MightyTools_IsolationProbe_") + unique;
        const bool written = putSz(HKEY_CURRENT_USER, QString(), probe, QStringLiteral("probe"));
        const bool inHive = valueExists(m_hive, QString(), probe);
        const bool inReal = valueExists(m_real, QString(), probe);
        if (inReal) {
            // No deberia pasar nunca (la lectura de arriba ya lo descarto): se borra lo que se filtro.
            HKEY realWrite = nullptr;
            if (RegOpenCurrentUser(KEY_SET_VALUE, &realWrite) == ERROR_SUCCESS) {
                deleteRootValue(realWrite, probe);
                RegCloseKey(realWrite);
            }
        }
        check(written && inHive && !inReal,
              QStringLiteral("aislamiento: un valor escrito por HKEY_CURRENT_USER aparece en el hive (%1) y NO en el HKCU real (%2)")
                  .arg(inHive ? QStringLiteral("si") : QStringLiteral("no"), inReal ? QStringLiteral("si") : QStringLiteral("no")));
        if (!written || !inHive || inReal) {
            return false;
        }
        deleteRootValue(m_hive, probe);
        return true;
    }

    // Deshace la redireccion. true si HKEY_CURRENT_USER ya no ve la marca del hive.
    bool restore()
    {
        if (!m_overridden) {
            return true;
        }
        RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
        m_overridden = false;
        return !valueExists(HKEY_CURRENT_USER, QString(), m_marker);
    }

    // Deja el hive como recien abierto (solo la marca).
    bool reset()
    {
        return wipe(m_hive) && putSz(m_hive, QString(), m_marker, QStringLiteral("hive"));
    }

private:
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
    QStringList errors;
    check(WinFileAssociation::registerClasses(&errors),
          QStringLiteral("1 siembra: registro REAL de .nk (ProgID, Capabilities, RegisteredApplications, Classes\\.nk) %1")
              .arg(errors.join(QLatin1Char(' '))));
    // UserChoice/UserChoiceLatest como los deja Windows (el hash no importa: no se valida aca).
    putSz(hive, kFileExtsNk + QStringLiteral("\\UserChoice"), QStringLiteral("ProgId"), kNkProgIdName);
    putSz(hive, kFileExtsNk + QStringLiteral("\\UserChoice"), QStringLiteral("Hash"), QStringLiteral("legacy="));
    putSz(hive, kFileExtsNk + QStringLiteral("\\UserChoiceLatest\\ProgId"), QStringLiteral("ProgId"), kNkProgIdName);
    putSz(hive, kFileExtsNk + QStringLiteral("\\UserChoiceLatest"), QStringLiteral("Hash"), QStringLiteral("latest="));
    check(AutoStart::setEnabled(true), QStringLiteral("1 siembra: inicio con Windows REAL (Run)"));
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
    check(readSz(hive, kNkProgId + QStringLiteral("\\DefaultIcon")) == quoted(own) + QStringLiteral(",0"),
          QStringLiteral("1 siembra: el ProgID trae DefaultIcon con el icono de este exe (no el .ico del cliente viejo)"));

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
    QStringList errors;
    WinFileAssociation::registerClasses(&errors);
    check(readSz(hive, kRegApps, QStringLiteral("LGA_MightyTools")).isEmpty()
              && readSz(hive, kRegApps, WinFileAssociation::registeredApplicationValue()) == kNkCaps
              && readSz(hive, kNkCaps, QStringLiteral("ApplicationName")) == QLatin1String("LGA Mighty Tools (Nuke scripts)"),
          QStringLiteral("3 A1: registrar .nk migra LGA_MightyTools -> LGA_MightyTools_NukeScripts ('LGA Mighty Tools (Nuke scripts)')"));

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

} // namespace

namespace RegistryHiveTest {

void run(const Check &check)
{
    // Ningun SHChangeNotify durante la prueba: el shell releeria el registro REAL.
    const RegistryHelper::ShellNotifySuppression quiet;
    const int suppressed0 = RegistryHelper::ShellNotifySuppression::suppressedCount();
    QString path;
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
            check(session.restore(), QStringLiteral("hive: redireccion deshecha (HKEY_CURRENT_USER ya no ve el hive)"));
        }
    } // la guarda deshace la redireccion (si quedo), cierra el hive y borra sus archivos
    if (!path.isEmpty()) {
        check(!QFile::exists(path) && !QFile::exists(path + QStringLiteral(".LOG1")) && !QFile::exists(path + QStringLiteral(".LOG2")),
              QStringLiteral("hive: el archivo y sus .LOG1/.LOG2 se borraron (%1)").arg(path));
    }
    if (!isolated) {
        return; // la falla ya quedo reportada; no se probo nada mas
    }
    check(RegistryHelper::ShellNotifySuppression::suppressedCount() > suppressed0,
          QStringLiteral("sin SHChangeNotify: los %1 avisos al shell de la limpieza real se callaron")
              .arg(RegistryHelper::ShellNotifySuppression::suppressedCount() - suppressed0));
}

} // namespace RegistryHiveTest
