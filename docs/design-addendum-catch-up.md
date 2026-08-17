# Design addendum — catch-up: blocks that didn't happen (v26.2 → v26.8)

*Part 1 is the DOMAIN: `Event::outcome`, `missed::`, `reschedule::`, the
storage round-trip and the two `AppData` doors. Part 2 is the surfaces (the
catch-up card, its settings page, the briefing). Nothing in this document
touches a widget, and that is the point — every rule below is testable
headless, in microseconds.*

---

## §0 — The feature as it stands (v26.8)

*This document is a narrative — it records the design being argued, shipped,
and corrected, five codas included. Read it that way for the lessons. But if
you just need to know what the feature IS today, this section is the whole
answer; everything below is how it got here.*

A planned block whose window closes with under **50%** of its time tracked
as Focus (threshold yours, ⚙ → Catch-up) and whose date is within the last
**3 days** (default since v26.8) is *unresolved* — derived, never stored.
A quiet **chip** on the glance panel carries the count: **amber** at the
morning/evening moments, **faded gray** while snoozed or between moments
("31 · back 22:30"), absent when there's nothing to say. The chip always
opens a **drawer** — snooze governs attention, never access — holding, per
block: the verdict in plain words, a **pre-filled proposal** from the
ranked ladder (free slot → split → shorten → *named conflict* → past the
deadline; possibly the honest empty hand), **Done** and **Skip**, plus
**Skip all N**, the **Undo receipt**, a **Bring back** section for
recently-resolved blocks, and **Later**. Decisions land on
`Event::outcome` (+ `movedToId`, one direction), sync, and survive disk
(schema v11). The AI briefing reports the unresolved gap. Nothing is ever
moved without a tap; every tap is reversible; hidden is never
indistinguishable from gone.

Key files: `MissedBlocks.h`, `Reschedule.h`, `CatchUpCard.h/.cpp`,
`AppData::resolveBlock/resolveBlocks/rescheduleBlock(/Split)`,
`CatchUpSettingsPage`. Diagrams: `catch_up_ladder`, `catch_up_surfaces`,
`catch_up_chip_states`. Question bank: V108–V170.

---

## A. The failure this feature exists to prevent

An `Event` is an INTENTION; its `Segment`s are REALITY. Keeping both,
separately, is the app's founding idea (`design-doc` §3.2). Until now
nothing ever asked whether the two matched.

A block whose window passed with nothing tracked simply sank into history.
Overslept, priority shifted, forgot — the plan quietly became fiction and
the app kept a straight face. `coverage::Reason::BlockInPast` came closest,
but it only fires for *tasks* that are flagged, so a missed `Gym` block on
an Activity had no safety net at all.

Four failure shapes hide behind "I didn't do it", and they do not want the
same answer:

| Shape | What it wants |
|---|---|
| Never started | move the whole block |
| Partially done | move the **remainder**, not the whole thing |
| Done but never tracked | log it, or just mark it done |
| The whole morning is gone | one bulk decision, not six |

Collapsing these into a single "reschedule missed blocks" button is the
obvious v1 and it would be wrong. The distinction between the first two is
encoded in `missed::Reason`; the third is `BlockOutcome::Done`; the fourth
is a part-2 UI concern.

---

## B. The dividing line

> **The judgement is derived. The decision is stored.**

- **"This block was missed"** is a pure function of the planned window, the
  focus segments inside it, and `now` (`missed::judge`). Nothing on disk.
  Move the threshold from 50% to 70% in Settings and every block in history
  re-judges on the next read — no migration, no stale flag to repair, no way
  for a stored judgement to disagree with the segments it came from.
- **"What I decided about it"** is not derivable from anything. No amount of
  staring at segments tells you the user deliberately skipped the gym rather
  than forgot. So `Event::outcome` is a real field, it syncs, and it lands in
  `data.json`.

This is the same split `coverage::rung` (derived) makes against
`Task::dismissCount` (stored), and it is the reason both features can change
their thresholds without a migration script.

---

## C. What counts as "missed"

```
focus seconds  <  minPercent %  of the planned window
```

Three deliberate choices inside that one line:

**Focus only.** Break time inside a block is legitimate but isn't the work;
Distracted time is explicitly lost time. Counting either would let a block
full of procrastination pass as done. (`stats::eventTotals` keeps the full
three-way split — the reporting screens ask a different question.)

