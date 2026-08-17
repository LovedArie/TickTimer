# Design Addendum — The Assistant Roadmap

**Status: PLANNED — none of this is implemented.**
Written 2026-07-20 from a design conversation. Continues the decision log in
`design-doc.md §3`; extends `design-addendum-chat.md` (v25, shipped) and
`design-addendum-provider.md` (v24, shipped).

> **How to read this file.** Every other `design-addendum-*.md` documents
> something that exists. This one documents decisions taken *before* code, so
> that a long planning conversation does not have to happen twice. Nothing
> here has a test behind it yet. As each part ships, its section should be
> promoted into its own addendum (or into `design-doc.md §3`) and struck out
> here — a roadmap that never shrinks is a wish list.

---

## A. The spine

One sentence governs every feature below:

> **Code decides *when*. Code computes *what is true*. The model only phrases.**

This was not designed up front — it drew itself three separate times while
working through the scenarios, which is why it is stated first:

1. **Triggers.** "Nudge me at breakfast" needs a recurring block and an alarm,
   not a model that learns your habits.
2. **Judgments.** "Can I afford to put this off?" is deadline arithmetic
   against free slots, not intuition.
3. **Wording.** What is left for the model is the sentence — which is exactly
   what models are good at, and the only part that is hard to write in C++.

**Why the discipline, stated as costs:**

- **Cost.** A model deciding *when* to speak must be asked constantly. A
  trigger written in C++ costs nothing and runs on a timer we already own.
- **Reliability.** Models have no clock, cannot do arithmetic dependably, and
  do not remember whether they already pinged you at 14:00.
- **The Supplementary Spec.** An assistant that interrupts on its own judgment
  becomes noise and gets muted. For an ADHD-facing tool, "gets muted" is total
  feature failure, not a tuning problem.

**Corollary — the model is an enhancement layer, never a dependency.** Every
AI feature below sits on top of something that already works without it: the
plain toast, the deterministic parser, the Upcoming list. If the model is
unreachable, slow, or wrong, the underlying feature still fires. This is what
makes a local-first assistant on a laptop viable at all.

---

## B. The trust boundary

**Status (v29.0, Slice 1):** stages 1–4 are BUILT, model-less — the closed
per-role verb set, per-turn handles, the confirm card, and the copy-aside
all ship and are pinned by tests; the debug panel plays the proposer.
Full design record: `design-addendum-write-boundary.md`.

**Read-only until the tool-use iteration; confirmation per change after it.**

Four stages, in this order — the order matters, because retrofitting stage 4
is expensive and designing for it is nearly free:

1. **Proposal.** The model emits a *narrow, closed verb set* mapping to
   existing `AppData` doors. Anything not on the list cannot be asked for.
2. **Validation.** `AppData` already refuses illegal mutations (`addEvent`
   returns empty on overlap; `addTask` refuses a missing category;
   `removeActivity` refuses while an `Event` references it), and 86 domain
   tests say so. The validation layer most tool-use implementations forget to
   build is already built and guarded.
3. **Confirmation.** Proposal cards with Apply / Discard. Not auto-apply:
   "here is what I will do, one tap" is lower-anxiety than discovering your
   calendar rearranged itself, and it is how you find out the model is wrong
   *before* it costs you.
4. **Recovery.** See below.

### B.1 No undo button — because the verb set makes one unnecessary

*Decision:* ship **no undo**, and instead guarantee that **every verb the
assistant can call has an inverse it can also call**. "Ask it for something
else" then *is* a complete undo story.

*Why this works:* changing a due date, moving a block, marking something done —
each has an inverse the assistant can perform on request.

*Why it would otherwise fail:* destructive verbs lose information the model
never had. The briefing deliberately excludes descriptions and notes
(chat addendum §C), so a deleted task cannot be restored — the assistant never
saw the description it destroyed. `removeEvent` takes its segments with it, and
segments are the app's raw truth; fifty minutes of recorded focus does not come
back from "please re-add that."

*Therefore:* **the assistant never gets `removeTask`, `removeEvent`, or
`removeActivity`.** Making something go away uses **archive** — which v7
already built as exactly this: the non-destructive "get it out of my sight,"
history intact, Restore always available. The domain made this decision years
before the assistant needed it.

*Cheap insurance, not a feature:* copy `data.json` aside before applying a
batch. No UI, no button — a file that exists if it is ever needed.

