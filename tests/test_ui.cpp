// ---------------------------------------------------------------------------
// test_ui.cpp — the project's first UI regression test.
//
// Born from a real crash: typing a task name in the Activities detail panel
// and pressing Enter killed the app (the task itself survived — it was
// already saved). The domain tests could never catch it, because the bug
// lives entirely in widget lifetime, not in any rule:
//
//     returnPressed  ->  addTask()  ->  changed() [DIRECT connection]
//         ->  rebuildDetail()  ->  delete the old detail panel
//         ->  ...which contains the very QLineEdit whose signal handler
//             is STILL EXECUTING on the call stack
//         ->  control unwinds into freed memory  ->  crash.
//
// This file drives the REAL page, offscreen, exactly like a finger would:
// select the category, type, press Enter. Before the fix it crashes here;
// after the fix (deleteLater instead of delete) it must stay green forever.
//
// Note what makes UI tests different from the domain suite: they need a
// QApplication and real widgets (QTEST_MAIN provides the former because we
// link Qt6::Widgets), and they run on the "offscreen" platform so no screen
// is required — same trick as the screenshot tool. Slower and heavier than
// domain tests, which is why the domain suite carries the bulk of the cases
// and this one is reserved for bugs that only a living widget tree can
// express. (Per-suite counts deliberately not quoted here — they belong in
// tests/README.md, which carries the command that re-derives them; the
// figure that used to sit in this sentence was ~5x stale by v29.1.)
// ---------------------------------------------------------------------------

#include "ActivitiesPage.h"
#include "AgendaWidget.h"
#include "TaskDetailDialog.h" // v28.5 — the piece-panel navigation tests
#include "TaskDetailPanel.h"  // v28.6 — the docked panel tests
#include "TaskDetailForm.h"   // v28.6.2 — the background-fill pin
#include "ArchivePage.h"
#include "ChatPage.h"
#include "ChatClient.h"
#include "AppData.h"
#include "CompareDialog.h"
#include "EventDialog.h"
#include "GlancePanel.h"
#include "JsonStore.h"
#include "LoginDialog.h"
#include "UpdateBanner.h"
#include "TrackerService.h"
#include "QuickCaptureOverlay.h"
#include "UpcomingPage.h"
#include "TaskListModel.h"
#include "TaskFilterProxy.h"
#include "TaskCardDelegate.h"
#include "EventDialog.h"
#include "NotificationToast.h"
#include "PomodoroEngine.h"
#include "PomodoroLink.h"
#include "PomodoroMiniWindow.h"
#include "NeedsBlockCard.h"
#include "SlidePanel.h"
#include "MainWindow.h"
#include "PlannerPage.h"
#include "PomodoroPage.h"
#include "Prefs.h"
#include "Widgets.h"
#include "LlmProvider.h"
#include "SettingsDialog.h"
#include "SettingsPages.h"
#include "AffordabilityService.h" // v28.10 — the debug panel tests
#include "CheckInService.h"       // v28.10 — forceOffer
#include "ChatPage.h"             // v29.0 — the write boundary
#include "DebugPanel.h"           // v28.10
#include "ProposalCard.h"         // v29.0
#include "CatchUpCard.h"
#include "MissedBlocks.h"
#include "WeekAgendaView.h"

#include <QPointer>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>

#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QToolButton>
#include <QDialogButtonBox>
#include <QScrollArea>
#include <QStackedWidget>
#include <QSettings>
#include <QToolButton>
#include <QListWidget>
#include <QTreeWidget>
#include <QListView>
#include <QStyleOptionViewItem>
#include <QMouseEvent>
#include <QApplication>
#include <QtTest>

