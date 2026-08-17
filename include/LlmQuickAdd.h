#pragma once
// ---------------------------------------------------------------------------
// LlmQuickAdd — the PURE half of the AI fallback: what we ask the model, and
// how its reply becomes a ParsedTask. No network in this file, on purpose.
//
// The split is the whole lesson of this feature:
//
//     LlmQuickAddClient (wire)      LlmQuickAdd (this file, pure)
//     ------------------------      -------------------------------
//     QNetworkAccessManager         systemPrompt(today)  — the contract
//     headers, POST, timeouts       parseApiReply(bytes) — the mapping
//     "did bytes arrive?"           "what do the bytes MEAN?"
//
// Everything that can be WRONG about an LLM integration — a hallucinated
// field, a date in the wrong format, markdown fences around the JSON, prose
// where JSON should be — lives in the mapping, and the mapping is a pure
// function we can feed forged replies in microsecond tests. The wire stays
// too thin to hide bugs in. (Same doctrine as stats:: and version:: — the
// testable core carries the logic; the shell just moves bytes.)
//
// THE CONTRACT with the model: reply with ONLY a JSON object —
//   { "title": string, "due_date": "YYYY-MM-DD"|null,
//     "priority": "urgent"|"medium"|"low",
//     "repeat": "none"|"daily"|"weekly"|"monthly"|"yearly",
//     "category": string|null }
// — resolving relative dates against the `today` we state in the prompt.
// The mapper is defensive anyway: every missing, misspelled, or garbage
// field degrades to the same defaults the deterministic parser uses. An LLM
// is a remote text generator; trusting its output shape would be a bug.
// ---------------------------------------------------------------------------

#include "LlmProvider.h" // ai::Dialect — which envelope the reply arrives in
#include "QuickAddParser.h"

#include <QByteArray>
#include <QDate>
#include <QString>

namespace nlp::llm
{

// The system prompt: the JSON-only contract plus today's date (so "friday"
// and "tomorrow" resolve server-side to absolutes we can trust verbatim).
QString systemPrompt(const QDate& today);

struct Outcome
{
    bool             ok = false;
    nlp::ParsedTask  task;   // valid only when ok
    QString          error;  // human-readable, shown in the overlay hint
};

// Map a raw response body onto a ParsedTask.
//
// v24: the ENVELOPE unwrap moved out to ai::extractText(dialect, body) —
// "what shape does this vendor wrap its answer in" is the provider layer's
// question, not this file's. Everything after the unwrap (```json fences,
// the JSON parse, the defensive field mapping) is identical for every vendor
// and stays here. The dialect defaults to Anthropic so that call sites and
// tests written before the provider layer keep compiling and keep meaning
// exactly what they meant.
//
// Never throws, never trusts.
Outcome parseApiReply(const QByteArray& body,
                      ai::Dialect dialect = ai::Dialect::Anthropic);

// The field mapping alone (exposed for direct tests): a parsed JSON object
// -> ParsedTask, defaulting every field it can't read.
Outcome fromJsonObject(const class QJsonObject& obj);

} // namespace nlp::llm
