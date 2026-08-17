// ---------------------------------------------------------------------------
// test_taskmodel.cpp — the data side of the app's first model/view screen.
//
// TaskListModel and TaskFilterProxy speak the QAbstractItemModel interface, so
// we can test them the way a QListView "sees" them — ask rowCount(), read roles
// off an index — with NO widgets and NO QApplication (QTEST_GUILESS_MAIN gives
// a QCoreApplication; QColor needs no GUI to be a value). The delegate is paint
// code, proven by test_ui driving the real page; here we pin the truth the
// delegate merely renders: which rows exist, what each role answers, and that
// the proxy filters and sorts correctly.
//
// Why this suite matters as a lesson: it shows that a model IS unit-testable in
// a way a rebuilt-on-change widget tree never was. The old Upcoming page could
// only be checked by hunting QPushButtons in a live widget tree (slow, in
// test_ui); the model answers questions directly, in microseconds.
// ---------------------------------------------------------------------------

#include "AppData.h"
#include "CategoryTaskModel.h"
#include "TaskFilterProxy.h"
#include "TaskListModel.h"

#include <QtTest>

using namespace taskmodel;

class TestTaskModel : public QObject
{
    Q_OBJECT

private slots:
    // The model must show exactly what upcomingTasks() shows: dated AND undone.
    void modelHoldsOnlyDatedUndoneTasks()
    {
        AppData data;
        const QString c = data.addCategory("School", QColor("#4C6FE0"));
        data.addTask("Has date",  c, QDate(2026, 8, 1));
        data.addTask("No date",   c);                       // TBD → excluded
        const QString done = data.addTask("Finished", c, QDate(2026, 8, 2));
        data.setTaskDone(done, true);                       // done → excluded

        TaskListModel model(&data);
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(model.index(0, 0), TitleRole).toString(),
                 QStringLiteral("Has date"));
    }

    // Every role the delegate asks for must answer with the task's real data.
    void rolesExposeTaskFields()
    {
        AppData data;
        const QColor  colour = QColor("#4C6FE0");
        const QString c = data.addCategory("School", colour);
        const QString id = data.addTask("Lab 4", c, QDate::currentDate());
        data.setTaskPriority(id, Task::Priority::Urgent);

        TaskListModel model(&data);
        const QModelIndex i = model.index(0, 0);

        QCOMPARE(i.data(IdRole).toString(), id);
        QCOMPARE(i.data(TitleRole).toString(), QStringLiteral("Lab 4"));
        QCOMPARE(i.data(Qt::DisplayRole).toString(), QStringLiteral("Lab 4"));
        QCOMPARE(i.data(CategoryNameRole).toString(), QStringLiteral("School"));
        QCOMPARE(i.data(CategoryColorRole).value<QColor>(), colour);
        QCOMPARE(i.data(PriorityRole).toInt(), int(Task::Priority::Urgent));
        QCOMPARE(i.data(DaysUntilRole).toLongLong(), qint64(0)); // due today
    }

    // Bucket classification is the section-header engine — pin it to "today".
    void bucketRoleClassifiesRelativeToToday()
    {
        AppData data;
        const QString c = data.addCategory("School", QColor("#4C6FE0"));
        const QDate today = QDate::currentDate();
        data.addTask("Overdue",  c, today.addDays(-3));
        data.addTask("ThisWeek", c, today.addDays(2));
        data.addTask("Later",    c, today.addDays(30));

        TaskListModel model(&data);
        // Rows arrive due-date sorted, so index order is overdue→week→later.
        QCOMPARE(model.data(model.index(0, 0), BucketRole).toInt(), 0);
        QCOMPARE(model.data(model.index(1, 0), BucketRole).toInt(), 1);
        QCOMPARE(model.data(model.index(2, 0), BucketRole).toInt(), 2);
    }

    // The model listens to AppData::changed — no manual refresh needed.
    void modelResnapshotsOnDataChange()
    {
        AppData data;
        const QString c = data.addCategory("School", QColor("#4C6FE0"));
        data.addTask("First", c, QDate(2026, 8, 1));

        TaskListModel model(&data);
        QCOMPARE(model.rowCount(), 1);

        const QString id = data.addTask("Second", c, QDate(2026, 8, 2));
        QCOMPARE(model.rowCount(), 2);   // grew by itself

        data.setTaskDone(id, true);
        QCOMPARE(model.rowCount(), 1);   // shrank by itself (done → gone)
    }

    // The proxy's filter hides every row of a different priority, reversibly.
    void proxyFilterHidesOtherPriorities()
    {
        AppData data;
        const QString c = data.addCategory("School", QColor("#4C6FE0"));
        const QString urgent = data.addTask("Fire", c, QDate(2026, 8, 1));
        data.addTask("Someday", c, QDate(2026, 8, 2)); // stays Medium
        data.setTaskPriority(urgent, Task::Priority::Urgent);

        TaskListModel model(&data);
        TaskFilterProxy proxy;
        proxy.setSourceModel(&model);
        QCOMPARE(proxy.rowCount(), 2);                 // All

        proxy.setPriorityFilter(int(Task::Priority::Urgent));
        QCOMPARE(proxy.rowCount(), 1);
        QCOMPARE(proxy.index(0, 0).data(TitleRole).toString(),
                 QStringLiteral("Fire"));

        proxy.setPriorityFilter(-1);                   // back to All
        QCOMPARE(proxy.rowCount(), 2);
    }

    // ----- granular updates (v20.1): the model emits the NARROWEST signal -----

    // The proxy sorts by due date, ties broken by title — even if inserted out
    // of order, the view sees soonest-first.
    void proxySortsByDueDateThenTitle()
    {
        AppData data;
        const QString c = data.addCategory("School", QColor("#4C6FE0"));
        data.addTask("Later one",   c, QDate(2026, 9, 1));
        data.addTask("Sooner",      c, QDate(2026, 8, 1));
        data.addTask("Also Aug 1",  c, QDate(2026, 8, 1)); // tie → by title

        TaskListModel model(&data);
        TaskFilterProxy proxy;
        proxy.setSourceModel(&model);

        QCOMPARE(proxy.index(0, 0).data(TitleRole).toString(),
                 QStringLiteral("Also Aug 1")); // Aug 1, "Also" < "Sooner"
        QCOMPARE(proxy.index(1, 0).data(TitleRole).toString(),
                 QStringLiteral("Sooner"));      // Aug 1
        QCOMPARE(proxy.index(2, 0).data(TitleRole).toString(),
                 QStringLiteral("Later one"));   // Sep 1
    }

    // Editing a visible field repaints ONE row — no reset, no structural churn.
    void editingTitleEmitsDataChangedOnly()
    {
        AppData data;
        const QString c = data.addCategory("School", QColor("#4C6FE0"));
        const QString id = data.addTask("Lab 4", c, QDate(2026, 8, 1));

        TaskListModel model(&data);
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
        QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);

        data.updateTask(id, "Lab 4 (revised)", "", QDate(2026, 8, 1), QTime(),
                        Task::Repeat::None, Task::Priority::Medium);

        QCOMPARE(changed.count(), 1); // exactly the one edited row
        QCOMPARE(reset.count(), 0);   // NOT the sledgehammer
        QCOMPARE(removed.count(), 0);
        QCOMPARE(model.data(model.index(0, 0), TitleRole).toString(),
                 QStringLiteral("Lab 4 (revised)"));
    }

    // Editing an INVISIBLE field (description) repaints nothing — the card never
    // shows it, so no role moved, so no signal. Surgical to a fault, on purpose.
    void editingOnlyDescriptionEmitsNothing()
    {
        AppData data;
        const QString c = data.addCategory("School", QColor("#4C6FE0"));
        const QString id = data.addTask("Lab 4", c, QDate(2026, 8, 1));

        TaskListModel model(&data);
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);

        data.updateTask(id, "Lab 4", "brand new notes", QDate(2026, 8, 1), QTime(),
                        Task::Repeat::None, Task::Priority::Medium);

        QCOMPARE(changed.count(), 0); // description isn't a card role
        QCOMPARE(reset.count(), 0);
    }

    // Completing a task removes exactly its row — scroll/selection would survive.
    void completingTaskRemovesOneRow()
    {
        AppData data;
        const QString c = data.addCategory("School", QColor("#4C6FE0"));
        data.addTask("Keep", c, QDate(2026, 8, 1));
        const QString id = data.addTask("Finish me", c, QDate(2026, 8, 2));

        TaskListModel model(&data);
        QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);

        data.setTaskDone(id, true);

        QCOMPARE(removed.count(), 1);
        QCOMPARE(reset.count(), 0);
        QCOMPARE(model.rowCount(), 1);
    }

    // Adding a dated task inserts exactly one row, at its sorted position.
    void addingTaskInsertsOneRow()
    {
        AppData data;
        const QString c = data.addCategory("School", QColor("#4C6FE0"));
        data.addTask("Existing", c, QDate(2026, 8, 2));

        TaskListModel model(&data);
        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);

        data.addTask("Newer", c, QDate(2026, 8, 1)); // earlier → sorts to front

        QCOMPARE(inserted.count(), 1);
        QCOMPARE(reset.count(), 0);
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.data(model.index(0, 0), TitleRole).toString(),
                 QStringLiteral("Newer"));
    }

    // A due-date edit that RE-SORTS a surviving row is the one case that resets.
    void reorderingByDueDateFallsBackToReset()
    {
        AppData data;
        const QString c = data.addCategory("School", QColor("#4C6FE0"));
        const QString a = data.addTask("Alpha", c, QDate(2026, 8, 1));
        data.addTask("Beta", c, QDate(2026, 8, 2)); // model rows: [Alpha, Beta]

        TaskListModel model(&data);
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);

        // Move Alpha's due date PAST Beta → survivors reorder to [Beta, Alpha].
        data.updateTask(a, "Alpha", "", QDate(2026, 8, 3), QTime(), Task::Repeat::None,
                        Task::Priority::Medium);

        QCOMPARE(reset.count(), 1); // the honest fallback
        QCOMPARE(model.data(model.index(0, 0), TitleRole).toString(),
                 QStringLiteral("Beta"));
    }

    // ----- CategoryTaskModel (v20.2): the parameterised, re-pointable list ----

    // Unlike Upcoming (dated + undone only), this model shows ALL non-archived
    // tasks of one category — done ones and undated "TBD" ones included.
    void categoryModelIncludesDoneAndTbdTasks()
    {
        AppData data;
        const QString c = data.addCategory("School", QColor("#4C6FE0"));
        const QString done = data.addTask("Finished", c, QDate(2026, 8, 1));
        data.setTaskDone(done, true);
        data.addTask("No date yet", c); // TBD — invalid due date

        CategoryTaskModel model(&data);
        model.setCategoryId(c);

        QCOMPARE(model.rowCount(), 2);
        bool sawDone = false, sawTbd = false;
        for (int i = 0; i < model.rowCount(); ++i) {
            const auto idx = model.index(i, 0);
            if (idx.data(cattask::DoneRole).toBool())
                sawDone = true;
            if (!idx.data(cattask::DueDateRole).toDate().isValid())
                sawTbd = true;
        }
        QVERIFY(sawDone); // done tasks stay until archived
        QVERIFY(sawTbd);  // TBD tasks are first-class here
    }

    // The star mechanic: re-pointing the model at another category swaps rows.
    // v28.7 — pieces ride directly under their parent as indented rows.
    // Pins BOTH halves of the family rule: interleave keeps a family
    // together (parent, its pieces, THEN the next parent — even when the
    // sort would order the second parent between them by date), and
    // archived pieces stay out.
    void categoryModelInterleavesPiecesUnderTheirParent()
    {
        AppData data;
        data.addCategory("School", QColor("#4C6FE0"));
        const QString c = data.categories().first().id;
        // Parent A due LATER than parent B: the sort puts B first among
        // parents; A's pieces must still hug A, not scatter around B.
        const QString a = data.addTask("Finals", c, QDate(2026, 8, 20));
        const QString b = data.addTask("Lab 4", c, QDate(2026, 8, 10));
        const QString p1 = data.addSubtask(a, "Chapter 10");
        const QString p2 = data.addSubtask(a, "Chapter 11");
        const QString hidden = data.addSubtask(a, "Chapter 12");
        data.setTaskArchived(hidden, true);

        CategoryTaskModel model(&data);
        model.setCategoryId(c);

        QCOMPARE(model.rowCount(), 4); // B, A, A.p1, A.p2 — no archived
        auto at = [&model](int row) {
            return model.index(row, 0);
        };
        QCOMPARE(at(0).data(cattask::IdRole).toString(), b);
        QVERIFY(!at(0).data(cattask::IsPieceRole).toBool());
        QCOMPARE(at(1).data(cattask::IdRole).toString(), a);
        QCOMPARE(at(2).data(cattask::IdRole).toString(), p1);
        QVERIFY(at(2).data(cattask::IsPieceRole).toBool());
        QCOMPARE(at(3).data(cattask::IdRole).toString(), p2);
        QVERIFY(at(3).data(cattask::IsPieceRole).toBool());
    }

    void categoryModelRepointsToAnotherCategory()
    {
        AppData data;
        const QString school = data.addCategory("School", QColor("#4C6FE0"));
        const QString health = data.addCategory("Health", QColor("#2F7E6E"));
        data.addTask("Lab 4", school);
        data.addTask("Run", health);
        data.addTask("Stretch", health);

        CategoryTaskModel model(&data);
        model.setCategoryId(school);
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.index(0, 0).data(cattask::TitleRole).toString(),
                 QStringLiteral("Lab 4"));

        model.setCategoryId(health); // re-point — the whole list changes
        QCOMPARE(model.rowCount(), 2);
    }

    // Archived tasks belong to the Archive page, never this list.
    void categoryModelSkipsArchivedTasks()
    {
        AppData data;
        const QString c = data.addCategory("School", QColor("#4C6FE0"));
        data.addTask("Visible", c);
        const QString gone = data.addTask("Retired", c);
        data.setTaskArchived(gone, true);

        CategoryTaskModel model(&data);
        model.setCategoryId(c);

        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.index(0, 0).data(cattask::TitleRole).toString(),
                 QStringLiteral("Visible"));
    }

    // ----- v20.3: Activities now updates INCREMENTALLY (shared base diff) -----

    // Toggling done within the current category flips ONE row — no reset.
    void categoryTogglingDoneEmitsDataChangedNotReset()
    {
        AppData data;
        const QString c = data.addCategory("School", QColor("#4C6FE0"));
        const QString id = data.addTask("Lab 4", c);
        CategoryTaskModel model(&data);
        model.setCategoryId(c);

        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
        data.setTaskDone(id, true);

        QCOMPARE(changed.count(), 1); // just the toggled row (done is a role here)
        QCOMPARE(reset.count(), 0);   // NOT a reset anymore
        QVERIFY(model.index(0, 0).data(cattask::DoneRole).toBool());
    }

    // Archiving a task removes exactly its row (it leaves the non-archived set).
    void categoryArchivingRemovesOneRow()
    {
        AppData data;
        const QString c = data.addCategory("School", QColor("#4C6FE0"));
        data.addTask("Keep", c);
        const QString gone = data.addTask("Retire me", c);
        CategoryTaskModel model(&data);
        model.setCategoryId(c);

        QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
        data.setTaskArchived(gone, true);

        QCOMPARE(removed.count(), 1);
        QCOMPARE(reset.count(), 0);
        QCOMPARE(model.rowCount(), 1);
    }

    // But RE-POINTING at another category is a context swap → a single reset.
    void categorySwitchStillResets()
    {
        AppData data;
        const QString a = data.addCategory("A", QColor("#4C6FE0"));
        const QString b = data.addCategory("B", QColor("#2F7E6E"));
        data.addTask("in A", a);
        data.addTask("in B", b);
        CategoryTaskModel model(&data);
        model.setCategoryId(a);

        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
        model.setCategoryId(b); // context swap, not an in-place edit
        QCOMPARE(reset.count(), 1);
    }
