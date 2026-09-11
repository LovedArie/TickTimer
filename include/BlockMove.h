#pragma once
// ---------------------------------------------------------------------------
// BlockMove — "can this planned block go THERE?" (v31.2, move-and-swap
// addendum)
//
// WHY THIS EXISTS. Until v31.2 a block could only move within its own day, by
// the dialog's nudge buttons. Moving it to another day, and swapping two
// blocks, raise questions a same-day nudge never had to answer: has it already
// happened, is it being timed, does its tracked time belong to the day it is
// leaving, does its rule already have a block on the new day. The answers live
// here, once, as pure functions - the same shape as DayLayout.h and
// Recurrence.h, so a table of microsecond tests can pin them without building
// a window.
//
// WHAT IS NOT HERE. The data. AppData gathers the day and the running state
// and hands them over as values; nothing in this header can mutate a
// container.
// ---------------------------------------------------------------------------

#include "DayLayout.h"    // daylay::problemWith is what dayWith() feeds
#include "Event.h"
#include "MissedBlocks.h" // missed::hasEnded - one definition of "over"
#include "Recurrence.h"   // recur::occurrences
#include "Schedule.h"

#include <QDateTime>
#include <QObject> // QObject::tr - the refusal sentences
#include <QString>
#include <QVector>

namespace blockmove
{

// Does `rule` put an occurrence on `date` at all, before the user's own
// deletions? A deleted date is still a date the rule PRODUCES - which is
// exactly why it is worth remembering in skipDates.
inline bool ruleProduces(const Schedule& rule, QDate date)
{
    if (!date.isValid())
        return false;
    Schedule unskipped = rule; // a copy: the caller's rule is not touched
    unskipped.skipDates.clear();
    return !recur::occurrences(unskipped, date, date).isEmpty();
}

// Has this occurrence been moved away from where its rule puts it - another
// date, or other times? Derived every time, never stored (addendum §M.5).
//
// A date in skipDates counts as displaced. Once skips are reconciled, the
// only occurrence that can sit on a skipped date is odd data, and "touched"
// is the answer that keeps it rather than withdraws it.
inline bool isDisplaced(const Event& e, const Schedule& ruleAsItWas)
{
    if (e.plannedStartMinutes != ruleAsItWas.startMinutes
        || e.plannedEndMinutes != ruleAsItWas.endMinutes)
        return true;
    return recur::occurrences(ruleAsItWas, e.date, e.date).isEmpty();
}

// `a` and `b` as they would be after a swap: each takes the other's date and
// START time and keeps its own length (owner decision, §M.3). Copies -
// nothing passed in is changed.
inline QVector<Event> swapped(const Event& a, const Event& b)
{
    Event a2 = a;
    a2.date                = b.date;
    a2.plannedStartMinutes = b.plannedStartMinutes;
    a2.plannedEndMinutes   = b.plannedStartMinutes
                           + (a.plannedEndMinutes - a.plannedStartMinutes);

    Event b2 = b;
    b2.date                = a.date;
    b2.plannedStartMinutes = a.plannedStartMinutes;
    b2.plannedEndMinutes   = a.plannedStartMinutes
                           + (b.plannedEndMinutes - b.plannedStartMinutes);
    return {a2, b2};
}

// One day's blocks as they WOULD be once every block in `moved` is applied:
// the originals of the moved blocks are left out, and the moved copies that
// land on `date` are put in. This is what makes a swap of two ADJACENT blocks
// come out right - each side is checked against the other's new position,
// not its old one (§M.3).
//
// LIFETIME. The result holds pointers into whatever `dayEvents` points at,
// and into `moved` itself. Both must outlive it, so hand it straight to
// daylay::problemWith in the same expression and never keep it.
inline QVector<const Event*> dayWith(const QVector<const Event*>& dayEvents,
                                     const QVector<Event>& moved, QDate date)
{
    const auto isMoved = [&moved](const QString& id) {
        for (const Event& m : moved)
            if (m.id == id)
                return true;
        return false;
    };

    QVector<const Event*> out;
    for (const Event* e : dayEvents)
        if (e && !isMoved(e->id))
            out.append(e);
    for (const Event& m : moved)
        if (m.date == date)
            out.append(&m);
    return out;
}

// "What, if anything, stops `e` going to (date, startMin)?" Empty when
// nothing does. Room on the target day is NOT asked here - that is
// daylay::problemWith over dayWith()'s day. The checks run in the order of
// §M.2, so the sentence names the most fundamental reason first.
//
// `beingTimed` and `ruleAlreadyThere` are facts only the aggregate root can
// see (the running state, the other occurrences), handed in as values.
inline QString problemWithMove(const Event& e, QDate date, int startMin,
                               const QDateTime& now, bool beingTimed,
                               bool ruleAlreadyThere)
{
    if (!date.isValid())
        return QObject::tr("That is not a day on the calendar.");
    if (e.outcome != BlockOutcome::Unset)
        return QObject::tr("This block was already settled in catch-up, so "
                           "it stays where it is.");
    if (missed::hasEnded(e, now))
        return QObject::tr("This block has already happened. A missed block "
                           "is rescheduled from the catch-up card.");
    if (beingTimed)
        return QObject::tr("This block is being timed right now. Stop the "
                           "timer before moving it.");
    const int nowMin = now.time().hour() * 60 + now.time().minute();
    if (date < now.date() || (date == now.date() && startMin < nowMin))
        return QObject::tr("That time has already passed.");
    if (!e.segments.isEmpty() && date != e.date)
        return QObject::tr("This block already has tracked time, and that "
                           "time belongs to the day it happened. It can only "
                           "move within that day.");
    if (ruleAlreadyThere)
        return QObject::tr("Its repeating rule already has a block on that "
                           "day, and a rule has one per day.");
    if (date == e.date && startMin == e.plannedStartMinutes)
        return QObject::tr("It is already there.");
    return {};
}

} // namespace blockmove
