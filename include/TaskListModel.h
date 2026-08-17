#pragma once
// ---------------------------------------------------------------------------
// TaskListModel — the app's FIRST real Qt model/view class.
//
// Every other list in TickTimer is "rebuild-on-changed()": a page throws away
// all its child widgets and builds new ones from AppData on every change
// (see UpcomingPage's old buildContent, or ArchivePage). It works, and at this
// data size it can never go stale — but it is the OPPOSITE of how Qt wants you
// to show a list. This class is the other way, kept deliberately small so the
// pattern is legible:
//
//   AppData ──changed()──▶ TaskListModel ──▶ QSortFilterProxyModel ──▶ QListView
//     (truth)              (this file: an     (filter + sort, no        (paints,
//                           adapter over        copy of the data)        never
//                           the derived list)                            rebuilt)
//
// The point of a model is that a widget (the view) asks it QUESTIONS through a
// tiny fixed interface — rowCount() and data(index, role) — instead of the
// page hand-assembling a widget per row. "What's the title of row 3?" "What
// colour is it?" are ROLES; the view and its delegate ask, the model answers.
// Nobody keeps a second copy of the list in sync, because there is no second
// copy: the view reads through the model, live.
//
// WHY THIS MODEL HOLDS COPIES (a QVector<Task>, not pointers):
// AppData::upcomingTasks() hands back QVector<const Task*> — raw pointers INTO
// AppData's own vector, which can move in memory the instant a task is added
// or edited (the old card code copied *task "by value — the vector may move on
// edit" for exactly this reason). A model that stored those pointers would
// dangle. So on every refresh we take a fresh snapshot by value. Category
// colour/name we resolve live in data() (they rarely change and the lookup is
// cheap), which keeps the snapshot to just the tasks.
//
// WHY refresh() DIFFS INSTEAD OF ALWAYS RESETTING (v20.1):
// the underlying list is fully RE-DERIVED on every change (a query, never a
// maintained table), so the domain can't hand us "row 2 changed." A blunt model
// would just beginResetModel() every time — correct, but the view then loses
// scroll position and selection on every edit ("rebuild through a model"). So
// refresh() compares the old snapshot with the new one and emits the NARROWEST
// signals it can prove correct: dataChanged for an in-place edit,
// begin/endInsertRows and begin/endRemoveRows for structure. Only when a
// surviving row REORDERS (a due-date edit re-sorts it) does it fall back to a
// full reset — "granular when you can prove it, reset when you can't."
// ---------------------------------------------------------------------------

#include "TaskSnapshotModel.h" // shared base: owns the diff (applySnapshot)

#include "Task.h" // Task is a value type we snapshot by copy

class AppData;

// Role keys: the questions the view/delegate/proxy ask a row. Custom roles
// start at Qt::UserRole so they never collide with Qt's built-ins. Declared in
// a namespace so the delegate and the proxy refer to the SAME numbers by name,
// never by a bare magic int.
namespace taskmodel
{
enum Role {
    IdRole = Qt::UserRole + 1, // the task's stable id (for edit/done/delete)
    TitleRole,                 // also served as Qt::DisplayRole, for tooling
    CategoryNameRole,          // life-area name, or "(no area)"
    CategoryColorRole,         // QColor — accent bar + dot
    DueDateRole,               // QDate — the proxy sorts on this
    DueTimeRole,               // QTime — invalid means "all day" (v22)
    DaysUntilRole,             // qint64, signed: negative == overdue
    PriorityRole,              // int(Task::Priority) — the proxy filters on this
    RepeatRole,                // int(Task::Repeat)
    BucketRole,                // 0 overdue · 1 this week · 2 later (view sections)
    AffordabilityRole,         // int(afford::Verdict) — the "tight" pill (v28)
    PiecesDoneRole,            // v28.3 — the "2/5" chip's numerator…
    PiecesTotalRole,           // …and denominator; 0 total == no chip at all
};
} // namespace taskmodel

class TaskListModel : public TaskSnapshotModel
{
    Q_OBJECT

public:
    explicit TaskListModel(AppData* data, QObject* parent = nullptr);

    // rowCount() comes from the base; data() answers the roles this list paints.
    QVariant data(const QModelIndex& index, int role) const override;

public slots:
    // Re-derive from AppData and hand the snapshot to the base's diff, which
    // emits the narrowest signals (dataChanged / insert / remove, or a reset
    // when survivors reorder). Wired to AppData::changed() in the constructor.
    void refresh();

protected:
    // A card repaints only when title/category/date/priority/repeat changes; a
    // description-only edit moves no visible role, so it must not repaint.
    bool rolesEqual(const Task& a, const Task& b) const override;

    // v28 — the verdict per task id, recomputed each refresh(). Cached
    // rather than computed in data() because affordability() walks every
    // event, and data() is called per-row-per-role-per-paint; a hover
    // would re-run the whole-calendar scan dozens of times. The cache also
    // solves a subtler problem: verdicts can change when EVENTS change
    // while the Task rows are byte-identical, so the base class's diff
    // (which compares Task fields) would stay silent — refresh() below
    // compares verdict maps itself and emits the dataChanged the base
    // cannot know to.
    QHash<QString, int> m_verdicts;

    // v28.3 — piece progress per parent id, the verdict cache's twin, and
    // for the twin reason: ticking a PIECE changes no field of its PARENT'S
    // Task, so the base diff stays silent while the chip should move.
    // refresh() diffs this map itself. Packed as (done, total); a QPair and
    // not the domain's PieceCount only to keep this header's forward-declare
    // hygiene — AppData.h stays un-included here on purpose.
    QHash<QString, QPair<int, int>> m_pieces;

private:
    AppData* m_data; // truth lives here; we only ever read it
};