// ---- pieces (v28.3, roadmap §I) ---------------------------------------

    // The parents-only policy, seen from the view's side of the glass: a
    // piece — even a DATED one, which upcomingTasks would otherwise love —
    // never becomes a row. It lives inside its parent's detail panel.
    void pieceNeverBecomesARow()
    {
        AppData data;
        const QString c      = data.addCategory("School", QColor("#4C6FE0"));
        const QString parent = data.addTask("Lab 4", c, QDate(2026, 8, 8));
        data.addSubtask(parent, "Marc's section", QDate(2026, 8, 4),
                        QTime(17, 0));

        TaskListModel model(&data);
        QCOMPARE(model.rowCount(), 1); // the parent, alone
        QCOMPARE(model.data(model.index(0, 0), TitleRole).toString(),
                 QStringLiteral("Lab 4"));
    }

    // The chip's two roles, and the sidecar diff that keeps them honest:
    // ticking a PIECE changes no field of the parent's Task, so the base
    // class diff sees nothing — the model must notice on its own (the same
    // mechanism the affordability pill uses, tested the same way).
    void pieceChipRolesTrackProgress()
    {
        AppData data;
        const QString c      = data.addCategory("School", QColor("#4C6FE0"));
        const QString parent = data.addTask("Lab 4", c, QDate(2026, 8, 8));
        const QString plain  = data.addTask("Solo task", c, QDate(2026, 8, 9));
        const QString a = data.addSubtask(parent, "read");
        data.addSubtask(parent, "write");

        TaskListModel model(&data);
        const auto rowOf = [&](const QString& id) -> QModelIndex {
            for (int r = 0; r < model.rowCount(); ++r)
                if (model.data(model.index(r, 0), IdRole).toString() == id)
                    return model.index(r, 0);
            return {};
        };

        QCOMPARE(rowOf(parent).data(PiecesTotalRole).toInt(), 2);
        QCOMPARE(rowOf(parent).data(PiecesDoneRole).toInt(), 0);
        // No checklist -> 0 total: the delegate paints no chip at all.
        QCOMPARE(rowOf(plain).data(PiecesTotalRole).toInt(), 0);

        QSignalSpy moved(&model, &QAbstractItemModel::dataChanged);
        data.setTaskDone(a, true); // changed() -> refresh() -> sidecar diff
        QCOMPARE(rowOf(parent).data(PiecesDoneRole).toInt(), 1);
        QCOMPARE(rowOf(parent).data(PiecesTotalRole).toInt(), 2);

        // And the view was TOLD — at least one dataChanged carried the
        // pieces roles for the parent's row.
        bool announced = false;
        for (const auto& sig : moved) {
            const auto roles = sig.at(2).value<QVector<int>>();
            if (roles.contains(int(PiecesDoneRole)))
                announced = true;
        }
        QVERIFY(announced);
    }
};

QTEST_GUILESS_MAIN(TestTaskModel)
#include "test_taskmodel.moc"
