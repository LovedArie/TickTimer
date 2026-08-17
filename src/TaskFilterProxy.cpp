#include "TaskFilterProxy.h"

#include "TaskListModel.h" // for the role keys

using namespace taskmodel;

TaskFilterProxy::TaskFilterProxy(QObject* parent)
    : QSortFilterProxyModel(parent)
{
    // dynamic == re-filter/re-sort automatically when the source model changes
    // (our source resets on every AppData::changed, so this keeps the lens
    // correct without us lifting a finger). sort(0) turns the sorting ON —
    // a proxy does not sort until asked, even with lessThan() defined.
    setDynamicSortFilter(true);
    sort(0);
}

void TaskFilterProxy::setPriorityFilter(int priority)
{
    if (priority == m_priority)
        return;
    m_priority = priority;
    // Tell the proxy "my filter answer changed — re-run filterAcceptsRow() for
    // every row." We use invalidate() (re-runs filter AND sort) rather than the
    // narrower invalidateFilter(): the latter is DEPRECATED as of Qt 6.11 (it
    // now wants begin/endFilterChange(), which only exists on Qt 6.9+), and
    // this app still builds on 6.4 LTS. invalidate() has been stable and
    // non-deprecated since Qt 4 — the portable choice — and re-sorting an
    // already-ordered short list costs nothing. (On a 6.9+-only project you'd
    // reach for begin/endFilterChange() as the modern idiom.)
    invalidate();
}

bool TaskFilterProxy::filterAcceptsRow(int sourceRow,
                                       const QModelIndex& sourceParent) const
{
    if (m_priority < 0)
        return true; // "All" lens — nothing hidden

    // Ask the SOURCE model the priority of this row through our role, and keep
    // the row only if it matches the chosen lens. Note we go through
    // sourceModel()->index(...) — the proxy reasons in source coordinates here.
    const QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);
    return sourceModel()->data(idx, PriorityRole).toInt() == m_priority;
}

bool TaskFilterProxy::lessThan(const QModelIndex& a, const QModelIndex& b) const
{
    // Same ordering the domain's upcomingTasks() used: soonest due date first,
    // ties broken by locale-aware title. Expressing it here (as view logic)
    // means the model can stay an unordered snapshot and still display sorted —
    // and the Overdue/This-week/Later section runs stay contiguous, which the
    // delegate relies on to draw one header per group.
    const QDate da = a.data(DueDateRole).toDate();
    const QDate db = b.data(DueDateRole).toDate();
    if (da != db)
        return da < db;
    // v22: same day, so the clock decides — earliest first, all-day last.
    // This mirrors AppData::upcomingTasks()'s tie-break exactly, and it has
    // to: the list you see and the list the domain hands out must not
    // disagree about what "next" means.
    const QTime ta = a.data(DueTimeRole).toTime();
    const QTime tb = b.data(DueTimeRole).toTime();
    if (ta != tb) {
        if (!ta.isValid()) return false;
        if (!tb.isValid()) return true;
        return ta < tb;
    }
    return a.data(TitleRole).toString().localeAwareCompare(
               b.data(TitleRole).toString()) < 0;
}
