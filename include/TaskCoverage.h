#pragma once
// ---------------------------------------------------------------------------
// coverage — the pure brain of "needs a block" (needs-a-block addendum).
//
// One question, asked by every surface: does this open task have time set
// aside for it, and if not, should we say so? The glance panel asks it, the
// week view will ask it (part 3), a future secretary will ask it. So the
// answer lives HERE — pure functions of plain values — and NOT in a widget,
// for the same reason eventLabel() exists: two surfaces each computing their
// own version of a rule is how the versions start to disagree.
//
// Everything in this file is a function of its arguments. No AppData, no
// QSettings, no clock. AppData supplies the events (bounded to today-or-
// later, addendum §F); prefs:: supplies the Rule and Escalation; the caller
// supplies `now`. That is what makes the whole feature testable in
// milliseconds, headless — the same layering payoff test_domain.cpp opens
// by celebrating.
// ---------------------------------------------------------------------------

#include "Task.h"

#include <QDate>
#include <QDateTime>
#include <QVector>

namespace coverage
{

// ---- the flag rule's knobs (addendum §B) ----------------------------------
// Two independent conditions, OR'd. Defaults are the shipped defaults:
// urgent-only, 3-day window. Stored in QSettings via prefs:: — TASTE about
// what deserves a nudge, not a fact about any task.
struct Rule
{
    bool flagUrgent  = true;
    bool flagMedium  = false;
    bool flagLow     = false;
    int  dueWithinDays = 3;   // 0 = off (priority-only)

