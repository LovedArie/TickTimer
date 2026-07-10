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
#include "Compare.h"
#include "Version.h"
#include "Stats.h"
#include "TrackerService.h"
#include "JsonStore.h"

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
                                        QDate(2026, 8, 9), Task::Repeat::Weekly));
            // A task must keep a real title: an all-space title is refused,
            // and refusal leaves the task untouched.
            QVERIFY(!original.updateTask(id, "   ", "x", QDate(),
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
};

QTEST_GUILESS_MAIN(TestDomain)
#include "test_domain.moc" // moc output for a Q_OBJECT declared in a .cpp
