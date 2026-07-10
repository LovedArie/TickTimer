#pragma once

#include "Stats.h"

// ---------------------------------------------------------------------------
// compare — the pure brain of "compare planners", SyncPlan.h's sibling.
//
// The heavy lifting was already done years^Wsessions ago: stats::summarize
// turns any AppData into numbers, and it doesn't care WHOSE AppData it is.
// So comparing two people's days is: summarize mine, summarize theirs,
// subtract. This header holds the subtraction and the one genuine decision
// in the feature — when do we call two people "even"?
//
// Same rule as sync::decide: pure functions of their inputs, no clock, no
// network, no widgets. The CompareDialog is thin glass over these numbers,
// and the test for the feature's logic is a handful of arithmetic checks in
// the Widgets-free domain suite.
// ---------------------------------------------------------------------------

namespace compare
{

// Signed differences, MINE minus THEIRS: positive = I have more of it.
// (Whether "more" is good depends on the row — more focus is winning, more
// distraction is not. The verdict below only ever judges focus.)
struct Delta
{
    qint64 focusSeconds      = 0;
    qint64 breakSeconds      = 0;
    qint64 distractedSeconds = 0;
    qint64 totalSeconds      = 0;
};

inline Delta delta(const stats::Totals& mine, const stats::Totals& theirs)
{
    Delta d;
    d.focusSeconds      = mine.focusSeconds      - theirs.focusSeconds;
    d.breakSeconds      = mine.breakSeconds      - theirs.breakSeconds;
    d.distractedSeconds = mine.distractedSeconds - theirs.distractedSeconds;
    d.totalSeconds      = mine.total()           - theirs.total();
    return d;
}

enum class Verdict { Ahead, Even, Behind };

// The one real decision: a 30-second focus lead is noise, not victory.
// Anything within the tolerance is Even — because this feature exists to
// nudge people ("mom tracked her walk, I should start my study block"),
// not to hand out photo-finish rankings that make one of them feel bad
// over two minutes. 5 minutes chosen as the default: below the size of
// any real tracked block in this app.
inline Verdict focusVerdict(const Delta& d,
                            qint64 toleranceSeconds = 5 * 60)
{
    if (d.focusSeconds > toleranceSeconds)
        return Verdict::Ahead;
    if (d.focusSeconds < -toleranceSeconds)
        return Verdict::Behind;
    return Verdict::Even;
}

} // namespace compare
