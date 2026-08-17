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

private:
    struct RowGeom {
        QRect check;
        QRect title;
        QRect notes;   // ≡ cue (empty if no notes)
        QRect repeat;  // ⟳ chip (empty if none)
        QRect prio;    // priority chip (empty if Medium)
        QRect due;     // date badge (always present)
        QRect archive; // "Archive" pill (empty unless done)
        QRect del;     // ×
    };
    RowGeom geometryFor(const QStyleOptionViewItem& option,
                        const QModelIndex& index) const;
};
