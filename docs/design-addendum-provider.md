# Design addendum — the LLM provider layer (v24)

## A. What shipped

The AI quick-add fallback stops being an Anthropic feature and becomes a
**provider** feature. Settings → AI quick-add now offers a dropdown —
Anthropic, OpenAI, Groq, **Ollama (local)**, Custom endpoint — with a
per-provider API key, a per-provider model override, and (for Custom) an
address + dialect row. Change the provider and the very next Ctrl+Enter uses
it; nothing is cached at construction.

The headline entry is **Ollama**: point the app at a small local model and the
AI fallback costs nothing, sends nothing off the machine, and needs no key.

## B. The shape of the problem

Before v24, `LlmQuickAddClient` held five vendor facts welded into one
function:

| fact | value (v21.2–v23) |
|---|---|
| host | `api.anthropic.com` |
| path | `/v1/messages` |
| auth header | `x-api-key` |
| model | `claude-haiku-4-5` |
| reply shape | `content[].text` |

A *provider* is those facts named and gathered into a value:

```
base URL + dialect + model + key
```

**Two dialects cover nearly the whole market**, because the industry converged
on OpenAI's request shape. Groq, Together, OpenRouter, LM Studio and Ollama
all speak it — one enum value buys most of the world, including the local
path. The differences that *are* the dialect:

| | Anthropic | OpenAI-compatible |
|---|---|---|
| path | `/v1/messages` | `/v1/chat/completions` |
| auth | `x-api-key: KEY` (+ pinned `anthropic-version`) | `Authorization: Bearer KEY` |
| system prompt | **top-level field** | **first message** in the array |
| reply | `content[].text` | `choices[0].message.content` |

Four rows. That table is the entire vendor delta, and the design question was
only *where it should live*.

## C. The decision: a value + free functions, not a class hierarchy

`ai::Provider` is a plain struct; the dialect-dependent steps are pure free
functions over a **closed enum**:

```cpp
ai::endpoint(p)                       // base + path (see §D!)
ai::requestBody(p, system, userText)  // the dialect's JSON shape
ai::requestHeaders(p, key)            // the dialect's auth
ai::extractText(dialect, bytes)       // unwrap the vendor envelope
```

Why not `AnthropicProvider : ProviderStrategy`? Because the dialect varies in
**data** — which path, which header name, which JSON shape — not in stateful
behaviour. There is nothing for an object to *hold* between calls. A `switch`
over a closed enum says the whole thing in four lines per function; a strategy
hierarchy would add a factory, ownership, virtual dispatch and a fake-in-tests
to say the same four lines. Two compounding wins from the value form:

- **`-Wswitch` is the registry.** Add a `Dialect` member and the compiler
  names every switch that must learn about it. An open hierarchy has no such
  roll-call.
- **Purity keeps the tests in the Core-only suite.** No
  `QNetworkAccessManager` appears anywhere in `LlmProvider.cpp`, so every
  request the app could ever send is asserted in `test_nlp` in microseconds,
  no socket, no mock server. Same doctrine as `stats::`, `version::` and
  `nlp::llm`.

**The honest counter-case, recorded now:** when the chat-loop step arrives
with streaming and a multi-turn tool-call transcript, a dialect will need to
hold state across calls. *That* is a real object, and this addendum is the
place a future session should point when it promotes the enum to a strategy.
Choosing the simple form today is not a claim that the complex form is wrong —
it is a claim that paying for it before the state exists buys nothing.

## D. The trap: `QUrl::resolved()` deletes Groq

The obvious way to build the endpoint — `baseUrl.resolved(QUrl("/v1/…"))`,
which is what v21.2 did — is wrong for one real provider. `resolved()`
implements RFC 3986 reference resolution, where an **absolute path reference
replaces the base's entire path**:

```
https://api.groq.com/openai  resolved  /v1/chat/completions
→ https://api.groq.com/v1/chat/completions        // "/openai" GONE → 404
```

