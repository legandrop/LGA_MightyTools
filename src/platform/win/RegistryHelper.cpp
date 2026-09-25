#include "platform/win/RegistryHelper.h"

#include <QDebug>

namespace {

std::wstring toW(const QString &s)
{
    return s.toStdWString();
}

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

} // namespace RegistryHelper
