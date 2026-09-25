#include "platform/AutoStart.h"
#include "platform/win/RegistryHelper.h"

#include <QCoreApplication>
#include <QDir>

namespace {

const QString kRunKey = QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\Run");
const QString kValueName = QStringLiteral("LGA_MightyTools");

// Donde Task Manager > Startup guarda lo que el usuario deshabilito. Solo se lee, para el
// diagnostico del log: un valor de Run con marca impar existe pero Windows no lo lanza.
const QString kStartupApprovedKey =
    QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run");

// Ruta del ejecutable actual entre comillas dobles (para que sobreviva a rutas con espacios), tal
// cual se escribe en el valor del registro.
QString currentExeQuoted()
{
    return QLatin1Char('"') + QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) + QLatin1Char('"');
}

} // namespace

namespace AutoStart {

QString storedCommand()
{
    return RegistryHelper::readString(HKEY_CURRENT_USER, kRunKey, kValueName);
}

bool disabledByTaskManager()
{
    const std::wstring sub = kStartupApprovedKey.toStdWString();
    const std::wstring val = kValueName.toStdWString();
    BYTE data[32] = {};
    DWORD size = sizeof(data);
    DWORD type = 0;
    const LONG rc = RegGetValueW(HKEY_CURRENT_USER, sub.c_str(), val.c_str(), RRF_RT_REG_BINARY, &type, data, &size);
    if (rc != ERROR_SUCCESS || size == 0) {
        return false; // sin marca = habilitado
    }
    // Primer byte: par (0x02, 0x06) = habilitado, impar (0x03, 0x07) = deshabilitado.
    return (data[0] & 0x01) != 0;
}

bool isEnabled()
{
    const QString stored = storedCommand();
    if (stored.isEmpty()) {
        return false;
    }
    // Habilitado solo si el valor guardado apunta al ejecutable ACTUAL y Task Manager no lo apago.
    return stored.contains(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()), Qt::CaseInsensitive)
        && !disabledByTaskManager();
}

bool setEnabled(bool enabled)
{
    if (enabled) {
        const bool ok = RegistryHelper::writeString(HKEY_CURRENT_USER, kRunKey, kValueName, currentExeQuoted());
        // Si Task Manager lo tenia deshabilitado, activar desde la app lo vuelve a habilitar: se
        // borra la marca (solo la de este valor).
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kStartupApprovedKey.toStdWString().c_str(), 0, KEY_SET_VALUE, &key)
            == ERROR_SUCCESS) {
            RegDeleteValueW(key, kValueName.toStdWString().c_str());
            RegCloseKey(key);
        }
        return ok;
    }
    // Desactivar = borrar puntualmente ESTE valor, sin tocar el resto de la clave Run.
    const std::wstring sub = kRunKey.toStdWString();
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, sub.c_str(), 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) {
        return true; // no existe la clave: nada que borrar
    }
    const std::wstring val = kValueName.toStdWString();
    const LONG rc = RegDeleteValueW(hKey, val.c_str());
    RegCloseKey(hKey);
    return rc == ERROR_SUCCESS || rc == ERROR_FILE_NOT_FOUND;
}

Availability availability()
{
    if (runsFromDevelopmentTree()) {
        return {false, Unavailability::DevelopmentTree, QStringLiteral("Not available from a development build")};
    }
    return {true, Unavailability::None, QString()};
}

} // namespace AutoStart