class TestUi : public QObject
{
    Q_OBJECT

private slots:
    // v28.3.2 — THE settings fix. Every QSettings() in app and test code is
    // scoped by QCoreApplication's organizationName AND applicationName.
    // The real app sets both (main.cpp); test_nlp sets both and its settings
    // tests pass; this suite only ever set the APPLICATION name — the
    // organization stayed empty, and on the owner's Qt 6.11 / Windows setup
    // an empty-organization QSettings simply doesn't persist. The symptom
    // was one signature behind fifteen different failures: "wrote a value
    // (or planted one), read it back empty". One process-wide fix here; the
    // per-test setApplicationName calls scattered below become harmless
    // repetition of the same value.
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("TickTimerTest"));
        QCoreApplication::setApplicationName(QStringLiteral("TickTimerTest"));
    }

    void addingATaskFromTheDetailPanelDoesNotCrash()
    {
        AppData data;
        data.addCategory("School", QColor("#4C6FE0"));

        ActivitiesPage page(&data);
        page.resize(900, 600);
        page.show();

        // Select the life area in the rail, the way a click would — this is
        // what makes the detail panel build its "+ Add a task…" input.
        auto* rail = page.findChild<QTreeWidget*>();
        QVERIFY(rail);
        QVERIFY(rail->topLevelItemCount() > 0);
        rail->setCurrentItem(rail->topLevelItem(0));

        // Find the task input by its placeholder — resilient to layout
        // changes, tied only to the text a user actually sees.
        QLineEdit* input = nullptr;
        const auto edits = page.findChildren<QLineEdit*>();
        for (QLineEdit* e : edits)
            if (e->placeholderText().startsWith(QStringLiteral("+ Add a task"))) {
                input = e;
                break;
            }
        QVERIFY(input);

        // The exact gesture that crashed: type, Enter. The QPointer is the
        // key to a DETERMINISTIC test: use-after-free is undefined behaviour
        // and may "pass" by allocator luck (it did here, even under ASAN —
        // while crashing reliably on Windows/MinGW). So instead of hoping
        // for a crash, assert the CONTRACT the crash violated: widgets whose
        // signal is still on the stack must not be destroyed synchronously.
        // QPointer nulls itself the moment its QObject dies — if it's null
        // right after keyClick returns, the panel was deleted mid-signal.
        // The exact gesture that once crashed: type, Enter. Under the old
        // rebuild-the-whole-panel design this could free the input mid-signal
        // (use-after-free). The v20.2 model/view conversion makes the input a
        // PERSISTENT widget — only the task LIST's model updates — so the crash
        // class is gone by construction, not by careful deferral. QPointer nulls
        // itself when its QObject dies; here it must stay valid throughout.
        QPointer<QLineEdit> aliveDuringSignal(input);
        QTest::keyClicks(input, "Lab 4");
        QTest::keyClick(input, Qt::Key_Return);
        QVERIFY2(!aliveDuringSignal.isNull(),
                 "the add-task input was destroyed during its own signal");

        // Drain the event loop and assert the STRONGER invariant the refactor
        // buys: the input is never destroyed at all (persistent), so there is no
        // deferred death to wait for. Previously this asserted the panel died
        // later-but-safely; now it must simply still be here.
        QTest::qWait(50);
        QVERIFY2(!aliveDuringSignal.isNull(),
                 "the persistent input must survive the rebuild entirely");

        // And the feature still works: the task exists, exactly one of it.
        QCOMPARE(data.tasks().size(), 1);
        QCOMPARE(data.tasks()[0].title, QString("Lab 4"));
    }

    // v21: one natural-language line through the REAL input becomes a fully
    // dressed task — title, date, priority, repeat, all set from one Enter.
    void quickAddParsesOneLineIntoAFullTask()
    {
        AppData data;
        data.addCategory("School", QColor("#4C6FE0"));

        ActivitiesPage page(&data);
        page.resize(900, 600);
        page.show();
        auto* rail = page.findChild<QTreeWidget*>();
        QVERIFY(rail && rail->topLevelItemCount() > 0);
        rail->setCurrentItem(rail->topLevelItem(0));

        QLineEdit* input = nullptr;
        for (QLineEdit* e : page.findChildren<QLineEdit*>())
            if (e->placeholderText().startsWith(QStringLiteral("+ Add a task")))
                input = e;
        QVERIFY(input);

        QTest::keyClicks(input, "Lab 4 tomorrow urgent weekly");
        QTest::keyClick(input, Qt::Key_Return);

        QCOMPARE(data.tasks().size(), 1);
        const Task& t = data.tasks()[0];
        QCOMPARE(t.title, QStringLiteral("Lab 4")); // facet words consumed
        QCOMPARE(t.dueDate, QDate::currentDate().addDays(1));
        QCOMPARE(t.priority, Task::Priority::Urgent);
        QCOMPARE(t.repeat, Task::Repeat::Weekly);
        QVERIFY(input->text().isEmpty()); // input cleared, ready for the next
    }

    // v21: a '#tag' re-routes the task to the NAMED life area, overriding the
    // rail selection — capture without switching context first.
    void quickAddHashTagRoutesToNamedCategory()
    {
        AppData data;
        data.addCategory("School", QColor("#4C6FE0")); // selected by default
        const QString health = data.addCategory("Health", QColor("#2F7E6E"));

        ActivitiesPage page(&data);
        page.resize(900, 600);
        page.show();
        auto* rail = page.findChild<QTreeWidget*>();
        QVERIFY(rail && rail->topLevelItemCount() > 0);
        rail->setCurrentItem(rail->topLevelItem(0)); // School is selected...

        QLineEdit* input = nullptr;
        for (QLineEdit* e : page.findChildren<QLineEdit*>())
            if (e->placeholderText().startsWith(QStringLiteral("+ Add a task")))
                input = e;
        QVERIFY(input);

        QTest::keyClicks(input, "run 5k #health"); // ...but the tag says Health
        QTest::keyClick(input, Qt::Key_Return);

        QCOMPARE(data.tasks().size(), 1);
        QCOMPARE(data.tasks()[0].title, QStringLiteral("run 5k"));
        QCOMPARE(data.tasks()[0].categoryId, health);
    }

    // v21: the live preview appears while typing, shows the parse, and hides
    // when the input empties — the "see it before you commit it" contract.
    void quickAddPreviewFollowsTyping()
    {
        AppData data;
        data.addCategory("School", QColor("#4C6FE0"));

        ActivitiesPage page(&data);
        page.resize(900, 600);
        page.show();
        auto* rail = page.findChild<QTreeWidget*>();
        QVERIFY(rail && rail->topLevelItemCount() > 0);
        rail->setCurrentItem(rail->topLevelItem(0));

        QLineEdit* input = nullptr;
        for (QLineEdit* e : page.findChildren<QLineEdit*>())
            if (e->placeholderText().startsWith(QStringLiteral("+ Add a task")))
                input = e;
        QVERIFY(input);

        QTest::keyClicks(input, "Lab 4 friday urgent");

        // The preview is the label whose rich text carries the parsed pieces.
        QLabel* preview = nullptr;
        for (QLabel* l : page.findChildren<QLabel*>())
            if (l->isVisible() && l->text().contains(QStringLiteral("URGENT")))
                preview = l;
        QVERIFY2(preview, "typing must surface a visible parse preview");
        QVERIFY(preview->text().contains(QStringLiteral("Lab 4")));

        input->clear(); // emptying the input must hide the preview again
        QVERIFY(!preview->isVisible());
    }

    void blockLabelIsMultilineAndCommitsOnFocusOut()
    {
        // The label box grew multiline (owner request), which changed its
        // SAVE CONTRACT: Enter now inserts a newline (so no editingFinished
        // exists), and the commit happens on focus-out instead. This test
        // pins both halves: Enter must NOT save-and-close the edit, and
        // clicking away MUST persist exactly what was typed — newline and
        // all.
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        const QString ev  = data.addEvent(QDate(2026, 7, 6), 540, 630, act);

        TrackerService tracker(&data);
        EventDialog dialog(&data, &tracker, ev);
        dialog.show();
        // Focus events only flow inside the ACTIVE window — and on the
        // offscreen platform nothing activates windows for you. Without
        // this, setFocus() silently does nothing, no focusOut ever fires,
        // and the commit path under test never runs. (Cost an iteration:
        // the test failed with title == "" until the window was activated.)
        dialog.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&dialog));

        // Two QPlainTextEdits live in the dialog now (label + note) —
        // find ours by the placeholder the user actually sees.
        QPlainTextEdit* label = nullptr;
        QPlainTextEdit* other = nullptr;
        const auto edits = dialog.findChildren<QPlainTextEdit*>();
        for (QPlainTextEdit* e : edits)
            (e->placeholderText().startsWith(QStringLiteral("Label")) ? label
                                                                      : other) = e;
        QVERIFY(label);
        QVERIFY(other);

        label->setFocus();
        QTest::keyClicks(label, "QUIZ #2");
        QTest::keyClick(label, Qt::Key_Return);   // newline, not "done"
        QTest::keyClicks(label, "chapters 3-5");

        // Nothing saved yet — Enter no longer means commit.
        QCOMPARE(data.eventById(ev)->title, QString());

        other->setFocus(); // "clicking away" — focus-out fires the commit
        QTest::qWait(20);
        QCOMPARE(data.eventById(ev)->title,
                 QString("QUIZ #2\nchapters 3-5"));
    }

    void liveDistractedTimeIsNotCountedAsBreak()
    {
        // The owner's exact complaint: while a DISTRACTED timer ran, the
        // glance panel's BREAK box ticked up — a stale two-way live split
        // ("not focusing" == "on break") in one display. The domain always
        // kept three buckets; only the panel lied. This pins the truth at
        // the level the user actually sees: the box captions and values.
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        // This test is about ATTRIBUTION, not liveness — the clock is
        // pinned to noon via the seam and the block spans the domain's
        // whole day (06:00–24:00; 0–1440 is refused at the door).
        // HISTORY (v19.6): this test used to qWait(1100) for real seconds,
        // because liveSeconds() secretly bypassed nowProvider and read the
        // wall clock — the one hole in the seam. When the hole was sealed,
        // this test went red: it had been passing FOR THE WRONG REASON.
        // Now the fake clock advances by hand, like every other test —
        // faster (no sleep) and honest.
        const QString ev  = data.addEvent(QDate::currentDate(), 360, 1440, act);

        TrackerService tracker(&data);
        QDateTime now(QDate::currentDate(), QTime(12, 0));
        tracker.nowProvider = [&now] { return now; };
        tracker.startDistracted(ev);

        GlancePanel panel(&data, &tracker);
        panel.show();
        now = now.addSecs(61);               // a minute of LIVE distraction
        panel.setDate(QDate::currentDate()); // public door that re-derives

        // Read a StatBox's value by its caption (captions render UPPERCASE;
        // the value is the box's other label). Searching by what the user
        // sees keeps the test honest and layout-independent.
        const auto valueOf = [&panel](const QString& caption) -> QString {
            const auto labels = panel.findChildren<QLabel*>();
            for (QLabel* l : labels)
                if (l->text() == caption.toUpper())
                    for (QLabel* v : l->parentWidget()->findChildren<QLabel*>())
                        if (v != l)
                            return v->text();
            return QStringLiteral("<no such box>");
        };

        // The live second lands in DISTRACTED…
        QVERIFY2(valueOf("Distracted") != QLatin1String("0s")
                     && valueOf("Distracted") != QLatin1String("<no such box>"),
                 qPrintable("distracted box shows: " + valueOf("Distracted")));
        // …and NOT in BREAK (the old bug), nor in FOCUSED.
        QCOMPARE(valueOf("Break"),   QString("0s"));
        QCOMPARE(valueOf("Focused"), QString("0s"));
    }

    void trackingButtonsAreGatedToTheLiveWindow()
    {
        // §3.38's braces: the dialog must make the illegal click
        // unreachable — start buttons disabled unless the block is live,
        // with the state label saying WHY. The clock is injected (11:30),
        // so the verdicts are the owner's literal example: an 11:00 block
        // is trackable, a 5 PM block is not.
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        const QDate today = QDate::currentDate();
        const QString liveEv   = data.addEvent(today,  660,  720, act); // 11–12
        const QString futureEv = data.addEvent(today, 1020, 1080, act); // 17–18

        TrackerService tracker(&data);
        tracker.nowProvider = [today] {
            return QDateTime(today, QTime(11, 30));
        };

        const auto button = [](EventDialog& d, const QString& text) {
            const auto buttons = d.findChildren<QPushButton*>();
            for (QPushButton* b : buttons)
                if (b->text() == text)
                    return b;
            return static_cast<QPushButton*>(nullptr);
        };

        {
            EventDialog live(&data, &tracker, liveEv);
            live.show();
            QVERIFY(button(live, "Start focus")->isEnabled());
            QVERIFY(button(live, "Distracted")->isEnabled());
        }
        {
            EventDialog future(&data, &tracker, futureEv);
            future.show();
            QVERIFY(!button(future, "Start focus")->isEnabled());
            QVERIFY(!button(future, "Take a break")->isEnabled());
            QVERIFY(!button(future, "Distracted")->isEnabled());
            // And the label explains itself instead of just greying out.
            bool hintFound = false;
            const auto labels = future.findChildren<QLabel*>();
            for (QLabel* l : labels)
                if (l->text().contains(QStringLiteral("tracking opens at")))
                    hintFound = true;
            QVERIFY(hintFound);
        }
    }

    void compareDialogShowsBothSidesFromABlob()
    {
        // The compare data path, end to end minus the network: MY live
        // AppData on one side, a peer's planner as a raw JSON blob on the
        // other — exactly the shape ShareClient::peerPlannerReady delivers.
        // If the dialog can turn the blob back into numbers that match the
        // domain suite's arithmetic, the whole client half of the feature
        // holds together.
        const QDate day(2026, 7, 6);
        const QDateTime t0(day, QTime(9, 0));
        const auto seg = [&](SegmentKind kind, int startMin, int minutes) {
            Segment s;
            s.kind  = kind;
            s.start = t0.addSecs(startMin * 60);
            s.end   = s.start.addSecs(minutes * 60);
            return s;
        };

        AppData mine;
        const QString mc = mine.addCategory("Work", QColor("#4C6FE0"));
        const QString ma = mine.addActivity("Study", mc);
        const QString me = mine.addEvent(day, 540, 720, ma);
        mine.appendSegment(me, seg(SegmentKind::Focus, 0, 60));

        AppData peerSource; // stands in for the friend's device
        const QString pc = peerSource.addCategory("Health", QColor("#4CAF50"));
        const QString pa = peerSource.addActivity("Walk", pc);
        const QString pe = peerSource.addEvent(day, 540, 720, pa);
        peerSource.appendSegment(pe, seg(SegmentKind::Focus, 0, 25));
        // Serialize → blob, as if it had crossed the wire.
        const QJsonObject blob = JsonStore::toJsonObject(peerSource);

        AppData* minePtr = &mine;
        TrackerService tracker(minePtr); // real service — the dialog wires
                                         // EventDialog and live paint to it
        CompareDialog dialog(minePtr, &tracker, "me", "mom", blob);
        dialog.showDay(day); // pin the date — never trust "today" in a test
        dialog.show();

        // v2 is a planning screen: two REAL agendas exist, and exactly one
        // of them is deaf to the mouse — the peer's. That attribute IS the
        // read-only design, so it's pinned here by name.
        const auto agendas = dialog.findChildren<AgendaWidget*>();
        QCOMPARE(agendas.size(), 2);
        int untouchable = 0;
        for (AgendaWidget* a : agendas)
            if (a->testAttribute(Qt::WA_TransparentForMouseEvents))
                ++untouchable;
        QCOMPARE(untouchable, 1);

        // Read what a user would read: 1h vs 25m, and a +35m focus delta.
        QStringList texts;
        const auto labels = dialog.findChildren<QLabel*>();
        for (QLabel* l : labels)
            texts << l->text();
        QVERIFY(texts.contains(QStringLiteral("1h 00m")));   // my focus
        QVERIFY(texts.contains(QStringLiteral("25m")));      // their focus
        QVERIFY(texts.contains(QStringLiteral("+35m")));     // the delta
        // 35m clears the 5-minute tolerance, so the headline says Ahead.
        bool headlineFound = false;
        for (const QString& t : texts)
            if (t.contains(QStringLiteral("more than mom")))
                headlineFound = true;
        QVERIFY(headlineFound);
    }

    void compareDialogPinsIdentitiesOutsideTheScroll()
    {
        // The owner-reported confusion: the old headers scrolled away WITH
        // the agendas, so at 9 PM two identical columns had no names. The
        // fix is structural — identity labels must live outside the
        // QScrollArea — so the test asserts STRUCTURE, not pixels: find
        // the header labels, then walk their ancestry and require that no
        // QScrollArea sits above them. (A label inside the scrolled host
        // would pass a "text exists" check and still be the bug.)
        AppData mine;
        const QJsonObject blob = JsonStore::toJsonObject(mine);
        TrackerService tracker(&mine);
        CompareDialog dialog(&mine, &tracker, "alice", "mom", blob);

        int alice = 0, mom = 0;
        const auto labels = dialog.findChildren<QLabel*>();
        for (QLabel* l : labels) {
            if (l->text() != QStringLiteral("alice (you)")
                && l->text() != QStringLiteral("mom"))
                continue;
            bool insideScroll = false;
            for (QWidget* w = l->parentWidget(); w; w = w->parentWidget())
                if (qobject_cast<QScrollArea*>(w))
                    insideScroll = true;
            QVERIFY2(!insideScroll,
                     "identity header must not scroll away with the agendas");
            (l->text().startsWith(QStringLiteral("alice")) ? alice : mom)++;
        }
        QCOMPARE(alice, 1); // the pinned header
        QCOMPARE(mom, 2);   // pinned header + the stats grid's column head —
                            // BOTH live outside the scroll, and the loop
                            // above already proved it for each occurrence
    }

    void agendaWindowShrinksTheWidgetButNeverHidesABlock()
    {
        AppData data;
        const QString c = data.addCategory("Work", QColor("#333388"));
        const QString a = data.addActivity("Study", c);
        const QDate day(2026, 3, 10);
        data.addEvent(day, 9 * 60, 10 * 60, a); // 9–10 AM, inside any window

        TrackerService tracker(&data);
        AgendaWidget agenda(&data, &tracker);
        agenda.setDate(day);

        // Height is read through minimumHeight (sizeHint is protected, as a
        // QWidget override usually is): the widget pins its minimum to the
        // hint in syncHeight, so the two are one number by construction.
        // Full-day default: 36 slots — the historical widget, pinned so the
        // preference can never change behaviour for people who never set it.
        const int fullH = AgendaWidget::kTopPad
                          + plan::kSlotsPerDay * AgendaWidget::kSlotHeight + 12;
        QCOMPARE(agenda.minimumHeight(), fullH);

        // Narrow to 8 AM–12 PM: 8 slots tall — the window moves the
        // viewport, never the meaning of a slot index.
        agenda.setVisibleWindow(8 * 60, 12 * 60);
        QCOMPARE(agenda.minimumHeight(),
                 AgendaWidget::kTopPad + 8 * AgendaWidget::kSlotHeight + 12);

        // Data always wins: a 6:00 block outside the window stretches the
        // shown range back to 6 AM (4 extra hours = 8 extra slots) instead
        // of hiding. No call on the widget — the event lands in AppData and
        // the widget notices via changed(); that self-sufficiency IS the
        // assertion.
        data.addEvent(day, 6 * 60, 6 * 60 + 30, a);
        QCOMPARE(agenda.minimumHeight(),
                 AgendaWidget::kTopPad + 12 * AgendaWidget::kSlotHeight + 12);
    }

    void weekViewStartsOnTheDayItIsTold()
    {
        AppData data;
        TrackerService tracker(&data);
        WeekAgendaView view(&data, &tracker);

        // 2026-07-08 is a Wednesday. Monday-first (the default) puts
        // column 0 on Jul 6; Sunday-first re-snaps it to Jul 5. Fixed
        // dates, as always — "today" has no place in a test.
        view.setDate(QDate(2026, 7, 8));
        const auto columns = view.findChildren<AgendaWidget*>();
        QCOMPARE(columns.size(), 7);
        QCOMPARE(columns.first()->date(), QDate(2026, 7, 6));

        view.setFirstDayOfWeek(Qt::Sunday);
        QCOMPARE(columns.first()->date(), QDate(2026, 7, 5));
        QCOMPARE(columns.last()->date(), QDate(2026, 7, 11)); // Sat closes it
    }

    void miniTimerIsASecondFaceOfTheSameEngine()
    {
        // The claim under test is SHARED STATE: a gesture on the mini card
        // must move the one engine, and an engine tick must repaint the
        // card. If someone ever gives the mini its own QTimer, this fails.
        PomodoroEngine engine;
        engine.setDurations(25, 5, 15);
        PomodoroMiniWindow mini(&engine);

        auto* play = mini.findChild<QPushButton*>();
        QVERIFY(play);
        QCOMPARE(engine.running(), false);
        play->click();
        QCOMPARE(engine.running(), true); // the card drives the engine...

        auto* time = mini.findChild<QLabel*>();
        QVERIFY(time);
        QCOMPARE(time->text(), QStringLiteral("25:00"));
        engine.tickOneSecond();
        QCOMPARE(time->text(), QStringLiteral("24:59")); // ...and back

        // The phase label doubles as Skip — find the "Focus ›" tool button
        // among the ghosts by its text.
        QToolButton* phase = nullptr;
        for (auto* b : mini.findChildren<QToolButton*>())
            if (b->text().startsWith(QStringLiteral("Focus")))
                phase = b;
        QVERIFY(phase);
        phase->click();
        QCOMPARE(engine.phase(), PomodoroEngine::Phase::ShortBreak);
    }

    void miniTimerSurvivesTheMainWindowMinimizing()
    {
        // The v19.5.1 bug, pinned structurally (offscreen CI can't observe
        // real Win32 stacking, but it CAN forbid the arrangement that
        // caused it): a parent would make the card an OWNED window on
        // Windows, and owned windows hide when their owner minimizes. So:
        // no parent, ever. And since parentless windows count toward
        // quit-on-last-window-closed, the card must opt out — or closing
        // the main window would leave a zombie app running one tiny card.
        PomodoroEngine engine;
        PomodoroMiniWindow mini(&engine);
        QVERIFY2(mini.parentWidget() == nullptr,
                 "a parent would re-create the hides-with-owner bug");
        QVERIFY(!mini.testAttribute(Qt::WA_QuitOnClose));
        QVERIFY(mini.windowFlags() & Qt::WindowStaysOnTopHint);
        QVERIFY(mini.windowFlags() & Qt::FramelessWindowHint);
    }

    void pomodoroPageTellsTheEngineItsDurations()
    {
        QCoreApplication::setApplicationName(QStringLiteral("TickTimerTest"));
        QSettings().remove(QStringLiteral("pomodoro/focusMinutes"));
        QSettings().remove(QStringLiteral("pomodoro/drivesTracker"));

        AppData data;
        TrackerService tracker(&data);
        PomodoroEngine engine;
        PomodoroLink link(&engine, &tracker);
        PomodoroPage page(&engine, &link, &tracker, &data);

        // The page read the (default) settings and TOLD the engine.
        QCOMPARE(engine.phaseTotalSeconds(), 25 * 60);

        // Spinning the focus box re-tells it — the engine never touches
        // QSettings itself, so this hand-off IS the feature.
        auto* focusSpin = page.findChild<QSpinBox*>();
        QVERIFY(focusSpin);
        focusSpin->setValue(26);
        QCOMPARE(engine.phaseTotalSeconds(), 26 * 60);
        QCOMPARE(engine.remaining(), 26 * 60); // idle clock reshapes now

        QSettings().remove(QStringLiteral("pomodoro/focusMinutes"));
    }

    void pomodoroPageNarratesWhatTheLinkIsDoing()
    {
        // v19.6, the owner's report distilled: the link followed every rule
        // and SAID nothing — invisible correctness reads as broken. This
        // walks the status line through its four sentences and, crucially,
        // checks each names the actor: what the machine is doing, or which
        // human act it's waiting for.
        QCoreApplication::setApplicationName(QStringLiteral("TickTimerTest"));
        QSettings().remove(QStringLiteral("pomodoro/drivesTracker"));

        AppData data;
        const QString cat = data.addCategory("Work", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study PHY335", cat);
        const QDateTime t0(QDate(2026, 7, 13), QTime(10, 15));
        const QString ev = data.addEvent(t0.date(), 10 * 60, 12 * 60, act);

        TrackerService tracker(&data);
        tracker.nowProvider = [t0] { return t0; }; // inside the block
        PomodoroEngine engine;
        PomodoroLink link(&engine, &tracker);
        PomodoroPage page(&engine, &link, &tracker, &data);

        auto* status = page.findChild<QLabel*>(QString());
        // The status label is found by content, not position — the page
        // has several labels, and creation order is layout detail.
        const auto statusLabel = [&page]() -> QLabel* {
            for (auto* l : page.findChildren<QLabel*>())
                if (l->text().contains(QStringLiteral("Link "))
                    || l->text().contains(QStringLiteral("Driving"))
                    || l->text().contains(QStringLiteral("Tracking")))
                    return l;
            return nullptr;
        };
        Q_UNUSED(status);

        QVERIFY(statusLabel());
        // v22 flipped the shipped default: the link starts ON (owner
        // request), so a fresh page narrates the link's active state, not
        // "Link off". This assertion breaking was the default flip WORKING —
        // the test now pins the new contract instead of the old one.
        QVERIFY(!statusLabel()->text().contains(QStringLiteral("Link off")));

        // Tick the box: a block IS planned for right now (10:15 sits in
        // 10–12), so the offer must be CONCRETE — name the block, name
        // the one action (v19.7 adoption).
        QCheckBox* linkCheck = nullptr;
        for (auto* c : page.findChildren<QCheckBox*>())
            if (c->text().contains(QStringLiteral("Drive")))
                linkCheck = c;
        QVERIFY(linkCheck);
        QVERIFY(linkCheck->isChecked()); // v22: on by default, per prefs
        linkCheck->setChecked(false);    // walk the OFF narration first…
        QVERIFY(statusLabel()->text().contains(QStringLiteral("Link off")));
        linkCheck->setChecked(true);     // …then the transitions below
        QVERIFY(statusLabel()->text().contains("Study PHY335"));
        QVERIFY(statusLabel()->text().contains("planned for right now"));

        // Track the block (Pomodoro not engaged): the hand-off sentence.
        tracker.startFocus(ev);
        QVERIFY(statusLabel()->text().contains("Study PHY335"));
        QVERIFY(statusLabel()->text().contains("press Start"));

        // Engage: the driving sentence, kind included.
        engine.start();
        QVERIFY(statusLabel()->text().contains("Driving"));
        QVERIFY(statusLabel()->text().contains("recording focus"));

        // Pause: the owner's exact confusion, now written on the page.
        engine.pause();
        QVERIFY(statusLabel()->text().contains("Pomodoro paused"));

        QSettings().remove(QStringLiteral("pomodoro/drivesTracker"));
    }

    void toastIsAWindowNoPipelineCanEat()
    {
        // v19.9: tray showMessage SUBMITS a balloon the OS may silently
        // decline (the owner's 12:00). This card is a plain window — so
        // the test pins the properties that make it (a) unsuppressable,
        // (b) polite, and (c) fire-and-forget. Structure again, not
        // pixels: any parent, or a missing attribute, re-opens a known
        // failure class (v19.5.1's owner-hides-owned-windows for the
        // parent; focus theft for ShowWithoutActivating; zombie app for
        // QuitOnClose).
        QPointer<NotificationToast> first =
            NotificationToast::show("Starting now", "Lunch · 12–2", 60000);
        QVERIFY(first);
        QVERIFY2(first->parentWidget() == nullptr,
                 "a parent would hide the toast with a minimized owner");
        QVERIFY(first->testAttribute(Qt::WA_ShowWithoutActivating));
        QVERIFY(!first->testAttribute(Qt::WA_QuitOnClose));
        QVERIFY(first->windowFlags() & Qt::WindowStaysOnTopHint);
        QVERIFY(first->isVisible());

        // A second toast stacks BELOW the first — simultaneous voices
        // (block finished + lunch starting is a real 12:00) must share
        // the corner, not each other's pixels.
        QPointer<NotificationToast> second =
            NotificationToast::show("Study PHY335 finished",
                                    "Tracking stopped.", 60000);
        QVERIFY(second->y() > first->y());
        QCOMPARE(second->x(), first->x());

        // Click = dismiss, and dismissal is DELETION (fire-and-forget):
        // the QPointer must null out once events run.
        QTest::mouseClick(first, Qt::LeftButton);
        QApplication::processEvents(QEventLoop::AllEvents, 100);
        QVERIFY(!first);

        // The survivor closes ranks to the top slot.
        QCOMPARE(second->y(),
                 QGuiApplication::primaryScreen()->availableGeometry().top()
                     + 16);
        second->close();
        QApplication::processEvents(QEventLoop::AllEvents, 100);
        QVERIFY(!second);
    }

    void eventDialogRepeatComboAppliesOnChange()
    {
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        const QString ev  = data.addEvent(QDate(2026, 7, 13),
                                          9 * 60, 10 * 60, act);
        TrackerService tracker(&data);
        EventDialog dialog(&data, &tracker, ev);

        // Find the repeat combo by its distinctive first item — the dialog
        // has another combo (segment kind), and position is layout detail.
        QComboBox* repeat = nullptr;
        for (auto* c : dialog.findChildren<QComboBox*>())
            if (c->itemText(0).contains(QStringLiteral("Does not repeat")))
                repeat = c;
        QVERIFY(repeat);
        QCOMPARE(repeat->currentIndex(), 0); // None, faithfully shown

        // This dialog is a control panel, not a form: the change applies
        // NOW, no OK anywhere in the transaction.
        repeat->setCurrentIndex(int(Task::Repeat::Weekly));
        QCOMPARE(data.eventById(ev)->repeat, Task::Repeat::Weekly);
    }

    void settingsDialogWritesPrefsOnlyOnOk()
    {
        QCoreApplication::setApplicationName(QStringLiteral("TickTimerTest"));
        QSettings().remove(QStringLiteral("agenda/startMinutes"));
        QSettings().remove(QStringLiteral("agenda/endMinutes"));
        QSettings().remove(QStringLiteral("week/firstDay"));

        // Untouched keys read back as the historical defaults — the
        // "absence of a preference must mean the old behaviour" rule.
        QCOMPARE(prefs::agendaWindow().first, plan::kDayStartMinutes);
        QCOMPARE(prefs::agendaWindow().second, plan::kDayEndMinutes);
        QCOMPARE(prefs::firstDayOfWeek(), Qt::Monday);

        SettingsDialog dialog;
        // By NAME, not by child order (v21.4): the dialog grew a whole
        // needs-a-block section, and `combos.size() == 3` was a layout
        // assumption pretending to be a test. Names survive growth.
        auto* startCombo = dialog.findChild<QComboBox*>(
            QStringLiteral("startHourCombo"));
        auto* endCombo = dialog.findChild<QComboBox*>(
            QStringLiteral("endHourCombo"));
        auto* weekCombo = dialog.findChild<QComboBox*>(
            QStringLiteral("weekStartCombo"));
        QVERIFY(startCombo && endCombo && weekCombo);
        startCombo->setCurrentIndex(startCombo->findData(7));      // 7 AM
        endCombo->setCurrentIndex(endCombo->findData(22));         // 10 PM
        weekCombo->setCurrentIndex(weekCombo->findData(int(Qt::Sunday)));
        auto* alarmCheck = dialog.findChild<QCheckBox*>(
            QStringLiteral("blockAlarmCheck"));
        QVERIFY(alarmCheck);
        QVERIFY(alarmCheck->isChecked()); // default ON, like the pref
        alarmCheck->setChecked(false);

        // Selecting is not saving (the Pomodoro persist-on-use rule): the
        // keys must still be untouched until OK.
        QCOMPARE(prefs::agendaWindow().first, plan::kDayStartMinutes);

        // Press the REAL OK button — driving accept() directly would pass
        // even if the button were never wired.
        QDialogButtonBox* box = dialog.findChild<QDialogButtonBox*>();
        QVERIFY(box);
        box->button(QDialogButtonBox::Ok)->click();

        QCOMPARE(prefs::agendaWindow().first, 7 * 60);
        QCOMPARE(prefs::agendaWindow().second, 22 * 60);
        QCOMPARE(prefs::firstDayOfWeek(), Qt::Sunday);
        QCOMPARE(prefs::blockStartNotify(), false);

        // Leave no trace for other tests (and other runs).
        QSettings().remove(QStringLiteral("agenda/startMinutes"));
        QSettings().remove(QStringLiteral("agenda/endMinutes"));
        QSettings().remove(QStringLiteral("week/firstDay"));
        QSettings().remove(QStringLiteral("agenda/notifyBlockStart"));
    }

    // ---- v24: the AI provider section --------------------------------------
    // The interesting behaviour isn't "does OK write" (settled above) — it's
    // that keys and models are PER PROVIDER, held in an edit buffer, and that
    // switching the combo mid-edit attributes each value to the right vendor.
    void settingsKeepsAKeyPerProvider()
    {
        QCoreApplication::setApplicationName(QStringLiteral("TickTimerTest"));
        QSettings().remove(QStringLiteral("ai"));

        SettingsDialog dialog;
        auto* provider =
            dialog.findChild<QComboBox*>(QStringLiteral("aiProviderCombo"));
        auto* keyEdit =
            dialog.findChild<QLineEdit*>(QStringLiteral("aiKeyEdit"));
        auto* modelEdit =
            dialog.findChild<QLineEdit*>(QStringLiteral("aiModelEdit"));
        QVERIFY(provider && keyEdit && modelEdit);

        // Type an Anthropic key, switch away, type an OpenAI key, switch back.
        provider->setCurrentIndex(provider->findData(QStringLiteral("anthropic")));
        keyEdit->setText(QStringLiteral("ANT-KEY"));
        provider->setCurrentIndex(provider->findData(QStringLiteral("openai")));
        QCOMPARE(keyEdit->text(), QString()); // not the Anthropic one!
        keyEdit->setText(QStringLiteral("OAI-KEY"));
        modelEdit->setText(QStringLiteral("gpt-4o"));
        provider->setCurrentIndex(provider->findData(QStringLiteral("anthropic")));
        QCOMPARE(keyEdit->text(), QStringLiteral("ANT-KEY")); // buffered, intact

        // Nothing written yet — switching providers is not consent to save.
        QVERIFY(QSettings().value(
                    ai::settingsKeyForKey(QStringLiteral("anthropic")))
                    .toString().isEmpty());

        QDialogButtonBox* box = dialog.findChild<QDialogButtonBox*>();
        QVERIFY(box);
        box->button(QDialogButtonBox::Ok)->click();

        // BOTH keys land, not just the one on screen at the end.
        QSettings settings;
        QCOMPARE(settings.value(ai::settingsKeyForKey(
                                    QStringLiteral("anthropic"))).toString(),
                 QStringLiteral("ANT-KEY"));
        QCOMPARE(settings.value(ai::settingsKeyForKey(
                                    QStringLiteral("openai"))).toString(),
                 QStringLiteral("OAI-KEY"));
        QCOMPARE(settings.value(ai::settingsKeyForModel(
                                    QStringLiteral("openai"))).toString(),
                 QStringLiteral("gpt-4o"));
        // The selected provider is the one the app will use next.
        QCOMPARE(ai::configured().id, QStringLiteral("anthropic"));

        QSettings().remove(QStringLiteral("ai"));
    }

    // The address/dialect row is meaningless for a known vendor and essential
    // for a custom one, so it appears and disappears with the choice.
    void settingsShowsTheAddressRowOnlyForACustomEndpoint()
    {
        QCoreApplication::setApplicationName(QStringLiteral("TickTimerTest"));
        QSettings().remove(QStringLiteral("ai"));

        SettingsDialog dialog;
        dialog.show(); // visibility is only meaningful once the parent is shown
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));

        // v28.3.3 — navigate to the ASSISTANT page first. The dialog opens
        // on page 0 and the AI section lives behind the stack; a widget is
        // visible only if every ancestor is, so on a non-current page BOTH
        // of this test's assertions are about the hidden page, not the row:
        // the !isVisible() check passed VACUOUSLY and the isVisible() check
        // could never pass at all. (First light exposed it: this assertion
        // had never actually run.) Found by title, not row number, so a
        // reordering of pages cannot silently re-vacuum the test.
        auto* nav = dialog.findChild<QListWidget*>();
        QVERIFY(nav);
        int assistantRow = -1;
        for (int r = 0; r < nav->count(); ++r)
            if (nav->item(r)->text() == QStringLiteral("Assistant"))
                assistantRow = r;
        QVERIFY(assistantRow >= 0);
        nav->setCurrentRow(assistantRow); // nav drives the stack directly

        auto* provider =
            dialog.findChild<QComboBox*>(QStringLiteral("aiProviderCombo"));
        auto* baseUrl =
            dialog.findChild<QLineEdit*>(QStringLiteral("aiBaseUrlEdit"));
        QVERIFY(provider && baseUrl);

        provider->setCurrentIndex(provider->findData(QStringLiteral("anthropic")));
        QVERIFY(!baseUrl->isVisible()); // now a REAL claim about the row
        provider->setCurrentIndex(provider->findData(QStringLiteral("custom")));
        QVERIFY(baseUrl->isVisible());

        QSettings().remove(QStringLiteral("ai"));
    }

    void loginDialogOwnsTheServerAddress()
    {
        // QSettings needs an application name to resolve a storage backend;
        // a bare test process has none. Set one for this test.
        QCoreApplication::setApplicationName(QStringLiteral("TickTimerTest"));
        QSettings().remove("sync/serverUrl");

        LoginDialog dialog(QStringLiteral("http://localhost:8080"));
        QLineEdit* server = nullptr;
        const auto edits = dialog.findChildren<QLineEdit*>();
        for (QLineEdit* e : edits)
            if (e->text().startsWith(QStringLiteral("http://")))
                server = e; // the only prefilled field on a fresh dialog
        QVERIFY(server);
        QCOMPARE(server->text(), QStringLiteral("http://localhost:8080"));

        // Scheme forgiveness: the single most common way a normal person
        // types an address is without "http://" — the accessor adds it.
        server->setText(QStringLiteral("192.168.1.50:8080"));
        QCOMPARE(dialog.serverUrl(),
                 QStringLiteral("http://192.168.1.50:8080"));

        // Typing alone persists nothing (persist-on-use, not on-type)…
        QVERIFY(!QSettings().contains("sync/serverUrl"));

        // …submitting does. The request itself will fail (nothing listens
        // there) — irrelevant: we're testing that the address was adopted
        // and saved BEFORE the network was even asked.
        const auto fields = dialog.findChildren<QLineEdit*>();
        for (QLineEdit* e : fields) {
            if (e->placeholderText() == QStringLiteral("Username"))
                e->setText(QStringLiteral("someone"));
            if (e->placeholderText() == QStringLiteral("Password"))
                e->setText(QStringLiteral("pw"));
        }
        const auto buttons = dialog.findChildren<QPushButton*>();
        int clicked = 0;
        for (QPushButton* b : buttons)
            if (b->objectName() == QStringLiteral("primary")) {
                b->click(); ++clicked; // the submit button, whatever its label
            }
        QCOMPARE(clicked, 1); // prove we actually found and hit the button
        QCOMPARE(QSettings().value("sync/serverUrl").toString(),
                 QStringLiteral("http://192.168.1.50:8080"));

        QSettings().remove("sync/serverUrl"); // leave no trace
    }

    void loginDialogOpensInLoginMode()
    {
        // Pins the double-flip bug: the old ctor toggled the mode TWICE
        // and opened on "Create account" while its comment claimed login.
        // Returning users outnumber first-timers every day after day one —
        // the door should open on Log in.
        LoginDialog dialog(QStringLiteral("http://localhost:8080"));
        bool loginButtonFound = false;
        for (QPushButton* b : dialog.findChildren<QPushButton*>())
            if (b->objectName() == QStringLiteral("primary")
                && b->text() == QStringLiteral("Log in"))
                loginButtonFound = true;
        QVERIFY(loginButtonFound);
    }

    void updateBannerDismissRemembersTheVersion()
    {
        QCoreApplication::setApplicationName(QStringLiteral("TickTimerTest"));
        QSettings().remove("update/lastDismissed");

        UpdateBanner banner(QStringLiteral("20.0.0"),
                            QStringLiteral("https://example.com/releases"),
                            QStringLiteral("test notes"));
        banner.show();

        // The message a user would read: version and notes, together.
        bool textFound = false;
        const auto labels = banner.findChildren<QLabel*>();
        for (QLabel* l : labels)
            if (l->text().contains(QStringLiteral("20.0.0"))
                && l->text().contains(QStringLiteral("test notes")))
                textFound = true;
        QVERIFY(textFound);

        // Click ✕: the banner hides AND the version is recorded — the two
        // halves of "don't mention this one again". The pure decideBanner
        // rule that consumes this value is pinned in the domain suite; here
        // we pin that the glass actually writes it.
        const auto buttons = banner.findChildren<QPushButton*>();
        int clicked = 0;
        for (QPushButton* b : buttons)
            if (b->objectName() == QStringLiteral("quiet")) {
                b->click(); ++clicked;
            }
        QCOMPARE(clicked, 1);
        QVERIFY(banner.isHidden());
        QCOMPARE(QSettings().value("update/lastDismissed").toString(),
                 QStringLiteral("20.0.0"));

        QSettings().remove("update/lastDismissed"); // leave no trace
    }

    void archivePageRestoresWhatItHolds()
    {
        // The archive round-trip through REAL widgets: archive a done task
        // and an in-use activity, see them greyed on the page, click
        // Restore, watch them leave. The page derives everything from
        // AppData — this test proves the loop closes.
        AppData data;
        const QString c = data.addCategory("School", QColor("#4C6FE0"));
        const QString a = data.addActivity("Old course", c);
        const QString t = data.addTask("Old lab", c, QDate(2026, 8, 1));
        data.setTaskDone(t, true);
        data.setTaskArchived(t, true);
        data.setActivityArchived(a, true);

        ArchivePage page(&data);
        page.show();

        // Both retired things are visible on the page…
        QStringList texts;
        for (QLabel* l : page.findChildren<QLabel*>())
            texts << l->text();
        QVERIFY(texts.contains(QStringLiteral("Old lab")));
        QVERIFY(texts.contains(QStringLiteral("Old course")));

        // …and Restore actually restores (both buttons — task first, then
        // the activity's; the rebuild between clicks means we re-find).
        for (int pass = 0; pass < 2; ++pass) {
            QPushButton* restore = nullptr;
            for (QPushButton* b : page.findChildren<QPushButton*>())
                if (b->text() == QStringLiteral("Restore"))
                    restore = b;
            QVERIFY(restore);
            restore->click();
            QCoreApplication::processEvents(); // let deleteLater settle
        }
        QVERIFY(data.archivedTasks().isEmpty());
        QVERIFY(data.archivedActivities().isEmpty());
        // Restored, not resurrected-blank: the task kept its done state.
        QVERIFY(!data.upcomingTasks().contains(nullptr));
    }

    void upcomingLensesFilterByPriority()
    {
        // Upcoming became a model/view screen (v20): task rows are PAINTED by a
        // delegate, not built as QPushButton widgets — so this test now asks the
        // MODEL what's visible instead of hunting the widget tree. That's a
        // better test anyway: it checks the source of truth the delegate merely
        // renders, and it can't be fooled by a painting bug.
        AppData data;
        const QString c = data.addCategory("School", QColor("#4C6FE0"));
        const QString urgent = data.addTask("Fire", c, QDate(2026, 8, 1));
        data.addTask("Someday", c, QDate(2026, 8, 2)); // stays Medium
        data.setTaskPriority(urgent, Task::Priority::Urgent);

        UpcomingPage page(&data);
        page.show();

        auto* view = page.findChild<QListView*>();
        QVERIFY(view);
        QAbstractItemModel* shown = view->model(); // the TaskFilterProxy

        const auto titlesShown = [&]() {
            QStringList titles;
            for (int r = 0; r < shown->rowCount(); ++r)
                titles << shown->index(r, 0).data(Qt::DisplayRole).toString();
            return titles;
        };

        // The All lens shows both…
        QCOMPARE(titlesShown().size(), 2);

        // …the Urgent lens shows exactly the urgent one. Tabs are still
        // QToolButtons (page chrome, not list rows); find by label and click.
        QToolButton* urgentTab = nullptr;
        for (QToolButton* b : page.findChildren<QToolButton*>())
            if (b->text() == QStringLiteral("Urgent"))
                urgentTab = b;
        QVERIFY(urgentTab);
        urgentTab->click();
        QCoreApplication::processEvents();

        const QStringList filtered = titlesShown();
        QCOMPARE(filtered.size(), 1);
        QCOMPARE(filtered.first(), QStringLiteral("Fire"));
    }

    void upcomingDelegateHitTestsClickZones()
    {
        // A painted row has no child widgets, so the delegate turns a click into
        // a signal by hit-testing which zone was hit. Prove the mapping: body →
        // edit, far-left → done, far-right → delete. We call editorEvent
        // directly with a known row rect — no live view needed.
        AppData data;
        const QString c = data.addCategory("School", QColor("#4C6FE0"));
        const QString id = data.addTask("Lab 4", c, QDate::currentDate());

        TaskListModel model(&data);
        TaskFilterProxy proxy;
        proxy.setSourceModel(&model);
        TaskCardDelegate delegate;

        QSignalSpy doneSpy(&delegate, &TaskCardDelegate::doneToggled);
        QSignalSpy editSpy(&delegate, &TaskCardDelegate::editRequested);
        QSignalSpy delSpy(&delegate, &TaskCardDelegate::deleteRequested);

        // v22.1 lesson: this test hardcoded pixel targets ("click(18, 57)
        // over the checkbox") — then the readability pass moved the
        // furniture (card 62->86px, pad 12->14) and every magic number
        // silently aimed at the wrong zone. A hit test must ask the
        // delegate WHERE things are: the row height comes from sizeHint,
        // and the targets are edges/centre of that measured row.
        QStyleOptionViewItem opt;
        opt.font = QApplication::font();
        const int rowH =
            delegate.sizeHint(opt, proxy.index(0, 0)).height();
        opt.rect = QRect(0, 0, 680, rowH);
        const QModelIndex idx = proxy.index(0, 0);

        const auto click = [&](int x, int y) {
            QMouseEvent e(QEvent::MouseButtonRelease, QPointF(x, y),
                          QPointF(x, y), Qt::LeftButton, Qt::LeftButton,
                          Qt::NoModifier);
            delegate.editorEvent(&e, &proxy, opt, idx);
        };

        // Vertical midline of the CARD (the strip above it is the section
        // header): measured, not remembered.
        const int cardMidY = opt.rect.bottom() - (rowH - 34) / 2;

        click(300, cardMidY);           // deep in the card body → edit
        QCOMPARE(editSpy.count(), 1);
        QCOMPARE(editSpy.first().first().toString(), id);

        click(22, cardMidY);            // far left over the checkbox → done
        QCOMPARE(doneSpy.count(), 1);
        QCOMPARE(doneSpy.first().at(1).toBool(), true);

        click(658, cardMidY);           // far right over the × → delete
        QCOMPARE(delSpy.count(), 1);
        QCOMPARE(delSpy.first().first().toString(), id);
    }
    // ----- v21.1: the global capture overlay --------------------------------

    // No '#tag': the task lands in the remembered DEFAULT category — capture
    // without deciding where, the whole point of a global bar.
    void captureOverlayUsesDefaultCategory()
    {
        AppData data;
        data.addCategory("School", QColor("#4C6FE0"));
        const QString health = data.addCategory("Health", QColor("#2F7E6E"));

        QuickCaptureOverlay overlay(&data);
        overlay.setDefaultCategoryId(health);
        overlay.popup();

        auto* input = overlay.findChild<QLineEdit*>();
        QVERIFY(input);
        QTest::keyClicks(input, "stretch tomorrow");
        QTest::keyClick(input, Qt::Key_Return);

        QCOMPARE(data.tasks().size(), 1);
        QCOMPARE(data.tasks()[0].categoryId, health);   // default, not first
        QCOMPARE(data.tasks()[0].dueDate, QDate::currentDate().addDays(1));
    }

    // A '#tag' overrides the default — and commit BATCHES: the overlay stays
    // open with a cleared input, ready for the next line of the brain-dump.
    void captureOverlayHashTagOverridesAndBatches()
    {
        AppData data;
        const QString school = data.addCategory("School", QColor("#4C6FE0"));
        const QString health = data.addCategory("Health", QColor("#2F7E6E"));

        QuickCaptureOverlay overlay(&data);
        overlay.setDefaultCategoryId(health);
        QSignalSpy captured(&overlay, &QuickCaptureOverlay::taskCaptured);
        overlay.popup();

        auto* input = overlay.findChild<QLineEdit*>();
        QVERIFY(input);
        QTest::keyClicks(input, "lab 4 #school");
        QTest::keyClick(input, Qt::Key_Return);

        QCOMPARE(data.tasks()[0].categoryId, school); // tag beat the default
        QCOMPARE(captured.count(), 1);                // MainWindow's memory hook
        QVERIFY(overlay.isVisible());                 // batch: still open…
        QVERIFY(input->text().isEmpty());             // …input ready for more

        QTest::keyClicks(input, "run 5k");            // second line, no tag
        QTest::keyClick(input, Qt::Key_Return);
        QCOMPARE(data.tasks().size(), 2);
    }

    // A remembered default whose category was DELETED must not ghost-write:
    // commit falls through to the first existing category.
    void captureOverlayStaleDefaultFallsBack()
    {
        AppData data;
        const QString school = data.addCategory("School", QColor("#4C6FE0"));

        QuickCaptureOverlay overlay(&data);
        overlay.setDefaultCategoryId(QStringLiteral("deleted-long-ago"));
        overlay.popup();

        auto* input = overlay.findChild<QLineEdit*>();
        QVERIFY(input);
        QTest::keyClicks(input, "essay outline");
        QTest::keyClick(input, Qt::Key_Return);

        QCOMPARE(data.tasks().size(), 1);
        QCOMPARE(data.tasks()[0].categoryId, school); // rule-3 fallback
    }

    // v21.2: capture is a beat, not a mode — clicking anywhere else (which
    // deactivates the overlay's window) dismisses it. Esc still works too.
    void captureOverlayClickAwayDismisses()
    {
        AppData data;
        data.addCategory("School", QColor("#4C6FE0"));

        QuickCaptureOverlay overlay(&data);
        overlay.popup();
        QVERIFY(overlay.isVisible());

        // A click on any other window arrives at the overlay as exactly this
        // event — sending it directly tests our handler without depending on
        // the offscreen platform's activation quirks.
        QEvent deactivate(QEvent::WindowDeactivate);
        QApplication::sendEvent(&overlay, &deactivate);
        QVERIFY(!overlay.isVisible());

        overlay.popup(); // and Esc, the original exit, still closes
        QVERIFY(overlay.isVisible());
        QTest::keyClick(&overlay, Qt::Key_Escape);
        QVERIFY(!overlay.isVisible());
    }

    // v21.2: Ctrl+Enter with no API key fails FAST, OFFLINE, and the hint
    // says where the fix is — the first failure every new user hits.
    void captureOverlayAiWithoutKeyExplainsItself()
    {
        AppData data;
        data.addCategory("School", QColor("#4C6FE0"));
        QuickCaptureOverlay overlay(&data);
        overlay.popup();

        auto* input = overlay.findChild<QLineEdit*>();
        QVERIFY(input);
        QTest::keyClicks(input, "dentist next week sometime");
        QTest::keyClick(input, Qt::Key_Return, Qt::ControlModifier);

        // No task was committed (Ctrl+Enter asks, never adds) and the hint
        // carries the guidance. (Assumes no ANTHROPIC_API_KEY in the test
        // environment — true for CI and this sandbox.)
        QCOMPARE(data.tasks().size(), 0);
        QLabel* hint = nullptr;
        for (QLabel* l : overlay.findChildren<QLabel*>())
            if (l->text().contains(QStringLiteral("key")))
                hint = l;
        QVERIFY2(hint, "the hint line must explain the missing key");
    }

    // v21.2: an AI reply ARMS the commit — Enter then commits the model's
    // interpretation, not a re-parse of the raw text. Driven through the
    // same slot the client's signal targets: the seam needs no network.
    void captureOverlayAiParseArmsCommit()
    {
        AppData data;
        const QString school = data.addCategory("School", QColor("#4C6FE0"));
        QuickCaptureOverlay overlay(&data);
        overlay.setDefaultCategoryId(school);
        overlay.popup();

        auto* input = overlay.findChild<QLineEdit*>();
        QVERIFY(input);
        // Text the deterministic parser can NOT crack ("end of next week"),
        // which is exactly the fallback's reason to exist.
        QTest::keyClicks(input, "dentist end of next week");

        nlp::ParsedTask ai;
        ai.title = QStringLiteral("dentist");
        ai.dueDate = QDate::currentDate().addDays(9);
        ai.priority = Task::Priority::Low;
        QVERIFY(QMetaObject::invokeMethod(&overlay, "onAiParsed",
                                          Q_ARG(nlp::ParsedTask, ai)));

        QTest::keyClick(input, Qt::Key_Return);
        QCOMPARE(data.tasks().size(), 1);
        QCOMPARE(data.tasks()[0].title, QStringLiteral("dentist")); // AI title
        QCOMPARE(data.tasks()[0].dueDate, ai.dueDate);              // AI date
        QCOMPARE(data.tasks()[0].priority, Task::Priority::Low);
    }

    // v21.2: ANY edit disarms the AI parse — the model answered the OLD
    // text, and committing a stale answer is the drift the preview forbids.
    void captureOverlayEditDisarmsAiParse()
    {
        AppData data;
        const QString school = data.addCategory("School", QColor("#4C6FE0"));
        QuickCaptureOverlay overlay(&data);
        overlay.setDefaultCategoryId(school);
        overlay.popup();

        auto* input = overlay.findChild<QLineEdit*>();
        QVERIFY(input);
        QTest::keyClicks(input, "dentist");

        nlp::ParsedTask ai;
        ai.title = QStringLiteral("WRONG — stale AI answer");
        QVERIFY(QMetaObject::invokeMethod(&overlay, "onAiParsed",
                                          Q_ARG(nlp::ParsedTask, ai)));

        QTest::keyClicks(input, " tomorrow"); // an edit after the AI reply
        QTest::keyClick(input, Qt::Key_Return);

        QCOMPARE(data.tasks().size(), 1);
        // The deterministic parse of the CURRENT text won, not the stale AI.
        QCOMPARE(data.tasks()[0].title, QStringLiteral("dentist"));
        QCOMPARE(data.tasks()[0].dueDate, QDate::currentDate().addDays(1));
    }

    // ---- needs-a-block, part 2: the gated glance panel --------------------
    // (design-addendum-needs-a-block §E.) The domain suite already proves
    // WHAT qualifies; these prove the panel's SHAPE: gate closed -> card
    // instead of numbers; "Show my day" -> numbers plus strip, nothing
    // dismissed; the gate re-arming on the review clock; the escalated
    // row demanding a decision. QSettings hygiene as always: clear the
    // needsBlock/* keys going in AND out, defaults do the rest.

    static void clearNeedsBlockPrefs()
    {
        QCoreApplication::setApplicationName(QStringLiteral("TickTimerTest"));
        QSettings s;
        s.remove(QStringLiteral("needsBlock"));
    }

    void needsBlockGateHoldsTheNumbersUntilReviewed()
    {
        clearNeedsBlockPrefs();
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4A7CC4"));
        const QString id  = data.addTask("Lab 4", cat,
                                         QDate::currentDate().addDays(3));
        data.setTaskPriority(id, Task::Priority::Urgent); // qualifies

        TrackerService tracker(&data);
        GlancePanel panel(&data, &tracker);
        panel.show();
        panel.refresh();

        // Gate CLOSED: the numbers wait, the card is the panel.
        auto* content = panel.findChild<QWidget*>(
            QStringLiteral("glanceContent"));
        QVERIFY(content);
        QVERIFY(!content->isVisible());
        auto* open = panel.findChild<QPushButton*>(
            QStringLiteral("showMyDay"));
        QVERIFY(open);

        open->click(); // one honest look
        QTest::qWait(0); // v22.3: reviewed() is deferred one loop turn so
                         // the rebuild can never tear down the button
                         // mid-click — the test lets that turn happen

        QVERIFY(content->isVisible());               // numbers unlocked…
        QVERIFY(panel.findChild<QPushButton*>(
            QStringLiteral("needsBlockStrip")));     // …task still listed:
        QVERIFY(!data.taskById(id)->dismissedUntil   // NOTHING was dismissed
                     .isValid());                    // (§E: pause, not toll)
        clearNeedsBlockPrefs();
    }

    void needsBlockGateStaysOpenWhenNothingQualifies()
    {
        clearNeedsBlockPrefs();
        AppData data; // no tasks at all
        TrackerService tracker(&data);
        GlancePanel panel(&data, &tracker);
        panel.show();
        panel.refresh();

        auto* content = panel.findChild<QWidget*>(
            QStringLiteral("glanceContent"));
        QVERIFY(content);
        QVERIFY(content->isVisible()); // the panel is what it always was
        QVERIFY(!panel.findChild<QPushButton*>(QStringLiteral("showMyDay")));
        clearNeedsBlockPrefs();
    }

    void needsBlockGateRearmsOnTheReviewClock()
    {
        // The derivation under test: gateOpen = nextReturn(lastReview) >
        // now. Review at 07:00 with a daily-06:00 clock holds until
        // tomorrow 06:00 — at 06:01 the gate must close again. The panel's
        // nowProvider seam is what lets this test live a day in two lines.
        clearNeedsBlockPrefs();
        prefs::setNeedsBlockLastReview(
            QDateTime(QDate::currentDate(), QTime(7, 0)));

        AppData data;
        const QString cat = data.addCategory("School", QColor("#4A7CC4"));
        const QString id  = data.addTask("Lab 4", cat);
        data.setTaskPriority(id, Task::Priority::Urgent);

        TrackerService tracker(&data);
        GlancePanel panel(&data, &tracker);
        QDateTime now(QDate::currentDate(), QTime(12, 0)); // same day, looked
        panel.nowProvider = [&now] { return now; };
        panel.show();
        panel.refresh();

        auto* content = panel.findChild<QWidget*>(
            QStringLiteral("glanceContent"));
        QVERIFY(content->isVisible());                    // still open

        now = QDateTime(QDate::currentDate().addDays(1), QTime(6, 1));
        panel.refresh();                                  // clock passed 06:00
        QVERIFY(!content->isVisible());                   // re-armed
        clearNeedsBlockPrefs();
    }

    void escalatedRowDemandsADecision()
    {
        // §D rung 1: after the threshold, "Not today…" opens the decision
        // menu instead of dismissing in one click — and "the deadline was
        // wrong" ASKS (a signal for the page to open DueDateDialog), it
        // never mutates from inside the card.
        clearNeedsBlockPrefs();
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4A7CC4"));
        const QString id  = data.addTask("Lab 4", cat);
        data.setTaskPriority(id, Task::Priority::Urgent);
        for (int i = 0; i < 3; ++i) // reach the threshold, then return
            data.dismissTask(id, QDateTime::currentDateTime().addSecs(60));
        data.clearDismissal(id);

        TrackerService tracker(&data);
        GlancePanel panel(&data, &tracker);
        panel.show();
        panel.refresh();

        auto* later = panel.findChild<QPushButton*>(
            QStringLiteral("notToday"));
        QVERIFY(later);
        QVERIFY(later->text().endsWith(QStringLiteral("…"))); // the tell

        QSignalSpy deadlineSpy(&panel,
                               &GlancePanel::editDeadlineRequested);
        later->click(); // opens the menu (a rebuild — deleteLater land)
        auto* decide = panel.findChild<QPushButton*>(
            QStringLiteral("decideDeadline"));
        QVERIFY(decide);
        decide->click();
        QCOMPARE(deadlineSpy.count(), 1);
        QCOMPARE(deadlineSpy.takeFirst().at(0).toString(), id);
        // The card itself changed NOTHING — const view, signals only.
        QVERIFY(data.taskById(id)->dueDate.isNull());
        clearNeedsBlockPrefs();
    }

    // ---- needs-a-block, part 3: placement -------------------------------
    // (addendum §H, retired this session.) "Find time" enters placing
    // mode; planAt — the ONE planning step both views route through — is
    // intercepted, so a click on any free slot on any day places the task
    // block directly, no picker dialog (which would deadlock a test and
    // annoy a user in equal measure).

    void findTimePlacesATaskBlockInAFreeRun()
    {
        clearNeedsBlockPrefs();
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4A7CC4"));
        const QString id  = data.addTask("Lab 4", cat,
                                         QDate::currentDate().addDays(3));
        data.setTaskPriority(id, Task::Priority::Urgent);

        TrackerService tracker(&data);
        PlannerPage page(&data, &tracker);
        page.show();

        auto* find = page.findChild<QPushButton*>(QStringLiteral("findTime"));
        QVERIFY(find);
        find->click(); // placing mode on

        auto* banner = page.findChild<QFrame*>(QStringLiteral("placingBanner"));
        QVERIFY(banner && banner->isVisible());

        // Click 9:00 (domain slot 6) — through the real single planning
        // step, exactly as a day click or a week-column click would arrive.
        QVERIFY(QMetaObject::invokeMethod(&page, "planAt",
                                          Q_ARG(QDate, QDate::currentDate()),
                                          Q_ARG(int, 6)));

        QCOMPARE(data.events().size(), 1);
        QCOMPARE(data.events()[0].taskId, id);              // a TASK block
        QCOMPARE(data.events()[0].plannedStartMinutes, 9 * 60);
        QCOMPARE(data.events()[0].plannedEndMinutes, 10 * 60); // 1h default
        QVERIFY(!banner->isVisible());                      // placement done
        // …and the task is COVERED, so the derived list is empty: gate open.
        QVERIFY(page.findChild<QWidget*>(QStringLiteral("glanceContent"))
                    ->isVisible());
        clearNeedsBlockPrefs();
    }

    void dayStripPreselectsTheEarliestDayThatFits()
    {
        // Today is packed solid; the deadline is +2. Entering placement
        // must land on TOMORROW — the earliest day with an hour free
        // before the wire — without the user doing the arithmetic.
        clearNeedsBlockPrefs();
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4A7CC4"));
        for (int m = plan::kDayStartMinutes; m < plan::kDayEndMinutes;
             m += 120)
            data.addAdHocEvent(QDate::currentDate(), m, m + 120, "busy");
        const QString id = data.addTask("Lab 4", cat,
                                        QDate::currentDate().addDays(2));
        data.setTaskPriority(id, Task::Priority::Urgent);

        TrackerService tracker(&data);
        PlannerPage page(&data, &tracker);
        page.show();
        page.findChild<QPushButton*>(QStringLiteral("findTime"))->click();

        QPushButton* checked = nullptr;
        const auto days =
            page.findChildren<QPushButton*>(QStringLiteral("placeDay"));
        QVERIFY(days.size() >= 3); // today, +1, +2, and 2 shown-refused
        for (QPushButton* b : days)
            if (b->isChecked())
                checked = b;
        QVERIFY(checked);
        QCOMPARE(checked->property("dayOffset").toInt(), 1); // tomorrow
        // Today's button is visible but disabled — full, not hidden.
        QCOMPARE(days[0]->property("dayOffset").toInt(), 0);
        QVERIFY(!days[0]->isEnabled());
        clearNeedsBlockPrefs();
    }

    void weekViewSharesTheListAndTheOnePlanningStep()
    {
        // The list is view-independent, so the week tab renders the SAME
        // derived query (a second NeedsBlockCard) — and because planAt is
        // the one planning step, placing from a week column's date needs
        // no new code path at all.
        clearNeedsBlockPrefs();
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4A7CC4"));
        const QString id  = data.addTask("Lab 4", cat,
                                         QDate::currentDate().addDays(3));
        data.setTaskPriority(id, Task::Priority::Urgent);

        TrackerService tracker(&data);
        PlannerPage page(&data, &tracker);
        page.show();

        QCOMPARE(page.findChildren<NeedsBlockCard*>().size(), 2);

        page.findChild<QPushButton*>(QStringLiteral("findTime"))->click();
        // A click on TOMORROW's column, as the week view would deliver it:
        QVERIFY(QMetaObject::invokeMethod(
            &page, "planAt",
            Q_ARG(QDate, QDate::currentDate().addDays(1)), Q_ARG(int, 6)));

        QCOMPARE(data.events().size(), 1);
        QCOMPARE(data.events()[0].date, QDate::currentDate().addDays(1));
        QCOMPARE(data.events()[0].taskId, id);
        clearNeedsBlockPrefs();
    }

    // v22.2 regression: the click-eating rebuild. The glance panel refreshes
    // once per second while a timer runs; if each refresh rebuilds the card,
    // "Show my day" is destroyed and recreated under the user's cursor and a
    // real press→release straddling a tick lands on a corpse. The fix is the
    // fingerprint gate: an unchanged card keeps its widgets. QPointer is the
    // perfect witness — it nulls itself the moment its widget is destroyed,
    // so "still non-null after five refreshes" IS the property under test.
    // (The original bug escaped this suite because open->click() invokes the
    // handler directly, skipping input delivery — a lesson in what a
    // programmatic click does and does not prove.)
    void showMyDaySurvivesLiveTicks()
    {
        clearNeedsBlockPrefs();
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4A7CC4"));
        const QString id  = data.addTask("Lab 4", cat);
        data.setTaskPriority(id, Task::Priority::Urgent);

        TrackerService tracker(&data);
        GlancePanel panel(&data, &tracker);
        panel.show();
        panel.refresh();

        QPointer<QPushButton> open =
            panel.findChild<QPushButton*>(QStringLiteral("showMyDay"));
        QVERIFY(open);

        // Five "ticks" with nothing changed: same fingerprint, same widgets.
        for (int i = 0; i < 5; ++i)
            panel.refresh();
        QVERIFY(open); // the SAME button object survived every refresh

        // And a change that IS visible must still rebuild: the click flips
        // the gate, so the fingerprint differs and the strip appears.
        open->click();
        QTest::qWait(0); // deferred reviewed(): let the loop turn
        QVERIFY(panel.findChild<QWidget*>(QStringLiteral("glanceContent"))
                    ->isVisible());
        QVERIFY(panel.findChild<QPushButton*>(
            QStringLiteral("needsBlockStrip")));
        clearNeedsBlockPrefs();
    }

    // v22.3: the click must open the gate even if the fingerprint gate, the
    // settings round-trip, or the re-derive misbehaves — the handler now
    // states the new state instead of inferring it. Because reviewed() is
    // deferred to the next event-loop turn, the test must let that turn
    // happen: qWait(0) processes the pending singleShot.
    void showMyDayOpensTheGateImmediately()
    {
        clearNeedsBlockPrefs();
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4A7CC4"));
        const QString id  = data.addTask("Lab 4", cat);
        data.setTaskPriority(id, Task::Priority::Urgent);

        TrackerService tracker(&data);
        GlancePanel panel(&data, &tracker);
        panel.show();
        panel.refresh();

        auto* open = panel.findChild<QPushButton*>(
            QStringLiteral("showMyDay"));
        QVERIFY(open);
        open->click();

        // In memory, before any re-derive has run — deliberately NO qWait
        // here: the whole point is that the gate answers honestly the very
        // instant the handler returns, before the deferred signal fires.
        auto* card = panel.findChild<NeedsBlockCard*>();
        QVERIFY(card);
        QVERIFY(!card->gateClosed());

        QTest::qWait(0); // let the deferred reviewed() land
        QVERIFY(panel.findChild<QWidget*>(QStringLiteral("glanceContent"))
                    ->isVisible());
        clearNeedsBlockPrefs();
    }

    // v22.4 (design A): the gate asks for ONE decision. Three tasks qualify,
    // one is presented; the counter offers the survey and expanding it turns
    // the same data back into the plain list — same builder, same actions.
    void gateShowsOneTaskThenTheRestOnRequest()
    {
        clearNeedsBlockPrefs();
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4A7CC4"));
        for (const char* t : {"Lab 4", "Essay draft", "Reading notes"})
            data.setTaskPriority(data.addTask(t, cat),
                                 Task::Priority::Urgent);

        TrackerService tracker(&data);
        GlancePanel panel(&data, &tracker);
        panel.show();
        panel.refresh();

        // One task on stage, not three: "Find time" appears exactly once.
        QCOMPARE(panel.findChildren<QPushButton*>(
                     QStringLiteral("findTime")).size(), 1);

        auto* all = panel.findChild<QPushButton*>(
            QStringLiteral("needsBlockShowAll"));
        QVERIFY(all); // the counter is a button because there IS more
        all->click();
        // Drain deleteLater before counting: the rebuild hides+deleteLater's
        // the old widgets, and findChildren still sees them until the loop
        // turns — the first run of this test counted 4 findTime buttons
        // where 3 exist (three new + one corpse). Count what the USER sees.
        QTest::qWait(0);
        QCOMPARE(panel.findChildren<QPushButton*>(
                     QStringLiteral("findTime")).size(), 3);

        all = panel.findChild<QPushButton*>(
            QStringLiteral("needsBlockShowAll"));
        all->click(); // and back to focus
        QTest::qWait(0); // drain deleteLater before counting (see above)
        QCOMPARE(panel.findChildren<QPushButton*>(
                     QStringLiteral("findTime")).size(), 1);
        clearNeedsBlockPrefs();
    }

    // With a single qualifying task there is nothing to survey, so the
    // counter is an inert badge rather than a button that expands to itself.
    void gateCounterIsNotAButtonForOneTask()
    {
        clearNeedsBlockPrefs();
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4A7CC4"));
        data.setTaskPriority(data.addTask("Lab 4", cat),
                             Task::Priority::Urgent);

        TrackerService tracker(&data);
        GlancePanel panel(&data, &tracker);
        panel.show();
        panel.refresh();

        QVERIFY(!panel.findChild<QPushButton*>(
            QStringLiteral("needsBlockShowAll")));
        QCOMPARE(panel.findChildren<QPushButton*>(
                     QStringLiteral("findTime")).size(), 1);
        clearNeedsBlockPrefs();
    }

    // The field bug's decisive test (v22.5): the owner reports real clicks
    // on "Show my day" doing nothing while every programmatic click()
    // passes. click() invokes the slot; THIS test synthesises a mouse
    // press+release on the top-level QWindow at the button's position, which
    // runs the full delivery pipeline — hit-testing down the widget tree,
    // overlapping siblings, event filters. If any widget were sitting over
    // the button eating input, this is the test that would catch it.
    void showMyDayRespondsToARealMouseClick()
    {
        clearNeedsBlockPrefs();
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4A7CC4"));
        const QString id  = data.addTask("Lab 4", cat);
        data.setTaskPriority(id, Task::Priority::Urgent);

        TrackerService tracker(&data);
        GlancePanel panel(&data, &tracker);
        panel.resize(360, 640);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        panel.refresh();
        QTest::qWait(0);

        auto* open = panel.findChild<QPushButton*>(
            QStringLiteral("showMyDay"));
        QVERIFY(open);
        QVERIFY(open->isVisible());

        const QPoint inWindow = open->mapTo(&panel, open->rect().center());
        QTest::mouseClick(panel.windowHandle(), Qt::LeftButton,
                          Qt::NoModifier, inWindow);
        QTest::qWait(0); // deferred reviewed()

        QVERIFY(panel.findChild<QWidget*>(QStringLiteral("glanceContent"))
                    ->isVisible());
        clearNeedsBlockPrefs();
    }

    // v22.7, the field bug pinned: on the owner's machine QSettings writes
    // were vanishing, so the rebuild re-read an empty lastReview and slammed
    // the gate shut on release ("Opening your day…" flips on press, snaps
    // back on release). The cure is a session witness: the card remembers
    // the review it SAW. This test simulates the broken store by erasing the
    // prefs right after the click — the gate must hold open anyway, because
    // the session's memory is not the disk's to lose.
    void showMyDayHoldsEvenIfSettingsForget()
    {
        clearNeedsBlockPrefs();
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4A7CC4"));
        const QString id  = data.addTask("Lab 4", cat);
        data.setTaskPriority(id, Task::Priority::Urgent);

        TrackerService tracker(&data);
        GlancePanel panel(&data, &tracker);
        panel.show();
        panel.refresh();

        auto* open = panel.findChild<QPushButton*>(
            QStringLiteral("showMyDay"));
        QVERIFY(open);
        open->click();
        QTest::qWait(0); // deferred reviewed()

        // The storage "forgets" the review the click just wrote…
        QSettings().remove(QStringLiteral("needsBlock/lastReview"));
        // …and the world re-derives from scratch.
        panel.refresh();
        QTest::qWait(0);

        QVERIFY(panel.findChild<QWidget*>(QStringLiteral("glanceContent"))
                    ->isVisible()); // the session witness held the door
        clearNeedsBlockPrefs();
    }

    // v22.9 (owner request): the inline accordions became two chips that
    // open a slide-over drawer. This walks the whole loop: chip opens the
    // drawer with the right rows; acting INSIDE the drawer (bring back)
    // flows through the same data->changed->rebuild pipeline as everything
    // else; and when the action empties the list, the drawer closes itself
    // rather than leaving an empty sheet on stage.
    void chipsOpenTheSlidePanelAndActionsFlowThrough()
    {
        clearNeedsBlockPrefs();
        AppData data;
        const QString cat  = data.addCategory("School", QColor("#4A7CC4"));
        const QString idA  = data.addTask("Lab 4", cat);
        const QString idB  = data.addTask("Essay draft", cat);
        data.setTaskPriority(idA, Task::Priority::Urgent);
        data.setTaskPriority(idB, Task::Priority::Urgent);
        // Put idB off so the second chip has something to show.
        //
        // RELATIVE to now, not the wall-clock 23:00 this used to say. That
        // literal made the test a time bomb: run it any time after 11pm and
        // the "dismissal" was already in the past, the task came straight
        // back, the put-off list was empty, and the chip under test was never
        // built — a red suite caused by the hour, not by the code. (Caught
        // for real: a v23 run at 23:55.) The test's actual requirement is
        // "this task is currently put off", so say THAT and let the clock do
        // whatever it likes.
        QVERIFY(data.dismissTask(
            idB, QDateTime::currentDateTime().addSecs(60 * 60)));

        TrackerService tracker(&data);
        GlancePanel panel(&data, &tracker);
        // The panel FORWARDS action signals; PlannerPage normally makes the
        // data call. A bare panel in a test has no page above it, so the
        // test stands in for the page — one line, same contract. (First run
        // of this test clicked "bring back" into the void and taught the
        // lesson: know which layer OWNS the mutation before asserting it.)
        connect(&panel, &GlancePanel::bringBackRequested, &data,
                [&data](const QString& id) { data.clearDismissal(id); });
        // …and the page's OTHER job: routing changed() back into the panel.
        // Without this the mutation lands but nothing re-derives — the first
        // run cleared the dismissal and then stared at a drawer that no one
        // had told about it. The pipeline is only a pipeline when both ends
        // are connected.
        connect(&data, &AppData::changed, &panel,
                [&panel]() { panel.refresh(); });
        panel.resize(360, 640);
        panel.show();
        panel.refresh();
        auto* open = panel.findChild<QPushButton*>(
            QStringLiteral("showMyDay"));
        QVERIFY(open);
        open->click();
        QTest::qWait(0); // deferred reviewed()

        // Chip 1 → the needs-a-block drawer, carrying the compact rows.
        auto* strip = panel.findChild<QPushButton*>(
            QStringLiteral("needsBlockStrip"));
        QVERIFY(strip);
        strip->click();
        QTest::qWait(0);
        auto* sheet = panel.findChild<SlidePanel*>();
        QVERIFY(sheet && sheet->isOpen());
        QVERIFY(sheet->findChild<QPushButton*>(QStringLiteral("findTime")));

        // Chip 2 → the put-off drawer. Same sheet, different room.
        auto* putOffChip = panel.findChild<QPushButton*>(
            QStringLiteral("putOffStrip"));
        QVERIFY(putOffChip);
        putOffChip->click();
        QTest::qWait(0);
        auto* back = sheet->findChild<QPushButton*>(
            QStringLiteral("bringBack"));
        QVERIFY(back);

        // Acting inside the drawer flows through the ordinary pipeline —
        // and emptying the list closes the drawer by itself.
        back->click();
        QTest::qWait(0);
        QVERIFY(!data.taskById(idB)->dismissedUntil.isValid());
        QVERIFY(!sheet->isOpen());
        clearNeedsBlockPrefs();
    }

    // ======================================================================
    // v23 — the window's own memory (geometry + sidebar).
    //
    // Two layers, tested at two costs. The POLICY (what counts as a
    // reachable rectangle) is a pure function, so it gets cheap exhaustive
    // tests against monitor layouts nobody here owns. The WIRING (does the
    // window actually write and read it) gets a small number of expensive
    // tests that build a real MainWindow. That ratio is the point: push the
    // thinking into something pure, and the slow tests only have to prove
    // the plumbing is connected.
    // ======================================================================

    static void clearWindowPrefs()
    {
        QCoreApplication::setApplicationName(QStringLiteral("TickTimerTest"));
        QSettings s;
        s.remove(QStringLiteral("window/geometry"));
        s.remove(QStringLiteral("window/sidebarVisible"));
        s.sync();
    }

    // ---- the pure policy --------------------------------------------------

    void reachableWindowOverlapsAScreen()
    {
        const QList<QRect> oneMonitor{ QRect(0, 0, 1920, 1040) };

        // Dead centre — obviously fine.
        QVERIFY(overlapsAnyScreen(QRect(400, 200, 800, 600), oneMonitor));

        // Hanging off the right edge: the user dragged it there on purpose,
        // and the title bar is still grabbable. Generous test, by design.
        QVERIFY(overlapsAnyScreen(QRect(1850, 300, 800, 600), oneMonitor));

        // One pixel of overlap is still overlap.
        QVERIFY(overlapsAnyScreen(QRect(1919, 1039, 800, 600), oneMonitor));
    }

    void unplugMonitorLeavesTheWindowUnreachable()
    {
        // The scenario the whole guard exists for: the app was closed on the
        // second monitor at x=1920, and that monitor is now gone.
        const QList<QRect> twoMonitors{ QRect(0, 0, 1920, 1040),
                                        QRect(1920, 0, 1920, 1040) };
        const QRect wasOnSecond(2200, 300, 900, 700);
        QVERIFY(overlapsAnyScreen(wasOnSecond, twoMonitors));

        const QList<QRect> oneMonitor{ QRect(0, 0, 1920, 1040) };
        QVERIFY(!overlapsAnyScreen(wasOnSecond, oneMonitor));
    }

    void negativeCoordinatesAreNotAutomaticallyWrong()
    {
        // A monitor arranged to the LEFT of the primary has negative x. A
        // naive "x < 0 means broken" check — which is what people write
        // before they've owned a multi-monitor machine — would reject a
        // perfectly good window here.
        const QList<QRect> leftAndPrimary{ QRect(-1920, 0, 1920, 1040),
                                           QRect(0, 0, 1920, 1040) };
        QVERIFY(overlapsAnyScreen(QRect(-1500, 100, 800, 600),
                                  leftAndPrimary));
    }

    void noScreensMeansNothingIsReachable()
    {
        // Degenerate but real (a headless session mid-teardown). The answer
        // must be false, not a crash and not an accidental true — false
        // routes MainWindow to its default size, which is the safe outcome.
        QVERIFY(!overlapsAnyScreen(QRect(0, 0, 800, 600), QList<QRect>{}));
    }

    // ---- the wiring -------------------------------------------------------

    void sidebarChoiceSurvivesARelaunch()
    {
        clearWindowPrefs();
        {
            MainWindow w;
            auto* toggle = w.findChild<QPushButton*>();
            QVERIFY(toggle); // the ☰ is the first button in the header
            // Rail starts open on a desktop-sized (here: offscreen) display.
            QSettings().setValue(QStringLiteral("window/sidebarVisible"),
                                 false);
        }
        // A fresh window, same machine: the choice is honoured.
        {
            MainWindow w;
            w.show();
            QVERIFY(QTest::qWaitForWindowExposed(&w));
            // The rail is the fixed-width 190px widget in the body.
            bool railHidden = true;
            const auto kids = w.findChildren<QWidget*>();
            for (QWidget* c : kids)
                if (c->width() == 190 && c->isVisible())
                    railHidden = false;
            QVERIFY(railHidden);
        }
        clearWindowPrefs();
    }

    void corruptGeometryFallsBackToTheDefaultSize()
    {
        clearWindowPrefs();
        // Bytes Qt will refuse: no version tag, no magic number. The window
        // must open at its ordinary size and say nothing — an unreadable
        // preference is not an error the user can act on.
        QSettings().setValue(QStringLiteral("window/geometry"),
                             QByteArray("not a geometry blob"));
        MainWindow w;
        QCOMPARE(w.size(), QSize(1150, 780));
        clearWindowPrefs();
    }

    void geometryIsWrittenOnClose()
    {
        clearWindowPrefs();
        QVERIFY(QSettings()
                    .value(QStringLiteral("window/geometry"))
                    .toByteArray()
                    .isEmpty());
        {
            MainWindow w;
            w.show();
            QVERIFY(QTest::qWaitForWindowExposed(&w));
            w.resize(900, 640);
            w.close(); // closeEvent is the save hook
        }
        QVERIFY(!QSettings()
                     .value(QStringLiteral("window/geometry"))
                     .toByteArray()
                     .isEmpty());
        clearWindowPrefs();
    }

    // v23.1 — the field bug this arc closed within a day of shipping:
    // write-on-close protects nothing when the process is KILLED instead of
    // closed, and Qt Creator's Stop button kills. Geometry must now be on
    // disk ~1s after the window stops moving, with no close at all. The test
    // deliberately never calls close(): the window just resizes, the clock
    // runs past the debounce, and the memory must already be there — the
    // exact sequence "Stop button after fiddling with the window" produces.
    void geometryIsWrittenWithoutAClose()
    {
        clearWindowPrefs();
        MainWindow w;
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        w.resize(870, 610);
        // Past the 1s debounce. qWait keeps the event loop SPINNING, which a
        // sleep would not — a timer can only fire in a running loop.
        QTest::qWait(1300);
        QVERIFY(!QSettings()
                     .value(QStringLiteral("window/geometry"))
                     .toByteArray()
                     .isEmpty());
        clearWindowPrefs();
        // No w.close(): the destructor tearing the window down IS the test.
    }

    // The gate the debounce depends on: construction and restore also fire
    // move/resize events, and a save scheduled THEN would overwrite the real
    // memory with the half-built default on every startup — the feature
    // erasing itself. So: plant a memory, build a window, let the loop spin
    // briefly WITHOUT any user-shaped geometry change, and the stored blob
    // must come through untouched.
    void startupDoesNotOverwriteTheStoredGeometry()
    {
        clearWindowPrefs();
        {
            MainWindow w;
            w.show();
            QVERIFY(QTest::qWaitForWindowExposed(&w));
            // The resize is only a nudge so SOMETHING gets saved; its value
            // is deliberately not asserted anywhere. Three drafts of this
            // test tried to pin the width and all three lost, each to a
            // different owner: 870 was clamped by restoreGeometry() to the
            // offscreen screen (800x600); 780 was pushed back UP by the
            // next layout pass, because with Qt's no-fonts fallback
            // metrics this window's layout computes a ~1166 minimum; and
            // the restored window measures 798 only because the ctor's
            // restore runs BEFORE its first layout pass enforces that
            // minimum. Pre-layout clamps, post-layout minimums, screen
            // fitting — three owners of "width" on this platform, none of
            // them the code under test. So the width is nobody's claim
            // here; the test asserts only what its NAME says, below.
            w.resize(780, 560);
            QTest::qWait(1300); // debounce writes
            w.close();          // close writes again (the courtesy pass)
        }
        // Baseline read AFTER the first window is completely gone. First
        // draft read it mid-lifetime, before close() — which bakes in an
        // assumption the test never meant to make (that the debounce write
        // and the close write are byte-identical; the offscreen platform
        // says otherwise). The claim under test is only "CONSTRUCTION does
        // not mutate storage", so the baseline is whatever storage holds
        // when construction begins.
        const QByteArray planted =
            QSettings().value(QStringLiteral("window/geometry")).toByteArray();
        QVERIFY(!planted.isEmpty());
        {
            MainWindow w2; // constructor restores from the blob, events fly
            // Restore ACTED — the size is not the untouched 1150x780
            // default, which is all "acted" needs to mean; the exact size
            // it lands on belongs to the platform and the layout (see the
            // block comment above). The claim this test exists for is the
            // storage comparison below: whatever construction did to the
            // WINDOW, it did nothing to the BLOB.
            QVERIFY(w2.size() != QSize(1150, 780));
            QTest::qWait(50); // a beat of event loop, no user action
            // Whatever construction did, it did not schedule a save of the
            // default rectangle over the real one.
            QCOMPARE(QSettings()
                         .value(QStringLiteral("window/geometry"))
                         .toByteArray(),
                     planted);
        }
        clearWindowPrefs();
    }

    // ---- v25.1: the Settings Test button -----------------------------------

    // The button must test WHAT IS ON SCREEN — and its failures must arrive
    // offline, inline, and without a single QSettings write (the dialog's
    // Cancel-writes-nothing promise extends to Test). Driven through the two
    // synchronous fail-fast paths, so no socket ever opens.
    void settingsTestKeyFailsFastOfflineAndWritesNothing()    {
        QCoreApplication::setApplicationName(QStringLiteral("TickTimerTest"));
        QSettings().remove(QStringLiteral("ai"));
        // A developer machine's real credential must not turn this into a
        // live call: the env half of the field-first/env-second composition
        // has to be empty for the fail-fast path to fire.
        qunsetenv("ANTHROPIC_API_KEY");

        SettingsDialog dialog;
        auto* provider =
            dialog.findChild<QComboBox*>(QStringLiteral("aiProviderCombo"));
        auto* keyEdit =
            dialog.findChild<QLineEdit*>(QStringLiteral("aiKeyEdit"));
        auto* test =
            dialog.findChild<QPushButton*>(QStringLiteral("aiTestButton"));
        auto* result =
            dialog.findChild<QLabel*>(QStringLiteral("aiTestResult"));
        QVERIFY(provider && keyEdit && test && result);

        // Scene 1: a key-needing provider with no key anywhere.
        provider->setCurrentIndex(
            provider->findData(QStringLiteral("anthropic")));
        keyEdit->clear();
        test->click();
        QVERIFY(result->text().contains(QStringLiteral("no API key set")));
        QVERIFY(result->text().contains(QStringLiteral("Anthropic")));
        QVERIFY(test->isEnabled()); // failure re-arms the button

        // Scene 2: a custom endpoint with no address — the other offline
        // guard, and the one that catches the commonest self-hosting slip.
        provider->setCurrentIndex(provider->findData(QStringLiteral("custom")));
        QCOMPARE(result->text(), QString()); // a verdict never survives a
                                             // provider switch
        test->click();
        QVERIFY(result->text().contains(
            QStringLiteral("no server address set")));

        // The promise: Test wrote NOTHING. No key slot, no provider id —
        // QSettings is exactly as empty as it started.
        QVERIFY(!QSettings().contains(
            ai::settingsKeyForKey(QStringLiteral("anthropic"))));
        QVERIFY(!QSettings().contains(QStringLiteral("ai/provider")));
        QSettings().remove(QStringLiteral("ai"));
    }

    // The provider the probe fires at is built from the FIELDS, not from
    // QSettings — a key typed but unsaved must be the key that gets tested.
    // Proven negatively: saved settings say Anthropic-with-key, the screen
    // says custom-with-no-address, and the failure is the SCREEN's.
    void settingsTestUsesTheFieldsNotTheSavedSettings()
    {
        QCoreApplication::setApplicationName(QStringLiteral("TickTimerTest"));
        QSettings s;
        s.remove(QStringLiteral("ai"));
        s.setValue(QStringLiteral("ai/provider"), QStringLiteral("anthropic"));
        s.setValue(ai::settingsKeyForKey(QStringLiteral("anthropic")),
                   QStringLiteral("SAVED-KEY"));

        SettingsDialog dialog;
        auto* provider =
            dialog.findChild<QComboBox*>(QStringLiteral("aiProviderCombo"));
        auto* test =
            dialog.findChild<QPushButton*>(QStringLiteral("aiTestButton"));
        auto* result =
            dialog.findChild<QLabel*>(QStringLiteral("aiTestResult"));
        QVERIFY(provider && test && result);

        provider->setCurrentIndex(provider->findData(QStringLiteral("custom")));
        test->click();
        // Settings hold a perfectly good Anthropic setup; the screen's custom
        // endpoint has no address — and it is the screen that gets tested.
        QVERIFY(result->text().contains(
            QStringLiteral("no server address set")));
        s.remove(QStringLiteral("ai"));
    }

    // v25.3 — the persona rows keep the dialog's two standing promises:
    // only OK writes (Cancel-writes-nothing), and what OK writes is what
    // the screen showed. Also pins the repair: a hand-edited unknown id
    // must land the combo on Calm, never on index -1 (a combo with no
    // current row silently saves an empty id — a fresh way to brick).
    void settingsPersonaRoundTripsOnOkOnly()
    {
        QCoreApplication::setApplicationName(QStringLiteral("TickTimerTest"));
        QSettings s;
        s.remove(QStringLiteral("ai"));

        {
            SettingsDialog dialog;
            auto* combo = dialog.findChild<QComboBox*>(
                QStringLiteral("aiPersonaCombo"));
            auto* text = dialog.findChild<QLineEdit*>(
                QStringLiteral("aiPersonaTextEdit"));
            QVERIFY(combo && text);
            // Fresh settings: the combo sits on Calm — the catalog's first
            // entry, same default the prompt layer promises.
            QCOMPARE(combo->currentData().toString(), QStringLiteral("calm"));

            combo->setCurrentIndex(
                combo->findData(QStringLiteral("coach")));
            text->setText(QStringLiteral("call me Sam"));
            // Cancel: the dialog dies without save() — nothing written.
        }
        QVERIFY(!s.contains(chat::settingsKeyPersona()));
        QVERIFY(!s.contains(chat::settingsKeyPersonaText()));

        {
            SettingsDialog dialog;
            auto* combo = dialog.findChild<QComboBox*>(
                QStringLiteral("aiPersonaCombo"));
            auto* text = dialog.findChild<QLineEdit*>(
                QStringLiteral("aiPersonaTextEdit"));
            combo->setCurrentIndex(combo->findData(QStringLiteral("brief")));
            text->setText(QStringLiteral("  no emoji  ")); // save() trims
            // Press the REAL OK button, as the other settings tests do —
            // driving save() directly would skip whatever accept() guards.
            dialog.findChild<QDialogButtonBox*>()
                ->button(QDialogButtonBox::Ok)
                ->click();
        }
        QCOMPARE(s.value(chat::settingsKeyPersona()).toString(),
                 QStringLiteral("brief"));
        QCOMPARE(s.value(chat::settingsKeyPersonaText()).toString(),
                 QStringLiteral("no emoji"));

        // The repair, on screen: an unknown stored id lands on Calm.
        s.setValue(chat::settingsKeyPersona(), QStringLiteral("wat"));
        {
            SettingsDialog dialog;
            auto* combo = dialog.findChild<QComboBox*>(
                QStringLiteral("aiPersonaCombo"));
            QVERIFY(combo->currentIndex() >= 0);
            QCOMPARE(combo->currentData().toString(), QStringLiteral("calm"));
        }
        s.remove(QStringLiteral("ai"));
    }

    // v26 — the fall-through walk, end to end through the REAL machinery
    // via the TICKTIMER_AI_DOWN hook (§E: testing a fallback must not
    // require unplugging a router). Primary down -> announced, next seat
    // tried; both down -> failed; and the SECOND send hits the breaker's
    // fast-fail with the seats named, instead of re-proving the outage.
    void chatRouteFallsThroughOnUnreachableAndBreakerFastFails()
    {
        QCoreApplication::setApplicationName(QStringLiteral("TickTimerTest"));
        QSettings s;
        s.remove(QStringLiteral("ai"));
        s.setValue(ai::settingsKeyRoute(ai::Feature::Chat),
                   QStringList{QStringLiteral("anthropic"),
                               QStringLiteral("ollama")});
        qputenv("TICKTIMER_AI_DOWN", "anthropic,ollama");
        // Shared, in-memory, process-wide: start this test clean and leave
        // it clean, or the breaker verdicts here poison unrelated tests.
        ai::breaker().noteOk(QStringLiteral("anthropic"));
        ai::breaker().noteOk(QStringLiteral("ollama"));

        ChatClient client;
        QSignalSpy unreachable(&client, &ChatClient::seatUnreachable);
        QSignalSpy failed(&client, &ChatClient::failed);

        client.send(QStringLiteral("sys"),
                    {ai::Message{ai::Role::User, QStringLiteral("hi")}});
        // The forced-down path is synchronous: no socket, no event loop.
        QCOMPARE(unreachable.count(), 1);
        QCOMPARE(unreachable.first().at(0).toString(),
                 QStringLiteral("anthropic"));
        QCOMPARE(unreachable.first().at(1).toString(),
                 QStringLiteral("ollama"));
        QCOMPARE(failed.count(), 1); // the last seat has nowhere to go

        // Send #2, inside the cooldown: the breaker answers instantly,
        // names the seats, and the walk never starts.
        client.send(QStringLiteral("sys"),
                    {ai::Message{ai::Role::User, QStringLiteral("hi")}});
        QCOMPARE(unreachable.count(), 1); // unchanged — nothing was tried
        QCOMPARE(failed.count(), 2);
        QVERIFY(failed.last().at(0).toString().contains(
            QStringLiteral("retry")));

        qunsetenv("TICKTIMER_AI_DOWN");
        ai::breaker().noteOk(QStringLiteral("anthropic"));
        ai::breaker().noteOk(QStringLiteral("ollama"));
        s.remove(QStringLiteral("ai"));
    }

    // v26 — the fallback row round-trips, and a fallback equal to the
    // primary collapses to a one-seat route: [x, x] is not a preference,
    // it is a typo the dialog refuses to store.
    void settingsChatFallbackWritesTheRouteOnOk()
    {
        QCoreApplication::setApplicationName(QStringLiteral("TickTimerTest"));
        QSettings s;
        s.remove(QStringLiteral("ai"));

        {
            SettingsDialog dialog;
            auto* fallback = dialog.findChild<QComboBox*>(
                QStringLiteral("aiChatFallbackCombo"));
            QVERIFY(fallback);
            // Fresh settings: no route stored, the row says fail-fast.
            QCOMPARE(fallback->currentData().toString(), QString());
            fallback->setCurrentIndex(
                fallback->findData(QStringLiteral("ollama")));
            dialog.findChild<QDialogButtonBox*>()
                ->button(QDialogButtonBox::Ok)
                ->click();
        }
        QCOMPARE(s.value(ai::settingsKeyRoute(ai::Feature::Chat))
                     .toStringList(),
                 (QStringList{QStringLiteral("anthropic"),
                              QStringLiteral("ollama")}));

        // Reopen: the stored second seat is what the row shows.
        {
            SettingsDialog dialog;
            auto* fallback = dialog.findChild<QComboBox*>(
                QStringLiteral("aiChatFallbackCombo"));
            QCOMPARE(fallback->currentData().toString(),
                     QStringLiteral("ollama"));
            // Fallback == primary: stored as one seat, not [x, x].
            fallback->setCurrentIndex(
                fallback->findData(QStringLiteral("anthropic")));
            dialog.findChild<QDialogButtonBox*>()
                ->button(QDialogButtonBox::Ok)
                ->click();
        }
        QCOMPARE(s.value(ai::settingsKeyRoute(ai::Feature::Chat))
                     .toStringList(),
                 QStringList{QStringLiteral("anthropic")});
        s.remove(QStringLiteral("ai"));
    }


    // THE OFF-BY-ONE THIS PAGE NEARLY SHIPPED WITH. The Assistant's rail
    // button sits ABOVE Archive's, but its page index (6) comes after
    // Archive's (5) — rail position and stack identity are deliberately
    // different orders now, and m_navButtons must follow the STACK. Appending
    // it in visual order would make showPage(5) display the Archive while
    // lighting up the Assistant; both are QToolButton*, so only a test can
    // catch it.
    void assistantPageHighlightsItsOwnButton()
    {
        MainWindow window;
        window.show();

        auto* pages = window.findChild<QStackedWidget*>();
        QVERIFY(pages);

        const auto checkedNavText = [&window]() -> QString {
            const auto buttons = window.findChildren<QToolButton*>();
            for (QToolButton* b : buttons)
                if (b->objectName() == QLatin1String("nav") && b->isChecked())
                    return b->text();
            return {};
        };

        window.showPage(6);
        QVERIFY(qobject_cast<ChatPage*>(pages->currentWidget()));
        QVERIFY(checkedNavText().contains(QStringLiteral("Assistant")));

        window.showPage(5); // and the neighbour it must not steal from
        QVERIFY(qobject_cast<ArchivePage*>(pages->currentWidget()));
        QVERIFY(checkedNavText().contains(QStringLiteral("Archive")));
    }

    // Enter sends; Shift+Enter is a newline. The one behaviour ChatInput
    // exists for, driven through real key events.
    void chatInputEnterSendsShiftEnterNewlines()
    {
        ChatInput input;
        QSignalSpy submitted(&input, &ChatInput::submitted);

        QTest::keyClick(&input, Qt::Key_A);
        QTest::keyClick(&input, Qt::Key_Return, Qt::ShiftModifier);
        QTest::keyClick(&input, Qt::Key_B);
        QCOMPARE(submitted.count(), 0); // Shift+Enter typed, nothing sent
        QCOMPARE(input.toPlainText(), QStringLiteral("a\nb"));

        QTest::keyClick(&input, Qt::Key_Return);
        QCOMPARE(submitted.count(), 1);
    }

    // A send that fails must land in the LOG as a warning bubble and put the
    // page back to idle — not raise a dialog, not leave "Thinking…" forever.
    // Driven entirely offline through the fail-fast path: a provider that
    // needs a key, with no key anywhere (envVar cleared so a developer
    // machine's real ANTHROPIC_API_KEY can't turn this into a live call).
    void chatFailureBecomesALogBubbleAndReleasesTheUi()
    {
        QCoreApplication::setApplicationName(QStringLiteral("TickTimerTest"));
        QSettings().remove(QStringLiteral("ai"));

        AppData data;
        ChatPage page(&data);
        page.show();

        ai::Provider keyless = ai::byId(QStringLiteral("anthropic"));
        keyless.envVar.clear();
        page.client()->setProviderOverride(keyless);

        auto* input = page.findChild<ChatInput*>();
        QVERIFY(input);
        input->setPlainText(QStringLiteral("hello?"));
        page.sendCurrentInput();

        // The failure is synchronous on this path, so the log already holds
        // the user's bubble AND the warning bubble.
        bool userBubble = false, warnBubble = false;
        const auto labels = page.findChildren<QLabel*>();
        for (QLabel* l : labels) {
            if (l->text() == QLatin1String("hello?"))
                userBubble = true;
            if (l->text().contains(QStringLiteral("no API key set")))
                warnBubble = true;
        }
        QVERIFY(userBubble); // what you typed stays visible
        QVERIFY(warnBubble); // what went wrong sits next to it

        // …and the composer is usable again: the dual-role button reads
        // "Send", not "Stop".
        auto* send =
            page.findChild<QPushButton*>(QStringLiteral("chatSend"));
        QVERIFY(send);
        QCOMPARE(send->text(), QStringLiteral("Send"));
    }

    // The briefing is generated at ASK time from live data, on the page's
    // clock seam — add a task, and the very next question knows about it.
    void chatBriefingIsLiveAndOnTheSeamClock()
    {
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));

        ChatPage page(&data);
        page.nowProvider = [] {
            return QDateTime(QDate(2026, 7, 19), QTime(10, 0));
        };

        QVERIFY(page.currentBriefing().contains(QStringLiteral("2026-07-19")));
        QVERIFY(!page.currentBriefing().contains(QStringLiteral("Lab 4")));

        data.addTask("Lab 4", cat, QDate(2026, 7, 20));
        QVERIFY(page.currentBriefing().contains(QStringLiteral("Lab 4")));
    }

    // ======================================================================
    // v26.2–v26.7 — catch-up: the settings page, and the chip+drawer card
    // ======================================================================
    //
    // v26.7 rewrote the card as ONE chip opening a SlidePanel drawer, so
    // these tests drive that flow: refresh -> chip -> (drawer fills) -> the
    // verb buttons. The drawer's host falls back to the card itself when
    // the card has no parent (exactly the bare embedding a test is), which
    // is what keeps findChild working: the sheet is a child of the card.

    void catchUpSettingsSaveOnOkOnly()
    {
        QCoreApplication::setApplicationName(QStringLiteral("TickTimerTest"));
        QSettings().remove(QStringLiteral("catchup"));

        SettingsDialog dialog;
        auto* threshold = dialog.findChild<QComboBox*>(
            QStringLiteral("catchUpThresholdCombo"));
        auto* onOpen = dialog.findChild<QCheckBox*>(
            QStringLiteral("catchUpOnOpenCheck"));
        QVERIFY(threshold && onOpen);

        threshold->setCurrentIndex(threshold->findData(75));
        onOpen->setChecked(false);
        QCOMPARE(prefs::missedRule().minPercent, 50); // untouched until OK

        QDialogButtonBox* box = dialog.findChild<QDialogButtonBox*>();
        QVERIFY(box);
        box->button(QDialogButtonBox::Ok)->click();

        QCOMPARE(prefs::missedRule().minPercent, 75);
        QCOMPARE(prefs::catchUpOnOpen(), false);
        QCOMPARE(prefs::catchUpAtEndOfDay(), true); // untouched -> default

        QSettings().remove(QStringLiteral("catchup"));
    }

    // Chip -> drawer -> Skip: the block is resolved through the wired door,
    // and the chip demotes itself to the muted "resolved" form (still
    // present — that is the way back staying on the map).
    void catchUpChipOpensDrawerAndResolves()
    {
        QCoreApplication::setApplicationName(QStringLiteral("TickTimerTest"));
        QSettings().remove(QStringLiteral("catchup"));

        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        const QString id =
            data.addEvent(QDate(2026, 7, 19), 9 * 60, 10 * 60, act, "");
        QVERIFY(!id.isEmpty());

        CatchUpCard card(&data);
        QObject::connect(&card, &CatchUpCard::resolveRequested,
                         [&](const QString& eventId, BlockOutcome outcome) {
                             data.resolveBlock(eventId, outcome);
                         });

        const QDateTime now(QDate(2026, 7, 20), QTime(8, 0));
        card.refresh(now);
        QVERIFY(card.hasAnything());
        QVERIFY(!card.isHidden());

        auto* chip =
            card.findChild<QPushButton*>(QStringLiteral("catchUpChip"));
        QVERIFY(chip);
        // Before the chip is pressed, no drawer content exists — the panel
        // is genuinely just a chip now.
        QVERIFY(card.findChild<QPushButton*>(
                    QStringLiteral("catchUpSkip")) == nullptr);
        chip->click();

        auto* skip =
            card.findChild<QPushButton*>(QStringLiteral("catchUpSkip"));
        QVERIFY(skip);
        skip->click();

        QCOMPARE(data.eventById(id)->outcome, BlockOutcome::Dropped);
        card.refresh(now);
        QVERIFY(!card.hasAnything());
        QVERIFY(!card.isHidden()); // muted "1 resolved" chip — not gone

        QSettings().remove(QStringLiteral("catchup"));
    }

    // Accepting the pre-filled proposal carries the exact pieces the
    // proposer computed — the button is still propose-don't-move.
    void catchUpDrawerAcceptCarriesTheProposal()
    {
        QCoreApplication::setApplicationName(QStringLiteral("TickTimerTest"));
        QSettings().remove(QStringLiteral("catchup"));

        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        const QString id =
            data.addEvent(QDate(2026, 7, 19), 9 * 60, 10 * 60, act, "");

        CatchUpCard card(&data);
        QObject::connect(
            &card, &CatchUpCard::acceptProposalRequested,
            [&](const QString& eventId, const reschedule::Option& o) {
                QVERIFY(o.pieces.size() == 1);
                const reschedule::Piece& p = o.pieces.first();
                data.rescheduleBlock(eventId, p.date, p.startMinutes,
                                     p.endMinutes);
            });

        const QDateTime now(QDate(2026, 7, 20), QTime(8, 0));
        card.refresh(now);
        card.findChild<QPushButton*>(QStringLiteral("catchUpChip"))->click();
        auto* move =
            card.findChild<QPushButton*>(QStringLiteral("catchUpMove"));
        QVERIFY(move);
        move->click();

        const Event* old = data.eventById(id);
        QVERIFY(old);
        QCOMPARE(old->outcome, BlockOutcome::Moved);
        QVERIFY(!old->movedToIds.isEmpty());
        QVERIFY(data.eventById(old->movedToIds.value(0)) != nullptr);

        // Moved blocks are excluded from BOTH derived sets, so the chip has
        // nothing left to say: the card hides entirely.
        card.refresh(now);
        QVERIFY(!card.hasAnything());
        QVERIFY(card.isHidden());

        QSettings().remove(QStringLiteral("catchup"));
    }

    // The 46-block accident, in the drawer: Skip all leaves the receipt in
    // place, Undo replays the same ids as Unset, everything returns.
    void catchUpDrawerUndoRevivesABulkSkip()
    {
        QCoreApplication::setApplicationName(QStringLiteral("TickTimerTest"));
        QSettings().remove(QStringLiteral("catchup"));

        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        // All four blocks INSIDE the 3-day horizon (v26.8: the default
        // shrank from 7, and a date at now-4 would silently fall out of
        // the derived set — Skip all only carries what it rendered, so
        // the fourth assertion below would fail for a horizon reason,
        // not an undo reason. Tests that straddle a default are tests of
        // the default, and this one is about the receipt.)
        QStringList ids;
        for (int d = 1; d <= 3; ++d)
            ids << data.addEvent(QDate(2026, 7, 20).addDays(-d), 9 * 60,
                                 10 * 60, act, "");
        ids << data.addEvent(QDate(2026, 7, 19), 11 * 60, 12 * 60, act, "");

        CatchUpCard card(&data);
        QObject::connect(&card, &CatchUpCard::resolveAllRequested,
                         [&](const QStringList& eventIds,
                             BlockOutcome outcome) {
                             data.resolveBlocks(eventIds, outcome);
                         });

        const QDateTime now(QDate(2026, 7, 20), QTime(8, 0));
        card.refresh(now);
        card.findChild<QPushButton*>(QStringLiteral("catchUpChip"))->click();
        auto* skipAll =
            card.findChild<QPushButton*>(QStringLiteral("catchUpSkipAll"));
        QVERIFY(skipAll);
        skipAll->click();

        for (const QString& id : ids)
            QCOMPARE(data.eventById(id)->outcome, BlockOutcome::Dropped);

        // The drawer stays open, receipt first under the title.
        auto* undo =
            card.findChild<QPushButton*>(QStringLiteral("catchUpUndo"));
        QVERIFY(undo);
        undo->click();

        for (const QString& id : ids)
            QCOMPARE(data.eventById(id)->outcome, BlockOutcome::Unset);
        card.refresh(now);
        QVERIFY(card.hasAnything());

        QSettings().remove(QStringLiteral("catchup"));
    }

    // The retroactive rescue (§K.3), drawer edition: resolutions with no
    // receipt in RAM are reachable because the bring-back section derives
    // from the DATA. Fresh card -> muted chip -> drawer -> bring all back.
    void catchUpDrawerBringBackReachesOldAccidents()
    {
        QCoreApplication::setApplicationName(QStringLiteral("TickTimerTest"));
        QSettings().remove(QStringLiteral("catchup"));

        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        QStringList ids;
        for (int d = 1; d <= 2; ++d)
            ids << data.addEvent(QDate(2026, 7, 20).addDays(-d), 9 * 60,
                                 10 * 60, act, "");
        QCOMPARE(data.resolveBlocks(ids, BlockOutcome::Dropped), 2);

        CatchUpCard card(&data); // fresh — no receipt, no session state
        QObject::connect(&card, &CatchUpCard::resolveAllRequested,
                         [&](const QStringList& eventIds,
                             BlockOutcome outcome) {
                             data.resolveBlocks(eventIds, outcome);
                         });

        const QDateTime now(QDate(2026, 7, 20), QTime(8, 0));
        card.refresh(now);
        QVERIFY(!card.hasAnything()); // nothing unresolved…
        QVERIFY(!card.isHidden());    // …but the muted chip keeps the card up

        card.findChild<QPushButton*>(QStringLiteral("catchUpChip"))->click();
        auto* toggle = card.findChild<QPushButton*>(
            QStringLiteral("catchUpResolvedChip"));
        QVERIFY(toggle);
        toggle->click(); // opens the bring-back section

        auto* all = card.findChild<QPushButton*>(
            QStringLiteral("catchUpBringAllBack"));
        QVERIFY(all);
        all->click();

        for (const QString& id : ids)
            QCOMPARE(data.eventById(id)->outcome, BlockOutcome::Unset);
        card.refresh(now);
        QVERIFY(card.hasAnything());

        QSettings().remove(QStringLiteral("catchup"));
    }

    // Snooze is de-emphasis, not a lock (§K.5): after Later the chip stays,
    // and one tap on the muted chip reopens the drawer — no "Show now"
    // needed, because access was never what the snooze governed.
    void catchUpSnoozeDemotesTheChipButNeverLocksTheDrawer()
    {
        QCoreApplication::setApplicationName(QStringLiteral("TickTimerTest"));
        QSettings().remove(QStringLiteral("catchup"));

        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        data.addEvent(QDate(2026, 7, 19), 9 * 60, 10 * 60, act, "");
        data.addEvent(QDate(2026, 7, 18), 9 * 60, 10 * 60, act, "");

        CatchUpCard card(&data);
        const QDateTime now(QDate(2026, 7, 20), QTime(8, 0));
        card.refresh(now);

        auto* chip =
            card.findChild<QPushButton*>(QStringLiteral("catchUpChip"));
        QVERIFY(chip);
        const QString prominentText = chip->text();
        chip->click();
        auto* later =
            card.findChild<QPushButton*>(QStringLiteral("catchUpLater"));
        QVERIFY(later);
        later->click(); // the accidental press, reproduced once more

        // Demoted, not gone: card visible, label now carries the return
        // time instead of the call to action.
        QVERIFY(!card.isHidden());
        QVERIFY(chip->text() != prominentText);
        QVERIFY(chip->text().contains(QStringLiteral("back")));

        // And the drawer is one tap away regardless of the snooze.
        chip->click();
        QVERIFY(card.findChild<QPushButton*>(
                    QStringLiteral("catchUpSkip")) != nullptr);

        QSettings().remove(QStringLiteral("catchup"));
    }

    // v26.7.1: the gate's veto goes through setSuppressed, and — the part a
    // naive panel-side hide() gets wrong — the chip COMES BACK when the
    // veto lifts even though the data (and thus the fingerprint) never
    // changed in between.
    void catchUpChipYieldsToTheGateAndReturns()
    {
        QCoreApplication::setApplicationName(QStringLiteral("TickTimerTest"));
        QSettings().remove(QStringLiteral("catchup"));

        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QString act = data.addActivity("Study", cat);
        data.addEvent(QDate(2026, 7, 19), 9 * 60, 10 * 60, act, "");

        CatchUpCard card(&data);
        const QDateTime now(QDate(2026, 7, 20), QTime(8, 0));

        card.refresh(now);
        QVERIFY(!card.isHidden());

        card.setSuppressed(true);   // the gate closes
        card.refresh(now);          // same data, same clock
        QVERIFY(card.isHidden());

        card.setSuppressed(false);  // "Show my day" opens the gate
        card.refresh(now);          // STILL the same data and clock
        QVERIFY(!card.isHidden());  // the print-clearing setter is why

        QSettings().remove(QStringLiteral("catchup"));
    }

    // ---- v28.5 — the piece's own panel -----------------------------------

    // The core contract of the slice, pinned by name: clicking a piece's
    // title RECORDS where to go and accepts — the dialog performs nothing.
    // (runTaskDetail, which acts on the record, is exec()-driven glue and
    // is exercised by hand per the QA checklist; the recordable half is
    // what a unit test can and should own.)
    void openingAPieceIsRecordedNotPerformed()
    {
        TaskDetailDialog dialog(QStringLiteral("Study for finals"), {},
                                QDate(), QTime(), Task::Repeat::None,
                                Task::Priority::Medium, 0, false);

        TaskDetailDialog::Piece ch3;
        ch3.id    = QStringLiteral("piece-ch3");
        ch3.title = QStringLiteral("Chapter 3");
        dialog.seedPieces({ch3});

        auto* open = dialog.findChild<QPushButton*>(
            QStringLiteral("pieceOpenButton"));
        QVERIFY(open);
        open->click();

        QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));
        QCOMPARE(dialog.navigationTarget(), QStringLiteral("piece-ch3"));
    }

    // Navigating away must never cost the user their edits: the hop is a
    // SAVE, not a cancel. A tick made just before clicking through still
    // reads back from chosenPieces() — the apply step will see it.
    void navigatingAwayStillSavesTheSitting()
    {
        TaskDetailDialog dialog(QStringLiteral("Study for finals"), {},
                                QDate(), QTime(), Task::Repeat::None,
                                Task::Priority::Medium, 0, false);

        TaskDetailDialog::Piece read;
        read.id    = QStringLiteral("piece-read");
        read.title = QStringLiteral("Read the spec");
        TaskDetailDialog::Piece ch3;
        ch3.id    = QStringLiteral("piece-ch3");
        ch3.title = QStringLiteral("Chapter 3");
        dialog.seedPieces({read, ch3});

        // Tick the first piece, then click through to the second.
        auto boxes = dialog.findChildren<QCheckBox*>();
        QCheckBox* firstBox = nullptr;
        for (QCheckBox* b : boxes)
            if (b->text().isEmpty()) { firstBox = b; break; } // piece boxes
        QVERIFY(firstBox);                                    // are textless
        firstBox->setChecked(true);

        auto opens = dialog.findChildren<QPushButton*>(
            QStringLiteral("pieceOpenButton"));
        QCOMPARE(opens.size(), 2);
        opens.last()->click();

        QCOMPARE(dialog.navigationTarget(), QStringLiteral("piece-ch3"));
        const auto answers = dialog.chosenPieces();
        QCOMPARE(answers.size(), 2);
        QVERIFY(answers[0].done); // the tick survived the hop
    }

    // A newborn line (typed here, id still empty) offers no door — there
    // is nothing to navigate TO until Save creates it. One seeded piece,
    // one newborn: exactly one open button. (This is a PARENT's dialog,
    // so no breadcrumb — the two never co-occur, per the one-level rule.)
    void newbornPiecesHaveNoDoorUntilSaved()
    {
        TaskDetailDialog dialog(QStringLiteral("Study for finals"), {},
                                QDate(), QTime(), Task::Repeat::None,
                                Task::Priority::Medium, 0, false);

        TaskDetailDialog::Piece real;
        real.id    = QStringLiteral("piece-real");
        real.title = QStringLiteral("Seeded");
        TaskDetailDialog::Piece newborn; // id stays empty: born here
        newborn.title = QStringLiteral("Typed this sitting");
        dialog.seedPieces({real, newborn});

        QCOMPARE(dialog.findChildren<QPushButton*>(
                     QStringLiteral("pieceOpenButton")).size(), 1);
    }

    // A PIECE's dialog: breadcrumb up top, no checklist at all. Clicking
    // the crumb records the parent id and accepts — same save-then-go
    // contract as the piece title, tested from the other end of the link.
    void theBreadcrumbGoesUpAndSaves()
    {
        TaskDetailDialog dialog(QStringLiteral("Chapter 3"), {},
                                QDate(), QTime(), Task::Repeat::None,
                                Task::Priority::Medium, 0, false);
        dialog.setBreadcrumb(QStringLiteral("task-finals"),
                             QStringLiteral("Study for finals"));

        auto* crumb = dialog.findChild<QPushButton*>(
            QStringLiteral("pieceBreadcrumb"));
        QVERIFY(crumb);
        crumb->click();
        QCOMPARE(dialog.navigationTarget(), QStringLiteral("task-finals"));
        QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));
    }
    // ---- v28.6 — the docked panel and explicit save ----------------------

    // The save contract, both feedback directions: the button lights on a
    // real difference, applying writes to AppData, and the button goes
    // quiet after — with the ✓ flash shown.
    void thePanelSavesExplicitlyWithFeedback()
    {
        AppData data;
        data.addCategory("School", QColor("#4C6FE0"));
        const QString c  = data.categories().first().id;
        const QString id = data.addTask("Study for finals", c,
                                        QDate(2026, 8, 8));

        TaskDetailPanel panel(&data);
        panel.openTask(id);
        QVERIFY(panel.isOpen());

        auto* save = panel.findChild<QPushButton*>(
            QStringLiteral("panelSaveButton"));
        QVERIFY(save);
        QVERIFY(!save->isEnabled()); // clean form: nothing to save

        QLineEdit* title = nullptr;
        for (QLineEdit* e : panel.findChildren<QLineEdit*>())
            if (e->placeholderText() == QStringLiteral("What needs doing?"))
                title = e;
        QVERIFY(title);
        title->setText(QStringLiteral("Study for finals — ch. 1-4"));
        QVERIFY(save->isEnabled()); // lit: you have unsaved work

        save->click();
        QCOMPARE(data.taskById(id)->title,
                 QStringLiteral("Study for finals — ch. 1-4"));
        QVERIFY(!save->isEnabled()); // quiet again: saved truth == form
        QVERIFY(!panel.findChild<QLabel*>(
                          QStringLiteral("panelSavedFlash"))->isHidden());

        // And the honest-flag property: retyping the ORIGINAL goes quiet
        // without saving — dirty is a comparison, not an accumulator.
        title = nullptr;
        for (QLineEdit* e : panel.findChildren<QLineEdit*>())
            if (e->placeholderText() == QStringLiteral("What needs doing?"))
                title = e;
        QVERIFY(title);
        title->setText(QStringLiteral("edited"));
        QVERIFY(save->isEnabled());
        title->setText(QStringLiteral("Study for finals — ch. 1-4"));
        QVERIFY(!save->isEnabled());
    }

    // The guard: leaving dirty work asks first, and every answer does
    // exactly what it says. Stay = the click does nothing; Discard =
    // move on, nothing written; Save = written, then move on.
    void switchingTasksWhileDirtyAsksFirst()
    {
        AppData data;
        data.addCategory("School", QColor("#4C6FE0"));
        const QString c = data.categories().first().id;
        const QString a = data.addTask("Task A", c, QDate(2026, 8, 8));
        const QString b = data.addTask("Task B", c, QDate(2026, 8, 9));

        TaskDetailPanel panel(&data);
        panel.openTask(a);

        auto editTitle = [&panel](const QString& text) {
            for (QLineEdit* e : panel.findChildren<QLineEdit*>())
                if (e->placeholderText()
                    == QStringLiteral("What needs doing?"))
                    e->setText(text);
        };

        editTitle(QStringLiteral("Task A edited"));

        panel.setUnsavedPromptForTests(
            []() { return TaskDetailPanel::UnsavedChoice::Stay; });
        panel.openTask(b);
        QCOMPARE(panel.currentTaskId(), a); // Stay means STAY

        panel.setUnsavedPromptForTests(
            []() { return TaskDetailPanel::UnsavedChoice::Discard; });
        panel.openTask(b);
        QCOMPARE(panel.currentTaskId(), b);
        QCOMPARE(data.taskById(a)->title,
                 QStringLiteral("Task A")); // nothing was written

        editTitle(QStringLiteral("Task B edited"));
        panel.setUnsavedPromptForTests(
            []() { return TaskDetailPanel::UnsavedChoice::Save; });
        panel.openTask(a);
        QCOMPARE(panel.currentTaskId(), a);
        QCOMPARE(data.taskById(b)->title,
                 QStringLiteral("Task B edited")); // saved on the way out
    }

    // Navigation is a swap-in-place: clicking a piece's title re-points
    // the SAME panel (breadcrumb appears), the breadcrumb points back —
    // no dialog, no close-and-reopen. Clean forms hop without questions.
    void pieceClickSwapsThePanelInPlace()
    {
        AppData data;
        data.addCategory("School", QColor("#4C6FE0"));
        const QString c = data.categories().first().id;
        const QString parent =
            data.addTask("Study for finals", c, QDate(2026, 8, 8));
        const QString piece = data.addSubtask(parent, "Chapter 3");
        QVERIFY(!piece.isEmpty());

        TaskDetailPanel panel(&data);
        panel.openTask(parent);

        auto* open = panel.findChild<QPushButton*>(
            QStringLiteral("pieceOpenButton"));
        QVERIFY(open);
        open->click();
        QCoreApplication::processEvents(); // the queued swap lands

        QCOMPARE(panel.currentTaskId(), piece);
        auto* crumb = panel.findChild<QPushButton*>(
            QStringLiteral("pieceBreadcrumb"));
        QVERIFY(crumb); // the piece's form, way back up included

        crumb->click();
        QCoreApplication::processEvents();
        QCOMPARE(panel.currentTaskId(), parent); // and back — same panel
    }

    // v28.6.1 — "click anywhere else closes" never means "click anywhere
    // else discards": the scrim routes through the SAME guard as ✕ and
    // Esc. Stay keeps the panel (and the click lands on nothing);
    // Discard closes it.
    void clickingOutsideRunsTheSameGuard()
    {
        AppData data;
        data.addCategory("School", QColor("#4C6FE0"));
        const QString c  = data.categories().first().id;
        const QString id = data.addTask("Task A", c, QDate(2026, 8, 8));

        QWidget host;
        host.resize(900, 600);
        host.show();

        auto* panel = new TaskDetailPanel(&data, &host);
        panel->openTask(id);
        QVERIFY(panel->isOpen());

        auto* scrim = host.findChild<QWidget*>(QStringLiteral("panelScrim"));
        QVERIFY(scrim);
        QVERIFY(!scrim->isHidden()); // the lower-contrast layer is up

        for (QLineEdit* e : panel->findChildren<QLineEdit*>())
            if (e->placeholderText() == QStringLiteral("What needs doing?"))
                e->setText(QStringLiteral("edited"));

        panel->setUnsavedPromptForTests(
            []() { return TaskDetailPanel::UnsavedChoice::Stay; });
        QTest::mouseClick(scrim, Qt::LeftButton);
        QVERIFY(panel->isOpen()); // Stay: the stray click cost nothing

        panel->setUnsavedPromptForTests(
            []() { return TaskDetailPanel::UnsavedChoice::Discard; });
        QTest::mouseClick(scrim, Qt::LeftButton);
        QVERIFY(!panel->isOpen());
        QCOMPARE(data.taskById(id)->title,
                 QStringLiteral("Task A")); // discarded, not written
    }

    // v28.6.2 — the Theme.h v3 trap, pinned at its second detonation:
    // QScrollArea::setWidget() flips the child's autoFillBackground ON,
    // and a form that fills itself paints palette grey inside the white
    // panel (the owner's patchwork screenshot). The panel must switch it
    // back off after EVERY setWidget — including the rebuilds a save and
    // a navigation do.
    void theFormNeverFillsItsOwnBackground()
    {
        AppData data;
        data.addCategory("School", QColor("#4C6FE0"));
        const QString c  = data.categories().first().id;
        const QString id = data.addTask("Task A", c, QDate(2026, 8, 8));

        TaskDetailPanel panel(&data);
        panel.openTask(id);
        auto* form = panel.findChild<TaskDetailForm*>();
        QVERIFY(form);
        QVERIFY(!form->autoFillBackground());

        // And after the rebuild a save performs — a fresh form, a fresh
        // setWidget, the trap re-armed.
        panel.findChild<QPushButton*>(
                 QStringLiteral("panelSaveButton"));
        for (QLineEdit* e : panel.findChildren<QLineEdit*>())
            if (e->placeholderText() == QStringLiteral("What needs doing?"))
                e->setText(QStringLiteral("edited"));
        panel.findChild<QPushButton*>(
                 QStringLiteral("panelSaveButton"))->click();
        form = panel.findChild<TaskDetailForm*>();
        QVERIFY(form);
        QVERIFY(!form->autoFillBackground());
    }

    // v28.7 — the TickTick door: right-click → "Add a piece" lands in
    // startPieceUnder. Create-first-name-second: a real "New piece"
    // subtask exists immediately; and aimed at a PIECE, the domain's
    // one-level guard turns the whole thing into a no-op.
    void startPieceCreatesUnderParentOnly()
    {
        AppData data;
        data.addCategory("School", QColor("#4C6FE0"));
        const QString c = data.categories().first().id;
        const QString parent =
            data.addTask("Finals", c, QDate(2026, 8, 20));

        // Page AND panel under one window — the shipped arrangement, and
        // the reason this test can run at all: with no panel in the
        // window, runTaskDetailNaming falls back to the MODAL dialog,
        // whose exec() blocks forever under offscreen ctest. (Caught by
        // this very test hanging: the fallback is a real code path with a
        // real cost — panel-less windows get a blocking session.)
        QWidget host;
        auto* page  = new ActivitiesPage(&data, &host);
        auto* panel = new TaskDetailPanel(&data, &host);

        page->startPieceUnder(parent);

        const auto pieces = data.subtasksOf(parent);
        QCOMPARE(pieces.size(), 1);
        QCOMPARE(pieces.first()->title, QStringLiteral("New piece"));
        QCOMPARE(panel->currentTaskId(),
                 pieces.first()->id); // the panel opened ON the newborn

        // Aim it at the piece itself: the one-level rule holds — no
        // grandchildren, and no crash from the empty-id early return.
        page->startPieceUnder(pieces.first()->id);
        QCOMPARE(data.subtasksOf(pieces.first()->id).size(), 0);
    }

    // v28.8 — the SIZE dropdown speaks hours past the hour mark, and the
    // ladder's steps are the designed ones (16h cap: past two workdays,
    // break it into pieces instead).
    void estimateDropdownSpeaksHours()
    {
        TaskDetailForm form(QStringLiteral("Finals"), {}, QDate(), QTime(),
                            Task::Repeat::None, Task::Priority::Medium,
                            /*estimateMinutes=*/720, false);
        auto* combo = form.findChild<QComboBox*>(
            QStringLiteral("estimateCombo"));
        QVERIFY(combo);
        QCOMPARE(combo->currentText(), QStringLiteral("12h"));
        QCOMPARE(form.chosenEstimateMinutes(), 720);
        QCOMPARE(combo->itemData(combo->count() - 1).toInt(), 960); // 16h cap
        QCOMPARE(combo->maxVisibleItems(), 6); // v28.9.1 — popup scrolls
        QVERIFY(combo->findData(15) > 0);  // "Fits short gaps"' own number
        QVERIFY(combo->findData(90) > 0);  // half-hour rungs through 8h
        QCOMPARE(combo->findData(510), -1); // no 8h30 — whole hours past 8h
    }

    // Values off the ladder (the spinbox era, parsed captures) survive
    // the control swap: inserted at their sorted rung, never snapped —
    // opening the panel must not be an edit.
    void oddEstimatesSurviveTheDropdown()
    {
        TaskDetailForm form(QStringLiteral("Finals"), {}, QDate(), QTime(),
                            Task::Repeat::None, Task::Priority::Medium,
                            /*estimateMinutes=*/100, false);
        auto* combo = form.findChild<QComboBox*>(
            QStringLiteral("estimateCombo"));
        QVERIFY(combo);
        QCOMPARE(form.chosenEstimateMinutes(), 100);
        QCOMPARE(combo->currentText(), QStringLiteral("1h 40m"));
        QVERIFY(!form.isDirty()); // seeding an odd value is not an edit
        // Sorted into place: 1h 30m sits just above, 2h just below.
        const int at = combo->currentIndex();
        QCOMPARE(combo->itemData(at - 1).toInt(), 90);
        QCOMPARE(combo->itemData(at + 1).toInt(), 120);
    }

    // ---- v28.10: the debug seams ------------------------------------------

    // The rehearsal button's whole contract in one scene: the gate would
    // say NO (empty day, 15:00 — no morning window, nothing heavy), the
    // offer fires anyway, and the ledger is NOT spent — the real morning's
    // one ask survives the rehearsal.
    void checkInForceOfferSkipsGateAndSparesLedger()
    {
        QSettings().remove(QStringLiteral("checkin/lastOffered"));

        AppData data; // deliberately empty: nothing about this day is heavy
        CheckInService svc(&data);
        svc.setNowProvider([] {
            return QDateTime(QDate(2026, 7, 20), QTime(15, 0));
        });

        QSignalSpy offered(&svc, &CheckInService::offer);

        svc.sweep(); // the honest gate: afternoon + light day = silence
        QCOMPARE(offered.count(), 0);

        svc.forceOffer(); // the rehearsal: same signal, gate skipped
        QCOMPARE(offered.count(), 1);
        QVERIFY(QSettings()
                    .value(QStringLiteral("checkin/lastOffered"))
                    .toString()
                    .isEmpty()); // nothing spent

        // And the ledger door the panel presses: set, clear, gone.
        QSettings().setValue(QStringLiteral("checkin/lastOffered"),
                             QStringLiteral("2026-07-20"));
        CheckInService::clearTodaysAsk();
        QVERIFY(QSettings()
                    .value(QStringLiteral("checkin/lastOffered"))
                    .toString()
                    .isEmpty());
    }

    // The panel is glass: every control presses an existing seam and
    // decides nothing. Driven by objectName — the same findChild contract
    // the Settings pages defend — so a reworded button label can never
    // break this test, and a renamed seam always will.
    void debugPanelPressesTheSeams()
    {
        AppData data;
        AffordabilityService afford(&data);
        CheckInService checkIn(&data);

        int  clockCalls   = 0;
        bool lastWasReal  = false;
        bool briefedOnce  = false;
        int  injections   = 0;
        int  interviews   = 0;
        DebugPanel panel(
            &afford, &checkIn,
            [&] {
                briefedOnce = true;
                return QStringLiteral("BRIEFING TEXT");
            },
            [&](const std::optional<QDateTime>& fake) {
                ++clockCalls;
                lastWasReal = !fake.has_value();
            },
            [&]() -> QString {
                ++injections;
                return QStringLiteral("INJECTED");
            },
            [&]() -> QString {
                ++interviews;
                return QStringLiteral("INTERVIEWING");
            });

        // Force check-in reaches the service's rehearsal door.
        QSignalSpy offered(&checkIn, &CheckInService::offer);
        auto* force =
            panel.findChild<QPushButton*>(QStringLiteral("debugForceCheckIn"));
        QVERIFY(force);
        force->click();
        QCOMPARE(offered.count(), 1);

        // The clock buttons hand the composition root a moment / a nullopt.
        auto* apply =
            panel.findChild<QPushButton*>(QStringLiteral("debugApplyClock"));
        auto* real =
            panel.findChild<QPushButton*>(QStringLiteral("debugRealClock"));
        QVERIFY(apply && real);
        apply->click();
        QCOMPARE(clockCalls, 1);
        QVERIFY(!lastWasReal);
        real->click();
        QCOMPARE(clockCalls, 2);
        QVERIFY(lastWasReal);

        // The briefing viewer fetches FRESH text per press (derived state,
        // never cached — the catch-up drawer's stale-print lesson).
        auto* show =
            panel.findChild<QPushButton*>(QStringLiteral("debugShowBriefing"));
        QVERIFY(show);
        show->click();
        QVERIFY(briefedOnce);

        // The injector presses the composition root's lambda and shows
        // its answer — the panel neither composes nor judges proposals.
        auto* inject = panel.findChild<QPushButton*>(
            QStringLiteral("debugInjectProposal"));
        auto* injectStatus =
            panel.findChild<QLabel*>(QStringLiteral("debugInjectStatus"));
        QVERIFY(inject && injectStatus);
        inject->click();
        QCOMPARE(injections, 1);
        QCOMPARE(injectStatus->text(), QStringLiteral("INJECTED"));

        auto* interview = panel.findChild<QPushButton*>(
            QStringLiteral("debugStartIntake"));
        QVERIFY(interview);
        interview->click();
        QCOMPARE(interviews, 1);
        QCOMPARE(injectStatus->text(), QStringLiteral("INTERVIEWING"));

        // The AI switch owns the wildcard, this process only.
        auto* down =
            panel.findChild<QCheckBox*>(QStringLiteral("debugAiDown"));
        QVERIFY(down);
        QVERIFY(!down->isChecked()); // no env set → box agrees
        down->setChecked(true);
        QCOMPARE(qEnvironmentVariable("TICKTIMER_AI_DOWN"),
                 QStringLiteral("*"));
        QVERIFY(ai::forcedDown(QStringLiteral("anthropic"))); // wire closed
        down->setChecked(false);
        QVERIFY(qEnvironmentVariable("TICKTIMER_AI_DOWN").isEmpty());
        QVERIFY(!ai::forcedDown(QStringLiteral("anthropic")));
    }

    // v29.0.2 — the SOURCE fix: whatever slash-bearing thing seeds or is
    // typed into the login dialog's server field, the value it hands the
    // rest of the program is born clean. (v29.0.1 normalized in one
    // consumer and armed the next; this is the fix at the birth.)
    void loginDialogServerUrlIsBornClean()
    {
        LoginDialog d(QStringLiteral("http://10.61.241.202:8080/"));
        QCOMPARE(d.serverUrl(),
                 QStringLiteral("http://10.61.241.202:8080"));
    }

    // ---- v29.0: the write boundary in the UI ------------------------------

    // The card is glass: born-valid enables Apply, born-broken shows the
    // reason and never does; settle() kills both buttons and is the
    // container's ONLY channel back into it.
    void proposalCardShowsAndSettles()
    {
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QString id  = data.addTask("Lab 4", cat);
        verbs::HandleMap handles;
        verbs::Proposal p;
        p.targetHandle    = handles.addTask(id);
        p.estimateMinutes = 90;

        ProposalCard ok(p, p.summary(data, handles),
                        verbs::Verdict{ true, QString() });
        auto* apply =
            ok.findChild<QPushButton*>(QStringLiteral("proposalApply"));
        auto* discard =
            ok.findChild<QPushButton*>(QStringLiteral("proposalDiscard"));
        QVERIFY(apply && discard);
        QVERIFY(apply->isEnabled());
        QSignalSpy applied(&ok, &ProposalCard::applyRequested);
        apply->click();
        QCOMPARE(applied.count(), 1);
        ok.settle(QStringLiteral("Applied."));
        QVERIFY(!apply->isEnabled());
        QVERIFY(!discard->isEnabled());

        ProposalCard broken(p, p.summary(data, handles),
                            verbs::Verdict{ false,
                                            QStringLiteral("No.") });
        auto* brokenApply =
            broken.findChild<QPushButton*>(QStringLiteral("proposalApply"));
        QVERIFY(brokenApply);
        QVERIFY(!brokenApply->isEnabled()); // readable refusal, dead button
    }

    // The whole road through the page: briefing refreshes the handle
    // world, the card lands in the log, the tap runs preApplyHook FIRST,
    // the estimate lands through the guarded door, and the receipt enters
    // the transcript. Discard on a second card changes nothing.
    void chatAppliesProposalsThroughTheBoundary()
    {
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QString id  = data.addTask("Lab 4", cat); // unsized, undated

        ChatPage page(&data);
        int copiedAside = 0;
        page.preApplyHook = [&] { ++copiedAside; };

        page.currentBriefing(); // this turn's names
        const int at = page.handles().taskIds.indexOf(id);
        QVERIFY(at >= 0);

        verbs::Proposal p;
        p.targetHandle    = QStringLiteral("T%1").arg(at + 1);
        p.estimateMinutes = 120;
        page.presentProposal(p, verbs::Role::Intake);

        auto* apply =
            page.findChild<QPushButton*>(QStringLiteral("proposalApply"));
        QVERIFY(apply);
        apply->click();
        QCOMPARE(copiedAside, 1); // aside BEFORE the mutation
        QCOMPARE(data.taskById(id)->estimateMinutes, 120);
        QVERIFY(!apply->isEnabled()); // settled

        // Discard: a second proposal for a fresh task dies untouched.
        const QString id2 = data.addTask("Essay", cat);
        page.currentBriefing();
        const int at2 = page.handles().taskIds.indexOf(id2);
        QVERIFY(at2 >= 0);
        verbs::Proposal p2;
        p2.targetHandle    = QStringLiteral("T%1").arg(at2 + 1);
        p2.estimateMinutes = 60;
        page.presentProposal(p2, verbs::Role::Intake);
        const auto discards =
            page.findChildren<QPushButton*>(QStringLiteral("proposalDiscard"));
        QVERIFY(!discards.isEmpty());
        discards.last()->click(); // the newest card
        QCOMPARE(data.taskById(id2)->estimateMinutes, 0); // untouched
        QCOMPARE(copiedAside, 1); // no aside for a discard
    }

    // ---- v29.1: the interview in the room ---------------------------------

    // The guess path, whole road, zero model: history builds the guess,
    // beginIntake asks with it, one tap composes the proposal, the card
    // confirms, the door fills the blank. The chained continuation then
    // finds nothing else and closes out loud.
    void intakeGuessPathEndToEnd()
    {
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QDate day(2026, 7, 6);
        for (int i = 0; i < 2; ++i) { // the history behind the guess
            const QString t = data.addTask(QString("Old %1").arg(i), cat);
            const QString e = data.addTaskEvent(day.addDays(i), 540, 700, t);
            Segment seg; // test_domain's makeSegment, inlined — this suite
                         // has no fixture header to share (yet; rule of
                         // three watches)
            seg.kind  = SegmentKind::Focus;
            seg.start = QDateTime(day.addDays(i), QTime(9, 0));
            seg.end   = seg.start.addSecs(120 * 60);
            data.appendSegment(e, seg);
            data.setTaskDone(t, true);
        }
        const QString id =
            data.addTask("Lab 4", cat, QDate::currentDate().addDays(5));

        ChatPage page(&data);
        QVERIFY(page.beginIntake());

        auto* yes =
            page.findChild<QPushButton*>(QStringLiteral("intakeGuess"));
        QVERIFY(yes); // the history spoke
        yes->click();

        auto* apply =
            page.findChild<QPushButton*>(QStringLiteral("proposalApply"));
        QVERIFY(apply);
        apply->click();
        QCOMPARE(data.taskById(id)->estimateMinutes, 120); // the median,
                                                           // through the
                                                           // card, through
                                                           // the door
        QTest::qWait(20); // let the chained beginIntake run and close out
    }

    // Skip is the OWNER's act through the domain door: dismissed for a
    // year, never re-asked, and a fresh beginIntake finds nothing.
    void intakeSkipDismissesOnce()
    {
        AppData data;
        const QString cat = data.addCategory("School", QColor("#4C6FE0"));
        const QString id =
            data.addTask("Lab 4", cat, QDate::currentDate().addDays(5));

        ChatPage page(&data);
        QVERIFY(page.beginIntake());
        auto* skip =
            page.findChild<QPushButton*>(QStringLiteral("intakeSkip"));
        QVERIFY(skip);
        skip->click();

        QVERIFY(data.taskById(id)->dismissedUntil.isValid());
        QVERIFY(data.taskById(id)->dismissedUntil
                > QDateTime::currentDateTime().addDays(300));
        QTest::qWait(20); // the deferred continuation runs, finds nothing
        QVERIFY(!page.beginIntake()); // ask once means once
    }
};

QTEST_MAIN(TestUi)
#include "test_ui.moc"
