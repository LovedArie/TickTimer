#pragma once
// ---------------------------------------------------------------------------
// ActivityListModel — the activities of one life area, as a model (v31).
//
// WHY THIS EXISTS NOW and did not before. The activity rows were hand-built
// QHBoxLayouts refilled into a container on every changed(), which was fine
// while a row was a dot, a name and a button. Reordering is what broke that
// shape: a stack of widgets in a layout has no drag machinery at all, and
// hand-rolling one over raw mouse events would be inventing a fifth pattern
// beside the four this app already follows. Qt's item views own dragging; the
// price of admission is a model.
//
// So this is the third model in the family, and the contrast with the other
// two is the lesson:
//
//   TaskListModel (Upcoming)     one fixed global query, feeds a proxy
//   CategoryTaskModel            query parameterised by category, diffed
//   ActivityListModel (here)     query parameterised by category, RESET
//
// It RESETS rather than diffing, and that is a deliberate step back down from
// CategoryTaskModel's machinery rather than an oversight. The diff exists to
// keep a checkbox toggle from rebuilding a long list; an activity list is a
// handful of rows with no per-row state to preserve — no selection, no
// editor, no checkbox — so a reset costs a repaint of six rows and buys the
// absence of a rolesEqual() that has to be kept in step with the delegate.
// Reach for the diff when there is something to protect.
//
// Widget-free by construction (QColor and QString only), so it links and
// tests Gui-only, exactly like its two siblings.
// ---------------------------------------------------------------------------

#include <QAbstractListModel>
#include <QColor>
#include <QString>
#include <QVector>

class AppData;

namespace actrow
{
enum Role {
    IdRole = Qt::UserRole + 1, // stable activity id
    NameRole,                  // also Qt::DisplayRole
    ColorRole,                 // its life area's colour — the dot
    HasNotesRole,              // bool — a non-empty description (the ≡ cue)
    UseCountRole,              // int — how many events reference it
};
} // namespace actrow

class ActivityListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    explicit ActivityListModel(AppData* data, QObject* parent = nullptr);

    int      rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;

    // Re-point at a different life area. Like CategoryTaskModel's setter this
    // is a context swap, so it resets — which is what this model does anyway.
    void setCategoryId(const QString& categoryId);
    QString categoryId() const { return m_categoryId; }

public slots:
    void refresh();

private:
    // The snapshot row. A VALUE copy of what the delegate paints, not a
    // pointer into AppData: AppData's containers move their elements when
    // they grow, so a stored pointer is a lifetime bug waiting for the next
    // insert. Copying five small fields per row is free at this size and
    // cannot dangle.
    struct Row {
        QString id;
        QString name;
        QColor  color;
        bool    hasNotes = false;
        int     useCount = 0;
    };

    QVector<Row> buildSnapshot() const;

    AppData*     m_data;
    QString      m_categoryId;
    QVector<Row> m_rows;
};
