# Design Addendum — Ordering by hand (v31)

*Status: **approved by direction (v31)**. Extends
`design-addendum-model-view.md` (v20), whose model/view family this joins.*

**Origin:** one owner request — *"I want to click and hold a task and or
activity and change the order for sorting."* Classified as a **domain
change**: which order a list is in must survive a restart and must reach
the phone, so it is a stored fact, not a preference.

---

## §O.1 The order is data, not a preference

**Choice:** `Task::sortKey` and `Activity::sortKey` (dense integers,
renumbered 0..n-1 on every reorder), plus `Category::SortMode { Smart,
Manual }`.

**Why data and not `QSettings`:** `CLAUDE.md`'s standing division is
"preferences never sync". The order you dragged your lab work into is not
per-device taste — it is part of the list, and a list that reads
differently on the phone than on the laptop is a list you stop trusting.

**Why the MODE is stored too, rather than derived:** something has to say
which of the two orderings a list is currently giving. Deriving it ("is any
`sortKey` non-default?") is a guess that gets louder as it ages — one
imported file with stray keys and every list silently switches. A stored
enum can be wrong only if someone writes it wrong.

**Why the mode hangs off the CATEGORY:** the right answer is genuinely
per-area. A course's deliverables want the deadline order; a morning
routine is a sequence and its dates mean nothing. A single app-wide setting
would be wrong for half the areas whichever way it was set.

## §O.2 The first drag must not scramble the list

**Choice:** `moveTaskBefore` / `moveActivityBefore` seed from the order the
user is **currently looking at**, move the one row, renumber densely, and
(for tasks) flip the area to Manual.

**Why:** before the first drag every `sortKey` is 0. Renumbering from the
stored keys would impose an arbitrary order on rows that had a meaningful
one a moment ago — you would drag one task and watch the other nine
rearrange themselves. Seeding from the display makes the smart order become
the manual order, so exactly one row moves. It is also what makes upgrade
invisible: `activitiesIn` uses `std::stable_sort`, so a pre-v31 file whose
keys are all 0 comes back in insertion order — the list the user saw
yesterday.

**Why neighbour ids and not a target index:** an index is a fact about a
list computed somewhere else, and the two can disagree between the drop and
the call. An id cannot go stale into a *different valid answer*, only into
one that is refused.

**Why dense renumbering** rather than the midpoint-of-two-floats trick real
editors use: a life area holds tens of rows, one pass costs nothing, and
integers that are always 0..n-1 cannot drift into the pathological state
where every gap is spent and a reorder silently does nothing.

## §O.3 Done sinks in both modes

**Choice:** `tasksIn` sorts finished tasks to the bottom before it consults
`sortKey`.

**Why:** this is the one place manual order is overruled, and it earns it.
"Manual" is a claim about the order of the work you still have to do; a
ticked task holding third place is not an arrangement anyone chose, it is
where the task happened to be when it was finished.

**Activities have no mode at all**, and the asymmetry is real rather than an
oversight: they have no deadline, no done-state and no priority, so there
was never a "smart" order to choose between — only the accident of insertion
order. They are always in hand order. This is precisely why `SortMode`
lives on `Category` rather than being a global "ordering" setting that would
have to pretend to govern both lists.

## §O.4 Manual is not a one-way door

**Choice:** a control on the TASKS caption — *"Sorted by hand · use
deadlines"* — appears only while the area is Manual, and switching back to
Smart **keeps** the keys.

**Why:** without it, the first drag would be a decision you could not
revisit except by dragging every row into place again. Keeping the keys
means a user who flips to Smart to check a deadline and flips back finds
their arrangement intact rather than erased by a glance.

**Why it sits on the caption:** the same rule the area switcher and the
task-notes toggle follow — put the control on the thing it affects;
distance is what creates the need for a label explaining what a control
acts on.

## §O.5 Hold the handle — and the five attempts it took

**Choice (final):** a drag handle at each row's leading edge, on every
device. A mouse **drags** it. A finger **holds** it, which puts the list in
reorder mode and lets that same finger drag the row; releasing drops it. A
hold anywhere *else* on the row opens a menu with Move up / Move down.

**This section is mostly a record of being wrong**, because four of the five
attempts look right on paper and every one of them was disproved on a
device rather than by reading. The competitor throughout is `QScroller`,
which the enclosing `QScrollArea` grabs a flick gesture on.

1. **Grip drag.** A press starting in a narrow strip can mean only one
   thing — true, and irrelevant, because the **moves never arrive**. The
   press lands, the release lands, `QScroller` eats everything between as a
   pan. It worked exactly once in testing: in the one state where the page
   was pinned at a scroll limit and the scroller declined, which made it
   look flaky rather than absent.

2. **Accepting `TouchBegin`** on the viewport (`WA_AcceptTouchEvents`), on
   the sound rule that an accepted touch sequence is not offered to
   ancestors. Not enough: the flick is recognised through `QGestureManager`,
   which filters at **`QApplication::notify` — before the target widget's
   `event()` or `viewportEvent()` runs.** *A child cannot win this from
   inside its own event handler.* This one fact is the spine of everything
   below.

3. **Abandoning the drag** for Move up / Move down in the long-press menu.
   It works. The owner's verdict: *"it does work. But I don't like it."*

4. **Reorder mode with ▲▼ arrows**, built on taps because taps had never
   failed. Also works, also kept (§O.5a) — but still not the gesture asked
   for.

5. **Hold the handle.** The one that works, and it is *not* attempt 1
   retried. **A hold is not a drag.** `QScroller` delays a press only until
   it can rule the gesture out as a pan; a finger that has sat still for
   half a second has already been ruled out, so the moves that follow a
   hold *do* reach us where the moves following a bare press did not. The
   evidence came from the owner, not from analysis: reporting the long-press
   menu working proved holds get through.

**And then the last collision.** With the drag working, the page scrolled
*at the same time* — both were consuming the same moves. Per (2) there is no
out-voting from inside, so the page's flick gesture is **ungrabbed** for the
duration of a hold-drag and grabbed back on release, with any in-flight
fling stopped first. The pair is flag-guarded, every exit routes through
`cancelDrag()`, and the destructor releases it too: an unbalanced ungrab
leaves a page that can **never** scroll again, which is far worse than the
bug it fixes.

**Known limitation, named rather than discovered:** with the page frozen,
a drag reaches only as far as the visible screen. Long lists need several
passes, or the arrows. Edge auto-scroll during a drag is the proper answer
and is deliberately not in this drop.

### §O.5a The arrows stay

Reorder mode survives as the second door: it is the only route a keyboard
has, it is what a hold on the handle drops you into, and it is what the
gesture degrades to if the drag half ever fails again. In that mode a row
trades its right-hand cluster for two 48dp arrows — which is also the only
version of these rows that has ever cleared Material's target floor, since
the due badge and archive pill are out of the way.

### Coda — what the suite could not have caught, and what it did

The desktop suite was green through every one of the five attempts and
would have stayed green. `QTouchEvent` cannot be forged from a test —
`QMouseEvent::source()` is set by Qt and no public API can fake it, which
`AgendaWidget::setTouchGesturesForTesting` already documents. Gesture
routing is **outside what this suite can promise**.

Neither could `adb`. `input swipe X Y X Y 900` is not a faithful hold: it
reported the long-press menu as broken when the owner's finger found it
working. **A synthetic gesture is not evidence about gestures.**

What the suite *did* catch, before the phone saw it: reorder mode reset
inside `refreshDetail()`, which runs on every `changed()` — so a single tap
on an arrow ejected the user from the mode. It surfaced only because the
ejected mode let a row tap open a modal, and a modal hangs a headless test.
The lesson cuts both ways: the suite cannot see the gesture layer, and it
sees state bugs underneath it that a person tapping would have blamed on
the gesture.

## §O.6 The view reports, the domain moves

**Choice:** the whole drag is hand-rolled — press, threshold, `QDrag`,
indicator, drop — and `ReorderListView` emits `reordered(movedId, beforeId)`
without ever touching the model.

**Why not Qt's `InternalMove`:** it asks the **model** to move rows, which
means `mimeData()`, `dropMimeData()`, `supportedDropActions()` and a
`removeRows()` that really removes. Our models hold a *snapshot* re-derived
from `AppData` (`TaskSnapshotModel`) — they own no rows to move, and
teaching them to pretend they do would put a second, fictional source of
truth beside the real one. Every one of those overrides would exist purely
to be undone by the next refresh.

About forty lines of event handling buys a model that stays honest about
owning nothing. It is `CategoryTree`'s rule one widget family over: the
widget reports what the user did, the domain decides what it means and
whether it is allowed, and the view is rebuilt from truth.

## §O.7 The activities list became model/view

**Choice:** `ActivityListModel` + `ActivityRowDelegate`, replacing the
hand-built `QHBoxLayout` rows refilled on every `changed()`.

**Why:** widgets in a layout have no drag machinery at all; item views do.
The conversion was forced by ordering, not chosen for elegance.

**Why this model RESETS where `CategoryTaskModel` diffs:** the diff exists
to stop a checkbox toggle rebuilding a long list. An activity list is a
handful of rows with no per-row state to preserve — no selection, no editor,
no checkbox — so a reset costs a repaint of six rows and buys the absence of
a `rolesEqual()` that must be kept in step with the delegate. Reach for the
diff when there is something to protect.

**The row's ONE exit**, decided in `geometryFor` from the same number
`removeActivity` consults: `×` when the activity has never been used,
`Archive` when it has. The older rule, restated — never draw a button the
aggregate root will bounce.
