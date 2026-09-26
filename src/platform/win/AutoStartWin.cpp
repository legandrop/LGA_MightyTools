#include "platform/AutoStart.h"
#include "platform/win/RegistryHelper.h"

#include <QCoreApplication>
#include <QDir>

namespace {

const QString kRunKey = QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\Run");
const QString kValueName = QStringLiteral("LGA_MightyTools");

// Donde Task Manager > Startup guarda lo que el usuario deshabilito: un valor de Run con marca impar
// existe pero Windows no lo lanza. Se lee para el log y se borra junto con el valor de Run.
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
    // Desactivar = borrar puntualmente ESTE valor (y su marca de Task Manager), sin tocar el resto
    // de las claves, y SOLO si apunta a este exe: el de otra copia (la instalada, un build) es suyo.
    if (!RegistryHelper::commandPointsTo(storedCommand(), RegistryHelper::ownExePath())) {
        return true; // no hay nada de este exe
    }
    const bool runOk = RegistryHelper::deleteValue(HKEY_CURRENT_USER, kRunKey, kValueName);
    const bool approvedOk = RegistryHelper::deleteValue(HKEY_CURRENT_USER, kStartupApprovedKey, kValueName);
    return runOk && approvedOk;
}

bool removeIfOwned(QString *detail)
{
    const QString stored = storedCommand();
    if (stored.isEmpty()) {
        if (detail) {
            *detail = QStringLiteral("Run: sin valor");
        }
        return true;
    }
    if (!RegistryHelper::commandPointsTo(stored, RegistryHelper::ownExePath())) {
        // De otra copia: ni el valor ni su marca de Task Manager son de este exe.
        if (detail) {
            *detail = QStringLiteral("Run: apunta a otra copia, no se toca (%1)").arg(stored);
        }
        return true;
    }
    const bool ok = setEnabled(false);
    if (detail) {
        *detail = ok ? QStringLiteral("Run: borrado (%1)").arg(stored)
                     : QStringLiteral("Run: no se pudo borrar (%1)").arg(stored);
    }
    return ok;
}

Availability availability()
{
    if (runsFromDevelopmentTree()) {
        return {false, Unavailability::DevelopmentTree, QStringLiteral("Not available from a development build")};
    }
    return {true, Unavailability::None, QString()};
}

} // namespace AutoStart
