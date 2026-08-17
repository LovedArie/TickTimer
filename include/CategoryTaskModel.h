#pragma once
// ---------------------------------------------------------------------------
// CategoryTaskModel — the app's SECOND model/view list, built to CONTRAST with
// TaskListModel (the Upcoming page). Same family, deliberately different shape,
// because the differences are the lesson:
//
//   TaskListModel (Upcoming)          CategoryTaskModel (Activities)
//   ----------------------------      ------------------------------
//   fixed global query                query PARAMETERISED by a category
//     upcomingTasks()                   tasksIn(categoryId) — re-pointable
//   dated + undone only               ALL non-archived (incl. TBD and DONE)
//   feeds a QSortFilterProxy          feeds the QListView DIRECTLY (no lens)
//   granular diff on change           plain reset on change (short list)
//
// THE NEW IDEA HERE is the parameterised source. Upcoming's model wrapped one
// unchanging question. This one wraps "the tasks of WHICH category?", and the
// answer changes every time you click a different life area in the rail. So the
// model grows a setter — setCategoryId() — that re-snapshots. Switching category
// is a wholesale context swap (entirely different rows), which is exactly when a
// full reset is the honest signal, not a diff.
//
// TWO KINDS OF UPDATE, on purpose (v20.3):
//   * setCategoryId() — a CONTEXT SWAP. The whole list becomes a different list,
//     so it RESETS (a diff here would be all-remove + all-insert noise).
//   * refresh() on a data change — an IN-PLACE edit within the current category
//     (a checkbox toggled, a title changed, a task added/deleted). This now goes
//     through the base's DIFF, so toggling "done" flips ONE row instead of
//     rebuilding the list. Earlier (v20.2) this model reset on every change;
//     sharing the base's reconcile made the granular path free to adopt.
// The rule to remember: reset for a context swap, diff for an in-place edit.
//
// Still by-value snapshot, still widget-free (QColor/QDate only, no QApplication)
// so it can be unit-tested Gui-only alongside TaskListModel.
// ---------------------------------------------------------------------------

#include "TaskSnapshotModel.h" // shared base: owns the diff (applySnapshot)

#include <QString>
#include <QVector>

#include "Task.h"

class AppData;

namespace cattask
{
enum Role {
    IdRole = Qt::UserRole + 1, // stable task id
    TitleRole,                 // also Qt::DisplayRole
    DoneRole,                  // bool — drives the checkbox + strikethrough
    DueDateRole,               // QDate — invalid means "date TBD"
    DueTimeRole,               // QTime — invalid means "all day" (v22)
    OverdueRole,               // bool — undone AND past due
    PriorityRole,              // int(Task::Priority)
    RepeatRole,                // int(Task::Repeat)
    HasNotesRole,              // bool — a non-empty description (the ≡ cue)
    IsPieceRole,               // bool — v28.7: indented under its parent
};
} // namespace cattask

class CategoryTaskModel : public TaskSnapshotModel
{
    Q_OBJECT

public:
    explicit CategoryTaskModel(AppData* data, QObject* parent = nullptr);

    // rowCount() comes from the base; data() answers this list's roles.
    QVariant data(const QModelIndex& index, int role) const override;

    // Re-point the model at a different life area — a CONTEXT SWAP, so it
    // resets. The star of this file: a model whose contents follow a parameter
    // the user controls. No-op when the id is unchanged.
    void setCategoryId(const QString& categoryId);
    QString categoryId() const { return m_categoryId; }

public slots:
    void refresh(); // in-place edit within the current category → diff (base)

protected:
    // A row repaints when its title, done-state, date, priority, repeat, or
    // has-notes flag changes — every field the flat delegate actually draws.
    bool rolesEqual(const Task& a, const Task& b) const override;

private:
    QVector<Task> buildSnapshot() const; // current category's non-archived tasks

    AppData* m_data;
    QString  m_categoryId;
};
