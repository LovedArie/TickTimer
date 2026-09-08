#include "ReorderListView.h"

#include "Theme.h"

#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QAbstractScrollArea>
#include <QMouseEvent>
#include <QPainter>
#include <QScroller>

namespace
{
// Our own mime type, and it carries the moved row's id. Two consequences
// worth stating: a drag from this list cannot be dropped on anything else in
// the app (nothing else claims the type), and nothing else can be dropped
// HERE — a stray file drag or a text selection is refused by type before any
// of our logic runs.
const char* kMime = "application/x-ticktimer-reorder";
} // namespace

ReorderListView::~ReorderListView()
{
    // A rebuild that deletes this view mid-gesture would otherwise leave the
    // page's scroller ungrabbed forever. The destructor is the one place
    // that cannot be skipped by an early return.
    suspendAncestorScrolling(false);
}

ReorderListView::ReorderListView(int idRole, QWidget* parent)
    : QListView(parent)
    , m_idRole(idRole)
{
    // NOT setDragEnabled(true): that hands the base class the decision of
    // when a drag begins (any press plus a few pixels of travel), which is
    // exactly the behaviour the grip exists to avoid. We start the drag
    // ourselves, from the grip, in mouseMoveEvent.
    setDragEnabled(false);
    setDropIndicatorShown(false); // we draw our own — see paintEvent
    // Drops are for the MOUSE path only — a phone reorders through the hold,
    // which never creates a QDrag at all. Accepting them unconditionally is
    // simpler than a condition that (since the handle came back everywhere)
    // can no longer be false, and a viewport that accepts a mime type
    // nothing else in the app produces is not a hazard.
    setAcceptDrops(true);

    // ---- the long press, detected HERE and not inherited (v31.0.2) --------
    //
    // The rail's comment says Qt synthesises a context-menu event from a
    // touch hold, and for a bare QTreeWidget it evidently does. It does not
    // reach a row in THIS list, and the reason is our own delegate: it acts
    // on MouseButtonRelease and its last branch is "anywhere else on the row
    // means edit", so a hold-then-release opens the editor before any
    // context menu can matter. Verified on a device — a long press on a task
    // row opened Task details.
    //
    // So the view detects the hold itself. That is safe from the collision
    // that killed the drag (ReorderListView.h): QScroller only takes over
    // once the finger TRAVELS, and a stationary press is not a pan. It also
    // beats the delegate, because the view sees the press first and can
    // consume the release that follows.
    m_longPress.setSingleShot(true);
    m_longPress.setInterval(500); // Android's own long-press threshold
    connect(&m_longPress, &QTimer::timeout, this, [this]() {
        const QModelIndex index = indexAt(m_pressPos);
        if (!index.isValid())
            return;
        m_longPressFired = true; // the release is now ours to swallow

        // TWO HOLDS, TWO MEANINGS, decided by where the finger landed.
        // On the handle it is "I am moving this one": the list goes into
        // reorder mode and this same finger may now drag the row. Anywhere
        // else it is "what can I do with this row?": the menu.
        if (!m_pressedId.isEmpty()) {
            m_holdDragging = true;
            suspendAncestorScrolling(true); // the row moves, the page does not
            emit gripHeld(m_pressedId);
            updateIndicator(m_pressPos);
            return;
        }
        emit rowLongPressed(index.data(m_idRole).toString(),
                            viewport()->mapToGlobal(m_pressPos));
    });
}

// ---------------------------------------------------------------------------

void ReorderListView::beginPress(const QPoint& pos)
{
    m_pressedId.clear();
    m_dragging = false;
    const QModelIndex index = indexAt(pos);
    if (!index.isValid())
        return;
    const QRect r = visualRect(index);
    if (pos.x() > r.left() + reorder::gripWidth())
        return; // not the handle: leave the gesture to whoever wants it
    m_pressPos  = pos;
    m_pressedId = index.data(m_idRole).toString();
}

QAbstractScrollArea* ReorderListView::ancestorScrollArea() const
{
    // Start at the PARENT: this view is itself a QAbstractScrollArea, and
    // the thing competing for the finger is the page above it.
    for (QWidget* w = parentWidget(); w; w = w->parentWidget())
        if (auto* area = qobject_cast<QAbstractScrollArea*>(w))
            return area;
    return nullptr;
}

