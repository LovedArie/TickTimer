# Design Addendum — The Assistant (chat panel), v25

*Extends `design-doc.md`. The provider addendum (§C) named the condition under
which this feature would arrive; this is that feature, and §C's promised
re-examination is honoured in §E below.*

---

## A. What was asked for

The iteration plan's AI arc (§4a) states the destination: an assistant that
"chats with you, asks how the day went, plans tomorrow." The step shipped here
is the **conversation with sight**: a rail page where you talk to a model that
can see today's plan, tracked time, and tasks — and can change **none** of it.

Scoping decision, recorded: chat and tool use are separate sessions on
purpose. A chat that cannot touch your data is safe to get wrong; the moment
it can call `addTask()` you are debugging conversation state *and* mutation
policy at once. Read-only first means every bug this version can have is a
display bug.

## B. The layer map — two pure layers, two suites

```
brief::dayBriefing(AppData, today, now)      pure, DOMAIN side  → test_domain
chat::Transcript / window() / systemPrompt   pure, CORE side    → test_nlp
ai::chatRequestBody(p, system, msgs, max)    pure, CORE side    → test_nlp
ChatClient                                    wire               (too thin)
ChatPage                                      UI                 → test_ui
```

The split worth memorising: **`brief::` knows tasks but no vendors; `chat::`
knows vendors but no tasks.** They meet only inside `ChatPage`, where one
string (the briefing) is handed to the other (the prompt). The build enforces
the separation — `ChatSession.cpp` compiles in the Core-only `test_nlp`
target, which cannot even see `AppData.h`'s dependencies.

## C. The briefing — what the assistant knows, as a testable artifact

`brief::` turns the aggregate root into one block of plain text. Four
anti-hallucination rules are *encoded*, each with a test whose failure names
the leak:

1. **Empty sections say so** ("nothing planned"). Silence invites invention.
   → `briefingStatesEmptinessOutLoud`
2. **Counts are stated and cuts are visible** ("DUE TODAY (3)", "+1 more").
   → `briefingCapsAreStatedNotSilent`
3. **No ids.** The assistant is read-only; ids are plumbing it could only
   misuse. → `briefingLeaksNoIdsAndNoDescriptions`
4. **No notes or descriptions.** The most private text in the file stays
   home. Same test as rule 3 — the briefing is the feature's privacy page in
   executable form.

Blocks are labelled `[past]` / `[NOW]` / `[upcoming]` relative to a `now` that
is a **parameter** (the nowProvider doctrine, §3.38, applied once more), and
the task partition reuses `upcomingTasks()` so the app and the assistant can
never disagree about what "upcoming" means.

**Cost note:** the briefing rides inside the system prompt of *every* turn,
so its length is billed per message. That is why the caps in
`brief::Options` are small and the format is terse.

### C.1 v28.10 — the field-report sections, and a fifth rule

The first real day of use (PROJECT_LOG, the field report) found three
wrong answers, and all three were *content gaps in this briefing* — none
was a prompt or model problem. Each became a section, and together they
name a **fifth anti-hallucination rule**:

5. **Computed facts are stated, never implied.** If answering needs
   arithmetic over the briefing's own lines — comparing timestamps to
   `now`, summing per-block figures — the briefing does the arithmetic
   and states the result, because §A's spine says models have no clock
   and can't do arithmetic dependably. Handing the model raw numbers and
   expecting inference is a spine violation wearing a data format.

The three sections it produced:

- **DAY STATUS** — remaining-block count and the day's last end, or "the
  planned day is OVER". The per-block `[past]/[NOW]/[upcoming]` tags
  stay (block-level detail); this line is the day-level verdict the
  model kept fumbling. → `briefingStatesDayPhaseWhileRunning` /
  `...WhenOver`
- **PLAN FOR TOMORROW** — blocks one day ahead, ISO-dated in the header,
  capped by `maxBlocks`, emptiness stated as "nothing planned *yet*"
  (unplanned tomorrow is normal, not failure — rule 1 with manners).
  Deliberately no phase tags and no tracked column: a future block has
  neither, and empty columns invite invention. One day only — task
  deadlines already reach `upcomingDays` out, and block detail past
  tomorrow is tokens spent on a plan that will change anyway.
  → `briefingCarriesTomorrowsBlocks`
- **TRACKED TODAY, disambiguated** — the line now says "(day totals; any
  per-block 'tracked' figures above are parts of these, not extra)",
  because the model once said "none of that time was logged" and "1h03m
  focused" in the same breath: it was reconciling the two numbers
  itself. The relationship is now a stated fact.

The debugging posture this arc fixed into doctrine: when the assistant is
wrong, read `currentBriefing()` first (the debug panel shows it live) and
ask *"what did we fail to tell it?"* — the context is the product.

## D. The transcript — the log is a superset of the conversation

