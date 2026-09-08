#include "ActivityListModel.h"

#include "AppData.h"

using namespace actrow;

ActivityListModel::ActivityListModel(AppData* data, QObject* parent)
    : QAbstractListModel(parent)
    , m_data(data)
{
}

QVector<ActivityListModel::Row> ActivityListModel::buildSnapshot() const
{
    QVector<Row> rows;
    if (m_categoryId.isEmpty())
        return rows;

    // activitiesIn() is the domain's answer to "which, in what order" — the
    // page used to loop over activities() and filter by hand, which is how
    // two surfaces end up ordering the same list differently.
    const Category* category = m_data->categoryById(m_categoryId);
    const QColor color = category ? category->color : QColor();
    for (const Activity* a : m_data->activitiesIn(m_categoryId)) {
        Row row;
        row.id       = a->id;
        row.name     = a->name;
        // The colour comes from the CATEGORY, never from the activity —
        // Activity.h's "reference, don't copy" rule, still paying out:
        // recolour the area and every row here follows.
        row.color    = color;
        row.hasNotes = !a->description.trimmed().isEmpty();
        row.useCount = m_data->eventCountUsing(a->id);
        rows.append(row);
    }
    return rows;
}

void ActivityListModel::setCategoryId(const QString& categoryId)
{
    if (m_categoryId == categoryId)
        return;
    m_categoryId = categoryId;
    refresh();
}

void ActivityListModel::refresh()
{
    beginResetModel();
    m_rows = buildSnapshot();
    endResetModel();
}

int ActivityListModel::rowCount(const QModelIndex& parent) const
{
    // A flat list: only the invisible root has children. Without this guard
    // a view that asks a ROW for its child count is told there are more, and
    // recurses forever.
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant ActivityListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const Row& row = m_rows[index.row()];

    switch (role) {
    case Qt::DisplayRole:
    case NameRole:     return row.name;
    case IdRole:       return row.id;
    case ColorRole:    return row.color;
    case HasNotesRole: return row.hasNotes;
    case UseCountRole: return row.useCount;
    default:           return {};
    }
}
