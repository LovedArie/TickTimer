# Design Addendum — "Needs a block": surfacing unscheduled work

**Status: COMPLETE — parts 1 (domain, v21.3), 2 (the gated glance panel,
v21.4), and 3 (placement, v21.5) all shipped.** Continues the decision log in `design-doc.md §3`.
Validated by four rounds of HTML prototyping with the owner
(`prototypes/needs-a-block.html` … `needs-a-block-v4.html` — throwaway;
**deleted with part 3**, per their own rule; recoverable from the session
output archives if archaeology ever wants them).

**The requirement, discovered rather than stated:** task *blocks* have existed
since the block-identity addendum — you can plan a task onto the agenda. What
the app never did was **notice** that an urgent task has no time set aside for
it anywhere. You could create "Lab 4, due Friday", mark it Urgent, and get zero
signal that Friday was coming with nothing planned. That silence is the gap;
this feature is the app noticing.

*(Session archaeology: `06_IterationPlan.md §4b` listed "tasks meet the plan"
as still open. It wasn't — `design-doc.md §5` had already retired it, "done
twice over". The roadmap entry was doc drift; the plan file is corrected this
session. Lesson recorded: check the retired list before writing a roadmap.)*

---

## A. The coverage rule — deadline-aware, and deliberately NOT configurable

*Decision:* a block **covers** a task only when it falls in the window
`[today, deadline]`, where `deadline = max(dueDate, today)` (a task with no
due date has no upper bound — any today-or-later block covers it).

Three consequences, each argued for in prototyping:

1. **A block after the deadline does not count.** "Read chapter 7" due
   Wednesday with a block on Thursday *looks* handled and isn't. The owner's
   call: "this needs to be caught."
2. **A block already in the past does not count.** Monday's block came and
   went; the task is still open; that time didn't do the job. Needs new time.
3. **The `max(…, today)` clamp keeps overdue tasks satisfiable.** Without it,
   a task due last week could never be covered by any placeable block — it
   would nag forever with no action that resolves it. For an app built for
   anxiety-driven procrastination, an unsatisfiable nag is the worst possible
   failure mode.

*Why not a setting:* every relaxation of this rule is a way for the app to
call a task handled when it isn't — the exact failure the feature exists to
prevent. Settings choose *taste*; this is *truth*. (The Settings dialog shows
the rule as a read-only note, so the user learns it exists without being able
to bend it.)

*Explainability requirement:* when a task the user believes is handled gets
flagged anyway, the app owes a reason. `coverage::uncoveredReason` names which
clause fired (block-after-deadline / block-in-the-past); the UI renders it as
a "why" line (part 2).

## B. The flag rule — two independent settings, OR'd

*Decision:* a task **needs a block** when it is open (not done, not archived),
not covered (§A), not currently dismissed (§C), and *either*:

- its **priority** is in the user's always-flag set
  (default: Urgent only), *or*
- its **due date** falls within the user's window
  (default: 3 days; overdue always counts; Off = priority-only).

*Why two settings rather than presets:* the owner's default ("urgent + due
≤3d") is really a priority rule plus a due-window rule, and they're
independent. Modelled that way, "only ever urgent" is window=Off and
"everything this week" is no-priorities + window=7 — every combination exists
without the app shipping a preset for each. A task with **no due date** can
only be caught by the priority rule; the window has nothing to measure.

*Where the rule lives:* `coverage::needsBlock` in `include/TaskCoverage.h` —
a **pure function** of (task, covered?, rule, now). Not in a widget. The
glance panel, the week view, and any future surface all ask the same
function, so they cannot drift — the same reasoning that extracted
`eventLabel()` when three screens each walked Event→Activity→Category
themselves.

## C. Dismissal & re-arm — two independent clocks

*Decision:* "Not today" hides a task until a **return time**; the review
panel **re-arms** on its own schedule. The two clocks are deliberately not
wired together.

*The owner's case, verbatim in spirit:* a dismissal that returns at **21:00 —
while planning tomorrow** — is useful; one that returns the next morning,
after the day is already committed, is too late. Meanwhile the review re-arms
at 06:00. The gap between the clocks is the feature: dismissing isn't "shut
up", it's *defer to a better moment*. Both are settings; setting them equal
is allowed, just never assumed.

*One mechanism, used twice:* both clocks answer "when does this come back?",
so both are the same value type — `ReturnPolicy` (`include/ReturnPolicy.h`):
a mode (end-of-day / at-a-clock-time / after-N-hours) plus its parameter,
with one pure `nextReturn(from)` function. Two bespoke implementations would
be the classic similar-but-subtly-different pair; the second-consumer rule
fired on day one here rather than day three.

*Classification (the domain/settings line, drawn carefully):*

| Fact | Where | Why |
|---|---|---|
| `Task.dismissedUntil` (QDateTime; invalid = active) | data.json, **synced** | A fact about the task — dismissing on the laptop should hold on the phone |
| `Task.dismissCount` | data.json, **synced** | Ditto — the escalation evidence travels with the task |
| the two `ReturnPolicy` values | QSettings | Taste about *this machine's* rhythm |
| "have I reviewed today" (gate memory) | QSettings | A fact about **this device** — if it synced, opening the phone at noon would skip the review because the laptop looked that morning. The pause belongs to each device you plan from |

