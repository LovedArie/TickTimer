#include "DayBriefing.h"

#include "Affordability.h"

#include "AppData.h"
#include "MissedBlocks.h"
#include "Stats.h"

#include <QLocale>
#include <QStringList>

namespace brief
{
namespace
{

// Priority and repeat as the user's own vocabulary, not enum names. The model
// sees the same words the UI shows, which keeps its replies quotable.
QString priorityWord(Task::Priority p)
{
    switch (p) {
    case Task::Priority::Urgent:
        return QStringLiteral("urgent");
    case Task::Priority::Medium:
        return QStringLiteral("normal");
    case Task::Priority::Low:
        return QStringLiteral("low");
    }
    return QStringLiteral("normal");
}

// One task, one line. Deliberately dense: this shape repeats up to 30 times.
QString taskLine(const AppData& data, const Task& t, QDate today,
                 verbs::HandleMap& handles)
{
    // v29.0 (§B.2): the handle FIRST — "[T3] Lab 4 (due …)". The briefing
    // still never contains a UUID; the map that turns T3 back into an id
    // lives with the turn, on this side of the trust boundary.
    QString line = QStringLiteral("- [") + handles.addTask(t.id)
                   + QStringLiteral("] ") + t.title;

    QStringList facts;
    if (t.dueDate.isValid()) {
        if (t.dueDate == today)
            facts << QStringLiteral("due today");
        else
            facts << QStringLiteral("due ") + t.dueDate.toString(Qt::ISODate);
        if (t.dueTime.isValid())
            facts << QStringLiteral("at ") + t.dueTime.toString(QStringLiteral("HH:mm"));
    }
    if (t.priority != Task::Priority::Medium)
        facts << priorityWord(t.priority);
    if (const Category* c = data.categoryById(t.categoryId))
        facts << c->name;

    if (!facts.isEmpty())
        line += QStringLiteral(" (") + facts.join(QStringLiteral(", "))
                + QStringLiteral(")");
    return line;
}

// Append a task section, capped, with the count and the truncation stated.
// Returns nothing: it writes into `out`, which keeps the caller a readable
// list of section names rather than a wall of if-statements.
void appendTaskSection(QString& out, const QString& heading,
                       const QVector<const Task*>& tasks, const AppData& data,
                       QDate today, int cap, verbs::HandleMap& handles)
{
    if (tasks.isEmpty())
        return; // "none" is stated once, globally, rather than five times

    out += QStringLiteral("\n%1 (%2):\n").arg(heading).arg(tasks.size());
    const int shown = qMin(cap, tasks.size());
    for (int i = 0; i < shown; ++i)
        out += taskLine(data, *tasks.at(i), today, handles) + QLatin1Char('\n');
    if (tasks.size() > shown)
        out += QStringLiteral("- (+%1 more)\n").arg(tasks.size() - shown);
}

} // namespace

QString clockLabel(int minutesAfterMidnight)
{
    const int h = minutesAfterMidnight / 60;
    const int m = minutesAfterMidnight % 60;
    return QStringLiteral("%1:%2")
        .arg(h, 2, 10, QLatin1Char('0'))
        .arg(m, 2, 10, QLatin1Char('0'));
}

QString spanLabel(qint64 seconds)
{
    const qint64 minutes = seconds / 60;
    if (minutes < 60)
        return QStringLiteral("%1m").arg(minutes);
    return QStringLiteral("%1h %2m")
        .arg(minutes / 60)
        .arg(minutes % 60, 2, 10, QLatin1Char('0'));
}

QString dayBriefing(const AppData& data, QDate today, const QDateTime& now,
                    const Options& opts,
                    verbs::HandleMap* handlesOut)
{
    // v29.0: handles are ALWAYS printed (one briefing dialect, not two);
    // the caller keeps the map only if it plans to resolve proposals
    // against this turn. A local map serves callers who don't.
    verbs::HandleMap localHandles;
    verbs::HandleMap& handles = handlesOut ? *handlesOut : localHandles;
    handles.clear(); // a reused map must not leak last turn's world

    QString out;

    // ---- when we are -------------------------------------------------------
    // The date is stated in ISO *and* in words: ISO so any date the model
    // echoes back is unambiguous, words so "what's on Thursday?" resolves
    // without it doing calendar arithmetic in its head (which models are
    // famously bad at).
    out += QStringLiteral("TODAY IS %1 (%2). Local time now: %3.\n")
               .arg(QLocale::system().toString(today, QLocale::LongFormat),
                    today.toString(Qt::ISODate),
                    now.time().toString(QStringLiteral("HH:mm")));

    // ---- today's plan ------------------------------------------------------
    const QVector<const Event*> blocks = data.eventsOn(today);
    const int nowMinutes = now.date() == today
                               ? now.time().hour() * 60 + now.time().minute()
                               : 24 * 60; // a past day is entirely "done"

    if (blocks.isEmpty()) {
        out += QStringLiteral("\nPLAN FOR TODAY: nothing planned.\n");
    } else {
        out += QStringLiteral("\nPLAN FOR TODAY (%1 blocks):\n").arg(blocks.size());
        const int shown = qMin(opts.maxBlocks, blocks.size());
        for (int i = 0; i < shown; ++i) {
            const Event& e = *blocks.at(i);

            // WHEN a block sits relative to `now` is the single most useful
            // thing the model can know — it is the difference between "you
            // missed it" and "you have it coming up". Derived, never stored.
            const QString when = e.plannedEndMinutes <= nowMinutes
                                     ? QStringLiteral("past")
                                 : e.plannedStartMinutes <= nowMinutes
                                     ? QStringLiteral("NOW")
                                     : QStringLiteral("upcoming");

            QString line = QStringLiteral("- %1-%2 %3 [%4]")
                               .arg(clockLabel(e.plannedStartMinutes),
                                    clockLabel(e.plannedEndMinutes),
                                    data.eventLabel(e), when);

            // Tracked time per block: the plan-vs-actual comparison is the
            // app's whole reason to exist, so the briefing carries it.
            const stats::Totals t = stats::eventTotals(e);
            if (t.total() > 0)
                line += QStringLiteral(", tracked ") + spanLabel(t.total());

            if (const Category* c = data.categoryById(data.eventCategoryId(e)))
                line += QStringLiteral(", area: ") + c->name;

            out += line + QLatin1Char('\n');
        }
        if (blocks.size() > shown)
            out += QStringLiteral("- (+%1 more)\n").arg(blocks.size() - shown);
    }

    // ---- day status (v28.10 — field report #3) -----------------------------
    // A spine violation of our own making, caught on the first real day of
    // use: §A says models have no clock and can't do arithmetic dependably —
    // and this briefing handed the model raw timestamps and expected it to
    // INFER "your day is over" from them. It guessed wrong. The phase costs
    // one integer compare, so it is now stated as a computed fact. The
    // [past]/[NOW]/[upcoming] tags above remain per-block detail; this line
    // is the day-level verdict the model kept fumbling.
    if (!blocks.isEmpty()) {
        int remaining = 0;
        int lastEnd   = 0;
        for (const Event* e : blocks) {
            if (e->plannedEndMinutes > nowMinutes)
                ++remaining;
            lastEnd = qMax(lastEnd, e->plannedEndMinutes);
        }
        if (remaining == 0)
            out += QStringLiteral("\nDAY STATUS: the planned day is OVER — "
                                  "the last block ended at %1.\n")
                       .arg(clockLabel(lastEnd));
        else
            out += QStringLiteral("\nDAY STATUS: %1 of %2 planned blocks "
                                  "still ahead or running; the last block "
                                  "ends at %3.\n")
                       .arg(remaining)
                       .arg(blocks.size())
                       .arg(clockLabel(lastEnd));
    }

    // ---- what actually happened -------------------------------------------
    // Derived live by stats::, exactly like the Glance panel. Unaccounted is
    // included on purpose: "you planned 2h and tracked none of it" is a fact
    // an assistant should be able to notice without being asked.
    const stats::PeriodSummary day = stats::summarizeDay(data, today, now);
    // v28.10 (field report #4): the model once said "none of that time was
    // logged" and "1h03m focused" in the same breath — it was reconciling
    // the per-block figures against this total ITSELF, which is arithmetic,
    // which is §A's forbidden ground. The relationship between the two
    // numbers is now stated instead of implied.
    out += QStringLiteral("\nTRACKED TODAY (day totals; any per-block "
                          "'tracked' figures above are parts of these, "
                          "not extra): %1 focused, %2 on break")
               .arg(spanLabel(day.totals.focusSeconds),
                    spanLabel(day.totals.breakSeconds));
    if (day.totals.distractedSeconds > 0)
        out += QStringLiteral(", %1 distracted")
                   .arg(spanLabel(day.totals.distractedSeconds));
    if (day.unaccountedSeconds > 0)
        out += QStringLiteral("; %1 of elapsed planned time was never tracked")
                   .arg(spanLabel(day.unaccountedSeconds));
    out += QStringLiteral(".\n");

    // ---- what didn't happen (v26.2) ----------------------------------------
    // Without this section the assistant genuinely cannot tell a executed
    // week from a fictional one — the plan lines above say what was
    // intended and the tracked line says what was recorded, but only this
    // names the gap and says the user hasn't dealt with it yet. Counts
    // stated, list capped, "+N more" visible: the anti-hallucination rules
    // apply to bad news too.
    {
        const QVector<const Event*> unresolved =
            missed::unresolvedIn(data.events(), opts.missedRule, now);
        if (!unresolved.isEmpty()) {
            out += QStringLiteral("\nUNRESOLVED BLOCKS (%1) — planned, "
                                  "didn't happen, no decision yet:\n")
                       .arg(unresolved.size());
            const int shown = qMin(3, int(unresolved.size()));
            for (int i = 0; i < shown; ++i) {
                const Event& e = *unresolved.at(i);
                const missed::Verdict v =
                    missed::judge(e, opts.missedRule, now);
                const QString why =
                    v.reason == missed::Reason::NeverStarted
                        ? QStringLiteral("never started")
                        : QStringLiteral("only %1 of %2 tracked")
                              .arg(spanLabel(v.focusSeconds),
                                   spanLabel(v.plannedSeconds));
                // Block handles are registered HERE and nowhere else
                // (v29.2). The MoveBlock verb accepts only blocks the
                // domain already judges missed, so making the namespace
                // exactly this list keeps two promises at once: the
                // briefing never advertises a target the verb would
                // refuse, and a block named from outside this section
                // resolves to "" and fails safe. The namespace IS the set
                // of legal targets, rather than a superset the validator
                // has to whittle down.
                out += QStringLiteral("- [%1] %2 %3-%4 %5 (%6)\n")
                           .arg(handles.addBlock(e.id),
                                e.date.toString(Qt::ISODate),
                                clockLabel(e.plannedStartMinutes),
                                clockLabel(e.plannedEndMinutes),
                                data.eventLabel(e), why);
            }
            if (unresolved.size() > shown)
                out += QStringLiteral("- (+%1 more)\n")
                           .arg(unresolved.size() - shown);
        }
    }

    // ---- tomorrow (v28.10 — field report #2) -------------------------------
    // "I don't have a plan for tomorrow" was TRUE and still wrong: the
    // blocks existed in AppData and never entered the context. The context
    // is the product — when the assistant is missing a fact, the first fix
    // is the briefing, not the prompt. One day ahead only, on purpose:
    // task DEADLINES already reach `upcomingDays` out, and block-level
    // detail past tomorrow is tokens spent on a plan that will change
    // anyway. No [past]/[NOW] tags and no tracked time here — a future
    // block has neither, and inventing the columns would invite the model
    // to fill them.
    {
        const QDate tomorrow = today.addDays(1);
        const QVector<const Event*> tomorrowBlocks = data.eventsOn(tomorrow);
        if (tomorrowBlocks.isEmpty()) {
            out += QStringLiteral("\nPLAN FOR TOMORROW (%1): nothing "
                                  "planned yet.\n")
                       .arg(tomorrow.toString(Qt::ISODate));
        } else {
            out += QStringLiteral("\nPLAN FOR TOMORROW (%1, %2 blocks):\n")
                       .arg(tomorrow.toString(Qt::ISODate))
                       .arg(tomorrowBlocks.size());
            const int shown = qMin(opts.maxBlocks, tomorrowBlocks.size());
            for (int i = 0; i < shown; ++i) {
                const Event& e = *tomorrowBlocks.at(i);
                QString line = QStringLiteral("- %1-%2 %3")
                                   .arg(clockLabel(e.plannedStartMinutes),
                                        clockLabel(e.plannedEndMinutes),
                                        data.eventLabel(e));
                if (const Category* c =
                        data.categoryById(data.eventCategoryId(e)))
                    line += QStringLiteral(", area: ") + c->name;
                out += line + QLatin1Char('\n');
            }
            if (tomorrowBlocks.size() > shown)
                out += QStringLiteral("- (+%1 more)\n")
                           .arg(tomorrowBlocks.size() - shown);
        }
    }

    // ---- obligations -------------------------------------------------------
    // ONE query, partitioned three ways. upcomingTasks() already filters done
    // and hidden tasks and sorts by urgency, so the partition below preserves
    // that order for free — and the app and the assistant can never disagree
    // about what "upcoming" means, because they ask the same function.
    QVector<const Task*> overdue, dueToday, soon;
    const QDate horizon = today.addDays(opts.upcomingDays);
    for (const Task* t : data.upcomingTasks()) {
        if (t->dueDate < today)
            overdue.append(t);
        else if (t->dueDate == today)
            dueToday.append(t);
        else if (t->dueDate <= horizon)
            soon.append(t);
    }

    appendTaskSection(out, QStringLiteral("OVERDUE"), overdue, data, today,
                      opts.maxTasks, handles);
    appendTaskSection(out, QStringLiteral("DUE TODAY"), dueToday, data, today,
                      opts.maxTasks, handles);
    appendTaskSection(out,
                      QStringLiteral("DUE IN THE NEXT %1 DAYS")
                          .arg(opts.upcomingDays),
                      soon, data, today, opts.maxTasks, handles);

    if (overdue.isEmpty() && dueToday.isEmpty() && soon.isEmpty())
        out += QStringLiteral("\nTASKS: nothing due in the next %1 days.\n")
                   .arg(opts.upcomingDays);

    // ---- needs details (v29.0, §K.1) ---------------------------------------
    // The intake queue, derived: open tasks with no estimate. Keyed on the
    // ESTIMATE alone because that is the fact affordability starves
    // without, while a TBD due date is first-class in this domain (Task.h's
    // opening comment) and does not make a task incomplete. Derived, not
    // stored — the queue cannot drift from the data because it IS the
    // data, asked politely. Iterates tasks() directly rather than
    // upcomingTasks() because undated tasks belong here most of all.
    // Empty → silence, same manners as MOOD: an empty header would invite
    // the model to speculate about it.
    {
        QVector<const Task*> needing;
        for (const Task& t : data.tasks())
            if (!t.done && !t.archived && t.estimateMinutes == 0)
                needing.append(&t);
        if (!needing.isEmpty()) {
            out += QStringLiteral("\nNEEDS DETAILS — captured but never "
                                  "sized (%1):\n")
                       .arg(needing.size());
            const int shown = qMin(opts.maxTasks, needing.size());
            for (int i = 0; i < shown; ++i)
                out += taskLine(data, *needing.at(i), today, handles)
                       + QLatin1Char('\n');
            if (needing.size() > shown)
                out += QStringLiteral("- (+%1 more)\n")
                           .arg(needing.size() - shown);
        }
    }

    // ---- mood (v28.2, §G.2) -----------------------------------------------
    // Coarse values ONLY — the note is the owner's words about their own
    // state and never enters a prompt. Compact: recent days, newest first,
    // and silence when there is no history (an empty MOOD header would
    // invite the model to speculate about it).
    if (opts.includeMood) {
        QStringList moodBits;
        for (int back = 0; back < 14; ++back) {
            const QDate d = today.addDays(-back);
            if (const Mood* m = data.moodOn(d))
                moodBits += QStringLiteral("%1: %2")
                                .arg(back == 0 ? QStringLiteral("today")
                                     : back == 1
                                         ? QStringLiteral("yesterday")
                                         : d.toString(
                                               QStringLiteral("ddd d MMM")),
                                     moodLevelToString(m->level));
        }
        if (!moodBits.isEmpty())
            out += QStringLiteral("\nMOOD (self-reported, last 14 days): ")
                   + moodBits.join(QStringLiteral(" · "))
                   + QLatin1Char('\n');
    }

    // ---- affordability (v28.1) --------------------------------------------
    // The ask-side of §H: the same verdict the toast volunteers, handed to
    // the chat so "I want to go out tonight — can I?" is finally answerable
    // from data. One line per deadlined task; the numbers ride along
    // because §H.4 wants the model to show evidence, not pronounce.
    // NotApplicable is skipped (nothing to say) and Comfortable is stated,
    // not omitted — the model needs "you're fine" to be a fact it can cite,
    // not an absence it must guess about.
    {
        QStringList lines;
        // v28.4: one personal rate for all of today's pressure lines —
        // the same once-per-sweep reasoning as AffordabilityService.
        const double multiplier = afford::personalMultiplier(data);
        for (const Task* t : overdue + dueToday + soon) {
            const afford::Report r =
                afford::affordability(data, *t, now, {}, multiplier);
            switch (r.verdict) {
            case afford::Verdict::NotApplicable:
                break;
            case afford::Verdict::Unknown:
                lines += QStringLiteral(
                             "- %1: UNKNOWN — %2 tracked, no blocks were "
                             "ever planned, so remaining work is unknowable")
                             .arg(t->title,
                                  afford::fmtMinutes(r.minutesTracked));
                break;
            case afford::Verdict::Comfortable:
            case afford::Verdict::Tight:
                lines += QStringLiteral(
                             "- %1: %2 — %3 tracked, %4 of their plan "
                             "still to do, %5 of room before the deadline")
                             .arg(t->title,
                                  r.verdict == afford::Verdict::Tight
                                      ? QStringLiteral("TIGHT")
                                      : QStringLiteral("COMFORTABLE"),
                                  afford::fmtMinutes(r.minutesTracked),
                                  afford::fmtMinutes(r.minutesOutstanding),
                                  afford::fmtMinutes(r.minutesPlannedAhead
                                                     + r.minutesFreeAhead));
                break;
            }
        }
        if (!lines.isEmpty())
            out += QStringLiteral("\nDEADLINE PRESSURE (computed — trust "
                                  "these numbers over any estimate):\n")
                   + lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
    }

    // Undated open tasks: a count, not a list. They have no urgency to sort
    // by, so listing them would spend tokens on noise — but their EXISTENCE
    // matters ("anything I could pick up?").
    int undated = 0;
    for (const Task& t : data.tasks())
        if (!t.done && !data.taskHidden(t) && !t.dueDate.isValid())
            ++undated;
    if (undated > 0)
        out += QStringLiteral("Plus %1 open task(s) with no date set.\n")
                   .arg(undated);

    // ---- horizon -----------------------------------------------------------
    const QVector<const SpecialDay*> special = data.specialDaysSorted(today);
    if (!special.isEmpty()) {
        out += QStringLiteral("\nSPECIAL DAYS:\n");
        const int shown = qMin(3, special.size()); // a horizon, not a calendar
        for (int i = 0; i < shown; ++i) {
            const QDate next = special.at(i)->nextOccurrence(today);
            out += QStringLiteral("- %1: %2 (in %3 days)\n")
                       .arg(special.at(i)->title, next.toString(Qt::ISODate))
                       .arg(today.daysTo(next));
        }
    }

    // ---- vocabulary --------------------------------------------------------
    // The life areas exist so the assistant SPEAKS THE USER'S LANGUAGE: it
    // should say "School" because that is what the user called it, not
    // "studying". Names only — colours and ids are presentation and plumbing.
    QStringList areas;
    for (const Category& c : data.categories())
        if (!c.archived)
            areas << c.name;
    if (!areas.isEmpty())
        out += QStringLiteral("\nLIFE AREAS: %1.\n").arg(areas.join(QStringLiteral(", ")));

    return out;
}

} // namespace brief
