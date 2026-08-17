# Design addendum — Model/View, made real (the Upcoming page)

*Session: convert the Upcoming page from rebuild-on-change to a real Qt
model/view pipeline. Companion to `design-doc.md`; question bank section Y;
diagram `diagrams/model_view_pipeline.mmd`.*

---

## A. The archaeology: every list in this app is a bonfire

Open any page in TickTimer before this session and you find the same move.
On `AppData::changed()` the page **deletes every child widget and builds them
all again** from a fresh query — `UpcomingPage::rebuild()`, `ArchivePage`, the
Activities detail pane. It is honest (the list can never go stale — there is
no second copy to drift) and at this data size it is instant. But it is the
*opposite* of how Qt is built to show a list, and `READING_GUIDE.md` has
flagged it for a while as the one missing fundamental: **model/view.**

This session pays that debt once, on the cleanest possible candidate, so the
pattern exists in the codebase to point at.

## B. Why the Upcoming page, and nothing else

Upcoming was chosen on four counts, each of which lowers risk or raises the
teaching value:

1. **It is a derived view.** It owns no data and no save code — it is a
   question (`upcomingTasks()`) asked fresh. Convert its *rendering* and you
   cannot corrupt anything on disk.
2. **It already had a filter.** The All / Urgent / Medium / Low lenses used to
   `std::remove_if` a copy of the list and rebuild every widget. That maps
   *exactly* onto a `QSortFilterProxyModel` — so the proxy earns its keep on day
   one instead of being ceremony.
3. **Its rows are custom cards**, not plain text — a real reason for a
   `QStyledItemDelegate`, which is the model/view skill that actually shows up
   in production Qt.
4. **It is self-contained.** One page, one consumer. `TaskRow`,
   `ActivitiesPage`, and `AppData` were not touched, so any regression is
   boxed into one screen — and the existing UI tests prove the box held.

## C. The pipeline, and who owns what

    AppData ─changed()▶ TaskListModel ▶ TaskFilterProxy ▶ QListView
     (truth)            (snapshot,        (lens: filter    (+ TaskCardDelegate
                         listens to         + sort)          paints each row)
                         changed)

Four small classes, each with one job:

- **`TaskListModel : QAbstractListModel`** — the adapter. It answers two
  questions for the view: `rowCount()` and `data(index, role)`. It holds a
  **by-value snapshot** (`QVector<Task>`) taken from `upcomingTasks()`, because
  that function hands back `const Task*` pointers *into* AppData's own vector,
  which move in memory the instant a task is edited (the old card code copied
  `*task` "by value — the vector may move on edit" for exactly this reason). It
  connects to `AppData::changed()` and re-snapshots itself — so the *model* now
  owns the connection the page used to own.

- **`TaskFilterProxy : QSortFilterProxyModel`** — the lens. Two overrides carry
  it: `filterAcceptsRow()` (keep a row only if its `PriorityRole` matches the
  chosen lens) and `lessThan()` (order by due date, ties by locale-aware
  title — the same ordering `upcomingTasks()` used, now expressed as view
  logic). The source model never learns it is being filtered.

- **`TaskCardDelegate : QStyledItemDelegate`** — the brush. `paint()` draws one
  card; `sizeHint()` gives its height. It also draws the **section header**
  (see §D). It never builds a widget.

- **`UpcomingPage`** — now just *wiring*. It builds the pipeline once, connects
  the delegate's signals to AppData, and points the lenses at the proxy's
  filter. It rebuilds **nothing** on a data change — the model does the work
  and the view repaints. That is the headline: the page shrank from 283 lines
  of widget assembly to 177 lines of setup, and its per-change cost went from
  "rebuild everything" to "zero."

## D. Section headers in a flat list (the one real trick)

A `QListView` has no notion of groups, but Upcoming needs its Overdue / This
week / Later bands. The standard technique, used here:

1. The model exposes a **`BucketRole`** (0/1/2) computed from the due date.
2. The proxy sorts by due date, so rows of the same bucket are **contiguous**.
3. The delegate draws a header **only when a row's bucket differs from the row
   above it** (`startsBucket()`), and reserves the extra height for it in
   `sizeHint()`.

One header per group falls out for free, and it survives filtering: hide every
overdue row and the first surviving row — now "this week" — becomes row 0 and
draws its own header. `sizeHint()` and `paint()` read the same `geometryFor()`
helper, so the header height they reserve and draw can never disagree.

