#include "TaskCardDelegate.h"

#include "Affordability.h"

#include "Task.h"          // Priority / Repeat enums + labels
#include "TaskListModel.h" // role keys
#include "Theme.h"
#include "Touch.h"   // v30.7 — the 48dp minimum
#include "Widgets.h" // isCompactScreen

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

using namespace taskmodel;

// ---- fixed metrics (one place to retune the card) --------------------------
namespace
{
constexpr int kHeaderH   = 34; // section caption strip
constexpr int kCardH     = 86; // v22.1: sized for the bigger type below —
                               // bump a font here without bumping this and
                               // the two text lines start kissing the edges
constexpr int kGap       = 12; // space below each card
// The ONE line-length rule left on this page (v22.1): the panel no longer
// caps its width, so this is where reading comfort is enforced. Cards
// left-align and stop growing here; the page may be as wide as it likes.
constexpr int kMaxCardW  = 1100;
constexpr int kAccentW   = 4;
constexpr int kPad       = 14;
constexpr int kCheck     = 20;
constexpr int kDot       = 14;
constexpr int kDelW      = 28;

// The due-date headline: text + colour, straight from the old buildTaskCard —
// now with the clock folded in when the task has one (v22).
//
// ONE function, not two, even though the geometry pass and the paint pass both
// call it: the width reserved for this text is measured from the very string
// that gets drawn, so they cannot drift. (Add a second formatter and the first
// long time value overruns the delete button.)
QString countdownText(qint64 in, QTime at)
{
    if (in < 0) {
        const qint64 late = -in;
        return late == 1 ? QObject::tr("1 day overdue")
                         : QObject::tr("%1 days overdue").arg(late);
    }
    const QString clock = dueTimeLabel(at); // "" when the task is all-day
    if (in == 0)
        return clock.isEmpty() ? QObject::tr("due today")
                               : QObject::tr("due %1").arg(clock);
    if (in == 1)
        return clock.isEmpty() ? QObject::tr("due tomorrow")
                               : QObject::tr("tomorrow %1").arg(clock);
    return clock.isEmpty() ? QObject::tr("in %1 days").arg(in)
                           : QObject::tr("in %1 days, %2").arg(in).arg(clock);
}
QColor countdownColor(qint64 in)
{
    if (in < 0)  return theme::danger();
    if (in <= 6) return theme::focus();
    return theme::inkSoft();
}
QString bucketCaption(int b)
{
    if (b == 0) return QObject::tr("Overdue");
    if (b == 1) return QObject::tr("This week");
    return QObject::tr("Later");
}
QColor bucketColor(int b)
{
    if (b == 0) return theme::danger();
    if (b == 1) return theme::focus();
    return theme::inkSoft();
}
} // namespace

TaskCardDelegate::TaskCardDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

bool TaskCardDelegate::startsBucket(const QModelIndex& index) const
{
    // Row 0 always opens a section. Otherwise a header appears exactly where
    // the bucket changes from the row above — buckets are contiguous because
    // the proxy sorts by due date, so this yields one header per group.
    const int b = index.data(BucketRole).toInt();
    if (index.row() == 0)
        return true;
    const QModelIndex prev = index.model()->index(index.row() - 1, 0);
    return prev.data(BucketRole).toInt() != b;
}

TaskCardDelegate::RowGeom
TaskCardDelegate::geometryFor(const QStyleOptionViewItem& option,
                              const QModelIndex& index) const
{
    RowGeom g;
    g.bucket    = index.data(BucketRole).toInt();
    g.hasHeader = startsBucket(index);

    QRect r = option.rect;
    if (g.hasHeader) {
        g.header = QRect(r.left(), r.top(), r.width(), kHeaderH);
        r.setTop(r.top() + kHeaderH);
    }

    // Card: left-aligned, capped width, one gap of breathing room below.
    const int cardW = qMin(r.width(), kMaxCardW);
    g.card = QRect(r.left(), r.top(), cardW, kCardH);

    g.accent = QRect(g.card.left(), g.card.top(), kAccentW, g.card.height());

    const int cy = g.card.center().y();
    int x = g.card.left() + kAccentW + kPad;

    g.check = QRect(x, cy - kCheck / 2, kCheck, kCheck);
    x += kCheck + 11;
    g.dot = QRect(x, cy - kDot / 2, kDot, kDot);
    x += kDot + 11;

    // Right-side widgets are placed from the right edge inward; whatever is left
    // becomes the text column. Order (right→left): × , countdown , [chip].
    int right = g.card.right() - kPad;
    g.del = QRect(right - kDelW, cy - kDelW / 2, kDelW, kDelW);
    right -= kDelW + 10;

    QFont small = option.font;
    small.setPixelSize(15); // v22.1 type step — MUST match the paint pass
    small.setWeight(QFont::ExtraBold);
    const int cdW = QFontMetrics(small).horizontalAdvance(
        countdownText(index.data(DaysUntilRole).toLongLong(),
                      index.data(DueTimeRole).toTime())) + 4;
    g.countdown = QRect(right - cdW, g.card.top(), cdW, g.card.height());
    right -= cdW + 12;

    const int prio = index.data(PriorityRole).toInt();
    if (prio != int(Task::Priority::Medium)) {
        QFont chip = option.font;
        chip.setPixelSize(11); // lockstep with the paint pass, same as cd
        chip.setWeight(QFont::ExtraBold);
        const QString label = priorityLabel(Task::Priority(prio)).toUpper();
        const int chipW = QFontMetrics(chip).horizontalAdvance(label) + 18;
        g.prio = QRect(right - chipW, cy - 11, chipW, 22);
        right -= chipW + 10;
    }

    g.text = QRect(x, g.card.top() + 14, right - x, g.card.height() - 28);
    return g;
}

