#pragma once
// ---------------------------------------------------------------------------
// PomodoroPage — the standalone Pomodoro timer (UC5): 25 min focus,
// 5 min break, a 15-min long break every 4th round.
//
// A second state machine to compare with TrackerService — same pattern
// (an enum field + transitions), different shape: this one is driven by a
// COUNTDOWN (remaining seconds hit zero -> advance phase) where the
// tracker is driven by USER COMMANDS. Seeing the same pattern twice in
// different clothes is how patterns stick.
//
// Deliberately self-contained: it records nothing into AppData. The design
// docs keep UC5 separate from UC2's segments; linking a Pomodoro run to a
// planned block is future work, and gluing them prematurely would tangle
// two clean machines. Note what that buys: this page takes no AppData*,
// so you could lift the whole file into another project unchanged.
// ---------------------------------------------------------------------------

#include <QTimer>
#include <QWidget>

class QLabel;
class QPushButton;
class PomodoroRing;

class PomodoroPage : public QWidget
{
    Q_OBJECT

public:
    explicit PomodoroPage(QWidget* parent = nullptr);

private slots:
    void tick();          // one second passed
    void toggleRunning(); // Start <-> Pause
    void reset();
    void skip();          // jump to the next phase (preview / bail out)

private:
    enum class Phase { Focus, ShortBreak, LongBreak };

    int  phaseTotalSeconds() const;
    void advancePhase();
    void refresh();
    void applyDurations(); // re-read the durations into the live timer + hint

    Phase m_phase   = Phase::Focus;
    int   m_remaining = 0; // seconds; seeded from settings in the ctor
    int   m_round   = 1;
    bool  m_running = false;
    QTimer m_timer;

    // The durations the user picks are SETTINGS, not domain data: they must
    // survive a restart, but they are NOT facts about the user's time, so
    // they live in QSettings (see the task-details addendum), never in
    // data.json. The classic Pomodoro values are the defaults.
    int m_focusMinutes      = 25;
    int m_shortBreakMinutes = 5;
    int m_longBreakMinutes  = 15;

    QLabel*       m_phaseLabel = nullptr;
    PomodoroRing* m_ring       = nullptr;
    QVector<QLabel*> m_dots;
    QPushButton*  m_startBtn   = nullptr;
    QLabel*       m_hint       = nullptr; // dynamic: reflects current durations
};

// The countdown ring: progress arc, mm:ss, and the round number — one more
// custom-painted widget, this time built around drawArc.
class PomodoroRing : public QWidget
{
public:
    explicit PomodoroRing(QWidget* parent = nullptr);
    void setState(double progress, const QString& timeText,
                  const QString& roundText, const QColor& color);

protected:
    void paintEvent(QPaintEvent* event) override;
    QSize sizeHint() const override { return {230, 230}; }

private:
    double  m_progress = 0.0; // 0..1 of the phase elapsed
    QString m_timeText, m_roundText;
    QColor  m_color;
};