## E. The tradeoff you pay for painting: interaction

This is the part worth understanding, because it is the cost side of the ledger.

A rebuilt row is a real `QCheckBox`, a real `QPushButton` — you `connect()` to
them. A **painted** row has none of that; there is a *rectangle where a
checkbox is drawn*, and nothing to connect. So interaction becomes
**hit-testing**: `editorEvent()` catches the mouse, works out which zone was
hit (checkbox, the ×, or the card body), and emits a signal —
`doneToggled` / `deleteRequested` / `editRequested`. The page wires those to
`AppData::setTaskDone` / `removeTask` / the detail dialog, exactly as the old
`TaskRow` "forwarded to AppData and held no truth of its own." Same contract;
different mechanics. `geometryFor()` is again the single source of truth, so the
checkbox you *click* is the checkbox you *see*.

One rule carried over verbatim from the old card: the edit dialog is parented
to `window()`, never to the view. Saving resets the model and the clicked row
vanishes; a dialog parented to a disappearing row is the double-free the app
already fixed once.

## F. What this buys, measured honestly

- **Scaling.** The old page built `N` QFrames + `N` checkboxes + `2N` labels +
  `2N` buttons *per rebuild*. The delegate paints `N` rows with one object.
  Below a few hundred tasks nobody would notice; the point is the ceiling moved.
- **Testability — the real win here.** The old list could only be checked by
  driving a live widget tree and hunting `QPushButton`s by their text (slow,
  and it lives in `test_ui`). The model answers questions directly:
  `test_taskmodel` pins roles, filtering, sorting, and the reset-on-change
  behaviour in ~2 ms with no widgets at all. A model *is* unit-testable in a
  way a rebuilt widget tree never was.
- **A pattern to copy.** The next list that wants this (the Activities task
  pane is the obvious candidate) now has a worked example in the tree.

## G. What did NOT change, on purpose

The other pages still rebuild on change. That is fine: this was a *lesson and a
template*, not a crusade. `TaskRow` stays (Activities still uses it). `AppData`
was untouched — the domain neither knows nor cares that one of its consumers
stopped rebuilding. The **behaviour is identical on screen**; the proof is that
the existing UI regression suite passed unmodified except for the one test that
was coupled to the old implementation detail (titles-are-buttons), which was
*retargeted to the model* — a better test, since it now checks the source of
truth a delegate merely renders.

## H. Tests

- **`test_taskmodel` (new, 6 cases):** the model shows only dated+undone tasks;
  every role answers correctly; `BucketRole` classifies relative to today; the
  model re-snapshots on `changed()` without a manual refresh; the proxy filters
  by priority reversibly; the proxy sorts by due date then title.
- **`test_ui` (+1, now 23):** `upcomingLensesFilterByPriority` retargeted to
  read the view's model; `upcomingDelegateHitTestsClickZones` proves
  body→edit, left→done, right→delete by calling `editorEvent` with a known row
  rect.

Totals after this session: **60 domain + 6 model + 23 UI + 19 auth (+ 11 live)
= 108 automated, 119 with the server suite.**

---

## I. From sledgehammer to scalpel: granular updates (v20.1)

The v20 model above was honest but blunt: `refresh()` called
`beginResetModel()` on **every** change. That works, but it is essentially
*rebuild-on-change wearing a model's coat* — the view discards its scroll
position and selection each time, and can never animate a row in or out. The
whole reason model/view beats rebuild is the thing v20 wasn't yet doing:
**incremental updates.** v20.1 cashes that in.

### The obstacle, restated

Our source is a *derived query* (`upcomingTasks()`) that recomputes wholesale.
It cannot tell us "row 3 changed" — it just hands back a new list. So the model
has to **reconstruct** what changed by diffing the previous snapshot against the
new one, then emit the matching signals. That diff is the lesson.

### The decision `refresh()` now makes

(See `diagrams/model_refresh_decision.mmd`.)

1. **Same ids, same order?** → only content could have changed. Walk the rows;
   emit `dataChanged` for any whose *visible* fields moved. Crucially, "visible"
   means a field the **card paints** — title, category, due date, priority,
   repeat. Editing only the `description` (which the card never shows) moves no
   role, so **no signal fires at all.** Surgical to a fault, deliberately.

