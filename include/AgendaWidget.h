#pragma once
// ---------------------------------------------------------------------------
// AgendaWidget — the day timeline: 36 half-hour slots from 6 AM to midnight,
// with planned Events drawn as coloured blocks (the prototype's agenda).
//
// This is your first fully CUSTOM-PAINTED Qt widget, and it teaches the
// pattern behind every chart and calendar you'll ever build in Qt:
//
//   1. paintEvent(): draw the current state with QPainter. Never cache
//      pixels, never mutate state here — just read data and draw it.
//   2. mouse events: turn a click position back into a MEANING ("slot 7",
//      "event e42") with the same geometry math, then emit a signal.
//   3. update(): whenever the data changes, schedule a repaint. Qt calls
//      paintEvent again and the widget redraws from scratch.
//
// The widget renders; the PAGE decides what a click means. That's why it
// emits signals instead of opening dialogs itself — it stays reusable and
// ignorant of the rest of the app.
// ---------------------------------------------------------------------------

#include <QDate>
#include <QPoint>
#include <QWidget>

#include "Widgets.h" // isCompactScreen

class AppData;
class TrackerService;
class Event;

class AgendaWidget : public QWidget
{
    Q_OBJECT

public:
    AgendaWidget(const AppData* data, const TrackerService* tracker,
                 QWidget* parent = nullptr);

    void setDate(QDate date);
    QDate date() const { return m_date; }

    // Slot geometry, made public so the week view's shared time-axis lines up
    // with these columns to the pixel. One source of truth for the grid — a
    // sibling widget reading the SAME numbers can't drift out of alignment.
    // Pixels per 30-minute slot. A FUNCTION rather than a constant since
    // v30.7, because a half-hour is the calendar's primary tap target and
    // 30dp was well under Android's 48.
    //
    // 44, not 48, and the shortfall is deliberate. A timeline is a canvas,
    // not a row of buttons: every extra pixel per slot is an hour less of
    // the day on screen, and at 48 an 18-hour day needs nearly two full
    // screens of scrolling before you can see the afternoon. 44 clears
    // WCAG 2.5.8's 24dp floor comfortably, is within 4dp of Material's
    // guideline, and keeps the day readable. touch::meetsFloor() is the
    // predicate that says this is a considered compromise and not a defect.
    //
    // Still ONE answer app-wide: the week view's shared axis and both sides
    // of the compare dialog line up only because every column asks the same
    // question, and they are all built under the same screen.
    static int slotHeight() { return isCompactScreen() ? 44 : 30; }
    static constexpr int kTopPad       = 12; // headroom above the first line
    static constexpr int kDefaultGutter = 64; // hour-label column width
    // A phone's whole width is 360, so 64 of it is 18% spent on "12 PM".
    // 44 still fits the widest label at the theme's 13px and hands 20px back
    // to the part of the page that is actually the day.
    static constexpr int kCompactGutter = 44;
    // Which of the two applies (v30.8). PlannerPage already chose between
    // them by hand; the WEEK view did not, and its axis stayed 64dp on a
    // phone — 64 off the top before seven day columns divide what is left,
    // which measured 41dp each. One helper so the day view and the week view
    // cannot drift apart.
    //
    // `compact` is a PARAMETER, not a lookup, and the distinction is the one
    // Responsive.h keeps: a container can be narrow on a wide device (a
    // docked panel) and the two answers legitimately differ. PlannerPage
    // knows its container's mode and passes that; the week view has no mode
    // plumbing of its own and asks the device. Taking the argument is what
    // lets both be right — slotHeight() above deliberately does NOT, because
    // seven columns and one axis must agree on a row height or 9 AM stops
    // being one horizontal line.
    static int gutterWidth(bool compact)
    {
        return compact ? kCompactGutter : kDefaultGutter;
    }

    // Render as a narrow COLUMN with no label gutter (px = 0) for the week
    // view, where one shared axis serves all seven days. The default keeps the
    // single-day agenda pixel-identical — this is purely additive.
    void setGutter(int px);

    // TEST SEAM. Whether a press is touch or mouse is decided by
    // QMouseEvent::source(), which Qt sets when IT synthesises the event and
    // which no public API can forge — so a unit test cannot produce a touch
    // press at all. Same doctrine as TrackerService::nowProvider and
    // TICKTIMER_COMPACT: a behaviour that cannot be produced on demand is a
    // behaviour that cannot be verified.
    void setTouchGesturesForTesting(bool on) { m_forceTouch = on; }

    // Display preference: paint the linked task's DESCRIPTION on the block
    // (indented under the task line). The widget is TOLD the preference —
    // it never reads QSettings itself. Settings are the page's business;
    // a painter that reaches into app configuration stops being reusable
    // (the same widget serves day view, week columns, and the screenshot
    // tool, each of which may want a different answer someday).
    void setShowTaskDescriptions(bool show);

    // Display preference (settings addendum): which part of the domain grid
    // this widget PAINTS, in minutes after midnight. A window over the day,
    // never a new day: the legal planning range stays plan::* — slot indices
    // in signals, minutes in events, and every consumer of both are
    // untouched. Told by the page (from prefs::agendaWindow), like the
    // preference above.
    void setVisibleWindow(int startMinutes, int endMinutes);

    // The one honesty rule of the window, as a PURE, shared function:
    // the range that must be shown for `date` = the preference window,
    // stretched (never shrunk) to cover every event on that date. A
    // narrowed view may hide empty hours; it may never hide a block.
    // Static and public because multi-column containers (week view,
    // compare) need the SAME math to compute a union window that keeps
    // sibling columns pixel-aligned — one formula, every consumer.
    static QPair<int, int> windowCovering(const AppData* data, QDate date,
                                          int prefStartMin, int prefEndMin);

