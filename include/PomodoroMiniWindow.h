#pragma once
// ---------------------------------------------------------------------------
// PomodoroMiniWindow — the pin-on-top pocket timer (owner request, built to
// their screenshot): play/pause, phase, mm:ss, expand, close — in a card
// that floats above every app while you work elsewhere.
//
// It owns NO timer state. It is a second VIEW of the same PomodoroEngine
// the page shows — pause here, the page shows Paused; a phase flips while
// you're in another app, this card repaints. One clock, two faces; the
// engine extraction exists so this class could be this small.
//
// Window recipe (each flag earns its place):
//   Qt::Tool               — no taskbar entry of its own
//   Qt::FramelessWindowHint — the card IS the window; no title bar
//   Qt::WindowStaysOnTopHint — the whole point: visible over other apps
//   WA_TranslucentBackground — lets the rounded corners actually be round
// Frameless costs the OS-provided drag, so mousePress/Move reimplement it
// (the standard press-anchor + delta dance). Position persists in QSettings
// (window state = machine taste, the same shelf as every other preference)
// — and is validated against today's monitors on every show
// (`ensureOnScreen`), because a remembered position outlives the display it
// was remembered on.
//
// PARENTLESS, deliberately (v19.5.1 fix — owner report: the card vanished
// whenever the main window was minimized). The first version passed the
// main window as parent "for memory ownership only" — but on Windows a
// widget parent on a top-level window is never only memory: Qt maps it to
// a Win32 OWNER, and Windows hides owned windows while their owner is
// minimized. A pin-on-top card that dies when you minimize the app is the
// opposite of its job. So the parent is gone, and the two things it was
// quietly providing are now provided explicitly:
//   - MEMORY: PomodoroPage deletes the card in its destructor;
//   - APP LIFETIME: WA_QuitOnClose(false), so the card doesn't count as
//     "a window still open" — closing the main window still quits the app
//     even with the card floating (a face, not a second app).
// ---------------------------------------------------------------------------

#include <QPoint>
#include <QWidget>

class PomodoroEngine;
class QLabel;
class QPushButton;
class QShowEvent;
class QToolButton;

class PomodoroMiniWindow : public QWidget
{
    Q_OBJECT

public:
    explicit PomodoroMiniWindow(PomodoroEngine* engine);

signals:
    void expandRequested(); // ⤢ — bring the real window back

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void showEvent(QShowEvent* event) override; // re-asks ensureOnScreen()

private:
    void refresh();

    // The remembered position outlives the monitor it was saved on, so it is
    // checked against today's screens every time the card appears — not once
    // at construction. See the long note on the definition.
    void ensureOnScreen();

    PomodoroEngine* m_engine = nullptr; // not owned; owned by MainWindow

    QPushButton* m_playBtn    = nullptr;
    QToolButton* m_phaseBtn   = nullptr; // "Focus ›" — a quiet skip button
    QLabel*      m_timeLabel  = nullptr;

    QPoint m_dragAnchor;   // where inside the card the press landed
    bool   m_dragging = false;
};