**Integer maths on seconds**, never floating point on percentages. The
comparison is exact and cannot wobble on a value like `0.4999999`.

**A horizon** (`lookBackDays`, default **3** since v26.8; shipped at 7 and
lowered after the owner met a 46-block wall — the card recovers the
*recent* past, and a week-old miss gets re-planned, not rescheduled). Without it, reinstalling the app
or returning from a fortnight away greets you with four hundred unresolved
blocks — a wall of guilt nobody triages, which means the feature gets
ignored, which means it may as well not exist. Old failures are history, not
a to-do list. Note that `judge()` still reports the failure past the
horizon; only `isUnresolved()` filters it out. The two questions stay
separable so the evening review can still say what happened.

**Not a knob:** whether break time counts. Every relaxation of that rule is
a way for the app to call a block done when nothing happened — the same
reasoning that keeps `coverage`'s deadline rule out of Settings.

---

## D. Propose, don't move

The app computes a placement and shows it pre-filled. Nothing changes until
the user accepts.

**Why not auto-move.** A calendar that silently drags yesterday's blocks
into today starts lying: you look at Tuesday and see a plan you never made.
A week of oversleeping cascades into a schedule you don't recognise, and the
app becomes something you distrust and stop opening.

**Why not manual-only.** "You missed 6 blocks, here are 6 decisions" is an
executive-function tax levied at exactly the moment there is none. That is
how features like this die.

**The resolution:** one tap per block, or one tap for all. The proposal costs
nothing to accept and nothing to ignore.

---

## E. The ladder

`reschedule::propose()` refuses to return a single answer. The naive shape is
`QDateTime findSlot(...)` and it collapses the moment the week is already
full — which, for anyone who actually plans, is the normal state.

A missed block with a deadline two days out and no free time between here and
there is not a puzzle with a hidden solution. It is a **conflict** between
three things the user wants:

```
        the deadline
       /            \
the full block --- the other blocks
```

Something has to give, and only the human can say which. So the module
returns a ranked list, cheapest first:

| Rung | Offer | Cost |
|---|---|---|
| `FreeSlot` | it fits, whole, before the deadline | nothing |
| `Split` | the fragments add up | context switches |
| `Shorten` | less time than you owed | scope |
| `Bump` | something else gives up its slot | the other block |
| `BeyondDeadline` | there IS room — after the deadline | the deadline |

And it is allowed to return **nothing**. An honest dead end beats a fake
solution: it tells you something true about your week that you'd otherwise
discover on deadline day.

Three details that matter more than they look:

**It plans against the SHORTFALL, not the original duration.** Re-offering
the full 90 minutes to someone who already put in 40 double-books time they
have spent. This is the entire reason `missed::Reason` distinguishes
`Partial` from `NeverStarted`.

**Time of day is preserved where possible.** A gym block that was 07:00 must
not be proposed for 22:00 just because that gap was scanned first. Time of
day is part of what made the plan plausible; a proposal that ignores it gets
rejected and teaches the user the button isn't worth pressing.

**`Split` is quietly the most valuable rung.** A full calendar is almost
never *contiguously* full. Ninety minutes that fit nowhere often fit as
45 + 30 + 15.

---

## F. Who picks the victim

Deliberately dumb in v1: **the app finds the bump candidates, the user picks
one.**

To rank blocks by droppability the app would need to know how important each
one is, and it cannot. A block on a Task inherits priority and a due date; a
`Gym` block on an Activity has no priority at all; an ad-hoc block has
nothing. Inventing a "flexibility" field on every block is a real modelling
cost for a guess the app will often get wrong — and getting it wrong means
smugly deciding your gym session matters less than your lab report.

One filter the app *is* entitled to apply: a block with focus time already in
it is not a candidate. Taking a slot someone is halfway through is a loss,
not a swap.

**The v2 that has to earn its way in:** notice which kinds of block the user
consistently sacrifices, and pre-select them. That needs evidence to exist
first.

---

## G. Non-goals, stated so they read as decisions

- **No global rearrangement.** If the right answer is "move today's 14:00
  block to Thursday and put the missed study block in its place", that is
  constraint solving and it is out of scope. v1 offers single swaps.
- **No automatic bulk recovery.** "I lost the whole morning" is a part-2 UI
  affordance over these same primitives, not a sixth rung.
- **No `movedFromId`.** See §H.

---

## H. Storage

`Event` gains two fields, both additive:

```cpp
BlockOutcome outcome = BlockOutcome::Unset;  // Unset | Done | Moved | Dropped
QString      movedToId;                      // set only when Moved
```

`Unset` serialises to the **empty string**, which is also what a pre-v11
file's missing key reads back as. Tolerant read, no migration branch — the
fourth time this format has grown that way (`taskId` at v6, `repeat` at v9,
the task dismissal fields at v10). Schema version goes to **11**.

**One direction, deliberately.** The symmetric design stores a `movedFromId`
on the new block too, and its problem is that two pointers can disagree — a
half-applied edit leaves a chain that says different things depending on
which end you read. The reverse question ("was this rescheduled from
somewhere?") is a linear scan, which is trivially cheap and cannot drift.
Derive the reverse, store the forward. The repeat chain makes the same call:
the rule lives on exactly one link.

**Unknown values degrade to `Unset`.** Garbage on disk can never invent a
decision the user didn't make.

---

## I. The two doors

`AppData` gains exactly two mutations, and both defend an invariant:

**`resolveBlock(id, outcome)` refuses `BlockOutcome::Moved`.** "Moved" is not
a state you can assert — it is only true if a replacement block actually
exists, and the only thing that can produce one is `rescheduleBlock`.
Allowing it here would permit `outcome == Moved` with an empty `movedToId`,
which is exactly the class of lie the aggregate root exists to prevent.

**`rescheduleBlock(id, date, start, end)` copies identity, never segments.**
The time you already spent belongs to the day you spent it; carrying it
forward would double-count it in every report.

Two implementation details worth reading the code for:

- It takes **copies** of the source's identity fields *before* appending.
  `QVector::append` may reallocate and invalidate every `Event*` into the
  vector — including the one we're reading from. That is a use-after-free
  that would usually appear to work, which is the worst kind.
- `appendGuardedEvent` gained a `notify` flag so the two halves (create the
  replacement, mark the original `Moved`) emit **one** `changed()`. Qt's
  default connections are direct, so a listener would otherwise run
  synchronously between the halves and observe a replacement whose original
  still reads as unresolved.

---

## J. What part 2 adds

`CatchUpCard`, `CatchUpSettingsPage`, the morning/evening modes, and a
section in `brief::dayBriefing`. Delivered — see §K.

---

## K. Part 2 — the surfaces (v26.2, delivered)

### The card is a strip, not a gate

`CatchUpCard` is `NeedsBlockCard`'s sibling — const `AppData`, signals up,
injected `now`, fingerprint-gated rebuilds, `deleteLater` — but it
deliberately does **not** gate the panel. "You haven't looked at what needs
planning" may hold the day's numbers hostage, because planning the day is
what the panel is for. A missed block from yesterday does not get that
power: the day can proceed while the wreckage waits. One blocking review per
panel is the ceiling — two would teach the user to click through both
without reading either.

### Two moments, one card, zero stored mode

Morning asks *"3 blocks didn't happen. Recover the day?"*; evening asks
*"What should happen to what's left?"*. The mode is **derived** from `now`
against the agenda window — evening begins 90 minutes before the window
closes — so "end of day" means the end of *your* day: a 6 AM–2 PM schedule
flips at 12:30, not at a hardcoded 8 PM. `prefs::catchUpOnOpen` and
`catchUpAtEndOfDay` each silence exactly one framing.

**Snooze promises a return.** "Later" in the morning re-arms at the evening
moment; "Later" in the evening yields until tomorrow. The card never just
disappears — a dismissal with no comeback is how missed blocks went
unhandled for 26 versions. The timestamp lives in QSettings *and* in RAM
(`m_sessionSnooze`): the disk is allowed to forget, the widget that watched
you click is not (the v22.7 lesson, reapplied).

### The button is the design

The top of `reschedule::propose`'s ladder renders as one pre-filled button —
*"Move → tomorrow 09:00"*. Accepting emits the option **verbatim**;
`PlannerPage` translates pieces into the matching door (one span →
`rescheduleBlock`, several → `rescheduleBlockSplit`). The card does not know
the doors exist. "It happened" and "Skip it" are always offered, whatever
the calendar looks like — they are legal answers even when nothing fits.

The accept slot deliberately does **not** call `refresh()` after the door:
the door emits `changed()` and the ordinary pipeline repaints every surface.
And the door may *decline* — the slot can be taken between propose and
click. Then nothing changed, nothing repaints, the proposal stays pressable,
and the next `changed()` from any source re-proposes against current
reality. Quiet, but never wrong.

### Bump: shown, not one-tap-accepted (deferred, and why)

Accepting a bump displaces the victim — and propose-don't-move applied
*consistently* means the victim now needs its own proposal. That is a
cascade the card cannot host honestly in one row. So the dead-end row names
the conflict — *"Nothing free before the deadline — in the way: Meeting,
Tue 09:00"* — and offers **Open that day**. The user clears the way by hand
on the agenda (drag, resize, delete — all existing verbs), and the freed
slot surfaces as a normal `FreeSlot` proposal on the next refresh. The
information was always the valuable part; the tap can be added later without
changing anything shipped here.

### The split door

`rescheduleBlockSplit(id, spans)` is **all-or-nothing**: every span is
validated before anything is appended — against the calendar via `isFree`,
and against its *sibling* spans, which `isFree` cannot see because they
don't exist yet. A half-applied split leaves the calendar in a state nobody
proposed. `movedToId` points at the **first** piece: the forward pointer
stays one link (§H), and the rest are found by the scan that answers every
reverse question.

### The settings page — the v26.1 receipt

`CatchUpSettingsPage` cost one class and one `addPage()` line;
`SettingsDialog::save()` was not touched. That was the claim the refactor
made, now cashed. The threshold combo names **stances**, not percentages —
"Only if never started" stores `0`, which is load-bearing: `judge()`'s
Partial branch tests `focus×100 < minPercent×planned`, never true at 0, so
only the zero-focus case fires, which is exactly what the label promises.

### K.1 — the 320-pixel lesson (v26.3)

The first shipped card met reality and lost, and the screenshot is worth
keeping: a 46-block backlog, and an action row reading **"+ toda ⟨More..⟩
⟨apper⟩ ⟨3kip it⟩"**.

Two separate failures, one screenshot:

**Clipping.** The primary Move button, More…, "It happened" and "Skip it"
shared one `QHBoxLayout` inside the glance panel's fixed 320px. When a
layout's minimum width exceeds the room, Qt does not wrap — it **clips**,
mid-glyph. The fix is structural, not cosmetic: the primary action now owns
a **full-width row** (it cannot lose a width fight it never enters, and
full width also reads as *the* proposal, which it is); secondary actions
are one short word each — **Done**, **Skip** — flat on their own row, with
the sentence-length phrasing moved to tooltips, where length is free.
Expanded alternatives render directly under the primary so the placement
choices read as one group. The general rule this buys: **on a
fixed-width surface, a horizontal row of buttons may hold at most one
button whose label varies** — and here even that one gets its own row.

**The wall.** Forty-six blocks at three rows per screen is not a review, it
is a punishment — and it is exactly the "whole morning is gone" bulk case
§A predicted and v1 deferred. It now has its exit: **"Skip all N"** in the
footer (shown only when the backlog exceeds the visible rows — under that,
per-row Skip is the honest tool). The ids ride the signal verbatim, because
the page recomputing "all" from its own `now` and rule is two derivations
of one set, and that is how the button and the door would come to disagree.
Behind it sits a new bulk door, `AppData::resolveBlocks` — one decision,
**one `changed()`** — because looping the single door from the UI would be
46 synchronous full-surface repaints. Same `Moved`-refusal as the single
door; stale ids are skipped rather than sinking the batch; a no-op batch
emits nothing, because `changed()` means changed.

### K.2 — the eraser (v26.4)

The next field report arrived within a day of v26.3: **"Skip all 46"
pressed by accident, the card gone, no way back.** Nothing was lost —
outcomes are one field on events that still hold their history — but the
data being safe is worthless if no surface can reach it. The gap: any
action cheap enough to press by accident must be equally cheap to reverse,
and a 46-decision button had no reverse at all.

**Undo, not a confirmation dialog.** A confirm popup taxes every legitimate
press, and by the third one it gets clicked through blind — friction that
punishes attention instead of protecting it. The pattern that works is act
immediately, offer the eraser: after any resolution (single or bulk) the
card keeps a **receipt** — *"Skipped 46 blocks. · Undo"* — and the receipt
**outranks every hide rule**, because the accident's exact shape is "the
action emptied the card, and the way back vanished with it."

**Undo cost zero new wiring**, and that is the derived/stored split (§B)
paying out a third time: the judgement was never stored, so un-deciding is
one field write — replay the same ids through the same
`resolveAllRequested` signal with `outcome = Unset` (which the doors always
accepted; only `Moved` is earned), and the verdicts re-derive on the next
read. No repair step, no revive routine, no second code path to test.

Boundaries, chosen not defaulted: the receipt lives in **RAM only** — an
undo that survives restarts is a history feature; this is a mistake-eraser
for the click you just made. The ids are **copied at press time** — undo
means *that* action, not "whatever is dropped now". "Later" clears the
receipt (a snooze is a deliberate filing-away). And accepted **moves are
not undoable** here: the replacement block is visible on the calendar and
reversible with existing verbs (drag, delete), so the eraser stays scoped
to the two silent verdicts — Done and Dropped — the ones that leave no
trace on any other surface.

### K.3 — the way back, on the map (v26.5)

The receipt (§K.2) has a blind spot the very first user found the very
first day: it lives in RAM, so it cannot reach a resolution made before the
feature shipped, before a restart, or on another device. The user's
suggestion was a keyboard shortcut; the right shape is the one the sibling
card already uses — `NeedsBlockCard`'s put-off strip, a **visible chip**
listing reversible decisions with a bring-back button. A hidden shortcut
helps only the person who already knows it exists, at the exact moment
they're panicking; a chip is self-evident.

**"N resolved recently · review"** expands into the Done/Dropped blocks
still inside the look-back horizon, each with *Bring back*, plus *Bring all
back*. Every action rides the existing signals with `outcome = Unset` —
still zero new wiring.

The load-bearing decision: the set is **derived, with no `resolvedAt`
timestamp stored**. Deriving from the block's own date means the chip
reaches accidents that predate it (the schema stays at v11, and the rescue
needed no data surgery), and the horizon ages the chip out on its own —
nothing lingers forever. The cost, stated honestly: a block *resolved*
recently about a *date* now past the horizon falls off the chip. The knob
that already exists covers it — raise "Look back" temporarily and the net
widens — and the day that's not enough is the day `resolvedAt` earns its
schema bump. `Moved` is excluded: a moved block has a live replacement on
the calendar, and bringing it back would put one obligation on the board
twice.

Visibility got restated as three tenants with separate rules: the **list**
(obeys the moment and the snooze), the **receipt** (outranks everything —
§K.2), and the **chip** (quiet, obeys the snooze, suppressed while the
receipt covers the same ground). And with the list hidden, the footer goes
with it: "Skip all" over an invisible list would be a decision about things
not on screen.

### K.4 — hidden is indistinguishable from gone (v26.6)

The 46-block mystery resolved: the accidental press was (almost certainly)
**"Later"**, not "Skip all". Nothing was ever dropped — the list was
snoozed until the evening and would have returned by itself. But from the
outside, a snoozed card and destroyed data look **identical**: an empty
panel. The user read the scarier one, and was right to — the UI gave no
evidence either way. Two rules fell out, both now structural:

**The recovery surface never obeys the snooze.** v26.5's chip honoured it,
which meant the way back could be hidden by the very button that causes the
confusion. The chip is quiet — one line — and quiet things don't need
snoozing; they need to be findable at the exact moment of panic.

**Invisible state that suppresses UI must announce itself.** A snoozed
list now leaves a one-line marker — *"46 blocks waiting · back 22:30 —
Show now"* — naming what is hidden, when it returns, and offering the
reversal in one tap. This is §K.2's lesson one layer deeper: the receipt
made *actions* reversible; the marker makes *states* legible. The general
form is worth keeping: whenever a feature hides content on a timer, the
hiding itself must be visible, or every snooze is a support ticket titled
"my data is gone".

(The card's tenants are now four: list, receipt, chip, marker — each with
its visibility rule stated in `rebuild()`, which is getting close to the
complexity where a small state table in the header earns its keep. Noted
for the next touch.)

### K.5 — B′: one chip, three intensities (v26.7)

The glance panel had caught the settings dialog's disease: every feature
paying rent in one 320px column — needs-block strip, the full catch-up
card, three stat boxes, bars, a pie, an encouragement line. The owner
called it, and the redesign was **prototyped in HTML first**: three layout
variants side by side, then an interactive mock of the winning chip
lifecycle, argued over before a line of C++ moved. Worth keeping as
process: a layout debate held in a 100-line throwaway page costs an hour;
held in Qt widgets it costs a week and biases toward whatever got built
first.

**The shape.** The card collapses to ONE chip; every verb — the rows,
Move →, Done/Skip, Skip all, the undo receipt, bring-back, Later — moves
into a `SlidePanel` drawer, the exact v22.9 idiom the needs-block card
established ("chips are handles, lists live in slide-overs"). The glance
panel is a reading surface again. The chip's **intensity carries the
state**: *prominent* (amber) at the morning/evening moments with blocks
pending; *muted* (gray, dashed) when the count is real but the day is not
the time; *absent* when there is nothing to say.

**Snooze is de-emphasis, not a lock.** This came from the owner reasoning
about their own day: overwhelmed in the morning → one tap quiets
everything; free time at 14:00 → one tap on the muted chip re-summons the
review; never re-summoned → the evening reset re-promotes the chip by
itself. The snooze governs *attention*, never *access* — so v26.6's "Show
now" ceases to exist as a concept, and the muted chip absorbs the marker's
job (hidden ≠ gone, promoted from patch to design element). Deliberately,
the midday tap does **not** clear the snooze: reviewing is transient,
prominence returns only at the reset.

**The pie retired.** It rendered the same split as the bars — which were
literally its legend, a chart confessing it adds no information.
`CategoryPie` lives on in the week/month reviews, where it doesn't sit
beside its own data.

**Considered and rejected: a Recovery row in Settings.** The owner's
original instinct, and it was called "cheap" in discussion — reading the
code says otherwise. `SettingsDialog` is deliberately AppData-free (its
header's contract: closing it can't dirty the planner, can't trigger a
sync), a boundary held since v26.1. A bring-back button there needs the
mutable aggregate root and punctures that wall for a need the drawer's
bring-back already covers. Rejected with the reason on record; the earlier
"cheap" claim is hereby corrected.

**One structural payoff for free:** the chip is a single persistent button
restyled in place — the card no longer tears down and rebuilds widgets at
all, so the v22.2 destroyed-under-a-click bug class isn't mitigated here,
it's *impossible*. The drawer still rebuilds (via `clearContent`), where
the sibling's precedent already proved the pattern safe.

### K.5.1 — closing the seam (v26.7.1)

The first B′ build left a seam the owner's screenshot caught immediately:
the needs-block card still spoke the old full-width-strip dialect while the
new chip spoke pills, stacked one above the other. Scoping the sibling out
of the restructure was the right call for the *build*; leaving its visuals
unreconciled was not. Three-part fix:

- **One review row.** `GlancePanel` lays both cards on a single horizontal
  line; the needs-block card takes the row's width (its gate and pinned
  rows need room), the chip sits beside it. Known squeeze accepted and
  documented in-code: rare rung-2 pinned rows now share 320px with the
  chip.
