#include "AgendaWidget.h"

#include "Theme.h"
#include "Widgets.h"
#include "AppData.h"
#include "Stats.h"
#include "TrackerService.h"

#include <QMouseEvent>
#include <QTimer>
#include <QApplication>
#include <QPainter>
#include <QScrollArea> // autoScrollTo: the page that holds this widget
#include <QToolTip>    // the drag's sentence, under the pointer
#include <QGraphicsOpacityEffect> // the lifted block's picture, faded
#include <QLabel>                 // ...which is a label holding a pixmap
#include <QScroller>              // released for a finger's drag, taken back
#include <QTouchEvent>            // the phone reads fingers directly
#include <QTextLayout>
#include <QTextOption>

namespace
{
// ---- column-flowed text (owner request) ------------------------------------
// The problem: a description full of HARD line breaks ("item ;\nitem ;\n…")
// makes short lines that word-wrap can never widen — the right half of the
// block sits empty while the bottom clips. The fix is newspaper flow: when
// the text doesn't fit the area at full width, re-flow it into two BALANCED
// half-width columns, so the empty right side carries the overflow.
//
// Why QTextLayout and not drawText: drawText can wrap, but it decides every
// line's position itself — flowing to a second column needs line-by-line
// placement, which is exactly what QTextLayout exists for (createLine gives
// you each line; you choose where it goes). One trick makes '\n' work:
// QTextLayout treats text as a single paragraph and ignores '\n', but it
// HONORS QChar::LineSeparator (U+2028) as a forced break — so we swap them.
//
// Draws into `area` with the painter's current font/pen.
// `budget` (owner-found flaw, second iteration): the height this text may
// CONSUME — usually less than the physical area when something else must
// fit below it. The original rule columnized only when the text overflowed
// its area; a description that "fit" would hog one tall column and starve
// the comments underneath, right next to an empty right half. "Fits" was a
// selfish question. Now it's "fits within your fair share": exceed the
// budget and you columnize, even though the area itself had room.
// (-1 = no neighbor, budget is the whole area — the original behavior.)
// Returns the height actually consumed (so the caller can stack content
// below), or 0 if nothing fit.
// `maxColumns` (§3.39): the side-placement region is ALREADY half a block
// wide — letting the flow sub-columnize it would produce quarter-width
// slivers. maxColumns = 1 means "wrap and clip, never split".
int drawFlowedText(QPainter& p, const QRect& area, const QString& raw,
                   int budget = -1, int maxColumns = 2)
{
    if (raw.isEmpty() || area.height() <= 0 || area.width() <= 0)
        return 0;

    QString text = raw;
    text.replace(QLatin1Char('\n'), QChar::LineSeparator);

    QTextLayout layout(text, p.font());
    QTextOption opt;
    // WrapAtWordBoundaryOrAnywhere: prefer word wrap, but a word wider than
    // a half-width column must still break rather than vanish off the edge.
    opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(opt);

    const int lineH = QFontMetrics(p.font()).lineSpacing();
    if (lineH <= 0 || area.height() < lineH)
        return 0;

    // The effective ceiling: the physical area, tightened by the budget —
    // but never below one line (a starved budget must not erase the text
    // entirely; the neighbor's reservation is capped by the caller anyway).
    const int ceiling = (budget < 0)
                            ? area.height()
                            : qBound(lineH, budget, area.height());

    constexpr int kGap = 14; // breathing room between the two columns

    // Counting pass: how many lines at a given width? (Uniform line height —
    // one font, no rich text — so counting is all the measuring we need.)
    const auto countLines = [&layout](int width) {
        int n = 0;
        layout.beginLayout();
        forever {
            QTextLine line = layout.createLine();
            if (!line.isValid())
                break;
            line.setLineWidth(width); // width in, so the NEXT line knows
            ++n;                      // where this one ended
        }
        layout.endLayout();
        return n;
    };

    // Positioning pass: lay lines top-to-bottom, hopping to the next column
    // when the current one reaches `columnHeight`; lines past the last
    // column are simply not created (clipped).
    const auto positionLines = [&](int colWidth, int columns, int columnHeight) {
        layout.beginLayout();
        int col = 0;
        qreal y = 0, deepest = 0;
        forever {
            if (y + lineH > columnHeight) {         // this column is full
                if (++col >= columns)
                    break;                          // out of columns: clip
                y = 0;
            }
            QTextLine line = layout.createLine();
            if (!line.isValid())
                break;                              // out of text: done
            line.setLineWidth(colWidth);
            line.setPosition(QPointF(col * (colWidth + kGap), y));
            y += lineH;
            deepest = qMax(deepest, y);
        }
        layout.endLayout();
        return int(deepest);
    };

    // Fits at full width? Draw exactly as before — columns only appear when
    // they EARN something, so short text keeps its familiar look.
    const int fullLines = countLines(area.width());
    int consumed;
    if (fullLines * lineH <= ceiling || maxColumns < 2) {
        consumed = positionLines(area.width(), 1, ceiling);
    } else {
        // Two balanced columns: split the line count evenly instead of
        // stuffing column 1 to the brim — even columns read as one piece of
        // text; a full-left/stub-right pair reads as two.
        const int colWidth = (area.width() - kGap) / 2;
        const int colLines = countLines(colWidth);
        const int perColumn =
            qMin((colLines + 1) / 2,                // balanced target…
                 ceiling / lineH);                  // …capped by the share
        consumed = positionLines(colWidth, 2, perColumn * lineH);
    }

    layout.draw(&p, area.topLeft());
    return consumed;
}
} // namespace

namespace
{
// slotHeight() and kTopPad now live in AgendaWidget as public statics, so the
// week view's axis shares the EXACT same grid. The gutter became per-instance
// (m_gutter) so a column can drop its label gutter. Only kRadius stays
// file-private — nobody outside needs it.
constexpr int kRadius = 8; // rounded corners

// slotTop — the ONE place a slot index becomes a y-pixel — used to live here
// as a free function. The visible-window feature made it depend on widget
// state (WHICH slot sits at the top now varies), so it moved into the class.
// Same single-source-of-truth rule, new address: see AgendaWidget::slotTop.
} // namespace

AgendaWidget::AgendaWidget(const AppData* data, const TrackerService* tracker,
                           QWidget* parent)
    : QWidget(parent)
    , m_data(data)
    , m_tracker(tracker)
    , m_date(QDate::currentDate())
    , m_windowStart(plan::kDayStartMinutes) // full day until a page says less
    , m_windowEnd(plan::kDayEndMinutes)
{
    // Without this, mouseMoveEvent only fires while a button is held.
    // We want hover feedback ("+ plan"), so track all movement.
    setMouseTracking(true);

    // A two-finger tap opens a block's menu on a phone (§M.8a), and a widget
    // receives touch events only when it asks for them. Asked ONLY on a
    // phone, so a desktop - even one with a touch display - keeps exactly the
    // mouse delivery it had.
    if (isCompactScreen())
        setAttribute(Qt::WA_AcceptTouchEvents);
    syncHeight();

    // Observe the data we paint — for GEOMETRY, not policy. The shown
    // window is derived from the date's events (data always wins), so an
    // event appearing outside the window must be able to change this
    // widget's height without a page remembering to tell it. Repainting
    // stays the pages' habit too; duplicate update() calls coalesce.
    // (Note connect() happily takes a const sender — observing doesn't
    // require the right to mutate, and m_data stays const.)
    connect(m_data, &AppData::changed, this, [this]() {
        syncHeight();
        update();
    });

    // The live badge (below) ticks once a second, on every agenda that can
    // see the tracked block — not just the day view whose page happens to
    // forward ticks. Subscribing here is the same self-sufficiency as the
    // changed() connect above: a widget that paints live state must be
    // able to repaint when that state moves. Hidden widgets ignore
    // update() for free, so seven idle week columns cost nothing.
    if (m_tracker) {
        connect(m_tracker, &TrackerService::tick, this, [this]() {
            if (m_tracker->state() != TrackerService::State::Idle)
                update();
        });
        connect(m_tracker, &TrackerService::stateChanged,
                this, qOverload<>(&QWidget::update));
    }

    // THE NOW-LINE (31.2.0, F19) moves once a minute on a 30px slot, so a
    // repaint every 30 seconds is always within half a pixel of the truth.
    // Only today's agenda has a line to move, so other days skip the repaint.
    // Hidden widgets ignore update() anyway, so the seven week columns cost
    // nothing while the day view is on screen.
    m_nowTimer = new QTimer(this);
    m_nowTimer->setInterval(30 * 1000);
    connect(m_nowTimer, &QTimer::timeout, this, [this]() {
        const QDateTime now = m_tracker ? m_tracker->nowProvider()
                                        : QDateTime::currentDateTime();
        if (m_date == now.date())
            update();
    });
    m_nowTimer->start();
}

void AgendaWidget::setDate(QDate date)
{
    if (m_date == date)
        return;
    m_date = date;
    disarm(); // slot 12 is a different half-hour tomorrow
    syncHeight(); // the shown window is per-date (its events stretch it)
    update();     // schedule a repaint — NEVER paint directly from here
}

