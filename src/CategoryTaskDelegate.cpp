#include "CategoryTaskDelegate.h"

#include "Touch.h"   // v30.7 — the 48dp minimum
#include "Widgets.h" // isCompactScreen

#include "CategoryTaskModel.h"
#include "Task.h"
#include "Theme.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

using namespace cattask;

namespace
{
// 46 on a desktop, taller on a phone (v30.7). The row is itself a target —
// tapping it opens the task — and 46 sat 2dp under Android's 48. The extra
// height is also what lets the affordances inside grow vertically without
// overlapping each other, which is the only axis where there is room.
inline int rowHeight(bool compact) { return compact ? 56 : 46; }
constexpr int kPad   = 4;
constexpr int kCheck = 18;
constexpr int kPieceIndent = 24; // v28.7 — a piece's shift under its parent
constexpr int kDelW  = 24;
constexpr int kGap   = 9;

// A small rounded pill: measure text, place from the right, return its rect.
QRect placePill(int& rightEdge, int cy, const QString& text, const QFont& font,
                int hpad = 10)
{
    const int w = QFontMetrics(font).horizontalAdvance(text) + 2 * hpad;
    const QRect r(rightEdge - w, cy - 12, w, 24);
    rightEdge -= w + 8;
    return r;
}

void drawPill(QPainter* p, const QRect& r, const QString& text,
              const QColor& bg, const QColor& fg, const QFont& font,
              const QColor& border = QColor())
{
    QPainterPath path;
    path.addRoundedRect(r, 8, 8);
    if (bg.isValid())
        p->fillPath(path, bg);
    if (border.isValid()) {
        p->setPen(QPen(border, 1));
        p->drawPath(path);
    }
    p->setFont(font);
    p->setPen(fg);
    p->drawText(r, Qt::AlignCenter, text);
}
// The due pill's words, in ONE place: the geometry pass measures this string
// and the paint pass draws it, so the pill can never be sized for "Aug 8" and
// then asked to render "Aug 8 · 23:59" (v22).
QString dueBadgeText(const QModelIndex& index)
{
    const QDate due = index.data(DueDateRole).toDate();
    if (!due.isValid())
        return QObject::tr("date TBD");
    QString out = due.toString("MMM d");
    const QTime at = index.data(DueTimeRole).toTime();
    if (at.isValid())
        out += QStringLiteral(" \u00B7 ") + dueTimeLabel(at);
    return out;
}
} // namespace

CategoryTaskDelegate::CategoryTaskDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

CategoryTaskDelegate::RowGeom
CategoryTaskDelegate::geometryFor(const QStyleOptionViewItem& option,
                                  const QModelIndex& index) const
{
    RowGeom g;
    QRect r = option.rect;
    // v28.7 — a piece indents under its parent, TickTick-style. Done HERE
    // and nowhere else: geometryFor is the single source both paint() and
    // editorEvent() read, so the checkbox you SEE and the checkbox you CAN
    // CLICK shift together by construction — an indent applied in paint
    // alone is the classic off-by-24px dead-zone bug.
    if (index.data(cattask::IsPieceRole).toBool())
        r.adjust(kPieceIndent, 0, 0, 0);
    const int cy = r.center().y();

    g.check = QRect(r.left() + kPad, cy - kCheck / 2, kCheck, kCheck);

    // Right-side affordances, placed right-to-left; the title takes the rest.
    int right = r.right() - kPad;
    g.del = QRect(right - kDelW, cy - kDelW / 2, kDelW, kDelW);
    right -= kDelW + kGap;

    QFont pillFont = option.font;
    pillFont.setPixelSize(11);

    if (index.data(DoneRole).toBool()) {
        g.archive = placePill(right, cy, QObject::tr("Archive"), pillFont);
    }

    g.due = placePill(right, cy, dueBadgeText(index), pillFont);

    if (index.data(RepeatRole).toInt() != int(Task::Repeat::None)) {
        const QString rep = QStringLiteral("\u27F3 %1").arg(
            repeatLabel(Task::Repeat(index.data(RepeatRole).toInt())));
        g.repeat = placePill(right, cy, rep, pillFont);
    }

    const int prio = index.data(PriorityRole).toInt();
    if (prio != int(Task::Priority::Medium)) {
        QFont chip = option.font;
        chip.setPixelSize(10);
        chip.setWeight(QFont::ExtraBold);
        g.prio = placePill(right, cy,
                           priorityLabel(Task::Priority(prio)).toUpper(), chip,
                           7);
    }

    if (index.data(HasNotesRole).toBool()) {
        g.notes = QRect(right - 16, cy - 10, 16, 20);
        right -= 16 + kGap;
    }

    const int titleLeft = g.check.right() + kGap;
    g.title = QRect(titleLeft, r.top(), right - titleLeft, r.height());
    return g;
}

