#pragma once
// ---------------------------------------------------------------------------
// TaskFilterProxy — the "lens" between TaskListModel and the QListView.
//
// A QSortFilterProxyModel is Qt's answer to a recurring itch: you have a model,
// and you want the view to see a FILTERED and/or SORTED version of it without
// touching the model or copying its data. The proxy sits in the middle and
// re-maps rows — the source model never learns it's being filtered.
//
//   TaskListModel ──▶ TaskFilterProxy ──▶ QListView
//    (all tasks)      (this: hide by       (sees only the
//                      priority, order      surviving rows,
//                      by due date)         already ordered)
//
// This replaces the old page's manual work in TWO places at once:
//   * the priority tabs used to std::remove_if a copy of the list and rebuild
//     every widget → now they call setPriorityFilter() and the proxy hides
//     rows in place;
//   * upcomingTasks() sorted by (date, title) by hand → the proxy's lessThan()
//     expresses the same ordering as view logic, so the model needn't pre-sort.
//
// Two virtuals carry the whole class — the canonical pair every proxy overrides:
//   filterAcceptsRow(): "should the view see this source row?"  → the FILTER
//   lessThan():         "which of these two rows comes first?"  → the SORT
// ---------------------------------------------------------------------------

#include <QSortFilterProxyModel>

class TaskFilterProxy : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit TaskFilterProxy(QObject* parent = nullptr);

    // -1 == "All"; otherwise int(Task::Priority). A VIEW state — which lens is
    // on — so it lives here, not in the data. Setting it re-runs the filter.
    void setPriorityFilter(int priority); // -1 = all
    int  priorityFilter() const { return m_priority; }

protected:
    bool filterAcceptsRow(int sourceRow,
                          const QModelIndex& sourceParent) const override;
    bool lessThan(const QModelIndex& a, const QModelIndex& b) const override;

private:
    int m_priority = -1;
};
