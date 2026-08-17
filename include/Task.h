#pragma once
// ---------------------------------------------------------------------------
// Task — a one-off, completable obligation: "Lab 4", "LOG410 FINAL".
// (Design addendum §3.9–§3.11; requirement extracted from the owner's
// TickTick screenshot.)
//
// Task completes the type-vs-instance family of this domain:
//
//     Activity  = a reusable TYPE of thing you do   ("Gym")
//     Event     = a dated PLAN to do one            ("Gym, Jul 4, 9:00")
//     Task      = a one-off OBLIGATION to finish    ("Lab 4, due Aug 8")
//
// It is deliberately NOT related to Activity or Event: "Lab 4" is not an
// occurrence of any reusable type, and putting `done` on the shared
// Activity would mark every gym block in history complete (§3.9 — a model
// must be able to say every truth and unable to say falsehoods).
//
// The due date: an invalid QDate IS the "no date yet" state — the
// screenshot's "DATE TBD", first-class. QDate already knows how to be
// absent (isValid()); a parallel bool would be a second source of truth
// waiting to disagree with the first (§3.11).
//
// GROWN (task-details addendum): a Task now also carries free-text
// `description` (the detail panel's notes field) and a `repeat` hint.
// Both are ADDITIVE and optional — an old task with neither reads as an
// empty description and Repeat::None, so nothing breaks. Note what repeat
// does and does NOT do yet: it is STORED and DISPLAYED, but the app does
// not yet regenerate a task when you finish it. Capturing the fact now,
// acting on it later, is a deliberate scoping line drawn in the addendum —
// recurrence behaviour was a stated non-goal, and honest data is cheaper
// to ship than half-built behaviour.
// ---------------------------------------------------------------------------

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QTime> // QDateTime already drags it in — named anyway, because we
                 // use QTime directly (include what you use)

struct Task
{
    // Recurrence is a small, closed set of choices -> an enum, not a string.
    // An enum makes illegal values UNREPRESENTABLE (you cannot store
    // "wekely"); a bare string would let a typo become data. This is the
    // same "make illegal states unrepresentable" instinct behind using an
    // invalid QDate for TBD instead of a bool.
    enum class Repeat { None, Daily, Weekly, Monthly, Yearly };

    // How much this task matters — a small, closed set, so an enum again.
    // Medium is the default: an unranked task is ordinary, not urgent (an
    // urgent-by-default world makes "urgent" meaningless within a week).
    enum class Priority { Urgent, Medium, Low };

    QString id;
    QString title;        // "Lab 4 — Exigences contractuelles (5%)"
    QString categoryId;   // belongs to a life area DIRECTLY (§3.10)
    bool    done = false;
    QDate   dueDate;      // default-constructed = invalid = "DATE TBD"

    // v22 addition (deadline time). The CLOCK half of a deadline: "Lab 4,
    // Aug 8, 23:59". An invalid QTime is "no particular time" — the all-day
    // deadline every task had before this field existed. That is the SAME
    // convention dueDate already uses for TBD (§3.11), reused deliberately:
    // one absence idiom in the struct, not two.
    //
    // Why not fold both into a single QDateTime? Because "due Aug 8, no
    // time" is a real and common state, and a QDateTime cannot express it —
    // it would have to lie (midnight? end of day?) and the lie would leak
    // into every comparison. Two fields, each able to be absent, can say
    // every truth the domain has (§3.9's rule again).
    //
    // A time without a date is meaningless, so it is never written alone:
    // clearing the date clears the time (AppData enforces this at the door).
    QTime   dueTime;

    // v7 additions (daily-driver addendum). ARCHIVED is the third life
    // stage: open -> done -> archived. Done tasks stay visible (today's
    // victories belong on today's list); ARCHIVING is the deliberate "get
    // it out of my sight" that moves them to the Archive page. Never
    // deletion — history stays intact.
    bool     archived = false;
    Priority priority = Priority::Medium;

    QString description;              // free-text notes; empty is normal
    Repeat  repeat = Repeat::None;    // recurrence hint (stored + shown)

    // v10 additions (needs-a-block addendum §C). Both are FACTS about the
    // task, so they live here and in data.json — and therefore sync:
    // dismissing on the laptop holds on the phone. (What does NOT live
    // here: the ReturnPolicy that computed the timestamp, and the gate's
    // "have I looked today" — both are per-device taste, in QSettings.)
    //
    // dismissedUntil: "don't flag me before this moment". Invalid = not
    // dismissed — QDateTime knows how to be absent, same convention as the
    // due date (§3.11). The flag rule compares against `now` directly, so
    // a lapsed timestamp can never hide a task even before housekeeping
    // (AppData::expireDismissals) tidies it away.
    //
    // dismissCount: how many times this task has been put off — the
    // evidence the escalation ladder reads. The RUNG is derived from it on
    // every read (coverage::rung), never stored. Reset on completion.
    QDateTime dismissedUntil;
    int       dismissCount = 0;

