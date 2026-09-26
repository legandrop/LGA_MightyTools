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

// Borra un VALOR sin tocar el resto de la clave. true si se borro o no existia (ni el valor ni la
// clave); false si existia y no se pudo borrar.
bool deleteValue(HKEY root, const QString &subKey, const QString &valueName);

// Borra `subKey` SOLO si no tiene valores (ni el valor por defecto) ni subclaves. Nunca recursivo.
// true si se borro, si no existia o si no estaba vacia (no es un error: queda como esta).
bool deleteKeyIfEmpty(HKEY root, const QString &subKey);

// ---- Propiedad por contenido (regla "Registro limpio" del repo)

// Ruta del ejecutable de un comando del registro: `"C:\x\a.exe" "%1"` -> `C:\x\a.exe`; sin
// comillas, lo que va hasta el primer espacio. Tambien sirve para un icono (`"C:\x\a.exe",0`).
QString commandExecutable(const QString &command);

// Dos rutas de archivo son la misma: normalizadas (separadores, `..`) y sin distinguir mayusculas.
bool samePath(const QString &a, const QString &b);

// True si el ejecutable de `command` es exactamente `exePath` (con samePath). Un comando vacio o
// de otro exe da false.
bool commandPointsTo(const QString &command, const QString &exePath);

// Ruta nativa de ESTE exe (QCoreApplication::applicationFilePath()).
QString ownExePath();

// ---- Aviso al shell de que cambiaron las asociaciones (SHChangeNotify)

// Lo manda salvo que haya un ShellNotifySuppression vivo (el self-test con hive privado: el shell
// releeria el registro REAL mientras la prueba escribe en el hive).
void notifyAssociationsChanged();

class ShellNotifySuppression
{
public:
    ShellNotifySuppression();
    ~ShellNotifySuppression();
    ShellNotifySuppression(const ShellNotifySuppression &) = delete;
    ShellNotifySuppression &operator=(const ShellNotifySuppression &) = delete;
    // Avisos que se callaron mientras vivia alguna supresion (para que la prueba vea que el camino
    // real paso por aca).
    static int suppressedCount();
};

} // namespace RegistryHelper

#endif // MIGHTYTOOLS_REGISTRYHELPER_H
