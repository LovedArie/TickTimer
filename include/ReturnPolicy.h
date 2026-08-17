#pragma once
// ---------------------------------------------------------------------------
// ReturnPolicy — "when does this come back?", answered once, used twice.
//
// Two features ask that exact question (needs-a-block addendum §C): the
// review panel re-arming ("daily at 06:00") and a dismissed task returning
// ("at 21:00, while I plan tomorrow"). Same question, same answer shape —
// a MODE plus its parameter — so it is ONE value type with ONE pure
// nextReturn() function, not two similar-but-subtly-different
// implementations drifting apart. The second-consumer rule usually fires on
// the second caller; here both callers arrived on day one.
//
// Pure on purpose: nextReturn takes `from` instead of reading the clock, so
// tests can probe every boundary (before the time, after it, midnight
// rollover) without waiting for the wall clock to cooperate — the same seam
// discipline as Event::isLiveAt and TrackerService's nowProvider.
//
// The policy itself is TASTE (this machine's rhythm) and lives in QSettings
// via prefs::; the timestamps it *produces* (Task::dismissedUntil) are FACTS
// and live in data.json. The policy computes; the domain stores the result.
// ---------------------------------------------------------------------------

#include <QDateTime>
#include <QTime>

struct ReturnPolicy
{
    // A small, closed set of ways to answer "when?" -> an enum, as always.
    //   EndOfDay   — midnight tonight (start of the next day).
    //   AtTime     — the next occurrence of a clock time: today if it is
    //                still ahead, otherwise tomorrow.
    //   AfterHours — a plain duration from now.
    enum class Mode { EndOfDay, AtTime, AfterHours };

    Mode  mode  = Mode::EndOfDay;
    QTime time  = QTime(21, 0);   // used by AtTime
    int   hours = 4;              // used by AfterHours

    // The one computation. Boundary convention: a policy time EQUAL to
    // `from` means "already passed, next occurrence please" — a dismissal
    // at exactly 21:00 returns at 21:00 *tomorrow*, not instantly. (An
    // instant return would make the dismissal a no-op at one exact second
    // of the day — a bug report waiting for the person who plans at nine
    // sharp.)
    QDateTime nextReturn(const QDateTime& from) const
    {
        if (!from.isValid())
            return {};

        switch (mode) {
        case Mode::EndOfDay:
            return QDateTime(from.date().addDays(1), QTime(0, 0));

        case Mode::AtTime: {
            // Repair on read, like prefs:: does: an invalid QTime from a
            // hand-edited settings file degrades to the default, never UB.
            const QTime t = time.isValid() ? time : QTime(21, 0);
            const QDateTime today(from.date(), t);
            return (today > from)
                       ? today
                       : QDateTime(from.date().addDays(1), t);
        }

        case Mode::AfterHours:
            return from.addSecs(qint64(qMax(1, hours)) * 3600);
        }
        return {}; // unreachable — no default: the compiler guards the enum
    }
};

// ---- Mode <-> text, for QSettings ------------------------------------------
// Same shape as repeatToString/FromString: unknown or missing text reads as
// the safe default rather than exploding — garbage tolerated on disk, never
// in the program.

inline QString returnModeToString(ReturnPolicy::Mode m)
{
    switch (m) {
    case ReturnPolicy::Mode::AtTime:     return QStringLiteral("atTime");
    case ReturnPolicy::Mode::AfterHours: return QStringLiteral("afterHours");
    case ReturnPolicy::Mode::EndOfDay:   break;
    }
    return QStringLiteral("endOfDay");
}

inline ReturnPolicy::Mode returnModeFromString(const QString& s)
{
    if (s == QLatin1String("atTime"))     return ReturnPolicy::Mode::AtTime;
    if (s == QLatin1String("afterHours")) return ReturnPolicy::Mode::AfterHours;
    return ReturnPolicy::Mode::EndOfDay;  // covers "endOfDay", "", unknowns
}