void AgendaWidget::setVisibleWindow(int startMinutes, int endMinutes)
{
    if (m_windowStart == startMinutes && m_windowEnd == endMinutes)
        return;
    m_windowStart = startMinutes;
    m_windowEnd   = endMinutes;
    syncHeight();
    update();
}

QPair<int, int> AgendaWidget::windowCovering(const AppData* data, QDate date,
                                             int prefStartMin, int prefEndMin)
{
    // Sanitize the preference against the DOMAIN grid first (prefs:: already
    // clamps, but this function is public — trust no caller):
    const auto snap = [](int m) {
        return (m / plan::kSlotMinutes) * plan::kSlotMinutes;
    };
    int start = qBound(plan::kDayStartMinutes, snap(prefStartMin),
                       plan::kDayEndMinutes - plan::kSlotMinutes);
    int end   = qBound(start + plan::kSlotMinutes, snap(prefEndMin),
                       plan::kDayEndMinutes);

    // Data always wins: stretch (never shrink) over every block on `date`.
    // A window that can hide a block isn't a preference, it's a trap — the
    // block would still refuse new plans over its slots (isFree says no)
    // while being invisible, an unexplainable "haunted agenda".
    for (const Event* e : data->eventsOn(date)) {
        start = qMin(start, snap(e->plannedStartMinutes));
        // Ceil the end to its slot line so a block ending mid-slot (can't
        // happen today, but this function shouldn't rely on that) still
        // fits entirely inside the shown range.
        const int ceilEnd = ((e->plannedEndMinutes + plan::kSlotMinutes - 1)
                             / plan::kSlotMinutes) * plan::kSlotMinutes;
        end = qMax(end, qMin(ceilEnd, plan::kDayEndMinutes));
    }
    return {start, end};
}

QPair<int, int> AgendaWidget::shownWindow() const
{
    return windowCovering(m_data, m_date, m_windowStart, m_windowEnd);
}

int AgendaWidget::firstShownSlot() const
{
    return (shownWindow().first - plan::kDayStartMinutes) / plan::kSlotMinutes;
}

int AgendaWidget::shownSlotCount() const
{
    const auto w = shownWindow();
    return (w.second - w.first) / plan::kSlotMinutes;
}

int AgendaWidget::slotTop(int slotIndex) const
{
    // The ONE place a (domain) slot index becomes a y-pixel. Every consumer
    // — grid lines, labels, hover, event blocks, hit-testing — goes through
    // here, so painting and clicking can never drift apart. Window-aware:
    // the first SHOWN slot sits at the top pad, whatever its index.
    return kTopPad + (slotIndex - firstShownSlot()) * slotHeight();
}

void AgendaWidget::syncHeight()
{
    setMinimumHeight(sizeHint().height());
    updateGeometry(); // the preferred size changed — let layouts re-ask
}

void AgendaWidget::setGutter(int px)
{
    if (m_gutter == px)
        return;
    m_gutter = px;
    updateGeometry(); // the preferred width changed — let layouts re-ask
    update();
}

void AgendaWidget::setShowTaskDescriptions(bool show)
{
    if (m_showTaskDescriptions == show)
        return;
    m_showTaskDescriptions = show;
    update(); // repaint with the new preference — state changed, so redraw
}

QSize AgendaWidget::sizeHint() const
{
    // With a gutter it's a full day panel; without one it's a slim week
    // column that a horizontal layout will stretch to share the row.
    // Height follows the SHOWN window, not the whole domain grid — that is
    // the entire visible payoff of the hours setting.
    const int width = (m_gutter > 0) ? 560 : 150;
    return {width, kTopPad + shownSlotCount() * slotHeight() + 12};
}

QRect AgendaWidget::spanRect(int startMin, int endMin,
                             int column, int columnCount) const
{
    // Minutes -> pixels: the single place this conversion exists, so painting,
    // hit-testing, AND the live resize preview all agree on where things are.
    // v31.3 added the horizontal half of that same promise: when blocks stack,
    // the column arithmetic has to live here too, or the rect you click is not
    // the rect you saw.
    const int startSlot =
        (startMin - plan::kDayStartMinutes) / plan::kSlotMinutes;
    const int slotCount = (endMin - startMin) / plan::kSlotMinutes;

    const int band = width() - m_gutter - 4;
    const int cols = qMax(1, columnCount);
    const int col  = qBound(0, column, cols - 1);
    // Each edge is computed from its own exact fraction of the band rather
    // than from a rounded column width. Three columns of an odd number of
    // pixels would otherwise leave a ragged strip on the right, and the
    // rounding error would grow with the column index.
    const int left  = m_gutter + (band * col) / cols;
    const int right = m_gutter + (band * (col + 1)) / cols;
    // A hairline between neighbours, so two stacked blocks read as two
    // objects and not as one wide one with a line drawn on it.
    const int gap = (cols > 1) ? 2 : 0;

    return QRect(left, slotTop(startSlot) + 2,
                 right - left - gap, slotCount * slotHeight() - 4);
}

daylay::Slotting AgendaWidget::slottingFor(const Event& e) const
{
    return daylay::columns(m_data->eventsOn(m_date)).value(e.id);
}

QRect AgendaWidget::eventRect(const Event& e) const
{
    const daylay::Slotting s = slottingFor(e);
    return spanRect(e.plannedStartMinutes, e.plannedEndMinutes,
                    s.column, s.columnCount);
}

int AgendaWidget::minutesAtY(int y) const
{
    // Snap to the NEAREST slot line so dragging feels magnetic to the grid.
    // Slot indices stay DOMAIN indices (0 == 6 AM) — the window only shifts
    // which of them y == kTopPad lands on.
    int slot = firstShownSlot() + qRound(double(y - kTopPad) / slotHeight());
    slot = qBound(firstShownSlot(), slot,
                  firstShownSlot() + shownSlotCount());
    return plan::kDayStartMinutes + slot * plan::kSlotMinutes;
}

AgendaWidget::Edge AgendaWidget::edgeAt(const QPoint& pos, QString* eventId) const
{
    constexpr int kGrab = 6; // px band around an edge that counts as "on it"
    for (const Event* e : m_data->eventsOn(m_date)) {
        const QRect r = eventRect(*e);
        if (pos.x() < r.left() || pos.x() > r.right())
            continue; // outside this block's horizontal span
        if (qAbs(pos.y() - r.top()) <= kGrab) {
            if (eventId) *eventId = e->id;
            return Edge::Top;
        }
        if (qAbs(pos.y() - r.bottom()) <= kGrab) {
            if (eventId) *eventId = e->id;
            return Edge::Bottom;
        }
    }
    if (eventId)
        eventId->clear();
    return Edge::None;
}

int AgendaWidget::slotAt(const QPoint& pos) const
{
    // The y < kTopPad guard is correctness, not politeness: C++ integer
    // division truncates TOWARD ZERO, so a click 5 px into the headroom
    // gives (5 - 12) / 30 == 0 — it would silently register as slot 0
    // (6 AM) instead of "no slot". Negative division is a classic
    // boundary bug in C++ and Java alike.
    if (pos.x() < m_gutter || pos.y() < kTopPad)
        return -1;
    const int slot = firstShownSlot() + (pos.y() - kTopPad) / slotHeight();
    return (slot < firstShownSlot() + shownSlotCount()) ? slot : -1;
}

void AgendaWidget::setHighlightRuns(QVector<QPair<int, int>> runs)
{
    m_highlightRuns = std::move(runs);
    update(); // input writes state + update(); paint only reads — the rule
}

void AgendaWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Paint our own floor FIRST. Without this line the background comes
    // from autoFillBackground + the palette (QScrollArea::setWidget turns
    // that on behind our back) — which went black on dark-mode Windows.
    // A custom-painted widget should own every pixel it shows.
    p.fillRect(rect(), theme::surface());

    const QFont small = scaledFont(font(), -1.5);

    // 1) The time grid: solid line + label on the hour, dashed on the half.
    //    Only the shown window's slots — i stays a DOMAIN index throughout,
    //    so the hour labels and the "on the hour" test need no translation.
    const int firstSlot = firstShownSlot();
    const int lastSlot  = firstSlot + shownSlotCount();
    for (int i = firstSlot; i < lastSlot; ++i) {
        const int y = slotTop(i);
        const bool onTheHour = (i % 2 == 0);

        p.setPen(QPen(onTheHour ? theme::line() : QColor("#EEF0EC"), 1,
                      onTheHour ? Qt::SolidLine : Qt::DashLine));
        p.drawLine(m_gutter, y, width(), y);

        // Labels only exist when there IS a gutter to hold them. A week
        // column (m_gutter == 0) borrows the shared axis on the left instead.
        if (onTheHour && m_gutter > 0) {
            p.setPen(theme::inkSoft());
            p.setFont(small);
            const int minutes = plan::kDayStartMinutes + i * plan::kSlotMinutes;
            p.drawText(QRect(0, y - 8, m_gutter - 10, 16),
                       Qt::AlignRight | Qt::AlignVCenter,
                       timeLabel(minutes).remove(":00")); // "6 AM", not "6:00 AM"
        }
    }

    // 2) The invitation to plan, from EITHER source. One resolved slot and one
    //    paint path, so a mouse hover and a phone's armed slot can never drift
    //    apart visually. Armed is drawn stronger and says what to do next,
    //    because on a touchscreen there is no hover to explain itself.
    const int hintSlot = m_hoverSlot >= 0 ? m_hoverSlot : m_armedSlot;
    if (hintSlot >= 0) {
        // Guard on the slot still being FREE: a block can arrive underneath an
        // armed slot from a sync or the assistant, and an invitation left over
        // occupied time invites a refusal.
        const int hintStart =
            plan::kDayStartMinutes + hintSlot * plan::kSlotMinutes;
        if (m_data->isFree(m_date, hintStart, hintStart + plan::kSlotMinutes)) {
            // ARMED is about the state, not about the absence of hover:
            // Android synthesises a mouse move at the touch point, so
            // m_hoverSlot is set on a phone too and "no hover" would never be
            // true where the hint matters most.
            const bool armed = (m_armedSlot == hintSlot);
            const QRect r(m_gutter, slotTop(hintSlot) + 2,
                          width() - m_gutter - 4, slotHeight() - 4);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(47, 126, 110, armed ? 34 : 18));
            p.drawRoundedRect(r, kRadius, kRadius);
            p.setPen(theme::focus());
            p.setFont(small);
            p.drawText(r.adjusted(12, 0, 0, 0),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       armed ? tr("+ plan  ·  tap again")
                             : QStringLiteral("+ plan"));
        }
    }

    // 3) The planned Events, each in its category's colour, with the little
    //    plan-vs-actual bar along the bottom — reality visibly filling the
    //    plan, which is the entire idea of the app in one pixel strip.
    for (const Event* e : m_data->eventsOn(m_date)) {
        // Colour comes from the block's life area, whatever its identity
        // (activity, task, or ad-hoc) — AppData resolves it in ONE place.
        // Ad-hoc blocks resolve to no category and paint neutral grey:
        // visibly "outside your named life areas", which is the truth.
        const Category* category =
            m_data->categoryById(m_data->eventCategoryId(*e));
        const QColor color = category ? category->color : theme::inkSoft();
        // A block being carried (31.2.0) stays faded where it was, so the
        // dashed preview reads as "it will go there" and not as a second block.
        // A block that was MOVED away - rescheduled, this original kept as
        // the record (move-and-swap §M.11) - is faded too, or the day would
        // show the same block twice at full strength.
        const bool carried   = m_dragging && e->id == m_dragEventId;
        const bool movedAway = e->outcome == BlockOutcome::Moved;
        p.setOpacity(carried ? 0.35 : movedAway ? 0.45 : 1.0);
        // While dragging an edge, THIS block is drawn at its live preview span
        // (the fixed edge stays, the grabbed edge follows the mouse) so resize
        // feedback is immediate — paint still only READS state, never writes.
        const bool isResizing = (m_resizing && e->id == m_resizeEventId);
        // The preview keeps the block's COLUMN: a resize changes when it
        // happens, never who it sits beside, and a preview that jumped to
        // full width would promise a move it is not making.
        const daylay::Slotting slot = slottingFor(*e);
        const QRect  rect  = isResizing
                                 ? spanRect(m_previewStart, m_previewEnd,
                                            slot.column, slot.columnCount)
                                 : eventRect(*e);

        // SOFT BLOCKS (v3): the category colour is the block's identity,
        // not its literal paint. Pastel tint for the large fill, the deep
        // companion for text, and a thin stripe of the raw hue so
        // categories stay recognisable at a glance. Rule of thumb: the
        // bigger the area, the softer the colour — saturation is for
        // small accents.
        p.setPen(QPen(theme::mix(color, theme::surface(), 0.55f), 1));
        p.setBrush(theme::pastel(color));
        p.drawRoundedRect(rect, kRadius + 1, kRadius + 1);

        // The identity stripe, with a small painter trick: clip to a 5-px
        // band and draw the SAME rounded rect in the raw colour — the clip
        // hands us perfectly matching rounded corners for free.
        p.save();
        p.setClipRect(QRect(rect.left(), rect.top(), 5, rect.height()));
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawRoundedRect(rect, kRadius + 1, kRadius + 1);
        p.restore();

        // The block being MOVED (31.2.0, §M.8) is outlined, so "what would a
        // tap put down?" is answered on the timeline itself and not only in
        // the banner above it.
        if (!m_pickingForId.isEmpty() && e->id == m_pickingForId) {
            p.setPen(QPen(theme::focus(), 2, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(rect.adjusted(1, 1, -1, -1), kRadius + 1,
                              kRadius + 1);
        }

        // Committed totals + the live, still-running seconds if this very
        // block is being tracked right now.
        stats::Totals t = stats::eventTotals(*e);
        if (m_tracker->isTrackingEvent(e->id)) {
            // Three-way, matching the state — never dump distracted live time
            // into break (the else-trap we fixed in eventTotals and the dialog).
            const qint64 live = m_tracker->liveSeconds();
            switch (m_tracker->state()) {
            case TrackerService::State::Focusing:   t.focusSeconds      += live; break;
            case TrackerService::State::OnBreak:    t.breakSeconds      += live; break;
            case TrackerService::State::Distracted: t.distractedSeconds += live; break;
            case TrackerService::State::Idle:       break;
            }
        }

        const QRect inner = rect.adjusted(13, 5, -8, -5);
        p.setPen(theme::deep(color));
        QFont bold = font();
        bold.setBold(true);
        p.setFont(bold);

        // Line 1 — the block's identity, from the same resolver every other
        // screen uses. Elided, because ad-hoc titles are free text and free
        // text is long. The ✓ for a finished linked task goes on WHICHEVER
        // line shows the task — line 1 for a task-identity block, the
        // subtitle for an activity block working on a task (marking the
        // activity itself "✓ done" would be a small lie).
        QString label = m_data->eventLabel(*e);
        const Task* linkedTask = m_data->taskById(e->taskId);
        const bool taskIsLine1 = linkedTask && label == linkedTask->title;
        if (taskIsLine1 && linkedTask->done)
            label.prepend(QStringLiteral("✓ "));

        // THE LIVE BADGE (owner report: "I don't see any update") — the
        // plan-vs-actual bar below is honest but nearly mute: on a 2-hour
        // block, a minute of tracking is under 1% of a 5-px strip. The
        // tracked block now says so out loud: "● Focusing · 7:12", in the
        // state's own colour, ticking every second. It answers BOTH halves
        // of the owner's doubt at a glance — is it recording, and as WHAT
        // (a paused Pomodoro driving Distracted shows red and says so,
        // instead of silently growing a red sliver). Day view only
        // (m_gutter > 0): week columns are too narrow for line-1 real
        // estate, and their bar still carries the totals.
        int line1Right = inner.width();
        if (m_gutter > 0 && m_tracker->isTrackingEvent(e->id)) {
            QColor liveColor = theme::danger(); // Distracted
            QString liveWord = tr("Distracted");
            if (m_tracker->state() == TrackerService::State::Focusing) {
                liveColor = theme::focus();
                liveWord  = tr("Focusing");
            } else if (m_tracker->state() == TrackerService::State::OnBreak) {
                liveColor = theme::brk();
                liveWord  = tr("On break");
            }
            const qint64 s = m_tracker->liveSeconds();
            // Digital m:ss (h:mm:ss past the hour) rather than the app's
            // "7m" prose style: the visibly ticking seconds ARE the
            // feedback — a value that only moves once a minute would
            // re-create the very silence being fixed.
            const QString clock =
                s >= 3600 ? QStringLiteral("%1:%2:%3")
                                .arg(s / 3600)
                                .arg((s % 3600) / 60, 2, 10, QChar('0'))
                                .arg(s % 60, 2, 10, QChar('0'))
                          : QStringLiteral("%1:%2")
                                .arg(s / 60)
                                .arg(s % 60, 2, 10, QChar('0'));
            const QString badge =
                QStringLiteral("● %1 · %2").arg(liveWord, clock);
            p.setFont(small);
            const int badgeW = QFontMetrics(small).horizontalAdvance(badge);
            p.setPen(theme::deep(liveColor));
            p.drawText(inner, Qt::AlignRight | Qt::AlignTop, badge);
            p.setFont(bold);
            p.setPen(theme::deep(color));
            line1Right -= badgeW + 8; // the title yields; the badge never
                                      // fights an elided name for pixels
        }

        p.drawText(inner, Qt::AlignLeft | Qt::AlignTop,
                   QFontMetrics(bold).elidedText(label, Qt::ElideRight,
                                                 line1Right));

        // Line 2 (needs a 2-slot block) — what you're DOING in the block:
        // the custom label wins (the user typed it to be shown), else the
        // linked task ("Study GTI350" on top, "Lab 4" underneath). Without
        // either, the old time-range line keeps its seat.
        // Two INDEPENDENT facts, two lines (owner request — it used to be
        // either/or, and linking a task silently hid your comments): the
        // linked task is the structured "what", your label is the free-text
        // "and also". Both earned their pixels; neither evicts the other.
        const bool hasTaskLine = linkedTask && !taskIsLine1;
        // eventBody, not raw e->title: for an ad-hoc block the first line
        // of the title IS line 1 up there — the body is only what's left.
        // Painting the raw title would print the headline twice.
        const QString body = m_data->eventBody(*e);
        const bool hasComments = !body.isEmpty() && body != label;
        const int slotCount = (e->plannedEndMinutes - e->plannedStartMinutes)
                          / plan::kSlotMinutes;
        QString timeLine = QStringLiteral("%1 – %2 · %3")
                               .arg(timeLabel(e->plannedStartMinutes),
                                    timeLabel(e->plannedEndMinutes),
                                    durationLabel(slotCount));
        // Recurrence rides the anatomy line (v19.10): the ⟳ chip is the
        // same vocabulary the task rows already speak, so a glance reads
        // both kinds of repetition identically.
        // v31 reads it from the RULE that made this block rather than
        // from the block itself. The chip means what it always meant --
        // "this one repeats" -- and is now true of EVERY occurrence
        // rather than only of the newest link in a chain, which is what
        // it quietly meant before.
        if (const Schedule* sched = m_data->scheduleById(e->scheduleId))
            if (sched->repeat != Task::Repeat::None)
                timeLine += QStringLiteral(" · %1")
                                .arg(repeatLabel(sched->repeat));
        // A faded block says why in words, not only in opacity (§M.11).
        if (e->outcome == BlockOutcome::Moved)
            timeLine += QStringLiteral(" · %1").arg(tr("moved"));
        p.setFont(small);
        p.setPen(theme::inkSoft());
        const QFontMetrics smallFm(small);
        // Line order (owner request): name, TIME, then the description —
        // the time is the block's fixed anatomy, so it sits in the same
        // place on every block; the free-text detail reads below it.
        // Consequence, accepted: a 2-slot (1h) block only has room for the
        // time line, so the description shows on blocks of 3+ slots.
        if (rect.height() >= 2 * slotHeight() - 6)
            p.drawText(inner.adjusted(0, 18, 0, 0), Qt::AlignLeft | Qt::AlignTop,
                       smallFm.elidedText(timeLine, Qt::ElideRight,
                                          inner.width()));
        // Lines stack below the time at a running y-offset: the task line
        // (one line, elided — task titles are single-line by nature), then
        // the comments word-wrapped into whatever height remains above the
        // plan-vs-actual bar. drawText clips to its rect, so a long note
        // simply shows as much as the block is tall enough to hold: resize
        // the block, see more.
        int lineY = 36; // first slot under the time line
        const bool tallEnough = rect.height() >= 3 * slotHeight() - 6;
        const QString desc = (m_showTaskDescriptions && linkedTask)
                                 ? linkedTask->description
                                 : QString();

        // WHERE the description lives is decided BEFORE the task line is
        // drawn, because the answer changes the task line's width (§3.39,
        // owner-spotted): on a short block there is no room BELOW the task
        // line, but the right half BESIDE it sits empty — so the
        // description moves there and the task line keeps the left half.
        // The trigger is GEOMETRIC, in line-height units ("fewer than two
        // lines would fit below"), deliberately not a does-the-text-fit
        // measurement: fit tests wobble between font stacks (the §3.34
        // budget lesson — bit on Windows, scraped by on Linux); a
        // line-height threshold behaves identically everywhere.
        bool sideDesc = false;
        if (!desc.isEmpty() && hasTaskLine && tallEnough) {
            const int belowH =
                rect.bottom() - 12 - (inner.top() + lineY + 18);
            sideDesc = belowH < 2 * smallFm.lineSpacing();
        }
        const int kMidGap = 14;
        const int halfW = (inner.width() - kMidGap) / 2;

        if (hasTaskLine && tallEnough) {
            const QString taskLine =
                (linkedTask->done ? QStringLiteral("✓ ") : QString())
                + linkedTask->title;
            p.drawText(inner.adjusted(0, lineY, 0, 0),
                       Qt::AlignLeft | Qt::AlignTop,
                       smallFm.elidedText(taskLine, Qt::ElideRight,
                                          sideDesc ? halfW : inner.width()));
            if (sideDesc) {
                // Right half, anchored at the task line's own row, running
                // down to the bar — wrap and clip, never sub-columnize
                // (maxColumns = 1: half a block can't afford quarter
                // columns).
                const QRect side(inner.left() + halfW + kMidGap,
                                 inner.top() + lineY,
                                 inner.width() - halfW - kMidGap,
                                 rect.bottom() - 12 - (inner.top() + lineY));
                drawFlowedText(p, side, desc, -1, 1);
            }
            lineY += 18;
        }

        // Roomy block: the description's usual home — indented 12px below
        // the task line, budget-aware so the comments below keep their
        // share, advancing lineY by what it used. (Also covers
        // task-identity blocks, where the task is line 1 and the
        // description sits right under the time.)
        if (!desc.isEmpty() && !sideDesc && tallEnough) {
            const int top = inner.top() + lineY;
            const QRect descArea(inner.left() + 12, top,
                                 inner.width() - 12,
                                 rect.bottom() - 12 - top);
            if (descArea.height() >= smallFm.height()) {
                int budget = descArea.height();
                if (hasComments) {
                    const int need = smallFm.boundingRect(
                        QRect(0, 0, inner.width(), 1000),
                        Qt::TextWordWrap, body).height();
                    budget -= qMin(need + 2, descArea.height() / 2);
                }
                const int used = drawFlowedText(p, descArea, desc, budget);
                lineY += used + 2;
            }
        }

        if (hasComments && tallEnough) {
            const int top = inner.top() + lineY;
            // In side mode the right half belongs to the description all
            // the way down — comments get the LEFT half beneath the task
            // line (and on such short blocks, that is usually one line).
            const QRect textArea(inner.left(), top,
                                 sideDesc ? halfW : inner.width(),
                                 rect.bottom() - 12 - top);
            if (textArea.height() >= smallFm.height())
                drawFlowedText(p, textArea, body, -1, sideDesc ? 1 : 2);
        }

        // Mini plan-vs-actual bar — now in the app's SEMANTIC colours
        // (green = focus, amber = break, same as every other chart) on a
        // white inset track, instead of the old white-on-saturated
        // overlays that only worked on loud fills.
        const QRect bar(inner.left(), rect.bottom() - 9, inner.width(), 5);
        p.setPen(Qt::NoPen);
        p.setBrush(theme::surface());
        p.drawRoundedRect(bar, 2, 2);

        const qint64 planned = e->plannedSeconds();
        if (planned > 0 && t.total() > 0) {
            const int fw = int(bar.width() * qMin<qint64>(t.focusSeconds, planned) / planned);
            qint64 room = planned - qMin<qint64>(t.focusSeconds, planned);
            const int bw = int(bar.width() * qMin<qint64>(t.breakSeconds, room) / planned);
            room -= qMin<qint64>(t.breakSeconds, room);
            const int dw = int(bar.width() * qMin<qint64>(t.distractedSeconds, room) / planned);
            p.setBrush(theme::focus());
            p.drawRoundedRect(QRect(bar.left(), bar.top(), fw, bar.height()), 2, 2);
            p.setBrush(theme::brk());
            p.drawRoundedRect(QRect(bar.left() + fw, bar.top(), bw, bar.height()), 2, 2);
            p.setBrush(theme::danger()); // distraction, in the danger hue
            p.drawRoundedRect(QRect(bar.left() + fw + bw, bar.top(), dw, bar.height()), 2, 2);
        }
    }

    p.setOpacity(1.0); // the carried block's fade stops with the blocks

    // 4a) THE NOW-LINE (31.2.0, F19): a red rule across today at this minute,
    //     over the blocks, so a block you are inside shows where in it you
    //     are. The clock is the tracker's, so the debug panel's fake clock
    //     moves it too. fillRect rather than a pen: a 2px rectangle lands on
    //     whole pixels, where an antialiased pen would smear across three.
    {
        const QDateTime now = m_tracker ? m_tracker->nowProvider()
                                        : QDateTime::currentDateTime();
        const int nowMin = now.time().hour() * 60 + now.time().minute();
        const auto window = shownWindow();
        if (m_date == now.date() && nowMin >= window.first
            && nowMin < window.second) {
            // The same conversion slotTop() makes, at minute resolution.
            const int y = kTopPad
                        + (nowMin - window.first) * slotHeight()
                              / plan::kSlotMinutes;
            p.fillRect(QRect(m_gutter, y - 1, width() - m_gutter, 2),
                       theme::danger());
            p.setPen(Qt::NoPen);
            p.setBrush(theme::danger());
            p.drawEllipse(QPoint(m_gutter + 3, y), 4, 4);
        }
    }

    // 4b) THE DROP PREVIEW (31.2.0): where a carried block would land, or the
    //     block it would trade places with. Green when the domain allows it,
    //     red when it refuses - and the refusal's sentence is the tooltip
    //     under the pointer, so the colour never has to be decoded alone.
    if (!m_dropPreview.eventId.isEmpty()) {
        const bool   refused = !m_dropPreview.why.isEmpty();
        const QColor ink     = refused ? theme::danger() : theme::focus();
        if (const Event* other = m_data->eventById(m_dropPreview.swapWithId)) {
            p.setPen(QPen(ink, 2.5));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(eventRect(*other).adjusted(-2, -2, 2, 2),
                              kRadius + 2, kRadius + 2);
        } else if (const Event* dragged =
                       m_data->eventById(m_dropPreview.eventId);
                   dragged && m_dropPreview.startMin >= 0
                   && !m_dropPreview.unchanged) {
            const int end = m_dropPreview.startMin
                          + (dragged->plannedEndMinutes
                             - dragged->plannedStartMinutes);
            const QRect r = spanRect(m_dropPreview.startMin, end);
            QColor fill = ink;
            fill.setAlphaF(0.12f);
            p.setPen(QPen(ink, 1.5, Qt::DashLine));
            p.setBrush(fill);
            p.drawRoundedRect(r, kRadius + 1, kRadius + 1);
            if (r.height() >= 16) {
                p.setFont(small);
                p.setPen(ink);
                p.drawText(r.adjusted(10, 0, -6, 0),
                           Qt::AlignLeft | Qt::AlignVCenter,
                           QStringLiteral("%1 – %2")
                               .arg(timeLabel(m_dropPreview.startMin),
                                    timeLabel(end)));
            }
        }
    }

    // 5) Placement invitations (needs-a-block part 3) — LAST, so they sit
    //    on top of the empty grid they point at. Translucent fill + dashed
    //    border in the focus green: unmistakably "click me", unmistakably
    //    not a real block. spanRect does the geometry — the same one
    //    formula every painted rectangle here already rides.
    if (!m_highlightRuns.isEmpty()) {
        QPen dash(theme::focus(), 1.5, Qt::DashLine);
        QColor fill = theme::focus();
        fill.setAlphaF(0.10f);
        p.setFont(small);
        for (const auto& run : m_highlightRuns) {
            const QRect r = spanRect(run.first, run.second);
            p.setPen(dash);
            p.setBrush(fill);
            p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 8, 8);
            if (r.height() >= 18) {
                p.setPen(theme::focus());
                p.drawText(r, Qt::AlignCenter,
                           tr("%1 – %2 free · click to place")
                               .arg(timeLabel(run.first),
                                    timeLabel(run.second)));
            }
        }
    }

    // 6) THE EMPTY DAY'S ONE INSTRUCTION (v30.7).
    //
    // Press-and-hold is how you plan a block on a phone, and it is not
    // discoverable — docs/ANDROID.md had to write it down for exactly that
    // reason. It used to be said in a permanent two-line caption above the
    // grid, which cost 60dp of every day forever to teach something once.
    //
    // So it moved in here, and only for a day with nothing on it. That is
    // the whole trick: an empty timeline has room going spare and a person
    // looking at one has nothing else to act on, while a day with blocks in
    // it needs no explanation and gets none. The hint disappears the moment
    // it stops being useful, which no permanently-placed label can do.
    //
    // Only where the gesture is real: a week column (m_gutter == 0) is too
    // narrow for the sentence, and on a desktop the caption above is still
    // there because there is room for it.
    if (m_gutter > 0 && m_data->eventsOn(m_date).isEmpty()) {
        p.setPen(theme::inkSoft());
        p.setFont(small);
        const QRect room(m_gutter + 16, kTopPad,
                         width() - m_gutter - 32, height() - kTopPad);
        p.drawText(room, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
                   isCompactScreen()
                       ? tr("Nothing planned yet.\n"
                            "Press and hold a free slot to say what you're doing.")
                       : tr("Nothing planned yet.\n"
                            "Click a free slot to say what you're doing."));
    }
}

