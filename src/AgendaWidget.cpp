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
// kSlotHeight and kTopPad now live in AgendaWidget as public statics, so the
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
}

void AgendaWidget::setDate(QDate date)
{
    if (m_date == date)
        return;
    m_date = date;
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
    return kTopPad + (slotIndex - firstShownSlot()) * kSlotHeight;
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
    return {width, kTopPad + shownSlotCount() * kSlotHeight + 12};
}

QRect AgendaWidget::spanRect(int startMin, int endMin) const
{
    // Minutes -> pixels: the single place this conversion exists, so painting,
    // hit-testing, AND the live resize preview all agree on where things are.
    const int startSlot =
        (startMin - plan::kDayStartMinutes) / plan::kSlotMinutes;
    const int slotCount = (endMin - startMin) / plan::kSlotMinutes;
    return QRect(m_gutter, slotTop(startSlot) + 2,
                 width() - m_gutter - 4, slotCount * kSlotHeight - 4);
}

QRect AgendaWidget::eventRect(const Event& e) const
{
    return spanRect(e.plannedStartMinutes, e.plannedEndMinutes);
}

int AgendaWidget::minutesAtY(int y) const
{
    // Snap to the NEAREST slot line so dragging feels magnetic to the grid.
    // Slot indices stay DOMAIN indices (0 == 6 AM) — the window only shifts
    // which of them y == kTopPad lands on.
    int slot = firstShownSlot() + qRound(double(y - kTopPad) / kSlotHeight);
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
    const int slot = firstShownSlot() + (pos.y() - kTopPad) / kSlotHeight;
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

    // 2) Hover hint on a free slot — the invitation to plan.
    if (m_hoverSlot >= 0) {
        const QRect r(m_gutter, slotTop(m_hoverSlot) + 2,
                      width() - m_gutter - 4, kSlotHeight - 4);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(47, 126, 110, 18));
        p.drawRoundedRect(r, kRadius, kRadius);
        p.setPen(theme::focus());
        p.setFont(small);
        p.drawText(r.adjusted(12, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("+ plan"));
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
        // While dragging an edge, THIS block is drawn at its live preview span
        // (the fixed edge stays, the grabbed edge follows the mouse) so resize
        // feedback is immediate — paint still only READS state, never writes.
        const bool isResizing = (m_resizing && e->id == m_resizeEventId);
        const QRect  rect  = isResizing ? spanRect(m_previewStart, m_previewEnd)
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
        if (e->repeat != Task::Repeat::None)
            timeLine += QStringLiteral(" · \u27F3 %1")
                            .arg(repeatLabel(e->repeat));
        p.setFont(small);
        p.setPen(theme::inkSoft());
        const QFontMetrics smallFm(small);
        // Line order (owner request): name, TIME, then the description —
        // the time is the block's fixed anatomy, so it sits in the same
        // place on every block; the free-text detail reads below it.
        // Consequence, accepted: a 2-slot (1h) block only has room for the
        // time line, so the description shows on blocks of 3+ slots.
        if (rect.height() >= 2 * kSlotHeight - 6)
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
        const bool tallEnough = rect.height() >= 3 * kSlotHeight - 6;
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
}

void AgendaWidget::mousePressEvent(QMouseEvent* event)
{
    // Touch and mouse part company here. See the block comment in the header:
    // on a touchscreen a press is not yet a decision, because the identical
    // gesture starts a scroll.
    const bool touch = event->source() != Qt::MouseEventNotSynthesized;
    if (touch) {
        cancelPendingTouch();
        m_touchPressPos = event->pos();

        // Edge-resize is skipped entirely on touch, which is not a new
        // decision — the Android addendum already accepted that a drag on the
        // agenda scrolls rather than resizes, because scrolling is the vastly
        // more common gesture. Blocks are still adjusted from the block
        // dialog's nudge buttons.
        for (const Event* e : m_data->eventsOn(m_date)) {
            if (eventRect(*e).contains(event->pos())) {
                m_pendingEventId = e->id; // decided on release
                return;
            }
        }
        const int touchedSlot = slotAt(event->pos());
        if (touchedSlot < 0)
            return;
        const int startMin =
            plan::kDayStartMinutes + touchedSlot * plan::kSlotMinutes;
        if (!m_data->isFree(m_date, startMin, startMin + plan::kSlotMinutes))
            return;
        m_pendingSlot = touchedSlot;
        if (!m_longPress) {
            m_longPress = new QTimer(this);
            m_longPress->setSingleShot(true);
            // 450ms: long enough that a scroll has begun moving by then,
            // short enough not to feel like a hang. Qt's own tap-and-hold is
            // 700ms, which for a gesture people repeat all day reads as slow.
            m_longPress->setInterval(450);
            connect(m_longPress, &QTimer::timeout, this, [this]() {
                const int slot = m_pendingSlot;
                cancelPendingTouch();
                if (slot >= 0)
                    emit emptySlotClicked(slot);
            });
        }
        m_longPress->start();
        return;
    }

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
    // Past the platform's drag threshold this is a scroll, not a tap.
    if ((m_pendingSlot >= 0 || !m_pendingEventId.isEmpty())
        && (event->pos() - m_touchPressPos).manhattanLength()
               >= QApplication::startDragDistance()) {
        cancelPendingTouch();
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
    m_pendingSlot = -1;
    m_pendingEventId.clear();
}

bool AgendaWidget::event(QEvent* e)
{
    // QScroller announces "I have taken this gesture over to pan" by taking
    // the mouse grab away. That is the only reliable signal that a press has
    // become a scroll — without cancelling here, the long-press timer would
    // still fire in the middle of a flick and plan a block nobody asked for.
    if (e->type() == QEvent::UngrabMouse)
        cancelPendingTouch();

    return QWidget::event(e);
}

void AgendaWidget::mouseReleaseEvent(QMouseEvent* event)
{
    // A stationary tap on an existing block opens it. Movement already
    // cleared the pending id in mouseMoveEvent, so reaching here with one
    // still set means the finger stayed put.
    if (!m_pendingEventId.isEmpty()) {
        const QString id = m_pendingEventId;
        cancelPendingTouch();
        emit eventClicked(id);
        return;
    }
    // A short press on an empty slot is NOT a plan: the long press is the
    // gesture, and letting go early is how you say "no, I was scrolling".
    if (m_pendingSlot >= 0) {
        cancelPendingTouch();
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
