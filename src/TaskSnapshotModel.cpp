#include "TaskSnapshotModel.h"

#include <QSet>
#include <QStringList>

int TaskSnapshotModel::rowCount(const QModelIndex& parent) const
{
    // A flat list has no children under any row, so a valid parent means "how
    // many grandchildren" — always zero. Standard flat-model guard; forget it
    // and a QTreeView would recurse forever.
    if (parent.isValid())
        return 0;
    return int(m_rows.size());
}

void TaskSnapshotModel::resetSnapshot(QVector<Task> next)
{
    beginResetModel();
    m_rows = std::move(next);
    endResetModel();
}

void TaskSnapshotModel::applySnapshot(QVector<Task> next)
{
    // The domain re-derives its lists wholesale — it never tells us "row 3
    // changed" — so we DIFF the old snapshot against the new one and translate
    // the difference into the NARROWEST signals the view will believe. A
    // done-toggle should touch ONE row (scroll + selection survive), not reset
    // the world. Where a granular diff would be too hairy to PROVE correct — a
    // surviving row that reordered — we fall back to a reset.

    const auto idsOf = [](const QVector<Task>& v) {
        QStringList ids;
        ids.reserve(v.size());
        for (const Task& t : v)
            ids << t.id;
        return ids;
    };
    const QStringList oldIds = idsOf(m_rows);
    const QStringList newIds = idsOf(next);

    // --- Fast path: same rows, same order → at most in-place content edits. ---
    if (oldIds == newIds) {
        for (int i = 0; i < next.size(); ++i) {
            const bool visible = !rolesEqual(m_rows[i], next[i]);
            m_rows[i] = next[i];         // keep the snapshot fresh regardless...
            if (visible)                 // ...but only repaint if a role moved
                emit dataChanged(index(i), index(i));
        }
        return;
    }

    // Which rows survived, in each list's own order? If those two orders agree,
    // the change is pure inserts/removes and we can reconcile granularly. If
    // they disagree, a survivor jumped position (a re-sort) — the reset case.
    const QSet<QString> oldSet(oldIds.begin(), oldIds.end());
    const QSet<QString> newSet(newIds.begin(), newIds.end());
    QStringList survivorsOld, survivorsNew;
    for (const QString& id : oldIds)
        if (newSet.contains(id))
            survivorsOld << id;
    for (const QString& id : newIds)
        if (oldSet.contains(id))
            survivorsNew << id;

    if (survivorsOld != survivorsNew) {
        // A surviving row reordered. A minimal move sequence exists, but proving
        // it correct isn't worth it for a rare case — reset is honest and simple.
        resetSnapshot(std::move(next));
        return;
    }

    // 1) REMOVE ids that vanished — bottom-up, so earlier indices stay valid.
    for (int i = m_rows.size() - 1; i >= 0; --i) {
        if (!newSet.contains(m_rows[i].id)) {
            beginRemoveRows(QModelIndex(), i, i);
            m_rows.remove(i);
            endRemoveRows();
        }
    }
    // 2) INSERT new ids at their target position — top-down keeps the invariant
    //    "m_rows[0..j] already matches next[0..j]" true as we advance.
    for (int j = 0; j < next.size(); ++j) {
        if (j >= m_rows.size() || m_rows[j].id != next[j].id) {
            beginInsertRows(QModelIndex(), j, j);
            m_rows.insert(j, next[j]);
            endInsertRows();
        }
    }
    // 3) Content edits among survivors (positions now aligned to `next`).
    for (int i = 0; i < m_rows.size(); ++i) {
        if (!rolesEqual(m_rows[i], next[i])) {
            m_rows[i] = next[i];
            emit dataChanged(index(i), index(i));
        }
    }
}
