#pragma once
// ---------------------------------------------------------------------------
// TaskCardDelegate — draws one task as a card, and draws the section header
// ("OVERDUE" / "THIS WEEK" / "LATER") above the first card of each run.
//
// THE BIG IDEA of a delegate: in model/view, rows are PAINTED, not built. The
// old page created a QFrame + QCheckBox + two QLabels + two QPushButtons for
// EVERY task, every rebuild. A delegate throws that away: one object paints all
// rows with a QPainter, so a thousand tasks cost one delegate, not a thousand
// widget trees. That's the scaling win model/view exists for.
//
// The cost you pay for it — and the thing worth understanding — is that a
// painted row has NO child widgets to click. There is no QCheckBox to connect;
// there's a rectangle where a checkbox is drawn. So interaction is hit-testing:
// editorEvent() catches the mouse, works out which zone was hit (checkbox, the
// ×, or the body), and emits a signal. The page wires those signals to AppData,
// exactly as the old TaskRow "forwarded to AppData and held no truth of its
// own" — same contract, different mechanics.
//
// Two overrides do the visible work:
//   sizeHint(): how tall is this row? (taller when it starts a new section)
//   paint():    draw the header (if any) + the card
// One override does the interaction:
//   editorEvent(): translate a click into doneToggled / deleteRequested /
//                  editRequested
//
// SECTION HEADERS IN A FLAT LIST: a QListView has no notion of groups. We fake
// them the standard way — the proxy sorts so buckets are contiguous, and a row
// draws its header only when its BucketRole differs from the row above it. One
// header per group falls out for free, and it survives filtering (hide all
// "overdue" rows and the first surviving row, now "this week", becomes row 0
// and draws its own header).
// ---------------------------------------------------------------------------

#include <QStyledItemDelegate>

class TaskCardDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit TaskCardDelegate(QObject* parent = nullptr);

    void  paint(QPainter* painter, const QStyleOptionViewItem& option,
                const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;
    // Public, like Qt's own QAbstractItemDelegate::editorEvent — the view calls
    // it, and the UI test calls it directly to prove the click-zone hit-testing.
    bool editorEvent(QEvent* event, QAbstractItemModel* model,
                     const QStyleOptionViewItem& option,
                     const QModelIndex& index) override;

signals:
    // The delegate reports WHAT the user did; the page decides what it means
    // (mark done, delete, open the editor). Pure view component, testable,
    // never touches AppData itself.
    void doneToggled(const QString& taskId, bool done);
    void deleteRequested(const QString& taskId);
    void editRequested(const QString& taskId);

private:
    // All row geometry in one place — paint(), sizeHint() and editorEvent() all
    // read from here, so the drawn checkbox and the clickable checkbox can never
    // drift apart (the DRY-the-geometry rule: one layout truth, three readers).
    struct RowGeom {
        bool  hasHeader = false;
        int   bucket    = 0;
        QRect header;   // section caption strip (empty if hasHeader == false)
        QRect card;     // the white rounded card
        QRect accent;   // the coloured left bar
        QRect check;    // clickable checkbox square
        QRect dot;      // category colour dot
        QRect text;     // title + subtitle column
        QRect prio;     // priority chip (empty if none)
        QRect countdown;// the due headline
        QRect del;      // the × button
    };
    RowGeom geometryFor(const QStyleOptionViewItem& option,
                        const QModelIndex& index) const;
    bool startsBucket(const QModelIndex& index) const;
};
