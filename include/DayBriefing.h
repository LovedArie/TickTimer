#pragma once
// ---------------------------------------------------------------------------
// brief:: — WHAT the assistant knows about you, as one block of plain text.
//
// The v25 chat panel's whole value comes from this file. A chat box wired to
// an LLM and nothing else is a worse version of the vendor's own website; a
// chat box that already knows what you planned, what you tracked, and what is
// overdue is the first version of the secretary the roadmap is aiming at.
//
// WHY A STRING, AND NOT "GIVE THE MODEL THE DATA"
// A model consumes tokens, not objects. Every fact it can use has to be
// serialised into its prompt sooner or later, so the only real question is
// WHERE that serialisation lives — scattered through the UI as it builds a
// message, or in one pure function with a test. This is that function. It
// takes the whole aggregate root and a clock, and returns text. No network,
// no widgets, no QSettings.
//
// WHY IT LIVES WITH THE DOMAIN (and is tested in test_domain, not test_nlp)
// It reads AppData and calls stats::, so it belongs to the layer that owns
// those. Note the consequence: the project now has TWO pure AI-adjacent
// layers in two different suites — ai::/chat:: are Core-only (they know
// nothing about tasks), brief:: is domain-only (it knows nothing about
// vendors). Neither can drag the other's dependencies in. That split is the
// point, and it is enforced by the build, not by a comment.
//
// THE ANTI-HALLUCINATION RULES, encoded rather than hoped for:
//   1. Empty sections say so out loud ("nothing planned"). A section that
//      simply vanishes invites the model to fill the silence.
//   2. Counts are stated ("3 blocks", "+4 more"), so a truncated list is
//      visibly truncated and the model can say "you have more".
//   3. No ids ever leave this file. The assistant is read-only in v25 — it
//      has nothing to point an id at — and ids in a prompt are pure noise
//      that a model will eventually try to be clever with.
//   4. Notes and descriptions are EXCLUDED. They are the most private and
//      the most token-hungry text in the file, and the block-level facts
//      answer the questions people actually ask. (Revisit when a feature
//      needs it, not before.)
//
// COST: this block is re-sent with EVERY turn of a conversation, so its
// length is billed per message, not per session. That is why the caps in
// Options are small and why the format is terse rather than pretty.
// ---------------------------------------------------------------------------

#include "MissedBlocks.h" // missed::Rule — which blocks the briefing flags

#include <QDate>
#include <QDateTime>
#include <QString>

#include "AssistantVerbs.h" // HandleMap — the briefing prints what it registers
#include "Reschedule.h"     // the "can move to:" search under missed blocks

class AppData;

namespace brief
{

// The caps. Values, not constants, so a test can shrink them to two lines and
// assert the "+N more" behaviour without building a fake week.
struct Options
{
    int maxBlocks   = 12; // today's agenda is rarely longer; the cap is a bound
    int maxTasks    = 10; // per section (overdue / today / soon)
    int upcomingDays = 7; // how far "soon" reaches

    // v28.2p2 — §E.4's boundary, made mechanical: mood never leaves the
    // machine, so the MOOD line is OPT-IN and the caller may set it true
    // only when it has proven every seat the text can reach is local.
    // Default false means a new call site is private by accident instead
    // of leaky by accident — the failure mode chooses the safe side.
    bool includeMood = false;

    // v26.2: which blocks count as unresolved. A FIELD, not a prefs:: read —
    // brief:: is domain-only and touches no QSettings (the layering the
    // header celebrates, enforced by the build). The caller passes
    // prefs::missedRule() so the assistant and the card judge by the same
    // bar; a caller that doesn't gets the shipped defaults.
    missed::Rule missedRule;

    // v29.2: the search policy behind "can move to:" under each unresolved
    // block. A field for the same reason missedRule is one — brief:: is
    // domain-only and reads no QSettings — and it MUST match what
    // verbs::World carries, or the model would be offered placements the
    // verb then refuses as "not one of the options".
    //
    // deadline is left unset here too: it belongs to each block's own task
    // and is derived per block while printing.
    reschedule::Context rescheduleCtx;

    // How many slots to offer per unresolved block. Three is enough to feel
    // like a choice and few enough that the section stays readable — the
    // same number, and the same reasoning, as reschedule::maxFreeSlotOptions.
    // Zero suppresses the lines entirely, which is what a caller that cannot
    // act on them (no write verb in its role) should pass.
    int maxMoveOptions = 3;
};

// The context block. `now` is a parameter, defaulted to the wall clock — the
// nowProvider doctrine (§3.38) applied once more: pass time in, and "is this
// block finished or still to come?" becomes testable at a fixed moment.
// v29.0: `handlesOut` — pass a map to keep this turn's [T1] → id
// resolution (the write boundary resolves proposals against it, §B.2);
// nullptr callers get identical text and no obligation. LAST and
// defaulted so every pre-v29 call site compiles unchanged.
QString dayBriefing(const AppData& data, QDate today,
                    const QDateTime& now = QDateTime::currentDateTime(),
                    const Options& opts = Options{},
                    verbs::HandleMap* handlesOut = nullptr);

// Small shared formatters, exposed because the chat page's context chip shows
// the same numbers and two spellings of "1h 05m" is one too many.
QString clockLabel(int minutesAfterMidnight); // 570 -> "09:30"
QString spanLabel(qint64 seconds);            // 3900 -> "1h 05m"

} // namespace brief
