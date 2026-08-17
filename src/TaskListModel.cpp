#include "TaskListModel.h"

#include "AppData.h"
#include "Affordability.h"

#include <utility> // std::exchange
#include "Category.h"

#include <QColor>
#include <QDate>

using namespace taskmodel;

TaskListModel::TaskListModel(AppData* data, QObject* parent)
    : TaskSnapshotModel(parent)
    , m_data(data)
{
    // The whole "no second copy to keep in sync" promise is this one line:
    // when the truth changes, the model re-derives and the view repaints. The
    // page used to own this connection (to rebuild widgets); now the MODEL owns
    // it, and the view is a silent beneficiary.
    connect(m_data, &AppData::changed, this, &TaskListModel::refresh);
    refresh();
}

bool TaskListModel::rolesEqual(const Task& a, const Task& b) const
{
    // Two snapshots of the SAME task "differ visibly" only if a field the CARD
    // paints changed. Editing just the description (which the card never shows)
    // deliberately does NOT count — no repaint for an invisible change.
    return a.title == b.title
        && a.categoryId == b.categoryId
        && a.dueDate == b.dueDate
        && a.dueTime == b.dueTime  // v22: the card paints it, so it counts
        && a.priority == b.priority
        && a.repeat == b.repeat;
}

void TaskListModel::refresh()
{
    // Take a fresh snapshot BY VALUE — upcomingTasks() hands back pointers into
    // AppData's vector, which moves on edit — then let the base diff it against
    // the old rows and emit the granular signals.
    QVector<Task> next;
    const auto tasks = m_data->upcomingTasks();
    next.reserve(tasks.size());
    for (const Task* t : tasks)
        next.append(*t);

    // v28 — verdicts ride alongside the snapshot, not inside it. Computed
    // here (once per change) and diffed HERE, because a verdict can move
    // when a block is added/tracked while the Task itself is untouched —
    // invisible to the base class's Task-field diff.
    const QDateTime now = QDateTime::currentDateTime();
    QHash<QString, int> fresh;
    fresh.reserve(next.size());
    for (const Task& t : next)
        fresh.insert(
            t.id, int(afford::affordability(*m_data, t, now).verdict));

    // v28.3 — piece progress rides the same sidecar train as the verdicts,
    // for the same reason (see the header): the parent's Task bytes don't
    // move when a piece is ticked, so the base diff can't see the chip.
    QHash<QString, QPair<int, int>> freshPieces;
    freshPieces.reserve(next.size());
    for (const Task& t : next) {
        const PieceCount pc = m_data->pieceProgress(t.id);
        if (pc.total > 0)
            freshPieces.insert(t.id, qMakePair(pc.done, pc.total));
    }

    const QHash<QString, int> stale = std::exchange(m_verdicts, fresh);
    const QHash<QString, QPair<int, int>> stalePieces =
        std::exchange(m_pieces, freshPieces);
    applySnapshot(std::move(next));

    // After the base has settled rows, tell the view about pills and chips
    // that moved under unchanged tasks. Post-applySnapshot on purpose: row
    // indices are only meaningful against the NEW snapshot.
    for (int row = 0; row < rows().size(); ++row) {
        const QString& id = rows().at(row).id;
        if (stale.value(id, -1) != m_verdicts.value(id, -1)) {
            const QModelIndex idx = index(row, 0);
            emit dataChanged(idx, idx, {AffordabilityRole});
        }
        if (stalePieces.value(id) != m_pieces.value(id)) {
            const QModelIndex idx = index(row, 0);
            emit dataChanged(idx, idx, {PiecesDoneRole, PiecesTotalRole});
        }
    }
}

QVariant TaskListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rows().size())
        return {};

    const Task&  task  = rows().at(index.row());
    const QDate  today = QDate::currentDate();
    const qint64 in    = today.daysTo(task.dueDate);

    switch (role) {
    case Qt::DisplayRole: // convenience: generic tooling & tests read titles
    case TitleRole:
        return task.title;

    case IdRole:
        return task.id;

    case CategoryNameRole: {
        const Category* c = m_data->categoryById(task.categoryId);
        return c ? c->name : QObject::tr("(no area)");
    }

    case CategoryColorRole: {
        const Category* c = m_data->categoryById(task.categoryId);
        // Deliberately NOT via Theme.h: that header pulls in QApplication, and
        // keeping the model widget-free is the point (it links Gui-only, like
        // the domain suite). #616974 is theme::inkSoft() — the one value we
        // need here, inlined rather than dragging the whole UI toolkit in.
        return c ? c->color : QColor(0x61, 0x69, 0x74);
    }

    case DueDateRole:
        return task.dueDate;

    case DueTimeRole:
        return task.dueTime; // invalid == "all day"

    case DaysUntilRole:
        return in;

    case PriorityRole:
        return int(task.priority);

    case RepeatRole:
        return int(task.repeat);

    case AffordabilityRole:
        return m_verdicts.value(task.id,
                                int(afford::Verdict::NotApplicable));

    case PiecesDoneRole:
        return m_pieces.value(task.id).first; // absent -> (0,0): no chip

    case PiecesTotalRole:
        return m_pieces.value(task.id).second;

    case BucketRole:
        // 0 overdue · 1 this week (next 6 days) · 2 later. This threshold is a
        // VIEW decision (what earns the "this week" alarm level), so it lives in
        // the model/view layer, not the domain — same call the old page made,
        // just relocated to where the view can ask for it by role.
        if (task.dueDate < today)             return 0;
        if (task.dueDate <= today.addDays(6)) return 1;
        return 2;
    }

    return {};
}