`chat::Transcript` holds `Turn`s; a turn may be `localOnly` — shown to the
human, never sent to the model. Without that flag, the app would eventually
tell an LLM that *it* had said "couldn't reach the AI service", which is both
false and the kind of thing a model builds on.
(`localOnlyTurnsNeverReachTheModel`)

`window(budgetChars)` decides what is sent: walk backwards, whole turns only,
oldest fall off first, and a leading assistant turn is dropped because both
dialects expect the exchange to open with the user. Two deliberate edges:

- The **newest turn always goes**, even if it alone busts the budget —
  refusing to transmit what someone just typed is not a saving.
  (`oversizedNewestTurnIsStillSent`)
- The budget is **characters, not tokens**, and that is an honest
  approximation: a real tokenizer is vendor-specific and would make the
  layer impure. What matters is that the cost is *bounded*.

## E. §C revisited: does multi-turn promote the enum to strategies?

The provider addendum promised this check. Answer: **not yet.** Multi-turn
shaping turned out to be *data all the way down* — the per-message object is
identical across dialects (`{role, content}`); only the system prompt's
location differs, exactly the difference the one-shot body already expressed.
`ai::chatRequestBody` is one switch; `requestBody` now **delegates** to it
with a one-turn list (`oneShotBodyIsAOneTurnChat` pins the delegation, so a
re-inlined second switch cannot silently drift).

The revisit criterion, sharpened for next time: it is **streaming** and
**tool-call transcripts** (where a dialect must hold a parse cursor or
pending-call state *across* calls) that create the state a strategy object
exists to hold. Requests still vary in data; replies-over-time will vary in
behaviour.

## F. The wire — a fifth client, not a mode on the fourth

`ChatClient` was not merged into `LlmQuickAddClient` because they disagree on
every axis a network call has: payload (a windowed transcript vs one line),
reply handling (prose displayed verbatim vs JSON parsed defensively), budget
(800 vs 300 tokens), timeout (60 s vs 15 s — a local 8B model on a laptop CPU
genuinely takes half a minute), and cancellability (a visible Stop vs silent
supersession). A merged class would be two modes and a flag — the shape a
class takes just before it becomes two classes anyway. What *deserves*
sharing is shared: both ask `ai::` for URL, body, headers, and unwrap.

New wire facts: `429` gets its own message (several turns a minute is normal
conversation; free tiers count them), and `cancel()` bumps the generation
**before** aborting, so the abort-triggered `finished()` is discarded as
stale instead of surfacing a spurious network error.

Note what `ChatClient` has that `LlmQuickAddClient` doesn't need, and lacks
what the other has: there is **no pure reply-mapping counterpart** here. A
chat reply is prose — the text *is* the answer — so `nlp::llm`'s whole job
(defensive JSON field mapping) has no chat equivalent.

## G. The page — and why it ignores two house idioms

**No model/view**, three sessions after the model/view arc: the log is
append-only, tens of rows, every row a different height because it wraps
prose. Virtualisation is irrelevant at this scale, there is one view, and
sent messages never change; what a delegate would cost is word-wrap
`sizeHint` arithmetic, the fiddliest paint code in Qt. One `QLabel` per turn.
Knowing when *not* to reach for the pattern is the v20 lesson finishing.

**No rebuild-on-changed()**, the idiom every other page uses: the log only
grows at one end, and a rebuild would throw away scroll position to re-derive
a list that changed by one row. Append is the honest operation; `clear()` is
the only rebuild.

Context is rebuilt **at fire time, every turn** — not once per conversation.
Add a task between two questions and the second one knows it
(`chatBriefingIsLiveAndOnTheSeamClock`). Same read-at-fire-time doctrine the
provider and key already follow.

Smaller decisions, each with a reason on site: Enter sends / Shift+Enter
newlines (`ChatInput`, its one job); Send and Stop are one dual-role button
(a separate Stop is dead furniture 99% of the time); failures land in the log
as `localOnly` bubbles, not dialogs (they belong to the conversation they
happened in); bubbles are mouse-selectable (an answer you can't paste is half
an answer); three starter chips because a blank chat box is a small
executive-function tax — you must invent the question before you can ask it.

**"What can it see?"** shows `currentBriefing()` verbatim, plus which
provider receives it. A feature that ships your data somewhere should be able
to show you exactly what it sent, in one click.

## H. The near-miss: rail order vs stack identity

The Assistant's button sits *above* Archive's in the rail; its page is index
6, *after* Archive's 5 — because `showPage(5)` already means Archive to the
screenshot tool and every doc that mentions it. But `showPage(i)` lights
`m_navButtons[i]`, so appending buttons in **visual** order would have made
`showPage(5)` display the Archive while highlighting the Assistant. Both are
`QToolButton*`; no compiler can object. The fix is a sentence: *the layout
decides where a button sits; `m_navButtons` is indexed by page identity* —
and `assistantPageHighlightsItsOwnButton` walks both pages so the sentence
stays true.