void AgendaWidget::mousePressEvent(QMouseEvent* event)
{
    // Touch and mouse part company here. See the block comment in the header:
    // on a touchscreen a press is not yet a decision, because the identical
    // gesture starts a scroll.
    const bool touch =
        m_forceTouch || event->source() != Qt::MouseEventNotSynthesized;
    if (touch) {
        // A test forcing the phone's gestures sends mouse events; a real
        // phone sends touch events (event()). Both reach ONE place.
        touchPressed(event->pos());
        return;
    }

    // Only the LEFT button acts (31.2.0). A right press is the beginning of a
    // context menu, which contextMenuEvent now answers with the block's menu;
    // before this line it ALSO opened the block, and on an edge it started a
    // resize that the matching release then committed.
    if (event->button() != Qt::LeftButton)
        return;

    // An edge grab starts a RESIZE and pre-empts everything else — it must win
    // over "open the event", since the edge sits inside the event's rect.
    QString edgeId;
    const Edge edge = edgeAt(event->pos(), &edgeId);
    if (edge != Edge::None) {
        if (const Event* e = m_data->eventById(edgeId)) {
            m_resizing      = true;
            m_resizeEventId = edgeId;
            m_resizeEdge    = edge;
            m_previewStart  = e->plannedStartMinutes; // seed the preview at the
            m_previewEnd    = e->plannedEndMinutes;    // event's current span
            update();
        }
        return;
    }

    // Events first — they sit on top of slots, so they win the click,
    // exactly as they win visually.
    for (const Event* e : m_data->eventsOn(m_date)) {
        if (eventRect(*e).contains(event->pos())) {
            // With dragging on, a left press on a body is not yet a click
            // (31.2.0): it opens on RELEASE if the mouse stayed put, and
            // becomes a drag if it moved. With it off - the Compare dialog -
            // it opens on press, as it always has.
            if (m_blockDragEnabled && event->button() == Qt::LeftButton) {
                m_dragEventId  = e->id;
                m_dragPressPos = event->pos();
                m_dragGrabPx   = event->pos().y() - eventRect(*e).top();
                m_dragging     = false;
                return;
            }
            emit eventClicked(e->id);
            return;
        }
    }

    const int slot = slotAt(event->pos());
    if (slot < 0)
        return;
    const int startMin = plan::kDayStartMinutes + slot * plan::kSlotMinutes;
    if (m_data->isFree(m_date, startMin, startMin + plan::kSlotMinutes))
        emit emptySlotClicked(slot);
}