- **One pill grammar.** The strip buttons restyled to the chip's geometry —
  and a semantic alignment fell out for free: *solid = actionable, dashed
  gray = waiting*. "3 put off · 14:00" and "31 · back 22:30" are the same
  state in two features, and now they wear the same coat.
- **One visibility owner.** While the gate holds the panel, the chip yields
  (one blocking review at a time). But the panel calling `hide()` directly
  would fight the fingerprint gate — data unchanged, refresh early-returns,
  the chip never comes back when the gate opens. So the veto is an *input*:
  `setSuppressed()` on the card, which clears the print so the next refresh
  re-decides. The rule generalises: **when a widget caches its own
  rendering decisions, external state that affects them must flow through
  the widget, never around it.**

### K.5.2 — the fatigue report (v26.7.2)

The owner's feedback after v26.7.1 was two sentences: a gap in the middle,
and *"my eyes fatigue very quickly looking at it."* The second sentence is
the valuable one — subjective discomfort almost always has a mechanical
cause, and here it had three, stacked:

**The void was framed.** The needs-block card held the row's stretch, which
shoved the chip to the far edge. Two objects pinned to opposite walls make
the *empty middle* the composition's subject, and every comparison costs a
saccade across it. Fixed: both cards size to their hints, pack left, one
trailing stretch owns the slack — the layout rule from the AI Test row
(§K.1's "one stretch decides") applied to a row of widgets instead of a row
of buttons.

**The dashes buzzed.** A 1px dashed border is high-frequency edge detail —
dozens of contrast flips per pill that the eye keeps re-processing. §K.5.1's
"solid = actionable, dashed = waiting" rule is hereby **revised**: waiting is
a *weight*, not a line style. Faded solid border, gray text, no fill —
**strong = actionable, faded = waiting.** (Second corrected claim in this
chapter; both corrections are on record next to the originals, which is the
point of writing decisions down.)

**Qt silently un-pilled the pills.** The stylesheets said
`border-radius: 999px` — the standard CSS pill idiom — and Qt's stylesheet
engine **drops any radius larger than half the widget's height**, rendering
sharp-cornered boxes. Sharp corners + dashes + a framed void was the whole
fatigue, mechanically. Fixed at an explicit 14px. Worth generalising: Qt's
QSS is CSS-*shaped*, not CSS — idioms imported from the web (999px pills,
inherited box models, `em` units) must be checked against what QStyleSheet
actually implements, and the screenshot is the only reliable checker.

**Coda (v26.7.3).** The clamp bit twice: 14px is only lawful on a button at
least 28px tall, and font metrics decide the height — Windows' default 9pt
lands the pill near 25px, so Qt dropped the radius *again*. The durable fix
pins the height (`setFixedHeight(30)`) instead of shrinking the radius: a
radius is a promise about geometry, and a promise needs the geometry
guaranteed. General form: **when a style rule depends on a size, fix the
size in code — don't hope the font agrees.**

**Coda 2 (v26.7.4).** The pills then clipped each other: sized to its hint
in the row, the needs-block card came out too narrow and truncated its own
pill — because it wraps a `QScrollArea`, and *a scroll area's sizeHint is a
cached guess* (the v22 scar, third bite). The row's final arrangement ends
the whack-a-mole with a rule instead of a number: **the widget with the
unreliable hint takes the stretch — the stretch absorbs the hint's error
along with the slack — and honest-hint widgets pack first.** So the chip
(a plain button, honest hint) leads, and the card follows with the stretch;
its internal left-pack lands the pill flush after the chip. One visible
change: the chip now sits first in the row — defensible on its own terms,
since the chip is the *momentary* surface and the plan pill the persistent
one.

**Coda 3 (v26.7.5).** The review row broke both drawers, three versions
after it shipped. `drawer()` in both cards resolved its host as "my parent
widget" — true when the cards sat directly in the glance panel, silently
false the moment v26.7.1 re-parented them into a pill-height row. Every
slide-over then opened inside a ~40px strip: the needs-block one
invisibly ("the left pill doesn't pop anything"), the catch-up one
mangled. Nothing caught it — no compiler (the code is legal), no test
(bare test cards fall back to hosting on themselves), no review (the
assumption was never written where a reviewer would look). The fix writes
the contract down: `setDrawerHost()` on both cards, injected by the panel;
the parent fallback survives only for bare embeddings. The lesson is the
chapter's most general one: **an implicit assumption is a dependency
without a name — it breaks silently the day someone changes the thing it
secretly pointed at. Name it (a parameter, a setter, an assert) or lose
it.**

**Coda 4 (v26.7.6).** One last gap under the pill row: the scroll-area
hint struck on the *vertical* axis — nothing bounded the card's height, so
it reserved phantom space. Capped to the body's hint (a plain widget, so
an honest number, pinned rows included); gate mode lifts the cap because
there the card must fill the panel. Coda 2's rule, completed: **an
unreliable hint must be neutralised on every axis it can lie on** — the
stretch absorbed its width, the cap now bounds its height.

