#ifndef MIGHTYTOOLS_DICTATIONPROTOCOL_H
#define MIGHTYTOOLS_DICTATIONPROTOCOL_H

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

// Protocolo entre la app (DictationEngine) y el proceso del motor (--dictation-worker), por el stdin y
// el stdout del proceso. Binario y con largo: "DCT1" + u32 largo del cuerpo + u8 tipo + cuerpo (todo
// little endian). Los textos van SIEMPRE en UTF-8 valido (un "¿" en otra pagina de codigos hace fallar
// todas las pasadas del motor). Si aparecen bytes que no son un mensaje (algo que la DLL escribio en la
// salida estandar), el lector los saltea hasta el proximo "DCT1".
//
// App -> motor:  Begin(sesion, idioma, vocabulario, frase de contexto), Audio(float32 mono 16 kHz, de
//                la sesion abierta), End(sesion), Cancel(sesion). La app no manda un "salir": cierra el
//                canal o mata el proceso, y el motor sale al ver el canal cerrado.
// Motor -> app:  Ready(dispositivo, ms de carga), Partial(sesion, ms de audio, ms de la pasada, texto),
//                Final(idem), Error(sesion, texto; sesion 0 = el motor no pudo arrancar y sale).
namespace DictationProtocol {

constexpr int kSampleRate = 16000;
// Tope de una grabacion. Whisper no tiene limite (corta en ventanas de 30 s), pero cada pasada en vivo
// vuelve a transcribir todo: con 120 s la pasada sigue en decimas de segundo en una GPU.
constexpr int kMaxSeconds = 120;
constexpr int kMaxSamples = kMaxSeconds * kSampleRate;
// Cuerpo mas grande que se acepta: un mensaje de audio de 1 s pesa 64 KB.
constexpr quint32 kMaxBody = 4 * 1024 * 1024;

// Sin voz no hay texto: Whisper sobre silencio, ruido de fondo o un click inventa frases ("Gracias por ver
// el video.", "¡Gracias!", "Fijate si el roto anda bien."). Se cuenta cuanto audio tiene voz: tramos de
// 30 ms con RMS en o sobre el piso. Medido el 2026-11-03: las cinco grabaciones reales de Lega tienen su
// tramo mas fuerte de -23,1 a -19,2 dBFS; el silencio digital, -120; el ruido bajo de la auditoria, -54,7
// (22 dB de margen bajo la voz mas baja, para quien habla bajo o lejos del microfono).
constexpr float kVoiceFloorDb = -45.0f;
constexpr int kFrameSamples = kSampleRate * 30 / 1000;
// Tramos sobre el piso que hacen falta para transcribir (180 ms): un pico solo no alcanza. Medido por la
// auditoria: un click da 1 o 2 tramos, un golpe en el ruido 2, "Okay" 13, una frase a -42 dBFS 9, las
// grabaciones de Lega de 82 a 377.
constexpr int kMinVoiceFrames = 6;
// Con menos de esto (~1 s de voz), un final que es un pedazo seguido de la frase de contexto de 4 palabras
// o mas es Whisper repitiendo el contexto, no lo que se dijo.
constexpr int kShortVoiceFrames = 33;
// Antes de transcribir se saca el silencio de antes y de despues de la voz, dejando este margen: con
// segundos de silencio delante Whisper escribe basura ("simplific tapar pasteored z...").
constexpr int kTrimMarginSamples = kSampleRate * 300 / 1000;
// Un tramo de 30 s que Whisper da por "sin voz" con esta probabilidad o mas se descarta aunque el modelo
// haya escrito algo (su propio corte exige ademas poca confianza, y una frase inventada sale confiada).
constexpr float kNoSpeechProb = 0.6f;

// Cuanta voz hay en un audio.
struct Voice
{
    int frames = 0;            ///< tramos de 30 ms en o sobre el piso
    float loudestDb = -120.0f; ///< el tramo mas fuerte (-120 sin audio o con silencio digital)
    size_t from = 0;           ///< lo que conviene transcribir: desde el primer tramo con voz menos el margen...
    size_t to = 0;             ///< ...hasta el ultimo mas el margen (from == to sin voz)
    bool enough() const { return frames >= kMinVoiceFrames; }
};
Voice measureVoice(const float *pcm, size_t count);
// El tramo de 30 ms mas fuerte de `pcm`, en dBFS (-120 si no hay audio o es silencio digital).
inline float loudestFrameDb(const float *pcm, size_t count) { return measureVoice(pcm, count).loudestDb; }
// Error(sesion 0) con este prefijo: los archivos del motor no son los que se verificaron al bajarlo (sigue
// el archivo que fallo). El motor no cargo nada.
constexpr char kDamagedPrefix[] = "engine-files:";

enum class Type : quint8 {
    Begin = 1,
    Audio = 2,
    End = 3,
    Cancel = 4,
    Ready = 16,
    Partial = 17,
    Final = 18,
    Error = 19,
};

struct Message
{
    Type type = Type::Error;
    quint32 session = 0;
    // Begin
    QString language;
    QStringList vocabulary;
    QString prompt;
    // Audio
    QVector<float> samples;
    // Ready (el dispositivo), Partial, Final, Error
    QString text;
    // Ready: ms de carga en `passMs`. Partial / Final: largo del audio transcripto y lo que tardo la pasada.
    quint32 audioMs = 0;
    quint32 passMs = 0;
};

QByteArray encode(const Message &message);
// Saca del frente de `buffer` los mensajes completos y deja lo que falta llegar.
QList<Message> decode(QByteArray *buffer);

// Texto de la DLL -> UTF-8 valido: secuencias rotas (un caracter cortado entre dos tokens) afuera.
QString cleanText(const QByteArray &utf8);

} // namespace DictationProtocol

#endif // MIGHTYTOOLS_DICTATIONPROTOCOL_H
