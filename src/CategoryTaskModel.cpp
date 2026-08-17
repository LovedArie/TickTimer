#include "CategoryTaskModel.h"

#include "AppData.h"

#include <QDate>

using namespace cattask;

CategoryTaskModel::CategoryTaskModel(AppData* data, QObject* parent)
    : TaskSnapshotModel(parent)
    , m_data(data)
{
    // Like TaskListModel, the MODEL owns the changed() connection the page used
    // to own — so a task edit updates the list without the page rebuilding it.
    connect(m_data, &AppData::changed, this, &CategoryTaskModel::refresh);
}

bool CategoryTaskModel::rolesEqual(const Task& a, const Task& b) const
{
    // Every field the flat delegate paints. `done` is here (Upcoming's model
    // omits it) because this list shows and toggles completion — the checkbox
    // and strikethrough must repaint when it flips. The notes ≡ cue is a
    // presence flag, so compare has-notes, not the note text itself.
    return a.title == b.title
        && a.done == b.done
        && a.dueDate == b.dueDate
        && a.dueTime == b.dueTime  // v22: painted on the pill, so it counts
        && a.priority == b.priority
        && a.repeat == b.repeat
        && a.description.trimmed().isEmpty() == b.description.trimmed().isEmpty();
}

QVector<Task> CategoryTaskModel::buildSnapshot() const
{
    QVector<Task> next;
    if (!m_categoryId.isEmpty()) {
        const auto tasks = m_data->tasksIn(m_categoryId);
        next.reserve(tasks.size());
        for (const Task* t : tasks) {
            if (t->archived)
                continue; // archived tasks belong to the Archive page
            next.append(*t); // by value — the source vector may move on edit
            // v28.7 — TickTick-style structure: each parent's pieces ride
            // directly under it, in subtasksOf's insertion order. This is
            // the DISPLAY amendment to the §D policy: pieces appear in the
            // category list as indented structure; every COUNTING query
            // (upcoming, affordability, week digests) still sees parents
            // only. tasksIn stays parents-only on purpose — its sort must
            // never split a family apart, so interleaving happens HERE,
            // after the parents are ordered.
            for (const Task* piece : m_data->subtasksOf(t->id))
                next.append(*piece); // subtasksOf already skips archived
        }
    }
    return next;
}

void CategoryTaskModel::setCategoryId(const QString& categoryId)
{
    if (categoryId == m_categoryId)
        return; // reselecting the same area is a no-op — don't churn the view
    m_categoryId = categoryId;
    // A context swap: the whole list is now a different list, so RESET rather
    // than diff (a diff would report every row removed and every new row
    // inserted — pure noise for what is conceptually "show a different list").
    resetSnapshot(buildSnapshot());
}

void CategoryTaskModel::refresh()
{
    // An in-place change WITHIN the current category (done toggled, title
    // edited, task added/deleted). Hand it to the base's diff so a checkbox
    // toggle flips one row instead of rebuilding the list. (This list is never
    // sorted, so survivors don't reorder — the base's reset fallback effectively
    // never fires here; the win is all upside.)
    applySnapshot(buildSnapshot());
}

QVariant CategoryTaskModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rows().size())
        return {};

    const Task& task = rows().at(index.row());

    switch (role) {
    case Qt::DisplayRole:
    case TitleRole:
        return task.title;

    case Qt::ToolTipRole:
        // Hovering the row surfaces the notes — the delegate paints only a ≡
        // cue, the tooltip carries the text (the whole row is the hover target).
        return task.description.trimmed().isEmpty() ? QVariant()
                                                    : task.description;

    case IdRole:
        return task.id;

    case DoneRole:
        return task.done;

    case DueDateRole:
        return task.dueDate; // invalid == "date TBD"

    case DueTimeRole:
        return task.dueTime; // invalid == "all day"

    case OverdueRole:
        // The QDateTime overload, so a task due today at 09:00 turns rose at
        // 09:01 instead of waiting for the date to roll over.
        return task.isOverdue(QDateTime::currentDateTime());

    case PriorityRole:
        return int(task.priority);

    case RepeatRole:
        return int(task.repeat);

    case IsPieceRole:
        return task.isPiece(); // v28.7 — the delegate's indent cue
    case HasNotesRole:
        return !task.description.trimmed().isEmpty();
    }

    return {};
}
