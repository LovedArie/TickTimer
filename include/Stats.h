#pragma once
// ---------------------------------------------------------------------------
// Stats — DERIVE, DON'T STORE (design-doc §3.5), as code.
//
// There is deliberately no DaySummary object saved anywhere, no WeeklyTotal
// field kept up to date. Every number any screen shows is computed here,
// on demand, from the raw Segments. A stored summary can drift out of sync
// with the raw data and start lying; a derived one cannot.
//
// WHY free functions in a namespace, not methods on AppData: summarising
// only READS the data — it needs no privileged access and defends no
// invariant. Keeping it out of AppData keeps that class focused on its one
// job (guarding mutations) and keeps these functions trivially testable:
// in, data; out, numbers; no state anywhere.
// ---------------------------------------------------------------------------

#include "AppData.h"

#include <QDate>
#include <QDateTime>
#include <QMap>
#include <QString>
#include <QVector>

namespace stats
{

struct Totals
{
    qint64 focusSeconds = 0;
    qint64 breakSeconds = 0;
    qint64 distractedSeconds = 0;
    // total() is all REAL tracked time in the block — distraction included,
    // because it genuinely happened. (Whether distraction counts as "good"
    // time is a display question, not a bookkeeping one.)
    qint64 total() const { return focusSeconds + breakSeconds + distractedSeconds; }
};

// Totals for a single Event (only committed Segments; the second-by-second
// live extra is the TrackerService's business and is added by the UI).
Totals eventTotals(const Event& event);

struct PeriodSummary
{
    Totals totals;                        // focus vs break over the period
    QMap<QString, qint64> byCategory;     // categoryId -> tracked seconds
    QVector<QPair<QDate, Totals>> byDay;  // one entry per day, in order
    // UNACCOUNTED time (§3.40): planned window that has already ELAPSED
    // with nothing tracked over it — the block sat there and nobody
    // pressed anything. DERIVED, never stored: it is pure arithmetic
    // (elapsed window minus tracked) on facts that already exist; storing
    // it would record a conclusion that goes stale every minute.
    qint64 unaccountedSeconds = 0;
};

// One function covers day, week, and month — the period is just [from..to].
// The date dimension comes from each Event's date, exactly as the domain
// model intended ("a day is just the Events whose date matches").
PeriodSummary summarize(const AppData& data, QDate from, QDate to,
                        const QDateTime& now = QDateTime::currentDateTime());

// Convenience wrappers so call sites read like the requirement they serve.
// `now` is a PARAMETER (defaulting to the wall clock) because unaccounted
// time depends on it — the nowProvider lesson (§3.38) generalised to the
// pure layer: pass time in, and every boundary becomes testable at a fixed
// moment instead of at whatever o'clock the suite happens to run.
PeriodSummary summarizeDay(const AppData& data, QDate day,
                           const QDateTime& now = QDateTime::currentDateTime());
// `firstDay` decides WHICH seven days "the week of anyDayInWeek" means —
// Monday-first by default (the app's historical behaviour, and what every
// existing caller and test still gets for free from the default argument).
// A preference parameterised into the pure layer, never read from inside
// it: summarizeWeek stays a function of its arguments.
PeriodSummary summarizeWeek(const AppData& data, QDate anyDayInWeek,
                            Qt::DayOfWeek firstDay = Qt::Monday,
                            const QDateTime& now = QDateTime::currentDateTime());
PeriodSummary summarizeMonth(const AppData& data, QDate anyDayInMonth,
                             const QDateTime& now = QDateTime::currentDateTime());

// The one week-snap formula: the first `firstDay` at or before `anyDay`.
// Shared by the week agenda, the week review, the "Week of …" label, and
// the month grid's columns — four call sites hand-rolling
// `(dayOfWeek - first + 7) % 7` is four chances for one to drift.
inline QDate weekStart(QDate anyDay, Qt::DayOfWeek firstDay)
{
    const int offset = (anyDay.dayOfWeek() - int(firstDay) + 7) % 7;
    return anyDay.addDays(-offset);
}

// "2h 05m" / "12m" / "45s" — one formatter used everywhere, so durations
// look identical on every screen.
QString formatSeconds(qint64 seconds);

} // namespace stats