### B.2 Handles, not ids

The briefing strips ids on purpose and must keep doing so. Tool use needs a way
to *reference* objects, so the briefing gains **per-turn handles** (`T1`, `B3`)
mapped back to real UUIDs locally.

*Why:* models invent plausible-looking UUIDs. They do not invent `T7` when only
`T1`–`T3` exist — and if they do, the lookup fails safely instead of hitting
the wrong task.

### B.3 The §C promotion criterion should finally fire here

The provider addendum §C, revisited in chat addendum §E, said the `Dialect`
enum earns promotion to strategy objects when a dialect must hold **state
across calls**. Tool-call transcripts do exactly that — Anthropic's
`tool_use`/`tool_result` blocks versus OpenAI's `tool_calls` array — and unlike
multi-turn shaping, this is behaviour over time rather than data in one
request. **Expect the promotion during the tool-use iteration; if it still is
not needed, record why.**

*Recorded (v29.0, Slice 1):* not needed yet — every Slice 1 proposal is
single-shot and C++-composed, so no dialect holds state across calls;
the criterion is behaviour over time and this slice has none. Slice 2's
threaded tool transcripts are where it should genuinely fire
(write-boundary addendum §I).

*Recorded again (v29.1, Slice 2):* still not needed — and this time by
DESIGN, not by absence. Intake chose plain JSON extraction over the
vendors' tool-calling APIs (intake addendum §B): provider-neutral down
to a local Ollama, single-shot by construction, the confirm loop itself
serving as the tool layer. No tool transcripts exist to thread, so no
dialect holds state across calls. The criterion stays armed for a verb
that genuinely converses (multi-step rescheduling may); two honest
non-firings are not a dead letter, they are the doctrine working.

### B.4 Verb lists are per-role, not global

*Decision:* the closed verb set is scoped **per role** (§E.2), not defined once
for the whole assistant.

*Why:* withholding `removeTask` globally protects against *damage*. Scoping per
role protects against a whole *category* of action. The nudge and check-in
roles never need a write verb at all — they observe and they phrase. Handing
them an empty verb list makes "the 08:00 toast rearranged my afternoon"
structurally impossible rather than merely unlikely, and it means a
prompt-injection landing in a nudge has nothing to reach for.

*Prior art:* OpenClaw enforces per-agent tool allow/deny lists for the same
reason — the personal assistant gets exec and filesystem access, the public
Discord bot does not. Different threat model (they isolate *agents*; we isolate
*call sites*), identical mechanism.

*Cost:* near zero **now**, because the verb list is a data structure that does
not exist yet and can simply be born as `role → verbs` instead of one flat set.
Expensive after v29, when every verb has a call site assuming the flat shape.
This is the §B stage-4 argument applied one level down: designing for it is
nearly free, retrofitting it is not.

*Consequence:* intake (§K) is the first role to receive a write verb, and it
receives **exactly one**. That is a much easier thing to review than "the
assistant can now write."

---

## C. ~~Personality~~ — SHIPPED v25.3

Promoted to **`design-addendum-chat.md` §K**. Diagram:
`diagrams/persona_prompt_bands.*`. What shipped, in one line each:

- the prompt is four bands — contract → floors → style → context — with the
  old rule 4 split: non-shaming promoted to a locked floor, its style half
  now the Calm preset; a second floor (know your lane) added;
- presets Calm (default = the v25 voice; the one-arg `systemPrompt` overload
  *is* it, pinned by test), Brief, Coach, Custom — catalog + repair-on-read,
  the `ai::Provider` doctrine;
- free text appends to any preset, IS the band for Custom, lives in
  QSettings (taste, not facts), and is a **QLineEdit maxLength 240** — §C.4's
  short-beats-elaborate warning enforced by the widget;
- "how, never what" is a *string equality* in the tests: everything above
  the STYLE marker is byte-identical across the catalog;
- quick-add got no persona — a JSON parser has no tone.

**§N consequence:** v26's remaining content is §E (routing) alone.

---

## D. ~~Reasoning models~~ — SHIPPED v25.2

Promoted to **`design-addendum-provider.md` §L**, per this file's own rule
(a roadmap that never shrinks is a wish list). Diagram:
`diagrams/extract_text_flow.*`. What shipped, in one line each:

