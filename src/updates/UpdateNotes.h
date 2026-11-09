#ifndef MIGHTYTOOLS_UPDATENOTES_H
#define MIGHTYTOOLS_UPDATENOTES_H

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>

class QNetworkAccessManager;
class LgaWhatsNewFetcher;

// Las novedades ("What's new") que muestra el cartel de update: lo nuevo desde la version instalada
// hasta la ofrecida, en el idioma de la interfaz.
//
// Usa LgaWhatsNew de libs/lgaupdate (copia identica de la pieza canonica de LGA_Base_QT_C_Py): el
// asset `whats_new.json` sale del manifiesto de LGA_Updates (familia `assetLatest`), se baja de
// github.com con los reintentos de LGA, se verifica contra el SHA-256 del manifiesto y se arma en
// HTML. Con la interfaz en ingles cada item usa su `text_en` si lo trae (Docs/WhatsNew.md es
// bilingue desde la v1.53); si no, el castellano.
//
// Este header NO incluye nada de lgaupdate a proposito: lgaupdate/LgaNetRetry.h y el
// core/LgaNetRetry.h de la app son el mismo archivo con otra ruta, y un .cpp que incluyera los dos
// (UpdateService.cpp) chocaria por redefinicion. Todo lo de la biblioteca queda en UpdateNotes.cpp.
class UpdateNotes : public QObject
{
    Q_OBJECT

public:
    explicit UpdateNotes(QNetworkAccessManager *network, QObject *parent = nullptr);
    ~UpdateNotes() override;

    // Baja las notas y llama a `done` UNA vez, siempre asincrono, con el HTML del rango
    // (installed, offered] o vacio si no hay notas, no se pudieron bajar o no verificaron. Un
    // fallo nunca frena el cartel: solo sale sin novedades (y queda en el log).
    void fetch(const QByteArray &manifestJson, const QString &installedVersion, const QString &offeredVersion,
               std::function<void(const QString &html)> done);
    // Corta lo que este en curso sin llamar a `done`.
    void cancel();

    // El HTML de un `whats_new.json` ya leido, en el idioma actual de la interfaz. Vacio si el JSON
    // no es valido o el rango no trae nada. Lo usan fetch() y la captura --ui-shot.
    static QString html(const QByteArray &whatsNewJson, const QString &installedVersion,
                        const QString &offeredVersion);

private:
    QPointer<QNetworkAccessManager> m_network;
    LgaWhatsNewFetcher *m_fetcher = nullptr;
};

#endif // MIGHTYTOOLS_UPDATENOTES_H
