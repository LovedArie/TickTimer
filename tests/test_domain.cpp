// ---------------------------------------------------------------------------
// test_domain.cpp — automated proof that the domain keeps its promises.
//
// What gets tested, deliberately, is the DOMAIN AND STORAGE, not the UI:
// the rules (integrity, overlap), the maths (stats), and the two scariest
// paths in the app — persistence round-trip and crash recovery. These are
// exactly the layers we built WITHOUT Qt Widgets, and this file is the
// payoff: they run headless, in milliseconds, no window needed. Testable
// code and layered code turn out to be the same thing.
//
// Qt Test in one paragraph: each private slot is one test case; QVERIFY
// checks a condition, QCOMPARE checks equality (and prints both sides on
// failure, which is why it beats QVERIFY(a == b)). QTEST_GUILESS_MAIN
// generates a main() that runs every slot — no GUI, CI-friendly.
// ---------------------------------------------------------------------------

#include "AppData.h"
#include "AssistantVerbs.h" // v29.0 — the write boundary
#include "Intake.h"          // v29.1 — the interview's brain
#include "DayBriefing.h"
#include "Compare.h"
#include "ReturnPolicy.h"
#include "Version.h"
#include "Stats.h"
#include "BlockAlarmService.h"
#include "PomodoroEngine.h"
#include "PomodoroLink.h"
#include "TrackerService.h"
#include "JsonStore.h"
#include "TaskCoverage.h"
#include "MissedBlocks.h"
#include "Affordability.h"
#include "NudgePhrasing.h"
#include "CheckIn.h"
#include "LlmProvider.h"
#include "Reschedule.h"

#include <QTemporaryDir>
#include <QtTest>

namespace
{
// Test helper: a segment of `minutes` starting at a fixed, boring moment.
// Fixed timestamps make failures reproducible — never use currentDateTime()
// in a test if you can avoid it.
Segment makeSegment(SegmentKind kind, const QDateTime& start, int minutes)
{
    Segment s;
    s.kind  = kind;
    s.start = start;
    s.end   = start.addSecs(minutes * 60);
    return s;
}

const QDateTime kT0(QDate(2026, 7, 1), QTime(9, 0));
} // namespace

class TestDomain : public QObject
{
    Q_OBJECT

private slots:
    // ---- referential integrity (Supplementary Spec) -----------------------
    void categoryWithActivitiesCannotBeDeleted()
    {
        AppData data;
        const QString cat = data.addCategory("Work", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);

        QVERIFY(!data.removeCategory(cat));   // refused: not empty
        QVERIFY(data.removeActivity(act));    // empty the category…
        QVERIFY(data.removeCategory(cat));    // …now deletion is legal
    }

    // v21.1: '#tag' resolution promoted to a domain query the moment quick-add
    // grew a second surface. Exact name, case-insensitive, empty on no match —
    // never a fuzzy guess.
    void categoryIdByNameIsExactAndCaseInsensitive()
    {
        AppData data;
        const QString school = data.addCategory("School", QColor("#4C6FE0"));
        data.addCategory("Health", QColor("#2F7E6E"));

        QCOMPARE(data.categoryIdByName("school"), school);
        QCOMPARE(data.categoryIdByName("SCHOOL"), school);
        QCOMPARE(data.categoryIdByName("  School "), school); // trims
        QVERIFY(data.categoryIdByName("sch").isEmpty());      // no prefix magic
        QVERIFY(data.categoryIdByName("gym").isEmpty());      // unknown
        QVERIFY(data.categoryIdByName("").isEmpty());
    }

