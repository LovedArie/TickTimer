#pragma once
// ---------------------------------------------------------------------------
// PomodoroEngine — the Pomodoro countdown machine, extracted from the page.
//
// Why the extraction (the TrackerService lesson, second verse): three new
// consumers need this state and none of them is the page —
//   - a NOTIFICATION must fire when the page isn't even visible,
//   - the MINI window must show the SAME timer (two clocks that could
//     disagree would be a lie),
//   - the tracker LINK needs something signal-emitting to listen to.
// State whose lifetime is the app's belongs in a service owned by
// MainWindow; windows are views of it. The page keeps its widgets and its
// QSettings reads (doctrine: pages own settings) and TELLS the engine its
// durations — the engine reads nothing and records nothing, so it is still
// liftable into another project unchanged.
//
// Two state machines, compared once more (the old page comment, upgraded):
// TrackerService is driven by USER COMMANDS and writes finished facts into
// the domain; this engine is driven by a COUNTDOWN and writes nothing —
// which is exactly why linking them is an ADAPTER's job (PomodoroLink),
// not a merge. Each machine stays whole.
// ---------------------------------------------------------------------------

#include <QObject>
#include <QTimer>

class PomodoroEngine : public QObject
{
    Q_OBJECT

public:
    enum class Phase { Focus, ShortBreak, LongBreak };
    static constexpr int kRoundsPerCycle = 4; // long break every 4th round

    explicit PomodoroEngine(QObject* parent = nullptr);

    // ---- read side (views derive everything from these) --------------------
    Phase phase() const     { return m_phase; }
    bool  running() const   { return m_running; }
    int   remaining() const { return m_remaining; }
    int   round() const     { return m_round; }
    int   phaseTotalSeconds() const;

    // Engaged = a cycle is in progress (started and not reset). The
    // distinction PAUSED-vs-ABANDONED lives here as honest state instead of
    // a heuristic: paused keeps engaged=true (the link reads that as
    // "distracted — they'll be back"); reset clears it (hands off the
    // tracker; walking away isn't a statement about the block).
    bool engaged() const { return m_engaged; }

    // "05:00" — one formatter so the page, the mini window, and any future
    // view can never disagree on padding.
    QString timeText() const;
    QString phaseName() const;

public slots:
    void start();
    void pause();
    void toggle() { m_running ? pause() : start(); }
    void reset();
    void skip();  // deliberate jump — advances WITHOUT phaseEnded (no toast
                  // for something you just asked for with your own hands)
    void setDurations(int focusMin, int shortBreakMin, int longBreakMin);

    // One second of countdown. The internal QTimer calls this in
    // production; tests call it directly — same seam-for-determinism idea
    // as TrackerService::nowProvider, but even simpler: time doesn't need
    // faking when the caller IS the clock.
    void tickOneSecond();

signals:
    // Fine-grained on purpose — each consumer subscribes to exactly the
    // grain it needs, so the link is NOT poked once per second:
    void changed();     // anything visible moved (every tick) — for views
    void modeChanged(); // the (running, phase, engaged) tuple flipped —
                        // for the link; fires on start/pause/reset/advance
    void phaseEnded(PomodoroEngine::Phase finished,
                    PomodoroEngine::Phase next); // countdown hit zero —
                                                 // for the notifier only

private:
    void advancePhase();

    Phase m_phase     = Phase::Focus;
    int   m_remaining = 25 * 60; // reshaped by setDurations before any view
    int   m_round     = 1;
    bool  m_running   = false;
    bool  m_engaged   = false;

    int m_focusMinutes      = 25;
    int m_shortBreakMinutes = 5;
    int m_longBreakMinutes  = 15;

    QTimer m_timer;
};
