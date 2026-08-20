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

#include "Task.h" // Task::Repeat — the recurrence vocabulary, borrowed
                   // wholesale. Deliberately NOT moved to its own header:
                   // the enum + its to/from-string helpers were born in
                   // Task.h and every call site names them there; renaming
                   // for purity would churn a dozen files to change zero
                   // behaviour. Naming honesty is kept the cheap way — a
                   // comment at each borrow site (here, and the field).

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QStringList>
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

// v26.2 — the verdict on a block whose planned window has passed.
//
// THE DIVIDING LINE this enum defends: "this block was missed" is DERIVED
// (missed::judge — a pure function of the planned window, the tracked
// segments, and `now`; change the threshold in Settings and every block
// re-judges instantly with nothing to migrate). "What I decided to do about
// it" is NOT derivable from anything — no amount of staring at segments
// tells you the user deliberately skipped the gym rather than forgot. So
// the judgement stays computed and the DECISION gets stored, syncs, and
// lives here. Same split as coverage::rung (derived) vs Task::dismissCount
// (stored).
//
//   Unset   — no verdict yet. The only state missed:: will surface.
//   Done    — "it happened; stop asking." May or may not have segments:
//             the user can log the real time (a separate fact, appended as
//             a Segment) or simply refuse to invent timestamps.
//   Moved   — replaced by another block; `movedToId` names it.
//   Dropped — deliberately skipped. Not a failure, a decision.
enum class BlockOutcome
{
    Unset,
    Done,
    Moved,
    Dropped,
};

// Unset serialises to the EMPTY string on purpose: a pre-v11 event has no
// "outcome" key at all, o["outcome"].toString() gives "", and that reads
// back as Unset. Tolerant read, additive growth, no migration branch — the
// fourth time this file uses the trick (taskId at v6, repeat at v9).
inline QString blockOutcomeToString(BlockOutcome o)
{
    switch (o) {
    case BlockOutcome::Unset:   return QStringLiteral("");
    case BlockOutcome::Done:    return QStringLiteral("done");
    case BlockOutcome::Moved:   return QStringLiteral("moved");
    case BlockOutcome::Dropped: return QStringLiteral("dropped");
    }
    return {}; // unreachable — no default label, so the compiler warns if a
               // new enumerator is added and this switch isn't updated
}

inline BlockOutcome blockOutcomeFromString(const QString& s)
{
    if (s == QLatin1String("done"))    return BlockOutcome::Done;
    if (s == QLatin1String("moved"))   return BlockOutcome::Moved;
    if (s == QLatin1String("dropped")) return BlockOutcome::Dropped;
    return BlockOutcome::Unset; // "" and any unknown value — garbage on disk
                                // degrades to "undecided", never to a
                                // decision the user didn't make
}

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

    // v9: recurrence for planned blocks. The rule always lives on the
    // NEWEST link of a chain — when an occurrence rolls forward, the old
    // block's repeat is cleared and the new block carries it. That single
    // invariant is also the duplicate-spawn guard: a link can only ever
    // spawn once, because spawning strips it of the rule.
    Task::Repeat repeat = Task::Repeat::None;

    // v26.2 — the catch-up verdict. See BlockOutcome above for why the
    // judgement is derived and the decision is stored.
    BlockOutcome outcome = BlockOutcome::Unset;

    // Set only when outcome == Moved: the blocks this one turned into, in
    // the order they were created. A plain move has one; a SPLIT has several.
    //
    // ONE DIRECTION, still. The obvious symmetric design stores a movedFromId
    // on each new block, and the obvious problem with it is that two pointers
    // can disagree — a half-applied edit leaves a chain that says different
    // things depending on which end you read. The reverse question ("was this
    // block rescheduled from somewhere?") is a linear scan over events, which
    // is trivially cheap and cannot drift. Derive the reverse, store the
    // forward. (The repeat chain makes the same call: the rule lives on
    // exactly one link.)
    //
    // v29.3 corrected the CARDINALITY, not the direction — and the difference
    // is the whole lesson. This was a lone QString, so a split could only
    // record its FIRST piece and the siblings were linked at neither end;
    // nothing could answer "which pieces belong to this move?", which is why
    // a split had no inverse. That reads like an argument for the back-link,
    // and it isn't: a list keeps exactly one record owning the move, so there
    // is still nothing to disagree with. The back-link was rejected twice for
    // the same reason, and the second time it was rejected on stronger
    // evidence.
    QStringList movedToIds;

    qint64 plannedSeconds() const
    {
        return qint64(plannedEndMinutes - plannedStartMinutes) * 60;
    }

    // REAL focus time committed to this block. Derived from the segments it
    // owns, so it can never disagree with them — the same reasoning as
    // Segment::seconds() and plannedSeconds() above.
    //
    // Focus ONLY, and that is a domain judgement worth stating: the question
    // "did this block happen?" is a question about work. Break time inside a
    // block is legitimate but isn't the work, and Distracted time is
    // explicitly lost time. Counting either would let a block full of
    // procrastination pass as done. (stats::eventTotals keeps the full
    // three-way split for the reporting screens, which ask a different
    // question.)
    qint64 focusSeconds() const
    {
        qint64 total = 0;
        for (const Segment& s : segments)
            if (s.kind == SegmentKind::Focus)
                total += s.seconds();
        return total;
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