- the `<think>` scrub, both dialects, unclosed spans dropped to the end;
- the `reasoning` / `reasoning_content` fallback, only on empty-after-scrub,
  content always beating reasoning;
- `think: false` opt-in per catalog entry (Ollama only — OpenAI proper 400s
  on unknown fields), with the Modelfile remaining the reliable off-switch.

The latency guidance (a Modelfile pinning `num_ctx 8192`, thinking disabled,
on a 4 GB laptop GPU) stands and is referenced from §L rather than moved —
it is advice about the owner's machine, not a design decision.

**One §N consequence:** v26's remaining contents are §C (persona) and §E
(routing); the "ships first" dependency this section carried is discharged.

---

## E. ~~Per-role provider routing~~ — SHIPPED v26.0

Promoted to **`design-addendum-provider.md` §M**. Diagram:
`diagrams/chat_route_walk.*`. What shipped, in one line each:

- `ai::Feature` → ordered seat routes (`ai/route/<feature>`), chat wired
  end-to-end; fall-through on the **Unreachable class only** — everything a
  server said fails loudly on the seat that said it;
- migration by **derivation**, a recorded deviation from this section's
  copy-once note: a missing route key means `[configured()]` at read time,
  nothing written, downgrade-safe;
- the breaker (20 s, injected clock, process-wide) with `planRoute()`'s
  named fast-fail; the `TICKTIMER_AI_DOWN` forcing hook;
- §E.5 named seats plumbed (`seatName()` — cosmetic, never a key);
- transcript notices for the walk and for fallback attribution.

Deliberate cuts, recorded in §M: quick-add keeps one seat (its fallback is
the deterministic parser), "Test all" and the per-bubble badge and the
rename box are polish revs, primary stays shared until Check-in forces
per-role primaries with its "local, always" rule.

**§N consequence:** v26 is COMPLETE (§D v25.2, §C v25.3, §E v26.0). Next
per §N: **v27 — subtasks & sizing**, no AI in it at all.

---

## F. Proactive nudges *(planned)*

### F.1 Reuse, don't rebuild

The knock already exists: `BlockAlarmService`, the midnight re-arm, the tray
icon, `NotificationToast`. The rule engine already exists too: `coverage::Rule`
and `ReturnPolicy` are literally "does this deserve a nudge, and have I already
asked today?" A proactive assistant is **existing trigger → briefing → persona
→ one sentence → existing toast**, with the plain toast as the fallback.

### F.2 Triggers (deterministic, all computable today)

- a planned block is starting *(exists)*
- a block ended with nothing tracked against it (`unaccountedSeconds`)
- morning briefing / end-of-day review
- an overdue task crossing a threshold
- a long untracked gap during planned hours
- tracked time approaching an estimate while the task is still open *(needs §H)*

### F.3 Manners

Quiet hours, a daily cap, and never say the same thing twice. These are not
polish; they are what stops the feature being disabled in week two.

### F.4 The breakfast case, decomposed

The motivating request — *"nudge me for breakfast, and it needs to learn when"* —
needs far less AI than it appears:

- **when** → a recurring block at 08:00 *(v19 already ships this)*
- **whether it happened** → `unaccountedSeconds` *(already derived)*
- **what to say** → the model, offline, in half a second

"The AI learns your routine" is a hard, unreliable, expensive problem.
"The AI writes down that you eat breakfast at 08:00" is a **recurring block**.
The block could be created by hand today and the alarm would already fire — the
model is what makes the reminder feel like a person instead of a system tray.

---

## G. The morning check-in *(planned)*

### G.1 Shape

A nudge that **expects a reply**, which a toast does not: notification → tap →
the Assistant page opens with the check-in already in the transcript and the
day loaded. This is the standard notification-with-action pattern, so it ports
to Android as a normal thing rather than a fight.

**Fires once, in the morning, and only when the day is computably heavy** —
total planned minutes plus urgent deadlines over a threshold. Being asked how
you are doing five times a day is its own stressor; a quiet Tuesday does not
need a wellness interview.

### G.2 Mood — the only fact that cannot be derived

Everything else in the scenarios is computed from existing data. Mood must be
asked, and therefore stored.

*Decision:* a **coarse value** (rough / okay / good) plus an **optional short
note**. Only the coarse values go into the briefing by default — pattern work
("Wednesday mornings are consistently rough") needs the number, not the prose.

