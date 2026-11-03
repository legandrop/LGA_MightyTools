#ifndef MIGHTYTOOLS_DICTATIONWORKER_H
#define MIGHTYTOOLS_DICTATIONWORKER_H

// El proceso del motor de dictado: `LGA_MightyTools.exe --dictation-worker <carpeta del motor> <modelo>`.
// Lo lanza DictationEngine; nunca lo lanza el usuario. Modo corto de main.cpp, antes de la instancia
// unica: sin ventanas, sin bandeja, sin settings, sin registro LGA.
//
// Carga transcribe.dll con LoadLibraryEx (la app no enlaza contra el motor: arranca aunque no este), los
// modulos de CPU y Vulkan que estan al lado, el modelo, y hace una pasada en vacio de 1 s de silencio: la
// primera pasada de un proceso compila los shaders de Vulkan (7,3 s la primera vez en una RTX 4090, 1,3 s
// con la cache del driver) y asi no la paga el primer dictado. Despues manda Ready.
//
// En vivo: desde 1 s de audio, mientras dura la sesion vuelve a transcribir TODO el audio acumulado cada
// 500 ms (o apenas termina la pasada anterior, si tardo mas) y manda Partial. Las pasadas en vivo van con
// el vocabulario pero sin la frase de contexto ni los reintentos de Whisper a otra temperatura, y no
// mandan las que solo repiten el contexto (DictationText::echoesContext). Con End corta la pasada en vivo que este corriendo (callback de aborto del motor), hace
// la pasada final con vocabulario y frase y manda Final. Si la app cierra el canal (sale o se cae), el
// proceso termina solo.
//
// Mas de 30 s: Whisper no tiene limite (transcribe.cpp corta en ventanas de 30 s y las une, docs/
// input-limits.md), asi que el tope es de la app: DictationProtocol::kMaxSeconds. Lo que llega despues no
// se agrega.
namespace DictationWorker {

int run(int argc, char *argv[]);

} // namespace DictationWorker

#endif // MIGHTYTOOLS_DICTATIONWORKER_H
