# v29.0 — the write boundary (Slice 1: the machine, no model)

*The milestone the whole roadmap pointed at, taken in the safest possible
bite: the secretary's hands exist, and they only move when you tap.
Nothing in this slice contains a model — the debug panel plays one, so
the entire confirm loop is forceable by hand and pinned by tests before
any model output ever crosses it.*

## The verb layer (`AssistantVerbs.h/.cpp`, `design-addendum-write-boundary.md`)

A closed, **per-role** verb set (§B.4, born that way): Intake holds
exactly one verb, `SetTaskDetails` — an *additive* fill of a task's
absent estimate and/or due date — while Chat, Nudge, and CheckIn hold
empty lists. Priority is deliberately not in the verb: Medium is a value,
not a blank, and a field with no absence state has no additive
semantics. `validate()` refuses with owner-readable reasons (role first,
so a forbidden role learns nothing about the handle space);
`apply()` **re-validates at the tap** — the stale-card scene, where you
fill a field by hand while the card waits, refuses politely instead of
overwriting — then funnels through the existing guarded doors
(`setTaskSize` preserving chunkable, `setTaskDueDate`). Reach, not new
capability.

## Handles, not ids (§B.2)

Briefing task lines now carry `[T1]`-style per-turn handles (UUIDs still
never leave the process), deduplicated across sections, resolved
strictly: an invented `T9` dies to `""` and a readable refusal, never
the wrong task.

## The NEEDS DETAILS queue (§K.1, the derived half)

Captured-but-unsized open tasks surface in their own briefing section —
keyed on the missing **estimate** (the fact affordability starves
without; a TBD due date is first-class and does not queue), derived
fresh so the queue cannot drift from the data, silent when empty. This
also hands Slice 2's interview its targets.

## The confirm loop in the UI

`ProposalCard` (glass, the DebugPanel doctrine's third application):
summary rendered from the **structured fields**, never the proposer's
prose; Apply/Discard; born-broken cards show their reason with a dead
Apply. `ChatPage::presentProposal` puts the card in the transcript
column; the tap copies `data.json` aside first (rolling
`data.json.pre-apply`, §B's cheap insurance, wired where the file path
lives), applies through the boundary, and writes a `localOnly` receipt
into the transcript — the durable record. Discards record nothing.

## The panel plays the model

Debug panel gains **Inject sample proposal**: composes a real proposal
for the first unsized task in the current briefing turn and sends it
down the whole road — card, tap, boundary, receipt, aside. TESTING.md
carries the recipe.

## Numbers

**366 tests across six suites** (154 domain + 22 taskmodel + 68 nlp +
92 ui + 19 auth + 11 login_live; was 359): the per-role allow-list as a
tripwire, handle round-trip and every fail-safe, the gate refusal by
refusal, re-validate-at-tap with the by-hand value surviving, the card
lifecycle, and the full end-to-end through ChatPage with the aside
counted. Format stays **v13** — the boundary adds zero stored state
(`dismissedUntil`, which Slice 2's "ask once" needs, has existed since
the return-policy arc). §B.3's Dialect promotion is deferred again *with
the reason recorded* (addendum §I): single-shot C++ proposals hold no
cross-call state; Slice 2's tool transcripts are where the criterion
should genuinely fire. `installer/ticktimer.iss` rides along at 29.0.0 —
both files, checked before the zip was cut.
