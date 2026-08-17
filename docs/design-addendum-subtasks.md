# Design addendum — Subtasks & sizing (v28.3.0, format v13)

*The design record the roadmap's §I promised. Shipped v28.3.0 — the re-land
of the feature a v27 session built and lost (see §K for that story; it is
part of the design record because it shaped how this drop was made).*

Diagram: `diagrams/subtask_policies.*` — the five query policies at a
glance.

---

## A. What shipped, in one paragraph

A task may now have **pieces**: one level of checklist children ("read the
spec" under "Lab 4"), each a real `Task` with its own optional deadline,
living in the parent's detail panel rather than on the lists. Every task —
parent or piece — may also carry a **size**: `estimateMinutes` (0 = unset)
and `chunkable` ("fits short gaps"). Both facts persist (format v13), sync,
and survive recurrence. The Upcoming card shows a quiet **"☑ 2/5"** chip
when a task has a checklist.

## B. The link lives on the child

`Task.parentId` — a string id pointing **up**, not a list of children on
the parent. Three reasons, in the order they matter (they are also in
`Task.h`, where future readers will actually meet them):

1. `m_tasks` stays **one flat vector**. Every existing query — forty-odd
   loops — learns at most one boolean (`isPiece()`); none learns to
   recurse.
2. **Removal cannot corrupt the shape.** Erasing from a flat vector leaves
   nothing dangling; erasing from a parent's embedded list while pointers
   are held into it is the iterator-invalidation classic.
3. It **round-trips as one string key** — storage, sync and share get the
   feature free, with no nesting in the JSON.

The cost of pointing up: "children of X?" is a scan. Scans are how every
reverse question in this codebase is answered (see the catch-up addendum's
`movedToId` argument) — they cannot drift, and n is small.

## C. One level, enforced at the door

A piece may not have pieces. The type system cannot say this (a `Task` is
a `Task`), so `AppData::addSubtask` refuses it — the same *"when the type
can't, the door does"* move as the time-without-a-date rule. Depth-one is
a product decision, not a technical one: checklists are flat in how people
actually think ("the steps of Lab 4"), and every deeper hierarchy this
design considered turned the detail panel into a tree view and the five
policies below into recursive questions.

Pieces **inherit the parent's category at birth**, and cannot drift from
it afterwards — not because a rule checks, but because **no door mutates a
task's category post-birth** (verified: it is written at `addTask`,
`addSubtask`, and repeat-spawn, nowhere else). Invariant by absence: the
cheapest enforcement there is.

## D. The five query policies

The heart of the feature. Each query answers *"do pieces count?"*
differently, and each answer has a reason — the queries **disagree on
purpose** because they answer different questions:

| query | pieces? | because |
|---|---|---|
| `upcomingTasks()` | **parents only** | workload view; a parent already stands for its pieces — both would double-count the week |
| `tasksIn(category)` | **parents only** | same, per category |
| `tasksDueOn(date)` | **dated pieces included** | a piece due Thursday is a real obligation *on Thursday*; hiding it lies to the calendar |
| `taskCountIn(category)` | **counts everything** | this number **guards** `removeCategory`; undercounting deletes data out from under tasks. The category chip shows the same number — two answers that disagree would be worse |
| completion roll-up | **none, either direction** | ticking the last piece does **not** complete the parent; the tick is the reward and the app never takes it for you |

Tests pin each row by name (`queryPoliciesDisagreeOnPurpose`,
`completionNeverRollsUp`, …) so a future change breaks a *named policy*,
not a vague expectation.

## E. Sizing (§J.1)

`estimateMinutes` is an **int of minutes** because an estimate is
arithmetic, not a clock — it gets summed, subtracted from free time,
multiplied by the coming §J.2 coefficient; a `QTime` does none of that and
wraps at 24 h. **0 means "unset", never "instant"** — readers ask
`hasEstimate()`, and the honest absence matters because §J.2 will divide
by these numbers; fiction in, fiction out.

`chunkable` is a fact only the person doing the work can supply: two
90-minute tasks can differ entirely on whether a spare 15 minutes helps.

Both enter through **one door**, `setTaskSize` — the panel asks them as
one question, so they are one mutation and one repaint (the
coarse-vs-fine granularity argument from `updateTask`, reapplied).

## F. The dialog stays a pure question

`TaskDetailDialog` still mutates nothing. The checklist section records
*answers* (`Piece{id, title, done, archived}`; empty id = born in this
dialog), and two **free functions** beside the class carry the answers
across the boundary:

