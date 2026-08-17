#pragma once
// ---------------------------------------------------------------------------
// CheckIn — WHEN the morning check-in is offered (v28.2, roadmap §G.1). Pure.
//
// The §A spine, third application: the model will eventually HOLD the
// check-in conversation (part 2), but whether to knock at all is code —
// deterministic, testable, and stingy on purpose. §G.1's rule verbatim:
// "fires once, in the morning, and only when the day is computably heavy
// — being asked how you are doing five times a day is its own stressor;
// a quiet Tuesday does not need a wellness interview."
//
// Heaviness is derived from facts that already exist: planned minutes
// today, plus deadlines landing soon. No new storage; the only state the
// feature needs ("did I already offer today?") is manners, lives in
// QSettings at the caller, and arrives here as a parameter — same
// pure/wire split as afford::decide.
// ---------------------------------------------------------------------------

#include "AppData.h"
#include "Event.h"
#include "Task.h"

#include <QDate>
#include <QDateTime>

namespace checkin
{

struct Rule
{
    // A day is heavy when EITHER trips: a wall of planned work, or a
    // cluster of near deadlines. 5h of blocks is a real day; 2 deadlines
    // inside 2 days is pressure even with an empty calendar.
    int heavyPlannedMinutes = 5 * 60;
    int urgentWithinDays    = 2;
    int urgentTaskCount     = 2;

    // The morning window. Opens with the agenda's day (06:00); closes at
    // 11:00 because a "morning" check-in at 15:40 is a different, worse
    // feature — if the morning was missed, the day already answered.
    int morningStartMin = 6 * 60;
    int morningEndMin   = 11 * 60;

    // §G.2: mood is kept 14 days, trimmed by the domain on the midnight
    // knock. Long enough for "Wednesdays are rough" to be visible, short
    // enough to be a check-in, not a dossier.
    int retentionDays = 14;
};

inline bool isDayHeavy(const AppData& data, QDate day, const Rule& rule = {})
{
    int planned = 0;
    for (const Event& e : data.events())
        if (e.date == day)
            planned += e.plannedEndMinutes - e.plannedStartMinutes;
    if (planned >= rule.heavyPlannedMinutes)
        return true;

    int urgent = 0;
    for (const Task& t : data.tasks()) {
        if (t.done || t.archived || !t.dueDate.isValid())
            continue;
        const qint64 in = day.daysTo(t.dueDate);
        if (in >= 0 && in <= rule.urgentWithinDays)
            ++urgent;
    }
    return urgent >= rule.urgentTaskCount;
}

// The whole gate: morning window ∧ not yet offered today ∧ heavy day.
// `lastOffered` is the caller's QSettings fact; passing it in keeps the
// once-a-day rule testable without touching settings.
inline bool shouldOffer(const AppData& data, const QDateTime& now,
                        QDate lastOffered, const Rule& rule = {})
{
    const int m = now.time().hour() * 60 + now.time().minute();
    if (m < rule.morningStartMin || m >= rule.morningEndMin)
        return false;
    if (lastOffered == now.date())
        return false; // once means once — asking twice is nagging squared
    return isDayHeavy(data, now.date(), rule);
}

} // namespace checkin