void TaskCardDelegate::paint(QPainter* painter,
                             const QStyleOptionViewItem& option,
                             const QModelIndex& index) const
{
    const RowGeom g = geometryFor(option, index);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // ---- section header ----------------------------------------------------
    if (g.hasHeader) {
        QFont cap = option.font;
        cap.setPixelSize(11);
        cap.setWeight(QFont::Bold);
        cap.setLetterSpacing(QFont::AbsoluteSpacing, 1);
        painter->setFont(cap);
        painter->setPen(bucketColor(g.bucket));
        painter->drawText(g.header.adjusted(0, 8, 0, 0),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          bucketCaption(g.bucket).toUpper());
    }

    // ---- the card body -----------------------------------------------------
    const QColor accent = index.data(CategoryColorRole).value<QColor>();

    QPainterPath cardPath;
    cardPath.addRoundedRect(g.card, 12, 12);
    painter->fillPath(cardPath, QColor("#FFFFFF"));
    painter->setPen(QPen(QColor("#E6E9E4"), 1));
    painter->drawPath(cardPath);

    // Left accent bar, clipped to the card's rounded left corners.
    painter->save();
    painter->setClipPath(cardPath);
    painter->fillRect(g.accent, accent);
    painter->restore();

    // Checkbox (always empty — every task here is undone by definition).
    QPainterPath box;
    box.addRoundedRect(g.check, 4, 4);
    painter->setPen(QPen(QColor("#B9C0BA"), 1.5));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(box);

    // Category dot.
    painter->setPen(Qt::NoPen);
    painter->setBrush(accent);
    painter->drawEllipse(g.dot);

    // Title + subtitle, both elided so a long title never overruns the chips.
    const QString title = index.data(TitleRole).toString();
    QString sub = index.data(CategoryNameRole).toString();
    const int repeat = index.data(RepeatRole).toInt();
    if (repeat != int(Task::Repeat::None))
        sub += QStringLiteral("   \u27F3 %1")
                   .arg(repeatLabel(Task::Repeat(repeat)));

    QFont tf = option.font;
    tf.setPixelSize(17); // v22.1: the actual answer to "too small to read"
    tf.setWeight(QFont::Bold);
    painter->setFont(tf);
    painter->setPen(QColor("#2B2F36"));
    const QRect titleRect(g.text.left(), g.text.top(), g.text.width(), 24);
    painter->drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
        QFontMetrics(tf).elidedText(title, Qt::ElideRight, g.text.width()));

    // v28.3 — the pieces chip ("☑ 2/5"), folded INTO the subtitle string
    // rather than painted as its own pill. Two reasons: the card's chip
    // row is already contested real estate (priority, TIGHT, countdown all
    // claim corners), and progress is a fact about the task like its
    // category is — it reads naturally in the same quiet grey line. Total
    // of zero means "not a checklist task": no chip, no empty "0/0" noise.
    const int piecesTotal = index.data(PiecesTotalRole).toInt();
    if (piecesTotal > 0) {
        sub += QStringLiteral("   \u2611 %1/%2")
                   .arg(index.data(PiecesDoneRole).toInt())
                   .arg(piecesTotal);
    }

    QFont sf = option.font;
    sf.setPixelSize(14);
    painter->setFont(sf);
    painter->setPen(QColor("#7A828C"));
    const QRect subRect(g.text.left(), titleRect.bottom(), g.text.width(), 20);
    painter->drawText(subRect, Qt::AlignLeft | Qt::AlignVCenter,
        QFontMetrics(sf).elidedText(sub, Qt::ElideRight, g.text.width()));

    // Priority chip (Medium stays silent — the default shouldn't shout).
    if (!g.prio.isNull()) {
        const int prio = index.data(PriorityRole).toInt();
        const QColor c = prio == int(Task::Priority::Urgent)
                             ? theme::danger()
                             : QColor("#8A93A0");
        QFont chip = option.font;
        chip.setPixelSize(11);
        chip.setWeight(QFont::ExtraBold);
        chip.setLetterSpacing(QFont::AbsoluteSpacing, 1);
        painter->setFont(chip);
        painter->setPen(QPen(c, 1));
        painter->setBrush(Qt::NoBrush);
        QPainterPath chipPath;
        chipPath.addRoundedRect(g.prio, 8, 8);
        painter->drawPath(chipPath);
        painter->drawText(g.prio, Qt::AlignCenter,
                          priorityLabel(Task::Priority(prio)).toUpper());
    }

    // Countdown headline, coloured by urgency.
    const qint64 in = index.data(DaysUntilRole).toLongLong();
    QFont cd = option.font;
    cd.setPixelSize(15); // in lockstep with geometryFor's measurement
    cd.setWeight(QFont::ExtraBold);
    painter->setFont(cd);
    painter->setPen(countdownColor(in));
    painter->drawText(g.countdown, Qt::AlignRight | Qt::AlignVCenter,
                      countdownText(in, index.data(DueTimeRole).toTime()));

    // v28 — the affordability pill (§H.6: the verdict lives on THIS page
    // before any model phrases it). Painted only for Tight: Comfortable is
    // the default state of the world and labelling it would be noise, and
    // Unknown's honest sentence needs more room than a pill has — it lives
    // in the nudge, not the card. Tucked under the countdown so the card's
    // question order reads "when is it due → how bad is that".
    if (index.data(AffordabilityRole).toInt()
        == int(afford::Verdict::Tight)) {
        QFont pf = option.font;
        pf.setPixelSize(10);
        pf.setWeight(QFont::ExtraBold);
        painter->setFont(pf);
        const QString label = QObject::tr("TIGHT");
        const int w = QFontMetrics(pf).horizontalAdvance(label) + 14;
        const QRect pill(g.countdown.right() - w,
                         g.countdown.top() + g.countdown.height() / 2 + 12,
                         w, 16);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(194, 91, 84, 26)); // danger red @ 10%
        QPainterPath pillPath;
        pillPath.addRoundedRect(pill, 8, 8);
        painter->drawPath(pillPath);
        painter->setPen(theme::danger());
        painter->drawText(pill, Qt::AlignCenter, label);
    }

    // The × delete affordance.
    QFont x = option.font;
    x.setPixelSize(18);
    painter->setFont(x);
    painter->setPen(QColor("#B0574F"));
    painter->drawText(g.del, Qt::AlignCenter, QStringLiteral("\u00D7"));

    painter->restore();
}

