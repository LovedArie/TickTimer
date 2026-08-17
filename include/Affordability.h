#pragma once
// ---------------------------------------------------------------------------
// Affordability — "can I afford to rest / go out / put this off?" (v28.0)
//
// Assistant roadmap §H, shipped as a DOMAIN feature first (§H.6): everything
// in this header is computable, visible in the UI, and useful before any
// model ever phrases it. In 28.1 a model gets handed Report's numbers and
// writes a nicer sentence; if it is unreachable, sentence() below is the
// fallback — which, per §A's corollary, ships FIRST.
//
// The one design sentence (§H.1): both "I'm sick, can I rest?" and "I want
// to go out, can I?" call the SAME function, and the difference in answer is
// entirely in the data. The verdict cannot be talked into changing by how
// the question is phrased — which is exactly what makes it trustworthy.
//
// Pure on purpose, same doctrine as missed:: / reschedule:: / coverage:::
// value types in, value types out, the clock is a parameter. Every rule
// threshold lives in a Rule struct so tests pin them and Settings can expose
// them later without an API change.
//
// WHAT "THE WORK" IS — two eras (§H.3, §J.2):
//   v28.0–28.3: THE PROXY. The app had never been told how BIG a task is,
//   so "the work" was THE BLOCKS YOU PLANNED: outstanding = planned −
//   tracked. Honest, but hostage to the user having planned.
//   v28.4: THE ESTIMATE, when one exists (§J.1 gave tasks estimateMinutes;
//   this is where it pays). outstanding = estimate × personalMultiplier −
//   tracked — and personalMultiplier() below is the §J.2 payoff: the app
//   has recorded estimate-vs-actual since v1, so "you run about 1.5×" is
//   division, not opinion. Derived, never stored (§3.5): recomputed from
//   history each time, it cannot go stale and improves as work accumulates.
//   The proxy survives as the FALLBACK for unestimated tasks, and Unknown
//   now means "no estimate AND no blocks" — the estimate answers the
//   question the blocks used to. Admitting ignorance is still a feature;
//   it is just needed less often now.
// ---------------------------------------------------------------------------

#include "AppData.h"
#include "Event.h"
#include "Task.h"

#include <algorithm> // std::sort — the multiplier's median (§J.2)

#include <QDate>
#include <QHash>
#include <QSet>
#include <QVector>
#include <QDateTime>
#include <QString>

namespace afford
{

// ---- the verdict -----------------------------------------------------------
// Four values, not a score. A score invites "how close to Tight am I?", and
// the honest answer is that the inputs are too coarse to rank that finely —
// a proxy that admits ~half-hour precision should not print percentages.
enum class Verdict
{
    NotApplicable, // no deadline / done / archived — the question has no meaning
    Unknown,       // deadline exists, but no blocks were EVER planned (§H.3)
    Comfortable,   // the remaining plan fits the remaining room
    Tight,         // it doesn't — or the deadline is on top of you
};

// ---- the knobs -------------------------------------------------------------
struct Rule
{
    // "Behind" = you skipped your own past blocks: tracked focus under this
    // share of what you scheduled. 0.5 rather than missed::'s per-block
    // threshold because this judges a WHOLE HISTORY, where one bad block
    // should not condemn a task.
    double minFocusShare = 0.5;

    // The deadline bands. nearDays: "behind" starts mattering. lastDays:
    // outstanding work with the deadline this close is Tight regardless.
    int nearDays = 3;
    int lastDays = 1;

    // distinctDaysWorked looks back this far — matches mood's future
    // retention window (§G.2) so the two "recent effort" facts agree.
    int horizonDaysWorked = 14;

    // The day window free time is counted in. 06:00–22:00: the agenda
    // starts at 6, and counting 22:00–24:00 as "free" plans your sleep.
    int dayStartMin = 6 * 60;
    int dayEndMin   = 22 * 60;

