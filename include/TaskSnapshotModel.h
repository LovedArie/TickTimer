#pragma once
// ---------------------------------------------------------------------------
// TaskSnapshotModel — the shared base for list models backed by a re-derived
// Task SNAPSHOT (TaskListModel for Upcoming, CategoryTaskModel for Activities).
//
// WHY THIS EXISTS — the second-consumer rule, now for an ALGORITHM, not a widget.
// v20.1 taught one model to update incrementally: instead of beginResetModel()
// on every change, it DIFFS the old snapshot against the new one and emits the
// narrowest signals (dataChanged / insert / remove), resetting only when a
// surviving row reorders. When a SECOND model wanted the same behaviour, copying
// that ~50-line diff would invite the two copies to drift the day one gets a
// fix. So the diff moves here, to exactly one home, and both models inherit it.
//
// A subclass supplies two things and stays tiny:
//   * buildSnapshot()  — WHERE the rows come from (its own query into AppData).
//   * rolesEqual(a, b) — WHAT counts as a visible change: compare only the
//                        fields the subclass's delegate actually paints, so an
//                        edit to an unpainted field (e.g. a description) repaints
//                        nothing.
// and then calls one of:
//   * applySnapshot(next) — DIFF and emit granular signals (reset iff survivors
//                           reorder). The everyday path: an in-place edit, an
//                           add, a delete.
//   * resetSnapshot(next) — a plain reset, honest when the WHOLE context changed
//                           (e.g. the model was re-pointed at a new category),
//                           where a diff would be all-remove + all-insert noise.
//
// This is the Template Method pattern: the base fixes the algorithm's shape, the
// subclass fills the two holes. Widget-free (QAbstractListModel + value types),
// so both models still link Gui-only and unit-test without a QApplication.
// ---------------------------------------------------------------------------

#include <QAbstractListModel>
#include <QVector>

#include "Task.h"

class TaskSnapshotModel : public QAbstractListModel
{
public:
    // The count half of the model interface is identical for every snapshot
    // model, so it lives here. (data() differs per subclass — different roles —
    // and stays a subclass override.)
    int rowCount(const QModelIndex& parent = {}) const override;

protected:
    using QAbstractListModel::QAbstractListModel;

    // The snapshot the subclass's data()/rowCount() read from.
    const QVector<Task>& rows() const { return m_rows; }

    // DIFF the current rows against `next` and emit the narrowest signals that
    // describe the change; fall back to a full reset only when surviving rows
    // change relative order. "Granular when you can prove it, reset when you
    // can't."
    void applySnapshot(QVector<Task> next);

    // A blunt reset — the honest signal when the whole list is a different list
    // (a context swap), where a granular diff would be pure churn.
    void resetSnapshot(QVector<Task> next);

    // "Do these two snapshots of the SAME row differ in a way the view shows?"
    // Pure virtual: the base can't know which fields a given delegate paints.
    virtual bool rolesEqual(const Task& a, const Task& b) const = 0;

private:
    QVector<Task> m_rows;
};