*Retention:* **14 days**, trimmed by a domain rule on the same midnight knock
that already runs `expireDismissals()`. Trimming is domain, not UI convenience.

*Location:* `data.json`, **format v10**. It is a fact about the user, and the
project's own line puts facts in the data file — with the honest cost of a
format bump plus migration, and the benefit that a phone's assistant knows what
the laptop's does.

### G.3 Encouragement must be specific

Generated encouragement reads as fake almost instantly; *"You've got this!"* is
worse than silence. What lands is evidence: *"You did 50 minutes on Lab 4
yesterday. It's the deadline that's tight, not you."*

**Encouragement quality is therefore a *context* problem, not a personality
problem** — it comes from the briefing, not from the persona.

---

## H. ~~Affordability~~ — SHIPPED v28.0 (domain + manners + toast; model joins in 28.1)

Promoted to **`design-addendum-affordability.md`**. Diagram:
`affordability_flow.*`. What shipped, in one line each: `afford::` (the
§H.1 query, pure, planned-blocks proxy per §H.3, Unknown as an honest
verdict), the §F.3 manners gate (change-of-verdict, quiet hours, cap,
§H.5 dismissal respect), `AffordabilityService` (20-min sweep + debounce),
the TIGHT pill on Upcoming (§H.6), `ToastSpec` + the movement seam.
Sentences are plain C++ — §A's corollary shipped as structure: the
no-model path IS v28.0, so 28.1 can only ever be an enhancement.

> *v28.4 update:* the §H.3 proxy is no longer the only sizing source —
> `affordability()` is now **estimate-first** (estimate × the §J.2
> personal rate, proxy as fallback, Unknown only when BOTH are absent).
> Details in §J.2 below and `design-addendum-affordability.md` §B's
> update note. Diagram: `diagrams/affordability_sources.*`.

