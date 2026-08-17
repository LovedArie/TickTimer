#pragma once
// ---------------------------------------------------------------------------
// PomodoroLink — the adapter between two machines that must stay whole.
//
// The old PomodoroPage header warned that gluing the Pomodoro to the
// tracker prematurely "would tangle two clean machines". The untangled way
// is this object: the ENGINE still knows nothing about blocks, the TRACKER
// still knows nothing about countdowns, and every line of glue lives here,
// deletable in one file if the feature ever goes away.
//
// The one rule (owner's feature, sharpened twice): **you pick the block,
// the Pomodoro picks the kind — and on the PLAY edge it may ADOPT the
// block under the clock,** because you already picked that one when you
// planned it (v19.7, owner request: "start the Pomodoro without clicking
// focus on the block first"). Adoption never guesses: the domain forbids
// overlapping blocks, so at most ONE block is live at any instant.
//   engine running, Focus phase  -> tracker startFocus(the tracked block)
//   engine running, break phase  -> tracker startBreak(...)
//   engine paused (engaged)      -> tracker startDistracted(...)  — a pause
//                                   mid-cycle is "I got pulled away"
//   engine reset (not engaged)   -> hands off — abandoning the Pomodoro is
//                                   not a statement about the block
//   tracker Idle + PLAY edge     -> adopt liveEventNow() if there is one
//                                   (press play over a planned block and
//                                   the plan starts recording itself)
//   tracker Idle otherwise       -> hands off — phase flips and pauses
//                                   never adopt: a block the user Stopped
//                                   stays stopped (their explicit command
//                                   outranks the machine's standing one),
//                                   and adopting-while-paused would stamp
//                                   DISTRACTED onto a block they never
//                                   touched
//
// When it listens (as important as what it does):
//   - engine modeChanged — the Pomodoro speaks at its OWN transitions, once
//     per transition, not once per second (that's why the engine has a
//     separate coarse signal: steering on every tick would re-open a
//     tracker interval sixty times a minute).
//   - tracker stateChanged, but ONLY for the Idle -> tracking edge: start
//     tracking a block mid-Pomodoro and it snaps into the cycle's rhythm.
//     A manual kind-switch while already tracking is DELIBERATELY left
//     standing until the next Pomodoro transition — the human's explicit
//     command outranks the machine's standing one (same courtesy the
//     update banner shows: the app never wrestles the user).
//
// All of it behind canTrackNow — the §3.38 honesty door: if the block's
// window has passed, the link stops steering rather than forging actuals.
// ---------------------------------------------------------------------------

#include "TrackerService.h"

#include <QObject>

class PomodoroEngine;

class PomodoroLink : public QObject
{
    Q_OBJECT

public:
    PomodoroLink(PomodoroEngine* engine, TrackerService* tracker,
                 QObject* parent = nullptr);

    // Told by the page (which owns the checkbox and its QSettings key) —
    // the link itself never reads preferences, same doctrine as widgets.
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }

private:
    // mayAdopt is true only on the edges where grabbing the live block
    // serves an action the user JUST took (pressed play; enabled the link
    // mid-run) — everywhere else, an idle tracker stays idle.
    void apply(bool mayAdopt = false);

    PomodoroEngine* m_engine  = nullptr; // not owned
    TrackerService* m_tracker = nullptr; // not owned
    // Starts false and stays false until the PAGE says otherwise — the link
    // never reads QSettings itself (widgets are told, pages read). The
    // shipped default now says "on", but that fact lives in prefs::, not
    // here: this object's job is to obey, not to have an opinion.
    bool m_enabled = false;
    bool m_engineWasRunning = false;     // detects the play EDGE
    TrackerService::State m_lastTrackerState = TrackerService::State::Idle;
};
