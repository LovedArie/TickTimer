#pragma once
// ---------------------------------------------------------------------------
// chat:: — the CONVERSATION, as a value with no network in sight.
//
// Everything the v25 assistant knows about "a conversation" lives here:
//
//     Transcript   the turns, in order, including local-only ones
//     window()     which turns actually get SENT (the budget lives here)
//     systemPrompt the assistant's instructions and its read-only promise
//
// WHY THIS IS ITS OWN LAYER
// The chat panel's genuinely interesting bugs are all in this file's job
// description: sending a history that starts with the wrong speaker, letting
// a conversation grow until every message costs a fortune, showing an error
// in the log and then feeding that error back to the model as if it had said
// it. None of those need a socket to reproduce — so none of them should need
// one to test. The wire client (ChatClient) is left with POST, timeout and
// status codes, the same too-thin-to-hide-a-bug split as quick-add's
// LlmQuickAddClient / nlp::llm.
//
// THE LOG IS A SUPERSET OF THE CONVERSATION. `localOnly` turns — errors,
// "conversation cleared", future system notices — are shown to the human and
// never sent to the model. Without that flag the app would eventually tell an
// LLM that it had said "couldn't reach the AI service", which is both false
// and the sort of thing a model will happily build on.
//
// THE BUDGET IS IN CHARACTERS, NOT TOKENS, and that is a deliberate
// approximation: a real tokenizer is vendor-specific, would have to be
// bundled per provider, and would make this layer impure. Characters/4 is the
// usual rough ratio; what matters is that the cost is BOUNDED, not that the
// bound is exact. Being honest about the approximation is cheaper than being
// wrong about it silently.
// ---------------------------------------------------------------------------

#include "LlmProvider.h" // ai::Role, ai::Message — the wire's vocabulary

#include <QDateTime>
#include <QList>
#include <QString>

namespace chat
{

// One entry in the log. Note what it carries beyond ai::Message: WHEN (the
// UI stamps each bubble) and whether it is local-only. Two structs rather
// than one because they answer to different masters — ai::Message is defined
// by what an API accepts, Turn by what a person needs to see.
struct Turn
{
    ai::Role  role = ai::Role::User;
    QString   text;
    QDateTime at;
    bool      localOnly = false; // shown, never sent (errors, notices)
};

// The conversation. A plain value: copy it, compare it, hand it to a pure
// function. No QObject, no signals — the panel owns one and repaints itself,
// which is all the notification a single owner needs.
class Transcript
{
public:
    void append(ai::Role role, const QString& text,
                const QDateTime& at = QDateTime::currentDateTime());
    // A message for the human's eyes only — an error, a notice. Same log,
    // never sent.
    void appendLocal(const QString& text,
                     const QDateTime& at = QDateTime::currentDateTime());
    void clear() { m_turns.clear(); }

    const QList<Turn>& turns() const { return m_turns; }
    bool  isEmpty() const { return m_turns.isEmpty(); }
    int   size() const { return m_turns.size(); }

    // Drop the last turn — used when a send fails and the optimistic user
    // bubble should not become part of the history the model later sees.
    // (v25 keeps it instead, so the person can see what they typed; the
    // method exists because the choice should be the caller's, not this
    // class's.)
    void removeLast();