## I. The system prompt is a machine contract

It is deliberately **not** wrapped in `tr()`: translating it would change the
model's instructions per locale. The assistant is instead *instructed* to
reply in the user's language — the one place localisation belongs in a
prompt. Its safety clauses (read-only, never-invent, the Ctrl+N redirect, the
CONTEXT fence) are pinned by `chatPromptStatesTheReadOnlyContract`, which
asserts concepts, not sentences, so wording stays editable.

The tone rule ("calm, never shaming, one next step, not a lecture") is the
Supplementary Spec's non-shaming requirement extended to a subcontractor: the
model speaks inside our app, so it is bound by our usability spec.

## J. What this version deliberately does not do

- **No persistence.** The transcript dies with the window; data format stays
  at **v9**, no migration, no format test churn. Revisit if conversations
  prove worth keeping.
- **No streaming.** One request per turn. Streaming is the likeliest §E
  trigger and earns its own session.
- **No tool use.** The secretary's hands are the next iteration; this was its
  eyes.
- **No week/history in the briefing.** Today plus a 7-day task horizon.
  Extending the window is an `Options` edit when a real question needs it.

---

## K. v25.3 — persona: the prompt becomes four bands, two of them locked

Planned as assistant addendum §C, shipped here — the persona is a
conversation concern. Diagram: `diagrams/persona_prompt_bands.*`.

### What changed structurally

`systemPrompt` was one block (`kRules`, five numbered rules). It is now an
assembly in authority order — **contract → floors → style → context** — and
the old rules were *redistributed*, not rewritten:

- rules 1, 2, 5 (never invent, date arithmetic, reply-in-their-language)
  stayed in the **contract**, locked;
- rule 4 split in half: its *non-shaming* opening was **promoted to a
  floor** — above every persona, including Custom free text — and its
  *style* half became the Calm preset;
- rule 3 (brevity) moved into Calm, because **verbosity is a persona
  property** by §C's own definition, and Brief exists precisely to own a
  different answer to it.

The floors state their own authority in the prompt ("these override any
style below"), and a second floor was added: **know your lane** — no
persona pushes on someone who is struggling; a planner playing counsellor
is out of its depth.

### The rule that makes Custom shippable

A persona may change **how** things are said, never **what** is allowed.
That is not a comment — it is a *string equality*:
`personaChangesTheStyleBandOnly` asserts everything above the STYLE marker
is byte-identical across the whole catalog, and
`everyPersonaKeepsTheContractAndTheFloors` walks every preset (Custom gets
"Talk like a pirate") asserting the contract, both floors, and the band
*ordering* survive. If a style could soften "never invent", the feature
would be a prompt-injection hole with the user holding the injector.

### The catalog, and why Calm is first

Same doctrine as `ai::Provider`: the Settings combo is populated from
`personaCatalog()` (one list, nothing to drift), and `personaById` repairs
an unknown id to the **first** entry. First is therefore Calm — which is
the v25 voice, so the unset key, the hand-edited key, and the downgrade all
mean "what you had before". The one-arg `systemPrompt(briefing)` overload
survives and *is* the Calm band (`theOneArgPromptIsTheCalmDefault`):
shipping personas changes nobody's assistant until they opt in.

An empty band emits **no STYLE header at all** rather than an empty one — a
header with no body reads to a model like an instruction it failed to
receive.

### Storage: QSettings, and the free text is a QLineEdit

`ai/persona` + `ai/personaText`, spelled once in `chat::` key functions
(the LlmProvider lesson: a typo'd settings string fails silently). QSettings
and not `data.json`, deliberately: persona is **taste**, the same class as
agenda hours — facts sync between machines, taste stays on the machine that
chose it. Another device gets *its* persona, not yours.

The free text is a **QLineEdit with maxLength 240**, not a QPlainTextEdit,
and the widget choice is the design: the roadmap's §C.4 warns that long
character prompts crowd out the rules and measurably degrade
instruction-following. One line invites a note ("call me Sam, skip
greetings"); a text area invites a character sheet. Free text *appends* to
any preset and *is* the band for Custom — read fresh at fire time, same
doctrine as the provider and key, so the very next message speaks the new
way.

### What deliberately did NOT get a persona

Quick-add. `nlp::llm::systemPrompt` is a JSON machine contract — a parser
has no tone, and giving it one would only add new ways to break the parse.
The persona reaches exactly one call site: `ChatPage`'s send.

### Tests (+6: 5 pure, 1 UI)

Catalog defaults and repair; contract + floors + band ordering for every
preset; the style-band-only string equality; the one-arg-is-Calm promise
(and empty-band-emits-nothing); the QSettings composition
(preset+text / custom / repair); and the dialog round-trip — Calm on fresh
settings, Cancel writes nothing, the real OK button writes trimmed values,
an unknown stored id lands the combo on Calm rather than index -1 (a combo
with no current row silently saves an empty id — a fresh way to brick).