**Coda 5 (v26.7.7).** The needs-block pill would open its drawer once and
never again — while the catch-up pill worked forever. The difference was
the diagnosis: the strip click routes through the fingerprint gate, and the
drawer's `closed()` handler mutated `m_drawerMode` *without invalidating
the print*, so the next click recomputed the identical print and the
refresh early-returned. The catch-up chip was immune by accident twice
over: it opens its drawer directly, and its print carries a drawer-open
flag that self-heals on close. One line fixes it (`m_lastPrint.clear()` in
the handler) — and it is §K.5.1's rule (V158) caught violating itself in
the *sibling* card, latent since v22.9 and masked by the once-per-second
refreshes while a timer runs. Rules earn their keep the second time they
catch something.

### The briefing tells the truth

`brief::dayBriefing` gains an `UNRESOLVED BLOCKS` section — count stated,
capped at 3, `+N more` visible; the anti-hallucination rules apply to bad
news too. The rule arrives as a **field on `Options`**, not a `prefs::`
read: `brief::` stays domain-only and QSettings-free (the layering its
header celebrates, enforced by the build). `ChatPage` passes
`prefs::missedRule()` so the assistant and the card judge by the same bar.

---

## §L — Retrospective: the anatomy of a derail

*Written at v26.8, at the owner's request: "explain why we derailed from the
main feature." The honest answer is worth more than the feature.*

