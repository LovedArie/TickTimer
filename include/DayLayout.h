#pragma once
// ---------------------------------------------------------------------------
// DayLayout — "how many things are happening at once, and where does each one
// draw?" (v31.3)
//
// WHY THIS EXISTS. Until v31.3 the answer to the first question was always
// "one": AppData refused any event that overlapped another, so the agenda
// gave every block the full width and hit-testing could stop at the first
// rectangle containing the point. Allowing up to plan::kMaxConcurrentBlocks
// turns both of those into real questions, and this header is where they are
// answered — once, purely, for every caller.
//
// WHY PURE, AND WHY HEADER-ONLY. The same reason as Recurrence.h and Merge.h:
// the judgement a feature turns on should be a function that a table of
// microsecond tests can pin. test_domain links no widgets at all, so a layout
// decision living here is checkable without constructing a window, and the day
// the agenda paints something wrong the failing test names the arithmetic
// rather than the pixels.
//
// WHY NOTHING IS STORED. A column index is not a fact about an event; it is a
// fact about the NEIGHBOURS of that event. Store it and every move, resize or
// delete would have to rewrite the column of everything nearby, on every
// device, and two devices could disagree about a value both could have
// derived. Deriving it costs a sort of a handful of events per day and cannot
// go stale.
// ---------------------------------------------------------------------------

#include "Event.h" // plan::kMaxConcurrentBlocks, and the Event itself

#include <QHash>
#include <QObject> // QObject::tr — the refusal SENTENCE lives here too
#include <QPair>
#include <QString>
#include <QVector>

#include <algorithm>

