# Design Addendum — The Reschedule Verb (v29.2, Slice 3)

*§N's remaining v29 content: "the confirm loop (§B), intake first (§K), then
rescheduling." The second write verb, and the first that changes the calendar.
Companions: `design-addendum-write-boundary.md` (Slice 1, the machine),
`design-addendum-intake.md` (Slice 2, the model as proposer),
`design-addendum-catch-up.md` (where `reschedule::` was born).*

---

## A. What this slice is, and what it deliberately isn't

The assistant may propose moving a block that **already didn't happen** — and
nothing else. It does not rearrange your afternoon, it does not optimise your
week, and it never picks a time of its own invention. Both halves of that
sentence are load-bearing and each has its own section (§B, §C).

The slice is unusually cheap for what it buys, because three of its four parts
already exist and are tested: `reschedule::propose()` computes the ranked
options (catch-up §E), `AppData::rescheduleBlock` / `rescheduleBlockSplit`
perform the change behind the aggregate root's guards, and `ProposalCard`
renders the confirm. What is genuinely new is the verb, a handle namespace for
blocks, and the join between them.

## B. Only missed blocks, and why that fence is the safety story

`validate()` refuses any target whose `missed::judge` verdict is not Missed.

*Why:* a block that hasn't happened yet is a plan you are still living inside.
An assistant that may move it is a calendar editor, and the failure mode —
"the 08:00 toast rearranged my afternoon" — is the exact scene §B.4 built the
per-role verb lists to make structurally impossible. A block the domain already
judges *missed* is different in kind: it is a plan reality has finished with,
and every proposal about it is about recovery, not control.

This fence also means the verb inherits catch-up's whole vocabulary for free —
`missed::Verdict` already carries the shortfall the options must recover.

*Rejected:* letting the verb move any block (turns the assistant into a
scheduler, and no amount of confirm-tapping makes that a small capability);
gating on "the block is in the past" instead (a weaker, time-only proxy for a
judgement `missed::` already makes properly, including the tracked-time case
where a past block *did* happen).

## C. The model selects; it never invents a time

The `Proposal` carries a **concrete placement** — date, start, end (or the
spans, for a Split). `validate()` recomputes `reschedule::propose()` against
live data and requires the proposed placement to be **exactly one of the
options that search just returned**. Anything else is refused.

*Why:* this is §A's spine applied to the time axis — *code computes what is
true, the model only phrases*. `reschedule::` already reasons correctly about
deadlines, the agenda window, the 30-minute grid, collisions, and the honest
empty answer. A model emitting "Thursday 14:00" reasons about none of that,
and would reintroduce at the time axis precisely the wrong-target bug class
that handles killed at the identity axis (§C of the write boundary).

It also keeps the exchange **single-shot**: the options are computed before the
model is asked, so there is no tool transcript to thread. See §G.

*Rejected — and this is the subtle one:* having the Proposal carry an **index**
into the ranked list. An index is a handle with no fail-safe property. The list
is recomputed at the tap (§E of the write boundary — the world moves between
render and tap), and index 2 in the new list may name a different option than
index 2 in the old one, which is a silent wrong-target write rather than a
refusal. A concrete placement fails *closed*: if the option is no longer
offered, the match fails and the card refuses with a reason. Same lesson as
`HandleMap::idFor`, one axis over.

## D. Block handles: a second namespace, still strict

Today `HandleMap` is tasks-only: `idFor` accepts `'T'` + 1-based index and
`DayBriefing` registers handles only for task lines, while the `PLAN FOR TODAY`
block lines carry no handle at all. This verb needs to name blocks, so blocks
get `[B1]`, registered in the same print order, deduplicated the same way.

*Why a second namespace rather than one shared counter:* a handle that could
mean either a task or a block is the ambiguity §C exists to prevent — the model
would see `[T4]` and have no way to know which kind of thing it names, and the
resolution would have to guess. Two prefixes, two lookups, both strict.

