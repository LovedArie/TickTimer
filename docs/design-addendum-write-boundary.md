# Design Addendum — The Write Boundary (v29.0, Slice 1)

*The assistant addendum §B, built — deliberately without its model. Companion
to `diagrams/write_boundary.*` (the map), `docs/TESTING.md` (the recipe), and
`AssistantVerbs.h` (whose header comment is this file's abstract).*

---

## A. Machine before model

Slice 1 ships the entire proposal → validation → confirmation → effect
pipeline and **no model to drive it**. Every proposal this version can
produce is composed by C++ — the debug panel's injector — which means the
boundary is built, wired, force-testable, and pinned by 366 tests before a
single token of model output exists to cross it. Slice 2 swaps the
proposer and changes *nothing to the right of it*: the model arrives as a
new **caller** of an old, guarded path, not as a new path. This is the
v28 fallback doctrine (ship the no-model spine first) applied to writes,
where it matters most.

## B. The verb layer's birth shape

`verbs::Role → QVector<Verb>` is one switch that fits on a screen — and
that is the security model: reviewing the assistant's reach is reading
one function, and any diff to `AssistantVerbs.h` **is** the complete
review. Born per-role (§B.4) because retrofitting costs what the flat
shape saves: Intake holds the single verb; Chat, Nudge, and CheckIn hold
empty lists, which makes "the 08:00 toast rearranged my afternoon"
structurally impossible and leaves a prompt-injection landing in a nudge
nothing to reach for.

`Role` is deliberately **not** `ai::Feature`. Feature is *routing* (which
seats may answer) and deliberately lacks Nudge; Role is *trust* (which
call sites may write). Folding the axes would let a routing edit widen a
trust scope by accident — two enums is the cheap insurance against a
category of future mistake.

## C. Handles: fail safe, per turn, deduplicated

The briefing still never contains a UUID. Task lines carry `[T1]`-style
handles registered in print order; the map lives with the turn and dies
with it. Three properties carry the §B.2 promise: an *invented* handle
(`T9`, `B1`, `t3 `) resolves to `""` and dies in `validate()` with a
readable reason, never lands on the wrong task; a task printed in two
sections keeps **one** handle (or the model would see `[T2]` and `[T9]`
naming the same task and reasonably treat them as two); and resolution is
strict, not fuzzy — guessing that `t 3` "probably means" `T3` would
reintroduce the exact wrong-target bug handles exist to prevent.

`validate()`'s check order is part of the design: the role check runs
first so a forbidden role's refusal text reveals nothing about which
handles exist or what state any task is in.

## D. The additive rule, and priority's exclusion

`SetTaskDetails` fills **absent** fields only: estimate (`0` = unset) and
due date (invalid `QDate` = "DATE TBD" — the domain's own absence
idioms, reused so "not proposed" and "not set" read identically).
A proposal targeting a filled field is refused: overwrite is a *different
verb* with an inverse story of its own (§B.1), and it does not sneak in
as a special case of this one.

Priority is excluded from the verb entirely, and the reason is worth
keeping: **Medium is a value, not a blank.** A field with no absence
state has no additive semantics — "fill if empty" is undefined when
nothing is ever empty — so priority waits for whatever verb owns
overwrites.

## E. Two verdicts, two moments

The card renders with a verdict (born-broken → the reason shows, Apply
never enables) — and the tap **re-validates regardless**. Different
moments, different worlds: the owner may fill the estimate by hand while
the card sits there, and a stale Apply must refuse politely (the additive
check catches exactly this) rather than trust a verdict from an older
world and overwrite. `applyRevalidatesAndFillsBlanksOnly` pins the
scene: by-hand edit lands after the render, the tap refuses, the
by-hand value survives.

## F. The card is glass, and the summary is the request

Third application of the DebugPanel doctrine: `ProposalCard` shows,
emits `applyRequested`/`discardRequested`, and decides nothing —
validation and application belong to the container that owns the data
and the handle map. And its text is composed by `Proposal::summary()`
from the **structured fields** `apply()` will consume, never from the
proposer's prose: what you approve is what will run, because the
description *is* the request, re-rendered. A proposer cannot describe
one change and ask for another.

## G. The record and the live UI

Cards do not survive `rebuildLog` — the **transcript** is the record. So
an applied proposal writes a `localOnly` receipt turn ("✓ Applied: …"),
never sent to any model, same privacy stance as every notice. A
*discarded* proposal records nothing: nothing happened, and a declined
suggestion should not haunt the log. (Slice 2 will report declines to
the **model** as tool results — that is the proposer's business, not the
record's.)

## H. The copy-aside, and where it is wired

`data.json` → `data.json.pre-apply` immediately before any Apply
mutates: §B's "cheap insurance, not a feature", verbatim. A rolling
single copy — the state before the *most recent* apply — because
archives are what version control is for, and an unbounded
`.pre-apply-2026-07-26T…` collection is a disk-filling promise nobody
made. Wired in MainWindow because the file path is `m_store`'s knowledge
and the composition root's business; ChatPage only knows *when*
(`preApplyHook`, the `nowProvider` precedent — the page knows when, the
root knows what).

## I. §B.3, answered: the Dialect promotion stays deferred — recorded why

§B.3 predicted the tool-use iteration would finally promote the
`Dialect` enum to strategy objects, *if* a dialect must hold state
across calls, and instructed: if it still is not needed, record why.
Recording: **Slice 1 has no cross-call state to hold.** Every proposal
is single-shot and C++-composed — no tool-call transcript, no
`tool_use`/`tool_result` blocks, no provider-specific conversation
shape. The criterion is about *behaviour over time*, and this slice has
none. Slice 2, where the intake model converses and its tool exchanges
must be threaded per-provider, is where the criterion should genuinely
fire — and if it does not fire there, that answer gets recorded too.

*(It did not — recorded, v29.1: Slice 2 chose extraction over the
tool-use API precisely so nothing converses at the wire level. See the
intake addendum §B for the three reasons. The prediction above was
wrong about the mechanism and right about the method: record either
way.)*
