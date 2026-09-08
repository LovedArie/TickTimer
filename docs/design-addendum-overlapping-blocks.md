# Design addendum — up to three blocks in one slot (v31.3)

*Choice → why → alternative rejected, per `CLAUDE.md`. Read
`design-addendum-schedules.md` first: this changes a promise that one made.*

---

## §V.0 What was actually broken

Testing the v31.2 schedule fix, the owner found that two rules can name the
same day and the same time, and reported it as a bug and then immediately as a
feature: *"I am able to duplicate time slot. Now I can see this as a feature,
so let us make it a real feature. We can have up to 3 events taking place at
the same time slot, but it needs to show in the agendawidget on the
calendar."*

The duplicate rule was not the defect. The defect was what happened next:
**nothing**. `AppData` forbade overlapping events — one of the three aggregate
invariants named at the top of `AppData.h` — so the materialiser reached the
second rule, found the slot taken, and skipped it in silence. That silence was
already on the books: the schedules addendum §S.4 chose "an occupied slot is
skipped, not fought", and its own Coda listed *"a visible signal when a rule
produces nothing because its slot is permanently taken"* as owed.

So there were two ways to close it: make the silence audible, or make the
situation legal. The second is better, because the underlying complaint is not
that the app failed to explain itself — it is that **a lab inside a lecture
block, or two commitments that genuinely collide, is a real thing a planner
should be able to describe.** The old rule was not wrong so much as too strong.

---

## §V.1 The rule becomes a capacity, and the number is three

*Choice:* at most `plan::kMaxConcurrentBlocks` (3) events may cover any single
instant. The constant lives in `Event.h` beside `kSlotMinutes` and
`kDayStartMinutes`, because "you can be booked for at most three things at
once" is a statement about planning, not about pixels — the same test the
grid constants already pass.

*Why three, and why a constant rather than a preference:* a slot is a fixed
width on screen. At four columns a block is too narrow to read its own title,
so four is the point where the feature starts lying about being usable. That
ceiling is a property of the display, not a taste, and a preference here would
invite a value of 10 and a row nobody can read.

*Alternative rejected:* unlimited stacking with a scroll or a "+2 more"
affordance, as a web calendar does. It defers the readability problem instead
of answering it, and it adds a second interaction (expand the pile) to reach
blocks the app is otherwise able to show directly.

---

## §V.2 Concurrency is not a count of overlaps

*Choice:* the gate asks `daylay::peakConcurrency` — the most blocks covering
one **instant** inside the candidate range — not how many events overlap the
range.

*Why it has to be a function and not a count:* blocks at 09:00–10:00,
09:30–10:30 and 10:00–11:00 all overlap the range 09:00–11:00, yet no single
instant carries more than two of them. A count of overlapping events says
three and refuses a perfectly legal fourth. The sweep only has to test the
instants where something **starts**, since nothing else can raise the count.

`concurrencyCountsInstantsNotPairs` in `test_domain` is that exact fixture,
and it exists because the naive version passes every simpler test.

---

## §V.3 `isFree` and `hasRoomFor` are two questions, and both are kept

*Choice:* the capacity gate is a NEW name, `AppData::hasRoomFor`. The old
`isFree` survives, unchanged, still meaning "there is nothing here at all".

*Why the rename rather than re-pointing the old name:* `isFree` meant "nothing
is there". The new rule means "fewer than three are there". Leaving the old
name over the new behaviour is exactly the failure this repo has now logged
three times in `TROUBLESHOOTING.md` — state or a predicate gains a new
consumer and its call sites go un-audited. Renaming makes the compiler walk
every site.

**And it immediately paid.** The compiler found four call sites in
`AgendaWidget` that were not in the plan, and all four turned out to be asking
the *emptiness* question rather than the capacity one: they decide whether to
offer the "+ plan" invitation on a row. A slot holding one block has ROOM, but
it has no empty pixels — a block alone still draws full width — so an
invitation there would promise a click that lands on the existing block. Had
`isFree` simply been re-pointed, the agenda would have started inviting clicks
onto occupied rows, and no test would have said a word.

So the two questions stayed two questions. Creation-by-clicking-space remains
about space; stacking is done by **dragging a block onto another**, or by a
schedule, or by the event doors.

*Alternative rejected:* one predicate with a `mode` or `capacity` parameter.
Two callers would then differ by an argument rather than by a name, which is
precisely the shape that reads as identical during review.

---

## §V.4 The column is derived, never stored

*Choice:* `daylay::columns` computes `{column, columnCount}` per event at paint
time, from the day the widget already has. Nothing is written to `Event`, and
the JSON format stays at 14 — old files load untouched.

*Why:* a column index is not a fact about an event; it is a fact about its
**neighbours**. Stored, it would have to be recomputed and rewritten every
time any neighbour moved, resized or was deleted, on every device — and two
devices could then disagree about a value both could have derived from data
they already share, which is a conflict invented out of nothing. Deriving it
costs a sort of a handful of events per day.

