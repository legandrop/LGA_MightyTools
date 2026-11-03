#ifndef MIGHTYTOOLS_APPPREFERENCES_H
#define MIGHTYTOOLS_APPPREFERENCES_H

#include <QString>
#include <QStringList>
#include <QVariantMap>

// Preferencias de OTROS programas, solo lectura. Las usan las reglas de limpieza de Disk Space para saber
// donde puso el usuario la cache de un programa (CleanupRules, AppCacheRules).
//  - Windows (platform/win/AppPreferencesWin.cpp): HKCU con RegGetValueW / RegOpenKeyExW(KEY_READ). Nunca
//    QSettings: crea la clave al leer (regla "Registro limpio").
//  - macOS (platform/mac/AppPreferencesMac.cpp): el archivo .plist leido con QFile y convertido con
//    CoreFoundation. Nunca CFPreferences ni `defaults`: el archivo no se escribe ni se sincroniza.
namespace AppPreferences {

// Valor de texto de HKCU\<key>, valor <value>. Vacio si no existe o en mac.
QString userRegistryString(const QString &key, const QString &value);
// Nombres de las subclaves directas de HKCU\<key>. Vacio si no existe o en mac.
QStringList userRegistrySubKeys(const QString &key);
// El diccionario raiz de un .plist (XML o binario). Vacio si no se pudo leer o en Windows.
QVariantMap readPlist(const QString &path);

} // namespace AppPreferences

#endif // MIGHTYTOOLS_APPPREFERENCES_H
