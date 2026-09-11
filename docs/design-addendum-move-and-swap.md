# Design Addendum — Moving and swapping planned blocks (v31.2)

*Status: **approved by direction (2026-09-11)**. Extends
`design-addendum-schedules.md` (occurrences as real Events) and amends one
sentence of `design-addendum-overlapping-blocks.md` §V.3. Prototype:
`prototypes/move-and-swap.html`, tested by the owner on desktop before any C++
was written.*

**Origin:** "I find myself wanting to do this often." Two gestures, in the
owner's words:

| Request | Classification | Why |
|---|---|---|
| "Plan B at 13:00 has to be done at 10:00 instead, so Plan A takes 13:00" | **domain change** | a new door: two blocks change place as one edit |
| "Move Plan A to another day that is open, without deleting and re-creating it" | **domain change** | nothing today changes a block's DATE |

*A note on the label:* "v31.2" already names two working-tree slices
(merge-before-asking, schedules §S.11b) that shipped inside the release stamped
31.1.0 — see `06_IterationPlan.md` §3e. This is the first time the label is
also a release number. The release number is the one to quote.

---

## §M.1 A move happens IN PLACE, never through `rescheduleBlock`

*Choice:* `AppData::moveEventTo(id, date, startMin, now)` rewrites the block's
date and start and keeps its length. Same id, same identity, same note, same
links.

*Why:* `rescheduleBlock` is the catch-up card's door, and it answers a
different question. A MISSED block is history — "you planned this and it did
not happen" is a fact worth keeping, so that door leaves the original behind,
marked Moved, and creates a replacement. A block you move on Friday morning
for Saturday has not been missed; nothing happened, so there is nothing to
keep. Keeping the id also means alarm keys, `movedToIds` pointing at this
block, and the sync row all follow it with no bookkeeping.

*Alternative rejected:* one "move" door for both cases, with a flag. The two
answers disagree about whether the original survives, and a flag is exactly
the shape that reads as identical at the call site.

---

## §M.2 What cannot move, in the order it is asked

*Choice:* one pure function, `blockmove::problemWithMove`, returns the first
sentence that applies — empty when the move is legal. Then the ordinary room
check (`daylay::problemWith`) runs on the day as it would be.

1. **Already decided in catch-up** (`outcome` set). The verdict is about that
   slot; moving the block would leave a verdict describing a time it no longer
   occupies.
2. **Already over** (`missed::hasEnded`). A past block is catch-up's business
   (§M.1).
3. **Being timed right now.** `TrackerService` stops the timer the moment its
   block stops being live, so the move would silently end a session and leave
   a segment outside the plan.
4. **The target has already started.** You cannot plan a time that has passed.
5. **Tracked time, and a different day.** "The time you already spent belongs
   to the day you spent it" — `rescheduleBlock`'s sentence, and it applies here
   unchanged. The same block may still move *within* its day, which is what
   the dialog's nudge buttons have always allowed.
6. **Its rule already has an occurrence on that day** (§M.4) — ignoring a swap
   partner that is leaving.
7. **It is already there.**

*Why a sentence and not a bool:* the same reason `recur::problemWith` and
`daylay::problemWith` return one. Whoever refuses should explain, and one
definition means no screen can be kinder or harsher than the aggregate root.
The drag preview quotes it as you hover.

*Why `now` is a parameter:* the rule in `CLAUDE.md` — a door that decides what
"the past" means takes the clock as an argument, so tests pin it and the debug
panel's fake clock reaches it through `TrackerService::nowProvider`.

---

## §M.3 A swap trades START times; each block keeps its own length

*Choice (owner, 2026-09-11):* A goes to B's date and start, B to A's; lengths
stay. `swapEvents` is all-or-nothing and emits one `changed()`.

*Why lengths stay:* the length of a block is part of what was planned ("two
hours of study"). The prototype put both options side by side and the owner
picked this one.

*The case that decides the implementation:* each side is checked against its
day **with both moved copies applied** (`blockmove::dayWith`). Checking each
block against today's day while merely ignoring both ids is wrong for adjacent
blocks: A 10:00–10:30 and B 10:30–12:00, with two more blocks at 10:30–11:00,
passes that check and lands four blocks at 10:30. A test pins it.

*Alternative rejected:* trading exact slots (start AND end). It silently
resizes both blocks, which nobody asked for.

---

## §M.4 Recurring occurrences: one rule for skip dates

*Choice (owner, 2026-09-11):* moving or swapping one occurrence of a rule moves
only that occurrence, with no question. The "this one, or all future ones?"
question (backlog F8) arrives next iteration with delete-one-occurrence (F6),
answered once for both.

The bookkeeping is **one rule**, applied after every move or swap to the dates
a block left and arrived on:

> For a date the rule produces, "skipped" means "has no occurrence there".

So moving Tuesday's lecture to Wednesday skips Tuesday (or
`syncSchedules`, which keys occurrences by `scheduleId@date`, would re-create
it). Moving it back un-skips Tuesday, and the block is an ordinary occurrence
again. A date the rule never produces — Wednesday, for a Tuesday rule — is
never written into the skip list at all.

