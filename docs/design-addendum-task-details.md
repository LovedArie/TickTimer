# Design Addendum — Task Details, Settings, and Organising by Drag

**Status: implemented — the shipped application includes everything
below.** Indexed from `design-doc.md` §3; kept standalone as the record of
the session that produced it.

This addendum continues the decision log in `design-doc.md §3`. It records the
work of the v12 session: a task detail panel, adjustable Pomodoro durations,
tasks shown on the calendar, and drag-and-drop into folders.

---

## 3.15 A Task grows `description` and `repeat` — additively

*Decision:* `Task` gains two optional fields: a free-text `description` (the
detail panel's notes) and a `repeat` recurrence hint.

*Why:* a model must be able to say every truth it is asked to show (§3.9).
The panel promises a description and a repeat setting, so the `Task` must be
able to hold them. Both are **additive**: an old task with neither reads as an
empty description and `Repeat::None`, so every existing file loads unchanged.

*Why an `enum` for repeat, not a string:* an `enum class Repeat { None,
Daily, Weekly, Monthly, Yearly }` makes illegal values **unrepresentable** —
you cannot store `"wekely"`. A bare string would let a typo become data. This
is the same instinct behind using an invalid `QDate` for "TBD" instead of a
`bool` (§3.11): make illegal states impossible to express.

## 3.16 `repeat` is *stored and shown*, not *acted upon* — a drawn line

*Decision:* the app records and displays a task's repeat setting, but does
**not** yet regenerate the task when you complete it.

*Why:* recurrence *behaviour* (spawning the next occurrence, handling
"every 2nd Tuesday", skips, etc.) was a stated non-goal, and it is a real
feature deserving its own design. Capturing the *fact* now is cheap and
honest; half-built behaviour is neither. The comment in `Task.h` names this
line explicitly so a future reader knows it was a choice, not an oversight.

*Rejected:* shipping a naive "on done, clone with a bumped date" — it would
mishandle month-ends and the "done early" case, and quietly train the user to
distrust it.

## 3.17 `updateTask` — one coarse mutation for one user action

*Decision:* the detail panel saves through a single `AppData::updateTask(id,
title, description, dueDate, repeat)` call, not four fine-grained setters.

*Why:* mutation granularity should match the **user's** action. The checkbox
and the date badge are single-field flicks, so they keep single-field setters
(`setTaskDone`, `setTaskDueDate`). The panel is one deliberate "edit this
task" action, so it is **one** mutation and **one** `changed()`. Four setters
would fire four rebuilds for what the user experienced as a single edit.
`updateTask` still refuses an empty title (a task must keep a real name);
an empty description is allowed.

## 3.18 Pomodoro durations are SETTINGS, not domain data

*Decision:* the focus / break / long-break lengths live in **`QSettings`**,
never in `data.json`.

*Why — the classifier gains a third bin.* Until now every fact was either
*domain* (survives restart, guarded by rules — categories, tasks) or
*presentation* (session-only, deliberately discarded — which folders are
collapsed). A chosen focus length fits **neither**: it must survive a restart
(so it is not presentation), but it is not a fact about the user's tracked
time (so it is not domain). It is a **user setting**, and settings have their
own home: `QSettings`, Qt's store for persistent preferences.

*Why not fold it into `data.json`:* separation of concerns. A settings tweak
(25 → 26 min) must never dirty the domain file or drag it through the
version/migration logic that guards the user's actual data. Different reasons
to change ⇒ different files. `design-doc.md`'s non-goals already anticipated
this by parking "remembered window/sidebar state (`QSettings`)" — this cashes
that in.

*Behaviour note:* changing a duration reshapes the current phase only while
the clock is **idle**; it never yanks seconds out from under a running
countdown (the new length applies on the next phase).

## 3.19 Tasks on the calendar — a read-only "due today" strip

*Decision:* the Calendar's day view shows a read-only strip of the tasks
**due on the viewed day**, above the timeline. Clicking one opens the same
detail panel. It does **not** place a task onto a specific time block.

*Why this shape:* `design-doc.md`'s non-goals list flagged "placing a task
onto the agenda deserves its own design session." A due-block on the timeline
raises real questions (what time? what duration? is it an Event now?) that we
have not designed. The strip answers the actual need — *"what's due today,
where I plan my day"* — in the smallest, safest way, and leaves the harder
question openly deferred rather than half-answered.

*Derived, never stored (§3.13):* the strip's contents come from a query,
`AppData::tasksDueOn(date)` — undone tasks whose due date is exactly that day
— recomputed on every change. Nothing new is persisted.

## 3.20 Drag-and-drop into folders — gesture becomes intent

*Decision:* a life area can be dragged onto a folder (or to the top level).
The right-click "Move to folder" menu **stays** alongside it.

*Why the split:* a small `CategoryTree` subclass detects the drop gesture and
emits `categoryDropped(categoryId, folderId)`; the page turns that into the
existing `AppData::setCategoryFolder` call. The tree never mutates data and
never lets Qt reparent items itself — the domain performs the move, `changed()`
fires, and the tree rebuilds from truth. **One source of truth, even for a
drag.** Folders are drop targets but are neither draggable nor nestable, so
the "one level deep" rule (§3.12) is upheld at the gesture layer *and* still
enforced in the domain.

---

## 4. Data & persistence — format version 4

`data.json` format version is now **4** (v3 → v4 added `Task.description` and
`Task.repeat`). As with every prior bump the change is **additive**: a missing
key reads as its empty default (`""` / `Repeat::None`), so v3 files load with
no migration branch. Verified by `taskDetailsRoundTripAndGuardEmptyTitle` in
the automated suite.

Pomodoro durations are the first data to live **outside** `data.json`, in
`QSettings` (§3.18) — deliberately, so preferences and domain data evolve
independently.

## 5. Scope update

**Shipped (v12):** everything from v11, plus — task descriptions and a repeat
setting; a task detail panel; a read-only "due today" strip on the Calendar;
adjustable, remembered Pomodoro durations; and drag-and-drop of life areas
into folders. Automated suite now 22 tests.

**Still deferred (fences, not oversights):** acting on `repeat` (regenerating
a task on completion) · placing a task onto a specific agenda time block ·
tags / reminders on tasks · vacation date ranges · folder nesting · special
days on the month grid · a Qt model/view refactor · SQLite · Android · sync.
