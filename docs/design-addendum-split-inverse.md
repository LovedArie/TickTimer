# Design Addendum — The Split's Inverse (v29.3)

*The domain iteration the reschedule verb's §H.2 said was owed. Companions:
`design-addendum-reschedule-verb.md` (v29.2, which found the gap and fenced
around it), `design-addendum-catch-up.md` (where `reschedule::Kind::Split` was
born), and `include/Event.h`, whose `movedToIds` comment is this file's abstract.*

---

## A. The defect, stated exactly

v29.2 shipped `AppData::undoReschedule` so that the assistant's `MoveBlock` verb
would have an inverse — which is what lets the write boundary keep its promise
that **no undo button is needed, because every verb is undone by another verb**
(assistant addendum §B.1). It was only ever the inverse of `rescheduleBlock`.

`rescheduleBlockSplit` turns one missed block into several, and `movedToId` was
a lone `QString`, so it could record only the **first** piece. Pieces 2..n were
named at neither end. Nothing in the model could answer *"which pieces belong to
this move?"*, so undoing a split would have deleted piece 1 and left the rest
standing — the work appearing partly twice and partly not at all, which is the
"state nobody proposed" that `rescheduleBlockSplit`'s own all-or-nothing
validation exists to prevent on the way *in*.

This was not only an assistant problem. `PlannerPage.cpp` already offers Split
through the catch-up drawer, so ordinary use produces splits that no code path
could take back.

## B. The choice: fix the cardinality, not the direction

`Event::movedToId` (one `QString`) becomes `Event::movedToIds` (a
`QStringList`), in creation order. A plain move has one element; a split has
several. `undoReschedule` walks the list.

**Why this and not the back-link everyone had written down.** Both §H.2 of the
reschedule addendum and `AppData.h`'s own SCOPE paragraph named a `movedFromId`
on each piece as the fix. `Event.h` had already **rejected** that exact field,
in a comment written when the forward link was introduced:

> *"ONE DIRECTION, deliberately… two pointers can disagree — a half-applied edit
> leaves a chain that says different things depending on which end you read…
> Derive the reverse, store the forward."*

The repo was arguing with itself, and settling it meant naming what was actually
broken. The reverse question — *"was this block rescheduled from somewhere?"* —
was never the problem; a scan answers it and cannot drift. The problem was that
the forward link could hold **one** id when a split produces **many**. That is a
cardinality defect, and a cardinality defect is fixed by widening the field that
is too narrow, not by adding a second field pointing the other way.

The distinction matters because it preserves the property Event.h was protecting.
With a list, exactly **one** record still owns the move — the original — so there
is nothing for it to disagree with. With a back-link there would be n+1 records
that must agree, and for an ordinary single move the pair would be purely
redundant: precisely the shape the original comment refused.

*Alternative rejected — `movedFromId` on each piece.* Reverses a documented
decision to buy a lookup nobody needed, and re-introduces the disagreement risk
that decision existed to avoid. Rejected twice now, the second time with better
evidence than the first.

