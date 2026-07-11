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
#include <QString>

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

    // v7 additions (daily-driver addendum). ARCHIVED is the third life
    // stage: open -> done -> archived. Done tasks stay visible (today's
    // victories belong on today's list); ARCHIVING is the deliberate "get
    // it out of my sight" that moves them to the Archive page. Never
    // deletion — history stays intact.
    bool     archived = false;
    Priority priority = Priority::Medium;

    QString description;              // free-text notes; empty is normal
    Repeat  repeat = Repeat::None;    // recurrence hint (stored + shown)

    bool isOverdue(QDate today) const
    {
        return !done && dueDate.isValid() && dueDate < today;
    }
};

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
