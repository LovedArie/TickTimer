#pragma once
// ---------------------------------------------------------------------------
// Intake — the interview's brain, all of it C++ (v29.1, §K).
//
// The model's ONLY job in intake is turning messy prose into two numbers.
// Everything else — which task deserves a question (K.6), what the
// question says (K.2), the guess that stops the blank page (K.3), and the
// crisp-answer parse that makes the whole flow work offline — lives here,
// pure and tested, because none of it needs a model and everything that
// doesn't need a model must not depend on one (the v28 doctrine, again).
//
// Division of labour, exactly:
//   C++  : triage, question, guess, "2h"-shaped answers, the Proposal
//   model: "probably two evenings but Marc never does his part" → 480
//   owner: the tap (always — every write still crosses the Slice 1 card)
// ---------------------------------------------------------------------------

#include "LlmProvider.h" // ai::Dialect — the envelope the reply arrives in

#include <QByteArray>
#include <QDate>
#include <QString>

class AppData;
struct Task;

namespace intake
{

// §K.3 — the guess that replaces the blank page. Median of TRACKED
// actuals over FINISHED tasks in the same category: the same
// events-scan-by-taskId the personal rate uses, but the median of the
// actuals themselves rather than a ratio. Median, not mean, for the same
// reason the rate chose it — one 10× disaster is an anecdote, not your
// speed. `basis` says where the number came from, because a guess that
// can't show its work reads as the model inventing (§G's evidence rule).
struct Guess
{
    int     minutes = 0; // 0 = no guess (not enough history)
    QString basis;       // "3 finished School tasks ran ~2h each"

    bool exists() const { return minutes > 0; }
};

// Floor of TWO finished samples — deliberately lower than the rate's bar.
// The rate scales estimates SILENTLY, so it demands real evidence; a
// guess is spoken out loud and confirmed by the owner, so weak evidence
// is acceptable when labelled ("2 finished ... tasks"). The label is the
// license.
Guess historyGuess(const AppData& data, const Task& task);

// §K.6 — triage. "Buy milk" needs no interview. Worth asking about:
// NOT currently dismissed (ask once — the door the owner's Skip presses),
// and at least one substance signal: a real deadline, urgent priority, or
// a category whose history says tasks there absorb hours (guess ≥ 1h).
// The three signals are §K.6's own list, verbatim.
bool worthInterviewing(const AppData& data, const Task& task,
                       const QDateTime& now);

// §K.2 — one open question, composed HERE. Deterministic, free, and it
// still asks when every AI seat is down: the interview's availability is
// a C++ property, not a provider property. Folds the guess in when one
// exists ("… similar School tasks ran ~2h — sound right?").
QString questionFor(const AppData& data, const Task& task);

// The crisp-answer parser: "2h", "90m", "1h 30m", "90 min", "1h30", or a
// bare number (minutes). Returns 0 when the text is not a plain duration
// — which is precisely the signal to hand the sentence to the model.
// Deliberately narrow: this parser must never guess, because a wrong
// cheap parse would silently pre-empt the model that would have read the
// sentence correctly.
int parseDurationAnswer(const QString& text);

// ---------------------------------------------------------------------------
// intake::llm — the PURE half of the extraction, quick-add's split one
// level up (§K.2's own phrasing). No network here: the prompt is the
// contract, parseReply is the mapping, and everything that can be WRONG
// about the model's output — prose instead of JSON, fences, a date in the
// wrong shape, an invented field — dies in a pure function fed forged
// bytes in microsecond tests. The wire (IntakeClient) stays too thin to
// hide bugs in.
//
// THE CONTRACT: reply with ONLY a JSON object —
//   { "estimate_minutes": int|null, "due_date": "YYYY-MM-DD"|null }
// null for anything the answer didn't actually indicate. The prompt
// carries the task, today's date (relative dates resolve model-side to
// absolutes we trust verbatim — quick-add's rule), and the guess if one
// exists, so "yeah that sounds right" extracts to the guess's number.
// ---------------------------------------------------------------------------
namespace llm
{

// Pure over VALUES — the task, its area's display name, the guess, the
// date — so this unit earns its seat in the Core-only nlp suite (the
// v24/v25 doctrine in CMakeLists: pure AI layers must not drag AppData
// in; the caller resolves the one name it needs). The suite structure
// caught the lazier AppData-taking draft — purity rules with teeth.
QString systemPrompt(const Task& task, const QString& areaName,
                     const Guess& guess, QDate today);

struct Extraction
{
    bool    ok = false;
    int     estimateMinutes = 0; // 0 = not indicated
    QDate   dueDate;             // invalid = not indicated
    QString error;               // human-readable, spoken in the chat
};

Extraction parseReply(const QByteArray& body,
                      ai::Dialect dialect = ai::Dialect::Anthropic);

} // namespace llm

} // namespace intake
