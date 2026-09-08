#pragma once
// ---------------------------------------------------------------------------
// Schedule — a recurring PLAN for a block (v31). "GTI350 Laboratoire, every
// Tuesday 13:30–17:00, from Aug 25 to Dec 15."
//
// WHY A NEW CONCEPT AT ALL. Recurrence already existed twice and worked
// neither time in the way that mattered:
//
//   Task::repeat   spawns the next occurrence when you TICK the current one
//                  (AppData::setTaskDone). Right for a chore, useless for a
//                  timetable — nothing exists until you finish something.
//   Event::repeat  rolled ONE occurrence forward once its date had passed
//                  (the old AppData::rollRepeats). The rule lived on the
//                  newest link of a chain, which made duplicate spawns
//                  impossible and made looking ahead impossible too: next
//                  week's lecture did not exist until this week's was over.
//
// Both are "one at a time, after the fact". The complaint they produced —
// "the repeat does not work, I want to see it each week" — is not a bug in
// either; it is the shape of both. A rule you can look FORWARD along has to
// be a thing in its own right, separate from any one occurrence of it, with
// its own start and end. That thing is this.
//
// Event::repeat and rollRepeats are gone. Two mechanisms for one idea is
// what CLAUDE.md warns against, and keeping the weaker one "for ad-hoc
// blocks" would have been exactly that — which is why this struct carries a
// `title` as well as an `activityId`: an ad-hoc repeating block is a
// Schedule with no activity, not a second system.
//
// WHY OCCURRENCES ARE MATERIALISED AS REAL EVENTS rather than expanded when
// the agenda paints. The alternative — ghost blocks drawn from the rule —
// looks cheaper and is not. Every reader of the plan would need a second
// code path: the tracker, Stats, DayBriefing, Alarms, MissedBlocks, the
// catch-up card. Worse, a ghost cannot be TRACKED, and this is a
// plan-vs-actual app: a lecture you cannot start a timer on is not a plan,
// it is a picture of one. Real events cost about sixteen rows a semester and
// every existing consumer already knows what to do with them.
//
// The price of that choice is honest and paid here: an occurrence you delete
// must not come back, so the rule remembers what you removed (`skipDates`).
// That is the one piece of state a purely-derived design would not need, and
// it is a fair trade for not forking every reader in the app.
// ---------------------------------------------------------------------------

#include "Task.h" // Task::Repeat — the recurrence vocabulary, borrowed once
                  // more (Event.h borrowed it first). One enum for one idea.

#include <QDate>
#include <QList>
#include <QString>
#include <QStringList>

struct Schedule
{
    QString id;

    // WHAT recurs. Exactly one of these carries the identity, mirroring the
    // way an Event is either activity-backed or title-only — because a
    // Schedule's occurrences ARE Events and must be able to say the same
    // things. Reference-by-id for the activity, per Activity.h's rule: a
    // renamed activity renames every occurrence, past and future, at once.
    QString activityId;
    QString title; // used only when activityId is empty

    // WHEN the rule's window OPENS. Nothing more than that, since v31.1 —
    // and the correction is worth recording because the original design
    // read like a virtue and was a trap.
    //
    // startDate used to carry two facts: when the rule begins AND, for a
    // weekly rule, which weekday every occurrence lands on (the walk simply
    // stepped seven days at a time from here). That is wrong for the case
    // this feature exists to serve. A term and a timetable are independent:
    // "the semester runs from Aug 31" and "the class is on Wednesdays" are
    // two facts, and a term that happens to begin on a Monday must not turn
    // a Wednesday class into a Monday one. Reported exactly that way.
    //
    // So the weekday moved out, into `weekdays` below, and this field went
    // back to meaning one thing.
    QDate startDate;
    // Inclusive. An invalid QDate is "open-ended", the same absence idiom
    // Task::dueDate has used for "TBD" since v3 — one way of being absent in
    // this codebase, not two.
    QDate endDate;

    // Minutes after midnight, exactly like Event::plannedStartMinutes, so a
    // materialised occurrence is a straight copy with no unit conversion in
    // between (conversions are where off-by-an-hour bugs live).
    int startMinutes = 0;
    int endMinutes   = 0;

    Task::Repeat repeat = Task::Repeat::None; // None = one dated block

    // WHICH DAYS a weekly rule lands on: Qt's numbering, 1 = Monday through
    // 7 = Sunday, so `QDate::dayOfWeek()` can be tested against it directly
    // with no conversion table to get backwards.
    //
    // EMPTY MEANS "the weekday of startDate", which is exactly what every
    // rule written before v31.1 meant. That is what keeps existing
    // schedules working untouched through the upgrade — a v15 file has no
    // `weekdays` key, reads as empty, and behaves as it always did. The
    // editor pre-ticks that day so the old rule's behaviour is visible
    // rather than implied.
    //
    // Only WEEKLY consults it. Daily needs no day list, and monthly and
    // yearly are anchored to a date rather than a weekday; a set on those
    // would be a field that silently does nothing.
    QList<int> weekdays;

    // How many minutes BEFORE the block to chime. 0 means "at the start",
    // which is what every planned block has always done. Kept on the rule
    // rather than copied onto each occurrence so that changing it changes
    // every future chime at once — and alarms::upcoming resolves it through
    // Event::scheduleId, one lookup, no second copy to fall out of step.
    int reminderMinutes = 0;

    // ISO dates whose occurrence the user deleted. See the header: this is
    // the cost of materialising, and it is deliberately a list of DATES
    // rather than of event ids — the event is gone, and a date is what the
    // generator needs to not make it again.
    QStringList skipDates;
};
