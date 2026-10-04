#ifndef MIGHTYTOOLS_UPDATESERVICE_H
#define MIGHTYTOOLS_UPDATESERVICE_H

#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QProgressDialog;
class QSaveFile;
class QCryptographicHash;
class QTimer;
class QWidget;
class SettingsStore;

// Auto-update de LGA_MightyTools: chequea un manifiesto estatico publicado por
// GitHub Pages (legandrop/LGA_Updates), ofrece bajar el paquete de esta plataforma si hay una
// version mas nueva, verifica su SHA-256 y lo instala (platform/UpdateInstaller.h: en Windows
// lanza el instalador, en macOS reemplaza el bundle).
//
// Version SIMPLIFICADA de FM_UpdateService (LGA_FileManagerS3): esta app no tiene
// skip de version ni helper de cierre de procesos, asi que ese estado y esas ramas no
// existen aca. "Later" pospone el chequeo automatico 1 dia (`updates/snoozeUntil` en
// el SettingsStore), como "Remind me later" en las demas apps LGA. El chequeo
// automatico corre al arrancar y se repite cada 3 horas mientras la app sigue abierta.
// Lo que SI se porta tal cual son las decisiones finas: comparacion de version via
// VersionCompare, verificacion de digest, descarga por streaming a QSaveFile, y
// manejo de errores HTTP con detalle crudo.
class UpdateService : public QObject
{
    Q_OBJECT

public:
    explicit UpdateService(QWidget *parentWindow, QObject *parent = nullptr);
    ~UpdateService() override;

    // Programa el chequeo automatico una sola vez (llamadas siguientes no hacen nada) y, despues,
    // lo repite cada 3 horas mientras la app siga abierta.
    void scheduleAutomaticCheck();

    // Donde se guarda el "Later" (sin store, el snooze vive solo en memoria).
    void setSettingsStore(SettingsStore *store) { m_store = store; }
    // Si el chequeo automatico sigue prendido: lo consulta cada chequeo periodico, asi apagarlo
    // en General vale tambien para la sesion en curso.
    void setAutomaticChecksEnabled(std::function<bool()> enabled) { m_automaticChecksEnabled = std::move(enabled); }
    // Si la app esta haciendo algo que una actualizacion cortaria al cerrarla (Disk Space borrando):
    // mientras devuelva true no se ofrece ni se instala nada.
    void setBusyElsewhere(std::function<bool()> busy) { m_busyElsewhere = std::move(busy); }

    // manual = true (menu "Check for Updates..."): siempre hay respuesta, incluso
    // "ya estas al dia" o un error con detalle. manual = false (chequeo de arranque):
    // silencioso salvo que haya una version nueva.
    void checkForUpdates(bool manual);

    // "Check now" de General (D-13): el resultado va a la fila por las senales (checking,
    // upToDate, updateAvailable) en vez de un cartel; los errores siguen saliendo en cartel.
    void checkInline();
    // "Update" de la fila: baja e instala la version que encontro el ultimo chequeo.
    void installAvailable();

signals:
    void checking();
    void upToDate(const QString &latestVersion);
    void updateAvailable(const QString &version);
    // El chequeo termino sin resultado para la fila (error ya mostrado, o sin release).
    void checkFailed();

private:
    enum class Mode { Automatic, Manual, Inline };

    void startCheckRequest(Mode mode);
    void onCheckFinished(Mode mode);

    // Tick del chequeo periodico: si ya vencio el proximo chequeo, lo corre.
    void onPeriodicTick();
    // Fija el proximo chequeo periodico a `delayMs` (+/- el jitter si `withJitter`) de ahora.
    void schedulePeriodicCheckIn(qint64 delayMs, bool withJitter);
    QDateTime snoozeUntil() const;
    void setSnoozeUntil(const QDateTime &until);

    void promptForUpdate(const QString &version, const QUrl &downloadUrl,
                         const QString &assetName, const QString &sha256Digest);
    // True si ESTA copia no se puede actualizar sola (ya le mostro al usuario por que y lo dejo
    // pospuesto como "Later").
    bool blockedHere();
    // True si ahora no conviene cerrar la app (ya se lo dijo al usuario).
    bool refuseWhileBusyElsewhere();
    // Padre de los carteles de la descarga. En macOS, con la ventana principal cerrada, un cartel
    // hijo de ella no se ve: ahi van sin padre.
    QWidget *dialogParent() const;
    void downloadAndRunUpdate(const QUrl &downloadUrl, const QString &assetName,
                              const QString &sha256Digest, const QString &version);
    void onDownloadReadyRead();
    void onDownloadFinished();
    // Cierra el archivo parcial y libera el hash, sin commitear. Se llama al
    // cancelar, al fallar, y antes de arrancar una descarga nueva.
    void discardPartialDownload();

    QWidget *parentWindow() const;

    // QPointer: si el widget padre (o el dialogo de progreso) se destruye por otra
    // via, la referencia se vuelve null sola en vez de quedar colgante.
    QPointer<QWidget> m_parentWindow;
    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_checkReply = nullptr;
    QNetworkReply *m_downloadReply = nullptr;
    QPointer<QProgressDialog> m_progressDialog;

    bool m_busy = false;
    bool m_automaticCheckScheduled = false;

    // ------------------------------------------------------ chequeo periodico
    // Tick corto que compara el reloj contra m_nextPeriodicCheckUtc: al volver de una
    // suspension, el chequeo vencido corre enseguida en vez de esperar otras 3 horas.
    QTimer *m_periodicTimer = nullptr;
    QDateTime m_nextPeriodicCheckUtc;
    // El chequeo en curso lo lanzo el tick: si falla por red, se reintenta antes.
    bool m_periodicCheckActive = false;
    std::function<bool()> m_automaticChecksEnabled;
    std::function<bool()> m_busyElsewhere;
    SettingsStore *m_store = nullptr;
    // Snooze sin store (captura, self-test): vale solo para esta sesion.
    QDateTime m_memorySnoozeUntil;

    // Lo que encontro el ultimo chequeo con version nueva, para "Update" de la fila.
    QString m_availableVersion;
    QUrl m_availableUrl;
    QString m_availableAsset;
    QString m_availableDigest;

    // ------------------------------------------------------ descarga por streaming
    // El asset NO se acumula en RAM: se escribe al disco a medida que llega y el
    // SHA-256 se calcula incremental (mismo motivo que FM_UpdateService: con
    // readAll() el pico de memoria es ~2x el tamano del asset).
    QSaveFile *m_downloadFile = nullptr;
    QCryptographicHash *m_downloadHash = nullptr;
    QString m_downloadTargetPath;
    QString m_downloadVersion;
    QString m_pendingSha256Digest;
    bool m_downloadUserCancelled = false;
    // Escritura a disco corta o fallida (disco lleno, permisos, etc.): se corta la
    // descarga y se reporta como fallo de escritura, no como error de red generico.
    bool m_downloadWriteFailed = false;
    // Se valida el status HTTP una sola vez, al llegar el primer dato: sin esto se
    // escribiria al disco (y se firmaria como instalador valido) una pagina de error.
    bool m_downloadResponseChecked = false;
};

#endif // MIGHTYTOOLS_UPDATESERVICE_H
