#ifndef MIGHTYTOOLS_DICTATIONMODELS_H
#define MIGHTYTOOLS_DICTATIONMODELS_H

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

#include <atomic>
#include <map>
#include <memory>
#include <thread>

class QCryptographicHash;
class QFile;
class QNetworkAccessManager;
class QNetworkReply;
class QProcess;

// Lo que el dictado necesita en disco: el motor (transcribe.cpp, un .tar.gz) y el modelo (un .gguf),
// dentro de una carpeta raiz de modelos que se elige en la pagina de la herramienta (por defecto
// <carpeta del exe>\models; en la PC de desarrollo, E:\LGA_AI). Nada viaja con el instalador.
//
//   <raiz>\transcribe.cpp\0.4.1-windows-x86_64-cpu-vulkan\   transcribe.dll, ggml*.dll, licenses\
//   <raiz>\whisper\whisper-large-v3-turbo-Q8_0.gguf
//
// Estado por pieza: falta, bajando (%), verificando (%), lista o error. Se baja solo lo que falta, a un
// .part, y recien con el tamano y el SHA-256 correctos pasa a su lugar (el motor se desempaqueta con
// el tar.exe de Windows a una carpeta temporal dentro de la raiz y despues se renombra). Lista = el
// archivo con su marcador "<archivo>.verified" (tamano, fecha y SHA-256), o la carpeta del motor con
// ".verified" adentro (el SHA-256 del paquete y el tamano y SHA-256 de cada archivo desempaquetado): asi
// no se vuelven a leer 886 MB en cada arranque. Un archivo que ya estaba (el modelo copiado a mano) se
// verifica una vez, en otro hilo. Los archivos del motor se vuelven a verificar cada vez que se carga
// (checkEngineFiles), porque son codigo que se ejecuta.
//
// En una corrida automatizada no se baja nada ni se abre la red: solo se aceptan URL file:// (el
// self-test con archivos chicos), y borrar lo que ya estaba solo actua dentro de la carpeta de pruebas
// (AutomatedRun).
class DictationModels : public QObject
{
    Q_OBJECT

public:
    enum class State { Missing, Downloading, Verifying, Ready, Error };
    Q_ENUM(State)

    struct Piece
    {
        QString id;            ///< kEngine, kModel
        QString title;         ///< nombre del producto, sin traducir
        QUrl url;
        qint64 size = 0;       ///< bytes de la descarga
        QByteArray sha256;     ///< de la descarga, hex en minusculas
        QString license;
        QString relativePath;  ///< dentro de la raiz: el archivo, o la carpeta si es un .tar.gz
        QString archiveFolder; ///< .tar.gz: la carpeta de adentro que pasa a ser relativePath
        QString requiredFile;  ///< .tar.gz: lo que tiene que estar en esa carpeta
    };

    static inline const QString kEngine = QStringLiteral("engine");
    static inline const QString kModel = QStringLiteral("model");
    // transcribe.cpp 0.4.1 (CPU + Vulkan) y Whisper large-v3 turbo Q8_0, los dos MIT.
    static QList<Piece> defaultPieces();
    static QString defaultRoot();

    explicit DictationModels(const QString &root, const QList<Piece> &pieces = defaultPieces(), QObject *parent = nullptr);
    ~DictationModels() override;

    QString root() const { return m_root; }
    const QList<Piece> &pieces() const { return m_pieces; }
    QString path(const QString &id) const;
    QString engineDir() const { return path(kEngine); }
    QString modelPath() const { return path(kModel); }

    State state(const QString &id) const;
    int progress(const QString &id) const; ///< 0..100, bajando o verificando
    QString errorText(const QString &id) const;
    bool allReady() const;

    // Mira el disco (barato) y empieza a verificar lo que esta sin marcador. Llamarla al mostrar la
    // pagina y despues de cambiar la raiz.
    void refresh();
    // Solo si falta o dio error.
    void download(const QString &id);
    void downloadMissing();
    void cancel(const QString &id);
    // Borra la pieza del disco (el modelo no puede estar cargado: parar antes el motor).
    bool remove(const QString &id, QString *error = nullptr);
    // El motor no paso la verificacion de sus archivos al cargarse: queda en error hasta bajarlo de nuevo.
    void markDamaged(const QString &id, const QString &why);

    // Los archivos de la carpeta del motor contra su ".verified" (tamano y SHA-256 de cada uno, escritos
    // al desempaquetarlo). False, con el archivo en `problem`, si falta o cambio alguno o si hay una DLL
    // que no estaba (el motor carga las DLL de su carpeta). Lee todo (56 MB): la llama el proceso del
    // motor antes de cargar nada, no la app.
    static bool checkEngineFiles(const QString &engineDir, QString *problem);

signals:
    void changed(const QString &id);

private:
    struct Job
    {
        State state = State::Missing;
        int progress = 0;
        QString error;
        QNetworkReply *reply = nullptr;
        QFile *file = nullptr;
        QCryptographicHash *hash = nullptr;
        qint64 received = 0;
        bool cancelled = false;
        QString failure; // motivo del abort, si no lo pidio el usuario
        QProcess *tar = nullptr;
        QString extractDir;
        std::thread verifier;
        std::shared_ptr<std::atomic<bool>> stopVerify;
    };

    const Piece *piece(const QString &id) const;
    QString partPath(const Piece &piece) const;
    void setState(const QString &id, State state, int progress = 0, const QString &error = QString());
    void readDisk(const Piece &piece);
    void startVerify(const Piece &piece);
    void onVerified(const QString &id, bool stopped, bool match);
    void stopVerifier(Job &job);
    void onData(const QString &id);
    void onDownloaded(const QString &id);
    void install(const Piece &piece);
    void extract(const Piece &piece);
    void onExtracted(const QString &id, bool ok);
    bool markerOk(const Piece &piece) const;
    void writeMarker(const Piece &piece);

    QString m_root;
    QList<Piece> m_pieces;
    std::map<QString, Job> m_jobs;
    QNetworkAccessManager *m_network = nullptr;
};

#endif // MIGHTYTOOLS_DICTATIONMODELS_H
