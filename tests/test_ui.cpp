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
// domain tests, which is why there are 33 of those and this suite is
// reserved for bugs that only a living widget tree can express.
// ---------------------------------------------------------------------------

#include "ActivitiesPage.h"
#include "AppData.h"
#include "CompareDialog.h"
#include "EventDialog.h"
#include "GlancePanel.h"
#include "JsonStore.h"
#include "LoginDialog.h"
#include "UpdateBanner.h"
#include "TrackerService.h"

#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>

#include <QLineEdit>
#include <QSettings>
#include <QTreeWidget>
#include <QtTest>

class TestUi : public QObject
{
    Q_OBJECT

private slots:
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
        QPointer<QLineEdit> aliveDuringSignal(input);
        QTest::keyClicks(input, "Lab 4");
        QTest::keyClick(input, Qt::Key_Return);
        QVERIFY2(!aliveDuringSignal.isNull(),
                 "detail panel was destroyed DURING its own signal "
                 "(delete instead of deleteLater in rebuildDetail)");

        // Drain the event loop: NOW the deferred destruction runs — and the
        // rebuilt panel must have replaced the old one without incident.
        QTest::qWait(50);
        QVERIFY(aliveDuringSignal.isNull()); // old panel did die — later, safely

        // And the feature still works: the task exists, exactly one of it.
        QCOMPARE(data.tasks().size(), 1);
        QCOMPARE(data.tasks()[0].title, QString("Lab 4"));
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
        // This test is about ATTRIBUTION, not liveness — so the §3.38
        // guard's clock is pinned to noon via the seam, and the block spans
        // the domain's whole day (06:00–24:00; 0–1440 is refused at the
        // door). Live seconds still come from the real clock — that part
        // IS under test. (This line broke red the moment the guard landed:
        // the constraint auditing its own test suite.)
        const QString ev  = data.addEvent(QDate::currentDate(), 360, 1440, act);

        TrackerService tracker(&data);
        tracker.nowProvider = [] {
            return QDateTime(QDate::currentDate(), QTime(12, 0));
        };
        tracker.startDistracted(ev);

        GlancePanel panel(&data, &tracker);
        panel.show();
        QTest::qWait(1100);                  // let ~1s of LIVE time accrue
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
        CompareDialog dialog(minePtr, "mom", blob);
        dialog.showDay(day); // pin the date — never trust "today" in a test
        dialog.show();

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
};

QTEST_MAIN(TestUi)
#include "test_ui.moc"
