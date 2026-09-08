#pragma once
// ---------------------------------------------------------------------------
// Recurrence.h — the one real judgement of the schedules feature, extracted
// as a PURE function so a table of microsecond tests can pin it.
//
// This is the same move SyncPlan::decide, MissedBlocks, Affordability and
// version::decideBanner all made, and it is worth naming the property that
// makes it worth doing: "which dates does this rule produce between here and
// there?" needs no AppData, no clock, no I/O and no Qt Widgets. A function
// that needs none of those can be tested exhaustively — every month
// boundary, every end-date edge, every skip — in a loop that runs in
// microseconds. Everything AROUND it (creating the events, refusing an
// occupied slot, emitting changed()) is the impure part, and it stays in
// AppData where the state lives.
//
// NAMING NOTE, the sync/syncplan scar (CLAUDE.md): `sched` was the obvious
// short name and POSIX ships <sched.h>; `schedule` risks the same class of
// collision. `recur` collides with nothing in POSIX or bionic.
//
// KNOWN LIMIT, stated rather than hidden. Monthly stepping is
// QDate::addMonths, which CLAMPS: Jan 31 + 1 month is Feb 28, and the next
// step from there is Mar 28 — the day-of-month drifts once a rule crosses a
// short month. That is the app-wide rule already (Task.h's nextOccurrence
// has behaved this way since v7 and Event borrowed it), and this file
// deliberately does NOT invent a second, better one: two recurrence
// arithmetics in one app is exactly the kind of quiet disagreement the
// whole feature exists to remove. If it is ever fixed, it gets fixed in
// nextOccurrence and everything follows.
// ---------------------------------------------------------------------------

#include "Event.h"   // plan::kDayStartMinutes — one planning day
#include "Schedule.h"

#include <QDate>
#include <QLocale>
#include <QObject> // tr() for the two words in summary() below
#include <QStringList>
#include <QVector>

