# Design Addendum — Week Agenda, Resize, and a Third Kind of Time

**Status: implemented — the shipped application includes everything
below.** Indexed from `design-doc.md` §3; kept standalone as the record of
the session that produced it.

Continues the decision log in `design-doc.md §3`. Records the "Upcoming cards →
week agenda → drag-to-resize → distracted time" run.

---

## 3.21 Upcoming as cards — a presentation-only divergence

*Decision:* the Upcoming panel draws each task as a Special-Days-style **card**
(category-colour accent, large title, countdown headline), built **inline** in
`UpcomingPage`, not via the shared `TaskRow`.

*Why:* `TaskRow` was extracted because Upcoming and Activities needed an
*identical* row — that shared need was its whole justification. This request
makes them **diverge** (cards here, compact rows there). Forcing one shared class
to carry two layouts is worse than a clean split. The card has one consumer, so
it's a private helper, not a new widget class (speculative abstraction, §pattern).
Pure **presentation** — no domain change, no format bump, tests unchanged.

*Note:* this drops `TaskRow` to a single consumer (Activities) — technically the
same smell in reverse. Re-inlining it is a clean future tidy, deliberately not
done mid-feature.

## 3.22 Week agenda — reuse `AgendaWidget` ×7 behind one axis

*Decision:* the Week tab shows a seven-day timeline (agenda on top, review stats
below) built from **seven `AgendaWidget`s** in gutter-less "column mode" sharing
**one hour-label axis**.

*Why:* this is the payoff of `AgendaWidget` emitting signals instead of opening
its own dialogs (§model/view). Because each column is app-ignorant, we tile seven
and let the page decide a click's meaning — *plan on that column's date*. Chosen
over a brand-new custom-painted week widget (throws away tested code) and over
seven full agendas with seven repeated gutters (wide, noisy).

*Enablers, both additive:* the fixed `kGutter` became a per-instance `m_gutter`
(0 hides the label column) — the single-day view stays pixel-identical; and
`kSlotHeight`/`kTopPad` became **public statics** so the axis and columns share
the *exact* grid (one source of truth for geometry, not two drifting constants).

*Planning is shared, not duplicated:* `PlannerPage::planAt(QDate, int)` is called
by both the day view (`m_date`) and every week column (its own date). One rule,
two callers.

## 3.23 Drag-to-resize events — clamp in the UI, guard in the domain

*Decision:* an event's top/bottom edge can be dragged to change its span. The
widget shows a ↕ cursor on the edge, previews the drag **clamped** to the
neighbour/midnight, and on release **reports** the new span; the domain's
`AppData::resizeEvent` enforces the rules and commits.

*Why the split:* the **UI clamp** makes the illegal drag impossible to *express*
(good UX); the **domain guard** makes an illegal span impossible to *exist*
(correctness). They're different jobs — keep both. `resizeEvent` reuses the same
`isFree(date, start, end, ignore=id)` guard as creation and `moveEvent` (one
door, §aggregate-root), adding only a one-slot-minimum floor; it **refuses**
rather than clamps.

*Why the widget can't just call the domain:* `AgendaWidget` holds a
`const AppData*` — a read-only view — so a mutating call is a **compile error**.
Const-correctness *forces* "widget reports (`eventResized` signal), page routes
to the mutable `AppData`." The type system encodes the architecture.

*Resizing the plan leaves tracked `Segment`s untouched* — the plan is an
intention, the segments are facts (same as `moveEvent`).

## 3.24 A third kind of tracked time — `Distracted`

*Decision:* off-task time (procrastination or disruption) is tracked as a third
`SegmentKind::Distracted`, with a `TrackerService::State::Distracted` and a
**Distracted** button beside Focus/Break. Shown in the danger (rose) hue in the
plan-vs-actual bars. **One** umbrella kind, not two (`Procrastination` vs
`Disrupted`) — the distinction blurs in the moment and doubles the surface;
splitting later is a clean additive change.