// ---------------------------------------------------------------------------
// WHY THE PAGE'S SCROLLER HAS TO BE TAKEN AWAY, not merely out-voted.
//
// Once the hold has armed a drag, the finger's movement means two things at
// once: this view moves a row with it, and the enclosing page pans with it
// too, because QScroller is still watching the same events. The owner's
// report — "when I hold the sandwich and try to change the order, the screen
// is also scrolling" — is both of them working at the same time.
//
// There is no way to out-vote it from here. QScroller's flick is recognised
// through QGestureManager, which filters at QApplication::notify, before any
// widget's event handler runs (the same fact that defeated an earlier
// attempt to accept TouchBegin). Accepting events later cannot un-ring that
// bell. So the gesture is UNGRABBED for the duration and grabbed again after
// — blunt, symmetric, and the only thing that actually stops the pan.
//
// The pair is guarded by a flag because the failure mode of getting it wrong
// is a page that can never be scrolled again, which is far worse than the
// bug being fixed. Every exit from a hold-drag runs through cancelDrag().
// ---------------------------------------------------------------------------
void ReorderListView::suspendAncestorScrolling(bool suspend)
{
    if (suspend == m_scrollSuspended)
        return;
    QAbstractScrollArea* area = ancestorScrollArea();
    if (!area)
        return;
    QWidget* target = area->viewport();

    if (suspend) {
        // Kill any fling already in flight, or the page keeps coasting under
        // the row for a moment after the drag starts.
        if (QScroller::hasScroller(target))
            QScroller::scroller(target)->stop();
        QScroller::ungrabGesture(target);
    } else {
        // Re-grab exactly what makeTouchScrollable(QScrollArea*) grabs, so
        // the page comes back with the behaviour it had before.
        QScroller::grabGesture(target, QScroller::TouchGesture);
    }
    m_scrollSuspended = suspend;
}

void ReorderListView::updateIndicator(const QPoint& pos)
{
    const int y = indicatorYAt(pos);
    if (y != m_indicatorY) {
        m_indicatorY = y;
        viewport()->update();
    }
}

void ReorderListView::cancelDrag()
{
    suspendAncestorScrolling(false); // ALWAYS, and first — see the comment
    m_pressedId.clear();
    m_dragging     = false;
    m_holdDragging = false;
    m_indicatorY   = -1;
    viewport()->update();
}

QString ReorderListView::dropTargetAt(const QPoint& pos) const
{
    const QModelIndex index = indexAt(pos);
    if (!index.isValid())
        return {}; // past the last row -> "at the end"

    const QRect r = visualRect(index);
    // Above the row's midpoint means "before this one"; below means "before
    // whatever comes next", which for the last row is the end of the list.
    if (pos.y() < r.center().y())
        return index.data(m_idRole).toString();

    const QModelIndex next = model()->index(index.row() + 1, 0);
    return next.isValid() ? next.data(m_idRole).toString() : QString();
}

int ReorderListView::indicatorYAt(const QPoint& pos) const
{
    const QModelIndex index = indexAt(pos);
    if (!index.isValid()) {
        // Draw at the bottom of the last row, so "to the end" still shows a
        // line somewhere rather than silently nowhere.
        const int rows = model() ? model()->rowCount() : 0;
        if (rows == 0)
            return -1;
        return visualRect(model()->index(rows - 1, 0)).bottom();
    }
    const QRect r = visualRect(index);
    return pos.y() < r.center().y() ? r.top() : r.bottom();
}

void ReorderListView::mousePressEvent(QMouseEvent* event)
{
    m_longPressFired = false;
    m_holdDragging   = false;
    if (event->button() == Qt::LeftButton) {
        beginPress(event->pos());
        if (!m_pressedId.isEmpty()) {
            // A HANDLE press. On a phone the hold decides what it becomes
            // (see the timer); on a desktop mouseMoveEvent starts a QDrag
            // immediately, which is the gesture a mouse expects.
            if (isCompactScreen())
                m_longPress.start();
            // Swallow it either way. Letting the base see this press would
            // also let the delegate's editorEvent see the matching release
            // and open the row's editor at the end of the gesture.
            event->accept();
            return;
        }
        // Anywhere else on the row: arm the hold for the MENU — but ONLY on
        // a touchscreen. A desktop already has right-click for that menu, so
        // arming it here would give one gesture two meanings and take the
        // worse one: a left button held past half a second (which people do
        // without meaning anything by it) would pop the menu AND have its
        // release swallowed, so the row would refuse to open its editor.
        // The phone needs this door because it has no right-click; the
        // desktop needs it not to exist.
        //
        // m_pressPos is recorded even though beginPress declined, because
        // the timer needs to know which row was under the finger.
        if (isCompactScreen() && indexAt(event->pos()).isValid()) {
            m_pressPos = event->pos();
            m_longPress.start();
        }
    }
    QListView::mousePressEvent(event);
}

