#include "modules/openinnukex/OpenInNukeXMessages.h"

#include "core/I18n.h"
#include "ui/Theme.h"

#include <QDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QtGlobal>

namespace {

// D-14: cada ruta de un mensaje va en su propia linea y en un solo color (el link violeta del
// Theme). Reemplaza el coloreado por carpeta, rotando de paleta, del v1.83 (Dialogs::colorizePath).
QString colorizedPath(const QString &path)
{
    return QStringLiteral("<br><span style=\"color:%1;\">%2</span>")
        .arg(QLatin1String(Theme::kLink), path.toHtmlEscaped());
}

} // namespace

namespace OpenInNukeXMessages {

OpenInNukeXMessage chooseVersionFirst()
{
    return {I18n::tr("Warning"), I18n::tr("Choose a NukeX version first."),
            OpenInNukeXMessage::Icon::Warning};
}

OpenInNukeXMessage fileNoLongerExists()
{
    return {QStringLiteral("Error"), I18n::tr("That file no longer exists."),
            OpenInNukeXMessage::Icon::Critical};
}

OpenInNukeXMessage notANukeExecutable()
{
    return {I18n::tr("Warning"),
            I18n::tr("That file does not look like a Nuke executable. Saving it anyway."),
            OpenInNukeXMessage::Icon::Warning};
}

OpenInNukeXMessage nukeVersionSaved(const QString &path)
{
    return {I18n::tr("Nuke version saved"),
            I18n::tr(".nk files will open with this NukeX build:") + colorizedPath(path),
            OpenInNukeXMessage::Icon::Information};
}

OpenInNukeXMessage associationCompleted()
{
    return {I18n::tr("Association completed"),
            I18n::tr("Double-clicking a .nk file now opens it with LGA Mighty Tools."),
            OpenInNukeXMessage::Icon::Information};
}

OpenInNukeXMessage oneMoreStepInWindows()
{
    return {I18n::tr("One more step in Windows"),
            I18n::tr("LGA Mighty Tools is registered. Choose it as the app for .nk files "
                            "in the Windows dialog or in Default apps, then try a .nk file."),
            OpenInNukeXMessage::Icon::Information};
}

QString applyIssueText(ApplyIssue issue)
{
    switch (issue) {
    case ApplyIssue::RegisterProgId:
        return I18n::tr("Could not register the ProgID.");
    case ApplyIssue::RegisterDefaultApps:
        return I18n::tr("Could not register the app in Default apps.");
    case ApplyIssue::RegisterExtension:
        return I18n::tr("Could not register the .nk extension.");
    case ApplyIssue::CleanRegistry:
        return I18n::tr("Could not clean up the registry.");
    case ApplyIssue::WriteAssociation:
        return I18n::tr("Could not write the .nk association.");
    case ApplyIssue::OpenDefaultApps:
        return I18n::tr("Could not open Windows Default apps.");
    case ApplyIssue::Unknown:
        break;
    }
    return I18n::tr("Unknown error.");
}

OpenInNukeXMessage associationFinishedWithWarnings(const QString &technicalDetails)
{
    return {I18n::tr("Association finished with warnings"), technicalDetails,
            OpenInNukeXMessage::Icon::Warning};
}

OpenInNukeXMessage associationError(const QString &errorText)
{
    return {QStringLiteral("Error"),
            I18n::tr("Something went wrong while associating .nk files. %1").arg(errorText),
            OpenInNukeXMessage::Icon::Critical};
}

OpenInNukeXMessage almostDoneMac()
{
    return {I18n::tr("Almost done"),
            I18n::tr("The app is registered, but macOS did not hand it the .nk files. "
                            "Right-click any .nk in Finder, choose Get Info, pick LGA Mighty Tools "
                            "under Open with and click Change All."),
            OpenInNukeXMessage::Icon::Information};
}

OpenInNukeXMessage runningFromBuildFolder()
{
    return {I18n::tr("Warning"),
            I18n::tr("This copy is running from a build folder, so associating .nk files "
                            "with it would break as soon as that folder is rebuilt. Run the "
                            "installed copy instead."),
            OpenInNukeXMessage::Icon::Warning};
}

OpenInNukeXMessage bridgeInstalled(const QString &path)
{
    return {I18n::tr("Nuke Bridge installed"),
            I18n::tr("The bridge is in place. Restart NukeX for it to start listening.") + colorizedPath(path),
            OpenInNukeXMessage::Icon::Information};
}

OpenInNukeXMessage bridgeExported(const QString &path)
{
    return {I18n::tr("Bridge files exported"),
            I18n::tr("Follow the three steps with these files:") + colorizedPath(path),
            OpenInNukeXMessage::Icon::Information};
}

OpenInNukeXMessage bridgeError(NukeBridge::Error error)
{
    const QString title = I18n::tr("Could not install the Nuke Bridge");
    switch (error) {
    case NukeBridge::Error::DirMissing:
        return {title, I18n::tr("That folder does not exist."), OpenInNukeXMessage::Icon::Critical};
    case NukeBridge::Error::SourceRepo:
        return {title,
                I18n::tr("That folder is the plugin's source repository, not a .nuke "
                                "folder. Installing there would overwrite the source files."),
                OpenInNukeXMessage::Icon::Critical};
    case NukeBridge::Error::PayloadMissing:
        // El texto del canvas (seccion 5) habla de una "descarga" fallida: valia para un diseno
        // alternativo donde el bridge se baja del release. Con D-04 (el payload viaja EMBEBIDO en
        // el exe via nuke_plugin/OpenInNukeXBridge.qrc) esto solo puede pasar por un build
        // incompleto (el .qrc no se compilo), nunca por una descarga. Confirmado por el
        // supervisor: se usa el original traducido en vez del texto literal del canvas.
        return {title,
                I18n::tr("This copy of LGA Mighty Tools does not carry the bridge files. "
                                "The build is incomplete: download the app again."),
                OpenInNukeXMessage::Icon::Critical};
    case NukeBridge::Error::WriteFailed:
        return {title, I18n::tr("Could not write to that folder. Check that you have permission on it."),
                OpenInNukeXMessage::Icon::Critical};
    case NukeBridge::Error::None:
        break;
    }
    return {title, I18n::tr("Unknown error."), OpenInNukeXMessage::Icon::Critical};
}

OpenInNukeXMessage nukeNotConfigured()
{
    return {QStringLiteral("Open in NukeX"),
            I18n::tr("Nuke isn't set up yet. Open LGA Mighty Tools and choose a NukeX version."),
            OpenInNukeXMessage::Icon::Warning};
}

OpenInNukeXMessage nukeXPathGone(const QString &path)
{
    return {QStringLiteral("Open in NukeX"),
            I18n::tr("The saved NukeX no longer exists:") + colorizedPath(path),
            OpenInNukeXMessage::Icon::Warning};
}

OpenInNukeXMessage nukeXFailedToStart(const QString &errorDetail)
{
    return {QStringLiteral("Open in NukeX"),
            I18n::tr("NukeX could not start. Error: %1").arg(errorDetail),
            OpenInNukeXMessage::Icon::Critical};
}

void report(const OpenInNukeXMessage &message, QWidget *parent, bool automatedRun)
{
    // Unico punto de salida del modulo (encargo, punto 3). Siempre queda el log (con el HTML de
    // colorizedPath tal cual: es una linea de debug, no la UI). En corrida automatizada NUNCA se
    // muestra nada visible.
    switch (message.icon) {
    case OpenInNukeXMessage::Icon::Information:
        qInfo("[openInNukeX] %s: %s", qUtf8Printable(message.title), qUtf8Printable(message.text));
        break;
    case OpenInNukeXMessage::Icon::Warning:
    case OpenInNukeXMessage::Icon::Critical:
        qWarning("[openInNukeX] %s: %s", qUtf8Printable(message.title), qUtf8Printable(message.text));
        break;
    }
    if (automatedRun) {
        return;
    }

    // El mismo sistema de dialogos que el updater: un QMessageBox comun, que Theme::apply()
    // estiliza globalmente (QMessageBox { background-color: @dialog }, QLabel, QPushButton) sin
    // que este modulo toque una hoja de estilo propia. Un solo boton OK, como en el origen
    // (Dialogs::info/warn/error, siempre un solo boton en esta app).
    QMessageBox box(parent);
    box.setWindowTitle(message.title);
    box.setText(message.text);
    box.setTextFormat(Qt::RichText);
    switch (message.icon) {
    case OpenInNukeXMessage::Icon::Information:
        box.setIcon(QMessageBox::Information);
        break;
    case OpenInNukeXMessage::Icon::Warning:
        box.setIcon(QMessageBox::Warning);
        break;
    case OpenInNukeXMessage::Icon::Critical:
        box.setIcon(QMessageBox::Critical);
        break;
    }
    box.setStandardButtons(QMessageBox::Ok);
    box.exec();
}

QDialog *buildLaunchNoticeWidget(QWidget *parent)
{
    // Solo CONSTRUYE el cartel (widgets + temporizador sin arrancar): showLaunchNotice() lo usa
    // para el cartel de verdad (arranca el timer y lo muestra); createCaptureWidget() lo usa para
    // la captura de QA (nunca arranca el timer ni llama a show(), como pide Module.h).
    auto *dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("NukeX Launcher"));
    // Mismo fondo que el resto de los dialogos de la app (Theme::kDialog): sin objectName
    // "updateDialog" a proposito, para no mezclar este cartel chico con el del updater en un grep.
    dialog->setStyleSheet(QStringLiteral("QDialog { background-color: %1; }").arg(Theme::color(Theme::kDialog).name()));
    dialog->setModal(false);

