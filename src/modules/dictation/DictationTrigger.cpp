#include "modules/dictation/DictationTrigger.h"

DictationTrigger::DictationTrigger(int tapMs)
    : m_tapMs(tapMs)
{
}

DictationTrigger::Action DictationTrigger::press(qint64 nowMs)
{
    switch (m_state) {
    case State::Idle:
        m_state = State::Holding;
        m_startedAt = nowMs;
        return Action::Start;
    case State::Holding: // la liberación anterior se perdió: el segundo toque termina
    case State::Latched:
        m_state = State::Idle;
        return Action::Finish;
    }
    return Action::None;
}

DictationTrigger::Action DictationTrigger::release(qint64 nowMs)
{
    if (m_state != State::Holding) {
        return Action::None; // la liberación del toque que terminó, o una suelta
    }
    if (nowMs - m_startedAt < m_tapMs) {
        m_state = State::Latched;
        return Action::None;
    }
    m_state = State::Idle;
    return Action::Finish;
}

DictationTrigger::Action DictationTrigger::cancel()
{
    if (m_state == State::Idle) {
        return Action::None;
    }
    m_state = State::Idle;
    return Action::Cancel;
}

DictationTrigger::Action DictationTrigger::tick(qint64 nowMs)
{
    if (m_state == State::Idle || m_maxMs <= 0 || nowMs - m_startedAt < m_maxMs) {
        return Action::None;
    }
    m_state = State::Idle;
    return Action::Finish;
}
