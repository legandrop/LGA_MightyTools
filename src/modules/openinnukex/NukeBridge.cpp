#include "modules/openinnukex/NukeBridge.h"

#include "core/LgaRegistry.h"

#include <QDir>
#include <QtGlobal>

namespace NukeBridge {

const NukePlugin::Spec &plugin()
{
    static const NukePlugin::Spec spec = {
        QStringLiteral("LGA_OpenInNukeX"),
        QStringLiteral(":/bridge"),
        {QStringLiteral("init.py"), QStringLiteral("LGA_QtAdapter_OpenInNukeX.py")},
        {QStringLiteral("init.py"), QStringLiteral("LGA_QtAdapter_OpenInNukeX.py")},
        // La 1.84 traia el keyframe de Nuke Shortcuts aca adentro (D-41: cada herramienta, su carpeta).
        {{QStringLiteral("menu.py"), QStringLiteral("LGA_KeyframeToggle")},
         {QStringLiteral("LGA_KeyframeToggle.py"), QStringLiteral("LGA_KeyframeToggle")}},
        QStringLiteral("LGA OpenInNukeX"),
    };
    return spec;
}

QString pluginFolderName()
{
    return plugin().folderName;
}

QString pluginAddPathLine()
{
    return NukePlugin::pluginAddPathLine(plugin());
}

QString bundledVersion()
{
    return NukePlugin::bundledVersion(plugin());
}

ChipState chipState(const Status &status)
{
    return NukePlugin::chipState(plugin(), status);
}

QString detectNukeDirectory()
{
    return NukePlugin::detectNukeDirectory();
}

QString currentNukeDirectory()
{
    return NukePlugin::currentNukeDirectory();
}

Status inspect(const QString &nukeDir)
{
    return NukePlugin::inspect(plugin(), nukeDir);
}

bool publishToLgaRegistry(const QString &nukeDir)
{
    return LgaRegistry::saveNukeDirectory(nukeDir);
}

Error install(const QString &nukeDir, QString *detailForLog, bool automatedRun, RegistryPublisher publisher)
{
    const Error err = NukePlugin::install(plugin(), nukeDir, detailForLog);
    if (err != Error::None) {
        return err;
    }
    // Recien cuando la instalacion salio bien se publica la carpeta: registrar una `.nuke` en la
    // que el bridge no quedo instalado le daria a las otras apps LGA una ruta inutil. En
    // automatedRun (self-test, --ui-shot, --ui-probe) NUNCA se llama a `publisher`, aunque la
    // carpeta de instalacion sea una temporal legitima: el registro compartido no tiene forma de
    // distinguir una ruta de prueba de una real, asi que la guarda es sobre automatedRun, no sobre
    // la carpeta.
    const QString clean = QDir::cleanPath(nukeDir.trimmed());
    if (automatedRun) {
        qInfo("NukeBridge: automatedRun=true, NO se publica %s en el registro LGA compartido", qUtf8Printable(clean));
    } else if (publisher) {
        publisher(clean);
    }
    return Error::None;
}

Error exportPayload(const QString &destDir, QString *detailForLog)
{
    return NukePlugin::exportPayload(plugin(), destDir, detailForLog);
}

} // namespace NukeBridge