void AgendaWidget::mouseMoveEvent(QMouseEvent* event)
{
    // A finger's movement - the lifted drag, or the travel that turns a
    // press into a scroll. Shared with the touch route (touchMoved).
    if (touchMoved(event->pos(), event->globalPosition().toPoint()))
        return;

    // ---- 0) A block being carried (31.2.0) ---------------------------------
    // Pressed on a body: nothing happens until the mouse has moved as far as
    // the platform says a drag must, so a slightly shaky click still opens
    // the block instead of nudging it.
    if (!m_dragEventId.isEmpty()) {
        if (!m_dragging) {
            if ((event->pos() - m_dragPressPos).manhattanLength()
                < QApplication::startDragDistance())
                return;
            m_dragging  = true;
            m_hoverSlot = -1; // no "+ plan" invitation under a carried block
            setCursor(Qt::ClosedHandCursor);
        }
        const QPoint global = event->globalPosition().toPoint();
        if (!m_resolvesOwnDrops) {
            emit blockDragMoved(m_dragEventId, global, m_dragGrabPx);
            update(); // the carried block still dims in its own column
            return;
        }
        const DropTarget t =
            dropTargetAt(event->pos(), m_dragEventId, m_dragGrabPx);
        setDropPreview(t);
        const QString sentence = describeDrop(t);
        if (sentence.isEmpty())
            QToolTip::hideText();
        else
            QToolTip::showText(global, sentence, this);
        autoScrollTo(event->pos());
        return;
    }

    // ---- 1) A resize drag in progress: update the clamped preview span -----
    if (m_resizing) {
        const int snapped = minutesAtY(event->pos().y());
        if (m_resizeEdge == Edge::Bottom) {
            // Fixed edge = start; the end follows the mouse, clamped so it can
            // grow only up to the NEXT event (or midnight) and never shrink
            // below one slot. This is the UI clamp: it makes the illegal drag
            // impossible to even express. The domain still re-checks on commit.
            int maxEnd = plan::kDayEndMinutes;
            for (const Event* e : m_data->eventsOn(m_date)) {
                if (e->id == m_resizeEventId)
                    continue;
                if (e->plannedStartMinutes >= m_previewStart
                    && e->plannedStartMinutes < maxEnd)
                    maxEnd = e->plannedStartMinutes;
            }
            m_previewEnd =
                qBound(m_previewStart + plan::kSlotMinutes, snapped, maxEnd);
        } else if (m_resizeEdge == Edge::Top) {
            // Fixed edge = end; the start follows the mouse, clamped to the
            // PREVIOUS event (or day start) and one-slot minimum.
            int minStart = plan::kDayStartMinutes;
            for (const Event* e : m_data->eventsOn(m_date)) {
                if (e->id == m_resizeEventId)
                    continue;
                if (e->plannedEndMinutes <= m_previewEnd
                    && e->plannedEndMinutes > minStart)
                    minStart = e->plannedEndMinutes;
            }
            m_previewStart =
                qBound(minStart, snapped, m_previewEnd - plan::kSlotMinutes);
        }
        update();
        return;
    }

    // ---- 2) Not dragging: an edge under the mouse shows the ↕ resize cursor -
    QString edgeId;
    if (edgeAt(event->pos(), &edgeId) != Edge::None) {
        setCursor(Qt::SizeVerCursor);         // the "grab to resize" affordance
        if (m_hoverSlot != -1) {              // suppress the "+ plan" hint
            m_hoverSlot = -1;
            update();
        }
        return;
    }

    // ---- 3) Otherwise: the existing body / free-slot hover feedback --------
    bool overEvent = false;
    for (const Event* e : m_data->eventsOn(m_date))
        if (eventRect(*e).contains(event->pos()))
            overEvent = true;

    int hover = slotAt(event->pos());
    if (hover >= 0 && !overEvent) {
        const int startMin = plan::kDayStartMinutes + hover * plan::kSlotMinutes;
        if (!m_data->isFree(m_date, startMin, startMin + plan::kSlotMinutes))
            hover = -1; // occupied — no "+ plan" invitation
    } else {
        hover = -1;
    }

    setCursor(overEvent || hover >= 0 ? Qt::PointingHandCursor
                                      : Qt::ArrowCursor);
    if (hover != m_hoverSlot) {
        m_hoverSlot = hover;
        update();
    }
}