QSize TaskCardDelegate::sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const
{
    int h = kCardH + kGap;
    if (startsBucket(index))
        h += kHeaderH;
    // Width follows the view's content width; height is ours. (option.rect is
    // 0-wide during some measurement passes — fall back to a sane minimum.)
    const int w = option.rect.width() > 0 ? option.rect.width() : kMaxCardW;
    return {w, h};
}

bool TaskCardDelegate::editorEvent(QEvent* event, QAbstractItemModel* /*model*/,
                                   const QStyleOptionViewItem& option,
                                   const QModelIndex& index)
{
    // Only a completed left click acts — press-and-drag-away shouldn't fire.
    if (event->type() != QEvent::MouseButtonRelease)
        return false;
    auto* me = static_cast<QMouseEvent*>(event);
    if (me->button() != Qt::LeftButton)
        return false;

    const RowGeom g = geometryFor(option, index);
    const QPoint  p = me->pos();
    const QString id = index.data(IdRole).toString();

    // v30.7 — "generous" was 28dp against Android's 48. The card is 86 tall
    // so there is room on both axes here, unlike the crowded category rows:
    // both zones take the full minimum, and the card's own "edit" action
    // absorbs whatever they borrow. Paint is untouched — the checkbox still
    // draws at 20, because a 48dp box on this card would look like a bug.
    const bool compact = isCompactScreen();
    if (touch::expand(g.check, compact).contains(p)) {
        emit doneToggled(id, true); // every task here is undone → clicking = done
        return true;
    }
    if (touch::expand(g.del, compact).contains(p)) {
        emit deleteRequested(id);
        return true;
    }
    if (g.card.contains(p)) {
        emit editRequested(id); // click anywhere else on the card = edit
        return true;
    }
    return false; // click landed on a header/gap — let the view handle it
}