    // ---- the multiplier (§J.2, v28.4) ----
    // Fewer finished-and-tracked estimates than this and the multiplier is
    // a flat 1.0 — two data points is an anecdote, not a rate.
    int multiplierMinSamples = 3;
    // The sanity band the final figure is clamped into. Below 0.5 or above
    // 3.0 the history is describing data glitches or a different kind of
    // job, not the user's estimating rate.
    double multiplierFloor   = 0.5;
    double multiplierCeiling = 3.0;

    // ---- manners (§F.3 — load-bearing, not polish) ----
    int quietStartMin = 22 * 60; // no verdicts at 23:40
    int quietEndMin   = 8 * 60;  // ...or before breakfast
    int dailyCap      = 3;       // hard integer, not a vibe
};

// ---- the numbers behind the verdict ----------------------------------------
// Everything the sentence (or, in 28.1, the model) needs. The verdict is
// derived from these and shipped WITH them — §H.4's "informs, never forbids"
// means the user always gets the evidence, not just the conclusion.
struct Report
{
    Verdict verdict = Verdict::NotApplicable;

    int daysLeft           = 0; // negative = overdue
    int minutesTracked     = 0; // focus on this task's blocks, all time
    int minutesPlannedAll  = 0; // every block ever pointed at this task
    int minutesPlannedPast = 0; // the share of that plan already elapsed
    int minutesOutstanding = 0; // plannedAll − tracked, floored at 0
    int minutesFreeAhead   = 0; // unplanned daytime between now and the deadline
    int minutesPlannedAhead= 0; // this task's still-scheduled blocks
    int minutesPromoted    = 0; // v28.9 — estimate ceded to dated pieces
    int distinctDaysWorked = 0; // days with any focus, last horizonDaysWorked
    bool behindOwnPlan     = false;