### The numbers

Planned: **two slices** — a pure domain core (v26.2a) and its surfaces
(v26.2b), an afternoon each. Shipped: **twelve versions** across v26.1–
v26.8, a full UI redesign with HTML prototypes in the middle, five codas of
corrections. The domain core — the part that felt like "the feature" — was
roughly **a fifth of the total work** and needed exactly **zero** fixes
after landing: every one of its 18 tests passed on first compile and never
changed. Everything after was the surfaces meeting reality.

### What the tail was actually made of

Sort the twelve versions by *cause* and a taxonomy appears, worth reusing
as an estimation checklist for any user-facing feature:

1. **Real data breaks assumptions** (v26.3): the first live run produced a
   46-block backlog — four buttons clipped mid-glyph in 320px, and no bulk
   exit existed. No test could have found this; only a screenshot did.
2. **Cheap actions need cheap reversals** (v26.4): one accidental tap
   "destroyed" 46 decisions. Undo, receipts.
3. **Recovery must predate its accidents** (v26.5): the receipt was RAM,
   the accident was history — only a *derived* recovery surface reaches
   backwards. (The derive-don't-store architecture paid for the whole
   feature right here.)
4. **Hidden must be distinguishable from gone** (v26.6): a snoozed card
   and destroyed data looked pixel-identical, and the user rationally
   believed the scarier story.
