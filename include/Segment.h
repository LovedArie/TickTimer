#pragma once
// ---------------------------------------------------------------------------
// Segment — one continuous stretch of REAL tracked time inside an Event,
// of kind Focus or Break (design-doc §2). The timers produce these; summing
// the Focus segments of an Event gives your true productive time in it.
//
// THE key decision (design-doc §4, "Timestamps"): a Segment stores real
// start/end QDateTimes, NOT a naive "seconds" counter like the throwaway
// prototype did. Why: timestamps are facts; a counter is a running total
// that dies with the process. If the app crashes mid-focus, the start
// timestamp is already on disk, so the elapsed time can be reconstructed
// (see AppData::recoverInterruptedTracking). A counter would just be gone.
// This is the Supplementary Specification's reliability rule made concrete.
// ---------------------------------------------------------------------------

#include <QDateTime>

// WHY `enum class` and not plain `enum`: a plain C enum leaks its names into
// the surrounding scope and converts to int silently — both invite bugs.
// `enum class` (C++11) is scoped (SegmentKind::Focus) and doesn't convert,
// so the compiler catches misuse. Modern C++ default: always `enum class`.
enum class SegmentKind
{
    Focus,
    Break,
    Distracted // off-task time inside a block: procrastination or a disruption.
               // It's REAL time spent, so it's tracked like the others — but
               // it's lost time, which is why the UI paints it in the danger hue.
};

struct Segment
{
    SegmentKind kind = SegmentKind::Focus;
    QDateTime   start;
    QDateTime   end;

    // Derived, never stored (design-doc §3.5): duration is computed from the
    // two facts, so it can never disagree with them.
    qint64 seconds() const
    {
        return start.isValid() && end.isValid() ? start.secsTo(end) : 0;
    }
};
