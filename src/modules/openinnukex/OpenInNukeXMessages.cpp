#include "modules/openinnukex/OpenInNukeXMessages.h"

#include <QtGlobal>

namespace OpenInNukeXMessages {

OpenInNukeXMessage chooseVersionFirst()
{
    return {QStringLiteral("Warning"), QStringLiteral("Choose a NukeX version first."),
            OpenInNukeXMessage::Icon::Warning};
}

OpenInNukeXMessage fileNoLongerExists()
{
    return {QStringLiteral("Error"), QStringLiteral("That file no longer exists."),
            OpenInNukeXMessage::Icon::Critical};
}

OpenInNukeXMessage notANukeExecutable()
{
    return {QStringLiteral("Warning"),
            QStringLiteral("That file does not look like a Nuke executable. Saving it anyway."),
            OpenInNukeXMessage::Icon::Warning};
}

OpenInNukeXMessage nukeVersionSaved(const QString &path)
{
    return {QStringLiteral("Nuke version saved"),
            QStringLiteral(".nk files will open with this NukeX build: %1").arg(path),
            OpenInNukeXMessage::Icon::Information};
}

OpenInNukeXMessage associationCompleted()
{
    return {QStringLiteral("Association completed"),
            QStringLiteral("Double-clicking a .nk file now opens it with LGA Mighty Tools."),
            OpenInNukeXMessage::Icon::Information};
}

OpenInNukeXMessage oneMoreStepInWindows()
{
    return {QStringLiteral("One more step in Windows"),
            QStringLiteral("LGA Mighty Tools is registered. Choose it as the app for .nk files "
                            "in the Windows dialog or in Default apps, then try a .nk file."),
            OpenInNukeXMessage::Icon::Information};
}

OpenInNukeXMessage associationFinishedWithWarnings(const QString &technicalDetails)
{
    return {QStringLiteral("Association finished with warnings"), technicalDetails,
            OpenInNukeXMessage::Icon::Warning};
}

OpenInNukeXMessage associationError(const QString &errorText)
{
    return {QStringLiteral("Error"),
            QStringLiteral("Something went wrong while associating .nk files. %1").arg(errorText),
            OpenInNukeXMessage::Icon::Critical};
}

OpenInNukeXMessage almostDoneMac()
{
    return {QStringLiteral("Almost done"),
            QStringLiteral("The app is registered, but macOS did not hand it the .nk files. "
                            "Right-click any .nk in Finder, choose Get Info, pick LGA Mighty Tools "
                            "under Open with and click Change All."),
            OpenInNukeXMessage::Icon::Information};
}

OpenInNukeXMessage runningFromBuildFolder()
{
    return {QStringLiteral("Warning"),
            QStringLiteral("This copy is running from a build folder, so associating .nk files "
                            "with it would break as soon as that folder is rebuilt. Run the "
                            "installed copy instead."),
            OpenInNukeXMessage::Icon::Warning};
}

OpenInNukeXMessage bridgeInstalled(const QString &path)
{
    return {QStringLiteral("Nuke Bridge installed"),
            QStringLiteral("The bridge is in place. Restart NukeX for it to start listening. %1").arg(path),
            OpenInNukeXMessage::Icon::Information};
}

OpenInNukeXMessage bridgeExported(const QString &path)
{
    return {QStringLiteral("Bridge files exported"),
            QStringLiteral("Follow the three steps with these files: %1").arg(path),
            OpenInNukeXMessage::Icon::Information};
}

OpenInNukeXMessage bridgeError(NukeBridge::Error error)
{
    static const QString title = QStringLiteral("Could not install the Nuke Bridge");
    switch (error) {
    case NukeBridge::Error::DirMissing:
        return {title, QStringLiteral("That folder does not exist."), OpenInNukeXMessage::Icon::Critical};
    case NukeBridge::Error::SourceRepo:
        return {title,
                QStringLiteral("That folder is the plugin's source repository, not a .nuke "
                                "folder. Installing there would overwrite the source files."),
                OpenInNukeXMessage::Icon::Critical};
    case NukeBridge::Error::PayloadMissing:
        // Texto EXACTO de la tabla del canvas (seccion 5). OJO: el canvas anota que este texto
        // "reemplaza PayloadMissing si el bridge se baja del release" — un diseno alternativo
        // donde el payload no viaja embebido. Con D-04 (el payload SI viaja embebido en el exe,
        // via nuke_plugin/OpenInNukeXBridge.qrc) esta redaccion habla de una "descarga" que en
        // realidad nunca ocurre: PayloadMissing solo puede pasar por un build incompleto (el
        // .qrc no se compilo). Se deja el texto EXACTO pedido por el encargo; queda para que el
        // supervisor confirme si prefiere adaptarlo a "build incompleto" antes de etapa 2.
        return {title, QStringLiteral("The bridge download failed its integrity check. Try again."),
                OpenInNukeXMessage::Icon::Critical};
    case NukeBridge::Error::WriteFailed:
        return {title, QStringLiteral("Could not write to that folder. Check that you have permission on it."),
                OpenInNukeXMessage::Icon::Critical};
    case NukeBridge::Error::None:
        break;
    }
    return {title, QStringLiteral("Unknown error."), OpenInNukeXMessage::Icon::Critical};
}

OpenInNukeXMessage nukeNotConfigured()
{
    return {QStringLiteral("Open in NukeX"),
            QStringLiteral("Nuke isn't set up yet. Open LGA Mighty Tools and choose a NukeX version."),
            OpenInNukeXMessage::Icon::Warning};
}

OpenInNukeXMessage nukeXPathGone(const QString &path)
{
    return {QStringLiteral("Open in NukeX"),
            QStringLiteral("The saved NukeX no longer exists: %1").arg(path),
            OpenInNukeXMessage::Icon::Warning};
}

OpenInNukeXMessage nukeXFailedToStart(const QString &errorDetail)
{
    return {QStringLiteral("Open in NukeX"),
            QStringLiteral("NukeX could not start. Error: %1").arg(errorDetail),
            OpenInNukeXMessage::Icon::Critical};
}

void report(const OpenInNukeXMessage &message)
{
    // Unico punto de salida del modulo (encargo, punto 3): etapa 2 lo conecta al sistema de
    // dialogos de la UI (un solo OK, icono Information/Warning/Critical, rutas coloreadas en su
    // propia linea — D-14) en vez de este log. Nada mas del modulo llama a qWarning/qInfo para
    // mostrarle algo al usuario: todo pasa por aca.
    switch (message.icon) {
    case OpenInNukeXMessage::Icon::Information:
        qInfo("[openInNukeX] %s: %s", qUtf8Printable(message.title), qUtf8Printable(message.text));
        break;
    case OpenInNukeXMessage::Icon::Warning:
    case OpenInNukeXMessage::Icon::Critical:
        qWarning("[openInNukeX] %s: %s", qUtf8Printable(message.title), qUtf8Printable(message.text));
        break;
    }
}

} // namespace OpenInNukeXMessages
