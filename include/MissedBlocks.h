#pragma once
// ---------------------------------------------------------------------------
// missed — the pure brain of "catch-up" (design-addendum-catch-up).
//
// THE PROBLEM (stated once, here, because every surface will paraphrase it):
// an Event is an INTENTION and its Segments are REALITY, and until v26.2
// nothing in the app ever asked whether the two matched. A block whose
// window passed with nothing tracked simply sank into history. Overslept,
// priority shifted, forgot — the plan quietly became fiction and the app
// kept a straight face.
//
// THE RULE THIS FILE ENCODES: a block is *unresolved* when its planned
// window has closed, the focus time inside it falls short of a threshold,
// and the user has not yet said what to do about it.
//
// DERIVED, NEVER STORED. Nothing here writes anything. Move the threshold
// from 50% to 70% in Settings and every block in history re-judges on the
// next read, with no migration, no stale "missed" flag to repair, and no
// possibility of a stored judgement disagreeing with the segments it was
// computed from. The one thing that IS stored is the user's decision
// (Event::outcome), because no function of the segments can tell you that
// somebody deliberately skipped the gym. Same split as coverage::rung
// (derived) against Task::dismissCount (stored).
//
// PURITY, same contract as coverage::: every function here is a function of
// its arguments. No AppData, no QSettings, no clock — the caller supplies
// `now` and the Rule. That is what makes the whole feature testable in
// milliseconds, headless.
// ---------------------------------------------------------------------------

#include "Event.h"

#include <QDateTime>
#include <QVector>

#include <algorithm> // std::sort, used by unresolvedIn

namespace missed
{

// ---- the knobs (addendum §B) ----------------------------------------------
// TASTE, so it lives in QSettings via prefs:: — what counts as "enough of
// the block happened" is a personal call, and two devices may legitimately
// disagree.
struct Rule
{
    // Focus time below this share of the planned window means the block
    // didn't really happen. 50% is the shipped default: it forgives the
    // 90-minute block that ran 50 minutes, and catches the one that ran 15.
    //
    // 0 would mean "only a completely untouched block counts as missed",
    // which is a legitimate taste; 100 would mean "anything short of the
    // full window is a failure", which is a good way to feel bad every day.
    // Both are reachable, neither is the default.
    int minPercent = 50;

    // How far back to look. Without a horizon, reinstalling the app or
    // returning from a two-week holiday greets you with four hundred
    // unresolved blocks — a wall of guilt nobody triages, which means the
    // feature gets ignored, which means it may as well not exist. Old
    // failures are history, not a to-do list.
    //
    // 3, down from 7 (v26.8, owner call after meeting a 46-block wall):
    // the card's job is recovering the RECENT past — yesterday, the
    // weekend. A week-old missed block is almost never honestly
    // rescheduled; it gets re-planned from scratch, and listing it just
    // pads the pile. Nothing is lost at 3: past-horizon blocks stay Unset,
    // and widening the setting temporarily still recovers them (§K.3).
    // Note: users who ever pressed OK in Settings have 7 stored and keep
    // it — a changed default only reaches those who never chose.
    int lookBackDays = 3;
};

// ---- the verdict ----------------------------------------------------------
// WHY two failure reasons and not one bool: they deserve different
// proposals. "You never started this" wants the whole block moved. "You got
// 20 of 90 minutes" wants the REMAINING 70 moved — proposing the full 90
// would double-book time you already spent. The domain states the fact; the
// UI chooses the words and reschedule:: chooses the offer.
enum class Reason
{
    None,         // the block is fine — enough happened, or it hasn't ended
    NeverStarted, // zero focus seconds: nothing at all
    Partial,      // some focus time, but under the bar
};

struct Verdict
{
    Reason reason         = Reason::None;
    qint64 focusSeconds   = 0;
    qint64 plannedSeconds = 0;

    // How much of the block still owes you time. Zero unless the verdict is
    // a failure. This is the number reschedule:: actually plans against —
    // see the Partial case above.
    qint64 shortfallSeconds() const
    {
        return reason == Reason::None ? 0
                                      : qMax(qint64(0),
                                             plannedSeconds - focusSeconds);
    }

