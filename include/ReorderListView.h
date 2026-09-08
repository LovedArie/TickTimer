#pragma once
// ---------------------------------------------------------------------------
// ReorderListView — a QListView that turns a DRAG into a domain INTENT and
// nothing more (v31).
//
// It is CategoryTree's twin, one widget family over. That class already
// established the rule for the rail: on a valid drop it emits
// categoryDropped(...) and lets the page perform the one true mutation,
// deliberately NOT chaining to the base class's dropEvent so Qt never
// reparents items behind our back. The data moves in AppData, changed()
// fires, and the view is rebuilt from truth. This class does the same for a
// flat list, so both lists in a life area — tasks and activities — reorder
// through one implementation instead of two that can disagree.
//
// WHY THE WHOLE DRAG IS HAND-ROLLED, rather than switching Qt's
// InternalMove on. InternalMove asks the MODEL to move rows: it wants
// mimeData(), dropMimeData(), supportedDropActions() and a removeRows() that
// really removes. Our models hold a SNAPSHOT re-derived from AppData
// (TaskSnapshotModel) — they own no rows to move, and teaching them to
// pretend they do would put a second, fictional source of truth next to the
// real one. Every one of those overrides would exist purely to be undone by
// the next refresh. So the view reports, the domain moves, the snapshot
// follows. Roughly forty lines of event handling buys a model that stays
// honest about owning nothing.
//
// THE GESTURE, AND THE FOUR ATTEMPTS IT TOOK. Worth the space, because
// three of them are wrong in ways that look right, and every one of them
// was disproved on a device rather than by reading.
//
// A press-and-move inside a scrolling page belongs to QScroller, which the
// enclosing QScrollArea grabs a flick gesture on. What failed:
//
//   1. A grip band, on the theory that a press starting in a narrow strip
//      can mean only one thing. It does — but the MOVE events never
//      arrive. The press lands, the release lands, and QScroller eats
//      everything in between as a pan. It succeeded exactly once in
//      testing, in the one state where the page was pinned at a scroll
//      limit and the scroller declined, which made it look flaky rather
//      than absent.
//
//   2. Accepting TouchBegin on the viewport (WA_AcceptTouchEvents), on the
//      theory that an accepted touch sequence is never offered to
//      ancestors. That rule is real and it is not enough: QScroller's
//      flick goes through QGestureManager, which filters at
//      QApplication::notify — BEFORE the target's event() or
//      viewportEvent() is called. A child cannot win this from inside its
//      own event handler.
//
//   3. Dropping the drag entirely and putting Move up / Move down in the
//      row's long-press menu. That works, and the owner's verdict on it
//      was "it does work. But I don't like it."
//
// WHAT ACTUALLY WORKS, and why it is not attempt 1 again: a HOLD is not a
// drag. QScroller delays a press only until it can rule the gesture out as
// a pan, and a finger that has sat still for half a second has already
// been ruled out — so the moves that follow a hold DO reach us, where the
// moves that followed a bare press did not. So the handle comes back on
// every device, and the gesture that starts on it is:
//
//      hold the handle  ->  the list enters reorder mode (gripHeld)
//                       ->  the same finger drags the row
//                       ->  release drops it
//
// A hold anywhere ELSE on the row still opens the menu (rowLongPressed),
// which stays because it is the only reordering route a keyboard has, and
// because the owner asked to keep it.
//
// The view detects both holds itself rather than relying on Qt to
// synthesise a context-menu event, and that is not paranoia: our delegates
// act on MouseButtonRelease and their last branch is "anywhere else on the
// row means edit", so only whoever owns the PRESS can swallow the release
// before the editor opens.
//
// THE GRIP'S WIDTH IS SHARED, not agreed twice. gripWidth() is read by this
// view (to decide whether a press started in the grip) and by both delegates
// (to draw it and to shift the rest of the row right). One function, so the
// band you SEE and the band that WORKS cannot drift — the same discipline
// CategoryTaskDelegate's single geometryFor() enforces within a row.
// ---------------------------------------------------------------------------

#include "Widgets.h" // isCompactScreen — the grip is a mouse affordance

#include <QListView>
#include <QModelIndex>
#include <QPoint>
#include <QString>
#include <QTimer> // the long-press hold, owned by value

