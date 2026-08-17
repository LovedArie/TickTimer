# Project Log — TickTimer

*The running record the mentor keeps (per the working agreement): what was
built and its state, the C++/Qt concepts each arc demonstrates, and the
decisions worth remembering. Coarse for the early arcs, detailed for the
recent ones. **Updated at v28.2.1** — the v28 proactive-assistant arc,
the documentation audit, the v27 reconciliation, and the first field
report from a real day of use.*

---

## The project in one paragraph

A C++17/Qt 6 desktop planner-tracker: plan the day in 30-minute blocks,
track focus/break/distracted time inside each block, and see plan vs
reality. Grown across ~27 versions from a single-window prototype into a
multi-page app with tasks, recurrence, reviews, accounts + self-hosted
sync, an AI assistant layer (provider-neutral, offline-testable), two
review features (needs-a-block, catch-up), and a **proactive assistant**
that volunteers deadline verdicts and runs a morning check-in — all
read-only, all with a working non-AI path underneath. ~300 automated
tests across six suites. One JSON file of truth (`data.json`, **schema
v12**) plus QSettings for per-device taste. 30 design addenda, 26
diagrams, 224 question-bank entries.

## Architectural spine (established early, never overturned)

- **Derive, don't store** — every number recomputed from raw Segments;
  judgements derived, decisions stored. Paid out repeatedly (reviews for
  free; catch-up's undo and retroactive recovery cost zero wiring).
- **One aggregate root** (`AppData`) with named doors per operation;
  `changed()` as the single repaint pipeline; const-correctness as
  architecture (widgets hold `const AppData*`, ask via signals, pages
  decide).
- **The nowProvider doctrine** — time is injected everywhere, which is why
  gates, flags, verdicts, and chips are all testable at a fixed moment.
- **Taste vs facts** — QSettings vs data.json, decided per field by "should
  this sync?". v28 proved it a real criterion rather than a habit by
  answering it *both ways in one arc*: mood → data.json (a fact about the
  life), the nudge courtesy-ledger → QSettings (syncing it would actively
  misfire — a laptop's nudge must not mute the phone in your hand).
- **The AI spine (v25→v28)** — *code decides **when**, code computes **what
  is true**, the model only **phrases***. Its corollary became structural
  in v28: every AI feature ships its **no-model path first**, so the model
  can only ever be an enhancement. v28.0 shipped the whole nudge pipeline
  with C++ sentences; v28.1 swapped one box.
- **Mechanism over intention** — the session's most-repeated finding. The
  `.rc` files never drifted (they `#include` the version); the installer
  drifted five releases (a human retyped it). Applies to docs, privacy
  rules, and apply-steps alike: a comment that says MUST is not a
  mechanism.

## Arc summaries

| Arc (versions) | What shipped | Concepts it demonstrates |
|---|---|---|
| Core planner (≤v13) | day agenda, tracking, crash-safe heartbeat, reviews | custom-painted widgets, QPainter charts, derive-don't-store, JSON round-trip |
| Tasks & organisation | tasks/deadlines, folders, special days, Upcoming | domain modelling, sorting policies, archive semantics |
| Networked arc (v15–v19) | accounts, self-hosted server, sync, share | client/server split, auth, conflict rules |
| Model/View + AI (v20–v25) | QAbstractListModel lists, NL quick-add, provider layer, chat assistant | model/view with granular diffs, vendor-neutral AI contract, offline AI testing, prompt banding |
| Needs-a-block (v21–v22) | coverage rules, gated review, escalation, placement, SlidePanel drawers | pure rule modules, ReturnPolicy reuse, fingerprint-gated rebuilds (the v22.2 click-eater), drawer overlays |
| **Settings nav (v26.1)** | shell + pages, `save()` as a loop | junk-drawer refactor; contract-by-abstract-class; eager pages defending `findChild` tests |
| **Catch-up (v26.2–v26.8)** | the whole missed-blocks feature | the richest arc; §L of its addendum is the retrospective |
| **Docs audit (v26.8)** | two real version bugs, 11 diagrams rendered for the first time, 6 Mermaid→PlantUML, the `app_architecture` overview | *documentation as an engineering surface*: what a `static_assert` structurally cannot check; shell loops as a compiler for prose |
| **Proactive assistant (v28.0–v28.2)** | affordability verdicts + volunteered heads-up, model phrasing with a guaranteed fallback, morning check-in + mood | **the current flagship** — see below |
| **v27 reconciliation** | subtasks: designed, built, never landed | the apply-check ritual; when a human error repeats, find the missing check, not the careless person |

## The catch-up arc, in detail (closed at v26.8)

**State: complete and closed at v26.8.** Feature summary lives in the
addendum's §0; the process retrospective in §L.

- **v26.2** — domain core (`missed::`, `reschedule::`, `BlockOutcome`,
  two AppData doors, schema v11) + surfaces (card, settings page, briefing
  section). *Concepts:* pure-function modules, ranked options with an
  honest empty result, planning against the shortfall, one-direction
  chain links, use-after-append (`QVector` reallocation), quiet-append for
  observer-atomic double mutations.
- **v26.3** — real data (46 blocks): layout rule "one variable label per
  row", bulk door with **one** `changed()`.
- **v26.4** — Undo receipts; undo-not-confirm for a friction-sensitive
  user; write-receipt-before-deed ordering.
- **v26.5** — Bring-back chip *derived* from data → reaches accidents that
  predate it; `resolvedAt` deliberately not stored.
- **v26.6** — "hidden ≠ gone": snooze markers; recovery never obeys the
  snooze.
- **v26.7 (+.1–.7)** — B′ redesign, HTML-prototyped: one chip, three
  intensities; snooze = de-emphasis, not a lock; drawer holds every verb;
  pie retired. Then the layout-physics codas: framed voids, dash buzz,
  the QSS radius clamp (twice — pin heights), the scroll-area hint lie on
  both axes, the implicit drawer-host break, the stale-print reopen bug.
  Two doctrine rules emerged and each caught a second bug:
  *cached-rendering state must be invalidated through the widget* and
  *neutralise an unreliable hint on every axis*.
- **v26.8** — look-back default 7→3: **defaults are design**; one test
  broke by straddling the default (pin what a test depends on).

**Cross-arc echoes worth quizzing on later:** catch-up's chip/drawer is
needs-a-block's v22.9 pattern reapplied; its Rule/prefs split mirrors
coverage's; its fingerprint gates inherit (and twice repaid) the v22.2
scar; `SettingsPages` cashed its promised one-line page in v26.2.

## The v28 arc, in detail (current focus)

**State: complete and shipped at 28.2.1 — running on the owner's machine,
with a first field report in hand (see defects below).** Three slices,
cut so each one had standalone value before the next depended on it.

### What shipped

| slice | what it does |
|---|---|
| **28.0** | `afford::affordability()` — a verdict per deadlined task from *your own plan* (`outstanding = planned − tracked` vs `blocks-ahead + free daytime`). Four values, not a score. A TIGHT pill on Upcoming; an alert toast when a task *turns* Tight. **No model anywhere in it.** |
| **28.1** | `nudge::` + `NudgeClient` — the assistant phrases the heads-up in your persona; the C++ sentence remains the guaranteed voice. The briefing gained DEADLINE PRESSURE so the chat can answer "can I afford to go out?" |
| **28.2** | `Mood` (format **v12**), 14-day retention on the midnight knock, the stingy check-in gate, and the surfaces: a toast whose action opens the chat with the question waiting, answered in one tap |

### C++/Qt concepts it demonstrates

- **Pure/wire split, pushed further than anywhere else in the project.**
  `afford::`, `nudge::`, `checkin::` are header-only, Qt-value-only,
  clock-injected; the services are timers and signals with no judgment in
  them. The CMake `DOMAIN_SOURCES` list holds *only* the pure headers —
  and enforcing that forced a real design improvement (below).
- **A role derived from outside the snapshot.** `AffordabilityRole` moves
  when *events* change while `Task` rows stay byte-identical, so
  `TaskSnapshotModel`'s field diff can't see it. `TaskListModel::refresh`
  diffs a verdict map itself and emits the `dataChanged` the base cannot
  know to. **Transferable rule:** a role computed from data outside the
  snapshot needs its own change detection.
- **`std::function` injection as an architecture tool**, not test
  ceremony — see the link-graph story below.
- **Value types over parameter lists** — `ToastSpec` follows `ai::Provider`:
  new kinds of toast are new *values*, not new overloads.
- **Seams without speculation** — four toast joints built for a "distant
  future" animation, and zero animation written. YAGNI is right about the
  motion and wrong about the joints.
- **Exactly-once async with supersede-by-silence** — the generation
  counter, third use; the transfer timeout *is* the timeout, so there's no
  parallel `QTimer` to race.

### Decisions worth remembering

- **§O.1 closed by the owner: volunteer, not answer-only.** ("I want it to
  be my secretary.") That reshaped the slice — volunteering turns a query
  into a nudge, so §F's pipeline moved into 28.0 *model-less*, which is
  what made 28.1 a one-box swap.
- **Manners are the feature, not polish.** Change-of-verdict, quiet hours,
  a cap of 3, dismissal respect. For an ADHD-facing tool, "gets muted" is
  total feature failure — so the rule that decides survival is
  *Comfortable→Tight is news; Tight→Tight is nagging*.
- **Unknown is a verdict.** No blocks ever planned → "I can't tell how much
  is left", and it never toasts. An assistant admitting ignorance is the
  feature; performing confidence is the bug.
- **Bookkeeping timing, decided per promise — twice, oppositely.** The
  nudge marks at *delivery* (its promise is about speech you received); the
  check-in marks at *emit* (its promise is about asking once, and a
  dismissed toast was still an ask).
- **The include graph is architecture.** Having the service call
  `chat::configuredPersonaBand()` directly would have dragged ChatSession +
  Qt Network into `DOMAIN_SOURCES`, undoing the test split the CMake file
  celebrates in a comment. One `std::function` with a valid default fixed
  it. A one-line convenience that adds a link edge between test targets is
  not one line.
- **A privacy rule needs a mechanism.** §E.4 ("mood never leaves the
  machine") existed as planning prose — and part 1 shipped a briefing that
  sent mood to any seat, cloud included. Part 2 replaced the prose with a
  default (`includeMood = false`, so new call sites are private by
  accident) and a predicate (`ai::isLocal`, loopback-only, because a LAN
  box is still a wire). **The honest entry in this log: we wrote the rule,
  then broke it within one session of writing it.**

---

## Field report — first real day of use (v28.2.1)

*The most valuable page in this log, because none of it came from a test.*

| # | finding | diagnosis | fix lives in |
|---|---|---|---|
| 1 | **Markdown renders literally** — `**bold**` and ``` fences show as characters | `addBubble` builds a plain `QLabel`; `Qt::AutoText` sniffs for HTML, not markdown | `ChatPage::addBubble` — one line: `setTextFormat(Qt::MarkdownText)`. Markdown, not RichText: it can't carry `<img>` or scripts, so it's a narrower surface for model output |
| 2 | **"I don't have a plan for tomorrow"** | Correct and honestly reported — `dayBriefing` is *today-only*. Tomorrow's blocks exist in `AppData` and never enter the context. `Options.upcomingDays` covers task *deadlines*, not *blocks* | `DayBriefing.cpp` — a TOMORROW (or lookahead-N) block section |
| 3 | **"It didn't realise my day ended"** | **A spine violation of our own making.** We hand the model `Local time now` plus block timestamps and expect it to *infer* the phase — while §A says models have no clock and can't do arithmetic dependably | `DayBriefing.cpp` — state the phase as a computed fact: blocks remaining, last block end, day-over yes/no |
| 4 | **Self-contradiction:** "none of that time was logged" then "1h03m focused" | Almost certainly the same root as #3 — per-block tracked vs day-total tracked, reconciled by the model instead of by us | same section; disambiguate the two numbers |
| 5 | **"31 to catch up"** in the glance panel | Not a bug: 13h planned against ~1h tracked, over a 3-day horizon. But it's **v26.3's lesson recurring** — real data breaks assumptions | a design conversation, not a patch: is the horizon right, should the chip cap what it offers |

**The pattern across #2, #3 and #4:** all three are *briefing content*
gaps, none is a prompt or model problem. The context is the product. When
the assistant is wrong, the first question is "what did we fail to tell
it?", not "how do we word the prompt?"

**The omission behind the whole report:** every one of these services has
injection seams — `setNowProvider`, a public `sweep()`,
`setProviderOverride`, `TICKTIMER_AI_DOWN` — and **none is reachable from
the running app**. The owner can't force a check-in (it needs 06:00–11:00
*and* a heavy day), can't skip the 20-minute sweep, and has never seen the
v28.0 C++ voice, because a working provider always wins. *Seams only tests
can reach are half a seam.* A debug menu is the first item next session.

## Open threads (deliberate, documented)

- Bump acceptance as one tap (cascade problem — §K scope note).
- Global rearrangement (constraint solving — §G non-goal).
- Screenshot harness for visual regressions (§L's counter-analysis: the
  next fix for an invisible bug class is a new kind of test).
- The card's four visibility tenants → a state table on the next touch
  (§K.4 note).
- SQLite storage, Android build (roadmap, untouched by this arc).

## Next

**Immediate (the field report):** a debug menu reaching the seams, the
one-line markdown fix, the briefing's day-phase + tomorrow sections, and
`docs/TESTING.md` with a force-recipe per v28 feature.

**Then, per roadmap §N:** v29 — tool use (the confirm loop; intake first,
as the smallest possible blast radius), or the **subtasks re-land**, which
v29's intake would prefer to already exist.

**Portfolio framing.** Two exhibits, and they show different things. The
**catch-up chapter** (addendum §0–§L) is a feature meeting reality and
hardening across twelve versions, every deviation narrated. The **v28 arc**
is the opposite skill: an AI feature built so the AI is optional, with the
fallback shipped first on purpose — plus a documentation audit that found
two real bugs, and an honest record of writing a privacy rule and breaking
it in the same week. The second one is rarer on a CV than the first.


---

# v28.5 — the piece's own panel (one slice, one session)

**What was built.** The pieces polish headline: a checklist piece's title
now opens the piece's own full detail panel (date, time, size), with a
"‹ parent" breadcrumb back up and a quiet "Aug 8 · 45 min" chip on rows
that carry scheduled work. UI-only — no domain or format change.

**C++/Qt concepts it demonstrates.**
- *Navigation-as-answer*: the pure-question dialog contract extended to
  session navigation — the dialog records `navigationTarget()`, a free
  function acts on it. Compare Project 2's observer split, one level up.
- *Loop over recursion for modal chains*: `runTaskDetail` closes one
  `exec()` before opening the next — flat stack, single live dialog,
  one seam for the future modality swap.
- *Ids over snapshots*: re-reading by id each hop retired four defensive
  `Task` copies — the same point-up-don't-embed instinct as `parentId`,
  applied to UI lifetime instead of storage.

**Notable decisions.** Click-through ACCEPTS (the hop saves the sitting;
only Cancel discards). Newborn rows get no door (no id yet — the honest
UI). The modal close-and-reopen hop is recorded debt for the side-panel
slice. Drift caught in passing: README still wore the v27 "never landed"
banner over a feature that shipped in 28.3 — banners now join the
post-ship grep sweep.


---

# v28.6 — the docked detail panel (the seam pays out)

**What was built.** Task details moved from a blocking modal to a docked
right-side panel: the app stays live beside it, piece navigation swaps
in place, and saving is EXPLICIT by owner decision — lit Save button
(honest dirty-by-comparison), "Saved ✓" flash, Save/Discard/Stay guard
on every exit over unsaved work, Enter never discards.

**C++/Qt concepts it demonstrates.**
- *Extract-widget refactor with a compatibility wrapper*: TaskDetailForm
  pulled out of TaskDetailDialog; the dialog kept as a thin modal wrapper
  so the entire existing suite passed UNCHANGED — the refactor's proof.
- *Policy at the container, mechanism in the widget*: one
  navigateRequested signal, two meanings (record-and-accept vs. guarded
  swap). Compare v28.5's navigation-as-answer — this is that idea
  promoted from a value to a seam.
- *Qt object lifetime under self-referential signals*: deleteLater +
  queued connection as the PAIR that makes swap-in-place safe (V239).
- *QPropertyAnimation on maximumWidth* for the slide; a width-0 widget
  as "closed" — no show/hide choreography.
- *Injected-prompt test seam* (function<Choice()>), with the honest
  half hand-walked in QA per the v28 field-report lesson.

**Notable decisions.** Explicit save over TickTick's instant commit —
the owner's call, recorded with its grammar. Dirty form wins the race
against external changed() (last-write-wins, the app's standing sync
answer). runTaskDetail's three-argument signature survived its second
redesign with zero call-site changes — the log's clearest example yet of
"depend on the question, not the answer's shape."


---

# v28.6.1 — docked → overlay (feel bug, same-day fix)

**What was built.** The detail panel left the layout: overlay child +
click-away scrim + position-based slide + 440 px. Owner feedback within
hours of 28.6.0 — "reads as part of the Activities panel" and "shrank
the main screen" are one fact (layout members compete for space), fixed
categorically.

**C++/Qt concepts.** Overlay pattern in plain widgets: manually
positioned child + host event filter for resize-follow; scrim as both
visual layer and click target; WA_StyledBackground (V243 — the trap
that only detonates when widgets overlap); geometry animation replacing
width animation so content never reflows.

**Notable.** The guard generalized: every exit — ✕, Esc, scrim —
through one Save/Discard/Stay door. And the arc's recurring thesis
scored again: feel bugs are invisible to offscreen tests; only usage
finds them.


---

# v28.6.2 — the patchwork panel (one line, one lesson)

QScrollArea::setWidget() re-armed the Theme.h v3 trap
(autoFillBackground flipped ON) and the form painted palette grey
inside the white overlay. Fix: OFF after every setWidget. The lesson
that outranks the fix: second re-hit of a documented landmine in this
codebase — so the tripwire got upgraded from a comment (stops patches
that read it) to a test (stops patches, period). Owner verdict logged:
the unsaved prompt is LIKED — guard frequency question closed.


---

# v28.7 — pieces in the list (learning from the reference app)

The owner A/B-ed TickTick and TickTick won: pieces now live in the
category list as indented rows, created by right-click, named through
the panel with the title pre-selected. Concepts: display vs. counting
policy (the §D amendment — structure is not workload); interleave
placed after the sort so families never split; the single-geometry rule
(indent in geometryFor = paint and hit-tests move together); the
create-first tradeoff (V247). And a test that HUNG was itself the
lesson: the modal fallback's exec() blocks — a real cost of the
fallback path, now documented where it bit.


---

# v28.8 — the size ladder

Estimate spinbox → non-uniform dropdown (15m–16h), cap-as-doctrine
(past two workdays: pieces). Concepts: pickers should match the useful
density of their domain, not a uniform grid; opening-is-not-editing as
a testable contract (sorted insert of off-ladder values, isDirty false
after seed); formatter consolidation at the third caller (minutesLabel
core, durationLabel delegates) — the "second consumer" extraction rule,
observed at N=3 for formatting.


---

# v28.9 — promotion (the counting closes)

One trigger — the piece's due date — makes counting agree with what the
sweep already does: a dated piece answers for itself, its minutes leave
the parent (subtract/floor for sized parents, borrow-only-undated for
unsized). Concepts: derive state from observable facts instead of
storing a flag (no migration, no contradiction); floor-at-zero as the
inverse-lie guard (V251); additive amendments proven by old tests
passing unchanged (v28.4 borrow test, untouched — the 28.6 wrapper
proof's domain twin). The FINALS case from the owner's own screenshot
is the headline test. Ordering rationale on record: arithmetic before
v29's hands, so the assistant inherits numbers that don't lie.


---

# v28.9.1 — the ladder learns to scroll

One line: setMaxVisibleItems(6) caps the 26-rung dropdown's popup at
six scrolling rows (the sub-hour zone visible, the rest one flick
away). Concept for the log: Qt's "hint" properties — the cap is
advisory and fully native popup styles may ignore it, so the claim is
pinned twice: a unit test for the property, a QA line for the pixels.


---

# v28.10 — the seams, reachable (the field-report slice)

The whole first-field-report punch list, shipped as Slice 0 before v29:
the debug panel (Ctrl+Shift+D — frozen clock, sweep-now, the check-in
rehearsal, manners resets, a live briefing viewer), the briefing's three
content fixes, assistant markdown, and the forcing hook reaching every
wire. Concepts for the log: **the panel-is-glass rule** (a debug surface
with zero judgement — presses tested seams, never reimplements them);
**rehearsal semantics** (forceOffer skips the gate, not the script, and
spends nothing — a debug tool must never surprise the real feature);
**resets owned by key-owners** (services grew `forgetManners` /
`clearTodaysAsk` rather than the panel spelling private QSettings
prefixes — a comment is not a mechanism, third telling); **the wildcard
over the enumerated list** (`TICKTIMER_AI_DOWN=*` — one catalog, not
two); and the briefing's **fifth anti-hallucination rule** (*computed
facts are stated, never implied* — chat addendum §C.1), which turned all
three field findings into sections: DAY STATUS, PLAN FOR TOMORROW, and
the disambiguated day totals. The mechanism find worth remembering:
"has never heard the v28.0 voice" wasn't a UI gap — `forcedDown` was
only consulted by the chat's route walk, so the single-seat wires
*could not* be forced down. Fixed at the wire, with per-wire manners
(nudge falls back silently; quick-add names the cause). 359 tests across
six suites; format v13 untouched — exactly what "the panel decides
nothing" predicts. Drift caught in passing: README wore "format v11"
over a v13 tree — fixed, and the format line joins the README ×2 sweep.


---

# v29.0 — the write boundary (Slice 1: the machine, no model)

The milestone crossed at its narrowest point: the assistant's first
hands exist and move only on the owner's tap — and no model exists
anywhere in the slice, because the debug panel plays one. Concepts for
the log: **machine before model** (the v28 no-model-spine doctrine
applied to writes: Slice 2's model arrives as a new *caller* of a
guarded path, not a new path); **the one-screen security review** (the
per-role allow-list is a single switch — any diff to AssistantVerbs.h
is the complete review; Nudge and CheckIn hold empty lists, so a
prompt-injection in a toast has nothing to reach for); **trust ≠
routing** (verbs::Role is deliberately not ai::Feature — folding the
axes would let a routing edit widen a trust scope); **fail-safe
strictness** (invented handles die to "" and a readable refusal, never
a fuzzy hit on the wrong task; the role check runs first so refusals
leak nothing); **the additive rule** (absence uses the domain's own
idioms; priority excluded because Medium is a value, not a blank —
overwrite is a different verb and arrives as one); **two verdicts, two
moments** (re-validate at the tap: the stale-card scene refuses and the
owner's by-hand value survives — pinned); **glass, third telling** (the
card shows, emits, decides nothing; its summary is composed from the
request's own fields, so the description IS the request); **the record
vs the live UI** (receipts are localOnly transcript turns; discards
record nothing; cards survive neither rebuild nor restart); and **§B.3
answered by recording** (Dialect promotion deferred with the reason
written down — single-shot C++ proposals hold no cross-call state; the
criterion should fire in Slice 2, and if it doesn't, that gets recorded
too). 366 tests across six suites; format v13 untouched — the boundary
adds zero stored state, and Slice 2's "ask once" already has
dismissedUntil waiting from the return-policy arc. Drift caught in
passing: README's addendum count said twenty-four over a
thirty-four-file folder — the second hand-counted number to rot there,
fixed; the honest fix (deriving it) noted for a docs session.

**Postscript — v29.0.1, the first field find of the v29 era.** The
second-machine setup (the owner's girlfriend's laptop) hit "Please check
your details and try again" on Create account: a trailing slash in the
pasted server URL → `//register` → exact-match 404 → a catch-all that
blamed the credentials. Reproduced live against the real server before
any fix; then one shared normalizer at every URL entry (AuthClient ctor
+ setter, SyncClient ctor — the second consumer would have hit the same
landmine in sync, silently), plus a new UnknownServerReply outcome so
the unforeseen case names itself instead of impersonating a typo. Three
tests pin it, including the exact failing call end-to-end. Two log-worthy
lines: *when a user can paste it, the program normalizes it*, and *error
taxonomies earn their keep at the catch-all*. 369 tests; both version
files bumped together, as is now reflex.

**Postscript 2 — v29.0.2, the slash one layer deeper.** Share & compare
blamed the owner's girlfriend's spelling for a route-level 404: the
v29.0.1 fix normalized inside AuthClient, which let login SUCCEED with a
slash-bearing URL and save the raw value — arming the consumer the patch
missed (ShareClient) within hours. Two lessons earned the log. **Fix
where the value is born**: LoginDialog::serverUrl() now normalizes at
the source; every consumer inherits; the per-consumer normalizations are
demoted to defense in depth. A consumer-side fix for a source-side
problem doesn't just under-fix — it can actively arm the next failure by
teaching the nearest symptom to pass. **Second conviction for the
taxonomy crime**: ShareClient's classifier collapsed "no such user" and
"no such route" into one owner-blaming message, with a comment
rationalizing it; the body was parsed and the distinction was one
comparison away. When a message can blame either the user or the app,
the app must do the work to know which. 371 tests; the exact failing
share, the honest typo, and the named wrong-path all pinned.


---

# v29.1 — the interview (Slice 2: the model joins intake)

§K paid in full, and the model finally in the loop — as a proposer, and
nothing more. Concepts for the log: **the division of labour drawn
hard** (triage, question, guess, and crisp parsing are C++; the model's
entire job is prose → two nullable fields; the interview works with
every AI seat down — the v28 fallback doctrine now covers a write
feature end to end); **extraction over the tool-use API** (§B.3's
second recorded non-firing, this time by design: provider-neutral to a
local Ollama, single-shot by construction, and the confirm loop itself
is the tool layer — an inspectable dispatch with the owner as
dispatcher); **no convenience exceptions** (the guess button crosses
the card too — the boundary means nothing once trusted paths may skip
it); **three lifetimes around one question** (Skip = the owner's door,
a year; askedThisSession = this conversation; discard = this card
only — inferring dismissal from discard would write state a gesture
never meant); **the label is the license** (a two-sample spoken guess
vs the rate's higher silent bar — same scan, different failure modes);
and **suite structure with teeth** (the linker forced the one-header/
two-TU split by dependency group; test_nlp's purity charter rejected a
prompt that took AppData to format one name — drift caught as compile
errors, the cheapest place it is ever caught). On the record with equal
weight: one commit briefly held two extraction layers because the
author extended the tree from plan notes instead of re-reading it —
the header was the abstract; the live line, not the recollection, is
the truth. 379 tests across six suites; format v13 untouched — the
whole interview added zero stored state.