Correct per spec; wrong for our intent, which is "append the dialect's path to
whatever prefix the vendor mounts its API under." `ai::endpoint()` therefore
**concatenates** (trimming trailing slashes so a pasted `http://host:1234/`
doesn't double up). `endpointJoinsRatherThanResolves` pins both cases.

The general lesson: a convenience function implementing a *standard* can be
implementing a different question than the one you asked. `resolved()` answers
"where does this link go from this page" — a browser's question, not ours.

## E. The wound: QSettings can't let a name be both a value and a group

First cut named the new keys `ai/key/<id>` and `ai/model/<id>`. The migration
test failed on its first run: the migrated model read back **empty**.

QSettings uses `/` as a group separator, and a name cannot be **both a value
and a group**. v21 stored `ai/model` as a *value*; writing `ai/model/anthropic`
asks for a group called `model` where that value already lives. The write is
**silently lost** — no error, no warning, just an empty string on read.

Two fixes, both kept:

1. **Plural group names** — `ai/keys/<id>`, `ai/models/<id>` — which cannot
   collide with anything v21 ever wrote.
2. **Remove-before-write in the migration** — the legacy value is deleted
   *before* the new entry is written, so even a future name clash resolves in
   the right order.

This is the project's **second** QSettings silent-failure (the first: the
anonymous settings path of v22.7). The pattern to remember: QSettings' failure
mode is *silence*, so anything structural about keys — paths, org names,
group/value shape — deserves a test that round-trips through real storage.
The test caught this one before a user could; that is the suite doing its job
on day one of a feature.

## F. Keys and models are per-provider

A single global `ai/model` would send `claude-haiku-4-5` to OpenAI the moment
the dropdown moved; a single global key would make switching back mean
re-pasting a credential. So both are stored under the provider's id, and
`ai::configured()` resolves the override (or the catalog default) at read
time. The dialog mirrors this with an **in-memory edit buffer**
(`m_aiKeys` / `m_aiModels`): switching the combo *stashes* the fields under
the provider being left, then *loads* the provider being entered — in that
order, because fused into one step the Anthropic key would be saved under
"openai". OK writes every provider touched this session (switching away must
not discard a pasted key); Cancel writes nothing, the dialog's standing
promise, now proven to survive three provider switches by
`settingsKeepsAKeyPerProvider`.

`migrateLegacySettings()` (called once from `main()`, idempotent) carries the
v21 single-vendor entries into `anthropic`'s slots — copy only into an empty
destination, then remove the legacy entry, so exactly **one** copy of a
credential exists. The honest cost: downgrading to v23 loses the key from
Settings (one paste restores it; the env var still works). Two live copies of
a secret is the worse problem.

## G. Ollama, and what `needsKey=false` corrects

The old client refused to fire without a key — correct when the only provider
was a paid API, wrong the moment a local server exists. The guard is now
**per-provider** (`provider.needsKey && key.isEmpty()`), and a keyless request
sends **no auth header at all** rather than `Authorization: Bearer ` with an
empty credential, which some local servers reject as malformed. Absence of a
key is described by absence of the header — say nothing rather than say
something false.

Error messages grew provider-awareness for the same reason: "no API key set"
is confusing once there is more than one place a key could live, so failures
name the provider; a 404 gets its own message ("check the address") because
with self-hosted endpoints it almost always means a wrong base URL, not an
outage.

## H. What moved where

```
LlmQuickAddClient (wire)      ai:: (pure, NEW)              nlp::llm (pure)
------------------------      --------------------------    ------------------
POST, timeout, status,        catalog / byId / configured   systemPrompt(today)
staleness generation          endpoint / requestBody /      fence-stripping
                              requestHeaders / extractText  field mapping
                              migrateLegacySettings         (dialect-blind)
```

`nlp::llm::parseApiReply(body, dialect)` delegates the *envelope* unwrap to
`ai::extractText` and keeps everything after it, because fences and the
defensive field mapping are identical for every vendor —
`fenceStrippingIsDialectIndependent` exists precisely to fail if that layering
ever slips. The dialect parameter defaults to `Anthropic` so every pre-v24
call site and test keeps compiling and keeps meaning what it meant.

