#pragma once
// ---------------------------------------------------------------------------
// Event — an Activity placed on the calendar for a planned time range on a
// given day. This is your INTENTION. Its Segments are what REALLY happened.
// Keeping both, separately, is the whole point of the app (design-doc §3.2).
//
// PLANNING GRANULARITY — a doc-vs-prototype correction:
// The design doc (§3.1) said hourly slots, 6 AM–11 PM. The validated
// prototype actually uses 30-MINUTE slots from 6 AM to MIDNIGHT, and that is
// the UX we shipped to ourselves — so the code follows the prototype and the
// design doc gets updated. (Lesson: when doc and validated reality disagree,
// reality wins and the doc is fixed, never quietly ignored.)
//
// WHY plannedStartMinutes/plannedEndMinutes are ints (minutes after midnight)
// and not QTime, as the design doc sketched: QTime cannot represent 24:00,
// and our last slot ends exactly at midnight (1440). An int can say 1440;
// QTime cannot. The domain stores the honest number; the UI formats it.
// This is the kind of small, documented deviation implementation forces —
// the doc's *intent* (a planned time range) is preserved exactly.
// ---------------------------------------------------------------------------

#include "Segment.h"

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QVector>

// The planning grid is a DOMAIN rule (design-doc §3.1), not a UI detail:
// "you plan in 30-minute slots between 6 AM and midnight" would be true even
// if the app were a command-line tool. So the constants live here.
namespace plan
{
inline constexpr int kSlotMinutes      = 30;                 // one slot
inline constexpr int kDayStartMinutes  = 6 * 60;             // 06:00
inline constexpr int kDayEndMinutes    = 24 * 60;            // midnight
inline constexpr int kSlotsPerDay      = (kDayEndMinutes - kDayStartMinutes)
                                         / kSlotMinutes;     // 36
inline constexpr int kMaxSlotsPerEvent = 4;                  // up to 2 h
} // namespace plan

struct Event
{
    QString id;
    QDate   date;                    // which day this plan belongs to
    int     plannedStartMinutes = 0; // minutes after midnight, slot-aligned
    int     plannedEndMinutes   = 0; // exclusive; 1440 == midnight

    // WHAT the block is (block-labels addendum). An Event has exactly one
    // JOB — "a plan occupying a time range" — but three possible IDENTITIES:
    //
    //     activityId set  -> an occurrence of a reusable Activity ("Gym")
    //     taskId set      -> a work block on a one-off Task ("Lab 4")
    //     neither         -> a spontaneous, ad-hoc block; `title` names it
    //
    // All three are optional REFERENCES/TEXT here; the invariant "an Event
    // must have at least one identity" is NOT encoded in this struct — it is
    // enforced by AppData's creation/mutation doors, like every other rule.
    // (A struct can't defend an invariant that spans three fields without
    // becoming a class with accessors; the aggregate root already owns that
    // job, so we don't duplicate the guard.)
    QString activityId;              // reference, don't copy (§3.4)
    QString taskId;                  // reference to a Task, or empty
    QString title;                   // short label painted ON the block;
                                     // the ONLY identity an ad-hoc block has
    QString note;                    // "did anxiety creep in?"

    // COMPOSITION (the filled diamond in the domain model): an Event OWNS its
    // Segments — they live inside it and die with it. In C++, composition is
    // spelled "member by value": a QVector<Segment> right here. Delete the
    // Event and its Segments are gone; no shared ownership, no leaks, no
    // `new`/`delete`. This is RAII doing the memory management for us.
    QVector<Segment> segments;

    qint64 plannedSeconds() const
    {
        return qint64(plannedEndMinutes - plannedStartMinutes) * 60;
    }

    bool overlaps(int startMin, int endMin) const
    {
        // Two ranges overlap unless one ends before the other begins.
        // Learning to write overlap as "NOT disjoint" avoids a classic
        // off-by-one swamp of >=/<= cases.
        return !(endMin <= plannedStartMinutes || startMin >= plannedEndMinutes);
    }

    // Is this block LIVE at `t` — is `t` inside the planned window?
    // Half-open [start, end): at 11:00 a 9–11 block is over, an 11–12 block
    // is live — the same boundary convention as every other slot edge in
    // the app. A PURE function of (event, t): the caller supplies "now",
    // so tests can probe every boundary without touching the wall clock.
    bool isLiveAt(const QDateTime& t) const
    {
        if (t.date() != date)
            return false;
        const int m = t.time().hour() * 60 + t.time().minute();
        return m >= plannedStartMinutes && m < plannedEndMinutes;
    }
};