*Expiry:* `needsBlock` treats a task as dismissed only while
`dismissedUntil > now` — so a stale timestamp can never hide a task even if
housekeeping hasn't run. `AppData::expireDismissals(now)` additionally clears
lapsed timestamps (same startup/midnight pattern as `rollRepeats`), keeping
the stored data tidy; it is a nicety, not a correctness requirement.

## D. Escalation — specificity, not volume

*Decision:* a task repeatedly dismissed **while urgent** climbs a two-rung
ladder, both rungs **derived** from `dismissCount` on every read, never
stored:

- **Rung 1** (default: 3 dismissals): "Not today" stops being one click and
  becomes a decision — *give it time* (opens placement), *the deadline was
  wrong* (opens the existing `DueDateDialog` — reuse, not a new text box;
  the one door `setTaskDueDate` stays the one door), *it isn't urgent after
  all* (drops priority), or *put it off again anyway* (allowed, but it says
  the count out loud).
- **Rung 2** (three past the threshold): the task stays **pinned** above the
  gate — visible past "Show my day", never blocking it.

*Why not louder:* volume is nagging, and a nag you can't quiet becomes a nag
you train yourself to ignore — which then poisons every other signal in the
app. Specificity survives repetition; volume doesn't. The middle two decision
options *change the data*, which is the point: five dodges means either the
deadline is wrong or the priority is, and either correction is honest
information.

*Counter reset:* **on completion** (owner's call) — `setTaskDone(true)` zeroes
the count and clears any live dismissal. A spawned repeat-successor starts at
zero (it's a fresh task; the field defaults handle it). *Fence:* reset-after-N-
quiet-days was considered and deferred; revisit if long-lived tasks accumulate
stale counts in practice.

*Threshold and urgent-only are settings* (`prefs::needsBlockEscalation`);
change the threshold and every task re-rungs instantly with nothing to
migrate — derive-don't-store paying rent.

## E. The gate — a pause, not a toll booth

*Decision (part 2, recorded now while fresh):* when anything qualifies, the
list **takes over the glance panel**; the day's numbers appear after "Show my
day". Three load-bearing properties:

1. "Show my day" is **always available** and **dismisses nothing** — the
   tasks collapse to a strip, they don't vanish. If clearing the list were
   the price of seeing your numbers, the cheapest coin would be "Not today"
   ×N, and the app would be training the habit it exists to fight.
2. When nothing qualifies, the panel is exactly what it always was.
3. Disabling the gate in Settings resets its memory, so re-enabling re-arms
   honestly rather than resuming a stale "already looked" state.

*Tone:* the prototype carried a gentle/urgent tone switch. **Cut** (owner's
call). Ship gentle — the calm, non-shaming register the Supplementary Spec
already mandates; a settings row is permanent furniture and this one existed
to avoid a decision, not to serve a need.

## F. Efficiency fence — the bounded scan

`needsBlock` runs for every task on every `AppData::changed`, and the
coverage check walks events. After a year of real use that's the whole
history answering a question that only concerns today onward. *Decision:*
the flag path scans **only events dated today or later** (one pass, bucketed
by taskId, then per-task checks). `uncoveredReason` may look further back
(it wants "time was set aside Monday — it didn't happen"), but it runs only
for the handful of *flagged* tasks, on demand. Decided now rather than
discovered later as a slow glance panel.

## G. Storage & sync

Two additive keys on the task object — `dismissedUntil` (ISO datetime; absent
or empty → invalid → not dismissed) and `dismissCount` (absent → 0). Format
version **9 → 10**; tolerant read as always, zero migration. Because
`JsonStore`'s conversion feeds both the disk and the sync wire, the fields
sync with no further work — the same free ride `taskId` got in v6.

## H. Fences (deliberate, not oversights)

- ~~**Part 2 — the gated glance panel**~~ — ✅ **shipped, v21.4.**
  `NeedsBlockCard` (card / strip / clear states, decision menu, put-off
  strip with "bring back") inside `GlancePanel`, which stays a CONST view:
  the card reports via signals, `PlannerPage` decides (deadline edits reuse
  `DueDateDialog`; dismissals compute `until` from the prefs clock at fire
  time). The gate's whole state machine is one derived line —
  `gateOpen = reviewPolicy.nextReturn(lastReview) > now` — ReturnPolicy's
  third consumer, no stored open/closed flag anywhere. Settings grew the
  two policy editors (one helper, built twice), the rule, escalation, and
  the gate toggle; `expireDismissals` runs at startup and midnight beside
  `rollRepeats`. The rebuild uses `deleteLater` — a dismiss click destroys
  the row whose handler is still on the stack, the founding crash of
  test_ui.cpp, pinned again by `escalatedRowDemandsADecision`. Part-2
  interim for "Find time": it switches to the day view (the picker already
  lists open tasks); part 3 replaces this.
- **Part 3 — placement:** multi-day "Find time" (day strip showing each
  day's largest free run, earliest-that-fits preselected, past-deadline days
  shown-but-refused), and the panel on the week view. Note for part 3: the
  list is view-independent (the viewed date is not an input to `needsBlock`),
  so the week view renders the *same* derived list, not a copy.
- **Re-arm mode "only when something new qualifies"** — prototyped, deferred;
  it's gate logic (part 2), not a `ReturnPolicy`.
- **Tone setting** — cut, see §E.
- **Dismissal history beyond the count** (timestamps of each dismissal) —
  not stored; the count is the evidence the ladder needs.
- Observed while building: `diagrams/_style.puml` is referenced by
  `quickadd_flow.puml` but absent from the repo — restore or inline it.
