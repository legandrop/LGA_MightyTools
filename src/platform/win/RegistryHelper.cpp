#include "platform/win/RegistryHelper.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>

#include <shlobj.h>

namespace {

std::wstring toW(const QString &s)
{
    return s.toStdWString();
}

// Supresiones vivas y avisos callados. Solo las usa el self-test, en el hilo principal.
int g_suppressions = 0;
int g_suppressed = 0;

} // namespace

namespace RegistryHelper {

QString readString(HKEY root, const QString &subKey, const QString &valueName)
{
    const std::wstring sub = toW(subKey);
    const std::wstring val = toW(valueName);

    DWORD type = 0;
    DWORD size = 0;
    // Primera llamada para obtener el tamano.
    LONG rc = RegGetValueW(root, sub.c_str(),
                           valueName.isEmpty() ? nullptr : val.c_str(),
                           RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND,
                           &type, nullptr, &size);
    if (rc != ERROR_SUCCESS || size == 0) {
        return QString();
    }

    std::wstring buffer(size / sizeof(wchar_t), L'\0');
    rc = RegGetValueW(root, sub.c_str(),
                      valueName.isEmpty() ? nullptr : val.c_str(),
                      RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND,
                      &type, buffer.data(), &size);
    if (rc != ERROR_SUCCESS) {
        return QString();
    }

    // Quitar el terminador nulo sobrante.
    while (!buffer.empty() && buffer.back() == L'\0') {
        buffer.pop_back();
    }
    return QString::fromStdWString(buffer);
}

bool writeString(HKEY root, const QString &subKey, const QString &valueName, const QString &data)
{
    const std::wstring sub = toW(subKey);
    HKEY hKey = nullptr;
    LONG rc = RegCreateKeyExW(root, sub.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                              KEY_WRITE, nullptr, &hKey, nullptr);
    if (rc != ERROR_SUCCESS) {
        qWarning() << "[RegistryHelper] No se pudo crear/abrir clave:" << subKey << "rc=" << rc;
        return false;
    }

    const std::wstring val = toW(valueName);
    const std::wstring dataW = toW(data);
    const DWORD bytes = static_cast<DWORD>((dataW.size() + 1) * sizeof(wchar_t));
    rc = RegSetValueExW(hKey,
                        valueName.isEmpty() ? nullptr : val.c_str(),
                        0, REG_SZ,
                        reinterpret_cast<const BYTE *>(dataW.c_str()), bytes);
    RegCloseKey(hKey);
    if (rc != ERROR_SUCCESS) {
        qWarning() << "[RegistryHelper] No se pudo escribir valor:" << subKey << valueName << "rc=" << rc;
        return false;
    }
    return true;
}

QStringList subKeys(HKEY root, const QString &subKey)
{
    QStringList result;
    const std::wstring sub = toW(subKey);
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, sub.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return result;
    }

    DWORD index = 0;
    wchar_t name[256];
    DWORD nameSize = 0;
    while (true) {
        nameSize = static_cast<DWORD>(std::size(name));
        LONG rc = RegEnumKeyExW(hKey, index, name, &nameSize, nullptr, nullptr, nullptr, nullptr);
        if (rc == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (rc == ERROR_SUCCESS) {
            result << QString::fromWCharArray(name, static_cast<int>(nameSize));
        } else {
            break;
        }
        ++index;
    }
    RegCloseKey(hKey);
    return result;
}

bool deleteTree(HKEY root, const QString &subKey)
{
    const std::wstring sub = toW(subKey);
    LONG rc = RegDeleteTreeW(root, sub.c_str());
    return rc == ERROR_SUCCESS || rc == ERROR_FILE_NOT_FOUND;
}

bool keyExists(HKEY root, const QString &subKey)
{
    const std::wstring sub = toW(subKey);
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(root, sub.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }
    return false;
}

bool deleteValue(HKEY root, const QString &subKey, const QString &valueName)
{
    const std::wstring sub = toW(subKey);
    HKEY key = nullptr;
    const LONG openRc = RegOpenKeyExW(root, sub.c_str(), 0, KEY_SET_VALUE, &key);
    if (openRc == ERROR_FILE_NOT_FOUND) {
        return true; // la clave no existe: nada que borrar
    }
    if (openRc != ERROR_SUCCESS) {
        qWarning() << "[RegistryHelper] No se pudo abrir para borrar un valor:" << subKey << "rc=" << openRc;
        return false;
    }
    const std::wstring val = toW(valueName);
    const LONG rc = RegDeleteValueW(key, valueName.isEmpty() ? nullptr : val.c_str());
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS && rc != ERROR_FILE_NOT_FOUND) {
        qWarning() << "[RegistryHelper] No se pudo borrar el valor:" << subKey << valueName << "rc=" << rc;
        return false;
    }
    return true;
}

bool deleteKeyIfEmpty(HKEY root, const QString &subKey)
{
    const std::wstring sub = toW(subKey);
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, sub.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return true; // no existe
    }
    DWORD subKeys = 0;
    DWORD values = 0;
    const LONG infoRc = RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, &subKeys, nullptr, nullptr, &values,
                                         nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(key);
    if (infoRc != ERROR_SUCCESS || subKeys != 0 || values != 0) {
        return true; // con contenido (o ilegible): se deja
    }
    // RegDeleteKeyW no es recursivo: si entre medio aparecio una subclave, falla y no borra nada.
    const LONG rc = RegDeleteKeyW(root, sub.c_str());
    return rc == ERROR_SUCCESS || rc == ERROR_FILE_NOT_FOUND;
}

QString commandExecutable(const QString &command)
{
    const QString trimmed = command.trimmed();
    if (trimmed.startsWith(QLatin1Char('"'))) {
        const int close = trimmed.indexOf(QLatin1Char('"'), 1);
        return close > 1 ? trimmed.mid(1, close - 1) : QString();
    }
    // Sin comillas: hasta el primer espacio o la coma de un icono ("C:\a.exe,0").
    int end = trimmed.size();
    const int space = trimmed.indexOf(QLatin1Char(' '));
    const int comma = trimmed.indexOf(QLatin1Char(','));
    if (space >= 0) {
        end = qMin(end, space);
    }
    if (comma >= 0) {
        end = qMin(end, comma);
    }
    return trimmed.left(end);
}

bool samePath(const QString &a, const QString &b)
{
    if (a.trimmed().isEmpty() || b.trimmed().isEmpty()) {
        return false;
    }
    const QString left = QDir::cleanPath(QDir::fromNativeSeparators(a.trimmed()));
    const QString right = QDir::cleanPath(QDir::fromNativeSeparators(b.trimmed()));
    return left.compare(right, Qt::CaseInsensitive) == 0;
}

bool commandPointsTo(const QString &command, const QString &exePath)
{
    return samePath(commandExecutable(command), exePath);
}

QString ownExePath()
{
    return QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
}

void notifyAssociationsChanged()
{
    if (g_suppressions > 0) {
        ++g_suppressed;
        return;
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

ShellNotifySuppression::ShellNotifySuppression()
{
    ++g_suppressions;
}

ShellNotifySuppression::~ShellNotifySuppression()
{
    --g_suppressions;
}

int ShellNotifySuppression::suppressedCount()
{
    return g_suppressed;
}

} // namespace RegistryHelper