    void activityInUseCannotBeDeleted()
    {
        AppData data;
        const QString cat = data.addCategory("Work", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        const QString ev  = data.addEvent(QDate(2026, 7, 1), 540, 600, act);
        QVERIFY(!ev.isEmpty());

        QVERIFY(!data.removeActivity(act));   // an Event still points at it
        QVERIFY(data.removeEvent(ev));
        QVERIFY(data.removeActivity(act));    // reference gone -> deletable
    }

    void activityRequiresExistingCategory()
    {
        AppData data;
        QVERIFY(data.addActivity("Orphan", "no-such-id").isEmpty());
    }

    // ---- planning rules ------------------------------------------------------
    void overlappingEventsAreRejected()
    {
        AppData data;
        const QString cat = data.addCategory("Work", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        const QDate day(2026, 7, 1);

        QVERIFY(!data.addEvent(day, 540, 600, act).isEmpty()); // 9:00–10:00
        QVERIFY(data.addEvent(day, 570, 630, act).isEmpty());  // overlaps -> no
        QVERIFY(!data.addEvent(day, 600, 660, act).isEmpty()); // adjacent -> ok
        // Same times on ANOTHER day never conflict:
        QVERIFY(!data.addEvent(day.addDays(1), 540, 600, act).isEmpty());
    }

    void moveKeepsDurationAndSegments()
    {
        AppData data;
        const QString cat = data.addCategory("Work", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        const QDate day(2026, 7, 1);
        const QString ev = data.addEvent(day, 540, 600, act);
        data.appendSegment(ev, makeSegment(SegmentKind::Focus, kT0, 25));

        QVERIFY(data.moveEvent(ev, 720)); // 9:00 -> 12:00
        const Event* e = data.eventById(ev);
        QCOMPARE(e->plannedEndMinutes - e->plannedStartMinutes, 60);
        QCOMPARE(e->segments.size(), 1); // the tracked FACT travelled along

        const QString blocker = data.addEvent(day, 660, 690, act);
        QVERIFY(!blocker.isEmpty());
        QVERIFY(!data.moveEvent(ev, 660)); // landing on the blocker: refused
    }

    void resizeChangesSpanButGuardsTheRules()
    {
        AppData data;
        const QString cat = data.addCategory("Work", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        const QDate day(2026, 7, 1);
        const QString ev = data.addEvent(day, 540, 600, act); // 9:00–10:00
        data.appendSegment(ev, makeSegment(SegmentKind::Focus, kT0, 25));

        // Extend the end, then pull the start earlier — both legal.
        QVERIFY(data.resizeEvent(ev, 540, 660)); // -> 9:00–11:00
        QCOMPARE(data.eventById(ev)->plannedEndMinutes, 660);
        QVERIFY(data.resizeEvent(ev, 510, 660)); // -> 8:30–11:00
        QCOMPARE(data.eventById(ev)->plannedStartMinutes, 510);
        // The tracked fact stays put — resizing the PLAN doesn't touch history.
        QCOMPARE(data.eventById(ev)->segments.size(), 1);

        // Below one slot: refused, event untouched.
        QVERIFY(!data.resizeEvent(ev, 510, 520)); // 10 min span < 30
        QCOMPARE(data.eventById(ev)->plannedStartMinutes, 510);
        QCOMPARE(data.eventById(ev)->plannedEndMinutes, 660);

        // Past midnight: refused (isFree bounds).
        QVERIFY(!data.resizeEvent(ev, 510, 24 * 60 + 30));
    }

    void resizeRefusesOverlapWithOtherEvents()
    {
        AppData data;
        const QString cat = data.addCategory("Work", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        const QDate day(2026, 7, 1);
        const QString a = data.addEvent(day, 540, 600, act);  // 9:00–10:00
        const QString b = data.addEvent(day, 660, 720, act);  // 11:00–12:00
        QVERIFY(!a.isEmpty() && !b.isEmpty());

        // Growing 'a' into 'b' is refused; 'a' is left exactly as it was.
        QVERIFY(!data.resizeEvent(a, 540, 690)); // would cover 11:00–11:30
        QCOMPARE(data.eventById(a)->plannedEndMinutes, 600);
        // Growing 'a' up to (not into) 'b' is allowed — adjacency is legal.
        QVERIFY(data.resizeEvent(a, 540, 660));  // 9:00–11:00, touches b's start
        QCOMPARE(data.eventById(a)->plannedEndMinutes, 660);
    }

    // ---- derive, don't store ---------------------------------------------------
    void distractedTimeIsBucketedAndSurvivesRoundTrip()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("data.json");

        QString ev;
        {
            AppData data;
            const QString cat = data.addCategory("Work", QColor("#4C6FE0"));
            const QString act = data.addActivity("Study", cat);
            const QDate day(2026, 7, 1);
            ev = data.addEvent(day, 540, 660, act);

            data.appendSegment(ev, makeSegment(SegmentKind::Focus, kT0, 30));
            data.appendSegment(ev, makeSegment(SegmentKind::Distracted,
                                               kT0.addSecs(1800), 15));
            data.appendSegment(ev, makeSegment(SegmentKind::Break,
                                               kT0.addSecs(2700), 5));

            // Derived totals: distraction is its OWN bucket, NOT folded into
            // break (the bug the old `else` would have caused).
            const stats::Totals t = stats::eventTotals(*data.eventById(ev));
            QCOMPARE(t.focusSeconds,      qint64(30 * 60));
            QCOMPARE(t.distractedSeconds, qint64(15 * 60));
            QCOMPARE(t.breakSeconds,      qint64(5 * 60));
            QCOMPARE(t.total(),           qint64(50 * 60)); // all real time

            QVERIFY(JsonStore(path).save(data));
        }

        // The distracted kind round-trips through JSON unchanged.
        AppData loaded;
        QVERIFY(JsonStore(path).load(loaded));
        const stats::Totals t = stats::eventTotals(*loaded.eventById(ev));
        QCOMPARE(t.distractedSeconds, qint64(15 * 60));
        QCOMPARE(t.focusSeconds,      qint64(30 * 60));
        QCOMPARE(t.breakSeconds,      qint64(5 * 60));
    }

    void statsDeriveFromSegments()
    {
        AppData data;
        const QString cat = data.addCategory("Work", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        const QDate day(2026, 7, 1);
        const QString ev = data.addEvent(day, 540, 660, act); // 2 h planned

        data.appendSegment(ev, makeSegment(SegmentKind::Focus, kT0, 40));
        data.appendSegment(ev, makeSegment(SegmentKind::Break, kT0.addSecs(2400), 10));
        data.appendSegment(ev, makeSegment(SegmentKind::Focus, kT0.addSecs(3000), 25));
        data.appendSegment(ev, makeSegment(SegmentKind::Distracted,
                                           kT0.addSecs(4600), 20));

        const stats::PeriodSummary s = stats::summarizeDay(data, day);
        QCOMPARE(s.totals.focusSeconds, qint64(65 * 60));
        QCOMPARE(s.totals.breakSeconds, qint64(10 * 60));
        QCOMPARE(s.totals.distractedSeconds, qint64(20 * 60));
        // Category bars credit FOCUS ONLY (§3.37, reversing the original
        // t.total() rule after real use showed drift being read as
        // accomplishment). Break and distracted are pinned OUT of the bar.
        QCOMPARE(s.byCategory.value(cat), qint64(65 * 60));

        // The week containing that day sees the same numbers…
        QCOMPARE(stats::summarizeWeek(data, day).totals.focusSeconds,
                 qint64(65 * 60));
        // …and the day before sees nothing. No stored summary to go stale.
        QCOMPARE(stats::summarizeDay(data, day.addDays(-1)).totals.total(),
                 qint64(0));
    }

    void blockAlarmAnnouncesEachStartExactlyOnce()
    {
        AppData data;
        const QString cat = data.addCategory("Work", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        const QString ev  = data.addEvent(kT0.date(), 9 * 60, 10 * 60, act);

        // The movable clock, injected THROUGH the ctor (the mark is born
        // at "now", so a seam patched on afterwards would be too late).
        QDateTime now = kT0.addSecs(-120); // 08:58
        BlockAlarmService alarm(&data, [&now] { return now; });
        QSignalSpy fired(&alarm, &BlockAlarmService::blocksStarting);

        alarm.poll(); // 08:58 — nothing is due yet
        QCOMPARE(fired.count(), 0);

        now = kT0.addSecs(30); // 09:00:30 — inside the grace window
        alarm.poll();
        QCOMPARE(fired.count(), 1);
        const auto ids =
            fired.takeFirst().at(0).value<QVector<QString>>();
        QCOMPARE(ids, QVector<QString>{ev});

        alarm.poll(); // the high-water mark forbids a second announcement
        QCOMPARE(fired.count(), 0);

        // A later block on the same day is its own alarm.
        data.addEvent(kT0.date(), 10 * 60, 11 * 60, act);
        now = kT0.addSecs(3630); // 10:00:30
        alarm.poll();
        QCOMPARE(fired.count(), 1);
    }

    void blockAlarmSkipsStaleStartsInSilence()
    {
        AppData data;
        const QString cat = data.addCategory("Work", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        data.addEvent(kT0.date(), 9 * 60, 10 * 60, act);

        QDateTime now = kT0.addSecs(-60);
        BlockAlarmService alarm(&data, [&now] { return now; });
        QSignalSpy fired(&alarm, &BlockAlarmService::blocksStarting);

        // The laptop slept through 09:00 and woke at 09:05 — five minutes
        // stale is past the grace window: silence, not a late toast...
        now = kT0.addSecs(300);
        alarm.poll();
        QCOMPARE(fired.count(), 0);

        // ...and no resurrection either: the mark moved forward anyway.
        now = kT0.addSecs(360);
        alarm.poll();
        QCOMPARE(fired.count(), 0);
    }

    void blockAlarmIgnoresBlocksCreatedAlreadyUnderway()
    {
        AppData data;
        const QString cat = data.addCategory("Work", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);

        // Born at 09:05; the block you then create for 09:00 is your own
        // hands at work — its start is behind the mark, so: nothing.
        QDateTime now = kT0.addSecs(300);
        BlockAlarmService alarm(&data, [&now] { return now; });
        QSignalSpy fired(&alarm, &BlockAlarmService::blocksStarting);

        data.addEvent(kT0.date(), 9 * 60, 10 * 60, act);
        alarm.poll();
        QCOMPARE(fired.count(), 0);
    }

    void blockAlarmSweepsManyDueStartsButAnnouncesOnlyFreshOnes()
    {
        // One poll after a long stall can find SEVERAL due blocks at once.
        // The contract: stale ones (past grace) go silent, fresh ones get
        // announced, and the mark sweeps past ALL of them — that's why the
        // signal carries a vector. (With today's 30-min slot grid and a
        // 2-min grace, two FRESH blocks in one poll can't happen — the
        // vector is headroom, and this test pins the sweep that can.)
        AppData data;
        const QString cat = data.addCategory("Work", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        data.addEvent(kT0.date(), 9 * 60, 9 * 60 + 30, act);       // 09:00
        const QString fresh =
            data.addEvent(kT0.date(), 9 * 60 + 30, 10 * 60, act);  // 09:30

        QDateTime now = kT0.addSecs(-60); // 08:59
        BlockAlarmService alarm(&data, [&now] { return now; });
        QSignalSpy fired(&alarm, &BlockAlarmService::blocksStarting);

        // The app stalls straight through 09:00 and polls at 09:31 —
        // 09:00 is 31 min stale (silence), 09:30 is 60 s fresh (toast).
        now = kT0.addSecs(31 * 60);
        alarm.poll();
        QCOMPARE(fired.count(), 1);
        const auto ids = fired.takeFirst().at(0).value<QVector<QString>>();
        QCOMPARE(ids, QVector<QString>{fresh});

        // And the swept-past 09:00 can never resurrect.
        now = kT0.addSecs(32 * 60);
        alarm.poll();
        QCOMPARE(fired.count(), 0);
    }

    void pomodoroEngineWalksTheClassicCycle()
    {
        PomodoroEngine engine;
        engine.setDurations(1, 1, 2); // minute-long phases: 60 ticks each

        QSignalSpy ended(&engine, &PomodoroEngine::phaseEnded);

        QVERIFY(!engine.engaged());
        engine.start();
        QVERIFY(engine.engaged());
        QCOMPARE(engine.phase(), PomodoroEngine::Phase::Focus);
        QCOMPARE(engine.remaining(), 60);

        // 60 seconds of focus -> flows INTO the short break by itself
        // (still running — the rhythm needs no click between phases), and
        // phaseEnded fires exactly once.
        for (int i = 0; i < 60; ++i)
            engine.tickOneSecond();
        QCOMPARE(engine.phase(), PomodoroEngine::Phase::ShortBreak);
        QVERIFY(engine.running());
        QCOMPARE(ended.count(), 1);

        // Skip is DELIBERATE: it advances (break -> focus, round 2) but
        // announces nothing — no toast for what you did with your own hands.
        engine.skip();
        QCOMPARE(engine.phase(), PomodoroEngine::Phase::Focus);
        QCOMPARE(engine.round(), 2);
        QCOMPARE(ended.count(), 1); // unchanged

        // Rounds advance on break -> focus, so skips come in PAIRS
        // (focus->break, break->focus). Walk to round 4; its focus must
        // earn the LONG break — and remaining==120 (our 2-min long break)
        // proves WHICH break, not just "a break".
        engine.skip();               // r2: focus -> short break
        engine.skip();               //     break -> focus, round 3
        QCOMPARE(engine.round(), 3);
        engine.skip();               // r3: focus -> short break
        engine.skip();               //     break -> focus, round 4
        QCOMPARE(engine.round(), 4);
        engine.skip();               // the 4th focus ends...
        QCOMPARE(engine.phase(), PomodoroEngine::Phase::LongBreak);
        QCOMPARE(engine.remaining(), 120);

        // Pause keeps ENGAGED true (pulled away, coming back); reset drops
        // it (walked away) — the exact bit the tracker link steers by.
        engine.pause();
        QVERIFY(engine.engaged());
        engine.reset();
        QVERIFY(!engine.engaged());
        QCOMPARE(engine.round(), 1);
        QCOMPARE(engine.phase(), PomodoroEngine::Phase::Focus);
    }

    void completingARepeatingTaskSpawnsItsNextOccurrence()
    {
        // Since v7 the UI stored and SHOWED a repeat rule that completion
        // ignored — decoration. v19.10 makes it real, with one invariant
        // carrying the whole design: THE RULE LIVES ON THE NEWEST LINK.
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QString id  = data.addTask("Lab report", cat, QDate(2026, 7, 10));
        data.updateTask(id, "Lab report", "sect. 4", QDate(2026, 7, 10), QTime(),
                        Task::Repeat::Weekly, Task::Priority::Urgent);

        QCOMPARE(data.tasks().size(), 1);
        data.setTaskDone(id, true);

        // The next occurrence exists: fresh id, +7 days, everything
        // carried — title, notes, priority, and the RULE.
        QCOMPARE(data.tasks().size(), 2);
        const Task* old_ = data.taskById(id);
        const Task* next = nullptr;
        for (const Task& t : data.tasks())
            if (t.id != id)
                next = &t;
        QVERIFY(next);
        QVERIFY(old_->done);
        QCOMPARE(old_->repeat, Task::Repeat::None); // stripped: chain rule
        QCOMPARE(next->dueDate, QDate(2026, 7, 17));
        QCOMPARE(next->repeat, Task::Repeat::Weekly);
        QCOMPARE(next->priority, Task::Priority::Urgent);
        QCOMPARE(next->description, QStringLiteral("sect. 4"));
        QVERIFY(!next->done);

        // The invariant IS the duplicate guard: cycle the old task
        // undone -> done again, and nothing new spawns — its rule is gone.
        data.setTaskDone(id, false);
        data.setTaskDone(id, true);
        QCOMPARE(data.tasks().size(), 2);
    }

    void repeatingBlocksRollForwardHonestly()
    {
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study PHY335", cat);
        // A weekly Monday block, last materialized Jun 29 (a Monday):
        const QString ev = data.addEvent(QDate(2026, 6, 29),
                                         10 * 60, 12 * 60, act);
        data.setEventRepeat(ev, Task::Repeat::Weekly);

        // Twelve days pass unopened. The roll must NOT backfill Jul 6 —
        // an empty plan for a day you weren't there is noise, not
        // history — it re-arms at the first rule date >= today: Jul 13.
        QCOMPARE(data.rollRepeats(QDate(2026, 7, 11)), 1);
        QCOMPARE(data.events().size(), 2);
        const Event* spawned = nullptr;
        for (const Event& e : data.events())
            if (e.id != ev)
                spawned = &e;
        QCOMPARE(spawned->date, QDate(2026, 7, 13));
        QCOMPARE(spawned->plannedStartMinutes, 10 * 60);
        QCOMPARE(spawned->repeat, Task::Repeat::Weekly);   // newest link
        QCOMPARE(data.eventById(ev)->repeat, Task::Repeat::None); // stripped
        QVERIFY(spawned->segments.isEmpty()); // identity copies, not history

        // Idempotent within a day: rolling again spawns nothing.
        QCOMPARE(data.rollRepeats(QDate(2026, 7, 11)), 0);
    }

    void rollSkipsOccupiedDatesInsteadOfFighting()
    {
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        const QString ev = data.addEvent(QDate(2026, 7, 6), // Monday
                                         10 * 60, 12 * 60, act);
        data.setEventRepeat(ev, Task::Repeat::Weekly);
        // Jul 13's slots are already taken — the domain forbids overlap,
        // and the roll must respect the door, not shove through it:
        data.addEvent(QDate(2026, 7, 13), 10 * 60, 12 * 60, act);

        QCOMPARE(data.rollRepeats(QDate(2026, 7, 12)), 1);
        // The chain re-armed one rule-step later, on the free Jul 20.
        const Event* spawned = nullptr;
        for (const Event& e : data.events())
            if (e.repeat == Task::Repeat::Weekly)
                spawned = &e;
        QVERIFY(spawned);
        QCOMPARE(spawned->date, QDate(2026, 7, 20));
    }

    void eventRepeatSurvivesTheJsonRoundTripAndOldFilesReadAsNone()
    {
        AppData data;
        const QString cat = data.addCategory("Work", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        const QString ev  = data.addEvent(QDate(2026, 7, 13),
                                          9 * 60, 10 * 60, act);
        data.setEventRepeat(ev, Task::Repeat::Monthly);

        AppData copy;
        JsonStore::applyJsonObject(copy, JsonStore::toJsonObject(data),
                                   /*announceChange=*/false);
        QCOMPARE(copy.eventById(ev)->repeat, Task::Repeat::Monthly);

        // Pre-v9 files carry no field: absent must read as "nothing
        // repeats" — exactly how those files always behaved.
        QJsonObject blob = JsonStore::toJsonObject(data);
        QJsonArray events = blob["events"].toArray();
        QJsonObject e0 = events[0].toObject();
        e0.remove("repeat");
        events[0] = e0;
        blob["events"] = events;
        AppData old_;
        JsonStore::applyJsonObject(old_, blob, /*announceChange=*/false);
        QCOMPARE(old_.eventById(ev)->repeat, Task::Repeat::None);
    }

    void trackerStopsItselfWhenTheWindowCloses()
    {
        AppData data;
        const QString cat = data.addCategory("Work", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        const QString ev  = data.addEvent(kT0.date(), 9 * 60, 10 * 60, act);

        TrackerService tracker(&data);
        QDateTime now = kT0.addSecs(600); // 09:10, inside the window
        tracker.nowProvider = [&now] { return now; };
        QSignalSpy ended(&tracker, &TrackerService::trackedBlockEnded);

        tracker.startFocus(ev);
        tracker.enforceWindow(); // window open: the exit door does nothing
        QCOMPARE(tracker.state(), TrackerService::State::Focusing);
        QCOMPARE(ended.count(), 0);

        // 10:00:03 — the window has closed; the next tick's enforcement
        // commits and stops. The final segment's end is the REAL moment
        // (a breath past the boundary), because segments record what
        // happened, not what was planned.
        now = QDateTime(kT0.date(), QTime(10, 0, 3));
        tracker.enforceWindow();
        QCOMPARE(tracker.state(), TrackerService::State::Idle);
        QCOMPARE(ended.count(), 1);
        QCOMPARE(ended.takeFirst().at(0).toString(), ev);
        const Event* e = data.eventById(ev);
        QVERIFY(!e->segments.isEmpty());
        QCOMPARE(e->segments.last().end,
                 QDateTime(kT0.date(), QTime(10, 0, 3)));

        tracker.enforceWindow(); // idempotent: Idle has nothing to enforce
        QCOMPARE(ended.count(), 0);
    }

    void pomodoroPausesWhenItsBlockRunsOut()
    {
        AppData data;
        const QString cat = data.addCategory("Work", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        data.addEvent(kT0.date(), 9 * 60, 10 * 60, act);

        TrackerService tracker(&data);
        QDateTime now = kT0.addSecs(600);
        tracker.nowProvider = [&now] { return now; };

        PomodoroEngine engine;
        engine.setDurations(25, 5, 15);
        PomodoroLink link(&engine, &tracker);
        link.setEnabled(true);

        engine.start(); // play edge adopts the 9–10 block (v19.7)
        QCOMPARE(tracker.state(), TrackerService::State::Focusing);

        // The block runs out mid-focus: tracking stops (exit door) AND the
        // engine PAUSES — not resets: engaged stays true, the cycle
        // survives lunch, and ▶ later adopts whatever is under the clock.
        now = QDateTime(kT0.date(), QTime(10, 0, 2));
        tracker.enforceWindow();
        QCOMPARE(tracker.state(), TrackerService::State::Idle);
        QVERIFY(!engine.running());
        QVERIFY(engine.engaged());

        // Rule-8 interlock, now load-bearing: the pause above must NOT
        // re-adopt-and-stamp-distracted. Tracker stays Idle.
        QCOMPARE(tracker.state(), TrackerService::State::Idle);

        // And with the link DISABLED, the block's end is none of the
        // Pomodoro's business: it keeps running.
        now = kT0.addSecs(600);
        engine.reset();
        link.setEnabled(false);
        tracker.startFocus(data.events().first().id);
        engine.start();
        now = QDateTime(kT0.date(), QTime(10, 0, 2));
        tracker.enforceWindow();
        QCOMPARE(tracker.state(), TrackerService::State::Idle);
        QVERIFY(engine.running()); // unlinked machines stay strangers
    }

    void pomodoroLinkDrivesTheBlockUnderTheClock()
    {
        AppData data;
        const QString cat = data.addCategory("Work", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        // A block that is LIVE at the fixed test moment (09:10 inside
        // 09:00–11:00) — canTrackNow and liveEventNow both see it.
        const QString ev = data.addEvent(kT0.date(), 9 * 60, 11 * 60, act);

        TrackerService tracker(&data);
        QDateTime now = kT0.addSecs(600); // 09:10
        tracker.nowProvider = [&now] { return now; };

        PomodoroEngine engine;
        engine.setDurations(1, 1, 2);
        PomodoroLink link(&engine, &tracker);
        link.setEnabled(true);

        // Rule 1 (v19.7): the PLAY edge adopts the block under the clock —
        // you picked it when you planned it; pressing play starts the plan
        // recording itself. Focus phase => focus kind.
        engine.start();
        QCOMPARE(tracker.state(), TrackerService::State::Focusing);
        QCOMPARE(tracker.trackedEventId(), ev);

        // Rule 2: paused = distracted; resumed = focus (unchanged).
        engine.pause();
        QCOMPARE(tracker.state(), TrackerService::State::Distracted);
        engine.start();
        QCOMPARE(tracker.state(), TrackerService::State::Focusing);

        // Rule 3: the countdown crossing into a break switches the kind by
        // itself (unchanged — this is the whole feature).
        for (int i = 0; i < 60; ++i)
            engine.tickOneSecond();
        QCOMPARE(engine.phase(), PomodoroEngine::Phase::ShortBreak);
        QCOMPARE(tracker.state(), TrackerService::State::OnBreak);

        // Rule 4: a human Stop OUTRANKS the machine — phase flips are not
        // play edges, so the stopped tracker stays stopped through them...
        tracker.stop();
        engine.skip(); // break -> focus, round 2 — a transition, not a play
        QCOMPARE(tracker.state(), TrackerService::State::Idle);

        // ...but pressing play AGAIN is a fresh human command, and the
        // fresh command re-adopts the block still under the clock.
        engine.pause();
        engine.start();
        QCOMPARE(tracker.state(), TrackerService::State::Focusing);
        QCOMPARE(tracker.trackedEventId(), ev);

        // Rule 5: reset = abandoned = hands off (unchanged).
        engine.reset();
        QCOMPARE(tracker.state(), TrackerService::State::Focusing);

        // Rule 6: adoption never invents a block. Outside every planned
        // window (13:00), a play edge finds no live block and waits.
        tracker.stop();
        now = QDateTime(kT0.date(), QTime(13, 0));
        engine.start();
        QCOMPARE(tracker.state(), TrackerService::State::Idle);

        // Rule 7: enabling the link mid-run counts as a play edge (the
        // tick was the user's action just now) — back inside the window.
        link.setEnabled(false);
        now = kT0.addSecs(900); // 09:15, block live again
        engine.reset();
        engine.start();         // link disabled: nothing happens
        QCOMPARE(tracker.state(), TrackerService::State::Idle);
        link.setEnabled(true);  // ...until the box is ticked mid-run
        QCOMPARE(tracker.state(), TrackerService::State::Focusing);

        // Rule 8: adopting-while-PAUSED is forbidden — an untouched block
        // must never be stamped DISTRACTED by a Pomodoro lying idle.
        tracker.stop();
        engine.pause();         // engaged, not running
        link.setEnabled(false);
        link.setEnabled(true);  // enable edge, but engine paused
        QCOMPARE(tracker.state(), TrackerService::State::Idle);
    }

    void weekStartPreferenceMovesTheWeekBoundary()
    {
        // The formula itself, at its edges: for any first day, weekStart of
        // that day is itself, and the day BEFORE it belongs to the previous
        // week. 2026-07-05 is a Sunday, 2026-07-06 a Monday.
        const QDate sun(2026, 7, 5), mon(2026, 7, 6);
        QCOMPARE(stats::weekStart(mon, Qt::Monday), mon);
        QCOMPARE(stats::weekStart(sun, Qt::Monday), mon.addDays(-7));
        QCOMPARE(stats::weekStart(sun, Qt::Sunday), sun);
        QCOMPARE(stats::weekStart(mon, Qt::Sunday), sun);
        QCOMPARE(stats::weekStart(sun.addDays(-1), Qt::Sunday),
                 sun.addDays(-7)); // Saturday closes the PREVIOUS week

        // And the boundary MOVES real numbers: focus tracked on Sunday is
        // inside "the week of Wednesday" only when Sunday starts the week.
        // Same data, different firstDay, different totals — the parameter
        // is behaviour, not decoration.
        AppData data;
        const QString cat = data.addCategory("Work", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        const QString ev  = data.addEvent(sun, 540, 600, act);
        data.appendSegment(
            ev, makeSegment(SegmentKind::Focus,
                            QDateTime(sun, QTime(9, 0)), 30));

        const QDate wed = sun.addDays(3);
        QCOMPARE(stats::summarizeWeek(data, wed, Qt::Sunday)
                     .totals.focusSeconds,
                 qint64(30 * 60));
        QCOMPARE(stats::summarizeWeek(data, wed, Qt::Monday)
                     .totals.focusSeconds,
                 qint64(0)); // Mon-first week of Wednesday starts Jul 6 —
                             // Sunday's work belongs to LAST week there
    }

    void zeroLengthSegmentsAreDropped()
    {
        AppData data;
        const QString cat = data.addCategory("Work", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        const QString ev  = data.addEvent(QDate(2026, 7, 1), 540, 600, act);
        QVERIFY(!data.appendSegment(ev, makeSegment(SegmentKind::Focus, kT0, 0)));
        QCOMPARE(data.eventById(ev)->segments.size(), 0);
    }

    // ---- tasks (the addendum's promises) --------------------------------------
    void categoryWithTasksCannotBeDeleted()
    {
        AppData data;
        const QString cat  = data.addCategory("School", QColor("#4C6FE0"));
        const QString task = data.addTask("Lab 4", cat);
        QVERIFY(!task.isEmpty());

        QVERIFY(!data.removeCategory(cat)); // holds a task: refused
        QVERIFY(data.removeTask(task));
        QVERIFY(data.removeCategory(cat));  // empty now: legal
    }

    void taskBirthRules()
    {
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        QVERIFY(data.addTask("Lab 4", "no-such-category").isEmpty());
        QVERIFY(data.addTask("   ", cat).isEmpty());
    }

    void tasksSortLikeATodoList()
    {
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QString tbd     = data.addTask("Zeta TBD", cat);
        const QString soon    = data.addTask("Due soon", cat, QDate(2026, 7, 10));
        const QString later   = data.addTask("Due later", cat, QDate(2026, 8, 8));
        const QString doneOne = data.addTask("Already done", cat, QDate(2026, 7, 1));
        data.setTaskDone(doneOne, true);

        const auto sorted = data.tasksIn(cat);
        QCOMPARE(sorted.size(), 4);
        QCOMPARE(sorted[0]->id, soon);    // open + most urgent first
        QCOMPARE(sorted[1]->id, later);
        QCOMPARE(sorted[2]->id, tbd);     // open but undated after dated
        QCOMPARE(sorted[3]->id, doneOne); // finished sink to the bottom
    }

    void tasksRoundTripIncludingTbd()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("data.json");

        AppData original;
        const QString cat = original.addCategory("School", QColor("#4C6FE0"));
        const QString tbd   = original.addTask("Lab 4 (5%) - DATE TBD", cat);
        const QString dated = original.addTask("FINAL (35%)", cat, QDate(2026, 8, 8));
        original.setTaskDone(dated, true);

        QVERIFY(JsonStore(path).save(original));
        AppData loaded;
        QVERIFY(JsonStore(path).load(loaded));

        QCOMPARE(loaded.tasks().size(), 2);
        QVERIFY(!loaded.taskById(tbd)->dueDate.isValid()); // TBD survived
        QCOMPARE(loaded.taskById(dated)->dueDate, QDate(2026, 8, 8));
        QVERIFY(loaded.taskById(dated)->done);
    }

    void taskDetailsRoundTripAndGuardEmptyTitle()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("data.json");

        QString id;
        {
            AppData original;
            const QString cat = original.addCategory("School", QColor("#4C6FE0"));
            id = original.addTask("Lab 4", cat, QDate(2026, 8, 8));

            // The coarse edit the detail panel performs.
            QVERIFY(original.updateTask(id, "Lab 4 (revised)",
                                        "Bring the signed form.\nRoom B-204",
                                        QDate(2026, 8, 9), QTime(), Task::Repeat::Weekly));
            // A task must keep a real title: an all-space title is refused,
            // and refusal leaves the task untouched.
            QVERIFY(!original.updateTask(id, "   ", "x", QDate(), QTime(),
                                         Task::Repeat::None));
            QCOMPARE(original.taskById(id)->title, QString("Lab 4 (revised)"));

            QVERIFY(JsonStore(path).save(original));
        }

        AppData loaded;
        QVERIFY(JsonStore(path).load(loaded));
        const Task* t = loaded.taskById(id);
        QVERIFY(t);
        QCOMPARE(t->title, QString("Lab 4 (revised)"));
        QVERIFY(t->description.contains("B-204"));   // notes survived
        QCOMPARE(t->dueDate, QDate(2026, 8, 9));
        QCOMPARE(t->repeat, Task::Repeat::Weekly);   // recurrence survived
    }

    void newTaskDefaultsToNoNotesNoRepeat()
    {
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QString id  = data.addTask("Fresh", cat);
        const Task* t = data.taskById(id);
        QVERIFY(t);
        QVERIFY(t->description.isEmpty());
        QCOMPARE(t->repeat, Task::Repeat::None);
    }

    void tasksDueOnIsExactDayAndUndone()
    {
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QDate   day = QDate(2026, 7, 10);
        const QString onDay  = data.addTask("On the day", cat, day);
        const QString done   = data.addTask("Done that day", cat, day);
        data.setTaskDone(done, true);                    // finished: excluded
        data.addTask("Other day", cat, day.addDays(1));  // wrong day: excluded
        data.addTask("No date", cat);                    // TBD: excluded

        const auto due = data.tasksDueOn(day);
        QCOMPARE(due.size(), 1);
        QCOMPARE(due.first()->id, onDay);
    }

    // ---- folders & special days (addendum #2's promises) -----------------------
    void folderRules()
    {
        AppData data;
        const QString folder = data.addFolder("School");
        QVERIFY(!folder.isEmpty());
        const QString cat = data.addCategory("Work", QColor("#4C6FE0"));

        QVERIFY(!data.setCategoryFolder(cat, "no-such-folder")); // born broken: refused
        QVERIFY(data.setCategoryFolder(cat, folder));
        QVERIFY(!data.removeFolder(folder));                     // occupied: refused
        QVERIFY(data.setCategoryFolder(cat, QString()));         // back to top level
        QVERIFY(data.removeFolder(folder));                      // empty: legal
    }

    void specialDayNextOccurrence()
    {
        SpecialDay birthday;
        birthday.date = QDate(1990, 3, 10);
        birthday.repeatsYearly = true;
        QCOMPARE(birthday.nextOccurrence(QDate(2026, 7, 4)),  QDate(2027, 3, 10));
        QCOMPARE(birthday.nextOccurrence(QDate(2026, 2, 1)),  QDate(2026, 3, 10));
        QCOMPARE(birthday.nextOccurrence(QDate(2026, 3, 10)), QDate(2026, 3, 10)); // today counts

        SpecialDay leap; // the documented Feb 29 -> Mar 1 rule (§3.14)
        leap.date = QDate(2024, 2, 29);
        leap.repeatsYearly = true;
        QCOMPARE(leap.nextOccurrence(QDate(2026, 7, 4)), QDate(2027, 3, 1));

        SpecialDay oneOff;
        oneOff.date = QDate(2026, 7, 15);
        QCOMPARE(oneOff.nextOccurrence(QDate(2026, 7, 4)), QDate(2026, 7, 15));
    }

    void upcomingTasksIsAQueryNotATable()
    {
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        data.addTask("TBD stays out", cat);                       // undated: excluded
        const QString done = data.addTask("Done stays out", cat, QDate(2026, 7, 5));
        data.setTaskDone(done, true);                             // finished: excluded
        const QString late = data.addTask("Late", cat, QDate(2026, 6, 25));
        const QString soon = data.addTask("Soon", cat, QDate(2026, 7, 10));

        const auto up = data.upcomingTasks();
        QCOMPARE(up.size(), 2);
        QCOMPARE(up[0]->id, late); // most urgent first
        QCOMPARE(up[1]->id, soon);
    }

    void organizingRoundTrip()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("data.json");

        AppData original;
        const QString folder = original.addFolder("School");
        const QString cat = original.addCategory("Work", QColor("#4C6FE0"));
        original.setCategoryFolder(cat, folder);
        original.addSpecialDay("Christmas", QDate(2000, 12, 25), true);

        QVERIFY(JsonStore(path).save(original));
        AppData loaded;
        QVERIFY(JsonStore(path).load(loaded));

        QCOMPARE(loaded.folders().size(), 1);
        QCOMPARE(loaded.categoryById(cat)->folderId, folder);
        QCOMPARE(loaded.specialDays().size(), 1);
        QVERIFY(loaded.specialDays()[0].repeatsYearly);
        QCOMPARE(loaded.specialDays()[0].date, QDate(2000, 12, 25));
    }

    // ---- persistence ---------------------------------------------------------------
    void jsonRoundTripPreservesEverything()
    {
        QTemporaryDir dir; // self-deleting sandbox — tests never touch real data
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("data.json");

        AppData original;
        const QString cat = original.addCategory("Health", QColor("#4CA96A"));
        const QString act = original.addActivity("Gym", cat);
        const QString ev  = original.addEvent(QDate(2026, 7, 1), 540, 630, act);
        original.setEventNote(ev, "felt good");
        original.appendSegment(ev, makeSegment(SegmentKind::Focus, kT0, 45));

        QVERIFY(JsonStore(path).save(original));

        AppData loaded;
        QVERIFY(JsonStore(path).load(loaded));

        QCOMPARE(loaded.categories().size(), 1);
        QCOMPARE(loaded.categoryById(cat)->color, QColor("#4CA96A"));
        QCOMPARE(loaded.activityById(act)->name, QString("Gym"));
        const Event* e = loaded.eventById(ev);
        QVERIFY(e);
        QCOMPARE(e->note, QString("felt good"));
        QCOMPARE(e->segments.size(), 1);
        QCOMPARE(e->segments[0].start, kT0);          // timestamps intact
        QCOMPARE(e->segments[0].seconds(), qint64(45 * 60));
    }

    // ---- crash recovery (the Reliability requirement, end to end) -----------
    void interruptedTrackingIsRecoveredOnLoad()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("data.json");

        {
            // Simulated life before the "crash": tracking is running, the
            // heartbeat stamped lastSeen 17 minutes in — then the process
            // dies (this scope ends) WITHOUT any clean stop().
            AppData before;
            const QString cat = before.addCategory("Work", QColor("#4C6FE0"));
            const QString act = before.addActivity("Study", cat);
            const QString ev  = before.addEvent(QDate(2026, 7, 1), 540, 600, act);

            RunningState running;
            running.eventId  = ev;
            running.kind     = SegmentKind::Focus;
            running.start    = kT0;
            running.lastSeen = kT0.addSecs(17 * 60);
            before.setRunning(running);

            QVERIFY(JsonStore(path).save(before));
        }

        // Next launch:
        AppData after;
        QVERIFY(JsonStore(path).load(after));
        const QString message = after.recoverInterruptedTracking();

        QVERIFY(!message.isEmpty());               // the user is told
        QVERIFY(!after.running().has_value());     // insurance consumed
        QCOMPARE(after.events()[0].segments.size(), 1);
        QCOMPARE(after.events()[0].segments[0].seconds(),
                 qint64(17 * 60));                 // 17 min rescued
    }

    // ---- block identity (block-labels addendum) -----------------------------
    // Three ways a block can say what it is: an Activity, a Task, or just a
    // typed title. These tests pin the invariant ("at least one identity"),
    // the resolution helpers, and the reference-cleanup on task deletion.

    void taskEventResolvesLabelAndCategory()
    {
        AppData data;
        const QString cat  = data.addCategory("School", QColor("#4C6FE0"));
        const QString task = data.addTask("Lab 4", cat, QDate(2026, 8, 8));

        const QString ev = data.addTaskEvent(QDate(2026, 7, 6), 540, 600, task);
        QVERIFY(!ev.isEmpty());

        const Event* e = data.eventById(ev);
        QVERIFY(e->activityId.isEmpty());              // a task block, not an
        QCOMPARE(e->taskId, task);                     // activity occurrence
        QCOMPARE(data.eventLabel(*e), QString("Lab 4"));
        QCOMPARE(data.eventCategoryId(*e), cat);       // via the Task's area

        // The new door runs through the SAME overlap gate as the old one.
        QVERIFY(data.addTaskEvent(QDate(2026, 7, 6), 570, 630, task).isEmpty());
        // And it verifies its identity: a made-up task id is refused.
        QVERIFY(data.addTaskEvent(QDate(2026, 7, 6), 660, 720,
                                  "no-such-task").isEmpty());
    }

    void adHocEventRequiresATitle()
    {
        AppData data;
        // The title IS the identity — whitespace is not a name.
        QVERIFY(data.addAdHocEvent(QDate(2026, 7, 6), 540, 600, "").isEmpty());
        QVERIFY(data.addAdHocEvent(QDate(2026, 7, 6), 540, 600, "   ").isEmpty());

        const QString ev =
            data.addAdHocEvent(QDate(2026, 7, 6), 540, 600, "  Call the bank  ");
        QVERIFY(!ev.isEmpty());

        const Event* e = data.eventById(ev);
        QCOMPARE(e->title, QString("Call the bank"));  // stored trimmed
        QCOMPARE(data.eventLabel(*e), QString("Call the bank"));
        QVERIFY(data.eventCategoryId(*e).isEmpty());   // no life area, honestly
    }

    void adHocTitleCannotBeCleared()
    {
        AppData data;
        const QString cat = data.addCategory("Work", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);

        const QString adhoc =
            data.addAdHocEvent(QDate(2026, 7, 6), 540, 600, "Errand");
        const QString planned =
            data.addEvent(QDate(2026, 7, 6), 660, 720, act, "chapter 3");

        // Clearing the ad-hoc title would leave a nameless plan: refused.
        QVERIFY(!data.setEventTitle(adhoc, ""));
        QCOMPARE(data.eventById(adhoc)->title, QString("Errand"));

        // An activity block still has its Activity — clearing its LABEL is
        // fine (the block falls back to the activity's name).
        QCOMPARE(data.eventById(planned)->title, QString("chapter 3"));
        QVERIFY(data.setEventTitle(planned, ""));
        QCOMPARE(data.eventLabel(*data.eventById(planned)), QString("Study"));
    }

    void removingATaskDemotesItsBlocksToText()
    {
        AppData data;
        const QString cat  = data.addCategory("School", QColor("#4C6FE0"));
        const QString task = data.addTask("Lab 4", cat);

        const QString plain =
            data.addTaskEvent(QDate(2026, 7, 6), 540, 600, task);
        const QString labelled =
            data.addTaskEvent(QDate(2026, 7, 6), 660, 720, task);
        data.setEventTitle(labelled, "final push");

        QVERIFY(data.removeTask(task)); // NOT refused, unlike removeActivity

        // The link is gone, the meaning is not: the title was rescued…
        const Event* p = data.eventById(plain);
        QVERIFY(p->taskId.isEmpty());
        QCOMPARE(p->title, QString("Lab 4"));
        QCOMPARE(data.eventLabel(*p), QString("Lab 4"));
        // …except where the user had already named the block themselves.
        QCOMPARE(data.eventById(labelled)->title, QString("final push"));
    }

    void blockTimeIsAttributedThroughItsIdentity()
    {
        AppData data;
        const QString cat  = data.addCategory("School", QColor("#4C6FE0"));
        const QString task = data.addTask("Lab 4", cat);

        const QString tev = data.addTaskEvent(QDate(2026, 7, 6), 540, 600, task);
        data.appendSegment(tev, makeSegment(SegmentKind::Focus, kT0, 30));

        const QString aev =
            data.addAdHocEvent(QDate(2026, 7, 6), 660, 720, "Errand");
        data.appendSegment(aev, makeSegment(SegmentKind::Focus,
                                            kT0.addSecs(3600), 10));

        const stats::PeriodSummary s =
            stats::summarizeDay(data, QDate(2026, 7, 6));
        // A task block's time lands in the task's life area…
        QCOMPARE(s.byCategory.value(cat), qint64(30 * 60));
        // …ad-hoc time counts in the day's totals but no category bucket —
        // the documented limitation, pinned so it can't silently change.
        QCOMPARE(s.totals.focusSeconds, qint64(40 * 60));
        QCOMPARE(s.byCategory.size(), 1);
    }

    void blockIdentitySurvivesJsonRoundTrip()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("data.json");

        AppData original;
        const QString cat  = original.addCategory("School", QColor("#4C6FE0"));
        const QString task = original.addTask("Lab 4", cat);
        const QString tev  =
            original.addTaskEvent(QDate(2026, 7, 6), 540, 600, task);
        const QString aev  =
            original.addAdHocEvent(QDate(2026, 7, 6), 660, 720, "Errand");
        const QString act  = original.addActivity("Study", cat);
        const QString lev  =
            original.addEvent(QDate(2026, 7, 6), 780, 840, act, "chapter 3");

        QVERIFY(JsonStore(path).save(original));
        AppData loaded;
        QVERIFY(JsonStore(path).load(loaded));

        QCOMPARE(loaded.eventById(tev)->taskId, task);
        QCOMPARE(loaded.eventById(aev)->title, QString("Errand"));
        QCOMPARE(loaded.eventById(lev)->title, QString("chapter 3"));
        QCOMPARE(loaded.eventById(lev)->activityId, act);
    }

    void taskCanBeLinkedToAnActivityBlock()
    {
        AppData data;
        const QString cat1 = data.addCategory("School", QColor("#4C6FE0"));
        const QString cat2 = data.addCategory("Admin",  QColor("#4CA96A"));
        const QString act  = data.addActivity("Study GTI350", cat1);
        const QString task = data.addTask("Lab 4", cat2); // DIFFERENT area
        const QString ev   = data.addEvent(QDate(2026, 7, 6), 540, 600, act);

        QVERIFY(data.setEventTask(ev, task));
        const Event* e = data.eventById(ev);
        QCOMPARE(e->taskId, task);
        // The ACTIVITY stays the block's identity — name and life area both.
        // (Task in another category proves the precedence isn't accidental.)
        QCOMPARE(data.eventLabel(*e), QString("Study GTI350"));
        QCOMPARE(data.eventCategoryId(*e), cat1);

        QVERIFY(data.setEventTask(ev, QString()));  // unlink: fine, the
        QVERIFY(data.eventById(ev)->taskId.isEmpty()); // activity remains
    }

    void taskLinkGuardsItsInvariants()
    {
        AppData data;
        const QString cat  = data.addCategory("School", QColor("#4C6FE0"));
        const QString task = data.addTask("Lab 4", cat);
        const QString ev   = data.addTaskEvent(QDate(2026, 7, 6), 540, 600, task);

        // A link must point at a real Task…
        QVERIFY(!data.setEventTask(ev, "no-such-task"));
        // …and unlinking a task-ONLY block strips its last identity: refused.
        QVERIFY(!data.setEventTask(ev, QString()));
        QCOMPARE(data.eventById(ev)->taskId, task); // untouched by both

        // Give it a title first, and the same unlink becomes legal —
        // the invariant is about the LAST identity, not about tasks.
        QVERIFY(data.setEventTitle(ev, "final push"));
        QVERIFY(data.setEventTask(ev, QString()));
        QCOMPARE(data.eventLabel(*data.eventById(ev)), QString("final push"));
    }

    void adHocTitleSplitsHeadlineFromBody()
    {
        // ONE stored field, TWO derived views: for an ad-hoc block the
        // first line of the title is the headline (eventLabel), the rest is
        // the body (eventBody). The rule people already write by.
        AppData data;
        const QString ev = data.addAdHocEvent(
            QDate(2026, 7, 6), 960, 1080,
            "Rona +\nDon't forget to bring panels and measurement of hose");

        const Event* e = data.eventById(ev);
        QCOMPARE(data.eventLabel(*e), QString("Rona +"));
        QCOMPARE(data.eventBody(*e),
                 QString("Don't forget to bring panels and measurement of hose"));

        // Single-line ad-hoc: the title is ALL headline, no body — nothing
        // gets printed twice.
        const QString ev2 = data.addAdHocEvent(QDate(2026, 7, 6), 540, 600,
                                               "Call the bank");
        QCOMPARE(data.eventLabel(*data.eventById(ev2)),
                 QString("Call the bank"));
        QVERIFY(data.eventBody(*data.eventById(ev2)).isEmpty());
    }

    void activityAndTaskBlocksKeepWholeTitleAsBody()
    {
        // Blocks whose identity comes from elsewhere never used the title
        // as a headline — so the WHOLE title stays body, first line and
        // all. The split applies only where the doubling existed.
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        const QString ev  = data.addEvent(QDate(2026, 7, 6), 540, 660, act);
        QVERIFY(data.setEventTitle(ev, "quiz prep\nbring calculator"));

        const Event* e = data.eventById(ev);
        QCOMPARE(data.eventLabel(*e), QString("Study"));
        QCOMPARE(data.eventBody(*e),
                 QString("quiz prep\nbring calculator"));
    }

    void eventLivenessIsHalfOpenOnThePlannedWindow()
    {
        Event e;
        e.date                = QDate(2026, 7, 6);
        e.plannedStartMinutes = 660;  // 11:00
        e.plannedEndMinutes   = 720;  // 12:00
        const auto at = [](int h, int m) {
            return QDateTime(QDate(2026, 7, 6), QTime(h, m));
        };
        QVERIFY(!e.isLiveAt(at(10, 59)));  // before the window
        QVERIFY( e.isLiveAt(at(11, 0)));   // start is INCLUSIVE
        QVERIFY( e.isLiveAt(at(11, 59)));  // inside
        QVERIFY(!e.isLiveAt(at(12, 0)));   // end is EXCLUSIVE
        QVERIFY(!e.isLiveAt(
            QDateTime(QDate(2026, 7, 7), QTime(11, 30)))); // wrong day
    }

    void trackerRefusesBlocksThatAreNotLive()
    {
        AppData data;
        const QString cat = data.addCategory("Work", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        // Fixed dates + an injected clock: the wall clock plays NO part in
        // the verdict. (First draft used real "today" and an 0–1440 block —
        // refused at the door, because the domain's day runs 06:00–24:00,
        // and it would have gone flaky before 6 AM anyway. The seam killed
        // both problems.)
        const QString liveEv =
            data.addEvent(QDate(2026, 7, 6), 660, 720, act);  // 11–12
        const QString futureEv =
            data.addEvent(QDate(2026, 7, 6), 1020, 1080, act); // 17–18
        QVERIFY(!liveEv.isEmpty());
        QVERIFY(!futureEv.isEmpty());

        TrackerService tracker(&data);
        tracker.nowProvider = [] {
            return QDateTime(QDate(2026, 7, 6), QTime(11, 30)); // it's 11:30
        };
        QVERIFY(tracker.canTrackNow(liveEv));
        QVERIFY(!tracker.canTrackNow(futureEv));

        tracker.startFocus(futureEv); // refused: not live
        QCOMPARE(int(tracker.state()), int(TrackerService::State::Idle));

        tracker.startFocus(liveEv);   // allowed: we're inside its window
        QCOMPARE(int(tracker.state()), int(TrackerService::State::Focusing));

        // A refused start must have ZERO side effects — switching toward
        // the future block must not stop (or steal) the running interval.
        tracker.startBreak(futureEv);
        QCOMPARE(int(tracker.state()), int(TrackerService::State::Focusing));
        QCOMPARE(tracker.trackedEventId(), liveEv);

        tracker.stop(); // stop is NEVER guarded — the truth gets written
        QCOMPARE(int(tracker.state()), int(TrackerService::State::Idle));
    }

    void unaccountedTimeIsElapsedWindowMinusTracked()
    {
        AppData data;
        const QString cat = data.addCategory("Work", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        const QDate yesterday(2026, 7, 5);
        const QDate today(2026, 7, 6);
        const QDateTime now(today, QTime(11, 30)); // the injected clock

        // Yesterday, never tracked: the WHOLE hour is unaccounted. (This
        // is the case the summarize loop's never-tracked early-exit used
        // to skip — pinned here so it can't regress.)
        data.addEvent(yesterday, 540, 600, act);
        // Yesterday, OVER-tracked (90m on a 60m window — legal, §3.38
        // allows running past the end): clamps to zero, never negative.
        const QString ev2 = data.addEvent(yesterday, 660, 720, act);
        data.appendSegment(ev2, makeSegment(SegmentKind::Focus, kT0, 90));

        QCOMPARE(stats::summarizeDay(data, yesterday, now).unaccountedSeconds,
                 qint64(60 * 60));

        // Today, mid-block at 11:30: 10:00–12:00 has 90m elapsed, 40m
        // tracked → 50m unaccounted so far.
        const QString ev3 = data.addEvent(today, 600, 720, act);
        data.appendSegment(ev3, makeSegment(SegmentKind::Focus,
                                            kT0.addSecs(7200), 40));
        // Today, future block (17:00): zero — nothing has elapsed.
        data.addEvent(today, 1020, 1080, act);

        QCOMPARE(stats::summarizeDay(data, today, now).unaccountedSeconds,
                 qint64(50 * 60));
    }

    // ---- compare — two planners, one summarizer (design-addendum-share) ----

    void compareDeltaIsMineMinusTheirs()
    {
        // Two SEPARATE AppData instances — the whole point of the feature is
        // that stats::summarizeDay doesn't care whose data it reads. `mine`
        // stands in for the live planner, `theirs` for a blob that arrived
        // over the wire; the code path is identical from here on.
        AppData mine, theirs;
        const QDate day(2026, 7, 6);

        const QString mc = mine.addCategory("Work", QColor("#4C6FE0"));
        const QString ma = mine.addActivity("Study", mc);
        const QString me = mine.addEvent(day, 540, 720, ma);
        mine.appendSegment(me, makeSegment(SegmentKind::Focus, kT0, 60));
        mine.appendSegment(me, makeSegment(SegmentKind::Break,
                                           kT0.addSecs(3600), 10));

        const QString tc = theirs.addCategory("Health", QColor("#4CAF50"));
        const QString ta = theirs.addActivity("Walk", tc);
        const QString te = theirs.addEvent(day, 540, 720, ta);
        theirs.appendSegment(te, makeSegment(SegmentKind::Focus, kT0, 25));
        theirs.appendSegment(te, makeSegment(SegmentKind::Distracted,
                                             kT0.addSecs(1500), 15));

        const compare::Delta d = compare::delta(
            stats::summarizeDay(mine, day).totals,
            stats::summarizeDay(theirs, day).totals);

        // Signed, MINE minus THEIRS — the sign convention every display
        // in CompareDialog leans on, pinned once here.
        QCOMPARE(d.focusSeconds,      qint64((60 - 25) * 60));
        QCOMPARE(d.breakSeconds,      qint64(10 * 60));
        QCOMPARE(d.distractedSeconds, qint64(-15 * 60));
        QCOMPARE(d.totalSeconds,      qint64((70 - 40) * 60));
    }

    void compareFocusVerdictHasATolerance()
    {
        // The one real decision in Compare.h: small leads are Even, not
        // Ahead — this feature nudges, it doesn't rank. Test the boundary
        // EXACTLY: the default tolerance is 5 minutes, inclusive.
        compare::Delta d;

        d.focusSeconds = 0;
        QCOMPARE(int(compare::focusVerdict(d)),
                 int(compare::Verdict::Even));
        d.focusSeconds = 5 * 60;              // exactly on the line: Even
        QCOMPARE(int(compare::focusVerdict(d)),
                 int(compare::Verdict::Even));
        d.focusSeconds = 5 * 60 + 1;          // one second past: Ahead
        QCOMPARE(int(compare::focusVerdict(d)),
                 int(compare::Verdict::Ahead));
        d.focusSeconds = -(5 * 60 + 1);       // mirrored: Behind
        QCOMPARE(int(compare::focusVerdict(d)),
                 int(compare::Verdict::Behind));

        // The tolerance is a PARAMETER, not a constant baked into the maths:
        d.focusSeconds = 10 * 60;
        QCOMPARE(int(compare::focusVerdict(d, /*tolerance=*/15 * 60)),
                 int(compare::Verdict::Even));
    }

    // ---- version — semver + the banner rule (design-addendum-update) ------

    void semverParsesStrictlyAndComparesNumerically()
    {
        // Valid shapes parse; anything else is .valid == false — never a
        // guess, because an updater built on guessed versions nags people.
        QVERIFY(version::parse("19.0.0").valid);
        QVERIFY(version::parse(" 19.0.1 ").valid); // whitespace forgiven
        QVERIFY(!version::parse("19.0").valid);    // two fields isn't semver
        QVERIFY(!version::parse("v19.0.0").valid); // no prefixes
        QVERIFY(!version::parse("19.0.x").valid);
        QVERIFY(!version::parse("").valid);

        // THE trap this design exists to avoid: as strings,
        // "18.10.0" < "18.9.0" (because '1' < '9'). Numerically, 10 > 9.
        QVERIFY(version::isNewer("18.10.0", "18.9.0"));
        QVERIFY(!version::isNewer("18.9.0", "18.10.0"));

        // Field precedence: major beats minor beats patch.
        QVERIFY(version::isNewer("19.0.0", "18.99.99"));
        QVERIFY(version::isNewer("18.1.0", "18.0.99"));
        QVERIFY(!version::isNewer("19.0.0", "19.0.0")); // equal isn't newer

        // Fail closed: garbage on either side means "not newer".
        QVERIFY(!version::isNewer("banana", "19.0.0"));
        QVERIFY(!version::isNewer("20.0.0", "banana"));
    }

    void updateBannerRuleIsNewerAndNotDismissed()
    {
        using version::Banner;
        using version::decideBanner;

        // The whole feature's judgement as a truth table.
        // Newer + never dismissed → speak.
        QCOMPARE(int(decideBanner("19.0.0", "20.0.0", "")),
                 int(Banner::Show));
        // Same or older → silence (no "you're up to date!" popups).
        QCOMPARE(int(decideBanner("19.0.0", "19.0.0", "")),
                 int(Banner::Silent));
        QCOMPARE(int(decideBanner("19.0.0", "18.0.0", "")),
                 int(Banner::Silent));
        // Dismissed THIS one → stays dismissed forever.
        QCOMPARE(int(decideBanner("19.0.0", "20.0.0", "20.0.0")),
                 int(Banner::Silent));
        // ...but a NEWER release than the dismissed one speaks again:
        // dismissal means "not this one", not "never talk to me".
        QCOMPARE(int(decideBanner("19.0.0", "20.0.1", "20.0.0")),
                 int(Banner::Show));
        // Garbage from the wire → silence, always.
        QCOMPARE(int(decideBanner("19.0.0", "newest!!", "")),
                 int(Banner::Silent));
    }

    // ---- v7: archive, priority, honest tracking, editable days ------------

    void archivedThingsVanishFromEveryListButSurviveTheFile()
    {
        AppData data;
        const QString c = data.addCategory("School", QColor("#4C6FE0"));
        const QString a = data.addActivity("Old course", c);
        const QString t = data.addTask("Old lab", c, QDate(2026, 8, 1));

        QVERIFY(data.setActivityArchived(a, true));
        QVERIFY(data.setTaskArchived(t, true));

        // Gone from the living views…
        QVERIFY(data.upcomingTasks().isEmpty());
        QVERIFY(data.tasksDueOn(QDate(2026, 8, 1)).isEmpty());
        // …present in the archive's own queries…
        QCOMPARE(data.archivedTasks().size(), 1);
        QCOMPARE(data.archivedActivities().size(), 1);

        // …and the flags SURVIVE the disk (the whole point: hide, never
        // forget). Round-trip through the same JSON path sync uses.
        AppData reloaded;
        JsonStore::applyJsonObject(reloaded, JsonStore::toJsonObject(data),
                                   false);
        QCOMPARE(reloaded.archivedTasks().size(), 1);
        QCOMPARE(reloaded.archivedActivities().size(), 1);

        // Restore is one flag flip away — archive is a door, not a grave.
        QVERIFY(reloaded.setTaskArchived(t, false));
        QCOMPARE(reloaded.upcomingTasks().size(), 1);
    }

    void archivingALifeAreaHidesItsWholeWorldReversibly()
    {
        // The owner's semester story: one category holds a term's classes;
        // term ends, the AREA retires. The cascade rule: hidden = own flag
        // OR the owning category's — children keep their flags untouched,
        // so restore is exact.
        AppData data;
        const QString c = data.addCategory("Fall 2026", QColor("#4C6FE0"));
        const QString t = data.addTask("PHY335 lab", c, QDate(2026, 9, 1));

        QVERIFY(data.setCategoryArchived(c, true));
        // The task never got its own archived flag…
        QVERIFY(data.archivedTasks().isEmpty());
        // …yet it's hidden from every living view (the cascade).
        QVERIFY(data.upcomingTasks().isEmpty());
        QVERIFY(data.tasksDueOn(QDate(2026, 9, 1)).isEmpty());
        QCOMPARE(data.archivedCategories().size(), 1);

        // Survives the disk (v8 field round-trips)…
        AppData reloaded;
        JsonStore::applyJsonObject(reloaded, JsonStore::toJsonObject(data),
                                   false);
        QVERIFY(reloaded.upcomingTasks().isEmpty());

        // …and one flip brings the whole world back, exactly as it was.
        QVERIFY(reloaded.setCategoryArchived(c, false));
        QCOMPARE(reloaded.upcomingTasks().size(), 1);
        QCOMPARE(reloaded.upcomingTasks().first()->title,
                 QStringLiteral("PHY335 lab"));
    }

    void taskPriorityDefaultsToMediumAndRoundTrips()
    {
        AppData data;
        const QString c = data.addCategory("School", QColor("#4C6FE0"));
        const QString t = data.addTask("Lab", c, QDate(2026, 8, 1));

        // Born Medium — an unranked task is ordinary, not urgent.
        QCOMPARE(int(data.upcomingTasks().first()->priority),
                 int(Task::Priority::Medium));

        QVERIFY(data.setTaskPriority(t, Task::Priority::Urgent));
        AppData reloaded;
        JsonStore::applyJsonObject(reloaded, JsonStore::toJsonObject(data),
                                   false);
        QCOMPARE(int(reloaded.upcomingTasks().first()->priority),
                 int(Task::Priority::Urgent));

        // The string mapping fails SAFE: unknown text reads as Medium —
        // a v6 file (no priority key at all) must load as all-ordinary.
        QCOMPARE(int(priorityFromString("critical!!")),
                 int(Task::Priority::Medium));
        QCOMPARE(int(priorityFromString("")),
                 int(Task::Priority::Medium));
    }

    void removeSegmentRetractsExactlyOneFact()
    {
        AppData data;
        const QString c = data.addCategory("Work", QColor("#4C6FE0"));
        const QString a = data.addActivity("Study", c);
        const QString e = data.addEvent(QDate(2026, 7, 6), 540, 720, a);
        data.appendSegment(e, makeSegment(SegmentKind::Focus, kT0, 30));
        data.appendSegment(e, makeSegment(SegmentKind::Break,
                                          kT0.addSecs(1800), 10));

        // Out-of-range is REFUSED, never clamped — a retraction must name
        // exactly the fact it retracts.
        QVERIFY(!data.removeSegment(e, 2));
        QVERIFY(!data.removeSegment(e, -1));
        QVERIFY(!data.removeSegment("no-such-event", 0));

        QVERIFY(data.removeSegment(e, 0)); // the focus segment, by position
        const stats::Totals totals =
            stats::summarizeDay(data, QDate(2026, 7, 6)).totals;
        QCOMPARE(totals.focusSeconds, qint64(0));       // retracted
        QCOMPARE(totals.breakSeconds, qint64(10 * 60)); // untouched
    }

    void specialDayEditKeepsBirthRulesAndColorRoundTrips()
    {
        AppData data;
        const QString id =
            data.addSpecialDay("Birthday", QDate(2026, 3, 14), true);

        // Same birth rules on edit: no empty names, no invalid dates.
        QVERIFY(!data.updateSpecialDay(id, "  ", QDate(2026, 3, 14), true,
                                       QColor()));
        QVERIFY(!data.updateSpecialDay(id, "Birthday", QDate(), true,
                                       QColor()));

        QVERIFY(data.updateSpecialDay(id, "Maman's birthday",
                                      QDate(2026, 3, 15), true,
                                      QColor("#D4589C")));
        AppData reloaded;
        JsonStore::applyJsonObject(reloaded, JsonStore::toJsonObject(data),
                                   false);
        QCOMPARE(reloaded.specialDays().first().title,
                 QStringLiteral("Maman's birthday"));
        QCOMPARE(reloaded.specialDays().first().color, QColor("#D4589C"));

        // Invalid colour = "back to automatic", and it round-trips as
        // absence — the same trick as the TBD due date.
        QVERIFY(data.updateSpecialDay(id, "Maman's birthday",
                                      QDate(2026, 3, 15), true, QColor()));
        AppData again;
        JsonStore::applyJsonObject(again, JsonStore::toJsonObject(data),
                                   false);
        QVERIFY(!again.specialDays().first().color.isValid());
    }

    // ---- needs-a-block, part 1 (design-addendum-needs-a-block) ------------
    // The whole feature's brain is pure (coverage::, ReturnPolicy), so the
    // whole feature is provable here — headless, fixed dates, milliseconds.
    // House convention: every date below is pinned; `kToday` is a Tuesday.

    void coverageDeadlineClampKeepsOverdueTasksSatisfiable()
    {
        // §A consequence 3: deadline = max(due, today). A task due LAST
        // WEEK is covered by a block TODAY — without the clamp it could
        // never be covered by anything placeable and would nag forever.
        const QDate today(2026, 7, 21);
        Task t;
        t.dueDate  = today.addDays(-7);
        t.priority = Task::Priority::Medium;

        QCOMPARE(coverage::deadlineOf(t, today), today);
        QVERIFY(coverage::isCovered(t, {today}, today));
        QVERIFY(!coverage::isCovered(t, {today.addDays(1)}, today));
        // No due date = no upper bound: any today-or-later block covers.
        t.dueDate = QDate();
        QVERIFY(!coverage::deadlineOf(t, today).isValid());
        QVERIFY(coverage::isCovered(t, {today.addDays(30)}, today));
    }

    void lateAndPastBlocksDoNotCover()
    {
        // §A consequences 1 and 2: a block AFTER the deadline looks like
        // coverage and isn't; a block already SPENT did not do the job.
        const QDate today(2026, 7, 21);
        Task t;
        t.dueDate = today.addDays(1); // due tomorrow

        QVERIFY(!coverage::isCovered(t, {today.addDays(2)}, today)); // late
        QVERIFY(!coverage::isCovered(t, {today.addDays(-1)}, today)); // past
        QVERIFY(coverage::isCovered(t, {today.addDays(1)}, today));  // on it
        QVERIFY(coverage::isCovered(
            t, {today.addDays(-1), today.addDays(1)}, today)); // one is enough
    }

    void needsBlockPriorityRuleCatchesDatelessUrgent()
    {
        // §B condition 1: the priority set flags whatever the date says —
        // including a task with NO date, which the window can never reach.
        const QDateTime now(QDate(2026, 7, 21), QTime(8, 30));
        coverage::Rule rule; // defaults: urgent only, 3-day window
        Task t;
        t.priority = Task::Priority::Urgent; // and dueDate stays invalid

        QVERIFY(coverage::needsBlock(t, /*covered=*/false, rule, now));
        t.priority = Task::Priority::Low;    // dateless AND unflagged rank
        QVERIFY(!coverage::needsBlock(t, false, rule, now));
        t.priority = Task::Priority::Urgent; // flagged again…
        QVERIFY(!coverage::needsBlock(t, /*covered=*/true, rule, now));
        t.done = true;                       // …but never when finished
        QVERIFY(!coverage::needsBlock(t, false, rule, now));
        t.done = false;
        t.archived = true;                   // …or shelved
        QVERIFY(!coverage::needsBlock(t, false, rule, now));
    }

    void needsBlockDueWindowRuleAndOverdue()
    {
        // §B condition 2: an ordinary task inside the window is flagged;
        // outside it, not; window off, never; OVERDUE always, window or no.
        const QDateTime now(QDate(2026, 7, 21), QTime(8, 30));
        coverage::Rule rule; // 3-day window
        Task t;
        t.priority = Task::Priority::Medium; // priority rule can't fire

        t.dueDate = now.date().addDays(2);
        QVERIFY(coverage::needsBlock(t, false, rule, now));
        t.dueDate = now.date().addDays(3);           // boundary: inclusive
        QVERIFY(coverage::needsBlock(t, false, rule, now));
        t.dueDate = now.date().addDays(4);
        QVERIFY(!coverage::needsBlock(t, false, rule, now));

        rule.dueWithinDays = 0;                      // window OFF
        t.dueDate = now.date().addDays(1);
        QVERIFY(!coverage::needsBlock(t, false, rule, now));
        t.dueDate = now.date().addDays(-2);          // …but overdue is not
        QVERIFY(coverage::needsBlock(t, false, rule, now)); // a window fact
    }

    void staleDismissalCannotHideATask()
    {
        // §C: the flag compares dismissedUntil against `now` directly —
        // expiry housekeeping is a nicety, never load-bearing.
        const QDateTime now(QDate(2026, 7, 21), QTime(8, 30));
        coverage::Rule rule;
        Task t;
        t.priority = Task::Priority::Urgent;

        t.dismissedUntil = now.addSecs(3600);        // live -> hidden
        QVERIFY(!coverage::needsBlock(t, false, rule, now));
        t.dismissedUntil = now.addSecs(-3600);       // lapsed -> visible,
        QVERIFY(coverage::needsBlock(t, false, rule, now)); // no cleanup ran
    }

    void returnPolicyComputesAllThreeModes()
    {
        // ReturnPolicy.h — one nextReturn, three modes, every boundary.
        const QDateTime at(QDate(2026, 7, 21), QTime(8, 30));
        ReturnPolicy p;

        p.mode = ReturnPolicy::Mode::EndOfDay;       // midnight tonight
        QCOMPARE(p.nextReturn(at),
                 QDateTime(QDate(2026, 7, 22), QTime(0, 0)));

        p.mode = ReturnPolicy::Mode::AtTime;
        p.time = QTime(21, 0);                       // still ahead -> today
        QCOMPARE(p.nextReturn(at),
                 QDateTime(QDate(2026, 7, 21), QTime(21, 0)));
        p.time = QTime(6, 0);                        // passed -> tomorrow
        QCOMPARE(p.nextReturn(at),
                 QDateTime(QDate(2026, 7, 22), QTime(6, 0)));
        // The knife edge: dismissing AT 21:00 sharp returns tomorrow, not
        // instantly — otherwise the button is a no-op once a day.
        p.time = QTime(8, 30);
        QCOMPARE(p.nextReturn(at),
                 QDateTime(QDate(2026, 7, 22), QTime(8, 30)));

        p.mode  = ReturnPolicy::Mode::AfterHours;
        p.hours = 4;
        QCOMPARE(p.nextReturn(at),
                 QDateTime(QDate(2026, 7, 21), QTime(12, 30)));
        p.hours = 0;                                 // repair: min 1 hour
        QCOMPARE(p.nextReturn(at),
                 QDateTime(QDate(2026, 7, 21), QTime(9, 30)));
    }

    void dismissDoorCountsAndRefusesForever()
    {
        // §C/§D doors: each dismissal is one count; an invalid `until`
        // ("dismissed forever") is refused; bring-back clears the hide but
        // never the history.
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4A7CC4"));
        const QString id  = data.addTask("Lab 4", cat);
        const QDateTime nine(QDate(2026, 7, 21), QTime(21, 0));

        QVERIFY(!data.dismissTask(id, QDateTime()));       // refused
        QCOMPARE(data.taskById(id)->dismissCount, 0);

        QVERIFY(data.dismissTask(id, nine));
        QVERIFY(data.dismissTask(id, nine.addDays(1)));
        QCOMPARE(data.taskById(id)->dismissCount, 2);
        QCOMPARE(data.taskById(id)->dismissedUntil, nine.addDays(1));

        QVERIFY(data.clearDismissal(id));                  // bring back
        QVERIFY(!data.taskById(id)->dismissedUntil.isValid());
        QCOMPARE(data.taskById(id)->dismissCount, 2);      // history stays
    }

    void completionResetsTheEvidence()
    {
        // §D, the owner's call: finishing the task zeroes the count and
        // clears any live dismissal; un-finishing restores neither.
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4A7CC4"));
        const QString id  = data.addTask("Lab 4", cat);
        data.dismissTask(id, QDateTime(QDate(2026, 7, 21), QTime(21, 0)));
        data.dismissTask(id, QDateTime(QDate(2026, 7, 22), QTime(21, 0)));

        QVERIFY(data.setTaskDone(id, true));
        QCOMPARE(data.taskById(id)->dismissCount, 0);
        QVERIFY(!data.taskById(id)->dismissedUntil.isValid());
        QVERIFY(data.setTaskDone(id, false));              // correction,
        QCOMPARE(data.taskById(id)->dismissCount, 0);      // not resurrection
    }

    void repeatSuccessorStartsWithCleanEvidence()
    {
        // §D: the spawned next occurrence is a FRESH task — it inherits
        // the rule, never the put-off history.
        AppData data;
        const QString cat = data.addCategory("Home", QColor("#B0679A"));
        const QString id  = data.addTask("Rent", cat, QDate(2026, 7, 28));
        data.updateTask(id, "Rent", "", QDate(2026, 7, 28), QTime(),
                        Task::Repeat::Monthly);
        data.dismissTask(id, QDateTime(QDate(2026, 7, 21), QTime(21, 0)));

        QVERIFY(data.setTaskDone(id, true));               // spawns August
        const Task* next = nullptr;
        for (const Task& t : data.tasks())
            if (t.id != id)
                next = &t;
        QVERIFY(next);
        QCOMPARE(next->dismissCount, 0);
        QVERIFY(!next->dismissedUntil.isValid());
    }

    void expireDismissalsClearsOnlyTheLapsed()
    {
        // §C housekeeping, rollRepeats-style: one pass, returns the count,
        // leaves live dismissals alone.
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4A7CC4"));
        const QString a = data.addTask("A", cat);
        const QString b = data.addTask("B", cat);
        const QDateTime now(QDate(2026, 7, 21), QTime(21, 0));
        data.dismissTask(a, now.addSecs(-60));             // lapsed
        data.dismissTask(b, now.addSecs(+60));             // still live

        QCOMPARE(data.expireDismissals(now), 1);
        QVERIFY(!data.taskById(a)->dismissedUntil.isValid());
        QVERIFY(data.taskById(b)->dismissedUntil.isValid());
        QCOMPARE(data.expireDismissals(now), 0);           // idempotent
    }

    void tasksNeedingBlockFiltersCoversAndSorts()
    {
        // The one derived list (§B/§F): covered tasks drop out, the rest
        // arrive pinned-overdue-urgent-rest, ties by soonest due date.
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4A7CC4"));
        const QDateTime now(QDate(2026, 7, 21), QTime(8, 30));
        const QDate today = now.date();

        const QString overdue = data.addTask("Overdue", cat, today.addDays(-2));
        const QString urgent  = data.addTask("Urgent", cat, today.addDays(3));
        data.setTaskPriority(urgent, Task::Priority::Urgent);
        const QString soon    = data.addTask("Soon", cat, today.addDays(2));
        const QString covered = data.addTask("Covered", cat, today.addDays(2));
        data.addTaskEvent(today.addDays(1), 9 * 60, 10 * 60, covered);
        const QString pinnedT = data.addTask("Pinned", cat, today.addDays(5));
        data.setTaskPriority(pinnedT, Task::Priority::Urgent);
        for (int i = 0; i < 6; ++i)                        // rung 2 evidence
            data.dismissTask(pinnedT, now.addSecs(60));
        data.clearDismissal(pinnedT);                      // visible again
        data.addTask("Quiet", cat, today.addDays(30));     // outside window

        const auto list = data.tasksNeedingBlock(
            coverage::Rule{}, coverage::Escalation{}, now);
        QCOMPARE(list.size(), 4);
        QCOMPARE(list[0]->title, QStringLiteral("Pinned"));  // rung 2 first
        QCOMPARE(list[1]->title, QStringLiteral("Overdue")); // facts next
        QCOMPARE(list[2]->title, QStringLiteral("Urgent"));  // opinions
        QCOMPARE(list[3]->title, QStringLiteral("Soon"));    // window catch
    }

    void uncoveredReasonNamesTheFailure()
    {
        // §A explainability: when the app flags a task the user believes
        // is handled, it must say WHICH clause fired.
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4A7CC4"));
        const QDate today(2026, 7, 21);

        const QString bare = data.addTask("Bare", cat, today.addDays(1));
        QCOMPARE(data.taskUncoveredReason(bare, today),
                 coverage::Reason::NoBlock);

        const QString late = data.addTask("Late", cat, today.addDays(1));
        data.addTaskEvent(today.addDays(2), 13 * 60, 14 * 60, late);
        QCOMPARE(data.taskUncoveredReason(late, today),
                 coverage::Reason::BlockAfterDeadline);

        const QString past = data.addTask("Past", cat, today.addDays(1));
        data.addTaskEvent(today.addDays(-1), 10 * 60, 11 * 60, past);
        QCOMPARE(data.taskUncoveredReason(past, today),
                 coverage::Reason::BlockInPast);

        const QString ok = data.addTask("Ok", cat, today.addDays(1));
        data.addTaskEvent(today.addDays(1), 9 * 60, 10 * 60, ok);
        QCOMPARE(data.taskUncoveredReason(ok, today),
                 coverage::Reason::None);
    }

    void rungIsDerivedAndRespondsToSettings()
    {
        // §D: the rung is a pure function of (count, settings) — change
        // the threshold and every task re-rungs with nothing to migrate.
        coverage::Escalation esc;                          // 3 / +3 / urgent
        Task t;
        t.priority     = Task::Priority::Urgent;
        t.dismissCount = 2;
        QCOMPARE(coverage::rung(t, esc), 0);
        t.dismissCount = 3;
        QCOMPARE(coverage::rung(t, esc), 1);
        t.dismissCount = 6;
        QCOMPARE(coverage::rung(t, esc), 2);

        t.priority = Task::Priority::Medium;               // urgent-only:
        QCOMPARE(coverage::rung(t, esc), 0);               // ladder ignores
        esc.urgentOnly = false;
        QCOMPARE(coverage::rung(t, esc), 2);               // …until told not to

        esc.urgentOnly    = true;
        t.priority        = Task::Priority::Urgent;
        esc.decisionAfter = 10;                            // "migration"
        QCOMPARE(coverage::rung(t, esc), 0);               // is instant
    }

    void dismissalFieldsRoundTripAndOldFilesReadClean()
    {
        // §G: two additive keys, version 10; a file WITHOUT them (any v9
        // task) reads as never-dismissed with zero special-casing.
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4A7CC4"));
        const QString id  = data.addTask("Lab 4", cat, QDate(2026, 7, 24));
        data.dismissTask(id, QDateTime(QDate(2026, 7, 21), QTime(21, 0)));
        data.dismissTask(id, QDateTime(QDate(2026, 7, 22), QTime(21, 0)));

        AppData loaded;
        JsonStore::applyJsonObject(loaded, JsonStore::toJsonObject(data),
                                   false);
        QCOMPARE(loaded.taskById(id)->dismissCount, 2);
        QCOMPARE(loaded.taskById(id)->dismissedUntil,
                 QDateTime(QDate(2026, 7, 22), QTime(21, 0)));

        // Forge a pre-v10 task object: no dismissal keys at all.
        QJsonObject root = JsonStore::toJsonObject(AppData());
        root["tasks"] = QJsonArray{QJsonObject{
            {"id", "old1"}, {"title", "From v9"}, {"categoryId", ""},
            {"done", false}, {"dueDate", ""}}};
        AppData old;
        JsonStore::applyJsonObject(old, root, false);
        QCOMPARE(old.taskById("old1")->dismissCount, 0);
        QVERIFY(!old.taskById("old1")->dismissedUntil.isValid());
    }

    // ---- v22: the deadline's clock half ----------------------------------

    // The pairing invariant, proved at all three doors: a time only exists
    // alongside a date, and clearing the date takes the time with it.
    void dueTimeNeverOrphansItsDate()
    {
        AppData data;
        const QString cat = data.addCategory("School", QColor("#2F7E6E"));

        // Birth door: a time with no date is refused, silently and totally.
        const QString orphan = data.addTask("No date", cat, QDate(),
                                            QTime(17, 0));
        QVERIFY(!data.taskById(orphan)->dueTime.isValid());

        // Birth door with a date: the time survives.
        const QString id = data.addTask("Lab 4", cat, QDate(2026, 8, 8),
                                        QTime(23, 59));
        QCOMPARE(data.taskById(id)->dueTime, QTime(23, 59));

        // Date setter: clearing the date clears the clock.
        QVERIFY(data.setTaskDueDate(id, QDate(), QTime(9, 0)));
        QVERIFY(!data.taskById(id)->dueTime.isValid());

        // Coarse edit: same rule, third enforcer.
        QVERIFY(data.updateTask(id, "Lab 4", "", QDate(), QTime(9, 0),
                                Task::Repeat::None));
        QVERIFY(!data.taskById(id)->dueTime.isValid());
    }

    // An all-day task is due at the END of its day, not the start — the
    // single most likely off-by-a-day bug in the whole feature.
    void allDayDeadlineIsEndOfDay()
    {
        Task t;
        t.dueDate = QDate(2026, 8, 8);
        QCOMPARE(t.dueMoment(), QDateTime(QDate(2026, 8, 8), QTime(23, 59, 59)));
        QVERIFY(!t.isOverdue(QDateTime(QDate(2026, 8, 8), QTime(0, 1))));
        QVERIFY(!t.isOverdue(QDateTime(QDate(2026, 8, 8), QTime(23, 0))));
        QVERIFY(t.isOverdue(QDateTime(QDate(2026, 8, 9), QTime(0, 1))));

        t.dueTime = QTime(9, 0);
        QVERIFY(!t.isOverdue(QDateTime(QDate(2026, 8, 8), QTime(8, 59))));
        QVERIFY(t.isOverdue(QDateTime(QDate(2026, 8, 8), QTime(9, 1))));
        // The DATE-only overload must stay date-only: it is still correct for
        // every caller that reasons in whole days, and quietly making it
        // time-aware would change answers all over the calendar.
        QVERIFY(!t.isOverdue(QDate(2026, 8, 8)));
    }

    // A same-day deadline that has already lapsed must flag NOW, not at
    // midnight — the needs-a-block rule's one time-aware branch.
    void needsBlockSeesTheClockOnTheDueDay()
    {
        Task t;
        t.priority = Task::Priority::Low;   // priority alone won't flag it
        t.dueDate  = QDate(2026, 7, 15);
        t.dueTime  = QTime(9, 0);
        coverage::Rule rule;                // urgent-only, 3-day window
        rule.dueWithinDays = 0;             // window off: only the clock can flag

        const QDateTime before(QDate(2026, 7, 15), QTime(8, 30));
        const QDateTime after(QDate(2026, 7, 15), QTime(9, 30));
        QVERIFY(!coverage::needsBlock(t, false, rule, before));
        QVERIFY(coverage::needsBlock(t, false, rule, after));

        // Untimed tasks are untouched by that branch — the guarantee that
        // every pre-v22 expectation still holds.
        t.dueTime = QTime();
        QVERIFY(!coverage::needsBlock(t, false, rule, after));
    }

    // The time is part of the habit: completing a repeating task carries it.
    void repeatCarriesTheDeadlineTime()
    {
        AppData data;
        const QString cat = data.addCategory("Life", QColor("#2F7E6E"));
        const QString id = data.addTask("Rent", cat, QDate(2026, 7, 1),
                                        QTime(9, 0));
        data.updateTask(id, "Rent", "", QDate(2026, 7, 1), QTime(9, 0),
                        Task::Repeat::Monthly);
        QVERIFY(data.setTaskDone(id, true));

        const Task* spawned = nullptr;
        for (const Task& t : data.tasks())
            if (!t.done && t.title == "Rent")
                spawned = &t;
        QVERIFY(spawned);
        QCOMPARE(spawned->dueDate, QDate(2026, 8, 1));
        QCOMPARE(spawned->dueTime, QTime(9, 0));
    }

    // Storage: the time round-trips, and a file that predates it still loads.
    void dueTimeSurvivesTheRoundTrip()
    {
        AppData data;
        const QString cat = data.addCategory("School", QColor("#2F7E6E"));
        data.addTask("Timed", cat, QDate(2026, 8, 8), QTime(23, 59));
        data.addTask("All day", cat, QDate(2026, 8, 9));

        AppData loaded;
        JsonStore::applyJsonObject(loaded, JsonStore::toJsonObject(data),
                                   false);
        const Task* timed = nullptr;
        const Task* allDay = nullptr;
        for (const Task& t : loaded.tasks())
            (t.title == "Timed" ? timed : allDay) = &t;
        QVERIFY(timed && allDay);
        QCOMPARE(timed->dueTime, QTime(23, 59));
        QVERIFY(!allDay->dueTime.isValid()); // "" round-trips as all-day

        // A pre-v22 task object: the key simply is not there.
        QJsonObject root = JsonStore::toJsonObject(AppData());
        root["tasks"] = QJsonArray{QJsonObject{
            {"id", "old1"}, {"title", "From v21"}, {"categoryId", ""},
            {"done", false}, {"dueDate", "2026-08-08"}}};
        AppData old;
        JsonStore::applyJsonObject(old, root, false);
        QVERIFY(!old.taskById("old1")->dueTime.isValid());
    }

    // v22.8: the org-name move recovery. v22.7 named the organization and
    // thereby moved AppDataLocation one level deeper — the owner opened an
    // empty folder and believed their data erased. The migration rule that
    // brings it home is tested here on temp dirs: every data*.json is
    // COPIED (old folder intact as backup), and a lived-in destination is
    // never overwritten.
    void migrationCarriesEveryPlannerFileAndOverwritesNothing()
    {
        QTemporaryDir oldHome, newHome;
        QVERIFY(oldHome.isValid() && newHome.isValid());
        const QDir from(oldHome.path()), to(newHome.path());

        auto write = [](const QString& path, const QByteArray& body) {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(body);
        };
        write(from.filePath("data.json"),       "{\"v\":\"global\"}");
        write(from.filePath("data-alice.json"), "{\"v\":\"alice\"}");
        write(from.filePath("notes.txt"),       "not a planner file");
        // The destination already owns a bob file — migration must not touch it.
        write(to.filePath("data-bob.json"),     "{\"v\":\"bob-new\"}");
        write(from.filePath("data-bob.json"),   "{\"v\":\"bob-OLD\"}");

        QVERIFY(JsonStore::migrateDataFiles(from, to));

        auto read = [](const QString& path) {
            QFile f(path);
            // Checked, not because the migration could have failed silently
            // (QCOMPARE below would scream about empty bytes) but because
            // QFile::open is [[nodiscard]] on newer Qt — the owner's 6.11
            // flagged the unchecked call my 6.4 let slide. A stricter
            // compiler is a free reviewer; don't argue with it.
            if (!f.open(QIODevice::ReadOnly))
                return QByteArray();
            return f.readAll();
        };
        QCOMPARE(read(to.filePath("data.json")),       QByteArray("{\"v\":\"global\"}"));
        QCOMPARE(read(to.filePath("data-alice.json")), QByteArray("{\"v\":\"alice\"}"));
        QCOMPARE(read(to.filePath("data-bob.json")),   QByteArray("{\"v\":\"bob-new\"}"));
        QVERIFY(!QFile::exists(to.filePath("notes.txt"))); // planner files only
        // COPY, not move: the originals are all still there.
        QVERIFY(QFile::exists(from.filePath("data.json")));
        QVERIFY(QFile::exists(from.filePath("data-alice.json")));
    }
    // ---- v25: the day briefing --------------------------------------------
    // brief:: is what the assistant KNOWS. Tested here (not test_nlp) because
    // it reads AppData and stats:: — the domain side of the two-pure-layers
    // split. Time is a parameter throughout, per the nowProvider doctrine.

    // The core promises in one scene: a past block, a current block, a future
    // block, each labelled relative to `now`; tracked time carried per block;
    // the day totals present.
    void briefingLabelsBlocksRelativeToNow()
    {
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        const QDate day(2026, 7, 19);
        const QString past = data.addEvent(day, 540, 600, act);   // 9–10
        data.addEvent(day, 660, 720, act);                        // 11–12
        data.addEvent(day, 900, 960, act);                        // 15–16
        data.appendSegment(past, makeSegment(SegmentKind::Focus,
                                             QDateTime(day, QTime(9, 0)), 25));

        const QDateTime now(day, QTime(11, 30)); // inside the middle block
        const QString b = brief::dayBriefing(data, day, now);

        QVERIFY(b.contains(QStringLiteral("2026-07-19")));
        QVERIFY(b.contains(QStringLiteral("PLAN FOR TODAY (3 blocks)")));
        QVERIFY(b.contains(QStringLiteral("09:00-10:00 Study [past]")));
        QVERIFY(b.contains(QStringLiteral("11:00-12:00 Study [NOW]")));
        QVERIFY(b.contains(QStringLiteral("15:00-16:00 Study [upcoming]")));
        QVERIFY(b.contains(QStringLiteral("tracked 25m")));
        QVERIFY(b.contains(QStringLiteral("25m focused")));
        QVERIFY(b.contains(QStringLiteral("area: School")));
        QVERIFY(b.contains(QStringLiteral("LIFE AREAS: School")));
    }

    // Rule 1 of the anti-hallucination list: an empty section SAYS SO.
    // Silence invites the model to fill it.
    void briefingStatesEmptinessOutLoud()
    {
        AppData data;
        const QString b = brief::dayBriefing(data, QDate(2026, 7, 19),
                                             QDateTime(QDate(2026, 7, 19),
                                                       QTime(10, 0)));
        QVERIFY(b.contains(QStringLiteral("nothing planned")));
        QVERIFY(b.contains(QStringLiteral("nothing due in the next 7 days")));
        // v28.10: tomorrow is a section now, and its emptiness is stated
        // too — with "yet", because an unplanned tomorrow is normal, not
        // a failure.
        QVERIFY(b.contains(
            QStringLiteral("PLAN FOR TOMORROW (2026-07-20): nothing "
                           "planned yet")));
    }

    // ---- v28.10: the field-report fixes -----------------------------------
    // Three facts the first real day of use proved the model cannot infer
    // and must be TOLD: where the day stands, what tomorrow holds, and how
    // the two tracked numbers relate. Each was a briefing-content gap, not
    // a prompt problem — the context is the product.

    // Field #3: mid-day, the phase is stated — remaining count and the
    // day's last end — so "did my day end?" is a lookup, not arithmetic.
    void briefingStatesDayPhaseWhileRunning()
    {
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        const QDate day(2026, 7, 19);
        data.addEvent(day, 540, 600, act);  // 9–10
        data.addEvent(day, 660, 720, act);  // 11–12
        data.addEvent(day, 900, 960, act);  // 15–16

        const QString b = brief::dayBriefing(
            data, day, QDateTime(day, QTime(11, 30)));

        QVERIFY(b.contains(QStringLiteral(
            "DAY STATUS: 2 of 3 planned blocks still ahead or running; "
            "the last block ends at 16:00")));
    }

    // Field #3, the case that actually bit: evening, everything done —
    // "the day is over" is a computed FACT, not something the model
    // deduces from timestamps (§A: models have no clock).
    void briefingStatesDayPhaseWhenOver()
    {
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        const QDate day(2026, 7, 19);
        data.addEvent(day, 540, 600, act);  // 9–10
        data.addEvent(day, 900, 960, act);  // 15–16

        const QString b = brief::dayBriefing(
            data, day, QDateTime(day, QTime(22, 0)));

        QVERIFY(b.contains(QStringLiteral(
            "DAY STATUS: the planned day is OVER — the last block ended "
            "at 16:00")));
        // Field #4's companion: the totals line names its relationship to
        // the per-block figures instead of leaving the model to reconcile
        // two numbers by arithmetic.
        QVERIFY(b.contains(QStringLiteral("TRACKED TODAY (day totals")));
    }

    // ---- v29.0: the write boundary (Slice 1, model-less) -------------------
    // The §B machine, pinned before any model exists to drive it: closed
    // per-role verbs, fail-safe handles, the additive gate, and the
    // re-validate-at-tap rule. Every test here exercises the exact code
    // path a model proposal will take in Slice 2 — the model becomes a new
    // caller of an old, guarded pipeline.

    // §B.4 made checkable: the whole allow-list in four asserts. If a verb
    // is ever added to a phrasing role, this test is the tripwire.
    void verbsAreScopedPerRole()
    {
        QCOMPARE(verbs::verbsFor(verbs::Role::Intake),
                 QVector<verbs::Verb>{ verbs::Verb::SetTaskDetails });
        QVERIFY(verbs::verbsFor(verbs::Role::Chat).isEmpty());
        QVERIFY(verbs::verbsFor(verbs::Role::Nudge).isEmpty());
        QVERIFY(verbs::verbsFor(verbs::Role::CheckIn).isEmpty());
    }

    // §B.2's promise, mechanically: real handles round-trip, a task seen
    // twice keeps one handle, and every invention fails to "" — never to
    // the wrong task.
    void handleMapRoundTripsAndFailsSafe()
    {
        verbs::HandleMap map;
        QCOMPARE(map.addTask(QStringLiteral("id-a")), QStringLiteral("T1"));
        QCOMPARE(map.addTask(QStringLiteral("id-b")), QStringLiteral("T2"));
        QCOMPARE(map.addTask(QStringLiteral("id-a")),
                 QStringLiteral("T1")); // dedup: one task, one name

        QCOMPARE(map.taskIdFor(QStringLiteral("T2")), QStringLiteral("id-b"));
        QVERIFY(map.taskIdFor(QStringLiteral("T7")).isEmpty());  // invented
        QVERIFY(map.taskIdFor(QStringLiteral("T0")).isEmpty());  // off by one
        QVERIFY(map.taskIdFor(QStringLiteral("B1")).isEmpty());  // wrong kind
        QVERIFY(map.taskIdFor(QStringLiteral("T")).isEmpty());   // malformed
        QVERIFY(map.taskIdFor(QString()).isEmpty());             // empty
    }

    // The block namespace (v29.2) carries every §B.2 property the task one
    // does — and the two must not be able to answer for each other, which is
    // the whole reason they are separate vectors rather than one counter.
    void blockHandlesAreTheirOwnNamespaceAndFailSafe()
    {
        verbs::HandleMap map;
        QCOMPARE(map.addTask(QStringLiteral("task-1")), QStringLiteral("T1"));
        QCOMPARE(map.addBlock(QStringLiteral("block-1")), QStringLiteral("B1"));
        QCOMPARE(map.addBlock(QStringLiteral("block-2")), QStringLiteral("B2"));
        QCOMPARE(map.addBlock(QStringLiteral("block-1")),
                 QStringLiteral("B1")); // dedup holds here too

        QCOMPARE(map.blockIdFor(QStringLiteral("B2")),
                 QStringLiteral("block-2"));

        // The counters are independent: T1 and B1 coexist and name different
        // things. One shared counter would have made B1 into B2 here.
        QCOMPARE(map.taskIdFor(QStringLiteral("T1")), QStringLiteral("task-1"));
        QCOMPARE(map.blockIdFor(QStringLiteral("B1")),
                 QStringLiteral("block-1"));

        // Neither namespace answers for the other — the ambiguity §B.2 exists
        // to prevent, pinned in both directions.
        QVERIFY(map.blockIdFor(QStringLiteral("T1")).isEmpty());
        QVERIFY(map.taskIdFor(QStringLiteral("B1")).isEmpty());

        // Same fail-safe ladder as tasks.
        QVERIFY(map.blockIdFor(QStringLiteral("B9")).isEmpty());  // invented
        QVERIFY(map.blockIdFor(QStringLiteral("B0")).isEmpty());  // off by one
        QVERIFY(map.blockIdFor(QStringLiteral("b1")).isEmpty());  // not fuzzy
        QVERIFY(map.blockIdFor(QStringLiteral("B")).isEmpty());   // malformed
        QVERIFY(map.blockIdFor(QString()).isEmpty());             // empty
    }

    // The briefing prints what it registers: [T1] appears in the text, the
    // out-map resolves it to the real id, the NEEDS DETAILS section lists
    // exactly the unsized open tasks — and stays silent when there are
    // none (the MOOD manners: an empty header invites speculation).
    void briefingRegistersHandlesAndQueuesUnsized()
    {
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QDate today(2026, 7, 19);
        const QString unsized = data.addTask("Lab 4", cat, today);
        const QString sized   = data.addTask("Quiz prep", cat, today);
        data.setTaskSize(sized, 90, true);

        verbs::HandleMap handles;
        const QString b =
            brief::dayBriefing(data, today, QDateTime(today, QTime(10, 0)),
                               brief::Options{}, &handles);

        QVERIFY(b.contains(QStringLiteral("[T1]")));
        QVERIFY(b.contains(
            QStringLiteral("NEEDS DETAILS — captured but never sized (1)")));
        // The queue names the unsized task and not the sized one; resolve
        // the queue line's handle and it lands on the right task.
        // Scope to THIS section only: the briefing continues after it
        // (needs-a-block, mood…) and those sections legitimately name
        // other tasks — "Quiz prep" appears later as needing a block,
        // which is true and none of this test's business.
        const int at  = b.indexOf(QStringLiteral("NEEDS DETAILS"));
        const int end = b.indexOf(QStringLiteral("\n\n"), at);
        const QString queue = b.mid(at, end > at ? end - at : -1);
        QVERIFY(queue.contains(QStringLiteral("Lab 4")));
        QVERIFY(!queue.contains(QStringLiteral("Quiz prep")));
        QVERIFY(!handles.isEmpty());
        bool resolved = false;
        for (int i = 1; i <= handles.taskIds.size(); ++i)
            if (handles.taskIdFor(QStringLiteral("T%1").arg(i)) == unsized)
                resolved = true;
        QVERIFY(resolved);

        // Size everything → the section vanishes entirely.
        data.setTaskSize(unsized, 60, true);
        const QString b2 =
            brief::dayBriefing(data, today, QDateTime(today, QTime(10, 0)));
        QVERIFY(!b2.contains(QStringLiteral("NEEDS DETAILS")));
    }

    // The gate, refusal by refusal — each with a reason a card can print.
    void validateEnforcesTheBoundary()
    {
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QDate today(2026, 7, 19);
        const QString open  = data.addTask("Lab 4", cat, today);
        const QString shut  = data.addTask("Old one", cat, today);
        data.setTaskDone(shut, true);

        verbs::HandleMap handles;
        const QString hOpen = handles.addTask(open);
        const QString hShut = handles.addTask(shut);

        verbs::Proposal p;
        p.targetHandle    = hOpen;
        p.estimateMinutes = 120;

        // A phrasing role holding a perfectly valid proposal: still no.
        QVERIFY(!verbs::validate(data, handles, verbs::Role::Nudge, p).ok);

        // Intake may — this is the one allowed (role, verb) pair.
        QVERIFY(verbs::validate(data, handles, verbs::Role::Intake, p).ok);

        verbs::Proposal bad = p;
        bad.targetHandle = QStringLiteral("T9"); // invented handle
        QVERIFY(!verbs::validate(data, handles, verbs::Role::Intake, bad).ok);

        bad = p;
        bad.targetHandle = hShut; // closed target: history, not a blank
        QVERIFY(!verbs::validate(data, handles, verbs::Role::Intake, bad).ok);

        bad = p;
        bad.estimateMinutes = 0; // nothing proposed at all
        QVERIFY(!verbs::validate(data, handles, verbs::Role::Intake, bad).ok);

        // The additive rule: a filled field is not a blank.
        data.setTaskSize(open, 60, true);
        QVERIFY(!verbs::validate(data, handles, verbs::Role::Intake, p).ok);
    }

    // apply() re-validates AT THE TAP and funnels through existing doors:
    // the happy path fills both blanks (preserving chunkable), and a world
    // that changed while the card sat there is refused, not overwritten.
    void applyRevalidatesAndFillsBlanksOnly()
    {
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QDate today(2026, 7, 19);
        const QString id = data.addTask("Lab 4", cat); // undated, unsized

        verbs::HandleMap handles;
        verbs::Proposal p;
        p.targetHandle    = handles.addTask(id);
        p.estimateMinutes = 150;
        p.dueDate         = today.addDays(3);

        // Preservation is the claim — whatever chunkable was, it stays.
        // (Pinning the default's VALUE here would make this test break on
        // an unrelated default change, which is a lie about what it
        // defends.)
        const bool wasChunkable = data.taskById(id)->chunkable;

        QVERIFY(verbs::apply(data, handles, verbs::Role::Intake, p).ok);
        const Task* t = data.taskById(id);
        QCOMPARE(t->estimateMinutes, 150);
        QCOMPARE(t->dueDate, today.addDays(3));
        QCOMPARE(t->chunkable, wasChunkable); // preserved, not decided
        QVERIFY(!t->dueTime.isValid()); // the proposal's honest grain

        // The stale-card scene: owner fills a field by hand after the card
        // rendered; the tap must refuse. (Fresh task, card composed, then
        // the by-hand edit lands first.)
        const QString id2 = data.addTask("Essay", cat);
        verbs::Proposal p2;
        p2.targetHandle    = handles.addTask(id2);
        p2.estimateMinutes = 60;
        QVERIFY(verbs::validate(data, handles, verbs::Role::Intake, p2).ok);
        data.setTaskSize(id2, 45, true); // the world changes
        const verbs::Verdict late =
            verbs::apply(data, handles, verbs::Role::Intake, p2);
        QVERIFY(!late.ok);
        QCOMPARE(data.taskById(id2)->estimateMinutes, 45); // untouched
    }

    // ---- v29.1: the interview's brain (all C++) ----------------------------

    // §K.3's guess: two finished samples minimum, the MEDIAN of tracked
    // actuals, and a basis string that shows its work. One loud outlier
    // must not become the guess — that is the median's whole job here.
    void historyGuessTakesTheMedianAndShowsItsWork()
    {
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QDate day(2026, 7, 6);

        // One finished task with tracked time: a story, not a pattern.
        const QString t1 = data.addTask("Old lab", cat);
        const QString e1 = data.addTaskEvent(day, 540, 700, t1);
        data.appendSegment(e1, makeSegment(SegmentKind::Focus, kT0, 120));
        data.setTaskDone(t1, true);

        const QString probe = data.addTask("Lab 4", cat);
        QVERIFY(!intake::historyGuess(data, *data.taskById(probe)).exists());

        // A second sample and an outlier third: 120m, 90m, 600m → median
        // 120m, and the basis names the area and the count.
        const QString t2 = data.addTask("Old essay", cat);
        const QString e2 = data.addTaskEvent(day.addDays(1), 540, 640, t2);
        data.appendSegment(e2, makeSegment(SegmentKind::Focus, kT0, 90));
        data.setTaskDone(t2, true);
        const QString t3 = data.addTask("The disaster", cat);
        const QString e3 = data.addTaskEvent(day.addDays(2), 480, 1120, t3);
        data.appendSegment(e3, makeSegment(SegmentKind::Focus, kT0, 600));
        data.setTaskDone(t3, true);

        const intake::Guess g =
            intake::historyGuess(data, *data.taskById(probe));
        QCOMPARE(g.minutes, 120); // the median, not the disaster's mean
        QVERIFY(g.basis.contains(QStringLiteral("3 finished School")));
        QVERIFY(g.basis.contains(QStringLiteral("2h")));
    }

    // §K.6's gate, signal by signal — and the ask-once door in force.
    void worthInterviewingTriagesOnSubstance()
    {
        AppData data;
        const QString cat = data.addCategory("Chores", QColor("#888888"));
        const QDateTime now(QDate(2026, 7, 19), QTime(10, 0));

        const QString milk = data.addTask("Buy milk", cat); // undated,
                                                            // medium, no
                                                            // history
        QVERIFY(!intake::worthInterviewing(data, *data.taskById(milk), now));

        const QString dated = data.addTask("Lab 4", cat, now.date().addDays(9));
        QVERIFY(intake::worthInterviewing(data, *data.taskById(dated), now));

        const QString urgent = data.addTask("Fix the leak", cat);
        data.setTaskPriority(urgent, Task::Priority::Urgent);
        QVERIFY(intake::worthInterviewing(data, *data.taskById(urgent), now));

        // Sized → nothing left to ask.
        data.setTaskSize(dated, 120, true);
        QVERIFY(!intake::worthInterviewing(data, *data.taskById(dated), now));

        // Skip = ask once: a live dismissal silences the interview, and a
        // lapsed one does not (compared against now itself, no
        // housekeeping required).
        data.dismissTask(urgent, now.addDays(365));
        QVERIFY(!intake::worthInterviewing(data, *data.taskById(urgent), now));
        QVERIFY(intake::worthInterviewing(data, *data.taskById(urgent),
                                          now.addDays(366)));
    }

    // §K.2 + §K.3: the question is C++, and it folds the guess in when
    // one exists — a nod or a correction, never a blank page.
    void questionFoldsTheGuessWhenHistoryExists()
    {
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QString cold = data.addTask("Lab 4", cat);
        const QString q1 = intake::questionFor(data, *data.taskById(cold));
        QVERIFY(q1.contains(QStringLiteral("Lab 4")));
        QVERIFY(q1.contains(QStringLiteral("how long")));
        QVERIFY(!q1.contains(QStringLiteral("sound right")));

        const QDate day(2026, 7, 6);
        for (int i = 0; i < 2; ++i) {
            const QString t = data.addTask(QString("Old %1").arg(i), cat);
            const QString e = data.addTaskEvent(day.addDays(i), 540, 700, t);
            data.appendSegment(e, makeSegment(SegmentKind::Focus, kT0, 120));
            data.setTaskDone(t, true);
        }
        const QString q2 = intake::questionFor(data, *data.taskById(cold));
        QVERIFY(q2.contains(QStringLiteral("sound right")));
        QVERIFY(q2.contains(QStringLiteral("2h")));
    }

    // The crisp parser: everything it should read, and — more important —
    // everything it must REFUSE, because a cheap parse that plucks
    // numbers out of prose would pre-empt the model that understands it.
    void durationParserReadsCrispAndRefusesProse()
    {
        QCOMPARE(intake::parseDurationAnswer("2h"), 120);
        QCOMPARE(intake::parseDurationAnswer("90m"), 90);
        QCOMPARE(intake::parseDurationAnswer("1h 30m"), 90);
        QCOMPARE(intake::parseDurationAnswer("1h30"), 90);
        QCOMPARE(intake::parseDurationAnswer("90 min"), 90);
        QCOMPARE(intake::parseDurationAnswer("  2 Hours "), 120);
        QCOMPARE(intake::parseDurationAnswer("90"), 90);

        QCOMPARE(intake::parseDurationAnswer("probably 2h if Marc shows"), 0);
        QCOMPARE(intake::parseDurationAnswer("two evenings"), 0);
        QCOMPARE(intake::parseDurationAnswer("2024"), 0); // a year, not a span
        QCOMPARE(intake::parseDurationAnswer("1.5h"), 0); // model's problem
        QCOMPARE(intake::parseDurationAnswer(""), 0);
        QCOMPARE(intake::parseDurationAnswer("0"), 0);
    }

    // Field #2: tomorrow's blocks enter the context — times, label, area,
    // and the date stated in ISO in the header so "tomorrow" can be echoed
    // back unambiguously. No [past]/[NOW] tags: a future block has no
    // phase, and empty columns invite invention.
    void briefingCarriesTomorrowsBlocks()
    {
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        const QDate day(2026, 7, 19);
        data.addEvent(day.addDays(1), 540, 630, act); // tomorrow 9–10:30

        const QString b = brief::dayBriefing(
            data, day, QDateTime(day, QTime(10, 0)));

        QVERIFY(b.contains(
            QStringLiteral("PLAN FOR TOMORROW (2026-07-20, 1 blocks)")));
        QVERIFY(b.contains(QStringLiteral("09:00-10:30 Study, area: School")));
        QVERIFY(!b.contains(QStringLiteral("09:00-10:30 Study [")));
    }

    // The partition reuses upcomingTasks(), so app and assistant can never
    // disagree about what "upcoming" means; done tasks vanish, undated tasks
    // become a count, and the horizon is honoured.
    void briefingPartitionsTasksByUrgency()
    {
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QDate today(2026, 7, 19);
        data.addTask("Old lab", cat, today.addDays(-2));
        data.addTask("Quiz prep", cat, today);
        data.addTask("Essay", cat, today.addDays(3));
        // Sized on purpose (v29.0): this fixture isolates the URGENCY
        // partition, and an unsized task now legitimately surfaces in the
        // NEEDS DETAILS section regardless of horizon — the queue is
        // about missing facts, not dates. Sizing it keeps "beyond the
        // horizon means invisible" a true statement about the due
        // sections, which is what this test defends.
        const QString far =
            data.addTask("Far away", cat, today.addDays(30)); // beyond horizon
        data.setTaskSize(far, 120, true);
        data.addTask("Someday", cat);                     // undated
        const QString done = data.addTask("Done one", cat, today);
        data.setTaskDone(done, true);

        const QString b = brief::dayBriefing(
            data, today, QDateTime(today, QTime(10, 0)));

        QVERIFY(b.contains(QStringLiteral("OVERDUE (1)")));
        QVERIFY(b.contains(QStringLiteral("Old lab")));
        QVERIFY(b.contains(QStringLiteral("DUE TODAY (1)")));
        QVERIFY(b.contains(QStringLiteral("Quiz prep (due today")));
        QVERIFY(b.contains(QStringLiteral("DUE IN THE NEXT 7 DAYS (1)")));
        QVERIFY(b.contains(QStringLiteral("Essay (due 2026-07-22")));
        QVERIFY(!b.contains(QStringLiteral("Far away"))); // beyond horizon
        QVERIFY(!b.contains(QStringLiteral("Done one"))); // finished = gone
        QVERIFY(b.contains(
            QStringLiteral("Plus 1 open task(s) with no date set")));
    }

    // Rule 2: a truncated list is VISIBLY truncated. Options is a parameter
    // precisely so this can be proven with three tasks instead of eleven.
    void briefingCapsAreStatedNotSilent()
    {
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QDate today(2026, 7, 19);
        data.addTask("A", cat, today);
        data.addTask("B", cat, today);
        data.addTask("C", cat, today);

        brief::Options opts;
        opts.maxTasks = 2;
        const QString b = brief::dayBriefing(
            data, today, QDateTime(today, QTime(10, 0)), opts);

        QVERIFY(b.contains(QStringLiteral("DUE TODAY (3)"))); // true count
        QVERIFY(b.contains(QStringLiteral("(+1 more)")));     // honest cut
    }

    // Rule 3 and 4: no ids, no notes. The briefing is the FEATURE'S privacy
    // page in executable form — if someone later leaks a description into
    // it, this is the test that turns the leak into a red bar.
    void briefingLeaksNoIdsAndNoDescriptions()
    {
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QString id = data.addTask("Lab 4", cat, QDate(2026, 7, 20));
        data.updateTask(id, "Lab 4", "SECRET private notes",
                        QDate(2026, 7, 20), QTime(), Task::Repeat::None);

        const QString b = brief::dayBriefing(
            data, QDate(2026, 7, 19),
            QDateTime(QDate(2026, 7, 19), QTime(10, 0)));

        QVERIFY(b.contains(QStringLiteral("Lab 4")));
        QVERIFY(!b.contains(id));                       // ids are plumbing
        QVERIFY(!b.contains(QStringLiteral("SECRET"))); // notes stay home
    }
    // ======================================================================
    // v26.2 — catch-up: missed blocks and the reschedule proposer
    // ======================================================================
    //
    // The whole point of putting missed:: and reschedule:: in namespaces of
    // pure functions is that these cases need no AppData, no widgets, no
    // clock and no disk. Events are built inline as plain structs; `now` is
    // an argument. Every case below runs in microseconds.

    // A block is judged on FOCUS time, not on whether the timer was touched.
    // Break and Distracted time are real, and deliberately don't count: a
    // block full of procrastination must not pass as done.
    void missedJudgesOnFocusTimeOnly()
    {
        const QDateTime now(QDate(2026, 7, 20), QTime(18, 0));
        missed::Rule rule; // 50%, 7 days

        Event e;
        e.id = "e1";
        e.date = QDate(2026, 7, 20);
        e.plannedStartMinutes = 9 * 60;   // 09:00–10:30, 90 minutes
        e.plannedEndMinutes   = 10 * 60 + 30;

        // Nothing tracked at all.
        QCOMPARE(missed::judge(e, rule, now).reason,
                 missed::Reason::NeverStarted);

        // An hour of BREAK is still nothing done.
        e.segments.append(makeSegment(
            SegmentKind::Break, QDateTime(QDate(2026, 7, 20), QTime(9, 0)), 60));
        QCOMPARE(missed::judge(e, rule, now).reason,
                 missed::Reason::NeverStarted);

        // 30 minutes of focus out of 90 is 33% — under the bar.
        e.segments.append(makeSegment(
            SegmentKind::Focus, QDateTime(QDate(2026, 7, 20), QTime(9, 0)), 30));
        const missed::Verdict partial = missed::judge(e, rule, now);
        QCOMPARE(partial.reason, missed::Reason::Partial);
        QCOMPARE(partial.percent(), 33);
        QCOMPARE(partial.shortfallSeconds(), qint64(60 * 60)); // 60 min owed

        // Another 20 gets it to 55% — over the bar, no longer a failure.
        e.segments.append(makeSegment(
            SegmentKind::Focus, QDateTime(QDate(2026, 7, 20), QTime(10, 0)), 20));
        QCOMPARE(missed::judge(e, rule, now).reason, missed::Reason::None);
    }

    // Half-open windows, the same convention as Event::isLiveAt: at 10:30 a
    // 9:00–10:30 block is over. One minute earlier it is still running and
    // must never be called missed.
    void missedNeverJudgesARunningBlock()
    {
        missed::Rule rule;
        Event e;
        e.date = QDate(2026, 7, 20);
        e.plannedStartMinutes = 9 * 60;
        e.plannedEndMinutes   = 10 * 60 + 30;

        const QDateTime during(QDate(2026, 7, 20), QTime(10, 29));
        const QDateTime after (QDate(2026, 7, 20), QTime(10, 30));

        QVERIFY(!missed::hasEnded(e, during));
        QCOMPARE(missed::judge(e, rule, during).reason, missed::Reason::None);
        QVERIFY(missed::hasEnded(e, after));
        QCOMPARE(missed::judge(e, rule, after).reason,
                 missed::Reason::NeverStarted);
    }

    // The horizon is what stops a fortnight away turning into a wall of
    // guilt nobody triages. The block still FAILED — judge() says so — it is
    // just no longer surfaced.
    void missedHorizonRetiresOldFailures()
    {
        const QDateTime now(QDate(2026, 7, 20), QTime(9, 0));
        missed::Rule rule; // lookBackDays = 7

        Event old;
        old.date = QDate(2026, 7, 1);
        old.plannedStartMinutes = 9 * 60;
        old.plannedEndMinutes   = 10 * 60;

        QCOMPARE(missed::judge(old, rule, now).reason,
                 missed::Reason::NeverStarted);   // it did fail
        QVERIFY(!missed::isUnresolved(old, rule, now)); // it is not our problem
    }

    // A stored decision removes the block from the surfaced set — but does
    // NOT change the verdict. The two questions stay separable so the
    // evening review can still say what happened.
    void missedRespectsTheStoredDecision()
    {
        const QDateTime now(QDate(2026, 7, 20), QTime(18, 0));
        missed::Rule rule;

        Event e;
        e.date = QDate(2026, 7, 20);
        e.plannedStartMinutes = 9 * 60;
        e.plannedEndMinutes   = 10 * 60;

        QVERIFY(missed::isUnresolved(e, rule, now));

        e.outcome = BlockOutcome::Dropped;
        QVERIFY(!missed::isUnresolved(e, rule, now));
        QCOMPARE(missed::judge(e, rule, now).reason,
                 missed::Reason::NeverStarted); // unchanged: time is time
    }

    // The proposer prefers the block's original time of day. A 07:00 gym
    // block must not be offered at 22:00 just because that gap was scanned.
    void reschedulePrefersTheOriginalTimeOfDay()
    {
        reschedule::Context ctx;
        ctx.now = QDateTime(QDate(2026, 7, 20), QTime(9, 0));
        ctx.deadline = QDate(2026, 7, 22);
        ctx.dayStartMinutes = 6 * 60;
        ctx.dayEndMinutes   = 24 * 60;

        Event block;
        block.id = "gym";
        block.date = QDate(2026, 7, 20);
        block.plannedStartMinutes = 7 * 60;   // 07:00–08:00
        block.plannedEndMinutes   = 8 * 60;

        QVector<Event> events{block};
        const missed::Verdict v = missed::judge(block, missed::Rule(), ctx.now);

        const auto options = reschedule::propose(block, v, events, ctx);
        QVERIFY(!options.isEmpty());
        QCOMPARE(options.first().kind, reschedule::Kind::FreeSlot);

        // Tomorrow is wide open, so the offer is tomorrow at the same hour —
        // NOT today at 09:00, which is also free but a day earlier... and
        // that is the deliberate ordering: earliest day wins first.
        const reschedule::Piece p = options.first().pieces.first();
        QCOMPARE(p.date, QDate(2026, 7, 20));  // today still has room
        QCOMPARE(p.startMinutes, 9 * 60);      // 07:00 has passed; 09:00 is
                                               // the closest legal start
    }

    // Today's remaining time starts at the next SLOT LINE, never "three
    // minutes from now".
    void rescheduleNeverProposesTheImmediatePast()
    {
        reschedule::Context ctx;
        ctx.now = QDateTime(QDate(2026, 7, 20), QTime(9, 3));

        const auto free = reschedule::freeOn(QDate(2026, 7, 20), {}, ctx);
        QVERIFY(!free.isEmpty());
        QCOMPARE(free.first().startMinutes, 9 * 60 + 30); // snapped up
    }

    // The quietly most valuable offer: a full week is almost never
    // CONTIGUOUSLY full. Two 30-minute gaps cover a 60-minute debt.
    void rescheduleSplitsAcrossFragments()
    {
        reschedule::Context ctx;
        ctx.now = QDateTime(QDate(2026, 7, 20), QTime(6, 0));
        ctx.deadline = QDate(2026, 7, 20);
        ctx.dayStartMinutes = 9 * 60;
        ctx.dayEndMinutes   = 12 * 60;   // a three-hour working day

        Event block;
        block.id = "study";
        block.date = QDate(2026, 7, 19);      // yesterday, 60 minutes missed
        block.plannedStartMinutes = 9 * 60;
        block.plannedEndMinutes   = 10 * 60;

        // Today: 09:00-09:30 free, 09:30-11:00 busy, 11:00-11:30 free,
        // 11:30-12:00 busy. Two 30-minute gaps, nothing bigger.
        Event a; a.id = "a"; a.date = QDate(2026, 7, 20);
        a.plannedStartMinutes = 9 * 60 + 30; a.plannedEndMinutes = 11 * 60;
        Event b; b.id = "b"; b.date = QDate(2026, 7, 20);
        b.plannedStartMinutes = 11 * 60 + 30; b.plannedEndMinutes = 12 * 60;

        QVector<Event> events{block, a, b};
        const missed::Verdict v = missed::judge(block, missed::Rule(), ctx.now);

        const auto options = reschedule::propose(block, v, events, ctx);
        QVERIFY(!options.isEmpty());
        QCOMPARE(options.first().kind, reschedule::Kind::Split);
        QCOMPARE(options.first().pieces.size(), 2);
        QCOMPARE(options.first().recoveredSeconds, qint64(60 * 60));
        QVERIFY(options.first().isComplete(v.shortfallSeconds()));
    }

    // A partial block owes only its REMAINDER. Re-offering the full duration
    // would double-book time the user has already spent.
    void rescheduleOnlyOwesTheShortfall()
    {
        reschedule::Context ctx;
        ctx.now = QDateTime(QDate(2026, 7, 20), QTime(6, 0));

        Event block;
        block.id = "lab";
        block.date = QDate(2026, 7, 19);
        block.plannedStartMinutes = 9 * 60;      // 90 minutes planned
        block.plannedEndMinutes   = 10 * 60 + 30;
        block.segments.append(makeSegment(
            SegmentKind::Focus, QDateTime(QDate(2026, 7, 19), QTime(9, 0)), 30));

        const missed::Verdict v = missed::judge(block, missed::Rule(), ctx.now);
        QCOMPARE(v.reason, missed::Reason::Partial);

        const auto options = reschedule::propose(block, v, {block}, ctx);
        QVERIFY(!options.isEmpty());
        const reschedule::Piece p = options.first().pieces.first();
        QCOMPARE(p.endMinutes - p.startMinutes, 60); // the 60 still owed
    }

    // When the week is genuinely full, the honest answer is a short list of
    // things to give up — never a silently crammed block.
    void rescheduleOffersBumpCandidatesWhenFull()
    {
        reschedule::Context ctx;
        ctx.now = QDateTime(QDate(2026, 7, 20), QTime(6, 0));
        ctx.deadline = QDate(2026, 7, 20);
        ctx.dayStartMinutes = 9 * 60;
        ctx.dayEndMinutes   = 11 * 60;   // a two-hour day, wall to wall

        Event block;
        block.id = "study";
        block.date = QDate(2026, 7, 19);
        block.plannedStartMinutes = 9 * 60;
        block.plannedEndMinutes   = 10 * 60;

        Event full; full.id = "meeting"; full.date = QDate(2026, 7, 20);
        full.plannedStartMinutes = 9 * 60; full.plannedEndMinutes = 11 * 60;

        QVector<Event> events{block, full};
        const missed::Verdict v = missed::judge(block, missed::Rule(), ctx.now);

        const auto options = reschedule::propose(block, v, events, ctx);
        QVERIFY(!options.isEmpty());
        QCOMPARE(options.first().kind, reschedule::Kind::Bump);
        QCOMPARE(options.first().bumpEventId, QStringLiteral("meeting"));
    }

    // A block someone has already worked is not a bump candidate: taking a
    // slot you are halfway through is a loss, not a swap.
    //
    // With every rung of the ladder exhausted — no gap, no fragments, no
    // bumpable block, and nothing free past the deadline either — the list
    // comes back EMPTY. That is a real answer this module is allowed to
    // give, and the surface is expected to say so plainly rather than invent
    // a placement.
    void rescheduleRefusesToBumpWorkedTimeAndAdmitsDefeat()
    {
        reschedule::Context ctx;
        ctx.now = QDateTime(QDate(2026, 7, 20), QTime(6, 0));
        ctx.deadline = QDate(2026, 7, 20);
        ctx.dayStartMinutes = 9 * 60;
        ctx.dayEndMinutes   = 11 * 60;
        ctx.horizonDays     = 1;   // look exactly one day past the deadline

        Event block;
        block.id = "study";
        block.date = QDate(2026, 7, 19);
        block.plannedStartMinutes = 9 * 60;
        block.plannedEndMinutes   = 10 * 60;

        // Today: full, and already worked, so not bumpable.
        Event worked; worked.id = "worked"; worked.date = QDate(2026, 7, 20);
        worked.plannedStartMinutes = 9 * 60; worked.plannedEndMinutes = 11 * 60;
        worked.segments.append(makeSegment(
            SegmentKind::Focus, QDateTime(QDate(2026, 7, 20), QTime(9, 0)), 20));

        // Tomorrow: full too, so there is no slip-the-deadline offer either.
        Event next; next.id = "next"; next.date = QDate(2026, 7, 21);
        next.plannedStartMinutes = 9 * 60; next.plannedEndMinutes = 11 * 60;

        const missed::Verdict v = missed::judge(block, missed::Rule(), ctx.now);
        const auto options =
            reschedule::propose(block, v, {block, worked, next}, ctx);
        QVERIFY(options.isEmpty());
    }

    // The deadline is sometimes the wrong constraint — but only say so when
    // there is genuinely room on the other side of it.
    void rescheduleOffersToSlipTheDeadlineWhenRoomExistsBeyondIt()
    {
        reschedule::Context ctx;
        ctx.now = QDateTime(QDate(2026, 7, 20), QTime(6, 0));
        ctx.deadline = QDate(2026, 7, 20);
        ctx.dayStartMinutes = 9 * 60;
        ctx.dayEndMinutes   = 11 * 60;
        ctx.horizonDays = 7;

        Event block;
        block.id = "study";
        block.date = QDate(2026, 7, 19);
        block.plannedStartMinutes = 9 * 60;
        block.plannedEndMinutes   = 10 * 60;

        // Today is wall-to-wall AND already worked, so no free slot, no
        // bump. Tomorrow (past the deadline) is empty.
        Event full; full.id = "meeting"; full.date = QDate(2026, 7, 20);
        full.plannedStartMinutes = 9 * 60; full.plannedEndMinutes = 11 * 60;
        full.segments.append(makeSegment(
            SegmentKind::Focus, QDateTime(QDate(2026, 7, 20), QTime(9, 0)), 30));

        const missed::Verdict v = missed::judge(block, missed::Rule(), ctx.now);
        const auto options = reschedule::propose(block, v, {block, full}, ctx);

        QVERIFY(!options.isEmpty());
        QCOMPARE(options.last().kind, reschedule::Kind::BeyondDeadline);
        QCOMPARE(options.last().pieces.first().date, QDate(2026, 7, 21));
    }

    // ---- the doors ---------------------------------------------------------

    // rescheduleBlock copies IDENTITY, never segments — time you spent
    // belongs to the day you spent it — and links the two blocks one way.
    void rescheduleBlockCopiesIdentityNotHistory()
    {
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        const QString oldId =
            data.addEvent(QDate(2026, 7, 19), 9 * 60, 10 * 60, act, "chapter 4");
        QVERIFY(!oldId.isEmpty());
        data.appendSegment(oldId, makeSegment(
            SegmentKind::Focus, QDateTime(QDate(2026, 7, 19), QTime(9, 0)), 10));
        data.setEventNote(oldId, "was too tired");

        const QString newId =
            data.rescheduleBlock(oldId, QDate(2026, 7, 21), 9 * 60, 10 * 60);
        QVERIFY(!newId.isEmpty());

        const Event* fresh = data.eventById(newId);
        const Event* old   = data.eventById(oldId);
        QVERIFY(fresh && old);

        QCOMPARE(fresh->activityId, act);
        QCOMPARE(fresh->title, QStringLiteral("chapter 4"));
        QCOMPARE(fresh->note, QStringLiteral("was too tired"));
        QVERIFY(fresh->segments.isEmpty());        // history does not travel
        QCOMPARE(fresh->outcome, BlockOutcome::Unset);

        QCOMPARE(old->outcome, BlockOutcome::Moved);
        QCOMPARE(old->movedToId, newId);
        QCOMPARE(old->segments.size(), 1);         // the old day keeps its 10 min
    }

    // ---- undoReschedule: the inverse §B.1 needs (v29.2) --------------------
    // The round trip. Undo removes the replacement and returns the original
    // to unresolved — the state before the move, with its own history intact.
    void undoRescheduleRestoresTheOriginalAndRemovesTheReplacement()
    {
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        const QString oldId =
            data.addEvent(QDate(2026, 7, 19), 9 * 60, 10 * 60, act, "chapter 4");
        data.appendSegment(oldId, makeSegment(
            SegmentKind::Focus, QDateTime(QDate(2026, 7, 19), QTime(9, 0)), 10));

        const QString newId =
            data.rescheduleBlock(oldId, QDate(2026, 7, 21), 9 * 60, 10 * 60);
        QVERIFY(!newId.isEmpty());

        QVERIFY(data.undoReschedule(oldId));

        QVERIFY(!data.eventById(newId));               // replacement gone
        const Event* old = data.eventById(oldId);
        QVERIFY(old);
        QCOMPARE(old->outcome, BlockOutcome::Unset);   // unresolved again
        QVERIFY(old->movedToId.isEmpty());             // no dangling link
        QCOMPARE(old->segments.size(), 1);             // its own time survives
    }

    // THE refusal that protects a fact rather than a pointer. Once real time
    // is tracked against the replacement, that time exists nowhere else —
    // rescheduleBlock never copied it — so undoing would erase a fact to tidy
    // a link. The honest answer after you have worked the new slot is no.
    void undoRescheduleRefusesOnceTheReplacementHasTrackedTime()
    {
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        const QString oldId =
            data.addEvent(QDate(2026, 7, 19), 9 * 60, 10 * 60, act, "");
        const QString newId =
            data.rescheduleBlock(oldId, QDate(2026, 7, 21), 9 * 60, 10 * 60);
        data.appendSegment(newId, makeSegment(
            SegmentKind::Focus, QDateTime(QDate(2026, 7, 21), QTime(9, 0)), 25));

        QVERIFY(!data.undoReschedule(oldId));

        // Refused means NOTHING moved — not a partial undo.
        QVERIFY(data.eventById(newId));
        QCOMPARE(data.eventById(newId)->segments.size(), 1);
        QCOMPARE(data.eventById(oldId)->outcome, BlockOutcome::Moved);
        QCOMPARE(data.eventById(oldId)->movedToId, newId);
    }

    // This door reverses a MOVE and only a move; resolveBlock already
    // reverses Done/Dropped. One door per inverse, never one that guesses.
    void undoRescheduleRefusesBlocksThatWereNeverMoved()
    {
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        const QString id =
            data.addEvent(QDate(2026, 7, 19), 9 * 60, 10 * 60, act, "");

        QVERIFY(!data.undoReschedule(id));             // Unset
        data.resolveBlock(id, BlockOutcome::Dropped);
        QVERIFY(!data.undoReschedule(id));             // Dropped
        QCOMPARE(data.eventById(id)->outcome, BlockOutcome::Dropped);
        QVERIFY(!data.undoReschedule("no-such-id"));   // and no such block
    }

    // A movedToId whose target is already gone is a LIE, not an error: the
    // block reads Moved while nothing was moved. Clearing it is a repair, so
    // this succeeds rather than refusing.
    void undoRescheduleRepairsADanglingLink()
    {
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        const QString oldId =
            data.addEvent(QDate(2026, 7, 19), 9 * 60, 10 * 60, act, "");
        const QString newId =
            data.rescheduleBlock(oldId, QDate(2026, 7, 21), 9 * 60, 10 * 60);
        data.removeEvent(newId);                       // by hand, behind its back

        QVERIFY(data.undoReschedule(oldId));
        QCOMPARE(data.eventById(oldId)->outcome, BlockOutcome::Unset);
        QVERIFY(data.eventById(oldId)->movedToId.isEmpty());
    }

    // Both halves must look like one change. A listener that sees the middle
    // sees the work twice — an unresolved original AND a live replacement —
    // which is the state the door exists to make unobservable.
    void undoRescheduleEmitsExactlyOneChange()
    {
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        const QString oldId =
            data.addEvent(QDate(2026, 7, 19), 9 * 60, 10 * 60, act, "");
        data.rescheduleBlock(oldId, QDate(2026, 7, 21), 9 * 60, 10 * 60);

        QSignalSpy spy(&data, &AppData::changed);
        QVERIFY(data.undoReschedule(oldId));
        QCOMPARE(spy.count(), 1);
    }

    // The slot has to be free. Declining beats forcing — the same contract
    // as the three addEvent doors.
    void rescheduleBlockDeclinesAnOccupiedSlot()
    {
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        const QString oldId =
            data.addEvent(QDate(2026, 7, 19), 9 * 60, 10 * 60, act, "");
        data.addEvent(QDate(2026, 7, 21), 9 * 60, 10 * 60, act, "taken");

        QVERIFY(data.rescheduleBlock(oldId, QDate(2026, 7, 21),
                                     9 * 60, 10 * 60).isEmpty());
        QCOMPARE(data.eventById(oldId)->outcome, BlockOutcome::Unset);
    }

    // Moved is earned, not asserted: allowing it here would permit an
    // outcome of Moved with a movedToId pointing at nothing.
    void resolveBlockRefusesToFakeAMove()
    {
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        const QString id =
            data.addEvent(QDate(2026, 7, 19), 9 * 60, 10 * 60, act, "");

        QVERIFY(!data.resolveBlock(id, BlockOutcome::Moved));
        QCOMPARE(data.eventById(id)->outcome, BlockOutcome::Unset);

        QVERIFY(data.resolveBlock(id, BlockOutcome::Dropped));
        QCOMPARE(data.eventById(id)->outcome, BlockOutcome::Dropped);
    }

    // The scariest path, as always: does it survive disk?
    void catchUpVerdictsSurviveTheRoundTrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("data.json");

        QString newId;
        QString oldId;
        {
            AppData data;
            const QString cat = data.addCategory("School", QColor("#4C6FE0"));
            const QString act = data.addActivity("Study", cat);
            oldId = data.addEvent(QDate(2026, 7, 19), 9 * 60, 10 * 60, act, "");
            newId = data.rescheduleBlock(oldId, QDate(2026, 7, 21),
                                         9 * 60, 10 * 60);
            QVERIFY(!newId.isEmpty());
            QVERIFY(JsonStore(path).save(data));
        }

        AppData loaded;
        QVERIFY(JsonStore(path).load(loaded));
        const Event* old = loaded.eventById(oldId);
        QVERIFY(old);
        QCOMPARE(old->outcome, BlockOutcome::Moved);
        QCOMPARE(old->movedToId, newId);
        QCOMPARE(loaded.eventById(newId)->outcome, BlockOutcome::Unset);
    }

    // The split door is all-or-nothing: every span validated before anything
    // is appended, including spans against EACH OTHER — isFree can't see
    // siblings that don't exist yet.
    void rescheduleBlockSplitIsAllOrNothing()
    {
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        const QString oldId =
            data.addEvent(QDate(2026, 7, 19), 9 * 60, 10 * 60 + 30, act, "");

        // Two colliding spans on the same day: refused whole, nothing
        // appended, source untouched.
        {
            QVector<AppData::BlockSpan> bad{
                {QDate(2026, 7, 21), 9 * 60, 10 * 60},
                {QDate(2026, 7, 21), 9 * 60 + 30, 10 * 60 + 30}, // overlaps
            };
            QVERIFY(data.rescheduleBlockSplit(oldId, bad).isEmpty());
            QCOMPARE(data.events().size(), 1);
            QCOMPARE(data.eventById(oldId)->outcome, BlockOutcome::Unset);
        }

        // A legal split: two pieces, source Moved, forward pointer at the
        // FIRST piece (one link by design — §H).
        QVector<AppData::BlockSpan> good{
            {QDate(2026, 7, 21), 9 * 60, 10 * 60},
            {QDate(2026, 7, 22), 9 * 60, 9 * 60 + 30},
        };
        const QString firstId = data.rescheduleBlockSplit(oldId, good);
        QVERIFY(!firstId.isEmpty());
        QCOMPARE(data.events().size(), 3);
        QCOMPARE(data.eventById(oldId)->outcome, BlockOutcome::Moved);
        QCOMPARE(data.eventById(oldId)->movedToId, firstId);
        QCOMPARE(data.eventById(firstId)->date, QDate(2026, 7, 21));
    }

    // The bulk door: one decision, one changed(), stale ids skipped.
    void resolveBlocksIsOneEmissionAndSkipsStaleIds()
    {
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        const QString a =
            data.addEvent(QDate(2026, 7, 18), 9 * 60, 10 * 60, act, "");
        const QString b =
            data.addEvent(QDate(2026, 7, 19), 9 * 60, 10 * 60, act, "");

        int emissions = 0;
        QObject::connect(&data, &AppData::changed,
                         [&emissions]() { ++emissions; });

        // Two real ids, one stale: the stale one is skipped, the batch
        // lands, and the whole thing costs ONE changed().
        QCOMPARE(data.resolveBlocks({a, b, QStringLiteral("ghost")},
                                    BlockOutcome::Dropped),
                 2);
        QCOMPARE(emissions, 1);
        QCOMPARE(data.eventById(a)->outcome, BlockOutcome::Dropped);
        QCOMPARE(data.eventById(b)->outcome, BlockOutcome::Dropped);

        // Moved refused wholesale, nothing emitted, nothing changed.
        QCOMPARE(data.resolveBlocks({a, b}, BlockOutcome::Moved), 0);
        QCOMPARE(emissions, 1);

        // A no-op batch (already Dropped) emits nothing: changed() means
        // changed.
        QCOMPARE(data.resolveBlocks({a, b}, BlockOutcome::Dropped), 0);
        QCOMPARE(emissions, 1);

        // Unset is a legal destination — it is what Undo replays — and it
        // brings the blocks back under missed::'s eye: the judgement was
        // never stored, so un-deciding is one field write, not a repair.
        QCOMPARE(data.resolveBlocks({a, b}, BlockOutcome::Unset), 2);
        QCOMPARE(data.eventById(a)->outcome, BlockOutcome::Unset);
        QVERIFY(missed::isUnresolved(*data.eventById(a), missed::Rule(),
                                     QDateTime(QDate(2026, 7, 20),
                                               QTime(8, 0))));
    }

    // The briefing names the gap between plan and reality — and stops
    // naming a block the moment a decision lands on it.
    void briefingReportsUnresolvedBlocksUntilDecided()
    {
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        const QString id =
            data.addEvent(QDate(2026, 7, 19), 9 * 60, 10 * 60, act, "");

        const QDateTime now(QDate(2026, 7, 20), QTime(8, 0));
        verbs::HandleMap handles;
        const QString before =
            brief::dayBriefing(data, now.date(), now, brief::Options(), &handles);
        QVERIFY(before.contains(QStringLiteral("UNRESOLVED BLOCKS (1)")));
        QVERIFY(before.contains(QStringLiteral("never started")));

        // v29.2: the section the MoveBlock verb targets prints a block
        // handle, and that handle resolves back to this exact block. The
        // briefing prints what it registers — the §B.2 contract, now on the
        // block namespace too.
        QVERIFY(before.contains(QStringLiteral("[B1]")));
        QCOMPARE(handles.blockIdFor(QStringLiteral("B1")), id);

        // And still no UUID anywhere in the text — the whole point of handles.
        QVERIFY(!before.contains(id));

        QVERIFY(data.resolveBlock(id, BlockOutcome::Dropped));
        const QString after = brief::dayBriefing(data, now.date(), now);
        QVERIFY(!after.contains(QStringLiteral("UNRESOLVED")));
    }

    // ---- v28.0: affordability (assistant addendum §H) ----------------------
    // All pure: afford::affordability and afford::decide take the clock as
    // a parameter, so every rule below is pinned without a timer, a toast,
    // or QSettings. The fixture convention: "now" is Wed 2026-07-01 10:00,
    // deadlines land on nearby dates, and every block is placed explicitly
    // — a test that straddles a default is a test OF the default (the
    // lookBackDays lesson, V170), so nothing here leans on one.

    void affordabilityIsNotApplicableWithoutADeadline()
    {
        AppData data;
        const QString cat  = data.addCategory("School", "#2F7E6E");
        const QString id   = data.addTask("Sketchbook", cat); // no due date
        const QDateTime now(QDate(2026, 7, 1), QTime(10, 0));
        const Task* t = data.taskById(id);
        QVERIFY(t);
        QCOMPARE(afford::affordability(data, *t, now).verdict,
                 afford::Verdict::NotApplicable);
        // ...and NotApplicable produces no sentence: nothing to say.
        QVERIFY(afford::sentence(afford::affordability(data, *t, now))
                    .isEmpty());
    }

    void affordabilityIsUnknownWhenNoBlocksWereEverPlanned()
    {
        // §H.3 — the honest hand: a deadline and tracked-nothing, but the
        // app was never told the plan, so it must say "can't tell", not
        // perform a guess.
        AppData data;
        const QString cat = data.addCategory("School", "#2F7E6E");
        const QString id  = data.addTask("Lab 4", cat, QDate(2026, 7, 3));
        const QDateTime now(QDate(2026, 7, 1), QTime(10, 0));
        const afford::Report r =
            afford::affordability(data, *data.taskById(id), now);
        QCOMPARE(r.verdict, afford::Verdict::Unknown);
        QVERIFY(afford::sentence(r).contains(
            QStringLiteral("can't tell")));
    }

    void affordabilityComfortableWhenThePlanFitsTheRoom()
    {
        // 2h planned tomorrow, deadline Saturday, calendar otherwise
        // empty: outstanding (120) is far under scheduled+free capacity.
        AppData data;
        const QString cat = data.addCategory("School", "#2F7E6E");
        const QString id  = data.addTask("Lab 4", cat, QDate(2026, 7, 4));
        QVERIFY(!data.addTaskEvent(QDate(2026, 7, 2), 540, 660, id)
                     .isEmpty());
        const QDateTime now(QDate(2026, 7, 1), QTime(10, 0));
        const afford::Report r =
            afford::affordability(data, *data.taskById(id), now);
        QCOMPARE(r.verdict, afford::Verdict::Comfortable);
        QCOMPARE(r.minutesPlannedAll, 120);
        QCOMPARE(r.minutesPlannedAhead, 120);
        QCOMPARE(r.minutesOutstanding, 120);
    }

    void affordabilityTightWhenOutstandingExceedsCapacity()
    {
        // The cramped rule in isolation: a huge owed plan, a calendar so
        // full that free daytime cannot absorb it. Due tomorrow; today
        // 10:00→22:00 and all of tomorrow are walled off with other work,
        // and the task's own remaining block is 1h against 9h owed.
        AppData data;
        const QString cat = data.addCategory("School", "#2F7E6E");
        const QString act = data.addActivity("Job", cat);
        const QString id  = data.addTask("Lab 4", cat, QDate(2026, 7, 2));
        // The task's plan: 10h total, of which 9h already elapsed
        // (yesterday) with NOTHING tracked — and 1h still ahead.
        QVERIFY(!data.addTaskEvent(QDate(2026, 6, 30), 600, 1140, id)
                     .isEmpty()); // 9h yesterday, skipped
        QVERIFY(!data.addTaskEvent(QDate(2026, 7, 2), 540, 600, id)
                     .isEmpty()); // 1h tomorrow morning
        // Wall off the rest of both days with unrelated blocks.
        QVERIFY(!data.addEvent(QDate(2026, 7, 1), 600, 1320, act)
                     .isEmpty()); // today 10:00–22:00
        QVERIFY(!data.addEvent(QDate(2026, 7, 2), 360, 540, act)
                     .isEmpty()); // tomorrow 06:00–09:00
        QVERIFY(!data.addEvent(QDate(2026, 7, 2), 600, 1320, act)
                     .isEmpty()); // tomorrow 10:00–22:00
        const QDateTime now(QDate(2026, 7, 1), QTime(10, 0));
        const afford::Report r =
            afford::affordability(data, *data.taskById(id), now);
        QCOMPARE(r.verdict, afford::Verdict::Tight);
        QCOMPARE(r.minutesOutstanding, 600);      // 10h planned, 0 tracked
        QCOMPARE(r.minutesPlannedAhead, 60);
        QCOMPARE(r.minutesFreeAhead, 0);          // both days are walls
    }

    void affordabilityTightOnTheLastDayWithWorkOutstanding()
    {
        // lastDays rule: anything still owed with the deadline ≤1 day out
        // is Tight even when the free calendar could technically absorb
        // it — "you have all of today" is exactly when a secretary speaks.
        AppData data;
        const QString cat = data.addCategory("School", "#2F7E6E");
        const QString id  = data.addTask("Lab 4", cat, QDate(2026, 7, 2));
        QVERIFY(!data.addTaskEvent(QDate(2026, 7, 1), 720, 840, id)
                     .isEmpty()); // 2h today at noon, not yet started
        const QDateTime now(QDate(2026, 7, 1), QTime(10, 0));
        const afford::Report r =
            afford::affordability(data, *data.taskById(id), now);
        QCOMPARE(r.daysLeft, 1);
        QCOMPARE(r.verdict, afford::Verdict::Tight);
    }

    void affordabilityBehindOwnPlanTripsOnlyNearTheDeadline()
    {
        // The slipping rule needs BOTH halves: skipped past blocks alone,
        // with the deadline far away, stays Comfortable — being behind on
        // Monday for a due-in-two-weeks task is a Tuesday problem, not a
        // toast.
        AppData data;
        const QString cat = data.addCategory("School", "#2F7E6E");
        const QString id  = data.addTask("Essay", cat, QDate(2026, 7, 14));
        QVERIFY(!data.addTaskEvent(QDate(2026, 6, 29), 540, 660, id)
                     .isEmpty()); // 2h two days ago, skipped entirely
        QVERIFY(!data.addTaskEvent(QDate(2026, 7, 10), 540, 660, id)
                     .isEmpty()); // 2h planned ahead
        const QDateTime now(QDate(2026, 7, 1), QTime(10, 0));
        const afford::Report far =
            afford::affordability(data, *data.taskById(id), now);
        QVERIFY(far.behindOwnPlan);
        QCOMPARE(far.verdict, afford::Verdict::Comfortable);

        // Same shape, deadline in 2 days → the same "behind" now bites.
        AppData near;
        const QString cat2 = near.addCategory("School", "#2F7E6E");
        const QString id2  = near.addTask("Essay", cat2, QDate(2026, 7, 3));
        QVERIFY(!near.addTaskEvent(QDate(2026, 6, 29), 540, 660, id2)
                     .isEmpty());
        QVERIFY(!near.addTaskEvent(QDate(2026, 7, 2), 540, 660, id2)
                     .isEmpty());
        const afford::Report close =
            afford::affordability(near, *near.taskById(id2), now);
        QVERIFY(close.behindOwnPlan);
        QCOMPARE(close.verdict, afford::Verdict::Tight);
    }

    void affordabilityTrackedFocusPaysDownThePlan()
    {
        // The proxy is planned − TRACKED: focus segments on the task's own
        // block reduce what is outstanding. 2h planned yesterday, 90min of
        // focus actually tracked → 30min owed, deadline far → Comfortable,
        // and behindOwnPlan is false (90 ≥ 0.5 × 120).
        AppData data;
        const QString cat = data.addCategory("School", "#2F7E6E");
        const QString id  = data.addTask("Lab 4", cat, QDate(2026, 7, 6));
        const QString ev =
            data.addTaskEvent(QDate(2026, 6, 30), 540, 660, id);
        QVERIFY(!ev.isEmpty());
        data.appendSegment(
            ev, makeSegment(SegmentKind::Focus,
                            QDateTime(QDate(2026, 6, 30), QTime(9, 0)),
                            90));
        const QDateTime now(QDate(2026, 7, 1), QTime(10, 0));
        const afford::Report r =
            afford::affordability(data, *data.taskById(id), now);
        QCOMPARE(r.minutesTracked, 90);
        QCOMPARE(r.minutesOutstanding, 30);
        QVERIFY(!r.behindOwnPlan);
        QCOMPARE(r.verdict, afford::Verdict::Comfortable);
        QCOMPARE(r.distinctDaysWorked, 1);
    }

    // ---- the manners gate (§F.3): each rule in isolation -------------------

    void nudgeSpeaksOnlyOnAChangeOfVerdict()
    {
        // Tight→Tight is nagging; Comfortable→Tight is news. Same report,
        // only lastSpoken differs.
        Task t;
        t.id = "t1"; t.title = "Lab 4"; t.dueDate = QDate(2026, 7, 2);
        afford::Report r;
        r.verdict = afford::Verdict::Tight;
        r.minutesTracked = 60; r.daysLeft = 1;
        r.minutesOutstanding = 120; r.minutesFreeAhead = 60;
        const QDateTime day(QDate(2026, 7, 1), QTime(14, 0));

        const afford::Nudge fresh = afford::decide(
            r, t, afford::Verdict::Comfortable, 0, day);
        QVERIFY(fresh.speak);
        QVERIFY(fresh.title.contains(QStringLiteral("Lab 4")));
        QVERIFY(!fresh.body.isEmpty());

        const afford::Nudge repeat = afford::decide(
            r, t, afford::Verdict::Tight, 0, day);
        QVERIFY(!repeat.speak);
    }

    void nudgeRespectsQuietHoursIncludingTheMidnightWrap()
    {
        Task t;
        t.id = "t1"; t.title = "Lab 4"; t.dueDate = QDate(2026, 7, 2);
        afford::Report r; r.verdict = afford::Verdict::Tight;
        const auto last = afford::Verdict::Comfortable;
        // 23:40 — inside the wrap-around band.
        QVERIFY(!afford::decide(r, t, last, 0,
                                QDateTime(QDate(2026, 7, 1),
                                          QTime(23, 40))).speak);
        // 07:30 — still inside (before quietEnd 08:00).
        QVERIFY(!afford::decide(r, t, last, 0,
                                QDateTime(QDate(2026, 7, 1),
                                          QTime(7, 30))).speak);
        // 08:00 sharp — the band is [start, end): morning speech resumes.
        QVERIFY(afford::decide(r, t, last, 0,
                               QDateTime(QDate(2026, 7, 1),
                                         QTime(8, 0))).speak);
    }

    void nudgeHonoursTheDailyCapAndTheDismissal()
    {
        Task t;
        t.id = "t1"; t.title = "Lab 4"; t.dueDate = QDate(2026, 7, 2);
        afford::Report r; r.verdict = afford::Verdict::Tight;
        const QDateTime day(QDate(2026, 7, 1), QTime(14, 0));
        const auto last = afford::Verdict::Comfortable;

        QVERIFY(afford::decide(r, t, last, 2, day).speak);  // under cap (3)
        QVERIFY(!afford::decide(r, t, last, 3, day).speak); // at cap

        // §H.5 — a dismissed task said "not now"; the nudge must not make
        // the snooze a lie.
        t.dismissedUntil = QDateTime(QDate(2026, 7, 1), QTime(18, 0));
        QVERIFY(!afford::decide(r, t, last, 0, day).speak);
        // ...and it speaks again once the snooze lapses.
        QVERIFY(afford::decide(r, t, last, 0,
                               QDateTime(QDate(2026, 7, 1),
                                         QTime(18, 1))).speak);
    }

    void nudgeStaysSilentForComfortableAndUnknown()
    {
        // Volunteer-mode (§O.1) volunteers NEWS OF TROUBLE only. Unknown's
        // honest sentence exists — the chat can serve it — but it is not
        // worth an interruption; "I don't know" as a toast is noise.
        Task t;
        t.id = "t1"; t.title = "Lab 4"; t.dueDate = QDate(2026, 7, 5);
        const QDateTime day(QDate(2026, 7, 1), QTime(14, 0));
        afford::Report r;
        r.verdict = afford::Verdict::Comfortable;
        QVERIFY(!afford::decide(r, t, afford::Verdict::Tight, 0, day)
                     .speak);
        r.verdict = afford::Verdict::Unknown;
        QVERIFY(!afford::decide(r, t, afford::Verdict::Comfortable, 0, day)
                     .speak);
    }

    // ---- v28.1: the model's half, pure (NudgePhrasing.h) -------------------

    void nudgePromptCarriesTheRulesAboveTheStyle()
    {
        const QString bare = nudge::systemPrompt(QString());
        QVERIFY(bare.contains(QStringLiteral("Inform, never forbid")));
        QVERIFY(bare.contains(QStringLiteral("Never shame")));
        QVERIFY(!bare.contains(QStringLiteral("STYLE"))); // empty band,
                                                          // no empty header
        const QString styled =
            nudge::systemPrompt(QStringLiteral("Be brisk."));
        QVERIFY(styled.contains(QStringLiteral("Be brisk.")));
        // Order is the contract: the locked rules must come BEFORE the
        // style band, because the prompt says the earlier section wins.
        QVERIFY(styled.indexOf(QStringLiteral("RULES"))
                < styled.indexOf(QStringLiteral("STYLE")));
    }

    void nudgeUserMessageStatesTheNumbersItWasGiven()
    {
        afford::Report r;
        r.verdict = afford::Verdict::Tight;
        r.daysLeft = 1; r.minutesTracked = 130;
        r.minutesOutstanding = 180;
        r.minutesPlannedAhead = 60; r.minutesFreeAhead = 60;
        r.distinctDaysWorked = 5;
        const QString msg =
            nudge::userMessage(r, QStringLiteral("Lab 4"));
        QVERIFY(msg.contains(QStringLiteral("Lab 4")));
        QVERIFY(msg.contains(QStringLiteral("due tomorrow")));
        QVERIFY(msg.contains(QStringLiteral("2h10")));  // tracked
        QVERIFY(msg.contains(QStringLiteral("3h")));    // outstanding
        QVERIFY(msg.contains(QStringLiteral("2h")));    // room
        QVERIFY(msg.contains(QStringLiteral("5")));     // days worked
    }

    void nudgeAcceptGateCleansShapeAndRejectsEssays()
    {
        // Markdown and whitespace are cleaned, not punished...
        QCOMPARE(nudge::accept(QStringLiteral(
                     "  **Heads up!**\nLab 4 is `tight`.  ")),
                 QStringLiteral("Heads up! Lab 4 is tight."));
        // ...emptiness and essays are rejected — the fallback sentence is
        // a better answer than a truncated one.
        QVERIFY(nudge::accept(QStringLiteral("   \n  ")).isEmpty());
        QVERIFY(nudge::accept(QString(300, QLatin1Char('x'))).isEmpty());
        // Exactly at the cap passes: the limit is a limit, not a vibe.
        QVERIFY(!nudge::accept(QString(nudge::kMaxChars,
                                       QLatin1Char('x'))).isEmpty());
    }

    void briefingCarriesTheAffordabilityVerdicts()
    {
        // The ask-side: the chat's context now contains the same computed
        // verdict the toast volunteers — TIGHT with its numbers, and the
        // honest UNKNOWN.
        AppData data;
        const QString cat = data.addCategory("School", "#2F7E6E");
        const QString tight =
            data.addTask("Lab 4", cat, QDate(2026, 7, 2));
        QVERIFY(!data.addTaskEvent(QDate(2026, 7, 1), 720, 840, tight)
                     .isEmpty()); // 2h today, unstarted, due tomorrow
        const QString unknown =
            data.addTask("Essay", cat, QDate(2026, 7, 3)); // no blocks
        const QDateTime now(QDate(2026, 7, 1), QTime(10, 0));
        const QString brief =
            brief::dayBriefing(data, now.date(), now);
        QVERIFY(brief.contains(QStringLiteral("DEADLINE PRESSURE")));
        QVERIFY(brief.contains(QStringLiteral("Lab 4: TIGHT")));
        QVERIFY(brief.contains(QStringLiteral("Essay: UNKNOWN")));
        Q_UNUSED(unknown);
    }

    // ---- v28.2 part 1: mood + the check-in gate (roadmap §G) ---------------

    void moodUpsertsByDateAndRoundTripsThroughV12()
    {
        AppData data;
        data.recordMood(QDate(2026, 7, 1), Mood::Level::Rough,
                        QStringLiteral("slept badly"));
        data.recordMood(QDate(2026, 7, 1), Mood::Level::Okay); // re-answer
        QCOMPARE(data.moods().size(), 1); // upsert, not append
        QCOMPARE(data.moodOn(QDate(2026, 7, 1))->level, Mood::Level::Okay);
        QVERIFY(data.moodOn(QDate(2026, 7, 1))->note.isEmpty());

        data.recordMood(QDate(2026, 7, 2), Mood::Level::Good,
                        QStringLiteral("ça va bien"));
        const QJsonObject root = JsonStore::toJsonObject(data);
        // This pin is the FORMAT-VERSION TRIPWIRE: it names the current
        // format and must be bumped in the same drop as JsonStore's
        // literal. v28.3.0 bumped the format to 13 (subtasks) and missed
        // this line — the suite's first real run caught it, which is the
        // tripwire working, one drop late. Moods still round-trip below
        // regardless of the number; the number is its own test.
        QCOMPARE(root["version"].toInt(), 13);

        AppData back;
        QVERIFY(JsonStore::applyJsonObject(back, root, false));
        QCOMPARE(back.moods().size(), 2);
        QCOMPARE(back.moodOn(QDate(2026, 7, 2))->level, Mood::Level::Good);
        QCOMPARE(back.moodOn(QDate(2026, 7, 2))->note,
                 QStringLiteral("ça va bien")); // notes persist — for the
                                                // OWNER, never the model
    }

    void moodTrimHonoursTheRetentionPromise()
    {
        AppData data;
        data.recordMood(QDate(2026, 6, 15), Mood::Level::Okay); // 16 days old
        data.recordMood(QDate(2026, 6, 18), Mood::Level::Good); // 13 days old
        data.recordMood(QDate(2026, 7, 1),  Mood::Level::Rough);
        QCOMPARE(data.trimMoods(QDate(2026, 7, 1), 14), 1);
        QCOMPARE(data.moods().size(), 2);
        QVERIFY(!data.moodOn(QDate(2026, 6, 15)));
        // Idempotent: the knock runs daily; a second pass removes nothing.
        QCOMPARE(data.trimMoods(QDate(2026, 7, 1), 14), 0);
    }

    void briefingSpeaksCoarseMoodAndNeverTheNote()
    {
        // THE privacy test (§G.2). The note is the owner's words about
        // their own state; if this test ever fails, the fix is in the
        // briefing, not here.
        AppData data;
        data.recordMood(QDate(2026, 7, 1), Mood::Level::Rough,
                        QStringLiteral("fight with my brother"));
        const QDateTime now(QDate(2026, 7, 1), QTime(10, 0));
        // v28.2p2 — §E.4 made mechanical. DEFAULT briefing: no mood at
        // all; a new call site is private by accident, never leaky by
        // accident.
        const QString silent = brief::dayBriefing(data, now.date(), now);
        QVERIFY(!silent.contains(QStringLiteral("MOOD")));

        // Opted in (the chat does this only when every route seat is
        // local): coarse value yes, the owner's note NEVER.
        brief::Options opts;
        opts.includeMood = true;
        const QString brief =
            brief::dayBriefing(data, now.date(), now, opts);
        QVERIFY(brief.contains(QStringLiteral("MOOD")));
        QVERIFY(brief.contains(QStringLiteral("today: rough")));
        QVERIFY(!brief.contains(QStringLiteral("fight with my brother")));
    }

    void isLocalDrawsTheSeatBoundaryConservatively()
    {
        ai::Provider p;
        p.baseUrl = QUrl(QStringLiteral("http://localhost:11434"));
        QVERIFY(ai::isLocal(p));
        p.baseUrl = QUrl(QStringLiteral("http://127.0.0.1:1234/v1"));
        QVERIFY(ai::isLocal(p));
        // A LAN machine is still a wire the mood crossed: remote.
        p.baseUrl = QUrl(QStringLiteral("http://192.168.1.20:11434"));
        QVERIFY(!ai::isLocal(p));
        p.baseUrl = QUrl(QStringLiteral("https://api.anthropic.com"));
        QVERIFY(!ai::isLocal(p));
    }

    void checkInHeavinessTripsOnBlocksOrDeadlines()
    {
        // Either wall of work OR deadline cluster; each alone suffices.
        AppData planned;
        const QString cat = planned.addCategory("School", "#2F7E6E");
        const QString act = planned.addActivity("Study", cat);
        QVERIFY(!planned.addEvent(QDate(2026, 7, 1), 540, 870, act)
                     .isEmpty()); // 5h30 planned
        QVERIFY(checkin::isDayHeavy(planned, QDate(2026, 7, 1)));

        AppData deadlines;
        const QString cat2 = deadlines.addCategory("School", "#2F7E6E");
        deadlines.addTask("Lab 4", cat2, QDate(2026, 7, 2));
        deadlines.addTask("Essay", cat2, QDate(2026, 7, 3));
        QVERIFY(checkin::isDayHeavy(deadlines, QDate(2026, 7, 1)));

        AppData quiet; // one small block, one far deadline: a quiet Tuesday
        const QString cat3 = quiet.addCategory("School", "#2F7E6E");
        const QString act3 = quiet.addActivity("Study", cat3);
        QVERIFY(!quiet.addEvent(QDate(2026, 7, 1), 540, 600, act3)
                     .isEmpty());
        quiet.addTask("Essay", cat3, QDate(2026, 7, 20));
        QVERIFY(!checkin::isDayHeavy(quiet, QDate(2026, 7, 1)));
    }

    void checkInOffersOncePerMorningOnHeavyDaysOnly()
    {
        AppData data;
        const QString cat = data.addCategory("School", "#2F7E6E");
        data.addTask("Lab 4", cat, QDate(2026, 7, 2));
        data.addTask("Essay", cat, QDate(2026, 7, 3)); // heavy: 2 urgent

        const QDate none; // never offered
        // 08:30, heavy, not yet offered → yes.
        QVERIFY(checkin::shouldOffer(
            data, QDateTime(QDate(2026, 7, 1), QTime(8, 30)), none));
        // Already offered today → once means once.
        QVERIFY(!checkin::shouldOffer(
            data, QDateTime(QDate(2026, 7, 1), QTime(9, 0)),
            QDate(2026, 7, 1)));
        // Yesterday's offer does not spend today's.
        QVERIFY(checkin::shouldOffer(
            data, QDateTime(QDate(2026, 7, 1), QTime(8, 30)),
            QDate(2026, 6, 30)));
        // 05:59 and 11:00 — outside the morning, [start, end).
        QVERIFY(!checkin::shouldOffer(
            data, QDateTime(QDate(2026, 7, 1), QTime(5, 59)), none));
        QVERIFY(!checkin::shouldOffer(
            data, QDateTime(QDate(2026, 7, 1), QTime(11, 0)), none));
    }
// ---- pieces & sizing (v28.3, roadmap §I / §J.1) -----------------------
    // The five query policies, the cascades, and the two format-v13 facts.
    // Each test pins ONE decision from the subtasks addendum, so a future
    // change that breaks a policy names the policy it broke.

    void subtaskBirthEnforcesTheDoorRules()
    {
        AppData data;
        const QString cat    = data.addCategory("School", QColor("#4C6FE0"));
        const QString parent = data.addTask("Lab 4", cat, QDate(), QTime());

        // Refusals: no parent, blank title.
        QVERIFY(data.addSubtask("nope", "read the spec").isEmpty());
        QVERIFY(data.addSubtask(parent, "   ").isEmpty());

        // Birth: inherits the parent's category, records the link.
        const QString piece = data.addSubtask(parent, "read the spec");
        QVERIFY(!piece.isEmpty());
        QCOMPARE(data.taskById(piece)->categoryId, cat);
        QCOMPARE(data.taskById(piece)->parentId, parent);
        QVERIFY(data.taskById(piece)->isPiece());

        // THE ONE-LEVEL RULE: a piece may not have pieces.
        QVERIFY(data.addSubtask(piece, "sub-sub").isEmpty());
    }

    void subtaskMayCarryItsOwnDeadline()
    {
        AppData data;
        const QString cat    = data.addCategory("School", QColor("#4C6FE0"));
        const QString parent = data.addTask("Lab 4", cat,
                                            QDate(2026, 8, 8), QTime());

        const QString dated = data.addSubtask(parent, "Marc's section",
                                              QDate(2026, 8, 4),
                                              QTime(17, 0));
        QCOMPARE(data.taskById(dated)->dueDate, QDate(2026, 8, 4));
        QCOMPARE(data.taskById(dated)->dueTime, QTime(17, 0));

        // Same orphan-clock rule as every other birth door: a time
        // without a date is silently not a state.
        const QString clockOnly = data.addSubtask(parent, "loose end",
                                                  QDate(), QTime(9, 0));
        QVERIFY(!data.taskById(clockOnly)->dueTime.isValid());
    }

    void subtasksOfKeepsInsertionOrderAndSkipsArchived()
    {
        AppData data;
        const QString cat    = data.addCategory("School", QColor("#4C6FE0"));
        const QString parent = data.addTask("Lab 4", cat, QDate(), QTime());
        const QString a = data.addSubtask(parent, "read the spec");
        const QString b = data.addSubtask(parent, "write section 1");
        const QString c = data.addSubtask(parent, "proofread");

        // A checklist's order IS meaning — never sorted (header rationale).
        auto pieces = data.subtasksOf(parent);
        QCOMPARE(pieces.size(), 3);
        QCOMPARE(pieces[0]->id, a);
        QCOMPARE(pieces[1]->id, b);
        QCOMPARE(pieces[2]->id, c);

        // Archived pieces leave the list, so it always agrees with
        // pieceProgress (they must describe the same set).
        QVERIFY(data.setTaskArchived(b, true));
        pieces = data.subtasksOf(parent);
        QCOMPARE(pieces.size(), 2);
        QCOMPARE(pieces[1]->id, c);

        // Empty answers for a non-parent and for a piece.
        QVERIFY(data.subtasksOf("nope").isEmpty());
        QVERIFY(data.subtasksOf(a).isEmpty());
    }

    void pieceProgressCountsTheVisibleChecklist()
    {
        AppData data;
        const QString cat    = data.addCategory("School", QColor("#4C6FE0"));
        const QString parent = data.addTask("Lab 4", cat, QDate(), QTime());
        const QString a = data.addSubtask(parent, "read");
        const QString b = data.addSubtask(parent, "write");
        data.addSubtask(parent, "proofread");

        QCOMPARE(data.pieceProgress(parent).done, 0);
        QCOMPARE(data.pieceProgress(parent).total, 3);

        QVERIFY(data.setTaskDone(a, true));
        QCOMPARE(data.pieceProgress(parent).done, 1);

        // Archiving removes a piece from BOTH numbers — an archived piece
        // must not make a finished task read permanently incomplete.
        QVERIFY(data.setTaskArchived(b, true));
        QCOMPARE(data.pieceProgress(parent).done, 1);
        QCOMPARE(data.pieceProgress(parent).total, 2);
    }

    void completionNeverRollsUp()
    {
        AppData data;
        const QString cat    = data.addCategory("School", QColor("#4C6FE0"));
        const QString parent = data.addTask("Lab 4", cat, QDate(), QTime());
        const QString a = data.addSubtask(parent, "read");
        const QString b = data.addSubtask(parent, "write");

        // Every piece done -> the parent stays OPEN. The tick is the
        // reward (§I) — the app never takes it for you.
        QVERIFY(data.setTaskDone(a, true));
        QVERIFY(data.setTaskDone(b, true));
        QVERIFY(!data.taskById(parent)->done);

        // And downward: finishing the parent doesn't finish the pieces.
        const QString c = data.addSubtask(parent, "late addition");
        QVERIFY(data.setTaskDone(parent, true));
        QVERIFY(data.taskById(parent)->done);
        QVERIFY(!data.taskById(c)->done); // the parent's tick is its own
    }

    void archiveCascadesBothDirections()
    {
        AppData data;
        const QString cat    = data.addCategory("School", QColor("#4C6FE0"));
        const QString parent = data.addTask("Lab 4", cat, QDate(), QTime());
        const QString a = data.addSubtask(parent, "read");
        const QString b = data.addSubtask(parent, "write");

        // One piece archived by hand first — restore brings it back too
        // (deliberately unconditional; see setTaskArchived's comment).
        QVERIFY(data.setTaskArchived(a, true));

        QVERIFY(data.setTaskArchived(parent, true));
        QVERIFY(data.taskById(a)->archived);
        QVERIFY(data.taskById(b)->archived);

        QVERIFY(data.setTaskArchived(parent, false));
        QVERIFY(!data.taskById(a)->archived);
        QVERIFY(!data.taskById(b)->archived);

        // Archiving a lone PIECE cascades nowhere.
        QVERIFY(data.setTaskArchived(b, true));
        QVERIFY(!data.taskById(parent)->archived);
    }

    void removingAParentTakesItsPieces()
    {
        AppData data;
        const QString cat    = data.addCategory("School", QColor("#4C6FE0"));
        const QString parent = data.addTask("Lab 4", cat, QDate(), QTime());
        const QString piece  = data.addSubtask(parent, "Marc's section");

        // A block planned against the PIECE: after the cascade it must be
        // demoted to text like any other reference to a removed task —
        // the piece's title, not the parent's.
        const QString ev = data.addTaskEvent(QDate(2026, 8, 4), 540, 600,
                                             piece);
        QVERIFY(!ev.isEmpty());

        QVERIFY(data.removeTask(parent));
        QVERIFY(!data.taskById(parent));
        QVERIFY(!data.taskById(piece));  // the cascade
        QVERIFY(data.eventById(ev)->taskId.isEmpty());
        QCOMPARE(data.eventById(ev)->title, QString("Marc's section"));
    }

    void queryPoliciesDisagreeOnPurpose()
    {
        AppData data;
        const QString cat    = data.addCategory("School", QColor("#4C6FE0"));
        const QString parent = data.addTask("Lab 4", cat,
                                            QDate(2026, 8, 8), QTime());
        const QString piece  = data.addSubtask(parent, "Marc's section",
                                               QDate(2026, 8, 4), QTime());

        // Workload surfaces: PARENTS ONLY — even for a dated piece.
        for (const Task* t : data.upcomingTasks())
            QVERIFY(t->id != piece);
        for (const Task* t : data.tasksIn(cat))
            QVERIFY(t->id != piece);

        // The calendar day: the dated piece IS a real obligation there.
        const auto due = data.tasksDueOn(QDate(2026, 8, 4));
        QCOMPARE(due.size(), 1);
        QCOMPARE(due.first()->id, piece);

        // The guard counts EVERYTHING — and therefore keeps guarding.
        QCOMPARE(data.taskCountIn(cat), 2);
        QVERIFY(!data.removeCategory(cat));
    }

    void sizingClampsAndReadsHonestly()
    {
        AppData data;
        const QString cat  = data.addCategory("School", QColor("#4C6FE0"));
        const QString task = data.addTask("Lab 4", cat, QDate(), QTime());

        QVERIFY(!data.taskById(task)->hasEstimate()); // 0 == unset, not instant

        QVERIFY(data.setTaskSize(task, 90, true));
        QCOMPARE(data.taskById(task)->estimateMinutes, 90);
        QVERIFY(data.taskById(task)->chunkable);
        QVERIFY(data.taskById(task)->hasEstimate());

        // Negatives clamp back to "unset" — minus twenty minutes is not a size.
        QVERIFY(data.setTaskSize(task, -20, false));
        QVERIFY(!data.taskById(task)->hasEstimate());

        QVERIFY(!data.setTaskSize("nope", 30, false));
    }

    void repeatingPieceSpawnsAsAPiece()
    {
        AppData data;
        const QString cat    = data.addCategory("School", QColor("#4C6FE0"));
        const QString parent = data.addTask("Weekly review", cat,
                                            QDate(), QTime());
        const QString piece  = data.addSubtask(parent, "clear inbox",
                                               QDate(2026, 7, 6), QTime());
        QVERIFY(data.setTaskSize(piece, 45, true));
        // Give the piece a weekly rule through the coarse door.
        const Task* p = data.taskById(piece);
        QVERIFY(data.updateTask(piece, p->title, QString(),
                                p->dueDate, QTime(),
                                Task::Repeat::Weekly, Task::Priority::Medium));

        QVERIFY(data.setTaskDone(piece, true));

        // The spawned next occurrence is still a piece of the SAME parent,
        // same size, same shape — a checklist line must not quietly promote
        // itself to a full task every week.
        const Task* next = nullptr;
        for (const Task& t : data.tasks())
            if (t.id != piece && t.parentId == parent)
                next = &t;
        QVERIFY(next);
        QCOMPARE(next->dueDate, QDate(2026, 7, 13));
        QCOMPARE(next->estimateMinutes, 45);
        QVERIFY(next->chunkable);
        QVERIFY(!next->done);
    }

    void formatV13RoundTripsTheThreeFacts()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("data.json");

        AppData original;
        const QString cat    = original.addCategory("School",
                                                    QColor("#4C6FE0"));
        const QString parent = original.addTask("Lab 4", cat,
                                                QDate(2026, 8, 8), QTime());
        const QString piece  = original.addSubtask(parent, "read the spec");
        QVERIFY(original.setTaskSize(parent, 120, false));
        QVERIFY(original.setTaskSize(piece, 25, true));

        QVERIFY(JsonStore(path).save(original));
        AppData loaded;
        QVERIFY(JsonStore(path).load(loaded));

        QCOMPARE(loaded.taskById(piece)->parentId, parent);
        QCOMPARE(loaded.taskById(parent)->estimateMinutes, 120);
        QCOMPARE(loaded.taskById(piece)->estimateMinutes, 25);
        QVERIFY(loaded.taskById(piece)->chunkable);
        QVERIFY(!loaded.taskById(parent)->chunkable);
        QCOMPARE(loaded.pieceProgress(parent).total, 1);
    }

    void loadAdoptsOrphanedPieces()
    {
        // Hand-built vectors straight through the load door: one piece
        // whose parent id resolves to nothing, one nested deeper than the
        // domain allows. Both must come out as honest top-level tasks —
        // invisible-but-counted is the failure mode this guards against.
        Task parent;  parent.id = "p";  parent.title = "Lab 4";
        Task good;    good.id   = "g";  good.title   = "fine";
        good.parentId = "p";
        Task dangling; dangling.id = "d"; dangling.title = "lost";
        dangling.parentId = "ghost";
        Task nested;  nested.id  = "n";  nested.title  = "too deep";
        nested.parentId = "g"; // a piece under a piece

        AppData data;
        data.resetFrom({}, {}, {},
                       {parent, good, dangling, nested}, {}, {},
                       std::nullopt);

        QCOMPARE(data.taskById("g")->parentId, QString("p")); // untouched
        QVERIFY(data.taskById("d")->parentId.isEmpty());      // adopted
        QVERIFY(data.taskById("n")->parentId.isEmpty());      // un-nested
    }

    void batchEmitsExactlyOnce()
    {
        AppData data;
        const QString cat  = data.addCategory("School", QColor("#4C6FE0"));
        const QString task = data.addTask("Lab 4", cat, QDate(), QTime());

        QSignalSpy spy(&data, &AppData::changed);
        {
            AppData::Batch batch(data);
            QVERIFY(data.setTaskSize(task, 60, false));
            QVERIFY(!data.addSubtask(task, "read").isEmpty());
            QVERIFY(!data.addSubtask(task, "write").isEmpty());
            QCOMPARE(spy.count(), 0); // silence while the batch is alive
        }
        QCOMPARE(spy.count(), 1);     // ONE repaint for three mutations

        // A batch in which nothing actually changed emits nothing at all —
        // the idempotence promise, kept at the group level.
        {
            AppData::Batch batch(data);
            QVERIFY(data.setTaskSize(task, 60, false)); // same values: no-op
        }
        QCOMPARE(spy.count(), 1);
    }
// ---- sizing intelligence (v28.4, roadmap §J.2) ------------------------
    // The personal multiplier and the estimate-first affordability rewire.
    // Each test passes the multiplier EXPLICITLY where the arithmetic is
    // under test — the rate's own derivation gets its own tests first.

    void multiplierIsOneWithoutEnoughHistory()
    {
        AppData data;
        QCOMPARE(afford::personalMultiplier(data), 1.0); // no history at all

        // Two finished-and-tracked estimates: still an anecdote, not a rate.
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        for (int i = 0; i < 2; ++i) {
            const QString id = data.addTask(QStringLiteral("t%1").arg(i),
                                            cat, QDate(), QTime());
            QVERIFY(data.setTaskSize(id, 60, false));
            const QString ev =
                data.addTaskEvent(QDate(2026, 6, 1 + i), 540, 660, id);
            QVERIFY(!ev.isEmpty());
            QVERIFY(data.appendSegment(
                ev, makeSegment(SegmentKind::Focus,
                                QDateTime(QDate(2026, 6, 1 + i), QTime(9, 0)),
                                90)));
            QVERIFY(data.setTaskDone(id, true));
        }
        QCOMPARE(afford::personalMultiplier(data), 1.0);
    }

    void multiplierIsTheMedianOfFinishedRatiosAndClamps()
    {
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));

        // Three finished 60-minute estimates, tracked 90 / 90 / 600 —
        // ratios 1.5, 1.5, 10. The MEDIAN shrugs off the 10x disaster.
        //
        // Fixture note, learned the loud way: blocks must respect the
        // planner's own day window (isFree refuses anything before
        // plan::kDayStartMinutes) — the first draft planted 05:00 blocks,
        // every addTaskEvent politely returned "", and the unchecked
        // empty ids made appendSegment a silent no-op three asserts
        // upstream of the failure. Hence 09:00–21:00, and a QVERIFY on
        // EVERY door: a refusal you don't check becomes a mystery later.
        const int tracked[] = {90, 90, 600};
        for (int i = 0; i < 3; ++i) {
            const QString id = data.addTask(QStringLiteral("t%1").arg(i),
                                            cat, QDate(), QTime());
            QVERIFY(data.setTaskSize(id, 60, false));
            const QString ev =
                data.addTaskEvent(QDate(2026, 6, 1 + i), 540, 1260, id);
            QVERIFY(!ev.isEmpty());
            QVERIFY(data.appendSegment(
                ev, makeSegment(SegmentKind::Focus,
                                QDateTime(QDate(2026, 6, 1 + i), QTime(9, 0)),
                                tracked[i])));
            QVERIFY(data.setTaskDone(id, true));
        }
        QCOMPARE(afford::personalMultiplier(data), 1.5);

        // Evidence rules: an UNFINISHED estimate, a finished task WITHOUT
        // an estimate, and a finished estimate with NO tracked focus are
        // all non-samples — the rate must not move.
        const QString open1 = data.addTask("open", cat, QDate(), QTime());
        QVERIFY(data.setTaskSize(open1, 10, false));
        const QString noEst = data.addTask("noEst", cat, QDate(), QTime());
        QVERIFY(data.setTaskDone(noEst, true));
        const QString offBooks = data.addTask("offBooks", cat, QDate(), QTime());
        QVERIFY(data.setTaskSize(offBooks, 10, false));
        QVERIFY(data.setTaskDone(offBooks, true));
        QCOMPARE(afford::personalMultiplier(data), 1.5);

        // The clamp: three 6x ratios -> median 6, ceiling says 3.0.
        AppData wild;
        const QString wcat = wild.addCategory("School", QColor("#4C6FE0"));
        for (int i = 0; i < 3; ++i) {
            const QString id = wild.addTask(QStringLiteral("w%1").arg(i),
                                            wcat, QDate(), QTime());
            QVERIFY(wild.setTaskSize(id, 60, false));
            const QString ev =
                wild.addTaskEvent(QDate(2026, 6, 1 + i), 540, 1260, id);
            QVERIFY(!ev.isEmpty());
            QVERIFY(wild.appendSegment(
                ev, makeSegment(SegmentKind::Focus,
                                QDateTime(QDate(2026, 6, 1 + i), QTime(9, 0)),
                                360)));
            QVERIFY(wild.setTaskDone(id, true));
        }
        QCOMPARE(afford::personalMultiplier(wild), 3.0);
    }

    void estimatedTaskIsNeverUnknown()
    {
        // The Unknown retirement (§J.2): a deadline plus an estimate is an
        // answerable question even with ZERO blocks ever planned — the
        // estimate answers what the blocks used to.
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QString id  = data.addTask("Lab 4", cat, QDate(2026, 7, 20));
        QVERIFY(data.setTaskSize(id, 120, false));

        const QDateTime now(QDate(2026, 7, 1), QTime(10, 0));
        const afford::Report r =
            afford::affordability(data, *data.taskById(id), now, {}, 1.0);
        QVERIFY(r.verdict != afford::Verdict::Unknown);
        QVERIFY(r.estimateBased);
        QCOMPARE(r.minutesEstimated, 120);
        QCOMPARE(r.minutesOutstanding, 120); // nothing tracked yet
        QCOMPARE(r.verdict, afford::Verdict::Comfortable); // weeks of room

        // And the other verdict, still with zero blocks planned: an
        // estimated task DUE TOMORROW with work outstanding trips the
        // last-day rule — the estimate feeds every band the proxy used to.
        const QString rush = data.addTask("Rush job", cat, QDate(2026, 7, 2));
        QVERIFY(data.setTaskSize(rush, 120, false));
        const afford::Report tight =
            afford::affordability(data, *data.taskById(rush), now, {}, 1.0);
        QVERIFY(tight.estimateBased);
        QCOMPARE(tight.verdict, afford::Verdict::Tight);
    }

    void estimateOutstandingUsesTheMultiplierAndPaysDown()
    {
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QString id  = data.addTask("Lab 4", cat, QDate(2026, 7, 20));
        QVERIFY(data.setTaskSize(id, 60, false));
        const QString ev = data.addTaskEvent(QDate(2026, 6, 30), 540, 600, id);
        data.appendSegment(
            ev, makeSegment(SegmentKind::Focus,
                            QDateTime(QDate(2026, 6, 30), QTime(9, 0)), 30));

        // 60min estimate at the user's 1.5x rate = 90 real minutes; 30
        // already tracked -> 60 outstanding. The rate scales the ESTIMATE,
        // never the work already done — done minutes are facts.
        const QDateTime now(QDate(2026, 7, 1), QTime(10, 0));
        const afford::Report r =
            afford::affordability(data, *data.taskById(id), now, {}, 1.5);
        QVERIFY(r.estimateBased);
        QCOMPARE(r.multiplier, 1.5);
        QCOMPARE(r.minutesOutstanding, 60);
    }

    void pieceEstimatesSizeAnUnsizedParent()
    {
        // The decomposition dividend (§J.3's opening move): "write lab
        // report" is unguessable, its pieces aren't. An unsized parent
        // borrows the SUM of its pieces' estimates — and an archived piece
        // stops weighing, matching every other pieces query.
        AppData data;
        const QString cat    = data.addCategory("School", QColor("#4C6FE0"));
        const QString parent = data.addTask("Lab 4", cat, QDate(2026, 7, 20));
        const QString a = data.addSubtask(parent, "read the spec");
        const QString b = data.addSubtask(parent, "draft section 1");
        const QString c = data.addSubtask(parent, "old idea");
        QVERIFY(data.setTaskSize(a, 25, true));
        QVERIFY(data.setTaskSize(b, 35, false));
        QVERIFY(data.setTaskSize(c, 100, false));
        QVERIFY(data.setTaskArchived(c, true)); // ✕'d out of the checklist

        const QDateTime now(QDate(2026, 7, 1), QTime(10, 0));
        const afford::Report r =
            afford::affordability(data, *data.taskById(parent), now, {}, 1.0);
        QVERIFY(r.estimateBased);
        QCOMPARE(r.minutesEstimated, 60); // 25 + 35; the archived 100 doesn't

        // A parent with its OWN estimate ignores the pieces' numbers —
        // the owner's direct answer outranks the derived one.
        QVERIFY(data.setTaskSize(parent, 200, false));
        const afford::Report own =
            afford::affordability(data, *data.taskById(parent), now, {}, 1.0);
        QCOMPARE(own.minutesEstimated, 200);
    }

    // ---- v28.9 — promotion: dated pieces answer for themselves ------------

    // The headline: a dated piece gets its own verdict (this function's
    // first guard makes it so), therefore its minutes leave the parent —
    // the sweep must believe exactly what was entered, once.
    void promotedPieceStopsWeighingOnItsParent()
    {
        AppData data;
        const QString cat    = data.addCategory("School", QColor("#4C6FE0"));
        const QString parent =
            data.addTask("ING150 FINALS", cat, QDate(2026, 8, 14));
        QVERIFY(data.setTaskSize(parent, 720, false)); // 12h, the owner's case
        const QString ch10 = data.addSubtask(parent, "Chapter 10");
        QVERIFY(data.setTaskSize(ch10, 240, false));
        QVERIFY(data.updateTask(ch10, "Chapter 10", {}, QDate(2026, 8, 10),
                                QTime(), Task::Repeat::None,
                                Task::Priority::Medium)); // dated → promoted
        const QString ch11 = data.addSubtask(parent, "Chapter 11");
        QVERIFY(data.setTaskSize(ch11, 240, false)); // sized, UNDATED — stays

        const QDateTime now(QDate(2026, 8, 1), QTime(10, 0));
        const afford::Report p =
            afford::affordability(data, *data.taskById(parent), now, {}, 1.0);
        QCOMPARE(p.minutesEstimated, 480); // 720 − promoted 240; ch11 stays
        QCOMPARE(p.minutesPromoted, 240);  // the ledger of what left
        const afford::Report c =
            afford::affordability(data, *data.taskById(ch10), now, {}, 1.0);
        QCOMPARE(c.minutesEstimated, 240); // the piece answers for itself
        // Believed total = 480 + 240 = 720: exactly what was entered, once.
    }

    // The borrow amendment: an unsized parent borrows ONLY from undated
    // pieces — a dated piece's minutes are its own now, on the borrow
    // path too (before this, unsized parent + dated sized piece was the
    // second double-count).
    void borrowSkipsPromotedPieces()
    {
        AppData data;
        const QString cat    = data.addCategory("School", QColor("#4C6FE0"));
        const QString parent = data.addTask("Lab 5", cat, QDate(2026, 8, 20));
        const QString spec  = data.addSubtask(parent, "read the spec");
        QVERIFY(data.setTaskSize(spec, 25, true)); // undated: borrowed
        const QString draft = data.addSubtask(parent, "draft");
        QVERIFY(data.setTaskSize(draft, 240, false));
        QVERIFY(data.updateTask(draft, "draft", {}, QDate(2026, 8, 18),
                                QTime(), Task::Repeat::None,
                                Task::Priority::Medium)); // dated: its own

        const QDateTime now(QDate(2026, 8, 1), QTime(10, 0));
        const afford::Report r =
            afford::affordability(data, *data.taskById(parent), now, {}, 1.0);
        QCOMPARE(r.minutesEstimated, 25); // only the undated piece
        QCOMPARE(r.minutesPromoted, 240);
    }

    // Over-decomposed: promoted pieces sum past the parent's estimate.
    // The parent's number floors at 0 and its verdict basis honestly
    // degrades to the planned-blocks proxy (estimateBased false) — the
    // pieces are the truth now, and the parent must not go NEGATIVE and
    // subsidize other work.
    void fullyPromotedParentFallsBackToTheProxy()
    {
        AppData data;
        const QString cat    = data.addCategory("School", QColor("#4C6FE0"));
        const QString parent = data.addTask("Essay", cat, QDate(2026, 8, 20));
        QVERIFY(data.setTaskSize(parent, 240, false));
        for (int i = 0; i < 2; ++i) {
            const QString p = data.addSubtask(
                parent, QStringLiteral("part %1").arg(i));
            QVERIFY(data.setTaskSize(p, 180, false)); // 2 × 3h > 4h parent
            QVERIFY(data.updateTask(p, QStringLiteral("part %1").arg(i), {},
                                    QDate(2026, 8, 15 + i), QTime(),
                                    Task::Repeat::None,
                                    Task::Priority::Medium));
        }
        const QDateTime now(QDate(2026, 8, 1), QTime(10, 0));
        const afford::Report r =
            afford::affordability(data, *data.taskById(parent), now, {}, 1.0);
        QCOMPARE(r.minutesEstimated, 0);
        QVERIFY(!r.estimateBased); // proxy basis, stated honestly
        QCOMPARE(r.minutesPromoted, 360);
    }
};

QTEST_GUILESS_MAIN(TestDomain)
#include "test_domain.moc" // moc output for a Q_OBJECT declared in a .cpp
