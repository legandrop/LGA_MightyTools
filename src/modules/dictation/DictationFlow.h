#ifndef MIGHTYTOOLS_DICTATIONFLOW_H
#define MIGHTYTOOLS_DICTATIONFLOW_H

#include "modules/dictation/DictationOverlay.h"
#include "modules/dictation/DictationText.h"
#include "modules/dictation/DictationTrigger.h"
#include "platform/TextTyper.h"

#include <QDateTime>
#include <QObject>
#include <QStringList>
#include <QVector>

// El recorrido de UN dictado, sin sistema: apretar el atajo, grabar, ver el texto en vivo, soltar (o el
// segundo toque), la pasada final, escribir y cerrar la ventanita. Lo que toca el sistema (micrófono, motor,
// teclas, ventanita, Esc) pasa por Io: el módulo lo implementa de verdad y el self-test con dobles que anotan.
//
//   Idle --atajo--> Recording --soltar/toque/tope--> Transcribing --final--> Typing --finished--> Idle
//                       \--Esc, fallo del micrófono o del motor--> Idle (sin escribir nada)
//
// Mientras transcribe o escribe, el atajo no empieza otro dictado. Esc durante la transcripción descarta el
// texto, y durante la escritura corta lo que falta (queda en Recent como «Stopped»). Nunca queda el micrófono
// abierto: toda salida de Recording pasa por stopCapture().
class DictationFlow : public QObject
{
    Q_OBJECT

public:
    class Io
    {
    public:
        virtual ~Io() = default;
        // La ventana del frente al empezar: ahí se escribe al terminar.
        virtual quintptr foregroundWindow() = 0;
        // Vacío si el motor puede tomar el dictado (listo o cargando); si no, el motivo para la ventanita.
        virtual QString engineProblem() = 0;
        virtual bool engineReady() = 0;
        virtual void engineBegin() = 0;
        virtual void engineFeed(const QVector<float> &samples) = 0;
        virtual void engineFinish() = 0;
        virtual void engineCancel() = 0;
        // False si el micrófono ni siquiera arrancó (el motivo llega igual por captureFailed, o no llega).
        virtual bool startCapture() = 0;
        // Entrega los últimos bloques (samples) antes de volver.
        virtual void stopCapture() = 0;
        virtual bool type(quintptr target, const QString &text) = 0;
        // Corta la escritura en curso entre dos lotes (nunca queda una tecla apretada): typed() con Stopped.
        virtual void stopTyping() = 0;
        virtual void showOverlay(quintptr target) = 0;
        virtual void overlayState(DictationOverlay::State state, const QString &message) = 0;
        virtual void overlayText(const QString &confirmed, const QString &provisional) = 0;
        virtual void overlayLevel(float level) = 0;
        virtual void hideOverlay(int delayMs) = 0;
        // Hay un dictado en curso (grabando, transcribiendo o escribiendo): Esc registrado y el reloj andando.
        virtual void setActive(bool active) = 0;
    };

    enum class Phase { Idle, Recording, Transcribing, Typing };

    // Un dictado terminado, para «Recent» (solo en memoria).
    struct Entry
    {
        QDateTime when;
        QString text;
        bool typed = false;
        QString note; ///< qué pasó, en una línea («Typed», «Not typed: the window changed»)
    };

    static constexpr int kErrorMs = 4000; ///< cuánto queda un error en la ventanita

    explicit DictationFlow(Io &io, QObject *parent = nullptr);

    // El contexto que recibe el motor: sirve para descartar las pasadas en vivo que solo lo repiten.
    void setContext(const QStringList &vocabulary, const QString &prompt);
    void setReplacements(const DictationText::Replacements &replacements) { m_replacements = replacements; }
    void setClosingMarksOnly(bool on) { m_closingMarksOnly = on; }
    void setMaxDurationMs(int ms) { m_trigger.setMaxDurationMs(ms); }

    Phase phase() const { return m_phase; }
    bool busy() const { return m_phase != Phase::Idle; }

    // Entradas: el atajo (con el reloj de quien llama), Esc y el reloj del tope.
    void press(qint64 nowMs);
    void release(qint64 nowMs);
    void escape();
    void tick(qint64 nowMs);
    // Entradas: el micrófono, el motor y la escritura.
    void captureFailed(const QString &message);
    void samples(const QVector<float> &block);
    void level(float value);
    void engineReady();
    void partial(const QString &text);
    void final(const QString &text);
    void engineFailed(const QString &message);
    void limitReached();
    void typed(TextTyper::Result result, int count);
    // La herramienta se apaga: corta todo sin escribir nada.
    void shutdown();

    // Lo que dice «Recent» de cada resultado de la escritura.
    static QString noteFor(TextTyper::Result result, bool partly);

signals:
    void finished(const DictationFlow::Entry &entry);

private:
    void start();
    void finish();
    void cancel();
    void fail(const QString &message);
    void setPhase(Phase phase);
    void record(const QString &text, bool typed, const QString &note);

    Io &m_io;
    DictationTrigger m_trigger;
    Phase m_phase = Phase::Idle;
    quintptr m_target = 0;
    QStringList m_vocabulary;
    QString m_prompt;
    DictationText::Replacements m_replacements;
    bool m_closingMarksOnly = false;
    QString m_confirmed; // lo confirmado en vivo
    QString m_previous;  // la última pasada en vivo
    QString m_live;      // lo que muestra la ventanita (para «Recent» si se cancela)
    QString m_pending;   // el texto que se está escribiendo
    qint64 m_lastNow = 0;  // el reloj del último atajo o tick
    qint64 m_startedAt = 0;      // cuándo empezó a grabar
    qint64 m_finishAt = 0;       // cuándo empezó a transcribir
    qint64 m_finalTimeoutMs = 0; // tope de espera del final: crece con lo grabado
};

#endif // MIGHTYTOOLS_DICTATIONFLOW_H