**§O.1 is closed:** the owner chose **volunteer-mode** ("I want it to be
my secretary… give me a good heads up"). Consequence: §F's pipeline moved
into this slice, model-less. Tight volunteers; Unknown and Comfortable
never interrupt.

The original §H reasoning follows, kept because §H.2–§H.5 still govern
28.1's prompt design:

### The original section (planning prose, now largely delivered)


### H.1 One query, two opposite answers

Both motivating scenarios — *"I'm under the weather, can I rest?"* and *"I want
to go out, can I afford to?"* — call the same function:

```
affordability(task, now) -> { verdict, days_left, hours_tracked,
                              focus_ratio, distinct_days_worked,
                              free_slots_remaining }
```

Scenario 1 returns **Comfortable** (consistent effort, deadline far, slots
available). Scenario 2 returns **Tight** (little focused effort, deadline near,
slots scarce). *Same code; the difference is entirely in the data* — which is
the point, because it means the verdict cannot be talked into changing by how
the question is phrased.

### H.2 The effort history needs no new storage

Segments are facts in `data.json`, kept forever, and `stats::summarize(data,
from, to)` already takes an arbitrary range and returns focus, break,
**distracted** and unaccounted time broken down by day. "I worked 7 of the last
14 days on this" and "I've been distracted, not focused, on this project" are
both queries over data accumulated since v1.

**Only mood is new.** Everything else is derivation.

### H.3 What it cannot know, and the free proxy

The app has never been told how *big* a task is — it knows what was done, not
what remains. Until estimates exist (§J), the proxy is **the blocks you
planned**: 10 hours scheduled and 6 tracked means 60% through *your own* plan,
and that number came from the user, not a guess.

Where no blocks were planned, the assistant must **say so** rather than perform
confidence: *"6 hours went in and it's due Thursday — I don't know how much is
left."*

### H.4 It informs; it never forbids

Not *"No, you can't go out."* Instead: *"You've got 3 hours tracked, it's due
Thursday, and two free evenings left. Going out tonight makes Thursday a long
day — your call."*

Three reasons: the verdict might be wrong (the project may be nearly done in
ways the app cannot see); an app that tells you *no* gets closed and never
reopened; and it is the user's life. A good secretary does not ground you —
they say "that'll be tight."

### H.5 The deferral vocabulary already exists

`dismissTask(id, until)` is already "not now, ask me later" as a first-class
domain concept with a timestamp. `moveEvent` reschedules through a guarded door
that refuses overlaps. `isFree()` finds the landing spot. **"Put things off" is
mostly already written.**

### H.6 Standalone value

"This is tight" belongs on the Upcoming page whether or not a model ever
mentions it. The affordability query should ship as a domain feature, visible
in the UI, before any AI consumes it.

---

## I. ~~Subtasks~~ — SHIPPED **v28.3.0 (format v13)**

> *Correction (v28.3 session): this header used to claim "SHIPPED v27.0
> (format v11)". The v27 drop was built but never landed — the claim was
> written before the apply, and the docs audit that caught it elsewhere
> missed it here. True as of v28.3.0, with v13 (v11 belongs to catch-up,
> v12 to moods).*

Promoted to **`design-addendum-subtasks.md`**. Diagram:
`diagrams/subtask_policies.*`. One line each: `parentId` one level deep,
enforced at the door; five per-query policies (workload surfaces parents-
only, `tasksDueOn` shows dated pieces, `taskCountIn` counts everything
because it guards); roll-up = NO auto-complete (the tick is the reward);
archive cascades both ways; the dialog stays a pure question. Open
questions answered: children may carry deadlines, inherit category at
birth, and may NOT have children.

## J. Task sizing — §J.1 SHIPPED **v28.3.0**; §J.2 SHIPPED **v28.4.0**; §J.3 still planned

`estimateMinutes` + `chunkable` shipped with §I (same format bump, one
migration instead of two) — see `design-addendum-subtasks.md` §E.

> *Restoration note:* the v27.0 strike of this section accidentally removed
> the §J.2/§J.3 planning prose along with the shipped §J.1 — a roadmap must
> shrink by what SHIPPED, not by what sits next to it. Restored below
> (condensed, same arguments) in the same docs-audit session that caught it.

### J.2 The multiplier — TickTimer's unfair advantage — **SHIPPED v28.4.0**

Everyone estimates badly; almost nobody finds out *how* badly, because
nobody records what actually happened. **This app has recorded it since
v1.** Estimate 4 h → actual 6 h; 3 → 5; 8 → 11: *you run about 1.5×.* That
is not a model's opinion, it is division — and it converts "I'm bad at
estimating" (vague, unfixable, faintly shameful) into a coefficient you
can multiply by.

**Derived, never stored** (§3.5): recomputed from segments each time, so
it cannot go stale and it improves on its own as work accumulates.
**Shipped v28.4.0** as `afford::personalMultiplier()` — the MEDIAN of
actual÷estimate over finished, tracked, estimated tasks (median so one
10× disaster is an outlier, not a fact), flat 1.0 under 3 samples,
clamped to [0.5, 3.0] — alongside the §H rewire: `affordability()` now
sizes outstanding work from **estimate × multiplier** when an estimate
exists (the task's own, or the sum of its pieces' — the decomposition
dividend), keeps the planned-blocks proxy as the fallback, and Unknown
has shrunk to "no estimate AND no blocks". Sentences, nudge facts and
briefing lines name their basis; sweeps compute the rate once and pass
it down. Pure queries, no format change, on the shape v28.3.0 built.
Diagram: `diagrams/affordability_sources.*`.

### J.3 Decomposition is the genuinely AI part *(planned, v28+)*

Guessing from history is a query. Helping someone who has *no idea* is the
AI part, and the technique is decomposition: "what are the pieces?" → read
the spec / write the section / get Marc's part / format it → "reading
specs runs you ~40 min; writing sections ~2 h; call it 5 — and your
estimates run 1.5×, so budget 7." **The side effect may matter more than
the number:** "write lab report" is a paralysis-shaped task; "read the
spec" is a twenty-minute action. v28.3.0's `addSubtask` is the door this
will eventually write through (v29's intake verb, §K).

## K. Task intake — the first write verb *(SHIPPED: Slice 1 v29.0 — the
verb, the boundary, the queue; Slice 2 v29.1 — the interview, K.1–K.6
all live. Design record: `design-addendum-intake.md`; the memory-file
half of K.4 waits for §L.)*

### K.1 Never interrupt at capture

Quick-add exists to be fast: Ctrl+N, type, done, back to work. An assistant
that opens an interview on every capture trains the user to stop capturing.

*Decision:* **queue, don't interrupt.** New tasks land in a "needs details"
list; the intake happens when the user chooses — at the morning check-in, or
via a quiet badge. Capture stays instant.

### K.2 One open question, not a form

Six questions asked one at a time in a chat window is *worse* than a form. The
place an LLM beats a form is the reverse: **one answer in the user's own words,
every field extracted.**

> *"Lab 4 — what is it, and how long do you think?"*
> *"contract requirements doc, group project with Marc, probably 6 hours but he
> never does his part so realistically I'm doing 8"*

→ estimate, work type, a dependency, and a risk. This is exactly what
`parseApiReply` already does — free text in, structured object out, every field
degrading safely — one level up from a single line.

### K.3 Guess and confirm, don't ask cold

*"How long will Lab 4 take?"* is a blank-page question, and blank pages are
where ADHD stalls. *"Reports like this have taken you about 6 hours — sound
right?"* reduces the interaction to a nod or a correction. Falls back to asking
cold when there is no history, and gets better as data accumulates.

### K.4 Fields vs. the memory file

- **Structured facts → real `Task` fields.** An estimate is a number: in
  `data.json` it syncs, `affordability()` can query it, the Upcoming page can
  show it, and it costs **zero tokens** because nothing re-sends it every turn.
- **The memory file holds only the residue** — *"group project with Marc, he's
  unreliable, budget extra"* — the unstructured part no field can hold.

Get this backwards and a growing text blob re-describes, every single turn,
things a field could have held — slow, expensive, and drifting out of sync with
the data it describes.

### K.5 Why intake is the right first mutation

- **Purely additive** — fills empty fields, destroys nothing.
- **Naturally confirmable** — the user just answered the question; approving
  their own words is trivial.
- **Tiny blast radius** — a wrong estimate is a bad suggestion, not lost data.

Compare "reschedule my afternoon" as a first write. Intake first.

### K.6 Triage, and asking exactly once

"Buy milk" needs no interview. Only substantial-looking tasks get offered one
(far deadline, urgent priority, a category that historically absorbs hours).
And **ask once**: if skipped, it stays skipped — a task that keeps asking to be
explained becomes the one you archive to shut it up. `dismissedUntil` is already
the right shape for this.

---

## L. The memory file *(planned, last)*

### L.1 The model does not learn

Nothing is trained; nothing persists inside the model. "Memory" is text
re-sent with every request. That framing is not a disappointment — it is what
produces the two rules that follow.

### L.2 Rule 1 — if it can be derived, it does not go in memory

§3.5 (derive, don't store), applied:

| Belongs in memory | Never in memory |
|---|---|
| "I hate mornings, nothing before 09:00" | blocks, tasks, deadlines |
| "Exam period until Dec 15" | what was tracked yesterday |
| "Call my mother Sunday, she worries" | anything the briefing computes |
| "Building a C++ portfolio for internships" | task titles and due dates |

Break this and the memory file becomes a stale, contradictory shadow of the
planner — the number-one failure mode of memory systems. The briefing is
regenerated every turn *precisely* so it cannot go stale; memory must not undo
that.

### L.3 Rule 2 — memory is billed every turn, so it is updated, not appended

An append-only file eventually costs more than the conversation and crowds out
the actual question. "Breakfast at 08:00" followed later by "moved to 07:30"
cannot both live in the file — the model will see a contradiction and pick one
at random. **Entries are replaced.**

### L.4 Which makes memory writes tool use

If entries are created, edited and removed, they go through the **same proposal
→ confirm loop** as every other mutation. Do not build a second mutation system.

### L.5 Shape and location

**Structured sections** (Routines · Preferences · Current situation · People),
not freeform prose — sections are trimmable; prose drifts into a diary.

*Location, genuinely arguable:*

- **Sidecar `memory-<username>.md`** — no format bump, no migration, plain text
  the user can open and correct by hand. Does not sync. **Currently preferred**,
  because "a file you can read and fix yourself" is a trust feature.
- **Inside `data.json`** — syncs to every device, costs a format bump.

---

## M. What we deliberately will NOT build

Recorded so the decision does not get re-litigated. Larman's warning applies to
the versions of these that *sound* smart: *"if it is for speculative
future-proofing… restraint and critical thinking is called for."*

- **A second model for "executing changes."** The executor seat is already
  filled by code — `nlp::parseQuickAdd` plus guarded `AppData` doors:
  deterministic, offline, free, microsecond-fast, tested. Non-determinism is a
  cost accepted where necessary, not added for symmetry. *(Revisit only with a
  named failure the single model actually produces.)*
- **Hardware / VRAM detection and auto-tuning.** Vendor-specific, brittle, and
  it answers a question answerable by trying (§E.1).
- **A model capability registry.** That is maintaining a database of other
  people's model names; they age faster than it would be updated.
- **Syncing AI settings between devices.** Actively wrong — the whole point is
  that machines differ.
- **Per-user AI profiles.** One user per machine; two if she likes it.
- **A model that decides when to interrupt.** §A.
- **An undo button** — as long as §B.1's verb discipline holds.
- **Streaming**, until something needs it. It remains the other §C promotion
  trigger.

---

## N. Sequencing

Dependency-driven, and deliberately front-loading work that has standalone
value. Version numbers are indicative, not promises.

| # | Iteration | Contents | Blocked by |
|---|---|---|---|
| **v26** | ~~**AI foundation**~~ **SHIPPED** as three drops | ✓ reasoning models (v25.2, provider §L) · ✓ persona (v25.3, chat §K) · ✓ routing (v26.0, provider §M) | — |
| **v27** | **Subtasks & sizing** *(no AI)* — built, **never landed**; **re-landed fresh as v28.3.0 (format v13)** | ✓ subtasks (§I) + `estimateMinutes`/`chunkable` (§J.1) → `design-addendum-subtasks.md`. **Done, v28.4.0:** the multiplier (§J.2) + `affordability()` rewired off its proxy (estimate-first, proxy fallback) | nothing |
| **v28** | **The proactive assistant** *(read-only)* — **SHIPPED through 28.2** (28.3 = the subtasks re-land, above) | nudges (§F), morning check-in + mood (§G), affordability *phrased* by the model — on the planned-blocks proxy, as §N always allowed; the proxy retired in v28.4.0 (kept as fallback for unestimated tasks) | v26 ✓ |
| **v29** | **Tool use** | the confirm loop (§B), intake first (§K), then rescheduling; dialect strategy promotion (§B.3) | v28 |
| **v30** | **Memory** | the residue file (§L), riding v29's confirm loop | v29 |

**Notes on the order:**

- **v26 is unblocked** and independent of everything below it.
- **v27 has no AI in it at all.** It is a domain iteration that happens to
  unlock two AI features. It would be worth building regardless.
- **v28 before v29 is deliberate:** the whole check-in / affordability
  experience works *read-only* — the assistant proposes, the user moves things.
  v29 only removes the manual step. Living with a read-only assistant for an
  iteration is the cheapest way to find out whether its judgment is worth
  trusting with a write.
- **Only estimation strictly needs subtasks.** If nudges are wanted sooner,
  v28 can precede v27 at the cost of affordability using the planned-blocks
  proxy (§H.3) for one iteration.

---

## O. Open questions

Recorded rather than guessed at:

1. ~~**Does the assistant volunteer an affordability verdict, or only answer when
   asked?**~~ **CLOSED, v28.0 — volunteer.** The owner: "I want it to be my
   secretary and assistant… give me a good heads up on things." The
   helpful/nagging line is held by §F.3's manners instead of by asking-only:
   change-of-verdict, quiet hours, a cap of 3, dismissal respect — and only
   *Tight* volunteers. Recorded in `design-addendum-affordability.md` §A.
2. **Do subtasks carry their own deadlines and categories? Can they nest?**
   (v27's first design decision.)
3. **Completion roll-up:** does finishing every child complete the parent?
4. **Memory location** — sidecar vs. `data.json` (§L.5).
5. **Persona presets:** which three or four ship as defaults?
6. **Check-in storage beyond 14 days** — is longer-range mood pattern work
   worth the retention, or is 14 days the honest limit?
7. **Does memory partition by role?** (§L, §E.2.) One residue file serving both
   the work planner and the morning check-in re-mixes exactly the mood data
   that §E.4 deliberately pinned to the local seat: the fact never leaves the
   machine, but its *residue* would ride along in every cloud turn. OpenClaw
   runs isolated agents partly for this "bleed" reason — one memory serving two
   contexts eventually produces crossovers in tone and detail. Against that:
   one file is simpler, and memory is billed every turn (§L.3), so splitting it
   is not obviously cheaper. **Undecided until §L is actually built** — but if
   it is decided by default, it will be decided wrongly, so it is written down
   here.
