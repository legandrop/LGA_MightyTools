#ifndef MIGHTYTOOLS_DICTATIONQA_H
#define MIGHTYTOOLS_DICTATIONQA_H

#include <QString>
#include <QStringList>

#include <functional>

// QA del dictado, sin pantalla, sin microfono y sin mandar teclas.
//
// --self-test: protocolo (mensajes armados, cortados, con basura adelante, UTF-8), texto confirmado y
// provisional, reemplazos, estados de descarga y verificacion con archivos chicos por file:// dentro de
// una carpeta de pruebas (incluido un .tar.gz armado con tar.exe), que en una corrida automatizada no
// se baja nada ni se crea la red; el atajo (tocar/mantener/Esc/tope), la liberacion por el hub, la
// conversion del audio, la escritura descrita y que start()/type() quedan inertes.
//
// --simulate-action dictation:<accion>:
//   transcribe <a.wav> [<b.wav>...] [<raiz de modelos>] [--concat] [--no-context]
//       El camino real: proceso del motor, en vivo (el WAV entra en tiempo real, de a 100 ms), final y
//       confirmado/provisional. Imprime cada pasada con su tiempo y el final. Al terminar mata el motor
//       y comprueba que no quede el proceso, y despues lo mata "de afuera" y espera failed().
//       WAV PCM16 mono 16 kHz. --concat junta los WAV en un solo dictado (para pasar los 30 s).
//   models <raiz de modelos> [<motor.tar.gz local>]
//       Estado de cada pieza (verifica lo que esta sin marcador). Con el .tar.gz, instala el motor
//       desde ese archivo (file://, con el tamano y el SHA-256 reales). Nunca abre la red.
//   devices
//       Lista los microfonos (id y nombre) sin abrir ninguno.
namespace DictationQa {

void selfTest(const std::function<void(bool ok, const QString &what)> &check);
int simulateAction(const QString &action, const QStringList &args);

} // namespace DictationQa

#endif // MIGHTYTOOLS_DICTATIONQA_H
