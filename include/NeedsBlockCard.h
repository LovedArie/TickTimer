#pragma once
// ---------------------------------------------------------------------------
// NeedsBlockCard — the glance panel's review surface (needs-a-block §E,
// part 2). Renders THE derived list (AppData::tasksNeedingBlock) in one of
// three shapes:
//
//   GATE     — anything qualifies and you haven't looked since the review
//              clock last re-armed: the card IS the panel; the day's
//              numbers wait behind "Show my day".
//   STRIP    — you've looked: pinned (rung-2) tasks stay visible, the rest
//              collapse to an expandable one-liner, the numbers show.
//   CLEAR    — nothing qualifies: a quiet ✓ line (plus the put-off strip
//              if any dismissals are live) and the panel is exactly what
//              it always was.
//
// THE GATE DERIVATION — worth reading twice, it's the whole state machine:
//
//     gateOpen  =  reviewPolicy.nextReturn(lastReview)  >  now
//
// ReturnPolicy's third consumer, with zero new code: "when would the review
// come back after that look?" — if that moment is still ahead, the look
// still counts. Daily-06:00 + reviewed 07:00 -> open until tomorrow 06:00;
// every-4-hours -> open for 4 hours; never reviewed (invalid lastReview)
// -> closed. No stored open/closed flag anywhere — derive-don't-store,
// applied to UI state.
//
// Const-correctness as architecture, again: this widget holds a CONST
// AppData and cannot mutate it — every action is a SIGNAL, and PlannerPage
// (which owns the mutable pointer) decides. The one thing the card writes
// itself is prefs::needsBlockLastReview — QSettings, per-device taste, the
// gate's own memory (§C's classification table).
//
// HEIGHT (v22 -> v22.1, a lesson in two acts). The original bug: rows went
// straight into a QVBoxLayout, whose minimumSizeHint is the SUM of its
// children; that number climbs to the QMainWindow, and Qt refuses to shrink
// a window below its minimum — twenty due tasks froze the window.
//
// v22 fixed it with a QScrollArea capped at a fixed 280px. The ceiling
// worked; the sizing didn't. A scroll area's sizeHint is a CACHED GUESS
// about its content, and the card shipped squashed to two lines over an
// empty panel (the owner's screenshot). The deeper mistake: the card was
// still trying to decide its own height.
//
// v22.1 keeps what was right and deletes what guessed:
//   * The QScrollArea STAYS — its minimum is small and constant, and that
//     is the actual cure for the frozen window.
//   * The fixed ceiling and the "+N more" fold are GONE. The card no longer
//     sizes itself: GlancePanel moves the layout stretch to the card when
//     the gate closes (the card IS the panel then — §E) and back to the day
//     content when it opens. Whoever is on stage gets the room; the scroll
//     bar appears only when the room genuinely runs out.
//
// GATE PRESENTATION — design A, "focus" (owner pick, v22.4). The gate shows
// ONE task: the top-ranked one, big enough to read across the room, with its
// due line, its why-line, and its two actions. The rest are counted in the
// header ("1 of 5") and reachable in one click, not stacked underneath.
//
// The reasoning is the same one that justifies the gate existing at all. A
// list of five overdue things is a status report; one thing with two buttons
// is a decision. This app is built for a person who stalls in front of the
// status report — so the panel asks for one decision, then offers the next.
// coverage::rankAt already puts the right task first, so "the top one" is a
// real answer rather than an arbitrary pick.
//
// The rebuild uses deleteLater, not delete: a dismiss click mutates ->
// changed() -> refresh() -> rebuild rows — with the clicked button's
// handler STILL ON THE STACK. That is letter-for-letter the crash that
// founded test_ui.cpp; the cure is the same as it was then.
// ---------------------------------------------------------------------------

#include "TaskCoverage.h"

#include <QDateTime>
#include <QFrame>

class AppData;
class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class SlidePanel;

class NeedsBlockCard : public QFrame
{
    Q_OBJECT

public:
    explicit NeedsBlockCard(const AppData* data, QWidget* parent = nullptr);

    // Re-derive for `now` — injected, never read from the wall clock, so
    // tests can walk the gate through its whole day in microseconds
    // (the nowProvider doctrine, fourth application).
    //
    // v22.2: refresh is now GATED by a fingerprint of everything the card
    // renders, and only rebuilds when that fingerprint changes. The reason
    // is a click-eating bug, not performance: the glance panel refreshes
    // once per second while a timer runs, and an unconditional rebuild
    // destroyed every widget in the card each tick — INCLUDING the button
    // the user was mid-click on. A real click is press → ~100ms → release,
    // delivered to the widget that took the press; when a rebuild lands in
    // that window the release arrives at a deleteLater'd corpse and
    // clicked() never fires. ("Show my day does nothing.") Programmatic
    // click() in tests bypasses input delivery, which is why the suite
    // never saw it.
    //
    // Note what is cached: a print of the RENDERING inputs, not the data.
    // rebuild() itself is still a pure re-derivation every time it runs —
    // the derive-don't-store contract holds; we merely stopped re-deriving
    // into fresh widgets when the answer is provably identical.
    void refresh(const QDateTime& now);