void ReorderListView::mouseMoveEvent(QMouseEvent* event)
{
    // Any real travel means this is a scroll, not a hold. Disarming here is
    // what keeps the long press from firing under a flick that happened to
    // start on a row.
    if (m_longPress.isActive()
        && (event->pos() - m_pressPos).manhattanLength()
               >= QApplication::startDragDistance())
        m_longPress.stop();

    // AFTER a hold, the finger moves the ROW. This works where the old
    // grip-drag did not, and the difference is the hold itself: QScroller
    // delays a press only until it can rule the gesture out as a pan, and a
    // finger that has sat still for half a second has already been ruled
    // out — so the moves that follow reach us instead of becoming a scroll.
    if (m_holdDragging) {
        m_dragging = true;
        updateIndicator(event->pos());
        event->accept();
        return;
    }

    if (m_pressedId.isEmpty()) {
        QListView::mouseMoveEvent(event);
        return;
    }
    // On a touchscreen a handle press waits for the hold; it must never turn
    // into a QDrag, which is the thing that lost to QScroller.
    if (isCompactScreen())
        return;
    if ((event->pos() - m_pressPos).manhattanLength()
        < QApplication::startDragDistance())
        return; // a press that has not travelled yet is not a drag

    auto* mime = new QMimeData;
    mime->setData(kMime, m_pressedId.toUtf8());
    auto* drag = new QDrag(this); // Qt deletes it when exec() returns
    drag->setMimeData(mime);
    m_dragging = true;
    drag->exec(Qt::MoveAction);
    // exec() ran a nested event loop; by the time it returns the drop has
    // already been handled (or abandoned) in dropEvent below.
    m_dragging   = false;
    m_indicatorY = -1;
    m_pressedId.clear();
    viewport()->update();
}

void ReorderListView::mouseReleaseEvent(QMouseEvent* event)
{
    m_longPress.stop();

    // The held row lands here.
    if (m_holdDragging) {
        const QString movedId = m_pressedId;
        const bool moved = m_dragging;
        const QString beforeId = dropTargetAt(event->pos());
        cancelDrag();
        m_longPressFired = false;
        if (moved && !movedId.isEmpty() && movedId != beforeId)
            emit reordered(movedId, beforeId);
        event->accept();
        return;
    }

    // A press in the grip that never travelled: consume it, so a stray tap
    // on the handle does not fall through to the delegate as "edit this".
    if (!m_pressedId.isEmpty()) {
        cancelDrag();
        event->accept();
        return;
    }

    // The hold already spoke, and a menu is open on top of us. Letting this
    // release through would reach the delegate, whose last branch is "the
    // rest of the row means edit" — so the editor would open BEHIND the
    // menu. This one line is the whole reason the view detects the hold
    // instead of leaving it to a synthesised context-menu event: only
    // whoever owns the press can swallow the release.
    if (m_longPressFired) {
        m_longPressFired = false;
        event->accept();
        return;
    }

    QListView::mouseReleaseEvent(event);
}

void ReorderListView::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasFormat(kMime) && event->source() == this)
        event->acceptProposedAction();
    else
        event->ignore(); // anything from outside this list is not ours
}

void ReorderListView::dragMoveEvent(QDragMoveEvent* event)
{
    if (!event->mimeData()->hasFormat(kMime) || event->source() != this) {
        event->ignore();
        return;
    }
    const int y = indicatorYAt(event->position().toPoint());
    if (y != m_indicatorY) {
        m_indicatorY = y;
        viewport()->update();
    }
    event->acceptProposedAction();
}

void ReorderListView::dragLeaveEvent(QDragLeaveEvent* event)
{
    m_indicatorY = -1;
    viewport()->update();
    QListView::dragLeaveEvent(event);
}

void ReorderListView::dropEvent(QDropEvent* event)
{
    m_indicatorY = -1;
    if (!event->mimeData()->hasFormat(kMime) || event->source() != this) {
        event->ignore();
        return;
    }
    const QString movedId =
        QString::fromUtf8(event->mimeData()->data(kMime));
    const QString beforeId = dropTargetAt(event->position().toPoint());
    event->acceptProposedAction();
    viewport()->update();

    // NO chaining to QListView::dropEvent, and this is the whole point of the
    // class: the base would try to move rows in the model, which owns none.
    // The signal is the only outcome; the domain performs the move and its
    // changed() rebuilds the snapshot.
    if (!movedId.isEmpty() && movedId != beforeId)
        emit reordered(movedId, beforeId);
}

void ReorderListView::paintEvent(QPaintEvent* event)
{
    QListView::paintEvent(event);
    if ((!m_dragging && !m_holdDragging) || m_indicatorY < 0)
        return;

    // A single accent line where the row will land. Drawn over the base's
    // output rather than through Qt's own drop indicator, because that one
    // is styled per-platform and, on the flat rows this app draws, reads as
    // a selection rectangle rather than an insertion point.
    QPainter p(viewport());
    p.setPen(QPen(theme::focus(), 2));
    p.drawLine(0, m_indicatorY, viewport()->width(), m_indicatorY);
}