2. **Different, but do the survivors keep their relative order?** → the change
   is pure structure: some ids vanished, some are new, positions otherwise
   hold. Reconcile granularly: `beginRemoveRows` for the vanished (bottom-up, so
   indices stay valid), then `beginInsertRows` for the new (top-down, which
   keeps "rows `0..j` already match the target" true as we go), then
   `dataChanged` for any surviving row whose content also changed.

3. **A survivor reordered?** (only a due-date edit does this — it re-sorts the
   row to a new position) → a minimal move sequence *exists*, but proving it
   correct isn't worth it for a rare case. Fall back to `beginResetModel`. This
   is the deliberate, documented cutoff: **granular when you can prove it, reset
   when you can't.**

### Why the fallback is a feature, not a cop-out

A half-correct granular diff is worse than an honest reset: it desynchronises
the view from the model and you get blank rows, duplicate rows, or crashes on
the next click. The senior move is to know exactly which cases your diff handles
correctly and reset for the rest — *loudly*, in one place, with a comment saying
why. The test `reorderingByDueDateFallsBackToReset` pins that boundary so nobody
"optimises" the reset away without replacing it with correct move handling.

### What it buys

- Tick a task done far down the list → that one row leaves; **your scroll
  position and any selection survive.** Under the old reset they'd jump.
- Edit a title → one row repaints, not the whole viewport.
- The proxy, being `dynamicSortFilter`, consumes these granular source signals
  natively — filtering and sorting stay correct with zero extra work.

### Tests (5 new, model suite now 11)

`editingTitleEmitsDataChangedOnly` · `editingOnlyDescriptionEmitsNothing` ·
`completingTaskRemovesOneRow` · `addingTaskInsertsOneRow` ·
`reorderingByDueDateFallsBackToReset`. Each uses `QSignalSpy` to assert the
*exact* signal — proving not just the end state but that the model took the
narrow path, never the sledgehammer (except where it must).

Totals after v20.1: **60 domain + 11 model + 23 UI + 19 auth (+ 11 live) = 113
automated, 124 with the server suite.**

---

## J. The same pattern, a different shape: Activities tasks (v20.2)

The Upcoming page proved the pattern; the Activities detail pane proves it
*generalises* — and the differences are the point. (See
`diagrams/activities_modelview_contrast.mmd`.)

### What changed

The TASKS list in the Activities detail pane went from a rebuilt stack of
`TaskRow` widgets to `CategoryTaskModel → QListView + CategoryTaskDelegate`. The
category rail and the Activities section are untouched.

### Four deliberate contrasts with Upcoming

**1. A parameterised model.** `TaskListModel` wrapped one fixed question
(`upcomingTasks()`). `CategoryTaskModel` wraps "the tasks of WHICH category?",
and the answer changes when you click a different life area. So it grows a
setter, `setCategoryId()`, that re-snapshots. Switching category is a wholesale
context swap, which is exactly when a full `beginResetModel` is the honest
signal — no diff to attempt.

**2. No proxy.** Upcoming needed a `QSortFilterProxyModel` for its priority lens
and due-date sort. Activities has no lens and keeps insertion order, so the view
points *straight* at the model. The lesson is the restraint: a proxy you don't
need is a layer that can only cost you.

**3. A plain reset, not a granular diff.** Upcoming earned its diff — a long list
where a done-toggle shouldn't yank your scroll. A per-category list is short and
fully on screen, so `refresh()` just resets. Right-sizing the engineering per
list is a decision you're allowed to make; the pattern flexes.

**4. A richer delegate.** An Activities row does more than an Upcoming card:
checkbox (reflects *and* toggles done), strikethrough when done, a clickable due
badge that opens a *different* dialog than the title does, an Archive pill once
done, a notes cue, delete. So `editorEvent()` hit-tests five zones, and the
delegate emits five intents. It also paints FLAT rows, not cards — same
technique, different look.

### The bug class this deletes

The old detail pane rebuilt *everything* on `changed()`, including the add-task
input — which is why it carried an elaborate `deleteLater` comment: typing a task
and pressing Enter ran `addTask → changed() → rebuildDetail → delete the input`,
freeing the very widget whose `returnPressed` was still on the stack. Model/view
makes the input a **persistent** widget; only the task list's *model* updates. The
use-after-free isn't avoided by careful deferral — it's impossible by
construction. The characterisation test that used to assert "the panel dies
later, safely" now asserts the stronger "the input never dies."

