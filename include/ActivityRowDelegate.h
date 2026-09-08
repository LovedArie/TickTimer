#pragma once
// ---------------------------------------------------------------------------
// ActivityRowDelegate — paints one activity as a flat row (v31).
//
// The same technique as CategoryTaskDelegate, with a smaller job, and the
// difference between the two is the point of having both. A task row carries
// a checkbox, a deadline, a priority and a repeat because a task is a
// commitment with a shape. An activity is a NAME — a reusable kind of thing
// you do — so its row is a grip, a colour dot, the name, and whichever ONE
// exit the domain currently allows:
//
//   never used   -> ×          (removeActivity will accept it)
//   used n times -> "Archive"  (removeActivity would refuse; archive is the
//                               only retirement path an in-use activity has)
//
// Offering exactly one of the two, chosen from the same number the domain
// checks, is the older rule this file inherits: never draw a button the
// aggregate root will bounce.
//
// Zones, and the intents they report:
//   grip      -> nothing here; ReorderListView swallows it first
//   × / pill  -> deleteRequested(id) / archiveRequested(id)
//   elsewhere -> editRequested(id)   (the page opens ActivityDetailDialog)
//
// A pure view component, like every delegate here: it reports what was
// clicked and never touches AppData.
// ---------------------------------------------------------------------------

#include <QStyledItemDelegate>

class ActivityRowDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit ActivityRowDelegate(QObject* parent = nullptr);

    // See CategoryTaskDelegate for the full argument: on a phone every
    // gesture-based reorder loses to QScroller, so the reorder is built on
    // taps and the mode is what makes room for them.
    void setReorderMode(bool on);
    bool reorderMode() const { return m_reorderMode; }

    void  paint(QPainter* painter, const QStyleOptionViewItem& option,
                const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;
    bool  editorEvent(QEvent* event, QAbstractItemModel* model,
                      const QStyleOptionViewItem& option,
                      const QModelIndex& index) override;

signals:
    void editRequested(const QString& activityId);
    void archiveRequested(const QString& activityId);
    void deleteRequested(const QString& activityId);
    void moveUpRequested(const QString& activityId);
    void moveDownRequested(const QString& activityId);

private:
    struct RowGeom {
        QRect grip;    // ⠿ handle
        QRect dot;     // the life area's colour
        QRect name;
        QRect notes;   // ≡ cue (empty if none)
        QRect used;    // "in use (3)" pill (empty when unused)
        QRect archive; // "Archive" pill (empty when unused)
        QRect del;     // × (empty when in use)
        QRect up;      // ▲ (reorder mode only)
        QRect down;    // ▼ (reorder mode only)
    };
    // ONE geometry pass feeding paint, sizeHint and editorEvent — so every
    // affordance is exactly where it is clickable. The lesson
    // CategoryTaskDelegate states and this file obeys rather than re-derives.
    RowGeom geometryFor(const QStyleOptionViewItem& option,
                        const QModelIndex& index) const;

    bool m_reorderMode = false;
};