void CategoryTaskDelegate::paint(QPainter* painter,
                                 const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const
{
    const RowGeom g = geometryFor(option, index);
    const bool done = index.data(DoneRole).toBool();

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // A hairline separator under each row — the flat-list equivalent of the
    // card's border, and the reason this reads as a list, not a stack of cards.
    painter->setPen(QPen(QColor("#EEF0ED"), 1));
    painter->drawLine(option.rect.left() + kPad, option.rect.bottom(),
                      option.rect.right() - kPad, option.rect.bottom());

    // Checkbox: filled with a tick when done, empty outline otherwise.
    QPainterPath box;
    box.addRoundedRect(g.check, 4, 4);
    if (done) {
        painter->fillPath(box, theme::focus());
        painter->setPen(QPen(Qt::white, 2));
        const QRect c = g.check;
        painter->drawLine(c.left() + 4, c.center().y(),
                          c.center().x() - 1, c.bottom() - 4);
        painter->drawLine(c.center().x() - 1, c.bottom() - 4,
                          c.right() - 3, c.top() + 4);
    } else {
        painter->setPen(QPen(QColor("#B9C0BA"), 1.5));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(box);
    }

    // Title: strikethrough + muted when done — the universal "finished" idiom.
    QFont tf = option.font;
    tf.setPixelSize(14);
    tf.setStrikeOut(done);
    painter->setFont(tf);
    painter->setPen(done ? QColor("#AEB4AC") : QColor("#2B2F36"));
    painter->drawText(g.title, Qt::AlignLeft | Qt::AlignVCenter,
        QFontMetrics(tf).elidedText(index.data(TitleRole).toString(),
                                    Qt::ElideRight, g.title.width()));

    // Notes cue.
    if (!g.notes.isNull()) {
        QFont nf = option.font;
        nf.setPixelSize(15);
        painter->setFont(nf);
        painter->setPen(QColor("#8A9098"));
        painter->drawText(g.notes, Qt::AlignCenter, QStringLiteral("\u2261"));
    }

    // Priority chip (Medium stays silent).
    if (!g.prio.isNull()) {
        const int prio = index.data(PriorityRole).toInt();
        const QColor c = prio == int(Task::Priority::Urgent)
                             ? theme::danger()
                             : QColor("#8A93A0");
        QFont chip = option.font;
        chip.setPixelSize(10);
        chip.setWeight(QFont::ExtraBold);
        drawPill(painter, g.prio,
                 priorityLabel(Task::Priority(prio)).toUpper(),
                 QColor(), c, chip, c);
    }

    QFont pillFont = option.font;
    pillFont.setPixelSize(11);

    // Repeat chip.
    if (!g.repeat.isNull()) {
        const QString rep = QStringLiteral("\u27F3 %1").arg(
            repeatLabel(Task::Repeat(index.data(RepeatRole).toInt())));
        drawPill(painter, g.repeat, rep, QColor("#EEF0ED"), QColor("#616974"),
                 pillFont);
    }

    // Due badge: rose when overdue (and not done), quiet grey otherwise.
    const bool overdue = index.data(OverdueRole).toBool();
    drawPill(painter, g.due, dueBadgeText(index),
             overdue ? QColor("#F7ECEA") : QColor("#EEF0ED"),
             overdue ? theme::danger() : QColor("#616974"), pillFont);

    // Archive pill (only when done).
    if (!g.archive.isNull())
        drawPill(painter, g.archive, QObject::tr("Archive"), QColor("#EEF0ED"),
                 QColor("#616974"), pillFont);

    // Delete.
    QFont xf = option.font;
    xf.setPixelSize(16);
    painter->setFont(xf);
    painter->setPen(theme::danger());
    painter->drawText(g.del, Qt::AlignCenter, QStringLiteral("\u00D7"));

    painter->restore();
}

QSize CategoryTaskDelegate::sizeHint(const QStyleOptionViewItem& option,
                                     const QModelIndex&) const
{
    const int w = option.rect.width() > 0 ? option.rect.width() : 480;
    return {w, rowHeight(isCompactScreen())};
}

bool CategoryTaskDelegate::editorEvent(QEvent* event,
                                       QAbstractItemModel* /*model*/,
                                       const QStyleOptionViewItem& option,
                                       const QModelIndex& index)
{
    if (event->type() != QEvent::MouseButtonRelease)
        return false;
    auto* me = static_cast<QMouseEvent*>(event);
    if (me->button() != Qt::LeftButton)
        return false;

    const RowGeom g = geometryFor(option, index);
    const QPoint  p = me->pos();
    const QString id = index.data(IdRole).toString();

    // ---- hit zones, widened for a thumb (v30.7) ---------------------------
    // The rects in RowGeom are what gets PAINTED and must stay small: a 48dp
    // checkbox drawn on a 56dp row is a checkbox that has eaten the row. So
    // the growth happens here, at hit-test time only, and paint is untouched.
    //
    // Two different expansions, because the two sides have different room:
    //
    //   The CHECK sits alone at the left edge, and everything to its right is
    //   the row's own "edit" action rather than another affordance. It can
    //   take the full 48 without stealing a distinct target — worst case a
    //   tap near the box means "done" instead of "open", and that is the
    //   choice the user was aiming at anyway.
    //
    //   The DELETE, ARCHIVE and DUE pills are a cluster on the right with
    //   kGap between them. Growing those to 48 wide would overlap them, and
    //   Material is explicit that sub-48 targets must not overlap — a tap in
    //   the overlap would silently go to whichever is tested first, which is
    //   how a due-date tap becomes a delete. So they grow to the full row
    //   height and only half the gap sideways.
    //
    // The honest consequence: those three clear WCAG 2.5.8's 24dp floor and
    // do not reach Material's 48 in WIDTH. The real fix is fewer affordances
    // per row on a phone — moving archive and due into the task's own sheet —
    // which is a behaviour change, not a size change, and is named as the
    // next step in the addendum rather than smuggled in here.
    const bool compact = isCompactScreen();
    const auto cluster = [&](const QRect& r) {
        if (!compact || r.isNull())
            return r;
        const int dh = qMax(0, option.rect.height() - r.height());
        return r.adjusted(-kGap / 2, -dh / 2, kGap / 2, dh - dh / 2);
    };

    // Specific affordances first; the whole remaining row means "edit".
    if (touch::expand(g.check, compact).contains(p)) {
        emit doneToggled(id, !index.data(DoneRole).toBool());
        return true;
    }
    if (cluster(g.del).contains(p)) {
        emit deleteRequested(id);
        return true;
    }
    if (!g.archive.isNull() && cluster(g.archive).contains(p)) {
        emit archiveRequested(id);
        return true;
    }
    if (cluster(g.due).contains(p)) {
        emit dueDateRequested(id);
        return true;
    }
    if (option.rect.contains(p)) {
        emit editRequested(id);
        return true;
    }
    return false;
}
