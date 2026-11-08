#ifndef MIGHTYTOOLS_UPDATEDIALOG_H
#define MIGHTYTOOLS_UPDATEDIALOG_H

#include <QString>

class QDialog;
class QProgressDialog;
class QWidget;

// Dialogo "Update Available" de UpdateService, armado aparte para que --ui-shot lo pueda dibujar
// sin UpdateService ni QNetworkAccessManager. Accept = "Update now", reject = "Later".
// `notesHtml`: las novedades de UpdateNotes; con texto, van en una caja con scroll debajo del mensaje.
QDialog *createUpdateAvailableDialog(QWidget *parent, const QString &displayName, const QString &version,
                                     const QString &currentVersion, const QString &notesHtml = QString());

// Dialogo de progreso de la descarga del update (sin mostrarlo): el de UpdateService y el de --ui-shot.
QProgressDialog *createUpdateProgressDialog(QWidget *parent, const QString &displayName, const QString &version);

// Texto del dialogo de progreso. Con retryAttempt > 0, el pedido anterior fallo y se espera (o ya salio)
// el intento retryAttempt de attempts: el texto lo dice debajo.
QString updateProgressText(const QString &displayName, const QString &version, int retryAttempt, int attempts);

#endif // MIGHTYTOOLS_UPDATEDIALOG_H