namespace recur
{

// A hard ceiling on how many steps the walk below will take. It is not a
// business rule — it is a guard against a corrupt or hand-edited file
// (startDate in 1900, repeat daily, no end) turning a UI refresh into a
// hang. 20000 daily steps is over fifty years, so no honest schedule can
// reach it, and a dishonest one stops instead of spinning.
inline constexpr int kMaxSteps = 20000;

// Every date `s` produces within [from, to] inclusive, in ascending order,
// minus the ones the user has deleted.
//
// `from` and `to` are the CALLER'S window, not the rule's. The rule's own
// window (startDate..endDate) is applied on top, so the answer is the
// intersection of the two — which is what lets one function serve both "what
// should exist in the next 90 days?" (materialising) and "what would this
// rule do?" (a preview in the editor).
inline QVector<QDate> occurrences(const Schedule& s, QDate from, QDate to)
{
    QVector<QDate> out;
    if (!s.startDate.isValid() || !from.isValid() || !to.isValid())
        return out;

    // The rule's own end narrows the window; its start narrows nothing,
    // because the walk below begins there anyway.
    const QDate last = s.endDate.isValid() && s.endDate < to ? s.endDate : to;
    if (last < from)
        return out;

    // A non-repeating schedule is ONE dated block, and it is a real case
    // rather than a degenerate one: "add a date to this activity" with no
    // repeat is exactly how someone puts a single session on the calendar.
    // Handling it here, instead of demanding a separate code path, is what
    // lets the editor offer "Does not repeat" as just another choice.
    if (s.repeat == Task::Repeat::None) {
        if (s.startDate >= from && s.startDate <= last
            && !s.skipDates.contains(s.startDate.toString(Qt::ISODate)))
            out.append(s.startDate);
        return out;
    }

    // WEEKLY WITH A DAY SET walks day by day and keeps the days that match
    // (v31.1). The old code stepped seven days from startDate, which made
    // the START DATE decide the weekday — so a term beginning on a Monday
    // turned a Wednesday class into a Monday one. See Schedule.h.
    //
    // Day-by-day rather than clever arithmetic: the window is bounded (the
    // materialiser asks for ~120 days) and this cannot get the modular
    // arithmetic wrong, which the seven-day stride quietly did.
    if (s.repeat == Task::Repeat::Weekly && !s.weekdays.isEmpty()) {
        QDate d = s.startDate > from ? s.startDate : from;
        for (int step = 0; step < kMaxSteps && d.isValid() && d <= last;
             ++step, d = d.addDays(1)) {
            if (d < s.startDate)
                continue; // the rule has not opened yet
            if (!s.weekdays.contains(d.dayOfWeek()))
                continue;
            if (s.skipDates.contains(d.toString(Qt::ISODate)))
                continue;
            out.append(d);
        }
        return out;
    }

    // EVERY OTHER RULE strides: daily, monthly, yearly, and a weekly rule
    // with no day set — which is what every schedule written before v31.1
    // is, and which must keep meaning exactly what it meant (the weekday of
    // startDate, reached by stepping seven days at a time).
    QDate d = s.startDate;
    for (int step = 0; step < kMaxSteps && d.isValid() && d <= last; ++step) {
        if (d >= from && !s.skipDates.contains(d.toString(Qt::ISODate)))
            out.append(d);
        const QDate next = nextOccurrence(d, s.repeat);
        // nextOccurrence returns the input unchanged for Repeat::None, and a
        // rule that does not advance would loop until kMaxSteps. Guarded
        // rather than assumed: this function is public and its input is
        // data, which can be anything a file contains.
        if (!next.isValid() || next <= d)
            break;
        d = next;
    }
    return out;
}

// ---------------------------------------------------------------------------
// The days a rule ACTUALLY lands on, resolved once. An empty set means "the
// weekday startDate falls on" (Schedule.h), and every reader that wants to
// SHOW the days — the summary below, the editor's checkboxes — must resolve
// it the same way the walk above does, or the sentence and the calendar
// disagree.
// ---------------------------------------------------------------------------
inline QList<int> effectiveWeekdays(const Schedule& s)
{
    if (!s.weekdays.isEmpty())
        return s.weekdays;
    if (s.startDate.isValid())
        return {s.startDate.dayOfWeek()};
    return {};
}

// ---------------------------------------------------------------------------
// "What, if anything, is wrong with this rule?" — empty when it is fine, a
// sentence a person can act on when it is not.
//
// PURE, and shared, because the alternative bit: AppData refused a malformed
// rule by returning false, ScheduleEditDialog did no validation at all, and
// Save simply closed with the edit discarded and nothing said. Two places
// deciding what "legal" means, one of them silently. Now there is one
// definition and the dialog can quote it.
//
// What is NOT here: whether activityId resolves. That needs the whole data
// set, so it stays in AppData — this covers everything answerable from the
// rule alone.
// ---------------------------------------------------------------------------
inline QString problemWith(const Schedule& s)
{
    if (!s.startDate.isValid())
        return QObject::tr("Give it a first date.");
    if (s.endDate.isValid() && s.endDate < s.startDate)
        return QObject::tr("The end date is before the first date.");
    if (s.endMinutes <= s.startMinutes)
        return QObject::tr("The end time must be after the start time.");
    if (s.startMinutes < plan::kDayStartMinutes
        || s.endMinutes > plan::kDayEndMinutes)
        return QObject::tr("The planner's day runs %1 to midnight.")
            .arg(plan::kDayStartMinutes / 60);
    // A weekly rule whose days can never occur inside its own window is not
    // a rule, it is a silence. Cheap to check and impossible to diagnose
    // from the calendar, where it simply produces nothing.
    if (s.repeat == Task::Repeat::Weekly && s.endDate.isValid()) {
        const QList<int> days = effectiveWeekdays(s);
        bool any = false;
        for (QDate d = s.startDate; d <= s.endDate && !any; d = d.addDays(1))
            any = days.contains(d.dayOfWeek());
        if (!any)
            return QObject::tr("Those days never come round between the "
                               "first date and the end date.");
    }
    if (s.activityId.isEmpty() && s.title.trimmed().isEmpty())
        return QObject::tr("Give it something to be called.");
    return {};
}

// A human summary of a rule, as ONE OR TWO LINES:
//
//     Weekly  ·  Tue  ·  13:30-17:00
//     until Dec 15, 2026  ·  remind 15 min before
//
// WHY TWO LINES AND NOT ONE, which is what this returned when it was first
// written. A QPushButton reports its whole text as its MINIMUM width — the
// same trap `READING_GUIDE` §4 records for an unwrapped QLabel, and a
// button cannot word-wrap its way out of it. One long line measured 476dp
// inside a 360dp phone and pushed the activity editor to 546. Splitting the
// sentence where it already has a natural seam — WHEN it happens, then the
// qualifiers — drops the minimum to the longer of the two halves.
//
// Pure and here rather than in the dialog, because the same sentence has to
// appear in the activity editor and could easily be wanted on a block; two
// hand-built versions of it would drift the first time somebody adds a
// field. Reads dates through QLocale, which is Core.
inline QStringList summaryLines(const Schedule& s)
{
    const QLocale loc;
    const auto clock = [](int minutes) {
        return QStringLiteral("%1:%2")
            .arg(minutes / 60, 2, 10, QLatin1Char('0'))
            .arg(minutes % 60, 2, 10, QLatin1Char('0'));
    };
    const QString sep = QStringLiteral("  \u00B7  ");

    // Line 1 — WHEN it happens.
    QStringList when;
    if (s.repeat == Task::Repeat::None)
        when << loc.toString(s.startDate, QStringLiteral("MMM d, yyyy"));
    else
        when << repeatLabel(s.repeat);

    // The weekday is what a weekly rule actually MEANS to a reader, so it is
    // named — and read through effectiveWeekdays(), the same resolution the
    // walk uses, so the sentence cannot promise a day the calendar will not
    // deliver.
    if (s.repeat == Task::Repeat::Weekly) {
        QStringList names;
        for (int d : effectiveWeekdays(s))
            names << loc.dayName(d, QLocale::ShortFormat);
        if (!names.isEmpty())
            when << names.join(QStringLiteral(", "));
    }

    when << QStringLiteral("%1-%2").arg(clock(s.startMinutes),
                                        clock(s.endMinutes));

    // Line 2 — the qualifiers, and only when there are any. An empty second
    // line would give every row a blank half and make the list twice as
    // tall for nothing.
    QStringList qualifiers;
    if (s.endDate.isValid())
        qualifiers << QObject::tr("until %1")
                          .arg(loc.toString(s.endDate,
                                            QStringLiteral("MMM d, yyyy")));
    if (s.reminderMinutes > 0)
        qualifiers << QObject::tr("remind %1 min before").arg(s.reminderMinutes);

    QStringList out{when.join(sep)};
    if (!qualifiers.isEmpty())
        out << qualifiers.join(sep);
    return out;
}

// The one-line form, for anywhere a single string is wanted (a tooltip, a
// log, a test). Same words, same order, one separator — so the two renderings
// can never say different things.
inline QString summary(const Schedule& s)
{
    return summaryLines(s).join(QStringLiteral("  \u00B7  "));
}

} // namespace recur