    auto *layout = new QVBoxLayout(dialog);
    auto *label = new QLabel(I18n::tr("No NukeX instance found, opening a new one..."), dialog);
    label->setWordWrap(true);
    layout->addWidget(label);

    auto *closeButton = new QPushButton(I18n::tr("Closing in %1 seconds").arg(3), dialog);
    closeButton->setObjectName(QStringLiteral("launcherCountdown"));
    closeButton->setEnabled(false);
    layout->addWidget(closeButton);

    // El contador vive como propiedad del dialogo (no un puntero suelto: se borra solo con el
    // dialogo via WA_DeleteOnClose).
    dialog->setProperty("secondsLeft", 3);
    auto *countdown = new QTimer(dialog);
    countdown->setObjectName(QStringLiteral("launcherCountdownTimer"));
    QObject::connect(countdown, &QTimer::timeout, dialog, [dialog, closeButton, countdown]() {
        const int secondsLeft = dialog->property("secondsLeft").toInt() - 1;
        dialog->setProperty("secondsLeft", secondsLeft);
        if (secondsLeft <= 0) {
            countdown->stop();
            dialog->accept();
            return;
        }
        closeButton->setText(secondsLeft == 1 ? I18n::tr("Closing in %1 second").arg(secondsLeft)
                                          : I18n::tr("Closing in %1 seconds").arg(secondsLeft));
    });
    return dialog;
}

void showLaunchNotice(QWidget *parent, bool automatedRun, const std::function<void()> &onClosed)
{
    // Inventario, "Cartel NukeX Launcher": no modal, cuenta regresiva 3-2-1 en el texto de un
    // boton deshabilitado, se cierra solo a los 3 segundos. En corrida automatizada no se muestra
    // nada (solo el log), pero `onClosed` igual se llama para no cambiar el flujo de quien espera.
    qInfo("[openInNukeX] NukeX Launcher: No NukeX instance found, opening a new one...");
    if (automatedRun) {
        if (onClosed) {
            onClosed();
        }
        return;
    }

    QDialog *dialog = buildLaunchNoticeWidget(parent);
    // El timer de la cuenta regresiva ya esta armado adentro de buildLaunchNoticeWidget() (con su
    // propio accept() a los 3 segundos); aca solo falta avisar a quien espera y arrancarlo.
    QObject::connect(dialog, &QDialog::finished, dialog, [onClosed](int) {
        if (onClosed) {
            onClosed();
        }
    });
    if (QTimer *countdown = dialog->findChild<QTimer *>(QStringLiteral("launcherCountdownTimer"))) {
        countdown->start(1000);
    }
    dialog->show();
}

} // namespace OpenInNukeXMessages