void AgendaWidget::cancelPendingTouch()
{
    if (m_longPress)
        m_longPress->stop();
    if (m_blockHold)
        m_blockHold->stop(); // a press that became a scroll lifts nothing
    m_pendingSlot = -1;
    m_pressWasArmed = false;
    m_pendingEventId.clear();
    m_secondTap = false;     // and a second tap that scrolled is no double-tap
}

void AgendaWidget::disarm()
{
    if (m_armedSlot < 0)
        return;
    m_armedSlot = -1;
    update();
}

void AgendaWidget::armLongPress()
{
    if (!m_longPress) {
        m_longPress = new QTimer(this);
        m_longPress->setSingleShot(true);
        // 450ms: long enough that a scroll has begun moving by then, short
        // enough not to feel like a hang. Qt's own tap-and-hold is 700ms,
        // which for a gesture people repeat all day reads as slow.
        m_longPress->setInterval(450);
        connect(m_longPress, &QTimer::timeout, this, [this]() {
            // A hold on a FREE SLOT plans a block. (A block has its own,
            // longer hold - armBlockHold, §M.8a.) cancelPendingTouch() is
            // also what makes the release that follows do nothing: the hold
            // has already answered for this press.
            const int slot = m_pendingSlot;
            const bool scrolling = pageIsScrolling();
            cancelPendingTouch();
            disarm(); // a hold must not leave an armed slot behind
            if (scrolling) {
                return;
            }
            if (slot >= 0)
                emit emptySlotClicked(slot);
        });
    }
    m_longPress->start();
}

void AgendaWidget::setTargetPicking(const QString& movingEventId)
{
    if (m_pickingForId == movingEventId)
        return;
    m_pickingForId = movingEventId;
    disarm(); // a slot armed before the move means nothing now
    update(); // the outline appears, or goes
}

void AgendaWidget::contextMenuEvent(QContextMenuEvent* event)
{
    // A right-click on a block asks the page for the block's menu - the
    // desktop's door to where a phone's hold goes. Anywhere else, Qt's
    // default, which is nothing.
    for (const Event* e : m_data->eventsOn(m_date)) {
        if (eventRect(*e).contains(event->pos())) {
            emit eventContextMenuRequested(e->id, event->globalPos());
            event->accept();
            return;
        }
    }
    QWidget::contextMenuEvent(event);
}

// ---- the finger gestures, one implementation for both routes (§M.8a) -----

void AgendaWidget::touchPressed(const QPoint& pos)
{
    // Captured BEFORE anything clears it: "was this press on the slot the
    // last tap armed?" is the whole question the second tap asks.
    const int wasArmed = m_armedSlot;
    cancelPendingTouch();
    disarm();
    m_touchPressPos = pos;

    // Edge-resize is skipped entirely on touch, which is not a new decision -
    // the Android addendum already accepted that a drag on the agenda scrolls
    // rather than resizes, because scrolling is the vastly more common
    // gesture. Blocks are still adjusted from the block dialog's nudge buttons.
    for (const Event* e : m_data->eventsOn(m_date)) {
        if (eventRect(*e).contains(pos)) {
            m_pendingEventId = e->id; // a tap, decided on release...
            // ...unless this press lands on the block whose first tap is
            // still waiting: then it IS the second tap of a double-tap, and
            // that first tap must not open the block after all. A press on a
            // DIFFERENT block also cancels the waiting open - the finger has
            // moved on to something else.
            m_secondTap = (m_tapWaitingId == e->id);
            if (m_singleTap)
                m_singleTap->stop();
            m_tapWaitingId.clear();
            if (!m_secondTap)
                armBlockHold(); // ...or a half-second HOLD that lifts it
            return;
        }
    }
    const int touchedSlot = slotAt(pos);
    if (touchedSlot < 0)
        return;
    const int startMin =
        plan::kDayStartMinutes + touchedSlot * plan::kSlotMinutes;
    if (!m_data->isFree(m_date, startMin, startMin + plan::kSlotMinutes))
        return;
    m_pendingSlot = touchedSlot;
    m_pressWasArmed = (wasArmed == touchedSlot);
    armLongPress();
}

bool AgendaWidget::touchMoved(const QPoint& pos, const QPoint& globalPos)
{
    // After the hold this movement IS the drag, not a scroll - and the page's
    // scroller has already been released, so nothing else is claiming it.
    if (m_touchLifted) {
        moveGhostTo(pos);
        if (!m_resolvesOwnDrops)
            emit blockDragMoved(m_dragEventId, globalPos, m_dragGrabPx);
        else
            setDropPreview(dropTargetAt(pos, m_dragEventId, m_dragGrabPx));
        autoScrollTo(pos);
        return true;
    }
    // Past the platform's drag threshold this is a scroll, not a tap.
    if ((m_pendingSlot >= 0 || !m_pendingEventId.isEmpty())
        && (pos - m_touchPressPos).manhattanLength()
               >= QApplication::startDragDistance()) {
        cancelPendingTouch();
        disarm(); // a scroll means the finger was never aiming at that slot
    }
    return false;
}

bool AgendaWidget::touchReleased(const QPoint& pos, const QPoint& globalPos)
{
    // ---- a finger-carried block, put down ----------------------------------
    if (m_touchLifted) {
        dropLiftedBlock(pos, globalPos);
        return true;
    }

    // A finger that lifts at the end of a SCROLL is not a tap, even when none
    // of its movement reached this widget - the scroller may have eaten it.
    if ((!m_pendingEventId.isEmpty() || m_pendingSlot >= 0)
        && pageIsScrolling()) {
        cancelPendingTouch();
        disarm();
        return true;
    }

    // ---- a stationary TAP on a block ---------------------------------------
    // Movement already cleared the pending id, so reaching here with one
    // still set means the finger stayed put.
    if (!m_pendingEventId.isEmpty()) {
        const QString id     = m_pendingEventId;
        const bool    second = m_secondTap;
        cancelPendingTouch();
        m_secondTap = false;
        if (second) {
            emit eventMenuRequested(id, globalPos);
            return true;
        }
        // The first tap of what may yet be a double-tap. It opens the block
        // only once ~0.3 s pass with no second tap - the owner's choice over
        // making a single tap merely select.
        m_tapWaitingId = id;
        if (!m_singleTap) {
            m_singleTap = new QTimer(this);
            m_singleTap->setSingleShot(true);
            m_singleTap->setInterval(300);
            connect(m_singleTap, &QTimer::timeout, this, [this]() {
                const QString waiting = m_tapWaitingId;
                m_tapWaitingId.clear();
                if (!waiting.isEmpty())
                    emit eventClicked(waiting);
            });
        }
        m_singleTap->start();
        return true;
    }

    // ---- a tap on a FREE SLOT ----------------------------------------------
    // An empty slot takes TWO taps, and the decision is made on release so
    // that a flick which merely started here scrolls instead of planning:
    //   first tap  -> arm it, and say so ("+ plan - tap again")
    //   second tap -> open the planner
    // A long press still short-circuits both (armLongPress); two gestures,
    // one door. While a block is being MOVED (31.2.0, §M.8) one tap is the
    // whole gesture: the block is already chosen, so there is nothing for an
    // arming tap to disambiguate.
    if (m_pendingSlot >= 0) {
        const int  slot      = m_pendingSlot;
        const bool secondTap = m_pressWasArmed;
        cancelPendingTouch();
        if (!m_pickingForId.isEmpty() || secondTap) {
            disarm();
            emit emptySlotClicked(slot);
        } else {
            m_armedSlot = slot;
            update();
        }
        return true;
    }
    return false;
}

void AgendaWidget::touchCancelled()
{
    cancelPendingTouch();
    disarm();
    if (m_touchLifted)
        endTouchLift(); // a lift the system took away is put back, not dropped
    m_twoFingerActive = false;
}

