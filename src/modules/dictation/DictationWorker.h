#ifndef MIGHTYTOOLS_DICTATIONWORKER_H
#define MIGHTYTOOLS_DICTATIONWORKER_H

// El proceso del motor de dictado: `LGA_MightyTools.exe --dictation-worker <carpeta del motor> <modelo>`.
// Lo lanza DictationEngine; nunca lo lanza el usuario. Modo corto de main.cpp, antes de la instancia
// unica: sin ventanas, sin bandeja, sin settings, sin registro LGA.
//
// Carga transcribe.dll con LoadLibraryEx (en mac libtranscribe.dylib con dlopen, y la GPU es Metal; la app
// no enlaza contra el motor: arranca aunque no este), los
// modulos de CPU y Vulkan que estan al lado, el modelo, y hace una pasada en vacio de 1 s de silencio: la
// primera pasada de un proceso compila los shaders de Vulkan (7,3 s la primera vez en una RTX 4090, 1,3 s
// con la cache del driver) y asi no la paga el primer dictado. Despues manda Ready.
//
// En vivo: desde 1 s de audio con voz (DictationProtocol::measureVoice), mientras dura la sesion vuelve a
// transcribir TODO el audio acumulado, sin el silencio de antes y de despues, cada 500 ms o el doble de lo
// que tardo la ultima pasada, y manda Partial. Las pasadas en vivo van con el vocabulario pero sin la frase
// de contexto ni los reintentos de Whisper a otra temperatura, y no mandan las que solo repiten el contexto
// (DictationText::echoesContext). Con End corta la pasada en vivo que este corriendo (callback de aborto del
// motor), hace la pasada final con vocabulario y frase y manda Final (vacio si no hubo voz suficiente). Si
// la app cierra el canal (sale o se cae), el proceso termina solo.
//
// Mas de 30 s: Whisper no tiene limite (transcribe.cpp corta en ventanas de 30 s y las une, docs/
// input-limits.md), asi que el tope es de la app: DictationProtocol::kMaxSeconds. Lo que llega despues no
// se agrega.
#include <QString>
#include <QStringList>

#include <functional>

namespace DictationWorker {

int run(int argc, char *argv[]);

// Una pasada del motor, lo justo para decidir el texto final.
struct Attempt
{
    bool ok = false;      ///< el motor contesto; si no, `error`
    bool aborted = false; ///< se corto (Cancel o salir)
    QString text;
    QString error;
    quint32 ms = 0;       ///< lo que tardaron las pasadas
};

// El texto final de una sesion con voz suficiente. `pass(withPrompt)` corre el motor con el vocabulario y,
// si withPrompt, tambien con la frase de contexto. Un final que solo repite el contexto se descarta (con poca
// voz, menos de DictationProtocol::kShortVoiceFrames tramos, ya un pedazo de 4 palabras de la frase). Con
// poca voz Whisper contesta a un "No" suelto con la frase entera: entonces se repite la pasada sin la frase
// (como las pasadas en vivo, ~100 ms) y se usa si no es eco ni vacia. `log` recibe una linea por decision.
Attempt finalText(int voiceFrames, const QStringList &vocabulary, const QString &prompt,
                  const std::function<Attempt(bool withPrompt)> &pass, QStringList *log = nullptr);

} // namespace DictationWorker

#endif // MIGHTYTOOLS_DICTATIONWORKER_H
