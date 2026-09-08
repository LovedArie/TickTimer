#include "ActivityRowDelegate.h"

#include "ActivityListModel.h"
#include "ReorderListView.h" // reorder::kGripWidth — one band, two readers
#include "Theme.h"
#include "Touch.h"   // the 48dp minimum, as a value
#include "Widgets.h" // isCompactScreen

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

using namespace actrow;

namespace
{
// Matches CategoryTaskDelegate's rows exactly. The two lists sit one above
// the other in the same pane, and rows of two different heights there read
// as two unrelated widgets rather than two sections of one page.
inline int rowHeight(bool compact) { return compact ? 56 : 46; }
constexpr int kPad  = 4;
constexpr int kDot  = 10;
constexpr int kDelW = 24;
constexpr int kGap  = 9;

QRect placePill(int& rightEdge, int cy, const QString& text, const QFont& font,
                int hpad = 10)
{
    const int w = QFontMetrics(font).horizontalAdvance(text) + 2 * hpad;
    const QRect r(rightEdge - w, cy - 12, w, 24);
    rightEdge -= w + 8;
    return r;
}

void drawPill(QPainter* p, const QRect& r, const QString& text,
              const QColor& bg, const QColor& fg, const QFont& font)
{
    QPainterPath path;
    path.addRoundedRect(r, 8, 8);
    if (bg.isValid())
        p->fillPath(path, bg);
    p->setFont(font);
    p->setPen(fg);
    p->drawText(r, Qt::AlignCenter, text);
}

QString usedText(int n) { return QObject::tr("in use (%1)").arg(n); }

// Identical to CategoryTaskDelegate's, deliberately: the two lists sit in
// one pane and an arrow that moved or resized between them would read as two
// different controls.
void placeArrows(QRect& up, QRect& down, int& right, int cy, bool compact)
{
    const int side = compact ? 48 : 32;
    down = QRect(right - side, cy - side / 2, side, side);
    right -= side + 4;
    up = QRect(right - side, cy - side / 2, side, side);
    right -= side + 8;
}

void drawArrow(QPainter* p, const QRect& r, const QString& glyph, bool enabled)
{
    QFont f = p->font();
    f.setPixelSize(14);
    p->setFont(f);
    p->setPen(enabled ? QColor("#2B2F36") : QColor("#C7CCC6"));
    p->drawText(r, Qt::AlignCenter, glyph);
}
} // namespace

ActivityRowDelegate::ActivityRowDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

void ActivityRowDelegate::setReorderMode(bool on) { m_reorderMode = on; }

