#pragma once
// ---------------------------------------------------------------------------
// TrackerService — the live focus/break timer (UC2), the app's core value.
//
// This is the state machine from design-doc §3.8, "timer state as state,
// not subclasses": one State enum field, not an IdleTracker/FocusingTracker
// class hierarchy (Larman ch. 32 — states as subclasses cause a class
// explosion).
//
//                startFocus              startBreak
//        Idle ─────────────▶ Focusing ◀─────────────▶ OnBreak
//          ▲                    │        startFocus      │
//          └────── stop ────────┴──────── stop ──────────┘
//
// Every transition OUT of Focusing/OnBreak commits the interval that just
// ended as a Segment (real start & end timestamps) into the Event.
//
// WHY the live segment is NOT stored inside the Event while it runs: an
// open-ended, still-growing segment in the domain data would force every
// reader (stats, charts, save) to special-case "unless it's still running".
// Instead the domain holds only FINISHED facts; the in-flight interval
// lives here, plus its crash-insurance copy in AppData::running() (a tiny
// {eventId, kind, start, lastSeen} block, heartbeat-refreshed and saved).
//
// WHY a service object separate from the UI: the timer must keep running
// when the event dialog closes, when you switch to the Pomodoro tab —
// its lifetime is the app's, not any window's. One instance, owned by
// MainWindow, shared by reference with any screen that needs it.
// ---------------------------------------------------------------------------

#include "Segment.h"

#include <QDateTime>
#include <QObject>
#include <functional>
#include <QString>
#include <QTimer>

class AppData;

class TrackerService : public QObject
{
    Q_OBJECT

public:
    enum class State { Idle, Focusing, OnBreak, Distracted };

    explicit TrackerService(AppData* data, QObject* parent = nullptr);

    State   state() const           { return m_state; }
    QString trackedEventId() const  { return m_eventId; }
    bool    isTrackingEvent(const QString& eventId) const
    {
        return m_state != State::Idle && m_eventId == eventId;
    }

    // Seconds of the CURRENT, uncommitted interval — the UI adds this on
    // top of the committed totals so numbers grow live on screen.
    qint64 liveSeconds() const;

    // The id of the block whose planned window covers THIS INSTANT, or
    // empty. Unambiguous by domain law: blocks on a day cannot overlap,
    // so "the block under the clock" is at most one. Lives here (not in
    // the Pomodoro link that wanted it) because it's a pure question
    // about the schedule and the clock — the link is merely its first
    // customer (v19.7 adoption).
    QString liveEventNow() const;

    // (A plain public method, not a `slots:` section — PMF connects need
    // no moc registration, and a slots label here would swallow the
    // declarations after it into moc's jurisdiction.)
    // The EXIT door (v19.8, owner report: "the focus session should stop
    // when the planned block is finished" — their badge read Focusing at
    // 12:00 on a 10–12 block). canTrackNow() guards every START; this
    // guards the other boundary: called by the same 1-second tick that
    // repaints the UI (no new timer — the watcher was already running),
    // it commits and stops the moment the tracked block's window passes.
    // Public as a slot for the house test-seam reason: the timer calls it
    // in production; tests move the fake clock and call it directly.
    void enforceWindow();

    // Tracking honesty (§3.38): may tracking START (or switch kind) on
    // this event right now? True only while the block is LIVE — actuals
    // may only be written while the plan is actually happening. The three
    // start doors below enforce it; the UI uses this same query to grey
    // out the buttons. stop() is deliberately NOT guarded: stopping
    // records the truth of what already happened.
    bool canTrackNow(const QString& eventId) const;

    // TEST SEAM — "now" is a dependency like any other. Production leaves
    // this alone (wall clock); tests inject a fixed moment so liveness
    // verdicts never depend on WHEN the suite runs (a 3 AM CI run must not
    // fail: before 06:00 the domain's day hasn't even started, and no
    // block can be live at all). Same spirit as TICKTIMER_COMPACT: if a
    // condition can't be produced on demand, it can't be tested.
    std::function<QDateTime()> nowProvider =
        [] { return QDateTime::currentDateTime(); };

public slots:
    // moc lesson (cost one broken build): a `slots:` section may contain
    // ONLY function declarations — the query and the seam above first
    // landed here, and moc refused the member variable outright.
    // Starting on event B while tracking event A is legal: A's interval is
    // committed first, then B starts. One timer, app-wide, by design — you
    // are one person; you cannot focus on two blocks at once.
    void startFocus(const QString& eventId);
    void startBreak(const QString& eventId);
    void startDistracted(const QString& eventId); // off-task time, tracked too
    void stop();

signals:
    void stateChanged(); // Idle/Focusing/OnBreak flipped, or target moved
    // The tracked block's planned window just passed and tracking was
    // auto-stopped (the last interval is already committed). Carries the
    // id so listeners can NAME the block: the notifier toasts it, the
    // Pomodoro link pauses the engine it was driving.
    void trackedBlockEnded(const QString& eventId);
    void tick();         // once per second while tracking — repaint cue

private:
    void beginInterval(const QString& eventId, SegmentKind kind);
    void commitCurrentInterval(); // finished fact -> Segment -> AppData

    AppData*  m_data = nullptr;   // not owned; owned by MainWindow
    State     m_state = State::Idle;
    QString   m_eventId;
    SegmentKind m_kind = SegmentKind::Focus;
    QDateTime m_startedAt;

    QTimer m_secondTimer;    // drives tick() for live UI updates
    QTimer m_heartbeatTimer; // refreshes crash insurance every 30 s
};
