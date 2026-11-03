#include "platform/AppPreferences.h"

#include "platform/win/RegistryHelper.h"

// Solo lectura: RegistryHelper::readString usa RegGetValueW y subKeys abre con KEY_READ. Ninguno crea
// la clave si falta.

namespace AppPreferences {

QString userRegistryString(const QString &key, const QString &value)
{
    return RegistryHelper::readString(HKEY_CURRENT_USER, key, value);
}

QStringList userRegistrySubKeys(const QString &key)
{
    return RegistryHelper::subKeys(HKEY_CURRENT_USER, key);
}

QVariantMap readPlist(const QString &)
{
    return {};
}

} // namespace AppPreferences