ActivityRowDelegate::RowGeom
ActivityRowDelegate::geometryFor(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const
{
    RowGeom g;
    const QRect r = option.rect;
    const int cy = r.center().y();

    // 0 on a phone — see CategoryTaskDelegate and ReorderListView.h.
    if (reorder::gripWidth() > 0)
        g.grip = QRect(r.left(), r.top(), reorder::gripWidth(), r.height());
    g.dot  = QRect(r.left() + reorder::gripWidth() + kPad, cy - kDot / 2, kDot,
                   kDot);

    int right = r.right() - kPad;

    if (m_reorderMode) {
        placeArrows(g.up, g.down, right, cy, isCompactScreen());
        const int nameLeftR = g.dot.right() + kGap;
        g.name = QRect(nameLeftR, r.top(), right - nameLeftR, r.height());
        return g;
    }

    QFont pillFont = option.font;
    pillFont.setPixelSize(11);

    // THE ONE-EXIT RULE, decided here so paint and hit-test cannot disagree
    // about which button exists. The number is the same one removeActivity
    // consults, so the row can never offer the door the domain will shut.
    const int used = index.data(UseCountRole).toInt();
    if (used > 0) {
        g.archive = placePill(right, cy, QObject::tr("Archive"), pillFont);
        g.used    = placePill(right, cy, usedText(used), pillFont);
    } else {
        g.del = QRect(right - kDelW, cy - kDelW / 2, kDelW, kDelW);
        right -= kDelW + kGap;
    }

    if (index.data(HasNotesRole).toBool()) {
        g.notes = QRect(right - 16, cy - 10, 16, 20);
        right -= 16 + kGap;
    }

    const int nameLeft = g.dot.right() + kGap;
    g.name = QRect(nameLeft, r.top(), right - nameLeft, r.height());
    return g;
}

void ActivityRowDelegate::paint(QPainter* painter,
                                const QStyleOptionViewItem& option,
                                const QModelIndex& index) const
{
    const RowGeom g = geometryFor(option, index);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    painter->setPen(QPen(QColor("#EEF0ED"), 1));
    painter->drawLine(option.rect.left() + kPad, option.rect.bottom(),
                      option.rect.right() - kPad, option.rect.bottom());

    // Grip: the same six dots the task rows draw, deliberately identical —
    // one mark for one gesture across both lists. Absent on a phone.
    if (!g.grip.isNull()) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor("#C7CCC6"));
        const int gx = g.grip.center().x();
        const int gy = g.grip.center().y();
        for (int col = -1; col <= 1; col += 2)
            for (int row = -1; row <= 1; ++row)
                painter->drawEllipse(QPointF(gx + col * 3, gy + row * 5), 1.5,
                                     1.5);
    }

    // The life area's colour dot.
    painter->setBrush(index.data(ColorRole).value<QColor>());
    painter->drawEllipse(g.dot);
    painter->setBrush(Qt::NoBrush);

    QFont nf = option.font;
    nf.setPixelSize(14);
    painter->setFont(nf);
    painter->setPen(QColor("#2B2F36"));
    painter->drawText(g.name, Qt::AlignLeft | Qt::AlignVCenter,
                      QFontMetrics(nf).elidedText(
                          index.data(NameRole).toString(), Qt::ElideRight,
                          g.name.width()));

    if (!g.notes.isNull()) {
        QFont notesFont = option.font;
        notesFont.setPixelSize(15);
        painter->setFont(notesFont);
        painter->setPen(QColor("#8A9098"));
        painter->drawText(g.notes, Qt::AlignCenter, QStringLiteral("≡"));
    }

    if (m_reorderMode) {
        drawArrow(painter, g.up, QStringLiteral("\u25B2"), index.row() > 0);
        drawArrow(painter, g.down, QStringLiteral("\u25BC"),
                  index.row() < index.model()->rowCount() - 1);
        painter->restore();
        return;
    }

    QFont pillFont = option.font;
    pillFont.setPixelSize(11);

    if (!g.used.isNull())
        drawPill(painter, g.used, usedText(index.data(UseCountRole).toInt()),
                 QColor(), QColor("#616974"), pillFont);
    if (!g.archive.isNull())
        drawPill(painter, g.archive, QObject::tr("Archive"),
                 QColor("#EEF0ED"), QColor("#616974"), pillFont);

    if (!g.del.isNull()) {
        QFont xf = option.font;
        xf.setPixelSize(16);
        painter->setFont(xf);
        painter->setPen(theme::danger());
        painter->drawText(g.del, Qt::AlignCenter, QStringLiteral("×"));
    }

    painter->restore();
}

QSize ActivityRowDelegate::sizeHint(const QStyleOptionViewItem& option,
                                    const QModelIndex&) const
{
    const int w = option.rect.width() > 0 ? option.rect.width() : 480;
    return {w, rowHeight(isCompactScreen())};
}

bool ActivityRowDelegate::editorEvent(QEvent* event,
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

    // Same two-speed widening as CategoryTaskDelegate, and the same reason:
    // the drawn rects stay small so the row stays readable, and only the HIT
    // test grows. The right-hand pills grow to the full row height but only
    // half the gap sideways, because targets that overlap send a tap to
    // whichever is tested first — which is how an archive becomes a delete.
    const bool compact = isCompactScreen();
    const auto cluster = [&](const QRect& r) {
        if (!compact || r.isNull())
            return r;
        const int dh = qMax(0, option.rect.height() - r.height());
        return r.adjusted(-kGap / 2, -dh / 2, kGap / 2, dh - dh / 2);
    };

    if (m_reorderMode) {
        if (g.up.contains(p) && index.row() > 0) {
            emit moveUpRequested(id);
            return true;
        }
        if (g.down.contains(p)
            && index.row() < index.model()->rowCount() - 1) {
            emit moveDownRequested(id);
            return true;
        }
        return true;
    }

    if (!g.grip.isNull() && g.grip.contains(p))
        return true; // the view's, not ours — see CategoryTaskDelegate
    if (!g.del.isNull() && cluster(g.del).contains(p)) {
        emit deleteRequested(id);
        return true;
    }
    if (!g.archive.isNull() && cluster(g.archive).contains(p)) {
        emit archiveRequested(id);
        return true;
    }
    if (option.rect.contains(p)) {
        emit editRequested(id);
        return true;
    }
    return false;
}
