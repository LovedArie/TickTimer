#pragma once
// ---------------------------------------------------------------------------
// ai:: — WHO we ask, and in WHAT dialect. The vendor-neutral layer under the
// AI features (v24).
//
// Before this file, LlmQuickAddClient had one vendor welded into it: the host
// api.anthropic.com, the path /v1/messages, the header x-api-key, the model
// claude-haiku-4-5, and the reply shape content[0].text. Five hard-coded
// facts scattered across one function. A "provider" is those facts named and
// gathered into a value:
//
//     base URL  +  dialect  +  model  +  key
//
// Two DIALECTS cover nearly the whole market, because the industry converged
// on OpenAI's request shape:
//
//   Anthropic  POST {base}/v1/messages
//              x-api-key: KEY, anthropic-version: 2023-06-01
//              body   { model, max_tokens, system, messages:[user] }
//              reply  { content: [ {type:"text", text:…} ] }
//
//   OpenAI     POST {base}/v1/chat/completions
//              Authorization: Bearer KEY
//              body   { model, max_tokens, messages:[system, user] }
//              reply  { choices: [ {message:{content:…}} ] }
//
// The OpenAI dialect is not just OpenAI: Groq, Together, OpenRouter, LM
// Studio and Ollama all speak it. One enum value buys most of the world,
// including a local model with no key and no bill.
//
// WHY A VALUE + FREE FUNCTIONS, NOT A CLASS HIERARCHY
// The dialect varies in DATA — which path, which header name, which JSON
// shape — not in stateful behaviour. There is nothing for an object to hold
// between calls. A `switch` over a closed enum says that in four lines;
// `AnthropicProvider : ProviderStrategy` would need a factory, ownership,
// virtual dispatch and a fake-in-tests, to express the same four lines.
// The closed enum also gets compiler help: add a Dialect and -Wswitch names
// every site that must learn about it. And staying free functions keeps this
// layer PURE — no QNetworkAccessManager anywhere in this file, so every
// request we would ever send can be asserted in a microsecond test without a
// socket. Same doctrine as nlp::llm, stats:: and version::.
//
// Revisit when: a dialect needs to hold state across calls (a streaming
// cursor, a multi-turn tool-call transcript). That is a real object; this
// is not.
//
// KEYS AND MODELS ARE PER-PROVIDER, deliberately. A single global "ai/model"
// would send claude-haiku-4-5 to OpenAI the moment you switched vendors, and
// a single global key would make switching back mean re-pasting. The keys are
// "ai/key/<id>" and "ai/model/<id>"; migrateLegacySettings() carries the v21
// single-vendor keys across exactly once.
// ---------------------------------------------------------------------------

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>
#include <QDateTime>
#include <QHash>
#include <QStringList>
#include <QUrl>

namespace ai
{

// The wire protocol. Closed set: adding a member is a deliberate act that the
// compiler will make you finish (every switch in LlmProvider.cpp is total).
enum class Dialect
{
    Anthropic,
    OpenAi,
};

QString  dialectToString(Dialect d);
Dialect  dialectFromString(const QString& s); // unknown -> OpenAi (the common one)

// One provider, fully resolved and ready to send to. A plain value: copyable,
// comparable by inspection, no lifetime questions, safe to pass by const ref
// into pure functions.
struct Provider
{
    QString    id;           // stable settings token: "anthropic", "ollama", …
    QString    displayName;  // what the Settings combo shows
    QUrl       baseUrl;      // scheme + host + any path prefix (Groq has one!)
    Dialect    dialect = Dialect::Anthropic;
    QString    defaultModel; // used when the user has not overridden
    QString    model;        // RESOLVED: the override, or defaultModel
    QByteArray envVar;       // developer fallback for the key; empty = none
    bool       needsKey = true; // false for a local server (Ollama)
    QString    keyHint;      // placeholder text, e.g. "sk-ant-…"