    // The panel asks these to decide what ELSE to show.
    bool gateClosed() const { return m_gateClosed; }
    bool hasAnything() const { return m_hasAnything; }

    // Who the slide-over covers (v26.7.5). This used to be implicit —
    // "host = parentWidget()" — which held only as long as the card sat
    // directly in the glance panel. v26.7.1 re-parented the card into a
    // pill-height review row, and the drawer silently started opening over
    // a 40px strip: invisible. An unwritten contract broke without a
    // compiler, a test, or a review noticing — so now it is a written one:
    // the panel INJECTS the host. Unset (tests, bare embeddings) falls back
    // to parent-then-self as before.
    void setDrawerHost(QWidget* host) { m_drawerHost = host; }

signals:
    // "Find time" — part 2 routes this to the day view; part 3 grows the
    // multi-day strip. The card only ever asks.
    void planTaskRequested(const QString& taskId);
    // Decision menu (rung >= 1). Deadline edits reuse DueDateDialog — the
    // page opens it, setTaskDueDate stays the one door (§D).
    void editDeadlineRequested(const QString& taskId);
    void notUrgentRequested(const QString& taskId);
    // "Not today" / "put it off anyway" — the PAGE computes `until` from
    // prefs::dismissReturnPolicy and calls the domain door; the card
    // doesn't know what time it is in the user's settings.
    void dismissRequested(const QString& taskId);
    void bringBackRequested(const QString& taskId);
    // "Show my day" was pressed (lastReview already written).
    void reviewed();

private:
    // Everything rebuild() renders, flattened to one string: gate inputs,
    // each listed task's visible fields, the put-off set, and the card's own
    // expansion state. Equal print == pixel-identical card == skip.
    QString fingerprint(const QDateTime& now) const;
    // "Has the owner looked recently?" — answered in ONE place for both the
    // rebuild and the fingerprint, from two witnesses: the stored timestamp
    // (QSettings, survives restarts) OR this session's in-memory one (v22.7,
    // survives broken settings). Two derivations of this predicate is how
    // the gate and the print would eventually disagree.
    bool lookedRecently(const QDateTime& now) const;
    void rebuild(const QDateTime& now);
    // The drawer, created lazily on first open — the card's PARENT hosts it
    // (the glance panel), so the sheet can cover the whole column instead of
    // being clipped to this card's rectangle. Lazy because at construction
    // time the card has no parent yet; by the first click it does.
    SlidePanel* drawer();
    // (Re)fill the drawer for m_drawerMode from current data. Called from
    // rebuild's tail, which means every data change that reaches the card —
    // a task planned from inside the drawer, a put-off lapsing — refreshes
    // the open drawer for free, through the exact pipeline everything else
    // already uses.
    void fillDrawer(const QDateTime& now);
    // One builder, two densities. `focus` swaps the compact list row for the
    // hero presentation the gate uses — bigger title, an accent rail instead
    // of a dot, full-width actions. Deliberately a PARAMETER and not a second
    // function: the facts shown, the escalation rules, the decision menu and
    // the signals are identical, and the day someone adds a field to one
    // copy but not the other is the day the two presentations start lying
    // about the same task.
    QWidget* makeTaskRow(const Task& task, int rung, const QDateTime& now,
                         bool focus = false);
    QWidget* makeDecisionMenu(const Task& task);

    const AppData* m_data;
    QVBoxLayout*   m_layout = nullptr; // the ROWS' layout (lives in m_body)
    QScrollArea*   m_scroll = nullptr; // the height ceiling
    QWidget*       m_body   = nullptr; // scrolled content

    bool m_gateClosed  = false;
    bool m_hasAnything = false;
    // Expansion state survives rebuilds (they happen on every changed()):
    QString m_decisionFor;      // task whose "Not today…" menu is open
    // Which list the side panel is showing (v22.9): 0 = closed, 1 = the
    // tasks still needing a block, 2 = the put-off tasks. This replaces the
    // two inline-accordion flags — expansion is a NAVIGATION now (a drawer
    // over the panel), not a mutation of the page, so one enum-ish int
    // says everything the two bools plus their layout consequences used to.
    int m_drawerMode = 0;
    // "Show all N" inside the gate (design A). Note this is NOT the v22 fold
    // returning: that one hid rows to protect a fixed height, a bound that
    // belonged to the layout. This one is the FEATURE — one task at a time,
    // by choice — with the full list one click away for when you want to
    // survey rather than act. Same widget, opposite motivation.
    bool m_showAll           = false;
    QString m_lastPrint; // last rendered fingerprint (v22.2 click-safety)
    // The review THIS card witnessed, held in RAM (v22.7). The field bug:
    // the click wrote lastReview to QSettings, the rebuild re-read it, got
    // nothing back (settings writes failing on the owner's machine), and
    // re-closed the gate — press flips the label, release snaps everything
    // back. The disk is allowed to forget; the widget that watched you click
    // is not. QSettings stays the CROSS-SESSION memory; this is the session
    // truth no storage failure can overrule.
    QDateTime m_sessionReview;
    SlidePanel* m_drawer = nullptr;
    QWidget*    m_drawerHost = nullptr; // injected by the panel (v26.7.5)
};