*Rejected:* reusing `T` for both; fuzzy resolution of any kind (`b1`, `B 1`).
The write boundary's §C properties must hold per namespace, not merely overall:
an invented `B9` resolves to `""` and dies in `validate()` with a readable
reason.

## E. Not additive — and why the overwrite door stays shut anyway

§D of the write boundary drew a hard line: `SetTaskDetails` fills **absent**
fields, and overwrite is a different verb with an inverse story of its own.
A move plainly is not a blank-fill, so this verb has to answer that line rather
than sidestep it.

It answers it structurally: **`rescheduleBlock` does not mutate the original
block's time.** It appends a replacement carrying the source's identity, marks
the original `Moved`, and links the two — one `changed()`, by way of the
`notify` parameter that exists for exactly this. The original block, its
planned span, and every segment ever tracked against it survive untouched.
Nothing is overwritten; something is *added*, and something is *annotated*.

That is why this verb can ship without opening the overwrite door. A verb that
edited a block's `plannedStartMinutes` in place would be a different proposal
and would need §D's full inverse argument first.

*Corroborating precedent, worth reading before building:* `resolveBlock`
refuses `BlockOutcome::Moved` deliberately, because Moved "is only true if a
replacement block actually exists." The domain already treats a move as
something **earned by construction rather than asserted**. This verb must not
become the loophole that lets it be asserted.

## F. Which role holds it — DECIDED: `Role::Chat`