(The header and Activities rows are still refilled in place, and still use
`deleteLater` via a small `clearLayout()` helper, because their buttons *can*
fire `changed()` on their own click. The task input — the one that actually
crashed — is the piece that's now persistent.)

### One honest wrinkle: a list inside a scroll page

A `QListView` wants to own its scrolling, but here it's one section of a larger
scrolling pane. Rather than nest a scroll-in-a-scroll, the view reports its FULL
height (`updateTaskViewHeight` sums the delegate's row heights and fixes the
view's height) and disables its own scrollbar, so the surrounding `QScrollArea`
scrolls the whole pane as one. That's the trade for reusing the widget outside
its comfort zone — worth knowing before you reach for `QListView` mid-page.

### Cleanup: TaskRow deleted

`TaskRow` existed by the "second-consumer rule" — extracted when both Upcoming
and Activities needed the identical row. Upcoming moved to a delegate (v20);
Activities now has its own delegate (v20.2). Zero consumers remain, so the shared
widget is dead code and goes — the clean inverse of the extraction that created
it.

### Tests

3 new `CategoryTaskModel` tests (`categoryModelIncludesDoneAndTbdTasks`,
`categoryModelRepointsToAnotherCategory`, `categoryModelSkipsArchivedTasks`) and
the retargeted crash test. Totals after v20.2: **60 domain + 14 model + 23 UI +
19 auth (+ 11 live) = 116 automated, 127 with the server suite.**

---

## K. One diff, two models: the shared base (v20.3)

By v20.2 the app had two snapshot-backed list models — `TaskListModel`
(Upcoming) and `CategoryTaskModel` (Activities) — but only Upcoming updated
incrementally; Activities still reset on every change. Bringing incremental
updates to Activities meant either copying the ~50-line diff (two copies that
drift the day one gets a fix) or giving the diff a single home. We gave it a
home. (See `diagrams/snapshot_model_hierarchy.mmd`.)

### The base: `TaskSnapshotModel`

An abstract `QAbstractListModel` that owns the snapshot (`m_rows`), `rowCount()`,
and the two update paths:

- **`applySnapshot(next)`** — the v20.1 diff, verbatim: fast-path `dataChanged`
  for in-place edits, `begin/endInsertRows` / `begin/endRemoveRows` for
  structure, and a `beginResetModel` fallback only when surviving rows reorder.
- **`resetSnapshot(next)`** — a plain reset, for a genuine context swap.

A subclass fills exactly two holes: where the snapshot comes from (its own
query) and `rolesEqual(a, b)` — which fields *its* delegate paints, so an edit to
an unpainted field repaints nothing. That's the **Template Method** pattern: the
base fixes the algorithm's shape; the subclass supplies the specifics. It's the
same "second-consumer rule" that once justified extracting `TaskRow` — applied
now to an *algorithm* instead of a widget.

Note the two `rolesEqual` overrides differ, and that difference is meaningful:
Upcoming's omits `done` (its list is undone-only, so completion means the row
*leaves*), while Activities' includes `done` (its checkbox and strikethrough must
repaint in place when you tick a task). Same base, two honest answers to "what
does a change look like here?"

### The payoff: Activities is now incremental

`CategoryTaskModel` now splits its two updates by meaning:

- **`setCategoryId()`** stays a `resetSnapshot` — re-pointing at a different life
  area is a context swap, and "everything moved" is the honest signal.
- **`refresh()`** (a data change within the current category) now calls
  `applySnapshot` — so ticking a checkbox flips *one* row via `dataChanged`
  instead of rebuilding the list, and archiving a task removes exactly its row.

Because that list is never sorted, survivors never reorder, so the reset fallback
effectively never fires there — the granular path is all upside. The rule worth
keeping: **reset for a context swap, diff for an in-place edit.**

The page's `updateTaskViewHeight()` is now wired to `rowsInserted`/`rowsRemoved`
as well as `modelReset`, since the list's fixed height follows the row count and
that count now changes via granular signals, not only resets.

### Tests (3 new, model suite now 17)

`categoryTogglingDoneEmitsDataChangedNotReset` (one `dataChanged`, zero resets),
`categoryArchivingRemovesOneRow` (one `rowsRemoved`), and
`categorySwitchStillResets` (re-point → exactly one `modelReset`). The existing
16 pass unchanged, which is the real proof the extraction preserved behaviour.

Totals after v20.3: **60 domain + 17 model + 23 UI + 19 auth (+ 11 live) = 119
automated, 130 with the server suite.**
