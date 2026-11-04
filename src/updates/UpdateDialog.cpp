#include "updates/UpdateDialog.h"
#include "core/I18n.h"

#include "ui/UiWidgets.h"

#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressDialog>
#include <QPushButton>
#include <QVBoxLayout>

QDialog *createUpdateAvailableDialog(QWidget *parent, const QString &displayName, const QString &version,
                                     const QString &currentVersion)
{
    auto *dialog = new QDialog(parent);
    dialog->setObjectName(QStringLiteral("updateDialog"));
    dialog->setWindowTitle(I18n::tr("Update Available"));
    dialog->setModal(true);

    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(6);

    auto *title = new QLabel(I18n::tr("%1 %2 is available.").arg(displayName, version), dialog);
    title->setObjectName(QStringLiteral("dialogTitle"));
    title->setWordWrap(true);
    layout->addWidget(title);

#ifdef Q_OS_MACOS
    // En macOS no hay instalador: la app se reemplaza sola y vuelve a abrir.
    const QString body = I18n::tr("You are running version %1. Updating closes Mighty Tools and opens it "
                                  "again when it is done.");
#else
    const QString body = I18n::tr("You are running version %1. Updating closes Mighty Tools "
                                  "and opens the installer.");
#endif
    auto *message = new QLabel(body.arg(currentVersion), dialog);
    message->setObjectName(QStringLiteral("caption"));
    message->setWordWrap(true);
    layout->addWidget(message);

    auto *buttons = new QHBoxLayout();
    buttons->setContentsMargins(0, 12, 0, 0);
    buttons->setSpacing(8);
    buttons->addStretch(1);
    auto *later = Ui::button(I18n::tr("Later"), QString(), QString(), dialog);
    auto *update = Ui::button(I18n::tr("Update now"), QStringLiteral("primary"), QString(), dialog);
    update->setDefault(true);
    buttons->addWidget(later);
    buttons->addWidget(update);
    layout->addLayout(buttons);

    QObject::connect(update, &QPushButton::clicked, dialog, &QDialog::accept);
    QObject::connect(later, &QPushButton::clicked, dialog, &QDialog::reject);

    dialog->setFixedWidth(380);
    return dialog;
}

QString updateProgressText(const QString &displayName, const QString &version, int retryAttempt, int attempts)
{
    const QString text = I18n::tr("Downloading %1 %2...").arg(displayName, version);
    if (retryAttempt <= 0) {
        return text;
    }
    return text + QLatin1Char('\n') + I18n::tr("Retrying (%1 of %2)…").arg(retryAttempt).arg(attempts);
}

QProgressDialog *createUpdateProgressDialog(QWidget *parent, const QString &displayName, const QString &version)
{
    auto *dialog = new QProgressDialog(updateProgressText(displayName, version, 0, 0), I18n::tr("Cancel"), 0, 0, parent);
    dialog->setWindowTitle(I18n::tr("Downloading Update"));
    dialog->setWindowModality(parent ? Qt::WindowModal : Qt::NonModal);
    dialog->setMinimumDuration(0);
    dialog->setAutoClose(false);
    dialog->setAutoReset(false);
    return dialog;
}
