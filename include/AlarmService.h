#pragma once
// ---------------------------------------------------------------------------
// AlarmService — the thing that owns the SCHEDULE (v30.6; was
// BlockAlarmService, v19.7–v30.5).
//
// WHY THE RENAME. The old name was honest for eleven versions: the class
// watched planned blocks and nothing else. It now derives every alarm the
// app can know about in advance — block starts, the tracked block's end,
// the Pomodoro's phase end, the morning knock — and hands them over as one
// schedule. A class called BlockAlarmService carrying a Pomodoro alarm is
// the kind of small lie that costs the next reader an hour.
//
// WHY A SERVICE AND NOT THE WIDGET (unchanged since v19.7, still the reason
// this file exists): the AgendaWidget is a PAINTER, and up to ten of them
// are alive at once — day view, seven week columns, two in compare — so ten
// widgets would mean ten timers and ten toasts. And none may be visible
// when the alarm should fire; being elsewhere is the entire scenario a
// notification exists for. A block's schedule is DATA, and the thing that
// watches data plus the clock is app-lifetime state.
//
// WHAT CHANGED UNDERNEATH (the v30.6 inversion — see Alarms.h for the full
// argument). This class used to hold the judgement itself: it scanned
// events, applied the grace window, and emitted event IDS for MainWindow to
// re-resolve into text at fire time. All of that is now pure free functions
// in Alarms.h, and this class is reduced to what genuinely needs to be an
// object: it owns the CLOCK, the high-water MARK, and one QTimer, and it
// PUBLISHES.
//
// The publish half is the new part and the whole point. On desktop the
// schedule feeds our own QTimer, exactly as before. On Android it is handed
// to the OS, because the process will not be alive at fire time. This class
// does not know which — it emits scheduleChanged() and something in the UI
// layer decides. A service that touched a Notifier could not be constructed
// in a headless test, which is the same rule AffordabilityService states.
//
// WHAT KEEPS IT HONEST AND QUIET (all inherited, all still true):
//   - a HIGH-WATER MARK (m_announcedUpTo): only instants strictly after it
//     are ever announced, and it only moves forward. One remembered moment
//     makes duplicates and start-up back-spam both impossible (the mark is
//     born at "now").
//   - a GRACE WINDOW: waking from a long sleep, anything staler than
//     Rule::graceSeconds is skipped in silence.
//   - the OWN-HANDS rule: a block you are already tracking announces
//     nothing. That judgement moved into alarms::upcoming so it can survive
//     the app being closed — see Alarms.h.
// ---------------------------------------------------------------------------

#include "Alarms.h"

#include <QDate>
#include <QDateTime>
#include <QObject>
#include <QTimer>
#include <functional>

class AppData;

class AlarmService : public QObject
{
    Q_OBJECT

public:
    explicit AlarmService(const AppData* data, QObject* parent = nullptr);

    // TEST SEAM, the house pattern (TrackerService::nowProvider) — with one
    // instructive difference: THIS service reads the clock AT CONSTRUCTION
    // (the high-water mark is born at "now"), so a seam patched onto the
    // object afterwards would arrive too late — the mark would already be
    // wall-clock real. When a dependency is used in the constructor, it
    // must come in THROUGH the constructor; "inject after" only works for
    // dependencies first touched after. Hence the second ctor.
    AlarmService(const AppData* data, std::function<QDateTime()> now,
                 QObject* parent = nullptr);

    std::function<QDateTime()> nowProvider; // set by whichever ctor ran

    alarms::Rule rule; // public and plain, like afford::Rule and checkin::Rule

    // ---- the two facts that do NOT live in the planner ---------------------
    // Injected rather than looked up, for the reason AffordabilityService
    // injects its persona provider: reaching for TrackerService, the
    // Pomodoro engine and QSettings from here would make a headless test
    // build three collaborators to assert one grace window. Both default to
    // "nothing", so `AlarmService(&data)` is a complete, testable object.

    // Which block is being tracked right now (empty when idle). Drives both
    // mute rules in alarms::upcoming.
    void setTrackedEventProvider(std::function<QString()> provider);

    // Alarms that cannot be derived from AppData alone — today the
    // Pomodoro's phase end and the morning knock's QSettings ledger. The
    // composition root builds them with alarms::forPhase / forCheckIn.
    void setExtrasProvider(std::function<QVector<alarms::Alarm>()> provider);

    // The schedule as last derived — the WORKING SET, which reaches back to
    // the high-water mark so poll() can still find what just came due. What
    // is published outward (scheduleChanged) is the future-only subset; the
    // difference matters on Android, where a past instant handed to
    // AlarmManager rings at once. Public for the debug panel and tests.
    const QVector<alarms::Alarm>& schedule() const { return m_schedule; }

public slots:
    // Re-derive and re-publish. Wired to AppData::changed; the composition
    // root also calls it when the tracker or the Pomodoro moves, since
    // neither of those is a planner edit.
    void republish();

    // "Has anything come due?" Public so tests and the debug panel can ask
    // without waiting out a timer.
    void poll();

signals:
    // Fire now. Carries finished alarms rather than ids: the text was
    // rendered when the schedule was derived, because on a phone there is
    // no process left to resolve an id at fire time (Alarms.h).
    void due(const QVector<alarms::Alarm>& alarms);

    // The forward window changed. Desktop arms a timer from it; Android
    // hands the whole thing to AlarmManager.
    void scheduleChanged(const QVector<alarms::Alarm>& schedule);

private:
    void derive(const QDateTime& now);
    void rearm(const QDateTime& now);

    const AppData* m_data = nullptr;
    QDateTime      m_announcedUpTo;
    QTimer         m_timer;
    QVector<alarms::Alarm> m_schedule;

    std::function<QString()>                 m_tracked;
    std::function<QVector<alarms::Alarm>()>  m_extras;
};