    // v28.4 (§J.2) — WHERE the outstanding figure came from, shipped with
    // the verdict because §H.4's "informs, never forbids" applies to the
    // basis too: the user (and the model) should see whether they are
    // looking at their estimate or at their planned blocks.
    bool   estimateBased   = false; // outstanding sized from an estimate?
    int    minutesEstimated = 0;    // the raw estimate (own, or piece-sum)
    double multiplier      = 1.0;   // the personal rate applied to it
};

// ---- small shared helpers --------------------------------------------------

inline QString fmtMinutes(int minutes)
{
    if (minutes <= 0)
        return QStringLiteral("0min");
    const int h = minutes / 60, m = minutes % 60;
    if (h == 0)
        return QStringLiteral("%1min").arg(m);
    if (m == 0)
        return QStringLiteral("%1h").arg(h);
    return QStringLiteral("%1h%2").arg(h).arg(m, 2, 10, QLatin1Char('0'));
}

// Quiet hours wrap midnight (22:00 → 08:00), so the test is "outside the
// [end, start) daytime band", not a naive range check.
inline bool inQuietHours(const QDateTime& now, const Rule& rule)
{
    const int m = now.time().hour() * 60 + now.time().minute();
    if (rule.quietStartMin > rule.quietEndMin)               // wraps midnight
        return m >= rule.quietStartMin || m < rule.quietEndMin;
    return m >= rule.quietStartMin && m < rule.quietEndMin;  // same-day band
}

// ---- the multiplier (§J.2, v28.4) ------------------------------------------
// The app's unfair advantage, cashed in: for every FINISHED task that had an
// estimate and real tracked focus, actual ÷ estimate is one sample of how
// the user's guesses relate to reality. The MEDIAN of those samples is the
// personal rate — median and not mean, because one 10× disaster is an
// outlier to survive, not a fact to average in. Derived, never stored
// (§3.5): recomputed from history on demand, so it cannot go stale and it
// improves on its own as finished work accumulates.
inline double personalMultiplier(const AppData& data, const Rule& rule = {})
{
    // Tracked focus per task, one pass over events — the same "reverse
    // questions are scans" answer as everywhere else in the domain.
    QHash<QString, int> trackedByTask;
    for (const Event& e : data.events())
        if (!e.taskId.isEmpty())
            trackedByTask[e.taskId] += int(e.focusSeconds() / 60);

    QVector<double> ratios;
    for (const Task& t : data.tasks()) {
        if (!t.done || !t.hasEstimate())
            continue; // only a FINISHED estimate is evidence of anything
        const int actual = trackedByTask.value(t.id, 0);
        if (actual <= 0)
            continue; // finished off the books: no measured "actual" exists
        ratios.append(double(actual) / double(t.estimateMinutes));
    }

    if (ratios.size() < rule.multiplierMinSamples)
        return 1.0; // an anecdote is not a rate — see the Rule comment

    std::sort(ratios.begin(), ratios.end());
    const int n = ratios.size();
    const double median = (n % 2) ? ratios[n / 2]
                                  : 0.5 * (ratios[n / 2 - 1] + ratios[n / 2]);
    return qBound(rule.multiplierFloor, median, rule.multiplierCeiling);
}

// ---- the query (§H.1) ------------------------------------------------------
// The trailing `multiplier` lets a SWEEP compute personalMultiplier() once
// and hand it to every call; the -1 sentinel means "compute it for me", so
// single-shot callers and every pre-28.4 call site stay exactly as they
// were. Passing knowledge down instead of recomputing it is the same move
// as the verdict cache in TaskListModel, one layer lower.
inline Report affordability(const AppData& data, const Task& task,
                            const QDateTime& now, const Rule& rule = {},
                            double multiplier = -1.0)
{
    Report r;
    if (!task.dueDate.isValid() || task.done || task.archived)
        return r; // NotApplicable — the question has no meaning here

    const QDate today = now.date();
    r.daysLeft = int(today.daysTo(task.dueDate));

    // ---- what "the work" is (§J.2 + §O): the estimate, minus what left --
    // v28.9 PROMOTION. The trigger is the DATE: this very function's first
    // guard makes a dated piece answer for ITSELF (its own verdict, its
    // own nudge, its own needs-a-block line), so the same minutes must
    // stop weighing on the parent — before this rule, a 12h parent with
    // three dated 4h pieces read as 24h of believed work that nobody
    // entered. An UNDATED piece has no verdict of its own, so it still
    // weighs here. One trigger, both directions:
    //
    //   parent sized:   own estimate MINUS the dated pieces' estimates
    //                   (floored at 0 — a fully-decomposed parent has
    //                   ceded all its minutes and falls back to the
    //                   planned-blocks proxy for anything un-pieced)
    //   parent unsized: borrow ONLY the undated pieces (v28.4's rule,
    //                   amended — a dated piece's minutes are its own)
    //
    // subtasksOf() excludes archived pieces, so a ✕'d piece stops
    // weighing on the parent too — unchanged since v28.4.
    int estimate = task.estimateMinutes;
    if (estimate > 0) {
        for (const Task* piece : data.subtasksOf(task.id))
            if (piece->dueDate.isValid()) {
                r.minutesPromoted += piece->estimateMinutes;
                estimate -= piece->estimateMinutes;
            }
        estimate = std::max(0, estimate);
    } else {
        for (const Task* piece : data.subtasksOf(task.id))
            if (!piece->dueDate.isValid())
                estimate += piece->estimateMinutes;
            else
                r.minutesPromoted += piece->estimateMinutes;
    }
    r.minutesEstimated = estimate;
    r.estimateBased    = estimate > 0;
    r.multiplier = (multiplier > 0.0) ? multiplier
                                      : personalMultiplier(data, rule);

    // ---- this task's own blocks: plan, past share, tracked focus ----------
    const int nowMin = now.time().hour() * 60 + now.time().minute();
    const QDate since = today.addDays(-rule.horizonDaysWorked);
    QSet<QDate> workedDays;

    for (const Event& e : data.events()) {
        if (e.taskId != task.id)
            continue;
        const int span = e.plannedEndMinutes - e.plannedStartMinutes;
        r.minutesPlannedAll += span;

        // Elapsed share of the plan. A block straddling `now` counts its
        // elapsed part as past and the rest as ahead — a 2h block you are
        // 30 minutes into has not "failed" its remaining 90.
        if (e.date < today) {
            r.minutesPlannedPast += span;
        } else if (e.date == today) {
            const int past =
                qBound(0, nowMin - e.plannedStartMinutes, span);
            r.minutesPlannedPast  += past;
            r.minutesPlannedAhead += span - past;
        } else {
            r.minutesPlannedAhead += span;
        }

        const int focusMin = int(e.focusSeconds() / 60);
        r.minutesTracked += focusMin;
        if (focusMin > 0 && e.date >= since)
            workedDays.insert(e.date);
    }
    r.distinctDaysWorked = workedDays.size();

    // ---- free daytime between now and the deadline ------------------------
    // Busy minutes are a plain sum because AppData's isFree gate guarantees
    // blocks never overlap — the domain invariant is what makes this loop
    // exact rather than an estimate.
    if (r.daysLeft >= 0) {
        for (QDate d = today; d <= task.dueDate; d = d.addDays(1)) {
            int winStart = rule.dayStartMin;
            int winEnd   = rule.dayEndMin;
            if (d == today)
                winStart = qMax(winStart, nowMin);
            if (d == task.dueDate && task.hasDueTime())
                winEnd = qMin(winEnd, task.dueTime.hour() * 60
                                          + task.dueTime.minute());
            if (winEnd <= winStart)
                continue;
            int busy = 0;
            for (const Event& e : data.events()) {
                if (e.date != d)
                    continue;
                busy += qMax(0, qMin(winEnd, e.plannedEndMinutes)
                                    - qMax(winStart, e.plannedStartMinutes));
            }
            r.minutesFreeAhead += qMax(0, (winEnd - winStart) - busy);
        }
    }

    // ---- the verdict ------------------------------------------------------
    // Unknown has SHRUNK (v28.4): it now needs BOTH sources absent. An
    // estimated task with zero planned blocks was Unknown for four minor
    // versions; today the estimate answers the question the blocks used
    // to, which is the whole point of §J.
    if (!r.estimateBased && r.minutesPlannedAll == 0) {
        r.verdict = Verdict::Unknown; // §H.3 — say so, don't perform
        return r;
    }

    r.minutesOutstanding = r.estimateBased
        ? qMax(0, qRound(r.minutesEstimated * r.multiplier)
                      - r.minutesTracked)
        : qMax(0, r.minutesPlannedAll - r.minutesTracked);
    r.behindOwnPlan =
        r.minutesPlannedPast > 0
        && double(r.minutesTracked)
               < rule.minFocusShare * r.minutesPlannedPast;

    // capacity = the room the outstanding work could still land in: blocks
    // already scheduled for it, plus free daytime it could claim.
    const int capacity = r.minutesPlannedAhead + r.minutesFreeAhead;

    const bool cramped  = r.minutesOutstanding > capacity;
    const bool lastCall = r.daysLeft <= rule.lastDays
                          && r.minutesOutstanding > 0;
    const bool slipping = r.behindOwnPlan && r.daysLeft <= rule.nearDays;

    r.verdict = (cramped || lastCall || slipping) ? Verdict::Tight
                                                  : Verdict::Comfortable;
    return r;
}

// ---- the sentence (§H.4: informs, never forbids) ---------------------------
// The 28.0 voice — plain C++ formatting, evidence first, verdict as framing,
// decision left with the user. In 28.1 a model rewrites this from the same
// Report; this stays as the always-works fallback (§A corollary).
inline QString dueInText(int daysLeft)
{
    if (daysLeft < 0)
        return QStringLiteral("overdue by %1 day%2")
            .arg(-daysLeft)
            .arg(-daysLeft == 1 ? QString() : QStringLiteral("s"));
    if (daysLeft == 0)
        return QStringLiteral("due today");
    if (daysLeft == 1)
        return QStringLiteral("due tomorrow");
    return QStringLiteral("due in %1 days").arg(daysLeft);
}

inline QString sentence(const Report& r)
{
    switch (r.verdict) {
    case Verdict::NotApplicable:
        return QString();
    case Verdict::Unknown:
        // The honest hand (§H.3): effort + deadline, and an explicit "I
        // can't tell" instead of a confident guess.
        return QStringLiteral(
                   "%1 tracked and it's %2 — no estimate and no blocks were "
                   "ever planned for it, so I can't tell how much is left.")
            .arg(fmtMinutes(r.minutesTracked), dueInText(r.daysLeft));
    case Verdict::Comfortable:
        return QStringLiteral(
                   "%1 tracked, %2, and about %3 still open before then. "
                   "Looks comfortable.")
            .arg(fmtMinutes(r.minutesTracked), dueInText(r.daysLeft),
                 fmtMinutes(r.minutesPlannedAhead + r.minutesFreeAhead));
    case Verdict::Tight: {
        // Name the basis (§H.4 applies to provenance too): "of your plan"
        // when the proxy sized it, "of your estimate" when §J.2 did — plus
        // the rate, when the rate is doing real work. A user who sees
        // "at your usual 1.5×" is meeting their own history, which is the
        // §J.2 sentence this feature exists to say.
        QString basis = r.estimateBased ? QStringLiteral("of your estimate")
                                        : QStringLiteral("of your plan");
        if (r.estimateBased
            && (r.multiplier > 1.05 || r.multiplier < 0.95))
            basis += QStringLiteral(" (at your usual %1x)")
                         .arg(r.multiplier, 0, 'f', 1);
        return QStringLiteral(
                   "%1 tracked, %2, about %3 %4 still to do and "
                   "%5 of room left for it. Your call.")
            .arg(fmtMinutes(r.minutesTracked), dueInText(r.daysLeft),
                 fmtMinutes(r.minutesOutstanding), basis,
                 fmtMinutes(r.minutesPlannedAhead + r.minutesFreeAhead));
    }
    }
    return QString();
}

// ---- the manners gate (§F.3) -----------------------------------------------
// Pure: the service feeds it facts, it answers "speak, and say what?" — so
// every rule that decides whether the owner gets interrupted is testable
// without a timer, a toast, or a clock.
//
// The change-of-verdict rule (the one that decides whether this feature
// survives week two): Comfortable→Tight is NEWS; Tight→Tight at 10:00,
// 14:00 and 18:00 is nagging. A secretary mentions that Thursday looks
// tight. They do not mention it four times.
struct Nudge
{
    bool    speak = false;
    QString title;
    QString body;
};

inline Nudge decide(const Report& r, const Task& task, Verdict lastSpoken,
                    int spokenToday, const QDateTime& now,
                    const Rule& rule = {})
{
    Nudge n;
    if (r.verdict != Verdict::Tight)   // only Tight is volunteered (§O.1:
        return n;                      // owner chose secretary-mode, for news)
    if (lastSpoken == Verdict::Tight)  // change-of-verdict: already said so
        return n;
    if (inQuietHours(now, rule))       // not at 23:40
        return n;
    if (spokenToday >= rule.dailyCap)  // hard integer
        return n;
    // §H.5 — the deferral vocabulary is already domain: a dismissed task
    // said "not now, ask me later", and a nudge that ignores that teaches
    // the owner the snooze is fake.
    if (task.dismissedUntil.isValid() && task.dismissedUntil > now)
        return n;

    n.speak = true;
    n.title = QStringLiteral("%1 is looking tight").arg(task.title);
    n.body  = sentence(r);
    return n;
}

} // namespace afford