- `seedTaskDetailPieces(dialog, data, id)` — read the current pieces in;
- `applyTaskDetailAnswers(data, id, dialog)` — turn answers into
  mutations, inside **one `AppData::Batch`**, so however many mutations
  the answers amount to, listeners see one `changed()`.

Why free functions and not methods: four call sites already duplicated the
old one-line apply; pieces would have quadrupled a nontrivial loop, and
four copies of "how do answers become changes" is how call sites start
disagreeing. Putting the pair *next to* the class keeps the dialog itself
`AppData`-free — the pure-question contract survives intact.

Two deliberate niceties: text left in the add-row when **Save** is pressed
still counts (typing a line and reaching for Save instead of Enter is an
answer, not a mistake); a line added and then ✕ed in the same sitting is
a change of mind and is never created at all.

**The constructor grew** (`estimateMinutes, chunkable` inserted before
`parent`) instead of gaining optional seed-setters — deliberately, and the
reasoning is `updateTask`'s v22 comment verbatim: an optional seed a call
site forgot would silently save "unset" over the user's real estimate. A
compile error at all four call sites is the cheapest bug report there is.
(It fired four times during this very drop. Working as designed.)

## G. Cascades — what follows what

| action on parent | effect on pieces | why this and not the alternatives |
|---|---|---|
| archive / restore | **cascades, both directions, unconditionally** | archived-parent-with-visible-pieces = orphan checklist lines pointing at a hidden task; restore that skipped a hand-archived piece would look like data loss. Re-hiding one line is one click |
| `removeTask` | **cascade delete** (+ title-demotion into any block that referenced *any* removed id) | refusing = busywork; promoting orphans = checklist lines silently become full tasks. Note the asymmetry with archive: archive is reversible so its cascade is generous; delete is not, so its cascade is at least honest about the blast radius |
| complete | **nothing** (§D — no roll-up) | the tick is the reward |
| repeat-spawn | spawn **keeps `parentId`, `estimateMinutes`, `chunkable`** | a repeating piece must not promote itself to a full task every cycle; the same job next week takes the same time |

Archiving a lone **piece** cascades nowhere — "get this line out of my
sight" is allowed to mean only that.

## H. Storage (format v13) and orphan adoption

Three additive keys on the task record: `parentId`, `estimateMinutes`
(clamped ≥ 0 on read), `chunkable`. Missing keys read as the struct's
defaults — a v12 file loads as "everything top-level, nothing sized",
which is exactly what a v12 file *meant*. Sixth ride on the
additive-growth train; still no migration branch.

**Orphan adoption** lives in `AppData::resetFrom`, the one door all loaded
data enters through (file, sync, share alike): a piece whose `parentId`
resolves to nothing — or to another piece, i.e. a hand-edited file nested
deeper than the domain allows — is promoted to top level. Why promote
rather than keep or drop: a dangling `parentId` makes a task invisible on
every list surface (those show parents only) while the guards still count
it — **data loss by invisibility**, the worst of both. Why *there* and not
in `JsonStore`: the loader converts one record at a time and cannot see
the others; referential repair needs the whole picture. Two passes, so one
adoption cannot change the verdict on the next piece.

## I. The Batch, and the card chip

