#include "PomodoroEngine.h"

PomodoroEngine::PomodoroEngine(QObject* parent)
    : QObject(parent)
{
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &PomodoroEngine::tickOneSecond);
}

int PomodoroEngine::phaseTotalSeconds() const
{
    switch (m_phase) {
    case Phase::Focus:      return m_focusMinutes * 60;
    case Phase::ShortBreak: return m_shortBreakMinutes * 60;
    case Phase::LongBreak:  return m_longBreakMinutes * 60;
    }
    return m_focusMinutes * 60; // unreachable, but compilers want proof
}

QString PomodoroEngine::timeText() const
{
    return QStringLiteral("%1:%2")
        .arg(m_remaining / 60, 2, 10, QChar('0'))
        .arg(m_remaining % 60, 2, 10, QChar('0'));
}

QString PomodoroEngine::phaseName() const
{
    switch (m_phase) {
    case Phase::Focus:      return tr("Focus");
    case Phase::ShortBreak: return tr("Short break");
    case Phase::LongBreak:  return tr("Long break");
    }
    return tr("Focus");
}

void PomodoroEngine::start()
{
    if (m_running)
        return;
    m_running = true;
    m_engaged = true;
    m_timer.start();
    emit modeChanged();
    emit changed();
}

void PomodoroEngine::pause()
{
    if (!m_running)
        return;
    m_running = false; // engaged STAYS true: paused, not abandoned
    m_timer.stop();
    emit modeChanged();
    emit changed();
}

void PomodoroEngine::reset()
{
    m_running = false;
    m_engaged = false; // abandoned: the link must take its hands off
    m_timer.stop();
    m_phase     = Phase::Focus;
    m_round     = 1;
    m_remaining = phaseTotalSeconds();
    emit modeChanged();
    emit changed();
}

void PomodoroEngine::skip()
{
    advancePhase(); // no phaseEnded: you asked for this with your own hands
    emit modeChanged();
    emit changed();
}

void PomodoroEngine::setDurations(int focusMin, int shortBreakMin,
                                  int longBreakMin)
{
    m_focusMinutes      = focusMin;
    m_shortBreakMinutes = shortBreakMin;
    m_longBreakMinutes  = longBreakMin;

    // A duration change only reshapes the CURRENT phase when the clock is
    // idle — we never yank seconds out from under a running countdown; the
    // new length takes effect on the next phase instead. Least surprising:
    // fiddling with settings mid-focus doesn't reset your progress, but a
    // change while stopped is visible immediately.
    if (!m_running)
        m_remaining = phaseTotalSeconds();
    emit changed();
}

void PomodoroEngine::tickOneSecond()
{
    if (--m_remaining > 0) {
        emit changed();
        return;
    }
    const Phase finished = m_phase;
    advancePhase(); // countdown flows straight into the next phase — the
                    // classic Pomodoro rhythm needs no click between phases
    emit phaseEnded(finished, m_phase);
    emit modeChanged(); // the phase flipped: the link may need to re-steer
    emit changed();
}

void PomodoroEngine::advancePhase()
{
    // The transition table, unchanged from the page it came from:
    //   Focus -> Short break (rounds 1–3) or Long break (round 4)
    //   any break -> Focus, and the round counter advances.
    if (m_phase == Phase::Focus) {
        m_phase = (m_round % kRoundsPerCycle == 0) ? Phase::LongBreak
                                                   : Phase::ShortBreak;
    } else {
        m_phase = Phase::Focus;
        ++m_round;
    }
    m_remaining = phaseTotalSeconds();
}