    bool flags(Task::Priority p) const
    {
        switch (p) {
        case Task::Priority::Urgent: return flagUrgent;
        case Task::Priority::Medium: return flagMedium;
        case Task::Priority::Low:    return flagLow;
        }
        return false; // unreachable — no default: the compiler guards
    }
};

// ---- the escalation ladder's knobs (addendum §D) --------------------------
struct Escalation
{
    int  decisionAfter = 3;   // rung 1: "Not today" becomes a decision
    int  pinAfterExtra = 3;   // rung 2: this many MORE dismissals -> pinned
    bool urgentOnly    = true;
};

// Why a task that HAS blocks still counts as uncovered — owed to the user
// whenever the app flags something they believe is handled (addendum §A).
// An enum, not a string: the UI chooses the words; the domain states the
// fact.
enum class Reason
{
    None,               // it IS covered (or has no blocks at all — see below)
    NoBlock,            // nothing references this task
    BlockAfterDeadline, // time exists, but on the wrong side of the deadline
    BlockInPast,        // time existed, it came and went, task still open
};

// ---- the deadline (addendum §A) -------------------------------------------
// deadline = max(dueDate, today). The clamp is what keeps an OVERDUE task
// satisfiable: without it, nothing placeable could ever cover a task due
// last week, and it would nag forever with no action that resolves it — the
// worst failure mode for exactly this app's user. An invalid dueDate means
// "no deadline": the window has no upper edge.
inline QDate deadlineOf(const Task& t, QDate today)
{
    if (!t.dueDate.isValid())
        return {}; // invalid = unbounded, the same "absence is a value"
                   // convention as the due date itself (§3.11)
    return qMax(t.dueDate, today);
}

// ---- coverage (addendum §A) -----------------------------------------------
// A block covers the task iff its date lies in [today, deadline]. The caller
// normally passes only today-or-later dates (the bounded scan, §F), but the
// lower bound is re-checked here anyway — a pure function shouldn't need to
// trust its caller's filtering to be correct.
inline bool isCovered(const Task& t, const QVector<QDate>& blockDates,
                      QDate today)
{
    const QDate deadline = deadlineOf(t, today);
    for (const QDate& d : blockDates) {
        if (!d.isValid() || d < today)
            continue;                                  // spent time doesn't count
        if (!deadline.isValid() || d <= deadline)
            return true;
    }
    return false;
}

// ---- the flag (addendum §B) -----------------------------------------------
// `covered` is passed in rather than recomputed so the caller controls the
// event scan (and its bounds) — this function judges, it doesn't gather.
// Dismissal is checked against `now`, not against expireDismissals having
// run: a stale timestamp in the past can never hide a task (addendum §C).
inline bool needsBlock(const Task& t, bool covered, const Rule& rule,
                       const QDateTime& now)
{
    if (t.done || t.archived)
        return false;
    if (t.dismissedUntil.isValid() && t.dismissedUntil > now)
        return false;
    if (covered)
        return false;

    if (rule.flags(t.priority))
        return true;                                   // condition 1

    if (!t.dueDate.isValid())
        return false;      // no date -> the window has nothing to measure
    const QDate today = now.date();
    if (t.dueDate < today)
        return true;                                   // overdue always counts
    // v22: a deadline with a CLOCK can lapse without the date changing.
    // "Due today 09:00" is late at 09:01, and the card should say so then —
    // not at midnight. Note this is the only place the time affects the
    // FLAG: coverage still reasons in whole days (you cannot plan a block
    // finer than the day the deadline falls on), so only the "is it already
    // blown" question needed sharpening. Untimed tasks skip this entirely,
    // which is why every pre-v22 test still reads the same answer.
    if (t.dueDate == today && t.dueTime.isValid()
        && t.dueMoment() < now)
        return true;
    return rule.dueWithinDays > 0
           && today.daysTo(t.dueDate) <= rule.dueWithinDays; // condition 2
}

// ---- the rung (addendum §D) -----------------------------------------------
// DERIVED from dismissCount on every read, never stored — change the
// threshold in Settings and every task re-rungs instantly with nothing to
// migrate. 0 = ordinary, 1 = "Not today" demands a decision, 2 = pinned
// past the gate.
inline int rung(const Task& t, const Escalation& esc)
{
    if (esc.urgentOnly && t.priority != Task::Priority::Urgent)
        return 0;
    if (t.dismissCount >= esc.decisionAfter + esc.pinAfterExtra)
        return 2;
    if (t.dismissCount >= esc.decisionAfter)
        return 1;
    return 0;
}

// ---- the "why" (addendum §A) ----------------------------------------------
// Only meaningful for an UNCOVERED task, and only called for the handful
// that are flagged — so unlike the flag path, this one may receive the
// task's FULL block history (it wants "time was set aside Monday; it didn't
// happen"). Precedence when both apply: the after-deadline block is the
// actionable mistake (movable), so it wins over the merely-lapsed one.
inline Reason uncoveredReason(const Task& t, const QVector<QDate>& blockDates,
                              QDate today)
{
    if (blockDates.isEmpty())
        return Reason::NoBlock;
    if (isCovered(t, blockDates, today))
        return Reason::None;

    const QDate deadline = deadlineOf(t, today);
    bool sawPast = false;
    for (const QDate& d : blockDates) {
        if (!d.isValid())
            continue;
        if (deadline.isValid() && d > deadline)
            return Reason::BlockAfterDeadline;
        if (d < today)
            sawPast = true;
    }
    return sawPast ? Reason::BlockInPast : Reason::NoBlock;
}

// ---- ordering (part 2 renders in this order) ------------------------------
// Pinned first (they follow you), then overdue (facts), then urgent
// (opinions you set), then the rest; ties broken by the caller on soonest
// due date, dateless last. A free function so the sort is one rule
// everywhere, like the flag — and `today` is an argument, because no pure
// function in this file is allowed to touch the real clock.
inline int rankAt(const Task& t, const Escalation& esc, QDate today)
{
    if (rung(t, esc) == 2)
        return 0;
    if (t.dueDate.isValid() && t.dueDate < today)
        return 1;
    if (t.priority == Task::Priority::Urgent)
        return 2;
    return 3;
}

} // namespace coverage