    // ---- v28.3 additions (subtasks, roadmap §I; sizing, §J.1) -------------
    //
    // A task may be a PIECE of another task: "read the spec" under "Lab 4".
    // The link lives on the CHILD as a parent id, not on the parent as a
    // list of children. Three reasons, in order of how much they matter:
    //
    //   1. m_tasks stays one flat vector. A nested list would mean two
    //      places a Task can live, and every existing query — 40-odd loops
    //      over m_tasks — would have to learn to recurse. A pointer up
    //      costs each query one boolean instead.
    //   2. Removal cannot corrupt the shape. Erasing a child from a flat
    //      vector leaves nothing behind; erasing it from a parent's list
    //      while something holds a pointer into that list is the classic
    //      iterator-invalidation bug this file already warns about.
    //   3. It round-trips as a string. One extra key in data.json, no
    //      nesting in the JSON either, so sync and sharing get it free.
    //
    // Empty parentId = a top-level task — which is exactly what every task
    // in a pre-v13 file is, so the migration is "do nothing" (§3.11's
    // absence-is-a-value idiom, fourth time).
    //
    // ONE LEVEL ONLY. A piece may not have pieces. That is not enforceable
    // by the type system here (a Task is a Task), so AppData::addSubtask
    // enforces it at the door — the same "when the type system can't, the
    // door does it" move as the time-without-a-date rule.
    QString parentId;

    // How long you think this will take, in minutes. 0 = unset, and unset
    // is honest: a made-up estimate is worse than no estimate, because the
    // multiplier (§J.2) would then divide by fiction.
    //
    // Why minutes and not a QTime or a duration type: it is arithmetic, not
    // a clock. Estimates get summed, subtracted from free minutes, and
    // multiplied by a coefficient. An int of minutes does all three; a
    // QTime does none of them and wraps at 24 hours.
    int  estimateMinutes = 0;

    // "Fits short gaps" — can this be chipped at in a 15-minute hole, or
    // does it need a real run at it? A FACT about the work, supplied by the
    // person doing it, that no amount of data could derive: two 90-minute
    // tasks can differ entirely here.
    bool chunkable = false;

    // Is this task a piece of another? Named rather than open-coded,
    // because the answer is asked at roughly a dozen call sites and
    // `!parentId.isEmpty()` reads like a string check, not a question
    // about the domain.
    bool isPiece() const { return !parentId.isEmpty(); }

    // An estimate of zero is "unset", never "instant" — so every reader
    // asks THIS rather than testing the int, and nobody accidentally
    // treats an unanswered question as a five-minute job.
    bool hasEstimate() const { return estimateMinutes > 0; }

    // Is there a clock on this deadline at all?
    bool hasDueTime() const { return dueDate.isValid() && dueTime.isValid(); }

    // The deadline as one moment. An all-day task's deadline is the END of
    // its day (23:59:59), never its start — a task due "Aug 8" is not late
    // at 00:01 on Aug 8. Invalid date in, invalid QDateTime out.
    QDateTime dueMoment() const
    {
        if (!dueDate.isValid())
            return {};
        return QDateTime(dueDate,
                         dueTime.isValid() ? dueTime : QTime(23, 59, 59));
    }

    // The date-only overdue test, unchanged — still correct for every caller
    // that reasons in whole days (the calendar strip, the archive, sorting).
    bool isOverdue(QDate today) const
    {
        return !done && dueDate.isValid() && dueDate < today;
    }

    // The time-aware overdue test. OVERLOAD, not a replacement: callers that
    // hold a QDate keep the cheap day comparison; callers that hold a real
    // instant get the sharper answer ("due today 14:00" is late at 14:01).
    // Both read identically at the call site — the argument type picks the
    // rule, which is exactly what overloading is for.
    bool isOverdue(const QDateTime& now) const
    {
        if (done || !dueDate.isValid())
            return false;
        return dueMoment() < now;
    }
};

// The one place the app turns a deadline time into words. 24-hour "HH:mm",
// matching every other clock in this UI (the agenda's slot labels, the
// put-off strip) — one time format everywhere beats a locale-perfect one
// here and a hand-rolled one there.
inline QString dueTimeLabel(QTime t)
{
    return t.isValid() ? t.toString(QStringLiteral("HH:mm")) : QString();
}

