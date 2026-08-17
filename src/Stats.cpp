#include "Stats.h"

namespace stats
{

Totals eventTotals(const Event& event)
{
    Totals t;
    for (const Segment& s : event.segments) {
        // A switch with NO default on purpose: add a fourth kind someday and
        // the compiler flags this spot (-Wswitch) instead of an `else`
        // silently miscounting it. The old `if Focus else` did exactly that —
        // it would have folded Distracted into breakSeconds.
        switch (s.kind) {
        case SegmentKind::Focus:      t.focusSeconds      += s.seconds(); break;
        case SegmentKind::Break:      t.breakSeconds      += s.seconds(); break;
        case SegmentKind::Distracted: t.distractedSeconds += s.seconds(); break;
        }
    }
    return t;
}

// How much of this block's planned window has already ELAPSED at `now`?
// Future block: none. Past day: all of it. Today: clamp now-minus-start to
// the window ([0..planned]); tracking past the window's end is legal
// (§3.38), which is why the caller clamps the SUBTRACTION, not this value.
static qint64 elapsedWindowSeconds(const Event& e, const QDateTime& now)
{
    if (e.date > now.date())
        return 0;
    if (e.date < now.date())
        return e.plannedSeconds();
    const int nowMin = now.time().hour() * 60 + now.time().minute();
    const int elapsed = qBound(0, nowMin - e.plannedStartMinutes,
                               e.plannedEndMinutes - e.plannedStartMinutes);
    return qint64(elapsed) * 60;
}

PeriodSummary summarize(const AppData& data, QDate from, QDate to,
                        const QDateTime& now)
{
    PeriodSummary summary;

    // Pre-create one byDay bucket per day so charts get a bar even for
    // silent days — a week chart with missing weekdays would be confusing.
    for (QDate d = from; d <= to; d = d.addDays(1))
        summary.byDay.append({d, Totals{}});

    for (const Event& e : data.events()) {
        if (e.date < from || e.date > to)
            continue;

        const Totals t = eventTotals(e);

        // Unaccounted accrues BEFORE the never-tracked early-exit below —
        // a block nobody pressed a button on is unaccounted time's whole
        // subject, and the old `continue` would have skipped exactly those.
        // Clamped at zero: tracking past the window's end is legal, so
        // tracked can legitimately exceed elapsed.
        summary.unaccountedSeconds +=
            qMax<qint64>(0, elapsedWindowSeconds(e, now) - t.total());

        if (t.total() == 0)
            continue; // planned but never tracked — nothing else to count

        summary.totals.focusSeconds += t.focusSeconds;
        summary.totals.breakSeconds += t.breakSeconds;
        summary.totals.distractedSeconds += t.distractedSeconds;

        // Attribute PRODUCTIVE time to the block's life area — focus
        // seconds ONLY. This is a documented REVERSAL (§3.37): the first
        // rule credited t.total(), "two distracted hours in School are
        // still two hours in School" — until the owner tracked 1h08m of
        // drift and watched the GTI350 bar claim it as accomplishment.
        // The bars answer "what did I produce per life area?"; break and
        // distracted accumulate in their own sink rows instead, derived
        // from totals by the displays. Resolution still via
        // AppData::eventCategoryId, whatever the block's identity —
        // "reference, don't copy" keeps paying rent.
        const QString catId = data.eventCategoryId(e);
        if (!catId.isEmpty())
            summary.byCategory[catId] += t.focusSeconds;

        const qint64 dayIndex = from.daysTo(e.date);
        if (dayIndex >= 0 && dayIndex < summary.byDay.size()) {
            summary.byDay[dayIndex].second.focusSeconds += t.focusSeconds;
            summary.byDay[dayIndex].second.breakSeconds += t.breakSeconds;
            summary.byDay[dayIndex].second.distractedSeconds += t.distractedSeconds;
        }
    }
    return summary;
}

PeriodSummary summarizeDay(const AppData& data, QDate day,
                           const QDateTime& now)
{
    return summarize(data, day, day, now);
}

PeriodSummary summarizeWeek(const AppData& data, QDate anyDayInWeek,
                            Qt::DayOfWeek firstDay, const QDateTime& now)
{
    const QDate start = weekStart(anyDayInWeek, firstDay);
    return summarize(data, start, start.addDays(6), now);
}

PeriodSummary summarizeMonth(const AppData& data, QDate anyDayInMonth,
                             const QDateTime& now)
{
    const QDate first(anyDayInMonth.year(), anyDayInMonth.month(), 1);
    return summarize(data, first, first.addDays(first.daysInMonth() - 1), now);
}

QString formatSeconds(qint64 seconds)
{
    seconds = qMax<qint64>(0, seconds);
    const qint64 minutes = seconds / 60;
    const qint64 hours   = minutes / 60;
    if (hours > 0)
        return QStringLiteral("%1h %2m").arg(hours).arg(minutes % 60, 2, 10, QChar('0'));
    if (minutes > 0)
        return QStringLiteral("%1m").arg(minutes);
    return QStringLiteral("%1s").arg(seconds);
}

} // namespace stats
