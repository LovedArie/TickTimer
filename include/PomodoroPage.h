#pragma once
// ---------------------------------------------------------------------------
// PomodoroPage — the Pomodoro timer's FULL-SIZE face (UC5).
//
// History note worth keeping: this class used to BE the Pomodoro — phase,
// round, countdown, all private members here. The state machine moved out
// to PomodoroEngine the day three non-page consumers appeared (notifier,
// mini window, tracker link); what remains is a view plus the settings
// owner. Compare the diff of that refactor and notice what did NOT change:
// the ring, the dots, the buttons — views survive extractions precisely
// because they never owned the truth, only painted it.
//
// This page still owns every QSettings read/write for the Pomodoro
// (durations, notify toggle, link toggle) — doctrine: pages own settings;
// services and widgets are TOLD. The engine gets setDurations(); the link
// gets setEnabled(); neither ever sees a QSettings.
// ---------------------------------------------------------------------------

#include <QWidget>

class AppData;
class PomodoroEngine;
class PomodoroLink;
class TrackerService;
class PomodoroMiniWindow;
class QLabel;
class QPushButton;
class PomodoroRing;

class PomodoroPage : public QWidget
{
    Q_OBJECT

public:
    // tracker + data are here for one job: SAYING what the link is doing
    // (v19.6 — owner report: the link worked invisibly). The page never
    // commands the tracker; it reads state and names the block.
    PomodoroPage(PomodoroEngine* engine, PomodoroLink* link,
                 TrackerService* tracker, const AppData* data,
                 QWidget* parent = nullptr);
    ~PomodoroPage() override; // deletes the parentless mini card (v19.5.1)

private:
    void refresh();        // repaint every control from the engine's state
    void refreshHint();    // the durations sentence under the controls
    void refreshLinkStatus(); // the one line that makes the link VISIBLE
    void showMini();       // create-on-first-use, then just raise

    PomodoroEngine* m_engine = nullptr; // not owned; owned by MainWindow
    PomodoroLink*   m_link   = nullptr; // not owned; owned by MainWindow
    TrackerService* m_tracker = nullptr; // read-only here (status line)
    const AppData*  m_data    = nullptr; // read-only here (block's name)
    PomodoroMiniWindow* m_mini = nullptr; // lazily created; owned BY HAND —
                                           // it is parentless on purpose
                                           // (Windows owner semantics), so
                                           // Qt's tree can't delete it

    QLabel*       m_phaseLabel = nullptr;
    PomodoroRing* m_ring       = nullptr;
    QVector<QLabel*> m_dots;
    QPushButton*  m_startBtn   = nullptr;
    QLabel*       m_hint       = nullptr;
    QLabel*       m_linkStatus = nullptr;
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