bool AgendaWidget::pageIsScrolling() const
{
    for (QWidget* w = parentWidget(); w; w = w->parentWidget()) {
        auto* area = qobject_cast<QScrollArea*>(w);
        if (!area)
            continue;
        QWidget* target = area->viewport();
        if (!QScroller::hasScroller(target))
            return false; // a page with no scroller never scrolls under a finger
        const QScroller::State state = QScroller::scroller(target)->state();
        return state == QScroller::Dragging || state == QScroller::Scrolling;
    }
    return false;
}

// ---- touch gestures on a BLOCK (owner spec, 2026-09-15; §M.8a) -------------

AgendaWidget::~AgendaWidget()
{
    // The one exit a lift cannot talk its way out of. Without this, a widget
    // destroyed mid-drag - a sync pull rebuilding the page, the app closing -
    // would leave the page's scroller released for good.
    suspendPageScrolling(false);
    // The picture is the WINDOW's child, so nobody else knows it belongs to
    // this widget. If this widget is its own window (a test), Qt deletes it
    // as an ordinary child and it must not be deleted twice.
    if (m_ghost && m_ghost->parent() != this)
        delete m_ghost.data();
}

void AgendaWidget::armBlockHold()
{
    if (!m_blockHold) {
        m_blockHold = new QTimer(this);
        m_blockHold->setSingleShot(true);
        // HALF a second (owner, 2026-09-15). The first cut used the owner's
        // "after 1 second", and on the phone that was "a bit too long". 500ms
        // is what holding a task to reorder it already uses, so every hold in
        // the app now feels the same under the thumb.
        m_blockHold->setInterval(500);
        connect(m_blockHold, &QTimer::timeout, this, &AgendaWidget::liftBlock);
    }
    m_blockHold->start();
}

void AgendaWidget::liftBlock()
{
    // A hold that "fires" while the page is panning or coasting was never a
    // hold: the finger was scrolling, and the movement that would have
    // cancelled the timer went to the scroller instead of here.
    if (pageIsScrolling()) {
        cancelPendingTouch();
        return;
    }
    const Event* e = m_data->eventById(m_pendingEventId);
    if (!e || !m_blockDragEnabled) {
        // Nothing to carry (it vanished under the finger), or a host that
        // does not move blocks - the Compare dialog. The press ends here.
        cancelPendingTouch();
        return;
    }
    const QRect r = eventRect(*e);

    // The picture FIRST, while the block is still drawn at full strength:
    // once m_dragging is set, paint fades the block where it was.
    const QPixmap picture = grab(r);

    m_dragEventId     = e->id;
    m_dragGrabPx      = m_touchPressPos.y() - r.top();
    m_touchGrabOffset = m_touchPressPos - r.topLeft();
    m_dragging        = true;   // paint fades the block in place
    m_touchLifted     = true;
    m_pendingEventId.clear();   // the release no longer means "tap"
    m_secondTap       = false;
    suspendPageScrolling(true); // the finger moves the BLOCK now, not the page

    QWidget* host = window();
    if (!m_ghost) {
        m_ghost = new QLabel(host);
        m_ghost->setObjectName(QStringLiteral("dragGhost"));
        // Never a target: the finger's events must keep reaching this widget
        // even while the picture sits right under it.
        m_ghost->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto* fade = new QGraphicsOpacityEffect(m_ghost);
        fade->setOpacity(0.8);
        m_ghost->setGraphicsEffect(fade);
    }
    m_ghost->setPixmap(picture);
    m_ghost->resize(r.size());
    moveGhostTo(m_touchPressPos);
    m_ghost->show();
    m_ghost->raise();
    update();
}

void AgendaWidget::moveGhostTo(const QPoint& localPos)
{
    if (!m_ghost)
        return;
    // The point where the block was picked up stays under the finger, so the
    // picture does not jump its corner to the fingertip.
    m_ghost->move(mapTo(m_ghost->parentWidget(), localPos - m_touchGrabOffset));
}

void AgendaWidget::dropLiftedBlock(const QPoint& localPos,
                                   const QPoint& globalPos)
{
    const QString id     = m_dragEventId;
    const int     grabPx = m_dragGrabPx;
    if (!m_resolvesOwnDrops) {
        // A week column: the view decides which day this landed on. Told
        // BEFORE the lift ends, so it can still ask isTouchLifted() and know
        // that a refusal has to be said aloud rather than tooltipped.
        emit blockDragFinished(id, globalPos, grabPx);
        endTouchLift();
        return;
    }
    const DropTarget t = dropTargetAt(localPos, id, grabPx);
    endTouchLift();
    if (t.unchanged)
        return; // set back down where it was: nothing happened, nothing said
    if (!t.why.isEmpty()) {
        emit touchDropRefused(t.why);
        return;
    }
    if (!t.swapWithId.isEmpty())
        emit eventSwapRequested(id, t.swapWithId);
    else if (t.startMin >= 0)
        emit eventMoveRequested(id, m_date, t.startMin);
}

void AgendaWidget::endTouchLift()
{
    suspendPageScrolling(false); // ALWAYS, and first - see the header
    if (m_ghost)
        m_ghost->hide();
    const bool wasLifted = m_touchLifted;
    m_touchLifted = false;
    if (wasLifted) {
        m_dragEventId.clear();
        m_dragging = false;
        clearDropPreview();
        update(); // the faded block comes back to full strength
    }
}

void AgendaWidget::suspendPageScrolling(bool suspend)
{
    if (suspend == m_scrollSuspended)
        return;
    QScrollArea* area = nullptr;
    for (QWidget* w = parentWidget(); w && !area; w = w->parentWidget())
        area = qobject_cast<QScrollArea*>(w);
    if (!area) {
        m_scrollSuspended = false; // nothing was taken, nothing to give back
        return;
    }
    QWidget* target = area->viewport();
    if (suspend) {
        // Only a page that HAS a scroller gives one up. Releasing on a page
        // that never grabbed would, on the way back, GRAB a gesture that page
        // never had - a desktop page suddenly panning under a mouse.
        if (!QScroller::hasScroller(target))
            return;
        QScroller::scroller(target)->stop(); // no fling coasting under the block
        QScroller::ungrabGesture(target);
    } else {
        // Exactly what makeTouchScrollable(QScrollArea*) grabs, so the page
        // comes back with the behaviour it had before.
        QScroller::grabGesture(target, QScroller::TouchGesture);
    }
    m_scrollSuspended = suspend;
}

bool AgendaWidget::event(QEvent* e)
{
    // QScroller announces "I have taken this gesture over to pan" by taking
    // the mouse grab away. For a MOUSE-driven gesture that is the only
    // reliable signal that a press has become a scroll - without cancelling
    // here, the long-press timer would still fire in the middle of a flick
    // and plan a block nobody asked for.
    //
    // A widget that reads FINGERS directly (a phone) is not holding a mouse
    // grab for its gesture, so a grab going away says nothing about it - and
    // lifting a block itself releases the page's scroller, which can take the
    // grab on its way out. Answering that by cancelling ended every lift the
    // instant it began, which is what the owner's half-second hold did on the
    // phone. Scrolls are recognised there by travel, by TouchCancel, and by
    // asking the scroller (pageIsScrolling).
    if (e->type() == QEvent::UngrabMouse) {
        if (!testAttribute(Qt::WA_AcceptTouchEvents)) {
            cancelPendingTouch();
            disarm();
            cancelBlockDrag(); // a carried block whose grab was taken: not dropped
            endTouchLift();
        }
    }

    // ---- the phone reads FINGERS, not Qt's mouse imitation (§M.8a) -------
    // Found on the owner's Galaxy S21, in its own logs (2026-09-15): a quick
    // second tap's imitated mouse press never arrived; a second finger was
    // only ever reported in a LATER update, never in the first touch event;
    // and the half-second hold did not survive. All three lived in the mouse
    // imitation, so on a touchscreen this widget accepts every touch and
    // drives its gestures from the touch events - through the same
    // touchPressed/Moved/Released the mouse-route tests exercise. A desktop
    // never asks for touch events, so it never reaches this block.
    if (testAttribute(Qt::WA_AcceptTouchEvents)) {
        switch (e->type()) {
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd: {
            const auto* touch = static_cast<QTouchEvent*>(e);
            const QList<QEventPoint>& points = touch->points();
            e->accept(); // accepted: this sequence is ours, with no mouse imitation
            if (points.isEmpty())
                return true;

            // A SECOND finger, whenever it arrives, makes this a two-finger
            // tap candidate and ends any one-finger gesture already under way.
            // A block already lifted keeps its drag: the extra finger is noise.
            if (e->type() == QEvent::TouchBegin)
                m_touchPressPos = points.first().position().toPoint();
            if (points.size() >= 2 && !m_touchLifted && !m_twoFingerActive) {
                cancelPendingTouch();
                disarm();
                if (m_singleTap)
                    m_singleTap->stop();
                m_tapWaitingId.clear();
                m_twoFingerActive = true;
                m_twoFingerMoved  = false;
                m_twoFingerAt     = m_touchPressPos; // where the FIRST finger landed
                m_fingerStarts.clear();
                // A finger that was already down started where the touch
                // began; a finger landing now starts wherever it is seen first.
                for (const QEventPoint& p : points)
                    if (p.state() != QEventPoint::Pressed)
                        m_fingerStarts.insert(p.id(), m_touchPressPos);
            }
            if (m_twoFingerActive) {
                for (const QEventPoint& p : points) {
                    // A finger the system calls stationary did not move - and
                    // its reported position is not always filled in.
                    if (p.state() == QEventPoint::Stationary)
                        continue;
                    const QPoint at = p.position().toPoint();
                    if (!m_fingerStarts.contains(p.id())) {
                        m_fingerStarts.insert(p.id(), at); // first sight of it
                        continue;
                    }
                    if ((at - m_fingerStarts.value(p.id())).manhattanLength()
                        > 2 * QApplication::startDragDistance())
                        m_twoFingerMoved = true; // a pinch or a two-finger scroll
                }
                if (e->type() == QEvent::TouchEnd) {
                    m_twoFingerActive = false;
                    m_fingerStarts.clear();
                    if (m_twoFingerMoved) {
                        return true;
                    }
                    for (const Event* block : m_data->eventsOn(m_date)) {
                        if (eventRect(*block).contains(m_twoFingerAt)) {
                            emit eventMenuRequested(block->id,
                                                    mapToGlobal(m_twoFingerAt));
                            break;
                        }
                    }
                }
                return true;
            }

            const QEventPoint& p = points.first();
            const QPoint pos    = p.position().toPoint();
            const QPoint global = p.globalPosition().toPoint();
            if (e->type() == QEvent::TouchBegin)
                touchPressed(pos);
            else if (e->type() == QEvent::TouchUpdate)
                touchMoved(pos, global);
            else
                touchReleased(pos, global);
            return true;
        }
        case QEvent::TouchCancel:
            touchCancelled();
            e->accept();
            return true;
        default:
            break;
        }
    }

    return QWidget::event(e);
}

void AgendaWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    // A real second tap arrives as a PRESS and then this DOUBLE-CLICK, and
    // QWidget's default here calls mousePressEvent a second time. On the
    // touch path that second pass wiped the "this is a second tap" state the
    // first pass had just set - so on the device a double-tap never opened
    // the menu, and a quick second tap on a free slot never planned (found
    // 2026-09-15; aRealDoubleTapWithItsDoubleClickEventOpensTheMenu). The
    // press has already done everything a second tap needs, so on a
    // touchscreen a double-click is simply not a new press.
    const bool touch =
        m_forceTouch || event->source() != Qt::MouseEventNotSynthesized;
    if (touch) {
        return;
    }
    QWidget::mouseDoubleClickEvent(event); // a mouse: exactly as before
}

void AgendaWidget::mouseReleaseEvent(QMouseEvent* event)
{
    // A finger's release - the lifted drop, a tap, a double-tap, a tap on a
    // free slot. Shared with the touch route (touchReleased).
    if (touchReleased(event->pos(), event->globalPosition().toPoint()))
        return;

    // ---- a carried block, put down (31.2.0) --------------------------------
    if (!m_dragEventId.isEmpty()) {
        const QString id          = m_dragEventId;
        const bool    wasDragging = m_dragging;
        const int     grabPx      = m_dragGrabPx;
        m_dragEventId.clear();
        m_dragging = false;
        if (!wasDragging) {
            emit eventClicked(id); // the mouse stayed put: it was a click
            return;
        }
        setCursor(Qt::PointingHandCursor);
        QToolTip::hideText();
        if (!m_resolvesOwnDrops) {
            emit blockDragFinished(id, event->globalPosition().toPoint(),
                                   grabPx);
            update();
            return;
        }
        // Asked again at the release point rather than trusting the last
        // preview: the last move event is not always where the button came up.
        const DropTarget t = dropTargetAt(event->pos(), id, grabPx);
        clearDropPreview();
        update();
        if (t.unchanged || !t.why.isEmpty())
            return; // put back, or refused - the tooltip already said why
        if (!t.swapWithId.isEmpty())
            emit eventSwapRequested(id, t.swapWithId);
        else if (t.startMin >= 0)
            emit eventMoveRequested(id, m_date, t.startMin);
        return;
    }

    if (!m_resizing)
        return;
    m_resizing = false;
    m_resizeEdge = Edge::None;

    // REPORT the new span; the page routes it to AppData::resizeEvent, which is
    // the real guard. Only bother if the span actually changed — a plain click
    // on an edge shouldn't fire a no-op mutation and repaint the world.
    const Event* e = m_data->eventById(m_resizeEventId);
    if (e && (e->plannedStartMinutes != m_previewStart
              || e->plannedEndMinutes != m_previewEnd)) {
        emit eventResized(m_resizeEventId, m_previewStart, m_previewEnd);
    } else {
        update(); // snap the preview back to the (unchanged) stored span
    }
}

void AgendaWidget::leaveEvent(QEvent*)
{
    if (m_hoverSlot != -1) {
        m_hoverSlot = -1;
        update();
    }
}

// ---- moving a block by dragging it (31.2.0, move-and-swap addendum) ---------

void AgendaWidget::setBlockDragEnabled(bool on)
{
    if (m_blockDragEnabled == on)
        return;
    m_blockDragEnabled = on;
    if (!on)
        cancelBlockDrag();
}

AgendaWidget::DropTarget AgendaWidget::dropTargetAt(const QPoint& pos,
                                                    const QString& eventId,
                                                    int grabOffsetPx) const
{
    DropTarget t;
    t.eventId = eventId;
    t.date    = m_date;
    const Event* dragged = m_data->eventById(eventId);
    if (!dragged)
        return t; // gone mid-drag (a sync pull landed): nothing to drop

    // The clock the doors will be asked with, so the preview and the door
    // cannot disagree about what has already passed - and the debug panel's
    // fake clock reaches both.
    const QDateTime now = m_tracker ? m_tracker->nowProvider()
                                    : QDateTime::currentDateTime();

    // Over ANOTHER block: a swap (§M.6). The same rectangles clicks are
    // tested against, so the block that lights up is the one a click there
    // would open.
    for (const Event* e : m_data->eventsOn(m_date)) {
        if (e->id != eventId && eventRect(*e).contains(pos)) {
            t.swapWithId = e->id;
            t.why        = m_data->whyCannotSwap(eventId, e->id, now);
            return t;
        }
    }

    // Over space, or over its own old place: a move. minutesAtY snaps to the
    // nearest slot line; subtracting where the block was grabbed keeps it
    // under the pointer instead of jumping its top edge there.
    const int length =
        dragged->plannedEndMinutes - dragged->plannedStartMinutes;
    const int start = qBound(plan::kDayStartMinutes,
                             minutesAtY(pos.y() - grabOffsetPx),
                             plan::kDayEndMinutes - length);
    t.startMin  = start;
    t.unchanged = (m_date == dragged->date
                   && start == dragged->plannedStartMinutes);
    if (!t.unchanged)
        t.why = m_data->whyCannotMove(eventId, m_date, start, now);
    return t;
}

QString AgendaWidget::describeDrop(const DropTarget& t) const
{
    if (!t.why.isEmpty())
        return t.why; // the domain's own sentence, word for word
    if (t.unchanged)
        return {};
    if (const Event* other = m_data->eventById(t.swapWithId))
        return tr("Swap with \"%1\"").arg(m_data->eventLabel(*other));
    const Event* dragged = m_data->eventById(t.eventId);
    if (!dragged || t.startMin < 0)
        return {};
    const int end = t.startMin
                  + (dragged->plannedEndMinutes - dragged->plannedStartMinutes);
    return tr("Move to %1, %2 – %3")
        .arg(t.date.toString(QStringLiteral("ddd d MMM")),
             timeLabel(t.startMin), timeLabel(end));
}

void AgendaWidget::setDropPreview(const DropTarget& target)
{
    m_dropPreview = target;
    update(); // input writes state + update(); paint only reads it
}

void AgendaWidget::clearDropPreview()
{
    if (m_dropPreview.eventId.isEmpty())
        return;
    m_dropPreview = DropTarget();
    update();
}

void AgendaWidget::cancelBlockDrag()
{
    const QString id          = m_dragEventId;
    const bool    wasDragging = m_dragging;
    m_dragEventId.clear();
    m_dragging = false;
    clearDropPreview();
    if (!wasDragging)
        return;
    QToolTip::hideText();
    setCursor(Qt::ArrowCursor);
    if (!m_resolvesOwnDrops)
        emit blockDragCancelled(id);
    update(); // the dimmed block comes back to full strength
}

void AgendaWidget::autoScrollTo(const QPoint& pos)
{
    // Walk up to the page's scroll area rather than being handed one: the day
    // view and the Compare dialog each wrap this widget their own way, and a
    // widget that finds its container needs no page to remember to tell it.
    for (QWidget* w = parentWidget(); w; w = w->parentWidget()) {
        auto* area = qobject_cast<QScrollArea*>(w);
        if (!area)
            continue;
        if (QWidget* content = area->widget()) {
            const QPoint p = mapTo(content, pos);
            area->ensureVisible(p.x(), p.y(), 0, slotHeight());
        }
        return;
    }
}
