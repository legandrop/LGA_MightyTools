#ifndef MIGHTYTOOLS_AUDIOCAPTURE_H
#define MIGHTYTOOLS_AUDIOCAPTURE_H

#include <QList>
#include <QObject>
#include <QString>
#include <QVector>

#include <memory>

// Micrófono del dictado. Una implementación por plataforma:
//  - Windows (platform/win/AudioCaptureWin.cpp): WASAPI en modo compartido, en un hilo propio que espera el
//    evento del dispositivo. Pide float32 mono a 16 kHz y Windows convierte (AUTOCONVERTPCM); si el dispositivo
//    no lo acepta, abre con su formato de mezcla y convierte acá (AudioCapture::Converter).
//  - macOS (platform/mac/AudioCaptureMac.mm): AVAudioEngine con un tap en la entrada; convierte acá. Pide el
//    permiso de Micrófono de macOS la primera vez (PermissionAsked).
//
// El micrófono se abre SOLO entre start() y stop(): apenas se cierra, el sistema apaga su ícono de micrófono en
// uso. En una corrida automatizada start() no abre nada (core/AutomatedRun.h). Todas las señales llegan en
// el hilo de la UI.
class AudioCapture : public QObject
{
    Q_OBJECT

public:
    struct Device
    {
        QString id;   ///< identificador estable del sistema (el que se guarda en los ajustes)
        QString name; ///< nombre que muestra Windows ("Microphone (USB Audio)")
    };

    enum class Error {
        None,
        NoMicrophone,     ///< no hay ningún micrófono activo
        AccessDenied,     ///< la privacidad de Windows (o de macOS) no deja usar el micrófono a esta app
        PermissionAsked,  ///< macOS: está preguntando si la app puede usar el micrófono; este dictado no graba
        DeviceInUse,      ///< otra app lo tiene en modo exclusivo
        DeviceGone,       ///< se desconectó (o el elegido no está conectado)
        UnsupportedFormat,
        Other,
    };
    Q_ENUM(Error)

    static constexpr int kSampleRate = 16000;

    explicit AudioCapture(QObject *parent = nullptr);
    ~AudioCapture() override;

    // Los micrófonos activos, sin abrir ninguno. El primero NO es necesariamente el de por defecto.
    static QList<Device> devices();
    // Texto para el usuario (I18n), con el código del sistema si no hay uno mejor.
    static QString errorText(Error error, long systemCode = 0);

    // Empieza a grabar sin frenar la UI: si no se puede abrir, llega failed(). `deviceId` vacío: el
    // micrófono por defecto de Windows (el de «Elegir un dispositivo para hablar o grabar», rol consola).
    // False si ni siquiera arrancó: ya graba, o es una corrida automatizada.
    bool start(const QString &deviceId = QString());
    // Cierra el micrófono y espera al hilo (decenas de ms). Lo que quedaba en el búfer sale antes por samples().
    void stop();
    bool isRunning() const;

    // Mezcla a mono y lleva a 16 kHz con interpolación lineal; recuerda la fase entre bloques. Pura: la usa
    // el camino de respaldo de Windows y la prueba el self-test.
    class Converter
    {
    public:
        Converter(int sampleRate, int channels);
        void push(const float *interleaved, int frames, QVector<float> *out);

    private:
        double m_step;       ///< muestras de entrada por muestra de salida
        int m_channels;
        double m_next = 0.0; ///< posición (en muestras de entrada) de la próxima salida
        qint64 m_index = 0;  ///< índice de la próxima muestra de entrada
        float m_previous = 0.0f;
    };

    // Nivel 0..1 de un bloque para el medidor: RMS en dBFS, de -60 dB (0) a 0 dB (1).
    static float levelOf(const QVector<float> &block);

signals:
    // Bloques de ~50 ms a 16 kHz mono, en orden.
    void samples(const QVector<float> &block);
    // Nivel del último bloque, 0..1.
    void level(float value);
    // No se pudo abrir o se cortó grabando (el micrófono ya quedó cerrado).
    void failed(AudioCapture::Error error, const QString &message);

private:
    struct Private;
    std::unique_ptr<Private> d;
};

#endif // MIGHTYTOOLS_AUDIOCAPTURE_H