*Derived, not counted:* `distractedSeconds` is summed from segments in
`eventTotals` (a third bucket) — **no** running counter in the tracker, which
would be a second source of truth that drifts and dies on a crash
(derive-don't-store, §3.5).

*The `else`-trap, found four times:* adding the kind exposed four
`if focus … else …` sites that silently meant "everything non-focus"
(`eventTotals`, the dialog's + the agenda's live-second attribution, the state
label). All rewritten as **exhaustive `switch` with no `default`**, so a future
kind trips `-Wswitch` instead of a silent miscount. *Lesson recorded: an `else`
on an enum is a bug waiting for the enum to grow.*

*Tolerant reader:* one `kindFromStr` maps `focus`/`break`/`distracted` and
**anything else → Focus**, shared by the segment and crash-recovery paths, so old
files load untouched (additive).

---

## 4. Data & persistence — format version 5

`data.json` is now **v5** (v3 → v4 added `Task.description`/`repeat`; v4 → v5
added the `"distracted"` segment kind). All growth **additive**: missing keys and
unknown enum strings read as safe defaults, so older files load with no migration
branch. Verified by `distractedTimeIsBucketedAndSurvivesRoundTrip` and the resize
tests in the 25-test suite. Pomodoro durations remain **outside** `data.json`, in
`QSettings` (§3.18).

## 5. Scope update

**Shipped (~v13):** everything through v12, plus — Upcoming as cards; the
seven-day **week agenda**; **drag-to-resize** events; and **distracted** as a
third tracked-time kind. Automated suite now **25 tests**; format **v5**.

**Non-goals now partially retired:** *drag-and-drop into folders* — done (with
the right-click menu kept); *a task on the calendar* — done as a **read-only**
"due today" strip only. Placing a task onto a specific agenda **time block**
remains deferred.

**Still deferred (fences, not oversights):** task on an agenda time block ·
acting on `repeat` (regenerate on completion) · splitting Distracted into
procrastination vs disruption · a Distracted box in the glance panel · vacation
date *ranges* · folder nesting · special days on the month grid · window/sidebar
state in `QSettings` · Qt model/view refactor · SQLite · Android · sync.

## 3.35 Distracted becomes visible everywhere (owner request)

*The complaint:* "when I'm distracted, I don't want it to be considered
break." And the display was doing exactly that — while the DOMAIN had kept
three buckets since §3.24 (Stats.cpp even carries a comment warning against
folding Distracted into break), two displays lied:

- **GlancePanel's live split was a stale two-way `if/else`** — "not
  focusing" silently meant "on break", so a running Distracted timer ticked
  the BREAK box before the user's eyes. Every other live split in the
  codebase (EventDialog, AgendaWidget) already used the three-way switch;
  this one predated §3.24 and never got the memo. Same failure shape as the
  deleteLater story (§ TROUBLESHOOTING): the cure existed in-project,
  adoption was incomplete.
- **No Distracted box existed** in the glance panel (an explicit §5 fence,
  now retired), the week review, or the month review.

*Decision:* a third `StatBox` in all three summaries, in `theme::danger()`
— the hue the block bars already use for distracted slices; one colour per
meaning, everywhere. The month review pairs **Focused with Distracted only**
(no Break box): at a month's distance, rest needs no audit — drift is the
number worth watching. Category bars still count `t.total()` — all tracked
time credits the life area; the boxes judge the QUALITY of time, the bars
its QUANTITY.

*Pinned by a UI test* (`liveDistractedTimeIsNotCountedAsBreak`): one live
second of Distracted must land in the DISTRACTED box and leave BREAK at 0s —
asserted through the captions the user actually reads.

## 3.37 Category bars count focus only — a documented reversal (owner-driven)

*The falsifying data:* the owner tracked a GTI350 block — 10m focused,
1h08m distracted — and the GTI350 bar proudly claimed **1h 22m**. §3.35 had
ruled "bars report quantity: two distracted hours in School are still two
hours in School" (pinned as K3). Real use proved the bars are READ as
accomplishment: "it states that I did all that work. It's misleading."
Second full reversal of the project (after §3.29's link-clears-label), same
protocol: the old rule's test changes WITH the rule, the question bank gets
a sequel, nothing is silently rewritten.

*Decision:* `byCategory` credits **focus seconds only** — one line in
`Stats`, effective everywhere attribution is consumed (glance bars, week
pie), because attribution was derived in one place. GlancePanel's live
credit follows the same rule (live seconds join a category only while
Focusing).

*Where break and drift go instead:* two **fixed sink rows/slices** —
"Break" (amber) and "Distracted" (danger red) — appended to the glance bars
and the week pie + legend, derived from `totals`. The owner's phrase:
"something fixed that adds up over time." The books balance: productive
time per life area + two sinks = everything tracked.

*Rejected:* routing drift into the owner's hand-made "Wasting time"
category — a magic category name breaks on rename, silently double-counts
if the user also plans blocks there, and the user's categories are THEIRS.
A derived display row has no such failure modes.

*Named for a future session (fence intact, pressure noted):* SUBTYPES of
non-focus time — "how did I waste it: procrastination, doomscrolling; how
did I break: chores, a walk." That is a domain change (Segments would carry
a label), deserving its own addendum: classify → document → fence → build.

## 3.38 Tracking honesty: status changes only while the block is live (owner constraint)

*The rule, in the owner's words:* "If it's 11h and there's a planned block
at that time, we can change its status. If it's 11h, I shouldn't be able to
change the status of a block that starts at 5pm." Actuals may only be
written while the plan is actually happening — no pre-logging the evening,
no back-filling yesterday into fiction.

*Shape:* a pure predicate + a guarded door + greyed buttons.

- **`Event::isLiveAt(t)`** — pure function, half-open `[start, end)` (the
  app's standard slot boundary), caller supplies "now" → every boundary
  unit-tested without touching the wall clock.
- **`TrackerService::canTrackNow(id)`** + guards as the FIRST line of
  `startFocus/startBreak/startDistracted`: a refused start has **zero side
  effects** — in particular it must not stop (or steal from) whatever
  interval is currently running. Refusal means "nothing happened", never
  "something half-happened" — pinned by test.
- **`stop()` is deliberately unguarded** — stopping records the truth of
  what already happened; guarding it would force fiction, the opposite of
  the goal. Corollary: a running interval is NOT auto-cut when its window
  ends; you can keep focusing past the plan and stop when done, but you can
  no longer *switch* kinds on the expired block.
- **Dialog braces:** start buttons disabled unless live, and the idle label
  says WHY ("Not live yet — tracking opens at 5:00 PM" / "This block has
  passed"). A disabled control with no explanation reads as a bug. A coarse
  30 s timer re-runs the gate, because `tick()` only pulses while tracking —
  an idle dialog would never notice its window arriving.

*The clock became a dependency:* `nowProvider`, a `std::function` seam
defaulting to the wall clock. Forced by two discoveries in one test: the
domain's day starts at `plan::kDayStartMinutes` (06:00 — an "all-day"
0–1440 block is refused at the door), and any real-clock liveness test
goes flaky before 6 AM, when NO block can be live. Injected time makes the
tests the owner's literal example: it's 11:30; the 11:00 block tracks, the
5 PM block doesn't.

*Rejected:* auto-stopping at window end (surprising, and the plan-vs-actual
bar already caps at planned); a wall-clock-only implementation (untestable
on demand — same principle as `TICKTIMER_COMPACT`).

## 3.40 Glance polish: zero rows hidden, the day's pie, and UNACCOUNTED time

Three owner requests in one pass.

**Zero rows hidden** — categories and sinks alike: "0s is useless
information." A row where no time went says nothing; the panel now shows
only where time actually went.

**The day's pie** — the week review's `CategoryPie` reused (one chart
widget, every screen). Deliberately no legend of its own: the pie is fed
EXACTLY the rows the bars show, so **the bars are the legend** — same
colours, same order. That identity is a design constraint, not an accident:
if the pie ever ate different data than the bars, the legend would lie.
Hidden on an empty day.

**Unaccounted time** — planned window that has already ELAPSED with nothing
tracked over it ("time passed, nobody pressed anything"). Three decisions:

- **Derived, never stored** (§3.5 at its purest): unaccounted =
  `elapsedWindow(e, now) − tracked(e)`, clamped at zero (tracking past the
  window's end is legal, §3.38). Storing it would record a conclusion that
  goes stale every minute. It shows as a grey sink row + pie slice — grey
  on purpose, the same "no story" semantic as ad-hoc blocks' neutral paint.
- **`now` became a parameter of the Stats API** (defaulting to the wall
  clock) — the `nowProvider` lesson generalised to the pure layer. Every
  boundary (past day, mid-block, future block, over-tracked clamp) is
  pinned by a domain test at a fixed 11:30.
- **The trap that nearly ate it:** the summarize loop short-circuits
  never-tracked events (`if (t.total() == 0) continue`) — and never-tracked
  blocks are unaccounted time's whole subject. The accumulation lands
  BEFORE that early-exit, with a test pinning exactly that case.

*Live-tracking nuance:* the running interval is elapsed-but-not-yet-stored
— literally unaccounted's definition — so the glance panel subtracts live
seconds, or the number would grow while you're actively focusing.

*A real bug caught mid-session (two clocks, one decision):* the dialog's
gate verdict used the tracker's injectable clock while the hint's
future-vs-past wording read the wall clock — they disagreed the moment real
time crossed the test block's 5 PM start, and the suite went red within
hours of the code being written. Fix: the explanation derives from the SAME
`now` as the verdict. (QB L5.)

*Deferred:* unaccounted rows/slices in the week & month reviews — their
legend percentages need a denominator decision (share of tracked vs share
of tracked+unaccounted) that deserves its own minute.