The packing runs in two passes: lowest free column first, then **one width per
cluster** — a maximal run of events chained by overlap. Pass 2 is what stops
the layout twitching: without it a block would be full width where it happens
to be alone and half width a moment later, so a column boundary would appear
and vanish down the length of one afternoon.

`AgendaWidget::slottingFor` derives on every call and caches nothing, matching
`PlannerPage::freeRunsFor` — *"a handful of events per day makes that free,
and stale geometry is exactly what caching would invite."*

*Alternative rejected:* a stored `lane` field on `Event`, as above.

---

## §V.5 `spanRect` had to grow the horizontal half of its own promise

`AgendaWidget::spanRect` describes itself as *"the single place this
conversion exists, so painting, hit-testing, AND the live resize preview all
agree on where things are."* That promise was only ever about the vertical
axis, because the horizontal one was a constant.

*Choice:* the column arithmetic goes **into `spanRect`**, not into the paint
loop.

*Why:* the way this feature breaks silently is blocks drawn in columns while
every click still lands on whichever one the loop reached first. Painting and
hit-testing agree only if they divide the row in the same place, and the only
way to guarantee that is for there to be one place. The resize preview keeps
the block's column for the same reason — a resize changes *when* a block
happens, never who it sits beside, and a preview jumping to full width would
promise a move it is not making.

Each column edge is computed from its own exact fraction of the band rather
than from a rounded column width, so three columns of an odd number of pixels
do not leave a ragged strip on the right.

---

## §V.6 What the invariant was holding up

Relaxing an aggregate invariant is only safe if you know who was leaning on
it. Every screen reads its day through `AppData::eventsOn(date)` and iterates
a list, which is why this was tractable at all — but three places named the
rule explicitly, and they did not fare equally.

| Place | How it depended | Outcome |
|---|---|---|
| `Reschedule.h` | already MERGED busy ranges before its gap walk | fine as written |
| `TrackerService::liveEventNow` | *"at most one — the no-overlap rule at work"* | became ambiguous |
| `Affordability.h` | summed block durations as a plain total | **silently wrong** |

`Affordability` is the one worth the ink. Its comment was not decoration, it
was a proof: *"Busy minutes are a plain sum because AppData's isFree gate
guarantees blocks never overlap — the domain invariant is what makes this loop
exact rather than an estimate."* That was true, and it stopped being true the
moment three blocks could share an instant. A sum then counts a stacked hour
twice and reports **less free time than exists** — no error, no crash, on a
number people plan against.

*The reusable lesson:* **a comment that justifies code by citing an invariant
is a dependency, and it should be greppable as one.** Searching for prose that
names the rule found this in seconds; nothing else would have. The fix is
`daylay::busyMinutes` — a union, not a sum — shared with `Reschedule`, so
there is one answer to "how much of this window is spoken for" rather than
two that can drift.

---

## §V.7 The tracker refuses to guess

*Choice:* `liveEventsNow()` returns every block under the clock;
`liveEventNow()` returns a block only when there is **exactly one**, and empty
otherwise.

*Why not a tie-break:* that id is used to START TRACKING. Every auto-adopt
rule available — shortest block, earliest start, most recently added — is a
guess, and a wrong guess records your afternoon against the wrong block, which
is worse than recording nothing. With one block live, nothing about the old
behaviour changes; with several, you choose by clicking the one you mean.

*Alternative rejected:* adopt the shortest block ("most specific wins"). It
reads well until two blocks are the same length, and it is silent when wrong.

---

## §V.8 §S.4 amended: skip a FULL slot, not an occupied one

The schedules addendum promised that *"an occupied slot is skipped, not
fought — a block you placed by hand outranks one a rule merely predicted."*
The half of that worth keeping is the second clause: a hand-placed block is
never overwritten. It is not overwritten now either; the rule simply stacks
beside it.

So the materialiser fills up to the cap and skips only a full slot. Without
this the feature would not reach the case that prompted it, since the owner's
duplicate arrived as two schedules rather than as two hand-placed blocks.

---

## Coda — what this did not do

* **Task placement still offers only genuinely empty stretches.**
  `PlannerPage::freeRunsFor` reports runs with zero blocks. Stacking stays
  deliberate rather than something the placement helper starts suggesting.
* **The week view** draws no block rectangles, so it has no column logic and
  a stacked day reads there as it always did.
* **No visible signal when a rule skips a FULL slot.** §S.4's original debt is
  narrowed, not paid: the silence now only happens at three-deep, which is
  rare enough to leave, and loud enough to matter if it ever is not.
* **No per-block ordering control.** Which block takes the left column follows
  from start time and a stable tie-break; there is no way to say "put this one
  first", and nobody has asked for one.