    // Placement highlighting (needs-a-block part 3): the page hands this
    // widget a set of free minute-ranges to invite clicks into; empty
    // clears. Pure presentation — the runs are COMPUTED by the page and
    // clicks still travel the ordinary emptySlotClicked path, so this
    // widget stays what it has always been: a reporter, not a decider.
    void setHighlightRuns(QVector<QPair<int, int>> runs);

signals:
    void emptySlotClicked(int slotIndex);      // "plan something at 9:00"
    void eventClicked(const QString& eventId); // "open this block"
    // "the user dragged an edge — please set this span". The widget only
    // REPORTS; the page routes it to AppData::resizeEvent, which enforces the
    // rules and can refuse. (m_data is const here — the widget couldn't mutate
    // even if it wanted to.)
    void eventResized(const QString& eventId, int newStartMin, int newEndMin);

protected:
    // `override` (C++11) makes the compiler VERIFY we are really overriding
    // a virtual from QWidget — a typo in the signature becomes a compile
    // error instead of a silently-never-called function.
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override; // commit a resize drag
    void leaveEvent(QEvent* event) override;
    QSize sizeHint() const override;
    bool event(QEvent* event) override; // UngrabMouse cancels a pending tap

private:
    // ---- touch: a finger that means to SCROLL must not plan a block --------
    //
    // On a touchscreen the very first touch of a scroll drag arrives as a
    // mouse press, and this widget used to act on press — so trying to scroll
    // the day opened the "what are you doing?" dialog every time. On a phone
    // the two gestures start identically and can only be told apart by what
    // happens NEXT, so the decision has to be deferred:
    //
    //   empty slot  -> TWO TAPS, or one long press. The first tap ARMS the
    //                  slot and says "+ plan · tap again"; the second opens
    //                  the planner. Planning is a creating act, so it is
    //                  deliberate either way, and neither gesture can happen
    //                  by accident while scrolling.
    //   an existing
    //   block       -> tap, decided on RELEASE if the finger did not move.
    //                  Opening something is not creating something, and
    //                  "hold to open" would be a surprising gesture.
    //
    // Either way, MOVEMENT cancels: that is a scroll. So does losing the
    // mouse grab, which is how QScroller announces it has taken the gesture
    // over to pan — without that cancel the timer would still fire mid-flick.
    //
    // A real mouse keeps the old immediate behaviour; the ambiguity does not
    // exist there, and adding a hold to a desktop click would be a regression.
    void cancelPendingTouch();

    // ARMED, as distinct from hovered. A touchscreen has no hover: the "+ plan"
    // you see after one tap was an accident of Android synthesising a mouse
    // MOVE at the touch point, and because leaveEvent never fires on a phone
    // it never cleared — a stale invitation sat on a slot the finger had left
    // minutes earlier. Making it a real state fixes that and gives the second
    // tap something to mean.
    void disarm();

    class QTimer* m_longPress = nullptr;
    QPoint  m_touchPressPos;
    int     m_pendingSlot = -1;    // empty slot awaiting a long press
    int     m_armedSlot   = -1;    // tapped once; a second tap plans it
    bool    m_pressWasArmed = false; // this press landed on the armed slot
    bool    m_forceTouch    = false; // test seam; see setTouchGesturesForTesting
    QString m_pendingEventId;      // existing block awaiting a stationary tap

private:
    // Which horizontal edge of an event the mouse is near (the grab handles).
    enum class Edge { None, Top, Bottom };

    QRect spanRect(int startMin, int endMin) const; // minutes -> pixel rect
    QRect eventRect(const Event& e) const;          // = spanRect of its span
    int   slotAt(const QPoint& pos) const;          // paint AND hit-testing
    int   minutesAtY(int y) const;                  // snap a y to a slot time

    // The window this widget actually shows TODAY: windowCovering() of the
    // preference and the current date. Derived on demand, never cached —
    // an event added outside the window changes the answer, and stale
    // geometry is exactly the bug caching would invite (§3.5 again).
    QPair<int, int> shownWindow() const;
    int  firstShownSlot() const; // domain slot index of the top row
    int  shownSlotCount() const;
    int  slotTop(int slotIndex) const; // domain slot -> y (window-aware)
    void syncHeight();                 // minimum height follows the window
    // Is `pos` near a resizable edge? Returns the edge and, via eventId, which
    // event — the hit-test twin of how edges are DRAWN, so they never drift.
    Edge  edgeAt(const QPoint& pos, QString* eventId) const;

    const AppData*        m_data;    // read-only view of the data
    const TrackerService* m_tracker; // for the live-growing mini bar
    QDate m_date;
    int   m_gutter    = gutterWidth(isCompactScreen()); // 0 in week mode
    // The PREFERENCE window (initialized to the full day in the ctor, so
    // every existing caller and test sees the historical widget). What is
    // actually painted is shownWindow() — the preference stretched over the
    // date's events. (Initialized in the .cpp, not here: naming plan::*
    // would drag Event.h into this header for two integers.)
    int   m_windowStart;
    int   m_windowEnd;
    bool  m_showTaskDescriptions = true; // display preference, page-supplied
    int   m_hoverSlot = -1;          // empty slot under the mouse, or -1
    // Free minute-ranges to paint as click-me invitations while a task is
    // being placed (needs-a-block part 3). Empty = no placement running.
    QVector<QPair<int, int>> m_highlightRuns;

    // ---- resize-drag state (only meaningful while m_resizing) --------------
    bool    m_resizing     = false;
    QString m_resizeEventId;
    Edge    m_resizeEdge   = Edge::None;
    int     m_previewStart = 0; // the span shown live during the drag; the
    int     m_previewEnd   = 0; // fixed edge stays put, the grabbed edge moves
};