// ---- Repeat <-> text, and a human label -----------------------------------
// These live in the header as free `inline` functions on purpose: they are
// pure, tiny, and needed by BOTH the storage layer (JSON round-trip) and the
// UI layer (the combo box + the row chip). A header-side inline function is
// the C++ way to share a small helper across translation units without a
// separate .cpp — the ODR is satisfied because `inline` permits the
// definition in every file that includes it.

inline QString repeatToString(Task::Repeat r)
{
    switch (r) {
    case Task::Repeat::Daily:   return QStringLiteral("daily");
    case Task::Repeat::Weekly:  return QStringLiteral("weekly");
    case Task::Repeat::Monthly: return QStringLiteral("monthly");
    case Task::Repeat::Yearly:  return QStringLiteral("yearly");
    case Task::Repeat::None:    break;
    }
    return QStringLiteral("none");
}

inline Task::Repeat repeatFromString(const QString& s)
{
    if (s == QLatin1String("daily"))   return Task::Repeat::Daily;
    if (s == QLatin1String("weekly"))  return Task::Repeat::Weekly;
    if (s == QLatin1String("monthly")) return Task::Repeat::Monthly;
    if (s == QLatin1String("yearly"))  return Task::Repeat::Yearly;
    return Task::Repeat::None; // covers "none", "", and any unknown value
}

// The one date-advance rule for every recurrence in the app — tasks AND
// planned blocks share it (Event borrows Task::Repeat wholesale; see
// Event.h for why the enum stays here). Invalid QDate in, invalid out;
// None has no "next" and says so with an invalid date rather than a lie.
// Month/year arithmetic rides Qt's clamping (Jan 31 + 1 month = Feb 28/29)
// — the boring, predictable reading of "monthly on the 31st".
inline QDate nextOccurrence(QDate from, Task::Repeat r)
{
    if (!from.isValid())
        return {};
    switch (r) {
    case Task::Repeat::Daily:   return from.addDays(1);
    case Task::Repeat::Weekly:  return from.addDays(7);
    case Task::Repeat::Monthly: return from.addMonths(1);
    case Task::Repeat::Yearly:  return from.addYears(1);
    case Task::Repeat::None:    break;
    }
    return {};
}

// ---- Priority <-> text, colours, and ordering ------------------------------

inline QString priorityToString(Task::Priority p)
{
    switch (p) {
    case Task::Priority::Urgent: return QStringLiteral("urgent");
    case Task::Priority::Low:    return QStringLiteral("low");
    case Task::Priority::Medium: break;
    }
    return QStringLiteral("medium");
}

inline Task::Priority priorityFromString(const QString& s)
{
    if (s == QLatin1String("urgent")) return Task::Priority::Urgent;
    if (s == QLatin1String("low"))    return Task::Priority::Low;
    return Task::Priority::Medium; // covers "medium", "", unknowns — v6 files
}

inline QString priorityLabel(Task::Priority p)
{
    switch (p) {
    case Task::Priority::Urgent: return QStringLiteral("Urgent");
    case Task::Priority::Low:    return QStringLiteral("Low");
    case Task::Priority::Medium: break;
    }
    return QStringLiteral("Medium");
}

// For the UI: a display word, or empty for None (so the row shows no chip).
inline QString repeatLabel(Task::Repeat r)
{
    switch (r) {
    case Task::Repeat::Daily:   return QStringLiteral("Daily");
    case Task::Repeat::Weekly:  return QStringLiteral("Weekly");
    case Task::Repeat::Monthly: return QStringLiteral("Monthly");
    case Task::Repeat::Yearly:  return QStringLiteral("Yearly");
    case Task::Repeat::None:    break;
    }
    return QString();
}

// ---------------------------------------------------------------------------
// The pieces vocabulary (v28.3).
//
// A NAMING NOTE worth having once: the domain says "subtask", the UI says
// "piece". That is deliberate, not drift. "Subtask" is precise and tells a
// reader of this header exactly what the parentId does. "Break it into
// pieces" is what the feature is FOR — a paralysis-shaped task ("write lab
// report") becoming a twenty-minute action ("read the spec"), which is the
// whole point of §J.3. The code keeps the precise word; the button keeps
// the useful one.
//
// (The progress type, PieceCount, lives in AppData.h beside the query that
// fills it — v28.3.1 removed a second copy that briefly lived here; two
// definitions of one struct is an ODR violation and the compiler rightly
// refused. A SubtaskEdit handoff type also briefly lived here, part of a
// bulk-setSubtasks design that was never built — the shipped design is the
// seed/apply pair in TaskDetailDialog.h, and a type whose comment pointed
// at a door that doesn't exist would be a lie on disk.)
// ---------------------------------------------------------------------------