namespace reorder
{
// Wide enough for the six-dot glyph plus air, and — combined with the row
// height every delegate already meets — comfortably past WCAG 2.5.8's 24dp
// floor in both directions.
inline constexpr int kGripWidth = 22;

// WIDER on a phone, not absent (v31.0.4, owner request: "I'd like to long
// click the sandwich to re order"). It was briefly 0 there, on the finding
// that a grip DRAG loses to QScroller — true, and it does not follow that
// the handle is useless: a HOLD is not a drag, QScroller lets a stationary
// press through, and the owner confirmed the hold reaches us. So the handle
// comes back and the gesture that starts on it is a hold, not a drag.
//
// 32 rather than 22 where fingers are: the drawn dots are unchanged, only
// the band grows, which is the same paint-small/hit-big split
// CategoryTaskDelegate already applies to its right-hand cluster. Asked as
// a function rather than read as a constant so the delegates and the view
// cannot answer it differently.
inline int gripWidth() { return isCompactScreen() ? 32 : kGripWidth; }
} // namespace reorder

class ReorderListView : public QListView
{
    Q_OBJECT

public:
    // `idRole` is how this view asks a row "who are you?". Passing it in,
    // rather than hardcoding one model's enum, is what lets the task list
    // and the activity list share the class — they have different role
    // enums and neither should have to learn the other's.
    explicit ReorderListView(int idRole, QWidget* parent = nullptr);
    ~ReorderListView() override;

signals:
    // "Put `movedId` immediately before `beforeId`." An empty `beforeId`
    // means "at the end". Neighbour ids rather than a row index, matching
    // AppData::moveTaskBefore — an index is a fact about a list computed
    // elsewhere and can be stale by the time it is used; an id cannot turn
    // into a DIFFERENT valid answer, only into one that is refused.
    void reordered(const QString& movedId, const QString& beforeId);

    // "This row was held down." The touchscreen's way into the row menu —
    // and the view detects the hold itself rather than relying on Qt to
    // synthesise a context-menu event, because our delegates consume the
    // RELEASE as "edit this row" and would open the editor first. Only
    // whoever owns the press can swallow the release. `globalPos` is where
    // to pop the menu.
    void rowLongPressed(const QString& id, const QPoint& globalPos);

    // "The handle was held." The phone's way into reordering, and distinct
    // from rowLongPressed because it means something else: a hold ANYWHERE
    // asks a question (the menu), a hold ON THE HANDLE says "I am moving
    // this one". The page answers by turning the list's reorder mode on,
    // and this same finger can then drag the row straight away — see
    // mouseMoveEvent for why that works after a hold when it never worked
    // before one.
    void gripHeld(const QString& id);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    // Where would a drop at `pos` land? Returns the id of the row the moved
    // item should sit BEFORE, empty for "at the end". One function feeding
    // both the drawn indicator and the emitted intent, so the line you see
    // is the position you get.
    QString dropTargetAt(const QPoint& pos) const;
    // The y where the indicator line is drawn for `pos`; -1 for "nowhere".
    int indicatorYAt(const QPoint& pos) const;

    void beginPress(const QPoint& pos); // "did this land in a grip?"
    void updateIndicator(const QPoint& pos);
    void cancelDrag();

    // The enclosing scrollable page, found by walking up the parents. The
    // view does not know or care WHICH page it is in — only that something
    // above it is panning with the same finger.
    class QAbstractScrollArea* ancestorScrollArea() const;
    // Take the flick gesture away from that page for the duration of a
    // hold-drag, and give it back afterwards. See the .cpp for why nothing
    // less blunt works.
    void suspendAncestorScrolling(bool suspend);

    int     m_idRole;
    QPoint  m_pressPos;
    QString m_pressedId;   // non-empty only while a grip press is live
    bool    m_dragging = false;
    int     m_indicatorY = -1;

    // The hold. Armed on a press that is not a grip press, disarmed by any
    // real travel (that is a scroll) and by the release.
    QTimer m_longPress;
    bool   m_longPressFired = false; // the release belongs to us, not the row
    // A hold that landed on the handle. While true, finger movement moves the
    // ROW rather than the list, and the release drops it.
    bool   m_holdDragging = false;
    // Guards the ungrab/regrab pair so it cannot be unbalanced — a page left
    // permanently unable to scroll would be a far worse bug than the one
    // this fixes.
    bool   m_scrollSuspended = false;
};