5. **The design was wrong, and only usage could say so** (v26.7): the
   panel was crowded; the owner articulated a *workflow* (quiet workday,
   deliberate re-summon, guaranteed reset) that the layout debate had been
   circling; HTML prototypes settled in an hour what widget code argues
   about for a week.
6. **Layout physics** (v26.7.1–.4): the void, the dash buzz, the radius
   clamp (twice), the lying scroll-area hint — each a real Qt mechanism,
   each now a written trap in the Reading Guide.
7. **Latent debts called in** (v26.7.5, .7): the review row broke an
   *implicit* drawer-host contract from v22.9, and exposed a stale-print
   bug that had hidden in the sibling card for four versions. New layouts
   audit old assumptions whether you planned the audit or not.
8. **Deployment is part of the feature** (the FLAT zips): two fixes
   "didn't work" because the packaging never landed them. The fix wasn't
   code.
9. **Defaults are design** (v26.8): the intimidation knob existed from day
   one and protected no one, because the wall arrives before anyone looks
   for a setting.

### Why derailing was the right call — mostly

Three reasons, in force the whole time:

- **The owner was daily-driving the feature.** Live usage is the
  highest-value bug generator that exists; parking fresh reports to "get
  back to the plan" ships a feature its own user distrusts, and a
  distrusted review surface is worse than none (§D said so before any of
  this happened).