namespace daylay
{

// ---------------------------------------------------------------------------
// Merged busy ranges: overlapping and touching spans collapsed into one.
//
// Extracted because two callers need it and one of them was WRONG without it.
// Affordability summed block durations as a plain total and said so in a
// comment — "because the isFree gate in AppData guarantees blocks never
// overlap". The moment blocks may overlap, a plain sum double-counts and
// under-reports the free time before a deadline: no error, just a worse
// answer. Reschedule.h had already worked this out for its gap walk and kept
// its own inline copy; one copy now serves both, because two would drift the
// first time somebody fixed only one.
//
// Touching spans (10:00-11:00 and 11:00-12:00) merge into one, which is right
// for "how much of this window is spoken for" — there is no free minute
// between them.
// ---------------------------------------------------------------------------
inline QVector<QPair<int, int>> mergeSpans(QVector<QPair<int, int>> spans)
{
    if (spans.isEmpty())
        return spans;
    std::sort(spans.begin(), spans.end());

    QVector<QPair<int, int>> out;
    out.append(spans.first());
    for (int i = 1; i < spans.size(); ++i) {
        if (spans[i].first <= out.last().second)
            out.last().second = qMax(out.last().second, spans[i].second);
        else
            out.append(spans[i]);
    }
    return out;
}

// Total minutes covered, counting a minute ONCE however many spans cover it.
inline int busyMinutes(const QVector<QPair<int, int>>& spans)
{
    int total = 0;
    for (const auto& s : mergeSpans(spans))
        total += s.second - s.first;
    return total;
}

// ---------------------------------------------------------------------------
// THE CAPACITY QUESTION: at its busiest, how many blocks cover a single
// instant inside [startMin, endMin)?
//
// NOT the same number as "how many events overlap this range", and the
// difference is the whole reason this is a function rather than a count.
// Blocks at 09:00-10:00, 09:30-10:30 and 10:00-11:00 all overlap the range
// 09:00-11:00, but no single instant carries more than two of them. Counting
// events would refuse a legal fourth; counting concurrency accepts it.
//
// A sweep over boundaries rather than minute by minute: only an instant where
// something STARTS can raise the count, so those are the only instants worth
// asking about. The start of the candidate range is one of them.
// ---------------------------------------------------------------------------
inline int peakConcurrency(const QVector<const Event*>& dayEvents,
                           int startMin, int endMin,
                           const QString& ignoreEventId = QString())
{
    QVector<int> instants;
    instants.append(startMin);
    for (const Event* e : dayEvents) {
        if (!e || e->id == ignoreEventId)
            continue;
        if (e->plannedStartMinutes > startMin
            && e->plannedStartMinutes < endMin)
            instants.append(e->plannedStartMinutes);
    }

    int peak = 0;
    for (int t : instants) {
        int n = 1; // the candidate range itself covers this instant
        for (const Event* e : dayEvents) {
            if (!e || e->id == ignoreEventId)
                continue;
            if (e->plannedStartMinutes <= t && e->plannedEndMinutes > t)
                ++n;
        }
        peak = qMax(peak, n);
    }
    return peak;
}

// ---------------------------------------------------------------------------
// "What, if anything, is wrong with putting a block here?" — empty when it is
// fine, a sentence a person can act on when it is not.
//
// The shape and the reason are those of recur::problemWith (schedules
// addendum S.11a): the domain returned a bare false, the screen said nothing,
// and the edit vanished with no message. Whoever refuses should be the one who
// explains, and one definition means no screen can be kinder than the
// aggregate root.
// ---------------------------------------------------------------------------
inline QString problemWith(const QVector<const Event*>& dayEvents,
                           int startMin, int endMin,
                           const QString& ignoreEventId = QString())
{
    if (startMin >= endMin)
        return QObject::tr("A block has to end after it starts.");
    if (startMin < plan::kDayStartMinutes || endMin > plan::kDayEndMinutes)
        return QObject::tr("The day runs from %1 to midnight.")
            .arg(plan::kDayStartMinutes / 60);
    if (peakConcurrency(dayEvents, startMin, endMin, ignoreEventId)
        > plan::kMaxConcurrentBlocks)
        return QObject::tr("That time is already full. Three blocks at once "
                           "is the most the day can show.");
    return {};
}

// The same question as a yes/no, for doors that only need to refuse. The
// sentence above is the one a screen should quote.
inline bool hasRoom(const QVector<const Event*>& dayEvents,
                    int startMin, int endMin,
                    const QString& ignoreEventId = QString())
{
    return problemWith(dayEvents, startMin, endMin, ignoreEventId).isEmpty();
}

// ---------------------------------------------------------------------------
// WHERE EACH BLOCK DRAWS. One column index per event, plus how many columns
// its neighbourhood is divided into.
//
// The packing calendars use, in two passes:
//
//   1. Walk the events in start order and give each the LOWEST column that no
//      event it overlaps is already using. That alone decides the columns.
//   2. Walk again in CLUSTERS — maximal runs of events chained by overlap —
//      and give every event in a cluster the same columnCount: the widest the
//      cluster ever gets.
//
// Pass 2 is what stops the layout twitching. Without it a block would be full
// width where it happens to be alone and half width a moment later, so a
// column boundary would appear and vanish down the length of one afternoon.
// One width per cluster means the dividing lines run straight.
// ---------------------------------------------------------------------------
struct Slotting
{
    int column      = 0; // 0-based, left to right
    int columnCount = 1; // how many columns this cluster is split into
};

inline QHash<QString, Slotting> columns(const QVector<const Event*>& dayEvents)
{
    QVector<const Event*> sorted;
    for (const Event* e : dayEvents)
        if (e)
            sorted.append(e);
    std::sort(sorted.begin(), sorted.end(),
              [](const Event* a, const Event* b) {
                  if (a->plannedStartMinutes != b->plannedStartMinutes)
                      return a->plannedStartMinutes < b->plannedStartMinutes;
                  // A stable tie-break, so two blocks starting together do not
                  // swap columns between one repaint and the next.
                  if (a->plannedEndMinutes != b->plannedEndMinutes)
                      return a->plannedEndMinutes < b->plannedEndMinutes;
                  return a->id < b->id;
              });

    QHash<QString, Slotting> out;

    QVector<int> columnEnd; // when the block now in column i finishes
    QVector<QVector<const Event*>> clusters;
    int clusterEnd = -1;

    // ---- pass 1: the lowest free column ---------------------------------
    for (const Event* e : sorted) {
        if (e->plannedStartMinutes >= clusterEnd) {
            // Named type, not {}: QVector::append is overloaded and a
            // brace-enclosed empty list is ambiguous between the
            // element and an initializer_list of elements.
            clusters.append(QVector<const Event*>()); // new cluster
            columnEnd.clear();
        }
        clusterEnd = qMax(clusterEnd, e->plannedEndMinutes);
        clusters.last().append(e);

        int col = -1;
        for (int i = 0; i < columnEnd.size(); ++i) {
            if (columnEnd[i] <= e->plannedStartMinutes) {
                col = i;
                break;
            }
        }
        if (col < 0) {
            columnEnd.append(e->plannedEndMinutes);
            col = columnEnd.size() - 1;
        } else {
            columnEnd[col] = e->plannedEndMinutes;
        }
        out.insert(e->id, Slotting{col, 1});
    }

    // ---- pass 2: one width for the whole cluster ------------------------
    for (const QVector<const Event*>& cluster : clusters) {
        int widest = 1;
        for (const Event* e : cluster)
            widest = qMax(widest, out.value(e->id).column + 1);
        for (const Event* e : cluster)
            out[e->id].columnCount = widest;
    }

    return out;
}

} // namespace daylay