    int percent() const
    {
        return plannedSeconds > 0
                   ? int((focusSeconds * 100) / plannedSeconds)
                   : 0;
    }
};

// ---- has the window closed? ------------------------------------------------
// The mirror of Event::isLiveAt, and it uses the same half-open convention:
// at 11:00 a 9–11 block is over. A pure function of (event, t), so tests can
// probe every boundary without touching the wall clock.
inline bool hasEnded(const Event& e, const QDateTime& now)
{
    if (!e.date.isValid())
        return false; // a block with no date can't have a window to close
    if (e.date < now.date())
        return true;
    if (e.date > now.date())
        return false;
    const int m = now.time().hour() * 60 + now.time().minute();
    return m >= e.plannedEndMinutes;
}

// Is this block old enough to be someone else's problem? (Rule::lookBackDays)
inline bool beyondHorizon(const Event& e, const Rule& rule, const QDateTime& now)
{
    if (!e.date.isValid())
        return true;
    return e.date.daysTo(now.date()) > rule.lookBackDays;
}

// ---- the judgement (addendum §C) -------------------------------------------
// Note what is NOT consulted here: Event::outcome. This function answers
// "did enough of this block happen?", which is a question about time, and
// the answer must not change because somebody clicked a button. Resolution
// is a separate filter (isUnresolved below) precisely so the two stay
// separable — a resolved block can still be asked what its verdict was, and
// the evening review wants exactly that.
inline Verdict judge(const Event& e, const Rule& rule, const QDateTime& now)
{
    Verdict v;
    v.plannedSeconds = e.plannedSeconds();
    v.focusSeconds   = e.focusSeconds();

    if (!hasEnded(e, now))
        return v;               // still in the future, or running right now
    if (v.plannedSeconds <= 0)
        return v;               // degenerate block; nothing to fall short of

    if (v.focusSeconds <= 0) {
        v.reason = Reason::NeverStarted;
        return v;
    }
    // Integer maths on seconds, not floating point on percentages: the
    // comparison is exact and can't wobble on a value like 0.4999999.
    if (v.focusSeconds * 100 < qint64(rule.minPercent) * v.plannedSeconds)
        v.reason = Reason::Partial;
    return v;
}

// ---- the surfaced set (addendum §D) ----------------------------------------
// "Unresolved" is the state the catch-up card shows: it failed, it's recent
// enough to still matter, and nobody has decided anything about it yet.
inline bool isUnresolved(const Event& e, const Rule& rule, const QDateTime& now)
{
    if (e.outcome != BlockOutcome::Unset)
        return false;           // already decided; the decision is the answer
    if (beyondHorizon(e, rule, now))
        return false;
    return judge(e, rule, now).reason != Reason::None;
}

// ---- the recoverable set (v26.5) -------------------------------------------
// Blocks the user DECIDED about, still within the horizon: the review chip's
// list, and the retroactive answer to "I resolved something by mistake".
//
// Fully DERIVED — which is what makes recovery work for accidents that
// happened before the recovery feature existed: no resolvedAt timestamp is
// stored (deliberately: the horizon bounds the set by the block's own date,
// so the chip ages out on its own and the schema stays at v11).
//
// Moved is EXCLUDED on purpose: a moved block has a live replacement on the
// calendar, and "bringing it back" would put the same obligation on the
// board twice. The chip covers the two SILENT verdicts — Done and Dropped —
// the ones no other surface shows.
inline bool isRecentlyResolved(const Event& e, const Rule& rule,
                               const QDateTime& now)
{
    if (e.outcome != BlockOutcome::Done && e.outcome != BlockOutcome::Dropped)
        return false;
    if (beyondHorizon(e, rule, now))
        return false;
    return hasEnded(e, now);
}

inline QVector<const Event*> recentlyResolvedIn(const QVector<Event>& events,
                                                const Rule& rule,
                                                const QDateTime& now)
{
    QVector<const Event*> out;
    for (const Event& e : events)
        if (isRecentlyResolved(e, rule, now))
            out.append(&e);
    std::sort(out.begin(), out.end(), [](const Event* a, const Event* b) {
        if (a->date != b->date)
            return a->date < b->date;
        if (a->plannedStartMinutes != b->plannedStartMinutes)
            return a->plannedStartMinutes < b->plannedStartMinutes;
        return a->id < b->id;
    });
    return out;
}

// One pass over the whole event list, oldest first.
//
// Returns pointers INTO the caller's vector — cheap, and honest about
// ownership: these are borrowed views, valid exactly as long as the caller's
// data is. The same convention AppData::eventsOn already uses, so the call
// sites read alike.
inline QVector<const Event*> unresolvedIn(const QVector<Event>& events,
                                          const Rule& rule,
                                          const QDateTime& now)
{
    QVector<const Event*> out;
    for (const Event& e : events)
        if (isUnresolved(e, rule, now))
            out.append(&e);

    // Oldest first: the block you missed on Monday is more likely to be
    // urgent than the one you missed an hour ago, and a stable order means
    // the card doesn't reshuffle under the user's finger between renders.
    std::sort(out.begin(), out.end(), [](const Event* a, const Event* b) {
        if (a->date != b->date)
            return a->date < b->date;
        if (a->plannedStartMinutes != b->plannedStartMinutes)
            return a->plannedStartMinutes < b->plannedStartMinutes;
        return a->id < b->id; // total order: ties can't flip between calls
    });
    return out;
}

} // namespace missed
