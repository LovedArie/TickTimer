#pragma once
// ---------------------------------------------------------------------------
// reschedule — where could this missed block go? (design-addendum-catch-up)
//
// THE THING THIS MODULE REFUSES TO DO: return a single answer. The naive
// shape is `QDateTime findSlot(...)` and it collapses the moment the week is
// already full, which for anyone who actually plans is the NORMAL state, not
// the exception. A missed block with a deadline two days out and no free
// time between here and there is not a scheduling puzzle with a hidden
// solution — it is a CONFLICT between three things the user wants:
//
//     the deadline  ·  the full duration  ·  the other blocks
//
// Something has to give, and only the human can say which. So this module
// returns a RANKED LIST of options, cheapest first, each naming what it
// costs — and it is allowed to return an empty list, which is an honest
// answer that most task apps refuse to give. An app that silently crams the
// block somewhere produces a calendar that lies; an app that says nothing
// lets you discover the problem on deadline day.
//
// PURITY, same contract as coverage:: and missed::: functions of arguments.
// No AppData, no QSettings, no clock. Every option is a PROPOSAL — nothing
// here mutates anything, which is what lets the UI show a placement before
// the user commits to it (see §D of the addendum: propose, don't move).
//
// NON-GOAL, stated so it reads as a decision and not an oversight: this does
// not search for the cheapest global REARRANGEMENT. If the right answer is
// "move today's 14:00 block to Thursday and put the missed study block in
// its place", that is a constraint-solving problem and it is out of scope.
// v1 offers single swaps and says so.
// ---------------------------------------------------------------------------

#include "Event.h"
#include "MissedBlocks.h"

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QVector>

#include <algorithm>

namespace reschedule
{

// ---- the ladder (addendum §E) ---------------------------------------------
// Ordered by what they cost the user, cheapest first. The UI renders them in
// this order and the enum's ordering IS that ranking — one source of truth
// for "which offer is better", rather than a sort key repeated per surface.
enum class Kind
{
    FreeSlot,       // it fits, whole, before the deadline. The happy path.
    Split,          // doesn't fit contiguously, but the fragments add up.
    Shorten,        // less time than you owed. You decide if it's enough.
    Bump,           // something else gives up its slot. You pick what.
    BeyondDeadline, // there IS room — after the deadline. Move the deadline?
};

// One placement. A Split option has several; everything else has one.
struct Piece
{
    QDate date;
    int   startMinutes = 0;
    int   endMinutes   = 0; // exclusive, same convention as Event

    qint64 seconds() const
    {
        return qint64(endMinutes - startMinutes) * 60;
    }
};

struct Option
{
    Kind           kind = Kind::FreeSlot;
    QVector<Piece> pieces;
    QString        bumpEventId;      // Kind::Bump only: whose slot we'd take
    qint64         recoveredSeconds = 0; // how much of the shortfall this buys

    // Does this offer make the user whole, or is it a partial recovery? The
    // UI needs the distinction to phrase the button honestly ("Move it" vs
    // "Recover 50 of 90 minutes").
    bool isComplete(qint64 shortfallSeconds) const
    {
        return recoveredSeconds >= shortfallSeconds;
    }
};

// ---- what the search is allowed to touch ----------------------------------
struct Context
{
    QDateTime now;

    // The task's deadline, if the block serves one. Invalid = unbounded, the
    // same "absence is a value" convention as Task::dueDate and
    // coverage::deadlineOf.
    QDate deadline;

    // The hours the user is willing to work — prefs::agendaWindow(), which
    // is display taste doing double duty as a planning constraint. Defaulted
    // to the full domain grid so a caller that doesn't care gets sane
    // behaviour rather than an empty day.
    int dayStartMinutes = plan::kDayStartMinutes;
    int dayEndMinutes   = plan::kDayEndMinutes;

    // How far to look when there is no deadline, and how far PAST a deadline
    // to look for the BeyondDeadline offer.
    int horizonDays = 14;