*Why one reconciling rule rather than "skip the origin":* "skip the origin" is
wrong the second time a moved occurrence moves (its origin is then a date the
rule never made) and wrong when it comes home (the stale skip would outlive the
block). The same stray skip existed in `removeEvent` for an already-moved
block; the rule fixes that door too.

*Alternative rejected:* detaching a moved occurrence from its rule (clearing
`scheduleId`). Simpler, but a moved lecture is still that lecture: it would
lose its reminder lead time, and a later "delete all future" would miss it.

---

## §M.5 A moved occurrence counts as touched — derived, not stored

*Choice:* `occurrenceIsUntouched` gains one condition: an occurrence **displaced
from its rule** (not on a date the rule produces, or not at the rule's times)
has had something happen to it. Displacement is computed
(`blockmove::isDisplaced`), never stored.

*Why:* editing or deleting a rule withdraws its untouched future occurrences
and re-makes them. Without this, the lecture you moved would be withdrawn and
re-made at the rule's time — your move silently undone by an unrelated edit.

*The trap this avoids:* `updateSchedule` overwrites the rule before it
withdraws. Judged against the NEW rule, every occurrence of a time-edited rule
looks displaced and nothing would be re-made. So the predicate takes the rule
**as it was**, and `updateSchedule` copies it first.

*Is this a meaning change?* No — the question is still "is it safe to withdraw
and re-make this block?", and the answer is simply more careful. `CLAUDE.md`'s
rename rule is for predicates whose question changes. The signature did
change, so the compiler walked the one caller anyway.

*Visible consequence:* an occurrence nudged by the dialog's buttons now also
survives a rule edit. That is the same principle, and it was arguably a bug
before.

*Alternative rejected:* a stored `movedByHand` flag. It would be a second copy
of a fact the dates and times already state, and two devices could disagree
about it.

---

## §M.6 Dropping a block ONTO another block swaps them

*Choice (owner, 2026-09-11):* on the calendar, a drag released over another
block asks for a swap; released over space, a move.

*This amends `design-addendum-overlapping-blocks.md` §V.3*, which said
"stacking is done by dragging a block onto another". That drag was never built.
The hand route to stacking is now a move into space whose length overlaps a
neighbour with room — or a schedule, as before. Stated here so the change reads
as a decision rather than a regression.

---

## §M.7 The desktop drag is hand-rolled, not `QDrag`

*Choice:* `AgendaWidget` tracks press → drag distance → release itself, paints
a ghost at the snapped start (keeping where you grabbed the block), and asks
the domain's `why…` sentences for the preview. `WeekAgendaView` maps the
pointer to the column under it, because the mouse grab stays with the column
the press started in. Off by default (`setBlockDragEnabled`), so the Compare
dialog's agenda is unchanged.

*Why:* `QDrag::exec` runs a nested — on Windows, an OLE — event loop.
Offscreen tests cannot drive it, and `deleteLater()` rebuilds triggered by
`changed()` would linger under it (the nested-loop trap in `CLAUDE.md`).
Manual ordering reached the same verdict against `InternalMove` for the same
reason: a behaviour you cannot produce in a test is one you cannot keep.

---

## §M.8 On the phone: hold, then tap where it goes

*Choice:* holding a block (the widget's existing 450 ms threshold) opens
**Open / Move or swap…**; "Move or swap…" enters a moving mode in which one
stationary tap on an open slot moves the block there and a tap on another
block swaps. Date navigation stays available, a refusal stays in the mode
with its sentence, and the same mode is reachable on the desktop from a
right-click and from a button in the block dialog.

*Why not drag:* inside a `QScroller` page a drag receives its press and its
release and nothing in between (`ReorderListView.h`). *Why not
`contextMenuEvent`:* Android does not reliably synthesise it from a long press
— recorded against a real device in `ReorderListView.cpp`. *Why a menu rather
than picking the block up at once:* delete-one-occurrence (F6) joins it next
iteration. The prototype offers both; the owner confirms on a device.

---

## §M.9 No format change — and what that costs

Nothing new is stored: dates, times and skip lists already exist. So the file
stays at format 16 and old binaries are not refused by the floor. The cost is
one behaviour an old copy gets wrong: a 31.1.0 binary still uses the old
"untouched" test, so a rule edit made *there* withdraws a moved occurrence and
sync carries the deletion. Replacing the phone copies at release (backlog H5)
is the mitigation, and it is part of this release, not housekeeping.

A cross-day occurrence move edits two rows — the block and its rule's skip
list — so a rule edited at the same moment on another device becomes a sync
question rather than a silent merge. That is the merge doing its job.

---

## §M.10 Known limits

- The dialog's nudge buttons still use `moveEvent`, which does not apply §M.2's
  refusals. Routing them through `moveEventTo` would need a clock the dialog
  does not have, and nudging within a day was never the dangerous case.
- The drag autoscrolls only while the mouse moves.