    // v25.2: ask this provider not to run a hidden reasoning pass. OPT-IN PER
    // CATALOG ENTRY, deliberately not per dialect: "think" is an Ollama
    // extension, and OpenAI proper REJECTS unknown body fields with a 400 —
    // a blanket flag would break every cloud seat to maybe-help the local
    // one. Ollama ignores what it doesn't honour, so there the flag is free.
    // Custom endpoints never get it: silently adding fields to a server the
    // user defined is how "works with curl, fails in the app" bugs are born.
    bool       sendThinkFlag = false;
};

// The built-ins, in menu order. A function rather than a global: no static
// initialisation order to reason about, and it can consult translations.
QList<Provider> catalog();

// Look one up. An UNKNOWN id degrades to the first catalog entry rather than
// failing — a hand-edited settings file must never brick the feature. Same
// repair-on-read rule prefs:: applies to the agenda hours.
Provider byId(const QString& id);

// ---- QSettings-resolved (still no network) --------------------------------
// These read preferences but send nothing, so tests can drive them offline.

// "ai/keys/<id>" and "ai/models/<id>" — PLURAL on purpose, see the .cpp:
// QSettings forbids a name being both a value and a group, and v21 already
// used "ai/model" as a value.
QString settingsKeyForKey(const QString& providerId);
QString settingsKeyForModel(const QString& providerId);

// The provider as configured RIGHT NOW: chosen id, model override, and for
// "custom" the user's own base URL and dialect. Read fresh at every call —
// the pref-read-at-fire-time doctrine (change it in Settings, the very next
// request uses it; nothing is cached at construction).
Provider configured();

// Resolve one catalog id through the same QSettings overlays configured()
// applies (custom address/dialect, per-provider model) — the shared path
// that makes a custom endpoint behave identically in any route position.
Provider resolved(const QString& id);

// ---- per-role routing (v26) -----------------------------------------------
// role → ORDERED seat list, §E of the assistant roadmap. Named Feature and
// not Role ONLY because ai::Role was already the message speaker
// (User/Assistant) — when the docs say "role", this enum is what they mean.
// The two that exist today; Nudge and CheckIn join when their call sites
// are born (and CheckIn arrives with its documented rule: local, always —
// a privacy boundary, not a preference).
enum class Feature
{
    QuickAdd, // has a deterministic-parser fallback of its own — single
              // seat for now, fall-through deferred until it earns one
    Chat,     // the ⚠-bubble role: when every seat fails there is nothing
              // else to give, which is why routing ships HERE first
    // Nudge (v28.1) is deliberately ABSENT: it reads ai::configured()
    // directly, exactly as quick-add does, because its fallback seat is
    // afford::sentence() — a route table for a feature that cannot fail
    // is configuration surface with no failure to configure away. It
    // joins this enum the day someone wants nudges on a DIFFERENT seat
    // than the primary (the §E.2 per-role-primary trigger), not before.
};

QString     settingsKeyRoute(Feature role);      // "ai/route/<role>"
// The stored list, repaired (unknown ids dropped, order kept); a missing or
// empty route DERIVES [configured().id] at read time — v25 setups route to
// their one seat with no migration write. See the .cpp for why derivation
// beat copy-once.
QStringList configuredRouteIds(Feature role);
QList<Provider> routeFor(Feature role);          // the ids, resolved

// §E.5 — the ONE user-visible name per seat, for every surface that shows
// one. Cosmetic, never a key: routing, breaker state and settings all keep
// using ids, so a rename can never re-route, re-test or destroy anything.
QString settingsKeySeatName(const QString& providerId);
QString seatName(const QString& providerId);

// §E forcing hook: seats listed in TICKTIMER_AI_DOWN (comma-separated ids)
// are treated as unreachable before any socket opens.
bool forcedDown(const QString& providerId);

// The failure taxonomy the fall-through rule keys on. UNREACHABLE (nothing
// answered: refused, no route, timeout) is the ONLY class that moves to the
// next seat. REFUSED (something answered and said no: 401/403/404/429/5xx,
// or an unparseable reply) never falls through — a wrong key is a config
// bug, and masking it behind a quieter seat costs an evening of wondering
// why the answers got dumber.
enum class Failure
{
    Unreachable,
    Refused,
};

// The circuit breaker: a VALUE with the clock passed in, so tests never
// sleep. One instance per process via breaker(); 20 s of cooldown after an
// unreachable verdict, cleared by the next success.
class Breaker
{
public:
    void noteUnreachable(const QString& providerId, const QDateTime& when);
    void noteOk(const QString& providerId);
    bool coolingDown(const QString& providerId, const QDateTime& now,
                     int coolDownSecs = kCoolDownSecs) const;