- **Every report was small but blocking.** "My data is gone", "the drawer
  won't open", "it hurts to look at" — severity outranks roadmap, and each
  fix was hours, not days.
- **The fixes compounded into doctrine.** V158's cache rule caught its
  second bug two versions later; the hint rule closed on its second axis;
  "name the implicit dependency" came out of a real breakage. Rules earned
  this way stick.

### And the honest counter-analysis

The v26.7.1–.7.4 stretch — four versions of layout ping-pong at one
round-trip per pixel problem — should have been **one consolidated pass**.
The tell was visible after the second screenshot: every bug in that stretch
was *visual*, invisible to a suite that drives widgets programmatically.
The correct move was to stop, build a screenshot harness (render the panel
headless at 320px, eyeball a gallery), and batch the fixes. We paid in the
owner's round-trips for what a tool would have caught locally. Lesson filed
where it belongs: **when a bug class is invisible to your tests, the next
fix is a new kind of test, not a next patch.**

### The irony, and the point

This is an app whose founding idea is the gap between *plan* and
*reality* — and its own development just demonstrated the thesis at 6× the
estimate. The plan wasn't wrong; it was a plan. What made the overrun
cheap instead of chaotic was the same discipline the app teaches: every
deviation *noticed*, *decided*, and *written down* — five codas, two
on-record corrections, seventy question-bank entries. A derail you can
narrate is called iteration. The one you can't is called a mess.
