#ifndef MIGHTYTOOLS_DICTATIONENGINE_H
#define MIGHTYTOOLS_DICTATIONENGINE_H

#include <QByteArray>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

class QProcess;
class QTimer;

// El lado de la app del motor de dictado: arranca y para el proceso del motor (--dictation-worker, ver
// DictationWorker.h) y habla con el por su stdin/stdout (DictationProtocol.h). La app nunca carga el
// motor: si el proceso se cae, se emite failed() y nada mas.
//
// Uso: conectar las senales, start() (el modelo se carga en el otro proceso; ready() al terminar),
// y por cada dictado begin() -> feed()... -> finish() (o cancel()). partial() trae cada pasada en
// vivo (el texto entero, para DictationText::stabilize) y final() el texto de la pasada final. begin()
// y feed() se pueden llamar antes de ready(): el audio espera en el proceso del motor.
//
// Memoria: apagar (stop() o destruir el objeto) mata el proceso y devuelve toda la memoria de video.
// Con setIdleUnloadMinutes(N) el proceso se cierra solo despues de N minutos sin dictar (unloaded()) y
// el proximo begin() lo vuelve a arrancar.
class DictationEngine : public QObject
{
    Q_OBJECT

public:
    static constexpr int kSampleRate = 16000;
    static constexpr int kMaxSeconds = 120; ///< tope de una grabacion; pasado, limitReached()

    explicit DictationEngine(QObject *parent = nullptr);
    ~DictationEngine() override;

    // enginePath: la carpeta con transcribe.dll; modelPath: el .gguf. Si falta alguno, failed().
    void start(const QString &enginePath, const QString &modelPath);
    void stop();
    bool isRunning() const { return m_process != nullptr; }
    bool isReady() const { return m_ready; }
    QString device() const { return m_device; }

    // Un dictado. Con otro grabando, ese se descarta (uno que ya espera su final() lo recibe igual).
    // language: "es" (vacio = que lo detecte el modelo). vocabulary: terminos en orden de prioridad
    // ("Nuke Studio", "XYplorer"), en todas las pasadas. prompt: una frase de contexto en el registro del
    // usuario (voseo, puntuacion), solo en la pasada final.
    void begin(const QString &language, const QStringList &vocabulary, const QString &prompt);
    // Audio float32 mono a 16 kHz en [-1, 1].
    void feed(const QVector<float> &samples);
    void finish();
    void cancel();
    bool inSession() const { return m_open; }                      ///< grabando
    bool busy() const { return m_open || !m_awaitingFinal.isEmpty(); } ///< grabando o esperando un final()

    // 0 = nunca (por defecto).
    void setIdleUnloadMinutes(int minutes);

    // De la ultima pasada recibida (partial o final): audio que cubre y lo que tardo el motor.
    int lastAudioMs() const { return m_lastAudioMs; }
    int lastPassMs() const { return m_lastPassMs; }
    // Para la QA.
    qint64 workerPid() const;
    void killWorkerForQa();

signals:
    void ready(const QString &device, int loadMs);
    void partial(const QString &text);
    void final(const QString &text);
    void failed(const QString &error);
    // La grabacion llego a kMaxSeconds: lo que siga no se transcribe. Quien graba deberia llamar finish().
    void limitReached();
    // El proceso se cerro por inactividad (setIdleUnloadMinutes).
    void unloaded();

private:
    void launch();
    void send(const QByteArray &frame);
    void onOutput();
    void onErrorOutput();
    void onFinished();
    void closeProcess();
    void armIdle();

    QProcess *m_process = nullptr;
    QTimer *m_idle = nullptr;
    int m_idleMinutes = 0;
    QString m_enginePath;
    QString m_modelPath;
    QByteArray m_buffer;
    QByteArray m_errorBuffer;
    QString m_device;
    bool m_ready = false;
    bool m_failedSent = false;

    quint32 m_session = 0;
    bool m_open = false;          // grabando: acepta feed()
    QSet<quint32> m_awaitingFinal; // terminadas con finish(), esperando su final()
    qint64 m_samples = 0;
    bool m_limitSent = false;
    int m_lastAudioMs = 0;
    int m_lastPassMs = 0;
};

#endif // MIGHTYTOOLS_DICTATIONENGINE_H
