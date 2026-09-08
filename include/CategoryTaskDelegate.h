#pragma once
// ---------------------------------------------------------------------------
// CategoryTaskDelegate — paints one Activities task as a FLAT row (no card),
// the visual TaskRow used to be. Where TaskCardDelegate drew a self-contained
// card with a section header, this draws a lean line in a list — same delegate
// TECHNIQUE, different look, which is half the contrast lesson.
//
// The other half: this row has more to do than an Upcoming card. A task here
// can be toggled done (checkbox + strikethrough), edited (click the title),
// re-dated (click the date badge — which opens a DIFFERENT dialog than the
// title does), archived once done, or deleted. So editorEvent() hit-tests FIVE
// zones, not three, and the delegate emits five intents for the page to honour:
//
//   checkbox    -> doneToggled(id, !done)
//   date badge  -> dueDateRequested(id)   (page opens DueDateDialog)
//   Archive     -> archiveRequested(id)   (only drawn when the task is done)
//   ×           -> deleteRequested(id)
//   elsewhere   -> editRequested(id)       (page opens TaskDetailDialog)
//
// Still a pure view component: it reports what was clicked, never touches
// AppData. And still one geometryFor() feeding paint + sizeHint + editorEvent,
// so every drawn affordance is exactly where it's clickable.
// ---------------------------------------------------------------------------

#include <QStyledItemDelegate>

class CategoryTaskDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit CategoryTaskDelegate(QObject* parent = nullptr);

    // REORDER MODE (v31.0.3). While on, every row trades its right-hand
    // cluster for two arrows and the checkbox goes away.
    //
    // WHY A MODE AND NOT A GESTURE. Three gesture designs were tried on a
    // real phone and all three lost to QScroller, which owns press-and-move
    // inside a scrolling page and — the part that killed the last attempt —
    // DELAYS the press itself until it has decided the gesture is not a pan.
    // For a stationary hold that decision arrives at lift, so press and
    // release land together and a hold is indistinguishable from a tap.
    //
    // What has never failed on these rows is a TAP: tap-to-edit, the ×, the
    // date badge all work today. So the reorder is built on the one input
    // that is proven, and the mode is what buys the room for it — in reorder
    // mode there is no due badge or archive pill to crowd against, which
    // also fixes the sub-48dp width the touch gate has been tolerating.
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
    void doneToggled(const QString& taskId, bool done);
    void editRequested(const QString& taskId);
    void dueDateRequested(const QString& taskId);
    void archiveRequested(const QString& taskId);
    void deleteRequested(const QString& taskId);
    void moveUpRequested(const QString& taskId);
    void moveDownRequested(const QString& taskId);

private:
    struct RowGeom {
        QRect grip;    // ⠿ reorder handle (empty on a piece — see the .cpp)
        QRect check;
        QRect title;
        QRect notes;   // ≡ cue (empty if no notes)
        QRect repeat;  // ⟳ chip (empty if none)
        QRect prio;    // priority chip (empty if Medium)
        QRect due;     // date badge (always present)
        QRect archive; // "Archive" pill (empty unless done)
        QRect del;     // ×
        QRect up;      // ▲ (reorder mode only)
        QRect down;    // ▼ (reorder mode only)
    };
    RowGeom geometryFor(const QStyleOptionViewItem& option,
                        const QModelIndex& index) const;

    bool m_reorderMode = false;
};
