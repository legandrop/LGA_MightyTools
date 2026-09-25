#include "modules/linkredirector/BrowserDetection.h"
#include "platform/win/RegistryHelper.h"

#include <QDir>
#include <QFileInfo>

// Port de LGA_LinkRedirector src/windows/BrowserRegistry.cpp (v0.173), sin cambios de logica.

namespace {

const QString kStartMenuInternet = QStringLiteral("Software\\Clients\\StartMenuInternet");

// Extrae la ruta del .exe de un comando del registro.
// Maneja: "C:\...\app.exe" "%1"  |  "C:\...\app.exe"  |  C:\...\app.exe
QString extractExePath(const QString &command)
{
    QString cmd = command.trimmed();
    if (cmd.isEmpty()) {
        return QString();
    }
    if (cmd.startsWith(QLatin1Char('"'))) {
        const int end = cmd.indexOf(QLatin1Char('"'), 1);
        if (end > 0) {
            return cmd.mid(1, end - 1);
        }
        return cmd.mid(1);
    }
    // Sin comillas: cortar en el primer espacio (rutas sin espacios, ej. IE).
    const int space = cmd.indexOf(QLatin1Char(' '));
    return space > 0 ? cmd.left(space) : cmd;
}

// Lee la info de un navegador desde una clave de StartMenuInternet en una raiz dada.
bool readBrowser(HKEY root, const QString &keyName, DetectedBrowser &out)
{
    const QString base = kStartMenuInternet + QLatin1String("\\") + keyName;

    // Excluir Internet Explorer.
    if (keyName.compare(QLatin1String("IEXPLORE.EXE"), Qt::CaseInsensitive) == 0) {
        return false;
    }

    const QString command = RegistryHelper::readString(root, base + QLatin1String("\\shell\\open\\command"));
    const QString exePath = extractExePath(command);
    if (exePath.isEmpty() || !QFileInfo::exists(exePath)) {
        return false;
    }

    QString name = RegistryHelper::readString(root, base); // valor default = nombre visible
    if (name.isEmpty()) {
        name = RegistryHelper::readString(root, base + QLatin1String("\\Capabilities"), QStringLiteral("ApplicationName"));
    }
    if (name.isEmpty()) {
        name = QFileInfo(exePath).completeBaseName();
    }

    const QString httpProgId =
        RegistryHelper::readString(root, base + QLatin1String("\\Capabilities\\URLAssociations"), QStringLiteral("http"));

    out.name = name;
    out.exePath = QDir::fromNativeSeparators(exePath); // forward slashes para storage limpio
    out.handlerId = httpProgId;
    return true;
}

void collectFromRoot(HKEY root, QList<DetectedBrowser> &result)
{
    const QStringList keys = RegistryHelper::subKeys(root, kStartMenuInternet);
    for (const QString &keyName : keys) {
        DetectedBrowser info;
        if (!readBrowser(root, keyName, info)) {
            continue;
        }
        // Dedup por exePath (HKCU puede duplicar HKLM).
        bool dup = false;
        for (const DetectedBrowser &existing : result) {
            if (existing.exePath.compare(info.exePath, Qt::CaseInsensitive) == 0) {
                dup = true;
                break;
            }
        }
        if (!dup) {
            result << info;
        }
    }
}

} // namespace

namespace LinkRedirectorBrowsers {

QList<DetectedBrowser> installedBrowsersRaw()
{
    QList<DetectedBrowser> result;
    collectFromRoot(HKEY_LOCAL_MACHINE, result);
    collectFromRoot(HKEY_CURRENT_USER, result);
    return result;
}

} // namespace LinkRedirectorBrowsers
