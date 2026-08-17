#include "PomodoroLink.h"

#include "PomodoroEngine.h"

PomodoroLink::PomodoroLink(PomodoroEngine* engine, TrackerService* tracker,
                           QObject* parent)
    : QObject(parent)
    , m_engine(engine)
    , m_tracker(tracker)
    , m_lastTrackerState(tracker->state())
{
    // The Pomodoro speaks at its transitions (coarse signal — see header).
    // The play EDGE (running: false -> true) is the one transition allowed
    // to adopt; a phase flip keeps running true, so it can steer the kind
    // but never grab a block the user stopped.
    connect(m_engine, &PomodoroEngine::modeChanged, this, [this]() {
        const bool nowRunning = m_engine->running();
        const bool playEdge   = nowRunning && !m_engineWasRunning;
        m_engineWasRunning    = nowRunning;
        apply(playEdge);
    });

    // The block the Pomodoro was driving just ran out of window (v19.8):
    // pause the engine — PAUSE, not reset, so the cycle survives lunch and
    // pressing ▶ later adopts whatever block is under the clock then. Only
    // while enabled and actually running: a Pomodoro the user already
    // paused or reset needs no second opinion. (Note the direction: this
    // is the ONE message that flows tracker -> engine; everything else
    // steers the other way. An adapter may speak both languages — that is
    // what keeps the two machines themselves monolingual.)
    connect(m_tracker, &TrackerService::trackedBlockEnded, this, [this]() {
        if (m_enabled && m_engine->running())
            m_engine->pause();
    });

    // The Idle -> tracking edge only: joining a block mid-Pomodoro snaps it
    // into the cycle; manual kind-switches while already tracking stand.
    connect(m_tracker, &TrackerService::stateChanged, this, [this]() {
        const auto now = m_tracker->state();
        const bool joined = (m_lastTrackerState == TrackerService::State::Idle
                             && now != TrackerService::State::Idle);
        m_lastTrackerState = now;
        if (joined)
            apply();
    });
}

void PomodoroLink::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    if (m_enabled)
        apply(m_engine->running()); // flipping the switch mid-cycle takes
                                    // effect immediately — and mid-RUN it
                                    // counts as a play edge (the tick was
                                    // the user's action just now)
}

void PomodoroLink::apply(bool mayAdopt)
{
    if (!m_enabled)
        return;
    if (!m_engine->engaged())
        return; // fresh or reset Pomodoro: not our block to steer

    QString eventId = m_tracker->trackedEventId();
    if (m_tracker->state() == TrackerService::State::Idle) {
        // Adoption gate — every clause is a promise from the header:
        // only on a play edge, only while actually running (never stamp
        // DISTRACTED on an untouched block), and only the one block the
        // clock is inside of. No live block: keep waiting, say nothing.
        if (!mayAdopt || !m_engine->running())
            return;
        eventId = m_tracker->liveEventNow();
        if (eventId.isEmpty())
            return;
    }

    // The mapping, verbatim from the header's rule table.
    TrackerService::State desired;
    if (!m_engine->running())
        desired = TrackerService::State::Distracted;
    else if (m_engine->phase() == PomodoroEngine::Phase::Focus)
        desired = TrackerService::State::Focusing;
    else
        desired = TrackerService::State::OnBreak;

    if (m_tracker->state() == desired)
        return; // already there — never re-open an interval for nothing

    if (!m_tracker->canTrackNow(eventId))
        return; // the block's window has passed: stop steering, don't forge

    switch (desired) {
    case TrackerService::State::Focusing:
        m_tracker->startFocus(eventId);
        break;
    case TrackerService::State::OnBreak:
        m_tracker->startBreak(eventId);
        break;
    case TrackerService::State::Distracted:
        m_tracker->startDistracted(eventId);
        break;
    case TrackerService::State::Idle:
        break; // unreachable: desired is never Idle (the link never stops
               // the tracker — stopping is a human act too)
    }
}
