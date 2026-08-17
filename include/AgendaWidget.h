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
#include <QWidget>

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
    static constexpr int kSlotHeight   = 30; // pixels per 30-minute slot
    static constexpr int kTopPad       = 12; // headroom above the first line
    static constexpr int kDefaultGutter = 64; // hour-label column width

    // Render as a narrow COLUMN with no label gutter (px = 0) for the week
    // view, where one shared axis serves all seven days. The default keeps the
    // single-day agenda pixel-identical — this is purely additive.
    void setGutter(int px);

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
    int   m_gutter    = kDefaultGutter; // 64 by default; 0 in week-column mode
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
