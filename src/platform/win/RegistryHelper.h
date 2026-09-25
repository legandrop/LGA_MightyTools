#ifndef MIGHTYTOOLS_REGISTRYHELPER_H
#define MIGHTYTOOLS_REGISTRYHELPER_H

#include <QString>
#include <QStringList>

#include <windows.h>

// Helper minimo sobre la Win32 Registry API.
namespace RegistryHelper {

// Lee un valor string. valueName vacio = valor default (sin nombre) de la clave.
// Devuelve QString() si no existe.
QString readString(HKEY root, const QString &subKey, const QString &valueName = QString());

// Escribe un valor string (REG_SZ). valueName vacio = valor default.
bool writeString(HKEY root, const QString &subKey, const QString &valueName, const QString &data);

// Enumera los nombres de subclaves directas de subKey.
QStringList subKeys(HKEY root, const QString &subKey);

// Borra subKey y todo su contenido recursivamente.
bool deleteTree(HKEY root, const QString &subKey);

// True si la clave existe.
bool keyExists(HKEY root, const QString &subKey);

} // namespace RegistryHelper

#endif // MIGHTYTOOLS_REGISTRYHELPER_H
