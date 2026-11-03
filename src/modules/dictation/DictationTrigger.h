#ifndef MIGHTYTOOLS_DICTATIONTRIGGER_H
#define MIGHTYTOOLS_DICTATIONTRIGGER_H

#include <QtGlobal>

// Cuándo empieza y termina un dictado según el atajo. Pura: sin reloj ni sistema, quien la usa le pasa el
// tiempo (QElapsedTimer) y hace lo que devuelve.
//  - Apretar el atajo empieza a grabar.
//  - Soltarlo antes de tapMs (300 ms) deja grabando hasta el próximo toque: modo «tocar».
//  - Soltarlo después termina: modo «mantener».
//  - Esc cancela sin escribir nada. El tope de duración (lo fija el motor según su modelo) termina solo.
// Si se pierde la liberación (el gancho no la vio), volver a apretar termina; y si nadie aprieta, el tope.
class DictationTrigger
{
public:
    enum class Action { None, Start, Finish, Cancel };
    enum class State { Idle, Holding, Latched };

    static constexpr int kDefaultTapMs = 300;

    explicit DictationTrigger(int tapMs = kDefaultTapMs);

    // 0: sin tope.
    void setMaxDurationMs(int ms) { m_maxMs = ms; }
    int maxDurationMs() const { return m_maxMs; }

    Action press(qint64 nowMs);
    Action release(qint64 nowMs);
    Action cancel();
    // Mientras graba, seguido (cada ~100 ms): Finish al llegar al tope.
    Action tick(qint64 nowMs);
    // La grabación terminó por otro lado (falló el micrófono): vuelve a Idle sin acción.
    void reset() { m_state = State::Idle; }

    State state() const { return m_state; }
    bool recording() const { return m_state != State::Idle; }

private:
    int m_tapMs;
    int m_maxMs = 0;
    State m_state = State::Idle;
    qint64 m_startedAt = 0;
};

#endif // MIGHTYTOOLS_DICTATIONTRIGGER_H