**`AppData::Batch`** (RAII): while one is alive, `changed()` is withheld;
the last one out emits once, *iff* anything actually happened. Every
mutation now routes through a private `notifyChanged()` — there is no
direct `emit changed()` left in the file, so the fence has no holes. RAII
and not `begin()/end()` because a forgotten `end()` after an early return
would leave the app permanently silent — a destructor cannot be forgotten
(`std::lock_guard`'s instinct). Written for the detail panel's apply;
v29's tool-use verbs inherit it free.

**The "☑ 2/5" chip** rides the affordability pill's sidecar pattern in
`TaskListModel`, for the twin reason: ticking a piece moves no byte of the
*parent's* `Task`, so the base class's field-diff cannot see the chip move
— the model caches `pieceProgress` per refresh, diffs the cache itself,
and emits the `dataChanged` the base cannot know to. On the card it folds
into the subtitle line, not a new pill: the chip row is contested real
estate, and progress reads naturally in the same quiet grey as the
category. Zero pieces = no chip — never "0/0" noise.

## J. Deferred, deliberately

- **§J.2 multiplier + rewiring `afford::` onto real estimates** —
  **SHIPPED v28.4.0** exactly as planned: pure queries, no format change,
  on the shape this drop built. The proxy survives as the fallback for
  unestimated tasks, and an unsized parent borrows the sum of its
  pieces' estimates (the decomposition dividend).
- **Category-page rows** (`CategoryTaskDelegate`) don't show the chip yet
  — Upcoming is where the feature earns its keep first; polish when the
  page asks for it.
- ~~**Piece visibility gaps** — sharpened by the owner's v28.3 manual
  pass into one chicken-and-egg: **there is currently no UI door to give
  a piece a deadline at all.**~~ **CLOSED v28.5.0 — see §L.** The
  checklist row's title is now the door: it opens the piece's own panel
  (date, time, size, notes — the full form), with a breadcrumb back to
  the parent. The chicken-and-egg is broken at the egg: the piece no
  longer needs a date to become reachable. Still open from the original
  list: pieces cannot be **reordered** (with the v28.5 quiet chip, order
  matters more than it did — filed for a later polish pass).

## K. The re-land itself (a process note)

The original v27 drop was built and never fully applied. This drop was
made **against the current tree, fresh** — the old zip predates v28.0 and
the docs audit, and applying it would have rolled both back. Mid-way, the
session discovered the tree already contained a **half-applied earlier
attempt at v28.3** (a complete `Task.h`, a partially-written `AppData.h`,
nothing else) — the v27 failure mode, repeating. The re-land surveyed that
partial state by grep — and the greps didn't include every symbol, so a
`PieceCount` definition in `Task.h`'s tail went unseen, was written a
second time into `AppData.h`, and became v28.3.0's build-breaking ODR
violation (fixed in 28.3.1 by keeping the `AppData.h` copy). The rule that resolved both incidents is
the same one, and it is the process lesson this feature keeps teaching:
**land whole, or don't land** — and *check*, because a partial application
looks exactly like a finished one until it doesn't compile. The hotfix
adds this feature's second process lesson: **when completing someone
else's half-finished work, read every line of what they left, not just
the lines your search terms happened to match.** Grep answers the
question you asked; only reading answers the questions you didn't.

## L. The piece's own panel (v28.5.0)

*The polish list's headline, closed. No domain change, no format change —
the whole slice is UI wiring, which was the finding that shaped it: a
piece was already a full `Task` (date, time, size, notes), and
`TaskDetailDialog` already edited every one of those fields for any task.
The door was the only missing part.*

### L.1 The door is the title

The checklist row split its two jobs into two targets: the **checkbox**
ticks the piece done (one click — the reward stays cheap, §D's
completion-never-rolls-up reasoning from the other side), and the
**title** opens the piece's own detail panel. A newborn line — typed this
sitting, id still empty — gets a plain label instead of a door: there is
nothing to navigate *to* until Save creates it. Rows also gained a quiet
**"Aug 8 · 45 min" chip** (display-only, seeded, never edited in the
row), so a glance at a checklist answers *which of these pieces are
already real scheduled work*.

### L.2 Navigation is part of the ANSWER

The pure-question contract survives, extended: the dialog **records**
where the user asked to go (`navigationTarget()` — a piece id from the
checklist, or the parent id from the breadcrumb) and performs nothing.
Clicking through **accepts** the dialog rather than rejecting it, because
"I'm going somewhere else" and "undo my edits" are different intents and
only Cancel means the second — a tick made just before the hop must
survive it (pinned: `navigatingAwayStillSavesTheSitting`).

### L.3 `runTaskDetail` — seed, exec, apply, repeat

A third free function completes the family beside `seedTaskDetailPieces`
and `applyTaskDetailAnswers`: seed *reads*, apply *writes*, **run
orchestrates** — a loop (not recursion: parent → piece → parent must not
grow the stack, and recursion would keep every ancestor dialog alive
behind the current one). Each hop re-reads the task fresh by id, so
nothing held across iterations can dangle *or go stale* — which also let
all four call sites drop their defensive `Task` snapshots and shrink to
one line. The same consolidation argument that created
`applyTaskDetailAnswers` in v28.3, one level up: four copies of "how does
a detail session work?" became one.

### L.4 The breadcrumb — and the debt it carries

> **Debt PAID, v28.6.0** — the panel is docked now; the hop is a
> swap-in-place. See `design-addendum-detail-panel.md`. The paragraph
> below stands as the record of why the modal version shipped first —
> and it called the shot: the swap touched `runTaskDetail` and zero
> call sites.

A piece's panel opens with **"‹ parent title"** above the form
(retitled "Piece details"): the path back the polish list asked for.
Because today's panel is a **modal dialog**, the hop is
close-save-reopen — functional, loses nothing, but not the seamless
slide of a docked side panel. **Known debt, on purpose**, handed to the
side-panel slice: when the dialog becomes a docked panel, `runTaskDetail`
is the one place navigation lives, so the modality swap touches one
function, not four call sites.

### L.5 What this unlocks (the finals case)

The reason the slice exists: a heavy piece — "Chapter 3" under "Study for
finals" — can now be given a date and an estimate from the UI. The moment
it has them it flows into `tasksDueOn` (§D's policy, unchanged) and
`tasksNeedingBlock` (which never filtered pieces), earns its own planned
block, and its estimate joins the parent's piece-sum borrow (v28.4).
Zero new domain rules; the scheduling surfaces were ready and waiting.
The remaining half of the idea — a *promoted* piece appearing as its own
line in the workload views, with the parent shrinking to the un-promoted
remainder — is the next slice's design decision, deliberately not
smuggled into this one.

## M. Pieces in the list (v28.7.0) — the §D display amendment

The owner A/B-ed TickTick's subtask flow against the checklist door and
TickTick won: create from the list (right-click → subtask), see the
structure in the list (indented under the parent), edit like any task.
The decisive argument was scale — a checklist inside the parent's panel
stops working past a handful of pieces, and the list is where tasks
live.

**What §D still means.** The amendment is to DISPLAY, not counting: the
category list now interleaves each parent's pieces directly beneath it
(indented 24 px), while every counting query — upcoming, affordability,
week digests — still sees parents only. Structure is not workload.
Interleaving happens in `CategoryTaskModel::buildSnapshot` *after*
`tasksIn`'s parent sort, so a family can never be split by date
ordering (pinned: `categoryModelInterleavesPiecesUnderTheirParent`).
The Upcoming digest deliberately stays parents-plus-☑-chip: a
cross-category digest wants one row per work item.

**The door.** Right-click → "Add a piece" creates FIRST and names
SECOND (TickTick's own order — their "No Title" row is a created
subtask): a real "New piece" exists immediately, the panel opens on it
with the title selected, first keystroke replaces it. A walk-away
leaves an honest row to keep or ✕ — never lost typing. One level holds
at two layers: the menu doesn't offer it on pieces (chrome), and
`addSubtask` refuses piece parents (the wall).

**Indent mechanics worth stealing:** the shift lives in the delegate's
single `geometryFor`, the one function both `paint()` and
`editorEvent()` read — so the checkbox you SEE and the checkbox you CAN
CLICK move together by construction. An indent applied in paint alone
is the classic off-by-N dead-zone bug.

**Both doors stay.** The parent panel's checklist remains for quick
ticks while viewing the parent; the list shows the structure. They
cannot disagree: both read `subtasksOf`.

## N. The size ladder (v28.8.0)

§J.1's spinbox asked "how many minutes?" and answered "720" — honest,
unreadable. The dropdown replacement asks the better question the owner
raised: *how many entries should a duration picker hold?* The answer is
a shape, not a number: **non-uniform steps** (15/30/45m, half-hours to
8h, whole hours to 16h) with the **cap doubling as doctrine** — an
estimate past two workdays is the app telling you to break the task
into pieces, which is this addendum's whole feature. Off-ladder values
insert at their sorted rung (opening ≠ editing), and the label comes
from `minutesLabel`, now the single duration dialect the dropdown, the
piece chip, and the planner share.

## O. Promotion (v28.9.0) — the counting closes

§D let a dated piece into `tasksDueOn`; §L gave it a panel; §M put it in
the list; v28.4's affordability gave every dated task a verdict with no
piece filter. Sum those decisions and a dated, sized piece was ALREADY
its own line of work everywhere — except in its parent's arithmetic,
which still carried the same minutes. A 12h FINALS with three dated 4h
chapters read as 24h of believed work. §O closes the loop with one
trigger, stated once:

**A piece with its own due date is promoted: it answers for itself, and
its minutes leave the parent.** Sized parent → subtract (floor 0, then
proxy basis, honestly flagged). Unsized parent → borrow only the
undated. Undated pieces are untouched — no verdict of their own, still
weighing where they always did, which is why the v28.4 borrow test
passes without edits: the amendment is additive.

The floor case is doctrine again: pieces promoted past the parent's
estimate mean the decomposition outgrew the guess — the pieces are the
truth now, and the parent must not go negative and subsidize unrelated
work. `Report.minutesPromoted` records what left, so a sentence can one
day say "8h here, 4h already living on Chapter 10."

Deliberately NOT changed: the Upcoming digest stays one-row-per-parent
(§M's display decision) — promotion is about COUNTING, and this
addendum's §D/§M/§O now form the full policy: structure in the category
list, one row per work item on the digest, minutes counted exactly
once. Diagram: `piece_promotion.*`.
