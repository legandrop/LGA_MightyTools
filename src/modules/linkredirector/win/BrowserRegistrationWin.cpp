#include "modules/linkredirector/BrowserRegistration.h"
#include "modules/linkredirector/BrowserDetection.h"
#include "platform/win/RegistryHelper.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>

#include <shellapi.h>

// Port de LGA_LinkRedirector src/windows/DefaultBrowser.cpp (v0.173), con las claves renombradas
// para Mighty Tools (plan 4.6): ProgId "LGA.MightyTools.URL", clave "StartMenuInternet\LGA_MightyTools"
// en vez de "LinkRedirectorURL" / "StartMenuInternet\LinkRedirector".

namespace {

const QString kAppKey         = QStringLiteral("LGA_MightyTools");       // clave en StartMenuInternet
const QString kAppName        = QStringLiteral("LGA Mighty Tools");      // nombre visible
const QString kProgId         = QStringLiteral("LGA.MightyTools.URL");   // ProgId http/https/.htm/.html
const QString kStartMenu      = QStringLiteral("Software\\Clients\\StartMenuInternet\\LGA_MightyTools");
const QString kClasses        = QStringLiteral("Software\\Classes\\LGA.MightyTools.URL");
const QString kRegisteredApps = QStringLiteral("Software\\RegisteredApplications");
// A lo que apunta NUESTRO valor de RegisteredApplications. El `.nk` de Open in NukeX usa otro valor
// (LGA_MightyTools_NukeScripts, WinFileAssociation.cpp): cada modulo escribe y borra solo el suyo.
const QString kCapabilities   = kStartMenu + QStringLiteral("\\Capabilities");

QString exePath()
{
    return QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
}

QString openCommand()
{
    return QLatin1String("\"") + exePath() + QLatin1String("\" \"%1\"");
}

QString iconRef()
{
    return QLatin1String("\"") + exePath() + QLatin1String("\",0");
}

} // namespace