The Anthropic unwrap also stopped indexing `content[0]`: it now walks to the
first `text` block, so a reply leading with another block type (e.g. a
thinking block) no longer reads as "no text content".

## I. Tests

**+12 pure** (in `test_nlp`, still Core-only — the provider layer's purity is
what admits it): catalog degradation, the `resolved()` trap, body shape per
dialect, headers per dialect, the keyless-provider header rule, OpenAI replies
unwrapping to the same `ParsedTask`, dialect-independent fence-stripping,
wrong-dialect-fails-cleanly, the first-text-block walk, migration
(once/idempotent/never-overwrites), per-provider model resolution, and the
custom endpoint honouring Settings.

**+2 UI**: `settingsKeepsAKeyPerProvider` (the stash/load buffer through the
real OK button) and `settingsShowsTheAddressRowOnlyForACustomEndpoint`.

Totals after v24.0.0: **81 domain + 44 nlp + 19 taskmodel + 19 auth + 60 UI =
223 automated.**

## J. Bycatch: `Version.h` now enforces itself

v24 also closes the file's own embarrassment (v23.0 shipped with the macros at
22.0.0 and the notes at 23.0.0): a `constexpr` parser reads
`TICKTIMER_VERSION_STRING` at compile time and a `static_assert` stops the
build if it disagrees with the MAJOR/MINOR/PATCH macros. Discipline was the
wrong tool; the compiler is the right one. (Why check rather than *generate*
the string from the macros: the `.rc` files consume the string as a single
token, and resource compilers are unreliable about preprocessor
string-building — checking is portable, generating isn't.)


## K. v25.1 — the Test button (checking a key without leaving the dialog)

The first question every user has after pasting a key is *did that work?* —
and until v25.1 the only answer was to close Settings, fire a real feature,
and read its error. The Test button answers in place, and its design is three
decisions:

**1. It tests what is ON SCREEN, not what is saved.** Cancel-writes-nothing
forbids the shortcut of saving first just to test, so the probe is built by
`aiProviderFromFields()` — a deliberate mirror of `ai::configured()` that
reads the dialog instead of QSettings, with identical resolution rules
(custom takes address + dialect from the fields; a non-empty model field
overrides the default). A ✓ is therefore a promise about what OK *would*
produce. `settingsTestUsesTheFieldsNotTheSavedSettings` proves the negative
case: saved settings holding a perfectly good Anthropic setup, the screen
holding a custom endpoint with no address — and the failure is the screen's.

**2. The key composition is field-first, env-second** — the same order
`configuredKey` uses, with the unsaved field standing in for the QSettings
half. The env half was extracted as `ai::envKey(p)` so the dialog composes
rather than re-implements. The composed key is passed to the probe as a
**full override** (`ChatClient::setKeyOverride`), even when empty: the probe
must test this exact composition, not silently fall back to a key saved last
week.

**3. The probe IS ChatClient.** No new wire class: the assistant's client
already carries provider override, provider-aware fail-fast messages, the
404/401/429 vocabulary, and a 60 s timeout. The Test button is one more
caller with two overrides set — which also means the wire it exercises is
*literally* the wire the Assistant will use, so a ✓ vouches for both
features at once. The request is the cheapest one that still proves address,
auth, dialect and model agree: one system line ("Reply with only the word
OK."), one word, reply discarded.

House-rule notes: the fail-fast paths (no key, no address) answer
**synchronously and offline**, which is what makes the button testable
without a socket (`settingsTestKeyFailsFastOfflineAndWritesNothing`, which
also asserts QSettings is untouched — Test extends the Cancel-writes-nothing
promise). A verdict is cleared on every provider switch: "✓ Connected"
surviving a combo change would vouch for a setup nobody tried. And the probe
is a **child of the dialog**, so an in-flight test dies with a closed dialog
instead of toasting a window that no longer exists.

---

## L. v25.2 — reasoning models: the reply contains more than the answer

Planned as assistant addendum §D ("ships first — nothing else is verifiable
on the local model until it lands"), shipped here because it is pure
provider-layer work. Diagram: `diagrams/extract_text_flow.*`.

**The two failure modes** (Qwen3 / DeepSeek-R1 on Ollama, both documented
upstream):

- **Leak.** The model's deliberation stays *inside* `content` as
  `<think>…</think>`, and `extractText` returned it verbatim — a private
  reasoning pass landing in the chat bubble.
- **Silence.** Some configurations route *all* text into a `reasoning` /
  `reasoning_content` side field and leave `content` empty; the app reported
  "AI reply had no text content" and **discarded a correct answer**.

**This is V72's bug class on the other path.** The v21.2 Anthropic fix
(never index `content[0]`; walk to the first text block) answered "the reply
contains more than the answer" for a dialect that separates deliberation
*structurally*. The OpenAI dialect ships the deliberation *inside the
string*, so the same principle needs a scrub instead of a walk. One bug
class, two dialects, two mechanically different fixes — which is exactly
what makes it a class and not a coincidence.

### The three decisions

**1. The scrub is a scanner, not a regex — and it runs on both dialects.**
`ai::strippedOfThinking()` removes every `<think>…</think>` span with two
`indexOf` calls per span; the lazy-dotall regex alternative has quadratic
cliffs on pathological replies, and — worse — a *missing closer* would make
it match nothing, which is backwards: an **unclosed `<think>` is the
streaming-truncation case**, and it is dropped to end-of-text (half a
deliberation must not leak). The scrub also runs on Anthropic-dialect text:
Anthropic proper never inline-tags, but a Custom endpoint may claim either
dialect while proxying a model that does, and a scrub that finds nothing
costs one failed `indexOf`. Exposed as its own function so tests pin the
edge cases without forging an envelope each time.

**2. Fallback only on empty-after-scrub; content always beats reasoning.**
The `reasoning` fields are consulted **only** when `content` is empty once
scrubbed — recovering the discarded answer, never concatenating deliberation
onto a present one (that would reintroduce the leak bug as a side effect of
fixing the silence bug). Two field spellings (`reasoning`,
`reasoning_content`), because the field predates any standard. The "no text
content" error still exists — it now fires only when a reply genuinely
holds no answer, which is what the error always claimed to mean.

**3. `think: false` is opt-in per catalog entry, never per dialect.**
The flag is an Ollama extension; **OpenAI proper rejects unknown body fields
with a 400**, so a blanket per-dialect flag would break every cloud seat to
maybe-help the local one. `Provider.sendThinkFlag` is `true` for the Ollama
entry alone. Custom endpoints never get it — silently adding fields to a
server the user defined is how "works with curl, fails in the app" bugs are
born. Best-effort by design: Ollama's honouring of the flag is reported
inconsistent on the OpenAI-compatible endpoint, so the *reliable* off-switch
is a Modelfile (`num_ctx 8192`, thinking disabled — see assistant addendum
§D for the sizing arithmetic), and the scrub is the guarantee either way.

### Tests (+6, all offline, forged bytes)

Scrub with multiple case-varying spans on both dialects; unclosed-span drop
(including the deliberation-only reply degrading to the named error);
empty-content fallback through both field spellings, including
content-that-is-only-a-think-span counting as empty; content-beats-reasoning
(never concatenate); the think flag present for Ollama and absent for
OpenAI, Groq, Anthropic and Custom; and the layering proof — a
think-wrapped, fence-wrapped quick-add reply parses to a task with
`nlp::llm` learning nothing, because the scrub lives upstream of it in
`extractText`.

---

## M. v26 — per-role routing: the seat walk

Planned as assistant addendum §E; the mechanism lives here with the rest of
`ai::`. Diagram: `diagrams/chat_route_walk.*`.

### The mechanism, whole

`ai::Feature { QuickAdd, Chat }` (named `Feature` only because `ai::Role`
was already the message speaker — when the docs say "role", this enum is
what they mean) → `routeFor(feature)`: an **ordered** provider list from
`ai/route/<feature>`, each id resolved through `resolved()` — the overlay
path factored out of `configured()` so a custom endpoint behaves
identically in any route position. `ChatClient` walks it: skip seats the
breaker says are cooling, try one, and on the **Unreachable class only**
(nothing answered: refused connection, no route, timeout — status 0) move
to the next, announcing the move. Everything a server actually *said* —
401/403/404/429/5xx, an unparseable 200 — fails loudly on the seat that
said it: masking a wrong key behind a quieter seat costs an evening of
wondering why the answers got dumber.

### Migration by derivation — a recorded deviation from §E

§E suggested copy-once-and-remove-the-legacy-key. What shipped is stronger
and more house: a missing (or fully broken) route key **derives**
`[configured().id]` at read time. Nothing is written (nothing can be
written twice), nothing is removed (a downgrade to v25.3 finds
`ai/provider` untouched), and the legacy key stays *meaningful* — it is the
primary seat, which Settings still edits directly. Repair-on-read is the
idiom `personaById` and `dialectFromString` already established; a
migration that can be expressed as a derivation should be one.

### The breaker

`ai::Breaker` — a value with the clock passed in, so tests never sleep;
one process-wide instance via `breaker()`, shared by every wire client,
because seat health is a fact about the *machine's* connectivity, not any
one conversation. 20 s cooldown after an unreachable verdict, cleared by
the next success; in-memory on purpose (persisted state would refuse a seat
that came back an hour ago). `planRoute()` filters the cooling seats; an
all-cooling route comes back **empty** and the client fails fast *naming
the seats* — for one cooldown the app answers "unreachable" instantly
instead of re-proving the outage one timeout at a time, which is the §E
offline-mode complaint fixed.

### The hook, and the static that almost ate it

`TICKTIMER_AI_DOWN=<ids>` forces seats unreachable *before any socket
opens* — the §3.30-family forcing hook (`TICKTIMER_COMPACT=1`'s sibling):
testing a fallback must not require unplugging a router, and the UI test
drives the real machinery through it. The first draft cached the env read
in a function-local `static`, which would have made the hook a one-shot in
any process that called it before the test set the variable — the classic
static-initialization trap wearing a QString. Read per call; it is one
`qEnvironmentVariable` per attempt.

### §E.5 in the flesh

`seatName(id)`: the `ai/seatName/<id>` override, else the catalog display
name — the ONE string for Settings rows and both transcript notices.
Cosmetic and never a key: routing, breaker state and storage keep speaking
ids, so a rename can never re-route, re-test, or destroy anything. (No
rename UI yet; the plumbing is the point, the edit box is a polish rev.)

### Scope cuts, all deliberate

- **Quick-add keeps one seat.** Its fallback already exists and is not an
  AI: the deterministic parser ran first. Routing shipped on the role with
  *nothing else to give* (§E's own table); the enum, storage and
  `routeFor(QuickAdd)` are ready (and tested) for the day it earns a walk.
- **Attribution is a transcript notice, not a bubble badge** ("answered by
  Ollama (local)" as a local-only line). Same information, same place the
  eye already is; the per-bubble tag is recorded as follow-up polish.
- **"Test all" deferred.** The single-verdict probe design makes it less
  free than §E hoped; with two-seat routes, Test per seat via the combo
  nearly covers it. It lands with the seat-rename box.
- **Primary is shared across roles** (the Provider combo, unchanged). §E
  wants per-role lists; the storage IS per-role, the UI edits a shared
  primary + a chat fallback until a role exists whose primary must differ —
  which is Check-in, which arrives with "local, always" and will force the
  question properly.

### Tests (+8: 6 pure, 2 UI)

Derivation (route-free settings route to the v25 provider, and stay
route-free); stored-route repair (unknown ids and duplicates dropped, order
kept, fully-broken degrades to derivation); custom-seat overlays in
fallback position; breaker cooldown arithmetic with an injected clock;
planRoute's skip/empty/expiry; seat-name override changing words but never
routing; the end-to-end walk through the hook (announce → next → fail, then
the breaker's instant named fast-fail on send #2); and the Settings
round-trip (fresh = fail-fast, stored second seat shown back, fallback ==
primary collapsing to a one-seat route — [x, x] is a typo, not a
preference).
