# Design Addendum — The Undo Verb (v30.1)

*The verb §B.1 always assumed existed. Companions:
`design-addendum-write-boundary.md` (the machine this widens),
`design-addendum-reschedule-verb.md` (`MoveBlock`, whose inverse this is),
`design-addendum-split-inverse.md` (which made that inverse work for splits),
and `AssistantVerbs.h`, whose diff is the security review for this slice.*

---

## A. The promise that was not true

§B.1 shipped a decision: **no undo button**, on the grounds that

> *"every verb the assistant can call has an inverse **it can also call**.
> 'Ask it for something else' then is a complete undo story."*

It was not true, and had not been since `MoveBlock` shipped.
`AppData::undoReschedule` was built in v29.2 *specifically* as that inverse and
generalized in v29.3 to cover splits — and it had **no caller anywhere in the
app**. There is no Undo verb; catch-up's "Undo" and "Bring back" reverse a
*decision* (`resolveBlock`), never a move, and `missed::` excludes `Moved` from
both its lists on purpose. So if the assistant moved a block and you tapped
Apply and regretted it, nothing could reverse it: not the assistant, not the
drawer, not you.

§M's no-list makes the stakes explicit — it withholds an undo button *"as long
as §B.1's verb discipline holds."* The condition had quietly stopped holding.
This slice restores it rather than re-opening the button.

## B. The model never names the target

The obvious design gives `UndoMove` a handle, like every other verb. It is not
expressible, and the reason is a property worth protecting.

Block handles are registered in exactly one place — the briefing's UNRESOLVED
BLOCKS section — and `DayBriefing.cpp` says why: *"the namespace IS the set of
legal targets, rather than a superset the validator has to whittle down."* A
**moved** block is no longer unresolved, so it has no handle at all. Giving it
one means listing moved blocks in the briefing, which turns the block namespace
into a superset for `MoveBlock` and leaves `validate()` as the only thing
keeping moved blocks out of it. A deliberate v29.2 property, spent to make an
undo sayable.

*Chosen instead:* the model proposes `{"undo_move": {}}` — **no handle, no
date, no times, no fields at all** — and C++ decides which move it refers to.
That is the house doctrine in its strongest form: code decides what is true, the
model only asks. The model cannot aim this verb, by construction rather than by
a rule someone has to remember.

## C. The target rides in `World`, not in `Proposal`

`Proposal` is built from the model's own reply by `scrub::`. An id field on it
would sit one careless edit away from being model-populated, and the whole value
of §B.2 is that a reply can never reference a real id.

`verbs::World` is the opposite kind of value: assembled by the **caller**, fresh
per call, out of what the caller itself did. Its own comment already anticipated
growth — *"bundled into one value rather than three trailing parameters so that
adding a fourth later is not another signature change at every call site."* So
`World::undoableMoveId` is where this belongs, and `validateUndo()` reads the
`World` and never the `Proposal` at all. A test pins the property directly: a
proposal carrying a handle and a placement for a *different* block is applied,
and the World's block is the one that moves back.

`scrub::` returns early on the undo shape, before any field is read, so no code
path exists by which a reply could carry a target.

## D. Scope: the last move this conversation made

`ChatPage` records it at the **tap** — a proposal that was never applied is not
a move that happened — and clears it once used, and on a new conversation.

This lives in `ChatPage`, not `AppData`, because it is a fact about a
*conversation*: the domain has no opinion about who moved a block or how
recently. It is deliberately **not persisted**. §B.1's "ask it for something
else" describes immediate regret, and an undo that survived a restart would be
a different feature with a different risk profile.

Two moves in a row: the last one wins. Not covered, and stated so the gap is a
decision rather than an oversight: a move **you** made in the catch-up drawer.
That is not what §B.1 promised, and giving the assistant reach over changes it
did not make is a widening nobody has asked for.

## E. §B.1 amended, not quietly patched

The promise is also false for `SetTaskDetails`, and closing that would have cost
far more than the gap is worth. The verb is **additive-only** — `validate()`
refuses any field that is not currently absent — so it cannot clear an estimate
it set. Making §B.1 literally true would mean a clearing verb, which re-opens
§K.5's additive rule: the rule that exists precisely so the assistant can never
overwrite or destroy a value. A much larger security decision than the one being
fixed.

*So §B.1 gets an amendment rather than a rewrite,* with the original sentence
left visible above it: verbs that **rearrange** get an assistant-callable
inverse; verbs that only **fill an empty field** do not need one, because
nothing was destroyed and the owner clears them in one click. That is the
guarantee the code now actually keeps.

## F. What the model is told

The contract band gains the second shape and, more importantly, the sentence
that stops it inventing a target: *"It carries no block and no times, and there
is nowhere to put them: the app decides which move that refers to, not you."* A
model told only "you may undo" will helpfully supply a handle.

The no-claiming rule extends with it — *"I can put it back if you like"*, never
*"I've put it back"* — because the tap is still the boundary.

## G. Tests

Twelve new slots. The ones that matter:

| Test | What it pins |
|---|---|
| `verbsAreScopedPerRole` | **re-pinned, not loosened** — Chat's list is named as exactly `{ MoveBlock, UndoMove }`, in order. This is the security review's tripwire |
| `undoMoveCannotBeAimedByTheProposal` | the security property itself: a proposal naming another block is ignored, and the World's block is the one restored |
| `scrubNeverLetsAnUndoCarryATarget` | the same property one layer earlier — the reply parser drops handle, date and times on the undo shape |
| `undoMoveIsRefusedForEveryPhrasingRole` | Nudge, CheckIn and Intake refused, with the reason naming **permission** and never the world |
| `undoMoveRefusesWhenAnySplitPieceHasTrackedTime` | the fact-over-pointer refusal, driven by a split's **non-first** piece, raised at `validate()` so the card can say it before the tap |
| `undoMoveRefusesTheSecondTime` | the stale-tap path, refused by the verb itself rather than by the caller remembering to clear its copy |
| `chatRemembersItsOwnMoveSoTheUndoCanTakeItBack` | the whole loop through the real cards, and that the page forgets afterwards |

Six suites, **435 measured** (205 + 22 + 76 + 98 + 19 + 15; was 422). Measured,
not remembered: that is what QTest reports across the six binaries, including
each suite's synthesized `initTestCase`/`cleanupTestCase` pair.