namespace LinkRedirectorBrowserRegistration {

QString ownHandlerId()
{
    return kProgId;
}

bool isOwnHandlerId(const QString &handlerId)
{
    return !handlerId.isEmpty() && handlerId.compare(kProgId, Qt::CaseInsensitive) == 0;
}

bool registerAsBrowser(QString *error)
{
    bool ok = true;
    const HKEY hk = HKEY_CURRENT_USER;

    // ----- StartMenuInternet\LGA_MightyTools -----
    ok &= RegistryHelper::writeString(hk, kStartMenu, QString(), kAppName); // nombre visible (default)
    ok &= RegistryHelper::writeString(hk, kStartMenu + QLatin1String("\\DefaultIcon"), QString(), iconRef());
    ok &= RegistryHelper::writeString(hk, kStartMenu + QLatin1String("\\shell\\open\\command"), QString(), openCommand());

    // StartMenu (ayuda a que Windows lo clasifique como navegador).
    ok &= RegistryHelper::writeString(hk, kStartMenu + QLatin1String("\\Capabilities\\StartMenu"),
                                      QStringLiteral("StartMenuInternet"), kAppKey);

    // Capabilities (para aparecer en Apps predeterminadas).
    const QString caps = kStartMenu + QLatin1String("\\Capabilities");
    ok &= RegistryHelper::writeString(hk, caps, QStringLiteral("ApplicationName"), kAppName);
    ok &= RegistryHelper::writeString(hk, caps, QStringLiteral("ApplicationDescription"),
                                      QStringLiteral("Opens each link in the right browser."));
    ok &= RegistryHelper::writeString(hk, caps, QStringLiteral("ApplicationIcon"), iconRef());

    // URLAssociations: http/https -> nuestro ProgId.
    ok &= RegistryHelper::writeString(hk, caps + QLatin1String("\\URLAssociations"), QStringLiteral("http"), kProgId);
    ok &= RegistryHelper::writeString(hk, caps + QLatin1String("\\URLAssociations"), QStringLiteral("https"), kProgId);

    // FileAssociations: .htm/.html -> nuestro ProgId (plan 4.6).
    ok &= RegistryHelper::writeString(hk, caps + QLatin1String("\\FileAssociations"), QStringLiteral(".htm"), kProgId);
    ok &= RegistryHelper::writeString(hk, caps + QLatin1String("\\FileAssociations"), QStringLiteral(".html"), kProgId);

    // ----- Classes\LGA.MightyTools.URL (el handler real) -----
    ok &= RegistryHelper::writeString(hk, kClasses, QString(), QStringLiteral("LGA Mighty Tools URL Handler"));
    ok &= RegistryHelper::writeString(hk, kClasses, QStringLiteral("FriendlyTypeName"), kAppName);
    // AppUserModelId + subclave \Application: imprescindible para que Windows muestre la app en la
    // lista de "Default apps" (sin esto el registro existe pero la app no aparece listada).
    ok &= RegistryHelper::writeString(hk, kClasses, QStringLiteral("AppUserModelId"), kAppKey);
    const QString appSub = kClasses + QLatin1String("\\Application");
    ok &= RegistryHelper::writeString(hk, appSub, QStringLiteral("AppUserModelId"), kAppKey);
    ok &= RegistryHelper::writeString(hk, appSub, QStringLiteral("ApplicationName"), kAppName);
    ok &= RegistryHelper::writeString(hk, appSub, QStringLiteral("ApplicationDescription"),
                                      QStringLiteral("Opens each link in the right browser."));
    ok &= RegistryHelper::writeString(hk, appSub, QStringLiteral("ApplicationIcon"), iconRef());
    ok &= RegistryHelper::writeString(hk, kClasses + QLatin1String("\\DefaultIcon"), QString(), iconRef());
    ok &= RegistryHelper::writeString(hk, kClasses + QLatin1String("\\shell\\open\\command"), QString(), openCommand());

    // ----- RegisteredApplications -----
    ok &= RegistryHelper::writeString(hk, kRegisteredApps, kAppKey,
                                      kStartMenu + QLatin1String("\\Capabilities"));

    // Notificar al shell que cambiaron las asociaciones (para que Windows reindexe).
    RegistryHelper::notifyAssociationsChanged();

    if (ok) {
        qInfo() << "[linkRedirector] Registrado como navegador candidato. exe:" << exePath();
    } else {
        if (error) {
            *error = QStringLiteral("Registro incompleto: algun valor de HKCU no se pudo escribir.");
        }
        qWarning() << "[linkRedirector] Registro como navegador incompleto (algun valor fallo).";
    }
    return ok;
}

bool unregisterAsBrowser(QString *error)
{
    // Propiedad por CONTENIDO (regla "Registro limpio"): se borra solo lo que apunta a ESTE exe. Otra
    // copia (un build de desarrollo, otra carpeta) usa los mismos nombres de clave; lo suyo queda.
    // Primero se evalua todo, despues se borra.
    const HKEY hk = HKEY_CURRENT_USER;
    const QString exe = exePath();
    const bool startMenuExists = RegistryHelper::keyExists(hk, kStartMenu);
    const bool startMenuOwned = RegistryHelper::commandPointsTo(
        RegistryHelper::readString(hk, kStartMenu + QLatin1String("\\shell\\open\\command")), exe);
    const bool classesOwned = RegistryHelper::commandPointsTo(
        RegistryHelper::readString(hk, kClasses + QLatin1String("\\shell\\open\\command")), exe);
    // El valor de RegisteredApplications se decide por la ruta a la que apunta: la nuestra, y que esa
    // clave sea nuestra o ya no exista (un valor colgado no le sirve a nadie).
    const QString registered = RegistryHelper::readString(hk, kRegisteredApps, kAppKey);
    const bool registeredOwned = registered.compare(kCapabilities, Qt::CaseInsensitive) == 0
                                 && (startMenuOwned || !startMenuExists);

    bool ok = true;
    if (registeredOwned) {
        ok &= RegistryHelper::deleteValue(hk, kRegisteredApps, kAppKey);
    }
    if (startMenuOwned) {
        ok &= RegistryHelper::deleteTree(hk, kStartMenu);
    }
    if (classesOwned) {
        ok &= RegistryHelper::deleteTree(hk, kClasses);
    }
    RegistryHelper::notifyAssociationsChanged();
    qInfo() << "[linkRedirector] Registro como navegador retirado. StartMenuInternet:"
            << (startMenuOwned ? "borrado" : (startMenuExists ? "de otra copia, queda" : "no estaba"))
            << "| ProgId:" << (classesOwned ? "borrado" : "no era de este exe")
            << "| RegisteredApplications:" << (registeredOwned ? "borrado" : "no era de este exe")
            << "| ok:" << ok;
    if (!ok && error) {
        *error = QStringLiteral("Could not remove all the browser registration keys.");
    }
    return ok;
}

bool isRegistered()
{
    return RegistryHelper::keyExists(HKEY_CURRENT_USER, kStartMenu);
}

QString currentDefaultHandlerId()
{
    // Windows 11 guarda la eleccion en UserChoiceLatest\ProgId y puede dejar el UserChoice legado con el
    // navegador anterior (medido: UserChoice decia LinkRedirectorURL con Mighty Tools elegido). Manda
    // Latest; el legado queda para Windows 10. Mismo criterio que la asociacion de .nk.
    const QString base = QStringLiteral("Software\\Microsoft\\Windows\\Shell\\Associations\\UrlAssociations\\http\\");
    const QString latest = RegistryHelper::readString(HKEY_CURRENT_USER, base + QStringLiteral("UserChoiceLatest\\ProgId"),
                                                      QStringLiteral("ProgId"));
    if (!latest.isEmpty()) {
        return latest;
    }
    return RegistryHelper::readString(HKEY_CURRENT_USER, base + QStringLiteral("UserChoice"), QStringLiteral("ProgId"));
}

bool isDefaultBrowser()
{
    return currentDefaultHandlerId().compare(kProgId, Qt::CaseInsensitive) == 0;
}

QString currentDefaultBrowserName()
{
    const QString handlerId = currentDefaultHandlerId();
    if (handlerId.isEmpty()) {
        return QString();
    }
    if (isOwnHandlerId(handlerId)) {
        return kAppName;
    }
    const QString name = LinkRedirectorBrowsers::nameForHandlerId(handlerId);
    return name.isEmpty() ? handlerId : name;
}

QString exePathForHandlerId(const QString &handlerId)
{
    if (handlerId.isEmpty() || isOwnHandlerId(handlerId)) {
        return QString();
    }
    for (const DetectedBrowser &browser : LinkRedirectorBrowsers::installedBrowsersRaw()) {
        if (browser.handlerId.compare(handlerId, Qt::CaseInsensitive) == 0) {
            return browser.exePath;
        }
    }
    return QString();
}

void openDefaultAppsSettings()
{
    // Deep link a la pagina ESPECIFICA de esta app dentro de Apps predeterminadas (Windows 11
    // 21H2+ con update 2023-04). El nombre es el valor de HKCU\Software\RegisteredApplications.
    const QString deepLink = QStringLiteral("ms-settings:defaultapps?registeredAppUser=") + kAppKey;
    const std::wstring w = deepLink.toStdWString();
    HINSTANCE rc = ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(rc) <= 32) {
        // Fallback: pagina generica de Apps predeterminadas.
        qWarning() << "[linkRedirector] Deep link especifico de Apps predeterminadas fallo, abriendo pagina generica.";
        ShellExecuteW(nullptr, L"open", L"ms-settings:defaultapps", nullptr, nullptr, SW_SHOWNORMAL);
    }
}

bool requestSetAsDefault()
{
    // Windows no permite setear el navegador por defecto via API: registrar y abrir el panel para
    // que el usuario elija una vez.
    registerAsBrowser();
    openDefaultAppsSettings();
    return false;
}

} // namespace LinkRedirectorBrowserRegistration
