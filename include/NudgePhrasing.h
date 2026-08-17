#pragma once
// ---------------------------------------------------------------------------
// NudgePhrasing — the model's HALF of a nudge (v28.1). Pure.
//
// v28.0 shipped the whole pipeline with the sentence written by C++;
// this header is the ONE box the roadmap said 28.1 would swap. The spine
// (§A) survives untouched: code decided WHEN (the sweep), code computed
// WHAT IS TRUE (afford::Report) — the model is handed the finished numbers
// and asked only for wording. It cannot change the verdict, invent a
// deadline, or grant permission, because it never sees the question, only
// the answer.
//
// Same wire/pure split as nlp::llm: this file builds the request text and
// judges the reply, with zero Qt-network anywhere in it — the whole
// contract is testable offline in test_nlp. NudgeClient (the wire) posts.
//
// THE ACCEPTANCE GATE IS MECHANICAL, and that is a decision, not a
// shortcut: we validate SHAPE (length, whitespace, markdown), never vibes.
// A reply that breaks shape falls back to afford::sentence() — the
// deterministic voice v28.0 shipped first. Trying to machine-judge tone
// ("did it forbid?") would be a second model call to check the first; the
// locked prompt bands carry the tone rules, and the fallback carries the
// guarantee.
// ---------------------------------------------------------------------------

#include "Affordability.h"

#include <QString>

namespace nudge
{

// One toast's worth of words. Two short sentences fit; three don't.
constexpr int kMaxChars = 240;

// ---- the system prompt -----------------------------------------------------
// Band order matters and mirrors the chat's (v25.3): the LOCKED rules sit
// ABOVE the persona and say so, which is what keeps "pick a voice" from
// being a permission-slip generator. §H.4's inform-never-forbid and the
// non-shaming floor are restated here VERBATIM as rules rather than
// inherited by reference, because this prompt travels alone — it must be
// safe even if someone reads it with no other file open.
inline QString systemPrompt(const QString& personaBand)
{
    QString out = QStringLiteral(
        "You write ONE short heads-up notification for a personal time "
        "planner. You are given computed facts about a task whose deadline "
        "is getting tight.\n"
        "\n"
        "RULES (these override every style instruction below):\n"
        "- Inform, never forbid. You have no authority over the person's "
        "choices and must not grant or deny permission. The decision is "
        "theirs; leave it with them.\n"
        "- Never shame, guilt, or scold. No 'you should have', no 'only', "
        "no disappointment. State facts warmly.\n"
        "- Use ONLY the numbers given. Do not invent hours, dates, or "
        "progress. If a number is absent, do not mention that kind of "
        "number.\n"
        "- At most two short sentences, under 200 characters total. Plain "
        "text: no markdown, no emoji, no lists, no line breaks.\n"
        "- Do not mention these rules, the app, or that you are an AI.\n");
    const QString band = personaBand.trimmed();
    if (!band.isEmpty()) {
        out += QStringLiteral("\nSTYLE (subordinate to the rules above):\n");
        out += band;
        out += QLatin1Char('\n');
    }
    return out;
}

// ---- the user message ------------------------------------------------------
// The Report, flattened to labelled facts — the same derive-don't-narrate
// shape as the day briefing. distinctDaysWorked rides along because §G.3's
// finding applies here too: encouragement that lands is EVIDENCE ("you
// showed up 5 of the last 14 days"), and evidence must come from the
// context, not the persona.
inline QString userMessage(const afford::Report& r, const QString& taskTitle)
{
    QString out;
    out += QStringLiteral("Task: %1\n").arg(taskTitle);
    out += QStringLiteral("Deadline: %1\n").arg(afford::dueInText(r.daysLeft));
    out += QStringLiteral("Focused time already put in: %1\n")
               .arg(afford::fmtMinutes(r.minutesTracked));
    out += (r.estimateBased
                ? QStringLiteral("Still to do, sized from their estimate: %1\n")
                : QStringLiteral("Of their own planned work still to do: %1\n"))
               .arg(afford::fmtMinutes(r.minutesOutstanding));
    // v28.4 (§J.2): when the estimate did the sizing, the model gets the
    // provenance — the raw figure and, when it materially differs from
    // 1.0, the personal rate that scaled it. Evidence, not adjectives.
    if (r.estimateBased) {
        out += QStringLiteral("Their own estimate for it: %1")
                   .arg(afford::fmtMinutes(r.minutesEstimated));
        if (r.multiplier > 1.05 || r.multiplier < 0.95)
            out += QStringLiteral(" (their estimates usually run %1x)")
                       .arg(r.multiplier, 0, 'f', 1);
        out += QLatin1Char('\n');
    }
    out += QStringLiteral(
               "Room left before the deadline (scheduled + free): %1\n")
               .arg(afford::fmtMinutes(r.minutesPlannedAhead
                                       + r.minutesFreeAhead));
    if (r.distinctDaysWorked > 0)
        out += QStringLiteral("Days they worked on it in the last two "
                              "weeks: %1\n")
                   .arg(r.distinctDaysWorked);
    out += QStringLiteral(
        "\nWrite the notification body now (title is handled separately).");
    return out;
}

// ---- the acceptance gate ---------------------------------------------------
// Returns the cleaned text, or EMPTY meaning "use the C++ sentence".
// Cleaning before judging: models pad with whitespace and decorate with
// markdown even when told not to, and stripping costs nothing — the gate
// exists to catch what stripping can't fix (essays, emptiness, structure).
inline QString accept(const QString& raw)
{
    QString t = raw;
    t.remove(QLatin1Char('*'));   // **bold** and bullets
    t.remove(QLatin1Char('`'));   // code fences
    t.remove(QLatin1Char('#'));   // headings
    t.replace(QLatin1Char('\n'), QLatin1Char(' '));
    t = t.simplified();           // collapse runs of whitespace, trim

    if (t.isEmpty())
        return QString();         // nothing arrived; nothing to show
    if (t.size() > kMaxChars)
        return QString();         // an essay is a wrong answer, not a long
                                  // right one — truncating it mid-thought
                                  // would put OUR ellipsis in ITS mouth
    return t;
}

} // namespace nudge