    // Bump candidates are listed for the user to choose from, not ranked by
    // the app (addendum §F: the app finds the candidates, you pick the
    // victim). A cap keeps the card from becoming a wall.
    int maxBumpCandidates = 4;

    // Free-slot offers per search. Three is enough to feel like a choice and
    // few enough to read at a glance.
    int maxFreeSlotOptions = 3;
};

// ===========================================================================
// Grid helpers
// ===========================================================================

// Snap to the 30-minute planning grid. Rounding DIRECTION matters and is why
// these are two functions rather than one: a free interval must shrink to
// the grid (round its start up, its end down), never grow past its real
// bounds and offer time that is actually occupied.
inline int snapUp(int minutes)
{
    const int r = minutes % plan::kSlotMinutes;
    return r == 0 ? minutes : minutes + (plan::kSlotMinutes - r);
}

inline int snapDown(int minutes)
{
    return (minutes / plan::kSlotMinutes) * plan::kSlotMinutes;
}

// The longest single block AppData will accept (kMaxSlotsPerEvent = 4 slots
// = 2 hours). Discovered by reading the invariant rather than by having a
// proposal rejected at commit time: an offer the aggregate root would refuse
// is worse than no offer, because the user has already decided to accept it.
inline int maxPieceMinutes()
{
    return plan::kMaxSlotsPerEvent * plan::kSlotMinutes;
}

// The last day the search may consider.
inline QDate lastSearchDay(const Context& ctx)
{
    const QDate today = ctx.now.date();
    if (ctx.deadline.isValid())
        return qMax(ctx.deadline, today);
    return today.addDays(ctx.horizonDays);
}

// ---- free time on one day --------------------------------------------------
// The primitive everything else is built from: the day's working window,
// minus what is already planned, snapped to the grid.
//
// `ignoreEventId` exists because the missed block itself is usually still
// sitting on the calendar in the past — and because a Bump proposal has to
// ask "what would be free if THIS block weren't here?".
inline QVector<Piece> freeOn(QDate day, const QVector<Event>& events,
                             const Context& ctx,
                             const QString& ignoreEventId = QString())
{
    QVector<Piece> out;

    int winStart = qMax(snapUp(ctx.dayStartMinutes), plan::kDayStartMinutes);
    const int winEnd = qMin(snapDown(ctx.dayEndMinutes), plan::kDayEndMinutes);

    // Today is special: the past is not free. Round UP to the next slot line
    // so a proposal never starts three minutes from now — a slot you can't
    // realistically walk into is not an offer, it's a taunt.
    if (day == ctx.now.date()) {
        const int nowMin = ctx.now.time().hour() * 60 + ctx.now.time().minute();
        winStart = qMax(winStart, snapUp(nowMin));
    } else if (day < ctx.now.date()) {
        return out; // the past has no free time
    }
    if (winStart >= winEnd)
        return out;

    // Collect and merge the busy ranges. Merging first means the gap walk
    // below is a simple sweep — overlapping or touching blocks would
    // otherwise produce phantom zero-length gaps between them.
    QVector<QPair<int, int>> busy;
    for (const Event& e : events) {
        if (e.date != day || e.id == ignoreEventId)
            continue;
        const int s = qMax(e.plannedStartMinutes, winStart);
        const int t = qMin(e.plannedEndMinutes, winEnd);
        if (s < t)
            busy.append({s, t});
    }
    std::sort(busy.begin(), busy.end());

    int cursor = winStart;
    // NOT named `emit`: Qt #defines that to nothing, so `const auto emit =`
    // expands to `const auto = ` and the file stops compiling. The other
    // reserved-by-macro names to avoid in this codebase are `signals`,
    // `slots` and `foreach`.
    const auto emitGap = [&](int from, int to) {
        const int a = snapUp(from);
        const int b = snapDown(to);
        if (b - a >= plan::kSlotMinutes)
            out.append(Piece{day, a, b});
    };

    for (const auto& span : busy) {
        if (span.first > cursor)
            emitGap(cursor, span.first);
        cursor = qMax(cursor, span.second);
    }
    if (cursor < winEnd)
        emitGap(cursor, winEnd);

    return out;
}

// ===========================================================================
// The proposal
// ===========================================================================

namespace detail
{

// How much time we are actually trying to place, on the grid.
//
// The SHORTFALL, not the original duration — this is the whole reason
// missed::Verdict distinguishes NeverStarted from Partial. Re-offering the
// full 90 minutes to someone who already put in 40 double-books time they
// have spent, and it is the kind of small dishonesty that makes a user stop
// trusting the numbers.
inline int needMinutes(const missed::Verdict& v)
{
    const int raw = int((v.shortfallSeconds() + 59) / 60); // ceil to minutes
    return qMax(plan::kSlotMinutes, snapUp(raw));
}

// Place `need` minutes inside `gap`, preferring the block's original
// time-of-day.
//
// WHY the preference: a gym block that was 07:00 should not be proposed for
// 22:00 just because that gap happened to be scanned first. Time-of-day is
// part of what made the plan plausible, and a proposal that ignores it gets
// rejected and teaches the user that the button is not worth pressing.
inline Piece placeIn(const Piece& gap, int need, int preferredStart)
{
    const int latest = gap.endMinutes - need;
    const int start  = qBound(gap.startMinutes, snapDown(preferredStart), latest);
    return Piece{gap.date, start, start + need};
}

} // namespace detail

// The one entry point.
//
// `events` is the full list; this function does its own date filtering
// because the useful bounds differ per option kind (free slots look forward,
// bump candidates look forward, the missed block itself is behind us).
inline QVector<Option> propose(const Event& block,
                               const missed::Verdict& verdict,
                               const QVector<Event>& events,
                               const Context& ctx)
{
    QVector<Option> out;
    if (verdict.reason == missed::Reason::None)
        return out; // nothing to place

    const QDate today   = ctx.now.date();
    const QDate lastDay = lastSearchDay(ctx);
    const int   need    = detail::needMinutes(verdict);

    // ---- 1. free slots ----------------------------------------------------
    // Only reachable when the shortfall fits inside one legal block. A
    // 3-hour debt cannot be a single Event no matter how empty the week is,
    // so it falls through to Split — which is the correct offer anyway.
    QVector<Piece> freeSlotHits;
    if (need <= maxPieceMinutes()) {
        for (QDate d = today; d <= lastDay; d = d.addDays(1)) {
            for (const Piece& gap : freeOn(d, events, ctx, block.id)) {
                if (gap.endMinutes - gap.startMinutes < need)
                    continue;
                freeSlotHits.append(
                    detail::placeIn(gap, need, block.plannedStartMinutes));
            }
        }
    }

    // Earliest day wins (deadline pressure is the point), then closest to
    // the block's original time of day.
    std::sort(freeSlotHits.begin(), freeSlotHits.end(),
              [&](const Piece& a, const Piece& b) {
                  if (a.date != b.date)
                      return a.date < b.date;
                  const int da = qAbs(a.startMinutes - block.plannedStartMinutes);
                  const int db = qAbs(b.startMinutes - block.plannedStartMinutes);
                  if (da != db)
                      return da < db;
                  return a.startMinutes < b.startMinutes; // total order
              });

    for (const Piece& p : freeSlotHits) {
        if (out.size() >= ctx.maxFreeSlotOptions)
            break;
        Option o;
        o.kind             = Kind::FreeSlot;
        o.pieces           = {p};
        o.recoveredSeconds = p.seconds();
        out.append(o);
    }

    const bool solved = !out.isEmpty();

    // ---- 2. split across fragments ----------------------------------------
    // The quietly most valuable offer: a full calendar is almost never
    // CONTIGUOUSLY full. Ninety minutes that fit nowhere often fit as
    // 45 + 30 + 15 across three days.
    if (!solved) {
        Option split;
        split.kind = Kind::Split;
        int remaining = need;
        for (QDate d = today; d <= lastDay && remaining > 0; d = d.addDays(1)) {
            for (const Piece& gap : freeOn(d, events, ctx, block.id)) {
                if (remaining <= 0)
                    break;
                // Three ceilings at once, so std::min's initializer-list form
                // rather than qMin (which is strictly two-argument): the gap
                // we found, what's still owed, and the longest block AppData
                // will accept.
                const int take = std::min({gap.endMinutes - gap.startMinutes,
                                           remaining, maxPieceMinutes()});
                if (take < plan::kSlotMinutes)
                    continue;
                Piece p{d, gap.startMinutes, gap.startMinutes + take};
                split.pieces.append(p);
                split.recoveredSeconds += p.seconds();
                remaining -= take;
            }
        }
        // A one-piece "split" is a lie — that is a Shorten, and it will be
        // offered as one below. Two or more pieces, or nothing.
        if (split.pieces.size() >= 2)
            out.append(split);
    }

    // ---- 3. shorten --------------------------------------------------------
    // The largest single fragment that exists, when nothing bigger does.
    if (!solved) {
        Piece best;
        int bestLen = 0;
        for (QDate d = today; d <= lastDay; d = d.addDays(1)) {
            for (const Piece& gap : freeOn(d, events, ctx, block.id)) {
                const int len = qMin(gap.endMinutes - gap.startMinutes,
                                     maxPieceMinutes());
                if (len > bestLen) {
                    bestLen = len;
                    best    = Piece{d, gap.startMinutes,
                                    gap.startMinutes + len};
                }
            }
        }
        if (bestLen >= plan::kSlotMinutes && bestLen < need) {
            Option o;
            o.kind             = Kind::Shorten;
            o.pieces           = {best};
            o.recoveredSeconds = best.seconds();
            out.append(o);
        }
    }

    // ---- 4. bump something -------------------------------------------------
    // Candidates only. The app names what COULD give up its slot; the user
    // says which one does. Ranking these would mean the app deciding that a
    // gym session matters less than a lab report, and it has no basis for
    // that: a block on an Activity carries no priority at all.
    if (!solved) {
        int found = 0;
        for (QDate d = today; d <= lastDay && found < ctx.maxBumpCandidates;
             d = d.addDays(1)) {
            for (const Event& e : events) {
                if (found >= ctx.maxBumpCandidates)
                    break;
                if (e.date != d || e.id == block.id)
                    continue;
                if (e.plannedEndMinutes - e.plannedStartMinutes < need)
                    continue;
                // Don't offer to bump a block that has already been worked:
                // taking a slot someone is halfway through is not a swap,
                // it's a loss.
                if (e.focusSeconds() > 0)
                    continue;
                Option o;
                o.kind             = Kind::Bump;
                o.bumpEventId      = e.id;
                o.pieces           = {Piece{d, e.plannedStartMinutes,
                                            e.plannedStartMinutes + need}};
                o.recoveredSeconds = qint64(need) * 60;
                out.append(o);
                ++found;
            }
        }
    }

    // ---- 5. beyond the deadline --------------------------------------------
    // Offered ONLY when there is genuinely room on the other side of the
    // deadline. "Move your deadline" is useless advice if the following week
    // is also full, and offering it blindly would make the app sound like it
    // is blaming the user for having a deadline at all.
    if (!solved && ctx.deadline.isValid() && need <= maxPieceMinutes()) {
        const QDate beyondEnd = today.addDays(ctx.horizonDays);
        for (QDate d = lastDay.addDays(1); d <= beyondEnd; d = d.addDays(1)) {
            bool placed = false;
            for (const Piece& gap : freeOn(d, events, ctx, block.id)) {
                if (gap.endMinutes - gap.startMinutes < need)
                    continue;
                Option o;
                o.kind   = Kind::BeyondDeadline;
                o.pieces = {detail::placeIn(gap, need,
                                            block.plannedStartMinutes)};
                o.recoveredSeconds = qint64(need) * 60;
                out.append(o);
                placed = true;
                break;
            }
            if (placed)
                break; // the FIRST day past the deadline that works — the
                       // offer is "how far would you have to slip?", and the
                       // honest answer is the smallest slip
        }
    }

    return out;
}

} // namespace reschedule