*Alternative rejected — a shared `moveGroupId`.* Direction-free: original and
pieces all carry one opaque token, and `outcome == Moved` says which is the
original. It works, but it stores the same fact in n+1 places (the same defect as
the back-link, wearing a third concept's clothes) and buys nothing a list on the
owner does not already give.

## C. Storage: format v14, additive in both directions

- **Write** `movedToIds` (the truth, an array) **and** `movedToId` (its first
  element).
- **Read** `movedToIds` when the key is present; otherwise wrap `movedToId` into
  a one-element list — which reproduces exactly the pre-v14 behaviour, including
  the empty case, since an absent key reads back as `""`.

So a v13 file loads with no migration branch, the sixth time this format has
grown that way.

**`movedToId` is now a compatibility mirror, and a mirror is safe here in a way
it would not be safe in memory.** One door writes both fields in the same
instant, and the loader always prefers the list, so no reader ever chooses
between two live opinions. That is why the domain keeps a **single** field: the
duplication is quarantined to `JsonStore`, where all JSON knowledge already
lives, and nothing holding a live `Event` can observe two versions of the truth.

**The cost, on the record.** A v13 build that opens and re-saves a v14 file drops
`movedToIds` and keeps `movedToId`, collapsing a split back to its first piece
and making it uninvertible again. Every additive bump has had this property —
old builds drop keys they do not know — and it is the price of the no-migration
rule rather than a new risk. It matters slightly more here than usual because
`data.json` syncs between devices that may be on different versions.

**Splits already on disk cannot be retro-linked.** A split made before this
version recorded only its first piece; the siblings are unrecoverable because
nothing ever named them. Those blocks keep working exactly as they do today —
they simply stay uninvertible. Guessing which blocks belonged to an old move is
the kind of guess this codebase does not make.

## D. The inverse is all-or-nothing on the way back too

`undoReschedule` now decides about **every** replacement before touching any of
them:

- Any piece holding **tracked segments** refuses the whole move. Segments are
  time actually sat through, never copied from the original, so they exist
  nowhere else; deleting them to tidy a link trades a fact for a pointer. One
  piece is enough to refuse — and note that a piece *after the first* could not
  even be asked before this change, so that is the case the old design could not
  have failed correctly.
- A piece already deleted by hand is a **repair**, not a refusal, exactly as a
  dangling single link always was — now applied piece by piece.
- All of it under one `Batch`, so a three-piece undo is four mutations that a
  listener sees as one `changed()`.

This mirrors the door's entry contract: `rescheduleBlockSplit` validates every
span before appending anything. A split that can be half-undone would be no
better than one that can be half-applied.

## E. What this deliberately does not do

**The verb's fence stays closed.** `reschedule::Kind::Split` remains outside
`Verb::MoveBlock` (reschedule addendum §I). This slice removes the *reason* §I
cited — there is now an expressible inverse — but letting the assistant propose a
split means a new multi-placement proposal shape in `scrub::`, a card that
renders N placements, and a rewritten model contract. That is a widening of the
write boundary, and the write-boundary addendum is explicit that widening is a
deliberate design act with its own review. It is not a rider on a domain fix.

**No new human undo surface.** Catch-up's "Undo" and "Bring back" still emit
`resolveBlock(id, Unset)`, and `missed::` still excludes `Moved` from both lists
on purpose (*"a moved block has a live replacement on the calendar, and bringing
it back would put the same obligation on the board twice"*). So `undoReschedule`
remains reachable only through the assistant. Giving the drawer a real undo for
moves is a surface change that touches catch-up §K's decisions and deserves its
own reasoning; noting the gap here is not the same as closing it.

## F. Tests

Six new slots in `test_domain`, all headless and in microseconds:

| Test | What it pins |
|---|---|
| `splitIsUndoneWholeOrNotAtAll` | every piece removed, not just the one the old link could name |
| `splitUndoRefusesWhenAnyPieceHasTrackedTime` | the refusal, driven by a **non-first** piece |
| `splitUndoRepairsAPartiallyDeletedSplit` | hand-deleted pieces are a gap, not a reason to strand the rest |
| `splitUndoEmitsExactlyOneChange` | four mutations, one `changed()` |
| `splitRoundTripsEveryReplacementThroughV14` | the list survives the file, in order, and still inverts |
| `aPreV14FileStillNamesItsOneReplacement` | a v13-shaped document loads and inverts as it always did |

Plus the format-version tripwire in `moodUpsertsByDateAndRoundTripsThroughV12`,
which fired on the first run of this change and was bumped 13 → 14 in the same
drop — the behaviour its own comment asks for, after v28.3.0 missed it once.

Six suites, **408 measured** (187 + 22 + 70 + 95 + 19 + 15; was 402). Measured,
not remembered: that is what QTest reports across the six binaries and it
includes each suite's synthesized `initTestCase`/`cleanupTestCase` pair.