`Role::Chat` gains `MoveBlock`, its first write verb. The alternative — a new
`Role::CatchUp` scoped to the drawer — was the smaller, safer slice; it was
rejected because the gesture people actually have is a sentence ("find a slot
for the study block I missed"), and a verb reachable only from a drawer they
must first open is a feature they must first remember.

Nudge and CheckIn keep empty lists **forever**, unchanged. That is the part of
§B.4 doing its work: widening one role's scope leaves the other two provably
untouched, and the diff says so on one screen.

**Two consequences that must be paid deliberately, not discovered:**

1. **`test_domain.cpp` asserts `verbsFor(Role::Chat).isEmpty()`.** That test is
   the tripwire the verb layer was built to trip. It flips in the same commit
   as the verb, with the new expectation naming the single verb explicitly —
   never loosened to "not empty", which would stop pinning anything.
2. **"The assistant is read-only" is asserted in ten documents**, including the
   README's headline paragraph and `design-addendum-chat.md`. The claim is now
   false for one verb, and a claim that is 95% true reads as a lie to the one
   person who relied on it. Retiring it is part of this slice's definition of
   done, not documentation follow-up.

## G. §B.3's third recorded answer: it still does not fire

*(Revised after §F was decided. The first draft of this section assumed the
Chat reading would force the promotion. It does not, and the difference is
worth stating precisely, because the criterion is only useful if it is applied
honestly rather than triumphantly.)*

The criterion is: **does a dialect need to hold state across calls?**
The answer here is still no, and conversation does not change it.

That criterion was armed in §B.3 and explicitly re-armed by the write
boundary's §I, which named *multi-step rescheduling* — this slice — as the case
where it should genuinely fire. §I asked for the answer either way; this
section is that answer.

A back-and-forth ("not Thursday — what else have you got?") is carried by
`ChatSession`'s transcript, which is dialect-neutral and has been multi-turn
since v25. Each model call stays single-shot: system prompt + transcript +
the freshly computed option list → one JSON object out. The dialect holds
nothing between calls, because nothing between calls is dialect-shaped.

What *would* fire the criterion is adopting the vendors' native
`tool_use`/`tool_result` blocks, which must be threaded per provider. That is
declined here for intake §B's three reasons, and the first is decisive: a local
Ollama seat has no native tool support, and this verb must not become a
premium-seat feature. The confirm loop remains the tool layer, with the owner
as the dispatcher.

So: Slice 1 recorded "no cross-call state because C++ composes the proposals";
Slice 2 recorded "no cross-call state because extraction is one exchange by
construction"; Slice 3 records **"no cross-call state because the conversation
is the transcript's job, not the dialect's."** Three iterations, three
non-firings, one unchanged criterion — still armed, and still honest.

## H. The inverse did not exist — verified, and it was the first build task

§B.1's claim that no undo button is needed rests on every verb having an
inverse. Checked, and the honest answer is that a move's inverse was **not
available**, in two separate ways.

**1. Catch-up's "Undo" and "Bring back" invert a *decision*, not a move.**
Both emit `resolveBlock(id, Unset)`, which reverses Done or Skipped. Applied
to a block that was moved, it clears `outcome` and `movedToId` — and leaves the
replacement block sitting on the calendar. The result is the work appearing
twice: an original that reads unresolved, plus a replacement nobody is now
linked to. That is a "state nobody proposed", the exact phrase `AppData`'s own
header uses to justify making `rescheduleBlockSplit` all-or-nothing.

So the inverse needs a real door — `undoReschedule(id)`, removing the
replacement and clearing the original in one guarded step, symmetric with
`rescheduleBlock` and using the same `notify` trick so two mutations look like
one to `changed()`.

That door was built first, before any verb could reach it — `AppData.h:393`,
`AppData.cpp:510`.

**2. For a Split, the inverse is not expressible at all with today's data.**
`movedToId` is a *single forward link*, and `rescheduleBlockSplit` points it at
the **first** piece only. Pieces 2..n have no back-link and no stored grouping,
so nothing in the model can find them reliably — the header's "found by
scanning" answers a different question than "which pieces belong to this
move?". Undoing a split would have to guess, and guessing which blocks to
delete is not a thing this codebase does.

Two ways out, and the choice belongs in review:

- **Fence Split out of this slice** (see §I). One replacement, one link, one
  inverse — cheap, and consistent with how every other part of this slice
  reuses what already works.
- **Give each piece a `movedFromId` back-link.** A format bump to v14, additive
  like every bump before it, after which the inverse is a scan for pieces
  naming this original. Strictly better data, and it makes the split's own
  reverse questions answerable for every future reader — but it is a domain
  change, and domain changes get their own addendum in this project.

The draft recommends the fence now and the back-link later, but records both
because deciding this by default would decide it wrongly.

> **Answered in v29.3** — `design-addendum-split-inverse.md`. The fence was
> taken now and the data change came next, as recommended; the *shape* of that
> change is not the one sketched above. The back-link was rejected (again — and
> `Event.h` had rejected it once already), because the defect turned out to be
> the link's **cardinality**, not its direction: `movedToId` became
> `movedToIds`, so one record still owns the move. The v14 bump happened; the
> §I fence has **not** been lifted.

## I. Scope fence

In scope: `Kind::FreeSlot`, `Kind::Shorten`, and `Kind::BeyondDeadline` — the
three that produce exactly **one** replacement block, and therefore exactly one
link and one invertible move.

Out, with reasons:

- **`Kind::Split`** — no expressible inverse today (§H.2). It returns the day
  its pieces gain a back-link, and that is a domain iteration with its own
  addendum, not a rider on this one.
- **`Kind::Bump`** — asks the owner to choose which other block gives up its
  slot. That is a second decision inside one proposal, and the card renders
  one; catch-up §F deliberately kept victim-choosing with the human, and a
  confirm card is the wrong shape for it.
- **Deadline changes** for the `BeyondDeadline` case. The option is offered as
  "there is room, after your deadline"; moving the *deadline* is a different
  verb against a different object, and quietly bundling it here would let one
  tap change two things.
- **Any block not judged missed** — §B's fence, and not a candidate.

Worth noticing: the three in-scope kinds are exactly the ones whose shape the
existing `rescheduleBlock` door already handles. The fence was not drawn to be
conservative; it was drawn where the domain's existing guarantees end.