    static constexpr int kCoolDownSecs = 20;

private:
    QHash<QString, QDateTime> m_lastUnreachable;
};
Breaker& breaker();

// Route minus the seats currently cooling down. Empty means "everything
// failed within the last cooldown" — callers fail fast with a named error
// rather than re-proving the outage one timeout at a time.
QStringList planRoute(const QStringList& ids, const Breaker& b,
                      const QDateTime& now);

// The key for a provider: its QSettings entry first (the user-facing path),
// then its environment variable (the developer path). Empty when neither is
// set — which is CORRECT and expected for a local provider.
QString configuredKey(const Provider& p);

// v28.2p2 — the §E.4 boundary's mechanical half: a seat is LOCAL when its
// host is loopback. Deliberately conservative — a LAN Ollama on
// 192.168.x.x counts as remote, because "my other machine" is still a
// wire the mood crossed. The rule errs toward privacy exactly the way
// includeMood's default errs toward silence.
inline bool isLocal(const Provider& p)
{
    const QString host = p.baseUrl.host().toLower();
    return host == QLatin1String("localhost")
           || host == QLatin1String("127.0.0.1")
           || host == QLatin1String("::1");
}

// Just the ENVIRONMENT half of the lookup (v25.1). Exists so the Settings
// dialog's Test button can compose "the field on screen first, env second"
// without re-implementing the env read — the unsaved field replaces the
// QSettings half, but the developer path must keep working mid-edit.
QString envKey(const Provider& p);

// One-time, idempotent: v21's single-vendor "ai/anthropicApiKey" and global
// "ai/model" become "ai/key/anthropic" and "ai/model/anthropic". Copies only
// into an empty destination, then removes the legacy entry so exactly one
// copy of a credential exists. Called once from main(); safe to call twice.
void migrateLegacySettings();

// ---- the pure request/response shaping ------------------------------------

// The endpoint path for a dialect ("/v1/messages", "/v1/chat/completions").
QString requestPath(Dialect d);

// base + path, JOINED — deliberately NOT QUrl::resolved(). resolved() applies
// RFC 3986 reference resolution, where an absolute path reference REPLACES
// the base's path: "https://api.groq.com/openai".resolved("/v1/chat/…") is
// "https://api.groq.com/v1/chat/…", silently dropping the "/openai" prefix
// and 404-ing. Correct per spec, wrong for our intent. We concatenate.
QUrl endpoint(const Provider& p);

// ---- conversation ---------------------------------------------------------
// v25. One turn of a conversation, as the WIRE understands it: who spoke and
// what they said. Deliberately NOT chat::Turn (which also carries a timestamp
// and a local error flag) — this is the subset that crosses the network, and
// keeping them separate means a display-only field can never accidentally
// become a billed token.
enum class Role
{
    User,
    Assistant,
};

struct Message
{
    Role    role = Role::User;
    QString text;
};

// The multi-turn body. Every dialect difference in the project now lives in
// ONE switch statement per question, in LlmProvider.cpp — that concentration
// is the whole argument for the closed enum (add a Dialect, and -Wswitch
// reads you the list of things to fix).
//
// maxTokens is a PARAMETER here, not the file's constant: quick-add wants a
// short JSON object (300 is generous), a chat reply is prose and needs room.
// Same shaping code, two budgets, no duplicated switch.
QJsonObject chatRequestBody(const Provider& p, const QString& system,
                            const QList<Message>& messages, int maxTokens);

// The one-shot body — now a conversation of length one, delegating to the
// function above. Kept as its own name because every quick-add call site
// reads better for it, and because the 300-token budget is a quick-add
// decision, not a chat one.
QJsonObject requestBody(const Provider& p, const QString& system,
                        const QString& userText);

// Dialect-specific headers only (name, value). Content-Type is the wire
// layer's business — it uses QNetworkRequest's typed setter. A provider with
// no key gets no auth header at all rather than an empty one: some local
// servers reject a malformed Authorization outright.
QList<QPair<QByteArray, QByteArray>> requestHeaders(const Provider& p,
                                                    const QString& key);

// Unwrap the vendor envelope down to the model's own text. Everything past
// this point (fences, JSON, field mapping) is dialect-independent and stays
// in nlp::llm, which is why this returns text and not a ParsedTask.
//
// v25.2 — REASONING MODELS. Two documented failure modes on the OpenAI path
// (Qwen3 / DeepSeek-R1 on Ollama), both the same bug class as the v21.2
// Anthropic content[0] fix — "the reply contains more than the answer":
//   1. deliberation leaks:  content = "<think>…</think>The answer"
//   2. content goes silent: content = "", text lives in message.reasoning
// extractText now strips think-spans from whatever it extracts (both
// dialects — the strip is harmless where the tag never occurs) and, on the
// OpenAI path only, falls back to `reasoning` / `reasoning_content` when
// content is empty AFTER stripping. Content always beats reasoning when both
// hold text: the fallback is for recovering a discarded answer, never for
// concatenating deliberation onto one.
struct TextResult
{
    bool    ok = false;
    QString text;
    QString error;
};
TextResult extractText(Dialect d, const QByteArray& body);

// The think-span scrub, exposed on its own so tests can pin its edge cases
// (unclosed tag, multiple spans, tag-only reply) without forging a whole
// envelope each time. Removes every <think>…</think> span, and an UNCLOSED
// <think> onwards to the end — a model that died mid-deliberation must not
// leak half a deliberation. Pure; returns trimmed text.
QString strippedOfThinking(const QString& text);

} // namespace ai
