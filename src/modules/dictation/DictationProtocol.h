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
//                la sesion abierta), End(sesion), Cancel(sesion), Quit.
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

enum class Type : quint8 {
    Begin = 1,
    Audio = 2,
    End = 3,
    Cancel = 4,
    Quit = 5,
    Ready = 16,
    Partial = 17,
    Final = 18,
    Error = 19,
};

struct Message
{
    Type type = Type::Quit;
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