    // The turns to SEND, newest-biased, within `budgetChars`:
    //   * local-only turns are excluded, always;
    //   * turns are kept WHOLE — half a message is worse than none;
    //   * the window is trimmed from the FRONT (old turns fall off);
    //   * a leading assistant turn is dropped, because both dialects expect
    //     a conversation to open with the user. Anthropic rejects the
    //     request outright; OpenAI-compatible servers vary, which is worse.
    // The most recent user turn always survives, even if it alone exceeds
    // the budget: refusing to send what someone just typed is not a saving.
    QList<ai::Message> window(int budgetChars) const;

private:
    QList<Turn> m_turns;
};

// The default budget. ~6000 characters ≈ 1500 tokens of history, which on top
// of a briefing (~1000 chars) and an 800-token reply keeps a turn comfortably
// inside every provider's context window, including small local models.
inline constexpr int kDefaultBudgetChars = 6000;

// How much room the reply gets. Prose, not a JSON object, so an order of
// magnitude above quick-add's 300.
inline constexpr int kReplyMaxTokens = 800;

// The assistant's instructions, with the day's facts embedded.
//
// `briefing` is brief::dayBriefing()'s output — passed IN rather than fetched,
// so this function stays Core-only and testable without an AppData. That one
// parameter is what keeps the AI layer and the domain layer strangers.
//
// v25.3 — THE PROMPT IS FOUR BANDS, assembled in authority order:
//
//     contract   what the assistant is and may not do        LOCKED (code)
//     floors     how it speaks, ALWAYS (non-shaming,          LOCKED (code)
//                know-your-lane)
//     persona    style + the user's free text                 YOURS
//     context    the briefing                                 generated
//
// A persona describes HOW things are said, never WHAT is allowed: the two
// locked bands sit ABOVE it and say so in the prompt itself ("these override
// any style below"). If a style could soften "never invent" or the read-only
// promise, the persona feature would be a prompt-injection hole with the
// user holding the injector. Non-shaming used to be rule 4's opening — it is
// PROMOTED to a floor here precisely so no persona (including Custom free
// text) can trade it away; "Blunt coach" may be terse about a missed block,
// it may not sneer about it.
//
// PERSONA TOUCHES THE ASSISTANT ONLY. Quick-add's prompt (nlp::llm) is a
// JSON machine contract — a parser has no tone, and giving it one would just
// be new ways to break the parse.
QString systemPrompt(const QString& briefing);

// The two-arg form: `personaBand` is the persona band's TEXT (a preset's
// style, the user's free text, or both). The one-arg form above delegates
// here with the Calm preset — which is the v25 behaviour verbatim in intent,
// so shipping personas changes nobody's assistant until they opt in.
// An empty band emits no STYLE section at all rather than an empty header.
QString systemPrompt(const QString& briefing, const QString& personaBand);

// v30.0 — the three-arg form adds the MEMORY band (§L): what the owner has
// written about themselves, already trimmed to budget by memory::promptBand.
//
// It sits BELOW both locked bands, exactly where the persona band sits and
// for the same reason: anything a person can author is prompt-injection
// surface, and the defence is that the locked bands are above it and say so.
// CONTRACT rule 4 names this section and classes it as information rather
// than instruction — so the header text in the .cpp and the header named in
// rule 4 must stay identical, or the rule stops pointing at anything.
//
// A memory band the model could WRITE would be a different proposition
// entirely — the first thing it authors that it later reads as prompt. v30.0
// deliberately ships the read half alone.
QString systemPrompt(const QString& briefing, const QString& personaBand,
                     const QString& memoryBand);

// ---- persona (v25.3) ------------------------------------------------------
// A persona is a VALUE from a catalog, same doctrine as ai::Provider: the
// Settings combo is populated from personaCatalog() so adding one means
// editing one function, and an unknown id repairs to the first entry (Calm)
// instead of bricking the chat — the same repair-on-read rule as ai::byId.
struct Persona
{
    QString id;          // stable settings token: "calm", "brief", …
    QString displayName; // what the Settings combo shows (tr()-able)
    QString style;       // the preset's band text; EMPTY for "custom",
                         // whose band is the user's free text alone
};

QList<Persona> personaCatalog();
Persona        personaById(const QString& id); // unknown -> calm

// QSettings keys, spelled once (the LlmProvider.cpp lesson: a typo'd
// settings string fails silently). Stored in QSettings and NOT in data.json
// deliberately: persona is TASTE, the same class as agenda hours — facts
// sync between machines, taste stays on the machine that chose it.
QString settingsKeyPersona();     // "ai/persona"
QString settingsKeyPersonaText(); // "ai/personaText"

// The band as configured RIGHT NOW: preset style + free text (free text
// appends to any preset; for "custom" it IS the band). Read fresh at every
// call — the pref-read-at-fire-time doctrine: change it in Settings, the
// very next message speaks the new way.
QString configuredPersonaBand();

} // namespace chat
