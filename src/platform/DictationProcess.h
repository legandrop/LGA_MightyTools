#ifndef MIGHTYTOOLS_DICTATIONPROCESS_H
#define MIGHTYTOOLS_DICTATIONPROCESS_H

#include <QByteArray>
#include <QString>
#include <QtGlobal>

class QProcess;

// Solo Windows (platform/win/DictationProcessWin.cpp). Lo que el dictado necesita del sistema y Qt no da.
//  - Del lado de la app: lanzar el proceso del motor y tar.exe sin consola visible.
//  - Del lado del proceso del motor (--dictation-worker): sin carteles del sistema (DLL que falta, falla
//    del driver), stdin y stdout binarios, y stdout reservado para el protocolo: lo que la DLL del motor
//    escriba en la salida estandar va a parar a stderr.
//  - Cargar la DLL del motor con su carpeta como lugar de busqueda de sus dependencias (ggml*.dll).
namespace DictationProcess {

// QProcess con CREATE_NO_WINDOW: un exe de consola (tar.exe) no abre ninguna ventana.
void hideConsole(QProcess *process);

// Primer paso del proceso del motor, antes de cargar ninguna DLL.
void prepareWorker();
// Lee hasta `size` bytes de stdin, bloqueando. 0: la app cerro el canal (o se cayo); -1: error.
qint64 readInput(char *data, qint64 size);
// Escribe todo en el stdout reservado para el protocolo.
bool writeOutput(const QByteArray &data);

// Carga una DLL por ruta absoluta; sus dependencias se buscan en su carpeta. Nulo si fallo.
void *loadLibrary(const QString &path, QString *error);
QFunctionPointer resolve(void *library, const char *name);

// %SystemRoot%\System32\tar.exe, o vacio si no esta (Windows anterior a 10 1803).
QString systemTar();

// Para la QA: si el proceso con ese PID sigue vivo.
bool processAlive(qint64 pid);

} // namespace DictationProcess

#endif // MIGHTYTOOLS_DICTATIONPROCESS_H
