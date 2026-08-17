# TickTimer — Question Bank

A self-test bank covering the whole project: C++ language, the Qt framework,
architecture & design principles, this app's domain model, persistence,
debugging mindset, and documentation. Questions are drawn from our sessions
plus coverage questions on concepts the code relies on.

**How to use it:** cover the **A:** line, answer out loud (say *why*, not just
the letter), then check. Formats are marked — `[T/F]`, `[MC]`, or `[HANDS-ON]`.
The 25% hands-on ones matter most; treat them as "explain or write it."

---

## A. C++ language & idioms

**A1 `[MC]`** A dialog is created as a local (stack) variable and given a
parent widget. What must be true of that parent?
**A:** It must be **guaranteed to outlive the dialog** (e.g. the top-level
`window()`). A stack object already has one owner (the stack frame); if its
parent is deleted first, the parent-child deletion frees it once and the stack
unwind frees it again → **double free**. Parenting to the volatile row that a
save deletes is exactly the bug we hit.

**A2 `[T/F]`** Marking a pointer member `const AppData* m_data` is just a
politeness — it documents intent but doesn't really prevent anything.
**A:** **False.** It's a wall, not a note. Calling a *mutating* method through a
`const` view is a **compile error**. That's what forces "widget reports, domain
decides": the `AgendaWidget` *cannot* call `resizeEvent`, so it must emit a
signal instead. Make illegal actions un-compilable, not just discouraged.

**A3 `[MC]`** Why `enum class SegmentKind { Focus, Break, Distracted }` instead
of `int` constants or a `QString`?
**A:** A scoped `enum class` makes **illegal values unrepresentable** — you
can't store `"wibble"` or `SegmentKind::Focus + 1`. A string would let a typo
become data; a bare `enum`/`int` would convert implicitly and leak. Same
instinct as using an invalid `QDate` for "no due date."

**A4 `[HANDS-ON]`** Spot the bug: after adding `SegmentKind::Distracted`,
`for (s : segs) { if (s.kind==Focus) f+=s.seconds(); else b+=s.seconds(); }`.
**A:** The `else` catches **everything non-Focus**, so `Distracted` is silently
added to `breakSeconds`. Fix: an exhaustive `switch` with **no `default`**, so a
future kind triggers a `-Wswitch` warning instead of a silent miscount. Rule:
*an `else` on an enum is a bug waiting for the enum to grow.*

**A5 `[T/F]`** A small pure helper needed by two `.cpp` files should go in a
`.cpp` somewhere and be declared `extern`.
**A:** **False** (usually). A tiny pure function shared across translation units
is cleanest as an `inline` free function in a header (e.g. `repeatToString` in
`Task.h`) — `inline` satisfies the One Definition Rule so every includer gets
the definition, no separate `.cpp` needed.

**A6 `[MC]`** You capture a loop index `d` into a lambda connected to a signal:
`connect(col, &Sig::x, this, [this, d](int s){ ... m_weekStart.addDays(d) ...})`.
Why capture `d` **by value**?
**A:** The lambda outlives the loop iteration; capturing by reference would
dangle. By value, each column's lambda carries its own day offset. (Same reason
`TaskRow` snapshots the `Task` by value before opening its dialog — the vector
may move.)

**A7 `[HANDS-ON]`** Why does `TaskRow` copy the `Task` into a `const Task
snapshot = task;` before wiring the title-click lambda?
**A:** `task` is a reference into `AppData`'s vector; a later mutation can
reallocate/move the vector, dangling the reference. The by-value snapshot holds
the fields the dialog needs, so the click handler can never read freed memory.

---

## B. Qt framework

**B1 `[T/F]`** Inside `paintEvent` you may modify members or trigger a data
change, as long as you call `update()` afterward.
**A:** **False.** `paintEvent` must only **read state and draw**. Writing state
there is unpredictable (Qt repaints on resize/scroll/uncover at will), and
calling `update()` after a state change schedules another paint → infinite
repaint loop. Input handlers write state and call `update()`; paint only reads.

**B2 `[MC]`** A hover should highlight a slot. Where does `m_hoverSlot = n;` go,
and what follows it?
**A:** In `mouseMoveEvent`, followed by **`update()`** (not a direct
`paintEvent()` call). `update()` lets Qt schedule and batch the repaint with a
valid painter; calling `paintEvent()` yourself has no paint context and defeats
batching.

**B3 `[MC]`** `AgendaWidget` emits `emptySlotClicked(slot)` instead of opening
the picker dialog itself. Why does that specifically enable the week view?
**A:** Because the widget stays **ignorant of the app**, you can tile seven of
them and let the page decide what a click means (plan on *that* column's date).
A widget that opened its own dialog would drag its assumptions into every reuse.
Signals decouple; they aren't about speed.

**B4 `[T/F]`** `QSettings` and `data.json` should hold the same kind of data.
**A:** **False.** `data.json` is **domain** data (facts about your time);
`QSettings` is for **preferences** that must persist but aren't domain (Pomodoro
durations, someday window state). Mixing them means a settings tweak dirties the
domain file and drags it through migration logic — different lifetimes, different
stores.

**B5 `[MC]`** `deleteLater()` vs `delete` when rebuilding a panel from inside a
child widget's own signal handler?
**A:** `deleteLater()` (often with `setParent(nullptr)` to hide it now). The
clicked widget must **outlive the signal it's currently emitting**; a synchronous
`delete` frees it mid-call. `deleteLater` defers the free to the event loop.

**B6 `[HANDS-ON]`** In the Anthropic-API/`QSpinBox` sense — you connect
`&QSpinBox::valueChanged`. Which overload resolves in Qt 6, and why does the
function-pointer syntax matter?
**A:** In Qt 6 there's a single `valueChanged(int)` (plus `textChanged`), so
`&QSpinBox::valueChanged` resolves unambiguously to the `int` one. The
function-pointer connect is compile-checked (typo → build error), unlike the old
string-based `SIGNAL()/SLOT()` macros.

**B7 `[T/F]`** A custom widget class used in only one place should still be a
public class in its own header/`.cpp` for cleanliness.
**A:** **False.** One consumer = keep it **file-local** (the `AgendaAxis` in
`WeekAgendaView.cpp`, or an inline helper like `UpcomingPage::buildTaskCard`).
Extracting a shared class for a single caller is speculative abstraction — the
same smell as duplicating for a second caller, pointed the other way.

---

## C. Architecture & design principles

**C1 `[MC]`** Where must the rule "an event can't overlap the next one" live?
**A:** In **`AppData`** (the aggregate root) — the single door every change
passes through, so the rule **can't be bypassed** by any caller (dialog, drag,
future script, hand-edited file). A widget requests; the domain enforces and can
refuse.

**C2 `[T/F]`** If the overlap rule lives in `AppData::resizeEvent`, the
`EventDialog` automatically benefits — *as long as it also routes its change
through `AppData`*.
**A:** **True.** One guard protects every caller that goes through the door. The
condition is real: the moment any code writes `event.plannedEndMinutes`
directly, it walks around the guard and the rule may as well not exist for that
path.

**C3 `[HANDS-ON]`** A teammate says "we don't need the domain to check overlaps
— the UI simply won't let you drag into an occupied slot." What's the flaw?
**A:** The UI is only **one** of many doors (loaded files, imports, the dialog,
future features). Relying on it assumes the UI is the *only* way data changes,
which is never true. The UI can make a bad state **hard to reach** (good UX);
only the domain can make it **impossible to exist** (correctness). Keep both.

**C4 `[MC]`** Turning the Upcoming list into styled cards — which bin: domain,
settings, or presentation?
**A:** **Presentation.** It changes only how existing data is drawn; no new
stored fact, no format bump, tests untouched. If a change adds no field anyone
saves and reloads, it lives in the widget.

**C5 `[T/F]`** `UpcomingPage` keeps its own list of tasks and updates it on every
change.
**A:** **False — it's derived.** It connects to `AppData::changed`, calls
`upcomingTasks()`, and rebuilds from the result. No cached copy = nothing to
drift. **Derive-don't-store.**

**C6 `[MC]`** We added `distractedSeconds` to `Totals` but *not* a counter to
`TrackerService`. Which principle, and what would the counter cost?
**A:** **Derive-don't-store.** The total is summed from the segments on demand;
a stored counter is a **second source of truth** that can drift and dies on a
crash, while the segment's start timestamp is already safe on disk.

**C7 `[HANDS-ON]`** Name the "one geometry, shared" decision in the week view and
why it matters.
**A:** `AgendaWidget::kSlotHeight` / `kTopPad` were made **public statics** so
the shared axis and the seven columns read the *same* numbers. Two copies of a
magic `30`/`12` are two things that drift apart; one shared constant can't.

**C8 `[MC]`** `Task` is deliberately unrelated to `Activity`. Why not put a
`done` flag on `Activity`?
**A:** A model must say every truth and no falsehood. "Lab 4" is a one-off
obligation, not an occurrence of a reusable type; a `done` on the shared
`Activity` would mark *every* gym block in history complete. Type vs. instance:
Activity = reusable type, Event = a dated plan, Task = a one-off obligation.

---

## D. This project's domain model

**D1 `[T/F]`** An invalid `QDate` is a valid, first-class "no due date yet"
state.
**A:** **True.** `!isValid()` *is* "DATE TBD." A parallel `hasDueDate` bool would
be a second source of truth waiting to disagree.

**D2 `[MC]`** What are the three "kinds" of time a tracked block can contain, and
how are their totals produced?
**A:** `Focus`, `Break`, `Distracted` segments. Totals are **derived** by summing
`segment.seconds()` per kind in `eventTotals` — never stored counters.

**D3 `[HANDS-ON]`** Why does resizing an event leave its tracked `Segment`s
untouched?
**A:** The plan is an intention; the segments are **facts about what happened**.
Facts don't stretch when you redraw the plan. `resizeEvent` (like `moveEvent`)
changes only `plannedStart/End`.

**D4 `[MC]`** A category sits in a folder. How is folder membership stored?
**A:** As a stored `Category.folderId` (a fact that must survive restart) — not a
name-prefix convention like "School / LOG410" (a fact smuggled into a string).

**D5 `[T/F]`** The "next occurrence" of a yearly special day is stored so it
doesn't have to be recomputed.
**A:** **False — derived.** It's computed from today on demand. A Feb-29
anniversary in a common year resolves to **Mar 1** (arbitrary, *documented*,
decided at design time).

---

## E. Persistence & the data format

**E1 `[MC]`** We add `SegmentKind::Distracted`. Old `data.json` files contain
only `"focus"`/`"break"`. What must the reader do?
**A:** Be **tolerant**: map known strings, and map unknown/missing to a safe
default (`Focus`). Old files load unchanged; a future unknown kind won't crash.
This is additive growth — no migration needed.

**E2 `[T/F]`** Adding a field to a struct that's serialized requires a migration
that rewrites every old file.
**A:** **False**, if additive: a missing key reads as its empty default. That's
why `Task.description`/`repeat` (v4) and `Distracted` (v5) loaded old files with
zero migration branches.

**E3 `[MC]`** Why is every save an atomic write-then-replace (`QSaveFile`)?
**A:** A crash *mid-save* can never corrupt the existing file — the new content
is written to a temp file and swapped in only once complete. Reliability rule
from the Supplementary Spec.

**E4 `[HANDS-ON]`** Where does the `SegmentKind ⇄ string` mapping live, and why
one place?
**A:** In two file-local helpers (`kindToStr`/`kindFromStr`) in `JsonStore.cpp`,
shared by the segment path *and* the crash-recovery "running" block. One place =
writer and reader can't drift apart.

---

## F. Debugging & the developer mindset

**F1 `[T/F]`** When a user reports data is "missing" from a view, the right first
assumption is that it was never saved or the view is stale/hardcoded.
**A:** **False.** "Missing" is a *symptom*, not a diagnosis — it has ≥4 distinct
causes (never created, created-not-saved, saved-not-loaded, loaded-but-not-shown
/ filtered / ordered off-screen). **Reproduce and observe** the real state; let
evidence eliminate causes. (The picker bug was *ordering*, not absence.)

**F2 `[MC]`** A crash on one specific action is most likely which class of bug,
and where do you look?
**A:** A **memory-safety** bug (e.g. use-after-free / double-free). Look at
**object lifetimes/ownership**, not loops or algorithms. Get a **backtrace**
before "fixing" anything — the debugger is faster than guessing.

**F3 `[T/F]`** A disproven hypothesis during debugging is wasted effort.
**A:** **False.** Each disproof fences off a wrong direction. "Is it hardcoded?"
→ no. "Are the items missing?" → no. Only then does the real cause (ordering)
become visible. *You don't find the bug; you eliminate everywhere it isn't.*

**F4 `[HANDS-ON]`** Describe the reproduce-first loop we used on the double-free.
**A:** Form a falsifiable hypothesis → build the smallest faithful reproduction
→ run under a debugger for a backtrace → let the trace name the cause (a stack
dialog destroyed as a deleted row's child) → fix the actual cause (parent to
`window()`) → **re-run the same repro** to confirm the crash is gone.

---

## G. Documentation

**G1 `[MC]`** What is a design **addendum** for in this project, and what must it
contain?
**A:** It's the gate every domain change passes through *before* code:
**classify → document → fence.** It records the decision, the *why*, the
**rejected** alternatives, and any deferred scope — then later merges into the
design doc. (See §3.x entries.)

**G2 `[T/F]`** A README's job is to exhaustively document every class and
function.
**A:** **False.** A README orients a newcomer: what the project *is*, why it
exists, key features, how to build/run, and where to go next. Exhaustive
per-class detail belongs in API docs / the reading guide, not the README.

**G3 `[MC]`** Why record *rejected* alternatives in a design doc, not just the
decision taken?
**A:** So a future reader (or future you) knows the choice was **deliberate**,
not an oversight — and won't "helpfully" re-introduce the rejected option. It
turns "discovered in production" into "decided at design time."

---

## H. Block identity — labels, task blocks, ad-hoc blocks (v14)

**H1 `[MC]`** An Event can now be an Activity occurrence, a Task block, or an
ad-hoc block. Why ONE struct with optional fields instead of three Event
subclasses?
**A:** Every consumer (agenda, week view, stats, storage, tracker) treats
events identically *as time ranges*; only naming/colouring differ. A hierarchy
would push virtual dispatch into all of them to answer two string-sized
questions. Two optional fields + two resolution helpers (`eventLabel`,
`eventCategoryId`) capture the entire difference. (Addendum §3.25.)

**H2 `[MC]`** Why THREE creation functions (`addEvent`, `addTaskEvent`,
`addAdHocEvent`) instead of one taking all three identity parameters?
**A:** One function would accept nonsense at compile time ("both a task and an
activity", "neither") and have to detect it at runtime. Three signatures make
the nonsense call *unrepresentable at the call site* — the same "make illegal
states unrepresentable" instinct, applied to an API. The shared time-range
rules live once, in the private `appendGuardedEvent`. (§3.26.)

**H3 `[T/F]`** The "at least one identity" invariant is enforced inside the
`Event` struct.
**A:** **False.** A plain struct can't defend a rule spanning three fields
without growing accessors and becoming a class. The invariant is enforced at
`AppData`'s mutation doors — the aggregate root's job, same as every rule.

**H4 `[MC]`** `removeActivity` *refuses* while events reference it, but
`removeTask` *succeeds*. Why the asymmetry, and what does `removeTask` do
about the dangling references?
**A:** An Activity is a reusable type — a block is meaningless without it, so
refusal is right. A Task is a one-off you finish and delete; blocking deletion
because an old calendar block mentions it would make cleanup impossible.
`removeTask` **downgrades**: copies the task's title into each referencing
event (unless the user typed their own label) and clears `taskId`. A third
option between "refuse" and "cascade delete." (§3.27.)

**H5 `[SPOT THE BUG]`** Inside `removeTask`, what goes wrong if the code
erases the task from `m_tasks` first and only *then* reads `doomed->title`
through the pointer from `taskById()`?
**A:** `taskById()` returns a pointer INTO the QVector; `erase()` invalidates
it. Reading `doomed->title` after the erase is a use-after-invalidation —
undefined behaviour that may "work" in a debug build and corrupt in release.
Copy the title to a value *before* mutating the container.

**H6 `[MC]`** Where does a task block's tracked time land in the weekly
review, and where does an ad-hoc block's time land?
**A:** `eventCategoryId` resolves a task block to the **task's own category**
(tasks carry one directly), so its time lands in that life-area bar. An ad-hoc
block resolves to `""`: its time counts in the day/week **totals** but appears
in **no category bar** — a documented, test-pinned limitation, not an accident.

**H7 `[T/F]`** Adding `taskId` and `title` to the JSON required a migration
step for old data files.
**A:** **False.** Additive keys + tolerant read: a missing key reads as `""`,
which means "activity-only event" — exactly what every pre-v6 event was.
Version bumped 5 → 6 purely so future readers can tell files apart. Third
additive bump; still zero migration code.

**H8 `[MC]`** In `EventDialog`, why does the label field save on
`editingFinished` instead of `textChanged` like the note field?
**A:** The domain may **refuse** the edit (clearing an ad-hoc block's only
identity). Refusing on every keystroke would fight the user mid-rewrite — the
field would snap back while they type. Validate-on-commit: let them finish,
ask the domain once, restore the stored value only if refused. The note has no
refusal case, so per-keystroke saving is fine there.

**H9 `[HANDS-ON]`** Sketch what breaks (and where you'd fix it) if a fourth
block identity — say, a linked Special Day — were added tomorrow.
**A:** Thanks to §3.28, the touch points are enumerable: one new creation door
in `AppData` (reusing `appendGuardedEvent`), one branch each in `eventLabel` /
`eventCategoryId`, one additive JSON key, one row kind in the picker. Painting,
stats, and the dialog need **zero** changes — they already resolve through the
helpers. Before the helpers existed, three widgets each walked
Event→Activity→Category and all three would have needed parallel edits.

**H10 `[MC]`** The picker's one text field creates a *labelled activity block*
if you type-then-click, but an *ad-hoc block* if you type-then-Enter. Why one
field for two intents instead of two fields?
**A:** Two fields ("label" vs "or something else?") would force the user to
classify their intent *before* typing. One field lets them just say what
they're doing; the **next gesture** (click vs Enter) disambiguates. Lower
friction, same expressiveness. (§3.28, UI notes.)


**H11 `[MC]`** In `linkTaskFromCompleter`, why must `setEventTask` run
*before* `setEventTitle(id, "")`?
**A:** If the block is ad-hoc, its title is its ONLY identity at that moment —
clearing it first would be refused by the invariant. Linked first, the block
stands on the task, and the domain permits the clear. Two guarded mutations:
the ORDER of calls decides which intermediate states exist, and every
intermediate state must satisfy the invariant too. (§3.29.)
*Sequel:* the clear was later REMOVED entirely (linking now preserves the
label — see H15), so this exact code is gone. The ordering principle it
taught is not.

**H12 `[MC]`** The completer's `activated` signal is connected via its
`QModelIndex` overload rather than the `QString` overload. Why?
**A:** The index carries the task **ID** in `Qt::UserRole`; the string carries
only the title. Mapping title → id by string lookup breaks the moment two
tasks share a title — the same "id rides inside the row, no parallel lookup"
rule as the picker's list items. (§3.29.)

---

## I. Android port & compact-screen mode (v14)

**I1 `[MC]`** Compact mode is gated by `isCompactScreen()` (screen geometry)
rather than `#ifdef Q_OS_ANDROID`. Why?
**A:** The layout's real question is "how much room?", so the code asks about
room. Platform is a proxy that fails both ways: an Android tablet should get
the desktop layout, a tiny Windows tablet the compact one. Ask the actual
variable. (§3.30.)

**I2 `[MC]`** Why grab `QScroller::TouchGesture` on the scroll areas instead
of `LeftMouseButtonGesture`?
**A:** TouchGesture leaves the mouse untouched — the agenda's edge-resize
drag keeps working on desktop. A mouse-gesture scroller would hijack every
drag into a scroll. Tradeoff accepted: on touchscreens, agenda drags scroll;
blocks are adjusted via the dialog's nudge buttons instead. (§3.31.)

**I3 `[T/F]`** A `QStackedWidget`'s minimum size comes from the currently
visible page.
**A:** **False** — it's the MAX over ALL pages, visible or not. That's why a
window "mysteriously" refuses to shrink: any hidden page can hold it hostage.
The layout probe (`TICKTIMER_PROBE=1`) prints each page's contribution to
name the culprit. (§3.32.)

**I4 `[MC]`** Three widgets forced the 550px minimum; each got a DIFFERENT
fix. Match fix to reason.
**A:** Long label → `setWordWrap(true)` (a label's minimum width is its text
width; wrap lets it fold). Pomodoro settings row → flip `QBoxLayout`
direction on compact (same widgets, vertical). Month review page → wrap in a
`QScrollArea` (a scroll area's minimum ignores its content). Three
minimum-size CONTRACTS, three ways to change a contract. (§3.32.)

**I5 `[MC]`** What single, day-one CMake choice made the Android port
possible, and what new fence did the port add?
**A:** `qt_add_executable` (vs plain `add_executable`) — on an Android kit it
builds the app as a shared library and drives Qt's `androiddeployqt` APK
packaging. The port added `if(NOT ANDROID)` around tests/tools, because on
Android EVERY executable would be packaged as its own app.

**I6 `[T/F]`** Porting to Android required changes in the domain, storage, or
tracking layers.
**A:** **False** — zero changes there. `QStandardPaths`/`QSettings` resolve to
Android's app-private storage automatically, and the domain never included a
widget header (the test target enforces it). The port cost UI + build recipe
only: the layering paying rent.

**I7 `[MC]`** Why does `isCompactScreen()` have the `TICKTIMER_COMPACT` env
override?
**A:** So the compact layout can be PRODUCED ON DEMAND — the screenshot tool
renders the phone layout on a desktop-sized virtual display for verification
and docs. A layout mode you can't reproduce with a command is one you can't
verify repeatably. Same philosophy as the screenshot tool itself.

**I8 `[MC]`** You install a debug APK, later build a signed release APK, and
Android refuses to install it. Why, and what's the fix?
**A:** Android identifies "the same app" by **package name + signing key**;
debug and release builds share the name but carry different keys, so the
release build looks like an imposter trying to replace the original.
Uninstall the debug build first (back up `data.json` — uninstalling deletes
the app's private folder), then install the release APK. For a personal
phone, staying on debug APKs is fine indefinitely.

---

## J. The add-a-task crash — widget lifetime & the first UI test

**J1 `[MC]`** The app crashed on adding a task, yet the task was saved. What
does "crash after successful mutation" tell you about WHERE the bug lives?
**A:** The domain door and the storage listener both completed — so the
explosion is in a UI listener reacting to `changed()`. The symptom localizes
the bug to a layer before you read a line of code.

**J2 `[MC]`** State the exact death chain.
**A:** `returnPressed` → `addTask()` → `emit changed()` (direct connection,
runs synchronously) → `rebuildDetail()` → `delete m_detail->takeWidget()` —
which destroys the very QLineEdit whose signal handler is still on the call
stack → the stack unwinds into freed memory.

**J3 `[T/F]`** `deleteLater()` deletes the object "eventually, whenever."
**A:** **False** — it posts a deferred-delete event; destruction happens when
control RETURNS TO THE EVENT LOOP, i.e. at the first moment no code can be
executing inside the object. That precise timing is why it's the documented
cure for "a slot needs to destroy the sender (or its ancestors)."

**J4 `[MC]`** The buggy code passed the test on Linux — even under
AddressSanitizer. What's the lesson, and how did the test achieve a
deterministic red anyway?
**A:** Use-after-free is UB: freed memory often still "works" by allocator
luck, so "no crash" ≠ "correct." The test stopped hoping for a crash and
asserted the CONTRACT instead: a `QPointer` on the input must be non-null the
instant the keystroke returns (destruction must not be synchronous) and null
after the event loop drains. Contract testing beats crash-fishing.

**J5 `[MC]`** Why couldn't the 33 domain tests ever catch this bug?
**A:** They deliberately build without Qt Widgets — the bug lives entirely in
WIDGET LIFETIME during signal delivery, a layer the domain suite doesn't
instantiate. Hence the new `test_ui` target: real widgets, driven offscreen,
reserved for bugs only a living widget tree can express.

**J6 `[SPOT THE ISSUE]`** `PlannerPage`'s due strip never had this bug. Why —
and what does that say about fixing bug CLASSES vs bug instances?
**A:** Its clear loop already used `w->deleteLater()`. The cure existed in
the codebase; three other rebuild sites used plain `delete`. A bug report
names one instance ("adding a task crashes") — the fix hunts the PATTERN
(`grep takeWidget`) and closes every site, then pins the class with a test.

**H13 `[MC]`** Making the label box multiline forced a subclass (`LabelEdit`)
where a plain `QLineEdit` needed none. What exactly did the subclass have to
take over?
**A:** `QCompleter` integrates with `QLineEdit` for free (`setCompleter`);
for text edits Qt's documented custom-completer pattern makes YOU drive it:
`completer->setWidget(edit)`, a `keyPressEvent` that updates the completion
prefix and pops the list at the cursor, and an ignore-list for the popup's
keys (Enter must pick a suggestion, not insert a newline while the popup is
open).

**H14 `[MC]`** Why does the multiline label save on focus-out instead of
`editingFinished` — and why the `Qt::PopupFocusReason` guard?
**A:** A multiline edit HAS no `editingFinished`: the signal's trigger key,
Enter, now means "new line", so "done editing" had to be redefined as
"focus left the box". The guard exists because opening/clicking the
completer popup momentarily steals focus — committing there would save a
half-typed draft mid-selection. Contract change, both halves pinned by the
`blockLabelIsMultilineAndCommitsOnFocusOut` UI test.

**J7 `[SPOT THE BUG]`** A UI test types into a widget, calls
`otherWidget->setFocus()`, and asserts a focus-out commit ran — it fails
with nothing saved, though the app works fine by hand. What's missing?
**A:** An ACTIVE window. Focus events only flow inside the active window,
and on the offscreen platform nothing activates windows for you —
`setFocus()` silently does nothing. Fix: `dialog.activateWindow()` +
`QVERIFY(QTest::qWaitForWindowActive(&dialog))` before driving focus.
(`QTest::keyClicks` masked the problem earlier because it delivers events
directly to a widget, focus or not.)

**H15 `[MC]`** Linking a task originally cleared the block's label; now it
doesn't. What was wrong with the original design, and why is the text in the
box at link-time safe to discard anyway?
**A:** The label had become the block's COMMENTS in real use, so "let the
task show" silently destroyed user data — a design decision invalidated by
observed usage, reversed and documented (§3.29 revised). The box's content at
link-time is safe to discard because it was a SEARCH QUERY that never got
committed: commits happen on focus-out, and focusOutEvent's
`PopupFocusReason` guard specifically excludes popup interaction. The stored
label survives; the block now paints task line + comments together.

**H16 `[MC]`** "Show task descriptions on blocks" — domain fact,
presentation, or setting? Where does it live, and why doesn't AgendaWidget
read it itself?
**A:** A **setting**: it changes how blocks look, not what is true — so
`QSettings` (`planner/showTaskNotes`), never `data.json` (same rule as
Pomodoro durations). The widget is TOLD via
`setShowTaskDescriptions(bool)`: only `PlannerPage` touches QSettings,
because a painter that reads app configuration stops being reusable — the
same widget serves the day view, seven week columns, and the screenshot
tool. (§3.33.)

**H17 `[SPOT THE TRAP]`** Preferences file lands under
`Unknown Organization/TickTimer.conf`. Why not "fix" it by calling
`setOrganizationName()` in main()?
**A:** Because org+app names are the KEY to both QSettings' path and
`QStandardPaths::AppDataLocation` — changing the org now would relocate the
settings AND the data folder, making existing Pomodoro durations and
`data.json` appear to vanish on next launch. Cosmetic fix, data-loss
symptom. main.cpp sets only the application name, deliberately and with a
comment; naming choices become load-bearing the moment anything persists
under them.

**H18 `[MC]`** Descriptions with hard line breaks left the block's right half
empty while clipping at the bottom. Why couldn't `drawText`'s word wrap fix
it, and what does the fix use instead?
**A:** Word wrap can only SPLIT long lines — it can never widen lines the
author already broke short, so the right half stays unreachable. The fix is
newspaper flow with `QTextLayout`: it hands over each line (`createLine`)
for manual placement, enabling a hop to a second column when the first
fills. Two supporting tricks: '\n' → `QChar::LineSeparator` (the only break
QTextLayout honors inside its single-paragraph model), and balanced columns
(split the line count evenly — even columns read as one text). Columns
appear ONLY on overflow, so short text keeps its familiar single-column
look. (§3.34.)

**H19 `[SPOT THE FLAW]`** The first column-flow rule was "columnize when the
text overflows its area." A user screenshot showed a description in ONE tall
column, empty right half, and their comment below missing entirely. What was
wrong with the rule, and what replaced it?
**A:** The fit test was SELFISH — it asked "do I fit?" but never "does my
neighbor below still fit after me?" A fitting description consumed the full
height and starved the comments. Replaced by a **budget**: area minus a
reservation for the neighbor (the comments' measured height, capped at half
so neither side can erase the other); exceeding the budget columnizes even
when the area had room. Bonus lesson: the bug was font-metric dependent —
it bit on Windows fonts and initially passed on Linux — so "works on my
machine" was literally a matter of pixels. (§3.34, second iteration.)

---

## K. Distracted made visible (§3.35)

**K1 `[SPOT THE BUG]`** The domain kept focus/break/distracted in three
buckets, yet a running Distracted timer visibly ticked up the glance panel's
BREAK box. Where was the bug, and what's the diagnostic lesson?
**A:** In ONE display: GlancePanel's live-time split was a stale two-way
`if/else` — "not Focusing" fell through to "break". EventDialog and
AgendaWidget already switched three ways. Lesson: when a correct pattern
exists in-project, grep for its ABSENCE — partial adoption is a bug class of
its own (same shape as the deleteLater story). And a two-value `if/else`
over a three-value enum is a latent bug the compiler can't see; a `switch`
with explicit cases is self-auditing.

**K2 `[MC]`** Why does the month review pair Focused with Distracted but
deliberately omit Break?
**A:** At a month's distance, rest needs no audit — drift is the number the
app exists to expose (its thesis in one row). Break stays visible where the
timescale makes it actionable: today's glance and the week review. Which
numbers to SHOW is a design decision per timescale, not a data limitation —
the totals struct carries all three everywhere.

**K3 `[T/F]`** Since distracted time is "bad", it should stop counting
toward the category bars.
**A:** **False** — bars count `t.total()`: two distracted hours in School
are still two hours of your life spent in School, and hiding them would make
the bars lie about where time went. The BOXES judge time's quality; the BARS
report its quantity. Two questions, two widgets, one dataset.

**H20 `[MC]`** Multiline labels + ad-hoc blocks collided: the whole
paragraph became the block's bold headline everywhere. What fixed it, and
why did the fix touch only two functions?
**A:** A derived split of ONE stored field: `eventLabel` returns just the
first line of an ad-hoc title, new `eventBody` returns the rest (whole
title for activity/task blocks — the doubling never existed there). No new
field, no format bump. It touched almost nothing because §3.27 centralized
resolution: dialog header, agenda, and week columns all call `eventLabel`,
so one edit corrected every screen; only the agenda's comment paint had to
read `eventBody` instead of raw `title` to avoid printing the headline
twice. Emergent bugs between features are found by USE — and centralized
resolution is what makes them one-line fixes instead of screen-by-screen
hunts. (§3.36.)

**K4 `[MC]`** Break/distracted time now lands in fixed "Break" and
"Distracted" rows under the category bars. Why derived display rows instead
of attributing drift to the owner's own "Wasting time" category?
**A:** A magic category name is three failure modes in a trench coat: it
breaks on rename, it double-counts if the user also PLANS blocks in that
category, and it hijacks a namespace that belongs to the user. The sinks
are derived from `totals` at render time — no stored routing, no name
coupling, and the books always balance: focus-per-category + break + drift
= everything tracked. (§3.37.)

**K5 `[T/F]`** A test asserting `byCategory == focus + break` had to be
"fixed" when the attribution rule changed — so the test was wrong.
**A:** **False, and that's the point:** the test was RIGHT — it pinned the
rule of its era so the rule couldn't drift silently. When the DECISION
reversed (§3.37), the test changed in the same commit, with a comment
citing why. Tests pin decisions, not eternal truths; a pinning test
"failing" after a deliberate reversal is the system working. It also got
STRONGER in the rewrite: a distracted segment was added so both non-focus
kinds are pinned out of the bar, not just break.

---

## L. Tracking honesty — the live-window constraint (§3.38)

**L1 `[MC]`** The liveness guard is the FIRST line of each start method —
before the code that commits the currently running interval. What bug does
that ordering prevent?
**A:** A refused start with side effects. If the guard ran after "stop the
current interval, then begin the new one", trying to track a 5 PM block at
11:30 would kill your RUNNING 11:00 session as collateral. Refusal must
mean "nothing happened" — pinned by a test that starts focus on a live
block, attempts a switch to a future one, and asserts the original session
is still running, untouched.

**L2 `[MC]`** Why is `stop()` deliberately NOT guarded by the live window?
**A:** Stopping WRITES THE RECORD of time already spent — refusing it would
force fiction (an interval that never ends, or one silently discarded),
which is the exact dishonesty the constraint exists to prevent. Corollary
decision: a running interval is not auto-cut when its window expires — you
stop it yourself; you just can't *switch* kinds on an expired block.

**L3 `[SPOT THE FLAW]`** The first guard test used real
`QDate::currentDate()` with an "all-day" 0–1440 block. It failed
immediately — and would ALSO have been flaky. Both reasons?
**A:** (1) The domain's day runs 06:00–24:00 (`plan::kDayStartMinutes`):
0–1440 is refused at the event door, so the test's event never existed —
the failing assertion (`!liveEv.isEmpty()`) named it precisely. (2) Even at
360–1440, any real-clock liveness test fails before 6 AM, when no block CAN
be live. Fix: `nowProvider` — "now" injected as a dependency, defaulting to
the wall clock. If a condition can't be produced on demand, it can't be
tested (the TICKTIMER_COMPACT principle, applied to time).

**L4 `[SPOT THE BUG]`** Adding `std::function<QDateTime()> nowProvider` to
the header broke the build with `AutoMoc: Not a signal or slot declaration`.
Why?
**A:** It was pasted into the `public slots:` section — moc parses slot
sections and accepts ONLY function declarations there; a member VARIABLE is
a syntax error to moc (though fine to the C++ compiler). Members and
non-slot queries live in a plain `public:` section. Bonus lesson from the
same incident: when the build fails, test results come from the STALE
binary — a bewildering "impossible" failure evaporated once the moc error
was fixed. Grep the build log before trusting test output.
*Sequel (sync session):* second sighting — a `struct PullResult` declared
inside a test class's `private slots:` section broke the same way (moc
tolerates FUNCTIONS there, but a type declaration is a syntax error to it).
Caught in minutes this time because the error message was already an old
acquaintance — which is the whole point of logging these.

**H21 `[MC]`** A 1h30 block hid its task description entirely; the user
expected the §3.34 columns to push it into the empty right half. Why
couldn't they, and what does the fix trigger on?
**A:** §3.34's columns subdivide the description's OWN area, which sits
below the header lines — on a 3-slot block that area is ~one line tall, and
splitting nothing yields nothing. The fix is a PLACEMENT rule: when fewer
than two line-heights remain below, the task line takes the left half and
the description flows in the right half from the task line's row down
(`maxColumns = 1`: wrap and clip, never quarter-width slivers). The trigger
is geometric (line-height units), not a text-fit measurement — fit tests
already proved font-stack-dependent in §3.34; line-height thresholds give
Windows and Linux the same verdict. Bonus structure lesson: the placement
is decided BEFORE the task line draws, because it changes the task line's
width. (§3.39.)

**K6 `[MC]`** "Unaccounted time" is a first-class number in the glance
panel, yet nothing in `data.json` changed and no new Segment kind exists.
How — and what nearly made it silently wrong?
**A:** It's DERIVED: `elapsedWindow(event, now) − tracked(event)`, clamped
at zero (over-tracking past the window is legal). Storing it would record a
conclusion that goes stale every minute — derive-don't-store at its purest.
The near-miss: the summarize loop short-circuits never-tracked events
(`t.total() == 0 → continue`), and never-tracked blocks are unaccounted's
whole subject — the accumulation had to land BEFORE that early-exit, and a
test pins the case. Bonus: `now` became a Stats parameter (defaulting to
the wall clock), so every boundary is tested at a fixed 11:30. (§3.40.)

**K7 `[MC]`** The new glance pie has no legend. Why is that safe, and what
invariant does it impose on the code?
**A:** The BARS are the legend — but only because the pie is fed EXACTLY
the rows the bars display (same data, colours, order; both post-filtering
of zero rows). That identity is the invariant: build one `rows` container,
derive both widgets from it. Feed them separately and the "legend" starts
lying the first time one is filtered differently. (§3.40.)

**L5 `[SPOT THE BUG]`** The gated-buttons test passed for hours, then went
red with no code change touching the dialog. The hint said "this block has
passed" for a block the injected clock placed in the future. What happened?
**A:** TWO CLOCKS IN ONE DECISION: the live-verdict used the tracker's
injectable `nowProvider` (pinned at 11:30), but the hint's future-vs-past
wording read `QDateTime::currentDateTime()` — and the real clock crossed
the test block's 5 PM start mid-session, making verdict and explanation
disagree. Fix: the explanation derives from the SAME `now` as the verdict.
A decision and its justification must share inputs, or time will find the
seam. (§3.40.)

---

## M. Networking — the client/server login (design addendum: login)

**M1 `[SPOT THE BUG]`** The login client reported `NetworkError` for a
perfectly valid "username taken" response — but only on every *second*
request. Name the THREE stacked causes.
**A:** (1) `QNetworkReply::error()` is non-zero for HTTP 4xx, not just
transport failures — testing it alone misreads a valid 409 as a network
error and throws the body away; the fix branches on
`HttpStatusCodeAttribute` (status present → server answered → parse body).
(2) An empty HTTP reason phrase (`HTTP/1.0 409 `) is legal but mishandled by
some clients under connection reuse — send real phrases. (3) The server
closes each socket after one response, but QNAM pools/reuses connections, and
firing a request from inside a nested event loop reuses a dead socket —
`clearConnectionCache()` before each request cures it. All three had to be
fixed; each alone left the bug.

**M2 `[MC]`** Why is `AuthClient` asynchronous (signal-based) rather than a
blocking `bool login(...)`?
**A:** A network call can take a second or fail; blocking the UI thread that
long freezes the whole app. QNAM is fire-and-forget — POST now, a `finished`
signal later — and `AuthClient` translates that into one `resultReady`
signal the dialog reacts to while staying responsive. The one place blocking
IS acceptable is `LoginDialog::exec()` in main(), because there's genuinely
nothing else for the app to do until someone logs in.

**M3 `[MC]`** The server is built on `QTcpServer` with hand-parsed HTTP, not
`Qt::HttpServer`. Why?
**A:** `Qt::HttpServer` is an optional module absent from many installs
(including the build environment and possibly the user's), so depending on it
would break configuration on some machines. `QTcpServer` is always present,
and parsing the request ourselves makes HTTP legible — request line, headers,
blank line, body — instead of hiding it behind a framework.

**M4 `[MC]`** How does one line let the same client talk to localhost today,
a phone-to-laptop LAN tomorrow, and a Pi later — with no recompile?
**A:** The server URL is a `QSettings` value (`sync/serverUrl`, default
`http://localhost:8080`), injected into `AuthClient`. Changing where the
server lives is a settings edit, not a code change — the same
inject-the-dependency seam as `nowProvider` and the `AccountStore` path.

**M5 `[MC]`** `test_login_live` spawns the real server binary and hits it
over a real socket, instead of mocking the network. Why keep such a test
despite it being slower and needing a live port?
**A:** The three-bug interaction (reply error semantics × reason phrase ×
connection reuse) lived entirely in the seam BETWEEN client and server — a
mock of either side would have hidden it by construction. Only a real
socket through the real code path exposed it, and only that same path keeps
it from regressing. It's the auth equivalent of the UI regression suite:
reserved for bugs the unit tests can't see.

---

## N. Authentication & password security (design addendum: login)

**N1 `[MC]`** What exactly is stored for a password, and why each part?
**A:** `pbkdf2$<iterations>$<salt-hex>$<hash-hex>`. Never the password. The
**salt** (random per user) makes identical passwords hash differently and
defeats rainbow tables; **stretching** (200k iterated SHA-256) makes
brute-forcing a stolen file ruinously slow; the **iterations + salt are
stored alongside** so verify() re-derives with the original parameters, and
the **`pbkdf2` tag** lets a future Argon2 upgrade dispatch by algorithm
without forcing password resets.

**N2 `[T/F]`** Registering the same password twice should produce the same
stored string.
**A:** **False** — and a test pins the opposite. Each registration draws a
fresh random salt, so two users (or the same user twice) with identical
passwords get different stored strings. Identical stored hashes would leak
which accounts share a password and let one crack break many.

**N3 `[MC]`** Login distinguishes "no such user" from "wrong password"
internally, but the server sends ONE `bad_credentials` error for both. Why
the asymmetry?
**A:** The store needs the distinction (different internal results); the WIRE
hides it because telling an attacker which usernames exist is an information
leak (username enumeration). A real, deliberate security tradeoff — the UI
still shows a friendly "wrong username or password" either way.

**N4 `[MC]`** Why does `verifyPassword` read the iteration count and salt
FROM the stored string rather than using the current constants?
**A:** So hashes made with older parameters still verify after the constants
change. If you later raise iterations from 200k to 400k, every existing
account would fail to log in if verify used the new constant. Reading the
stored parameters makes the hash self-contained and parameter changes safe —
the same principle as additive JSON schemas: old records stay readable.

**N5 `[T/F]`** Using Google Sign-In would have been simpler, so it's the
better choice here.
**A:** **False for THIS project's goal.** The user explicitly wanted
independence from Google. Google login only handles authentication and makes
storage location Google's problem — the opposite of "host it on my own
laptop/Pi." Rolling our own auth (done safely: salted, stretched, never
plaintext) keeps storage a free choice and is a stronger portfolio line.
Simplicity isn't the only axis; ownership was the requirement.

---

## O. Sync — full-document sync with revisions (design-addendum-sync)

**O1 `[MC]`** The entire sync decision is a pure function of two values.
Which two, and what are the four outcomes?
**A:** *Did the server move since we last synced* (`serverRev !=
lastSyncedRev`) and *did we change anything locally* (`dirty`). Four rows:
neither → **Nothing**; only local → **Push**; only server → **Pull**; both →
**Conflict**, and a human chooses. `sync::decide()` is that table verbatim —
no network, no files — so the test IS the table. The async mess in
SyncService is thin wiring around a verdict that can't surprise anyone.

**O2 `[MC]`** A push carries `baseRevision`, and the server answers 409 if
its shelf moved past it. What is this pattern, and why "detect" instead of
"merge"?
**A:** Optimistic concurrency — HTTP's `ETag`/`If-Match` idea: proceed
assuming nobody interfered, let the version check catch you when they did.
Detection is v1-honest: it can never lose data silently, only ask. Merging
(per-entity or CRDTs) is real machinery deserving its own project — and a
bad merge LOSES data quietly, which is the one unacceptable failure mode.

**O3 `[T/F]`** A device that has never synced should default to "clean"
(dirty = false).
**A:** **False — it defaults to DIRTY, deliberately.** A never-synced device
must assume its local data matters. Existing data pushes up on first sync
(right); a brand-new second device meets one safe conflict prompt and picks
"use server version". One question beats one silent overwrite. The clean
default would be worse in both directions: a full local planner would report
"already in sync" and never upload.

**O4 `[SPOT THE BUG]`** After implementing pull, every sync immediately
reported new local changes to push — forever. Why?
**A:** Applying a pull goes through `AppData::replaceAll`, which emits
`changed()` so every screen rebuilds and the autosave writes the pulled
data. But the sync service SETS THE DIRTY FLAG on that same signal — so the
pull marked itself dirty. The `m_applying` reentrancy guard (set while
applying, checked in the changed-handler) breaks the loop. Same family as
LabelEdit's PopupFocusReason guard: when you both cause and observe an
event, you must recognise your own reflection.

**O5 `[MC]`** The server stores planners but contains zero planner code — no
Event, no Category, nothing. Why is that a feature?
**A:** The blob is opaque: `PlannerStore` shelves `{revision, data}` and
never looks inside. The planner format can evolve v6 → v20 without the
server updating, and there's simply less server code to get wrong. Client
corollary: JsonStore's conversion was split from its file I/O, so the SAME
`toJsonObject`/`applyJsonObject` feeds disk and wire — one format, two
destinations, zero drift.

**O6 `[MC]`** Sync requests contain no username. How does the server know
whose planner to serve — and why is that MORE secure, not less?
**A:** The Bearer token IS the identity: the server maps token → account,
minted at login. There's no username field to lie about — a stranger's
made-up token gets 401 whatever it asks, and a valid token can only reach
its own shelf (pinned by the isolation test: frank's fresh token sees an
empty revision-0 shelf, not erin's data). Tokens live in memory on both
ends; a server restart logs everyone out, and re-login mints fresh ones.

**O7 `[SPOT THE BUG]`** The test helper `awaitService(fire)` calls `fire()`
via `QTimer::singleShot(0, …)` instead of directly. Remove that and one
resolution path hangs the test forever. Which, and why?
**A:** `resolveUseServer()` — it applies the held server data and emits
`finished()` SYNCHRONOUSLY, with no network hop. Fired directly, the signal
(and the lambda's `loop.quit()`) runs BEFORE `loop.exec()` starts; the quit
is lost and the loop then waits forever. Deferring with `singleShot(0)`
puts the emission inside the running loop. General law: when awaiting a
signal that MIGHT fire synchronously, arm the loop first, fire from within
it.

---

## P. Per-account storage & data adoption (design-addendum-accounts)

**P1 `[MC]`** Before this change, logging in as a different user on the same
machine showed the SAME planner. Why — and what fixed it?
**A:** Login gated access but every account opened one global `data.json`;
accounts were isolated on the SERVER (separate shelves) but shared one LOCAL
file. Fix: scope local storage to the user — `data-<username>.json` plus
`sync/<user>/…` state keys — so switching users switches files. Threaded via
`MainWindow(username = "")`, default-empty so existing callers/tests are
untouched.

**P2 `[MC]`** The adoption migration copies the old global file rather than
moving it, then renames the original to `.pre-accounts.bak`. Why this dance
instead of a plain move?
**A:** Copy-then-backup means a failure at any step leaves the original
intact — the same write-then-replace instinct as `QSaveFile`. The rename
retires the global file so the NEXT user doesn't also adopt it (adoption is
one-time, for the first user after the upgrade), and keeping it as a backup
means a wrong guess about whose data it was is recoverable by hand.

**P3 `[T/F]`** Adoption runs every time a user logs in.
**A:** **False.** It's guarded: if the user's per-account file already
exists it does nothing (never clobbers their data), and once the global file
is retired to `.bak` there's nothing left to adopt. Pinned three ways —
first user adopts, second user starts fresh, re-run is a no-op.

**P4 `[SPOT THE BUG]`** The first draft of the adoption test built its own
paths under a `QTemporaryDir` and set `XDG_DATA_HOME` — and adoption
returned false. Why?
**A:** `QStandardPaths::AppDataLocation` resolves at process start, so
setting the env var mid-test came too late: the code's real paths pointed at
the profile dir, not the temp dir, and the seeded file and the derived user
path lived in different places. Fix: drive the REAL paths
(`defaultFilePath` / `filePathForUser`) with unique usernames and a
scope-guard that restores any real global file. Lesson: don't second-guess
where `QStandardPaths` points — ask it.

---

## Q. Share & compare (design-addendum-share)

**Q1 `[MC]`** The server can't compute "just alice's daily totals" for bob.
Is that (a) a missing feature, (b) a bug in `PlannerStore`, or (c) a direct
consequence of a deliberate design decision?
**A:** **(c).** The server treats every planner as an **opaque blob** — it
never parses planner internals (sync addendum §D), so it *cannot* extract
totals. That's why "share my planner" honestly means "permit this person to
fetch my blob", and all understanding (Compare.h + stats) happens on the
client. Dumb server, smart client — the trade-off is §H's privacy limit:
you share everything or nothing.

**Q2 `[T/F]`** If alice shares with bob, bob and alice can now see each
other's planners.
**A:** **False.** A grant is a **directed arrow**: alice→bob lets bob read
*alice*. Bob's planner stays private until BOB grants. Mutual visibility is
two grants, one made by each owner — because each person should give away
only what is theirs to give.

**Q3 `[MC]`** Iris has a valid token and requests `GET /planner/henry`
without a grant. The server answers: (a) 401, (b) 403, (c) 404?
**A:** **(b) 403 Forbidden.** 401 means "I don't know who you are" (bad
token → log in again); 403 means "I know *exactly* who you are, and the
answer is no" (→ ask the owner to share). Different problems, different
fixes — so `ShareClient` keeps them as different `Outcome` values, the same
reasoning that made `AccountStore::Result` an enum, not a bool.

**Q4 `[T/F]`** `ShareStore::revoke` on a grant that never existed returns
false, because there was nothing to revoke.
**A:** **False — it returns true.** Grant and revoke are **idempotent**: the
caller asked for an end state ("this name can't read me"), and that end
state holds. Retry-friendly APIs matter on flaky Wi-Fi — "you already
shared" and "already revoked" are not errors a human should see.

**Q5 `[MC]`** Why does `POST /share` check `m_accounts.hasUser(with)` and
answer 404 for unknown names, while `POST /unshare` deliberately doesn't?
**A:** Granting to a typo'd name would "work" silently and confuse both
humans later ("I shared, why can't you see it?"). Validate at the door —
and identity questions go to `AccountStore`, the one store that knows.
Revoking a typo'd (or deleted) name should still succeed: the requested end
state — no access — already holds. Doing beats existence-checking when
you're *un*-doing.

**Q6 `[HANDS-ON]`** CompareDialog holds `const AppData* m_mine` but
`AppData m_peer` — pointer to one, value member for the other. Explain both
choices in one breath.
**A:** `m_mine` is the app's **live aggregate** — borrowed (the window owns
it), `const` because comparing must not mutate. `m_peer` is a **snapshot**:
a document poured from the wire blob via `JsonStore::applyJsonObject`,
owned by the dialog, wired to nothing (no store, no tracker, no signals),
born when the dialog opens and gone when it closes. Live data is shared and
guarded; a loaded document is yours and disposable.

**Q7 `[MC]`** Why is the compare feature's arithmetic trustworthy across
two different people's data? (a) the server normalises both blobs, (b) the
same pure `stats::summarizeDay` runs on both `AppData`s, (c) CompareDialog
cross-checks the totals.
**A:** **(b).** `stats::summarize` never knew whose data it reads — it's a
pure function of an `AppData` (§3.5). Fetch, deserialize with the same
`JsonStore` path sync trusts, then run the *identical* summarizer on both
sides. The numbers are comparable because the code path is the same.

**Q8 `[T/F]`** With a 5-minute tolerance, a 5-minute-exactly focus lead is
judged `Ahead`.
**A:** **False — `Even`.** The boundary is inclusive (`>` tolerance, not
`>=`), pinned by a test at exactly 5:00 vs 5:01. And the tolerance exists
at all because this feature nudges ("mom tracked her walk — good moment to
start a block"), it doesn't hand out photo-finish rankings.

**Q9 `[SPOT THE BUG]`** A first draft displayed the focus delta with
`stats::formatSeconds(d.focusSeconds)`. When the peer was ahead, every
delta showed as `0s`. Why?
**A:** `formatSeconds` **clamps negatives to zero** (`qMax<qint64>(0, s)`)
— it's a magnitude formatter for durations, where negative time is a bug.
Deltas are signed, so the dialog's `formatDelta` prints the sign itself and
passes `qAbs(seconds)` down. Reuse the formatter, don't change its contract
for one caller.

**Q10 `[MC]`** `ShareClient` grew a `classify()` helper for the
status-code if-ladder that `AuthClient` and `SyncClient` each hand-rolled.
Why extract it now, and why NOT retrofit the two older clients in the same
commit?
**A:** The **rule of three**: twice might be coincidence; the third copy is
a pattern asking to be named. But back-porting into `AuthClient`/`SyncClient`
would churn *tested, shipped* code inside an unrelated feature — that's
hygiene work for its own focused pass (noted in the addendum), not a rider
on this one. Refactor opportunistically, land deliberately.

**Q11 `[HANDS-ON]`** The live test asserts the 403s as carefully as the
successful read. Argue why the refusals are "the feature".
**A:** A sharing system's promise is two-sided: the grant path proves
sharing *works*; the refusal path proves privacy *holds*. A bug in a
permission store doesn't crash — it silently shows someone a planner they
weren't given. So the test walks the full lifecycle (forbidden → granted →
readable → revoked → forbidden again) and distinguishes 401 from 403,
because both doors must be closed for the right reasons.

**Q12 `[T/F]`** `GET /planner/alice` needed a new routing mechanism in
AuthServer.
**A:** **Mostly false** — one `startsWith("/planner/")` prefix match; the
text after the prefix is the owner's name (a **path parameter**: nouns in
the URL, verbs in the method). The if-ladder router absorbed it in four
lines, which is exactly why the ladder is still the right size of
abstraction for this server.

---

## R. Packaging & deployment (INSTALLING.md, the deploy script)

**R1 `[MC]`** You copy `ticktimer.exe` to a friend's PC and it dies
instantly with `0xC0000135` before opening a window. Most likely cause:
(a) their antivirus, (b) a missing Qt DLL, (c) a corrupt copy?
**A:** **(b).** The exe *depends on* `Qt6Core.dll`, `Qt6Widgets.dll`, etc.
but doesn't contain them. Qt Creator puts them on the path when you press ▶;
a machine without Qt can't find them, so the loader fails before `main()`.
`0xC0000135` is literally "a required DLL was not found."

**R2 `[HANDS-ON]`** What does `windeployqt ticktimer.exe` actually do, and
why is it the heart of both the portable folder and the installer?
**A:** It scans the exe's imports, then copies every Qt DLL and plugin it
needs into the same folder — turning a machine-dependent exe into a
self-contained one that runs anywhere. Both deployment levels are just
wrappers: the portable zip *is* that folder; the installer *packages* that
folder. Get windeployqt right and everything downstream works.

**R3 `[MC]`** The `.rc` file is added to the target as
`$<$<PLATFORM_ID:Windows>:.../ticktimer.rc>`. Why the generator expression
instead of just listing the file?
**A:** A `.rc` only compiles on Windows (windres/rc.exe). The generator
expression evaluates to the path on Windows and to **nothing** everywhere
else, so the *same* `CMakeLists.txt` builds cleanly on Linux/macOS (where the
icon resource is meaningless) without a platform `if()`. One list, every OS.

**R4 `[T/F]`** The server launcher `.bat` closes its console window as soon
as the server starts, to stay out of the way.
**A:** **False — and deliberately.** The server's console window *is* its
UI: it prints the addresses a phone or a second PC must connect to. The
launcher keeps it open and even tells the user "leave this open." Hiding it
would hide the one piece of information the tester needs.

**R5 `[SPOT THE GAP]`** Before this session, a fresh install on someone
else's PC could register and log in but never sync. Why, and what fixed it?
**A:** The server address lived only in `QSettings` with no UI, defaulting
to `localhost` — where a tester's machine has no server. There was no way to
point at *your* server short of the registry. Fix: a **Server** field on the
login screen, persisted **on use** (submit), not on type, so a half-typed
address never gets saved. First-run config belongs on the first screen.

**R6 `[MC]`** The login dialog forgives `192.168.1.20:8080` (no scheme) by
prepending `http://`. Where does that live, and why there?
**A:** In `LoginDialog::serverUrl()`, the single accessor both `submit()`
and `main()` read. Putting it at the one read point means the fix applies
everywhere the value is used, and a scheme-less string never reaches `QUrl`
(which parses it unpredictably) — a "can't reach server" that was really a
parse failure is the most confusing kind.

**R7 `[HANDS-ON]`** Why does the installer set `PrivilegesRequired=lowest`
and install into the user profile, rather than `Program Files`?
**A:** A home app being tested on a girlfriend's laptop shouldn't demand an
administrator prompt. Per-user install into `{autopf}` under the profile
needs no elevation, still gets Start-menu/desktop shortcuts and a clean
uninstaller, and avoids the UAC friction that makes a casual tester bail.

**R8 `[SPOT THE CAUSE]`** Closing the app triggers Windows' "This program
might not have installed correctly" dialog. The program isn't an
installer. What two ingredients produced this, and what's the durable fix?
**A:** (1) PCA's installer-detection reads **version metadata keywords** —
and the server's FileDescription contained "updates" (written during the
auto-update session, machine-read as installer vocabulary). (2) The
heuristics only apply to **manifest-less** exes, and MinGW embeds no
manifest by default. Fix: an embedded application manifest (`asInvoker` +
supportedOS list — "modern app, stop guessing") in both `.rc`s, plus the
keyword removed. Lesson: metadata strings are read by machines with
opinions, not just humans.

---

## S. Update notices (design-addendum-update — the arc closer)

**S1 `[MC]`** Why did we ship "notify" (Level 1) instead of a self-installing
update (Level 3)? (a) time, (b) the platform fights it, (c) users hate
automatic updates?
**A:** **(b)** — with (a) as the honest consequence. A running Windows exe
is file-locked and cannot overwrite itself; self-update needs a helper
process, sequencing, and a rollback story whose failure mode is *bricking
the install*. Level 1 delivers the valuable half (version model, server
contract, non-nag UX) without fighting the OS, and Level 2 bolts on later
without changing anything shipped.

**S2 `[T/F]`** Comparing version strings with `<` works fine since versions
only ever grow.
**A:** **False — the trap the whole semver layer exists to avoid.** As
strings, `"18.10.0" < "18.9.0"` because `'1' < '9'`. Versions are parsed
into `{major, minor, patch}` and compared field-by-field *numerically*.
There's a named test pinning exactly this pair.

**S3 `[MC]`** `version::isNewer("banana", "19.0.0")` returns…?
**A:** **false** — the layer *fails closed*: anything unparseable is "not
newer". An update banner is an interruption; an interruption built on
garbage input is the worst bug this feature could have. Silence is always
the safe answer, so malformed input buys silence.

**S4 `[HANDS-ON]`** Explain the `RC_INVOKED` trick in `Version.h` and what
problem it kills.
**A:** The version numbers are plain `#define`s; all C++/Qt content sits
behind `#ifndef RC_INVOKED` — the symbol the Windows resource compiler
defines about itself. So the *same header* is included by C++ code and by
both `.rc` files. Before: "v18" lived in four places, guaranteed to drift.
After: bump one file; the code, both exes' version metadata, and the update
check all follow. A load-bearing value gets a single source of truth.

**S5 `[MC]`** Why does `/version` re-read `version.json` on every request
instead of caching it at startup? And why is the route token-free?
**A:** Per-request read makes announcing a release *an edit* — no server
restart, nothing to remember; the cost is re-reading ~200 bytes once per
app launch, i.e. nothing. (The live test writes the file into the RUNNING
server's directory and the next check sees it.) Token-free because "what's
the newest TickTimer?" isn't private — and the question must be answerable
by arbitrarily old clients whose login flow may predate anything.

**S6 `[T/F]`** If the update check fails (server down, no version.json,
airplane mode), the banner says "couldn't check for updates."
**A:** **False — it says nothing.** A check the user never asked for has no
right to report its failures; every non-Show path is deliberate silence.
Only "there IS something newer, and you haven't dismissed it" may speak.

**S7 `[HANDS-ON]`** State the banner rule, and where it lives. Why does
dismissing 19.0.0 not silence 19.0.1?
**A:** *Show iff the advertised version is strictly newer than the running
one AND is not the exact version recorded in `update/lastDismissed`.* It
lives in `version::decideBanner` — a pure function (sync::decide's sibling)
whose whole truth table is six QCOMPAREs in the domain suite. Dismissal is
per-version because ✕ means "stop telling me about THIS one", not "never
talk to me again"; equality, not ordering, against the dismissed value.

**S8 `[SPOT THE ISSUE]`** The `url` the banner opens comes from your server
over plain HTTP. What's the trust assumption, and when does it break?
**A:** The banner trusts your server (and the network path to it) to hand
back an honest URL. On your home Wi-Fi that's fine; on a hostile network a
man-in-the-middle could substitute their own link — same trust model as the
rest of the arc, but worth naming (addendum §G) before ever exposing the
server to the open internet. The fix-shaped words are HTTPS and signed
releases — future work, consciously deferred.

---

## T. The daily-driver pass (design-addendum-daily-driver)

**T1 `[MC]`** Why is *archive* a separate action from *done*, instead of
done tasks auto-hiding? (a) simpler code, (b) they're different statements,
(c) undo would be harder?
**A:** **(b).** Done = "I finished this" — today's victories belong on
today's list (and the strike-through is the reward). Archived = "stop
showing me this," a deliberate second decision. Auto-hiding on done would
make checking a box *feel like deletion* — the model gained a third life
stage (open → done → archived) precisely so the two statements stay
separate.

**T2 `[T/F]`** Archiving an activity is a soft delete — old events show
"(unknown activity)" afterwards.
**A:** **False — that's the whole point.** Archived activities stay in the
data, resolvable by id; every past event keeps its name and colour. Only
*lists and pickers* skip them. That's why archive is the ONLY retirement
path for an in-use activity: `removeActivity` refuses those (history would
dangle), and the Archive page doesn't even offer a delete button the
domain would bounce.

**T3 `[MC]`** New tasks are born `Priority::Medium`. Why not Urgent?
**A:** An urgent-by-default world makes "urgent" meaningless within a week
— rank is only information when it's *rare*. Same reasoning drives the
display rule: Medium shows no chip at all (chips must stay rare to stay
readable); Urgent gets the danger hue, Low a grey whisper.

**T4 `[HANDS-ON]`** A v6 file (no `priority` key) loads in v19.1. What
priority does every task get, and which line of code guarantees it?
**A:** Medium — `priorityFromString("")` (a missing JSON key reads as an
empty string) falls through to the Medium default, exactly like
`repeatFromString("")` → None before it. Additive growth, seventh format
version, still zero migration branches. There's a test pinning
`priorityFromString("critical!!")` → Medium too: unknown strings fail
*safe*, never loud.

**T5 `[MC]`** The owner reported "false data" from studying longer than
scheduled or forgetting to press focus. Which half was actually a bug?
**A:** Only the second. Running long while the timer runs is captured
fine — segments are real timestamps, overflow included. The hole was the
**missing fact** (studied, never pressed focus) and its mirror, the
**false fact** (timer left running into lunch). Hence the fix: a segment
*editor*, not a timer change — add the fact, or retract it.

**T6 `[T/F]`** Manually added segments are marked in the data so stats can
treat them differently from timer-tracked ones.
**A:** **False, deliberately.** A manual segment enters through the SAME
door (`appendSegment`) and looks identical — a fact is a fact, whoever
typed it. Marking them would create two classes of truth and invite every
reader to special-case one. (Related honesty: `removeSegment` refuses
out-of-range indices rather than clamping — a retraction must name exactly
the fact it retracts.)

**T7 `[HANDS-ON]`** Compare v2 shows two live-looking agendas side by side
with almost no new machinery. Which THREE old decisions paid for it?
**A:** (1) Sharing ships the WHOLE planner as an opaque blob — the peer's
events were already crossing the wire. (2) The blob becomes a full snapshot
`AppData`, so every query answers for the peer exactly as for you. (3)
`AgendaWidget` takes a `const AppData*` and only ever REPORTS via signals —
it never cared whose data it paints, so the same painter serves the live
side and the snapshot side, rows aligned by the shared `kSlotHeight`.

**T7-b `[MC]`** The peer's agenda is made read-only how — and why isn't
"just don't connect the edit signals" enough? (a) setEnabled(false),
(b) WA_TransparentForMouseEvents, (c) a ReadOnly flag on AgendaWidget?
**A:** **(b).** `setEnabled(false)` greys the whole column — the peer's day
would look broken, not read-only. And skipping connections isn't enough
because drag-resize *feedback* is handled inside the widget: a block that
wiggles when dragged but never saves is a lie. Transparent-for-mouse means
events never reach the widget at all — read-only by physics, not
discipline. (A ReadOnly flag would also work but adds a mode to a widget
that doesn't need one; the attribute already exists.)

**T7-c `[MC]`** You open Compare and the peer's day is empty, but you can
SEE their app across the table, full of blocks. What's the most likely
explanation? (a) a share-permission bug, (b) they haven't pressed Sync,
(c) the blob got corrupted?
**A:** **(b).** Compare reads the SERVER'S copy, and the server only holds
what each person last pushed — sync is a manual button (auto-sync is
backlog). Their live screen and the server's blob are two different
documents. The app now says this instead of showing a blank day: a
never-synced peer gets "ask them to press Sync first", and the peer column
is labelled "as of their last sync". Corollary: after they sync, you must
REOPEN Compare — the snapshot is taken when the dialog opens.

**T8-bug `[SPOT THE BUG]`** The first draft called `rebuildSegmentList()`
unconditionally inside `EventDialog::refresh()`. Tests passed. What goes
wrong at runtime, and what's the fix?
**A:** `refresh()` also fires on every live-timer tick — once per second.
Rebuilding the rows at 1 Hz means visible flicker, a deleteLater queue
growing sixty widgets a minute, and a ✕ button destroyed and recreated
under the cursor mid-click. The fix: a fingerprint (`m_renderedSegments`,
the segment count — sufficient because segments only append/remove, and
each mutation triggers its own refresh) so rows rebuild only when they
CHANGED. The lesson: "connected to changed()" and "connected to a timer"
are different contracts, and code sitting on both must respect the faster
one.

**T8 `[MC]`** `SpecialDay.color` uses an *invalid* QColor to mean "no
choice made." Where has this trick appeared twice before?
**A:** The invalid `QDate` as "DATE TBD" on tasks, and the empty string /
missing key throughout the JSON layer. Absence-as-default: the type's own
"not valid" state IS the answer, so no parallel `bool hasColor` exists to
disagree with it. Automatic urgency colouring resumes the moment the
colour is invalid — and "back to automatic" is a first-class button.

**T9 `[MC]`** Auto-sync is "a debounce, not a heartbeat." What's the
difference, and why does it matter for a burst of six quick edits?
**A:** A heartbeat pushes every N seconds regardless; a **debounce**
restarts a one-shot timer on every change, firing only after the burst
goes QUIET. Six edits inside 5s = one push, not six (a live test pins
revision +1 after three rapid edits). The server stays calm, the Wi-Fi
stays calm, and the sync still lands 5s after your last touch.

**T10 `[T/F]`** With auto-sync on, a conflict is resolved automatically —
otherwise it wouldn't be "auto."
**A:** **False — auto means auto-WHEN, never auto-WHO-WINS.** The timeout
calls the same `syncNow()` as the button, same truth table, same rule:
losing data is a choice a human makes. New wrinkle: the conflict can now
arrive with NO dialog open — so the held state became *queryable*
(`hasPendingConflict()`), the Sync button turns ⚠ as the nudge, and
SyncDialog checks the state on construction. Signals only reach the
living; state answers latecomers.

**T11 `[SPOT THE BUG]`** The login dialog's old init was:
`toggleMode(); m_registerMode = false; toggleMode();` with a comment
claiming it lands on login. Trace it. What's the second, worse lesson?
**A:** Trace: flip→register, set false, flip→**register again** — it
always opened on Create account, against its own comment. The worse
lesson: our UI test had already MET this bug (it couldn't find a "Log in"
button) and we worked around the symptom with an objectName lookup
instead of asking why. **When a test can't find what the code claims
exists, the claim is the suspect.**

**T12 `[MC]`** Archiving a life area (category) hides its tasks from
Upcoming. Which implementation makes restore exact: (a) set `archived` on
every child task too, (b) leave children untouched and make views check
the owning category, (c) move children to a hidden list?
**A:** **(b)** — the cascade rule `taskHidden = own flag OR category's
flag`, named once (`AppData::taskHidden`) and reused by every filter.
(a) would destroy information: a child that was ALREADY individually
archived becomes indistinguishable from one archived by the cascade, so
restoring the area would wrongly resurrect it. Untouched children = exact
restore, and the two flags compose for free.

**T13 `[SPOT THE BUG]`** The "show held conflict on dialog open" feature
shipped, compiled, passed all 89 tests — and the owner still met a ⚠
button whose dialog offered no choice. What happened, and what are the
two lessons?
**A:** The anchored text edit that inserted the check matched the FIRST
`refreshInfo()` in the file — inside the finished-lambda — not the
constructor's trailing call. The code ran after future syncs finished
(useless) and never on open. Lessons: (1) **position bugs survive
compilers** — string-anchored edits demand reading the diff in context;
(2) the repair made the ⚠ *derived* from `hasPendingConflict()` instead
of event-toggled — derive-don't-store (§3.5) applies to a single glyph
as much as to stats, because an event-toggled icon can strand.

**T14 `[HANDS-ON]`** The owner asked: "isn't old-vs-new just… not a
conflict?" Defend the truth table, then name the limit their instinct
correctly exposes — and the race the investigation uncovered.
**A:** The table already agrees: server-ahead + clean = silent pull;
local-ahead + server-unmoved = silent push; **conflict requires BOTH
lineages to have moved** — concurrent edits in the distributed sense.
The correctly-exposed limit: the sync unit is the WHOLE planner, so
"concurrent" means *any* two edits, even to different days — only
entity-level merge (per-entity versions + deletion tombstones) delivers
"conflict only if we touched the same thing" (backlog). The uncovered
race: an edit made while a push was on the wire had its dirty flag wiped
by the push's completion — silently never syncing. Fix: a change-
generation counter; dirty clears only if the pushed snapshot is still
the latest generation, and the debounce re-arms itself (the in-flight
change couldn't — the busy guard). Pinned by a deterministic live test
that edits between syncNow() and the reply.

**T15 `[SPOT THE BUG]`** After ONE sync conflict — resolved or not — the ⚠
never calmed, the resolution box greeted every dialog open, and auto-sync
never pushed again. `resolveKeepMine` clearly clears the held data. What
survived, and why did the original code get away with it?
**A:** `m_heldServerRevision` survived — resolveKeepMine NEEDS it as the
force-push base, so the original author cleared only the data, and back
then nothing read the revision after resolution: harmless. Then
`hasPendingConflict()` was defined as `heldRevision != 0` without
auditing the field's writes — giving old state a new consumer turned a
benign leftover into a permanent gate poisoning four behaviours. Fix: one
`clearHeldConflict()` (capture the base first, THEN clear whole), and a
live regression test that resolves a real conflict and asserts the
service comes back to life. **When old state gains a new consumer, audit
every write site.**

## U. Settings, identity & the visible window (design-addendum-settings)

**U1 `[MC]`** The "change the agenda hours" feature was built as a display
window over an unchanged grid rather than making `plan::kDayStartMinutes`
configurable. Why? (a) less code, (b) the grid is a domain rule and the
hours are taste, (c) QSettings can't store the grid?
**A:** **(b).** The planning grid ("30-minute slots, 6 AM–midnight") would
be true even in a command-line build — it governs what's *legal to plan*,
what stored events *mean*, and what AppData's doors validate. How much of
it your screen paints is per-machine taste. Making the grid configurable
would entangle old `data.json` files with new settings; a display window
touches only pixels. Consequence: **no format bump** — preferences live in
QSettings and never sync.

**U2 `[T/F]`** If you set the agenda to 8 AM–6 PM and you have a block at
6:30 AM, that block is hidden until you widen the window.
**A:** **False — "data always wins."** `windowCovering` stretches the shown
range (never shrinks it) to cover every event on the date. A hidden block
would be a haunted agenda: still refusing new plans over its slots
(`isFree`) while invisible. The Settings dialog states this rule right
under the combos.

**U3** Why is `windowCovering` a *public static* function on AgendaWidget
instead of a private helper?
**A:** It has three consumers that must agree: each widget (its own shown
range), WeekAgendaView (folds it over 7 *dates* to build a union window so
the columns and shared axis stay pixel-aligned), and CompareDialog (folds
it over 2 *datasets* for the same reason). One formula, every consumer —
private logic would be reimplemented, and reimplementation drifts.

**U4 `[MC]`** After narrowing the window, `emptySlotClicked(4)` fires when
the top visible row is clicked at 8 AM. What is 4? (a) the 4th visible row,
(b) the domain slot index of 8:00, (c) pixels/kSlotHeight?
**A:** **(b).** Slot indices stay DOMAIN indices everywhere (0 = 6 AM,
whatever is shown). The window moves the viewport, never the meaning of an
index — which is exactly why PickActivityDialog, both pages, and all the
existing tests needed zero changes.

**U5** The whole geometry refactor was ~one subtraction. What old decision
made that true?
**A:** Every slot↔pixel conversion already went through ONE function
(`slotTop`). It moved into the class (it now depends on widget state) and
gained `- firstShownSlot()`; painting, hover, hit-testing, and drag-resize
all followed automatically. "One door per conversion" is cheap the day you
write it and priceless the day a viewport appears.

**U6 `[T/F]`** `stats::summarizeWeek` now reads the week-start preference
from QSettings.
**A:** **False — it takes `Qt::DayOfWeek firstDay` as a defaulted
parameter.** A pure summarizer that reads QSettings stops being a function
of its arguments (the `now`-parameter lesson, third appearance). The
default (`Qt::Monday`) keeps every existing caller and test on the
historical behaviour; PlannerPage reads the pref and passes it down.

**U7** Name the four screens the week-start preference must reach, and
what breaks if one is missed.
**A:** The week agenda's columns, the week review's totals beneath them,
the "Week of …" label, and the month grid's columns/headers. Miss the
review and a Sun–Sat grid sits above **Mon–Sun numbers** — same screen,
two different "this week"s, a quiet lie. One shared formula
(`stats::weekStart`) is the fix; four hand-rolled snaps would drift.

**U8 `[MC]`** Why did the compare dialog's identity headers move OUTSIDE
the QScrollArea? (a) styling, (b) they were correct at 6 AM and gone by
9 PM, (c) performance?
**A:** **(b).** The old headers scrolled away with the agendas — vanishing
exactly when two look-alike columns most need naming. Pinned headers are a
table's header row in spirit. The UI test asserts *structure* (no
QScrollArea in the labels' ancestry), because a "text exists" check would
pass on the old bug.

**U9** How does CompareDialog learn YOUR username, and why that route?
**A:** `MainWindow (m_username) → SharingDialog → CompareDialog`, one
constructor parameter each. The dialog can't know it alone (it holds data
pointers, not identity), and SharingDialog is an honest courier — it never
displays the name, but it's the only object that opens compares. Empty
name degrades to "You" so tests and nameless sessions still read sanely.

**U10 `[SPOT]`** A local variable in the new axis code was named `slots`
and the file wouldn't compile — `expected unqualified-id before '=' token`.
What happened?
**A:** Qt defines `slots` (and `signals`, `emit`) as **macros** for its
signal/slot syntax, so the identifier was macro-erased: `const int  = …`.
The codebase had already documented this trap in `durationLabel`'s comment
— renamed to `slotCount`. Never use those three words as identifiers in
Qt code.

**U11** SettingsDialog rebuilds the "Day ends at" combo whenever the start
changes, instead of validating on OK. What's the principle?
**A:** **Make illegal states unpickable, not detectable.** After a rebuild
the end combo only contains `start+1 … midnight`, so "end before start"
cannot be expressed — a whole validation path (and its error message)
deleted by widget choice. Bonus rule: the user's previous pick is kept
when it survives the rebuild; losing a selection to an unrelated combo
feels broken.

**U12 `[T/F]`** Pressing Cancel in Settings after changing the combos
still applies the new hours until the app restarts.
**A:** **False.** Widgets edit local state; `save()` runs only on OK
(Pomodoro's persist-on-use rule), and MainWindow re-applies prefs to the
pages only after `accept()`. Cancel writes nothing and repaints nothing —
and the dialog never touches AppData, so it can't dirty the planner or
trigger a sync either.

**U13** AgendaWidget now connects to `AppData::changed` itself. Doesn't
that violate "the widget is told; pages own QSettings"?
**A:** No — the doctrine is about **policy** (settings, dialogs, app
config), not about observing the data a widget already paints. The shown
window is derived from the date's events, so an event landing outside it
must be able to change the widget's *height* with no page remembering to
say so (the UI test pins that self-sufficiency: it adds an event to
AppData and asserts the widget's minimumHeight grew, calling nothing on
the widget). WeekAgendaView had already set the precedent. QSettings
reads still happen only in pages (and session-glass dialogs).

**U14 `[HANDS-ON]`** Add "week starts on Saturday" end to end. List every
file you'd touch.
**A:** `SettingsDialog.cpp` — one `addItem(tr("Saturday"),
int(Qt::Saturday))`. `Prefs.h` — nothing (the getter already accepts any
valid DayOfWeek). Everything else — nothing: `stats::weekStart` is
modular arithmetic over any first day, and every consumer is already
parameterised. That a 7-way preference costs one line is the payoff of
U6/U7's design; if the answer had been "edit four snap formulas", the
design would have failed.

## V. The Pomodoro grows up (design-addendum-pomodoro)

**V1 `[MC]`** Three features arrived at once — notifications, the tracker
link, the mini timer — and all three forced the same refactor first.
Which? (a) a new thread for the countdown, (b) extracting the state
machine from the page into a PomodoroEngine service, (c) moving the
Pomodoro into the Settings dialog?
**A:** **(b).** A notification must fire while the page is hidden (being
hidden is the scenario notifications exist for); the mini window must show
the SAME countdown (two clocks that could disagree would lie); the link
needs a signal source that isn't a widget. State whose lifetime is the
app's belongs in a MainWindow-owned service — the TrackerService lesson,
second verse. No thread needed: one QTimer in the engine serves every
face.

**V2** The engine emits `changed()`, `modeChanged()`, and `phaseEnded()`.
Why three signals instead of one?
**A:** Each consumer subscribes at exactly its grain. Views need every
tick (`changed`). The LINK must hear only real transitions
(`modeChanged`) — on `changed` it would re-assert the tracker's kind once
per second, and every `start*` call commits the current interval first:
sixty one-second segments per minute. The notifier wants only natural
completions (`phaseEnded`) — and `skip()` deliberately doesn't emit it:
no toast for something you did with your own hands. The grain of a signal
is part of its meaning.

**V3 `[T/F]`** If the Pomodoro is running a focus phase and you're not
tracking any block, the link starts tracking your next planned block
automatically.
**A:** **Still false — but the rule softened in v19.7, so read the
verbs.** "Your NEXT planned block" is a guess, and the link never
guesses. What it MAY do (since v19.7) is adopt **the block under the
clock** — the one whose planned window covers this instant — and only on
the **play edge**: pressing ▶ is you acting, and you picked that block
when you planned it. No live block → a play edge waits. It still never
STOPS the tracker. (Bank history: the original answer here was an
unqualified "never picks a block" — corrected when the rule evolved,
same honesty policy as V9.)

**V4 `[MC]`** You pause the Pomodoro mid-focus while tracking a block.
What does the link record? (a) nothing, (b) break, (c) distracted?
**A:** **(c) — the owner's spec.** A pause mid-cycle means "I got pulled
away," which is exactly what the distracted kind exists for. Contrast
RESET: engine.engaged() goes false and the link takes its hands off —
abandoning the Pomodoro is not a statement about the block. That
paused-vs-abandoned distinction is honest STATE (the engaged flag), not a
heuristic over remaining seconds.

**V5** You're mid-Pomodoro (focus phase, link on) and you press the
agenda's "Break" button on the tracked block. Then, separately: you start
tracking a NEW block by pressing its "Break" button. What happens in each
case, and why the asymmetry?
**A:** Case 1: your Break **stands** until the Pomodoro's next transition
— a human's explicit command outranks the machine's standing one, so the
link only listens to its own `modeChanged` plus one tracker edge. Case 2:
that edge is Idle → tracking — a fresh join **snaps into the cycle's
rhythm** (Focusing, because the Pomodoro is mid-focus): you ticked "the
Pomodoro drives," and joining is the moment you hand it the wheel. The
asymmetry costs one remembered member (`m_lastTrackerState`).

**V6 `[T/F]`** The phase-end sound required adding QtMultimedia to the
build and new DLLs to the Windows deploy.
**A:** **False.** The sound is `QApplication::beep()` and the toast rides
`QSystemTrayIcon::showMessage` — both in modules the app already ships,
so `deploy-windows.bat` and windeployqt are untouched. The tray icon
itself is PAINTED at startup (a green disc with clock hands), not shipped
as a resource. Named limit: the beep is mute on some Linux desktops;
QtMultimedia + a bundled chime is the upgrade path, priced at one new Qt
module in every deploy.

**V7** Why does the notify checkbox take effect on the very next phase
end, with no restart and no re-wiring?
**A:** The preference is read **at fire time** (`prefs::pomodoroNotify()`
inside the phaseEnded handler) — derive-don't-store at preference scale.
There is no cached "notifications on" flag anywhere to go stale.

**V8 `[MC]`** The mini card's window flags are Qt::Tool +
FramelessWindowHint + WindowStaysOnTopHint, plus WA_TranslucentBackground.
Which one is the reason the card must paint its own background? (a) Tool,
(b) Frameless, (c) StaysOnTop, (d) TranslucentBackground?
**A:** **(d).** A translucent top-level starts as NOTHING — every visible
pixel is the widget's to supply (own-every-pixel, again), which is what
makes truly round corners possible. Frameless is what costs the OS drag
(hence the press-anchor + delta reimplementation), Tool keeps it off the
taskbar, StaysOnTop is the feature itself.

**V9** The mini window's FIRST version passed the main window as parent
"for memory ownership only," keeping window flags for top-level-ness.
v19.5.1 removed the parent. What did the first version miss, and what
did removing the parent cost?
**A:** On Windows, a widget parent on a top-level window is never ONLY a
memory arrangement: Qt maps it to a Win32 OWNER, and **owned windows are
hidden while their owner is minimized** — the pin-on-top card vanished
the moment the app was minimized, the exact scenario it exists for
(owner-reported bug, W9). Removing the parent cost the two services the
parent provided silently, now paid explicitly: PomodoroPage's destructor
deletes the card by hand, and `WA_QuitOnClose(false)` keeps the open
card from counting as "a window still open" (closing the main window
still quits the app — a face, not a second app). Corrected lesson: Qt
lets you SPLIT memory-parent from window-parent, but the OS may not —
platform window managers read the parent as ownership with behaviour
attached.

**V10 `[SPOT]`** A teammate "optimizes" the mini window by giving it its
own QTimer so it doesn't depend on the engine's signals. What breaks, and
which test catches it?
**A:** The two faces can now DISAGREE — pause on the page while the card's
clock keeps falling, drift after a skip, two sources of truth. The UI test
`miniTimerIsASecondFaceOfTheSameEngine` pins both directions: the card's
play button must move the ENGINE, and an engine tick must move the CARD's
label. One clock, N faces is the invariant.

**V11** `tickOneSecond()` is public. Defend that against "you exposed an
internal."
**A:** It's the determinism seam: production's QTimer calls it once a
second; tests call it in a loop and cross a whole 25-minute phase in
microseconds, no qWait, no flaky sleeps. Same idea as
TrackerService::nowProvider but simpler — time needs no faking when the
caller IS the clock. The engine's INVARIANTS don't depend on who calls it
(a tick is a tick); publishing it trades an untestable internal for a
deterministic suite.

**V12 `[HANDS-ON]`** Add a "long-break ends the cycle" option: after the
long break, the Pomodoro stops instead of flowing into round 5. Sketch the
change and name every consumer that updates for free.
**A:** Engine only: in `tickOneSecond`/`advancePhase`, when the finished
phase is LongBreak and the option is on, set running=false, engaged=false,
round=1 (plus a `setStopsAfterCycle(bool)` told by the page, which owns
the new checkbox + QSettings key). `modeChanged` fires → the LINK releases
the block (engaged false = hands off); `changed` fires → PAGE and MINI
repaint to the stopped state; `phaseEnded` already fired → the toast still
announces the break's end. Three consumers, zero edits outside the engine
and the page — the payoff of one machine, N faces.

## W. Block-start alarms (design-addendum-block-alarms)

**W1 `[MC]`** The owner asked for "notification for the AgendaWidget."
Why does the alarm live in a service instead? (a) widgets can't own
QTimers, (b) up to ten AgendaWidgets are alive at once and none may be
visible when the alarm fires, (c) performance?
**A:** **(b).** Ten painters would mean ten toasts, and a hidden widget
can't notify — being elsewhere is the scenario notifications exist for.
A block's schedule is DATA; the thing that watches data plus the clock
is app-lifetime state: BlockAlarmService, third sibling of
TrackerService and PomodoroEngine. ((a) is false — QWidget is a QObject
and owns timers fine; it's just the wrong owner.)

**W2** There is no stored alarm list. How does the service know when to
wake, and what happens when you drag a block to a new time?
**A:** It DERIVES the single next start from AppData and arms one
single-shot for exactly then; `AppData::changed` re-derives. Dragging a
block re-aims the alarm with zero alarm-specific code, because the timer
was never anything but a view of the data — derive-don't-store, applied
to time.

**W3 `[T/F]`** After your laptop sleeps through three planned starts,
you get three toasts on wake so you know what you missed.
**A:** **False — you get silence.** Anything staler than the 2-minute
grace window is skipped, but still swept behind the high-water mark so
it can never resurrect. Ten stale pop-ups about a morning that already
happened is noise, not help; the agenda itself is the record of what you
missed.

**W4** Name the two halves of the own-hands rule as it applies to
alarms.
**A:** (1) A block created already-underway announces nothing — its
start is behind the mark, which was born at "now". (2) A block you are
ALREADY tracking when its start arrives is muted at toast time
(MainWindow checks `isTrackingEvent`) — announcing what the user is
actively doing is the machine talking over them. Same family as the
Pomodoro's silent skip().

**W5 `[MC]`** Why `Qt::PreciseTimer` here when the whole app otherwise
uses default timers? (a) coarse timers can't do single-shot, (b) coarse
allows ~5% slack, which on a 40-minute wait is a toast two minutes late,
(c) precise timers survive suspend?
**A:** **(b).** Coarse timing batches wakeups for battery — fine for
repaints, wrong for alarms; 5% of a long nap blows straight past the
grace window's spirit. ((a) is false; (c) is false — nothing survives
suspend, which is why the nap is ALSO capped at an hour: int-msec
overflow safety plus an hourly self-heal.)

**W6** The signal carries event IDS, not ready-made strings. Give both
reasons.
**A:** Freshness and safety. The receiver resolves titles from AppData
AT TOAST TIME, so an edited title stays truthful and a deleted block
simply skips (an id can only miss, and a miss is handled; a pointer
could dangle). It also keeps the domain service free of presentation —
composing "Study · 9:00 – 10:30" is the UI's voice, not the domain's.

**W7 `[SPOT]`** A teammate writes the test: construct the service, then
set `alarm.nowProvider = fixedClock;` like TrackerService does. Every
assertion about announcements fails. Why?
**A:** This service reads the clock IN ITS CONSTRUCTOR — the high-water
mark is born at "now" — so by the time the seam is patched on, the mark
is already wall-clock real and every test event (July 2026 in the
fixtures) sits in its past, permanently behind the mark. The lesson the
tests forced: a dependency used in the constructor must come in THROUGH
the constructor — hence the second ctor taking the clock function.
"Inject after" only works for dependencies first touched after.

**W8 `[HANDS-ON]`** Add "warn me 5 minutes before a block starts."
Sketch the smallest honest change.
**A:** In the service: announce at `start − lead` instead of `start` —
both `poll()`'s due-test and `rearm()`'s next-derivation subtract the
same lead (one constant or a told-by-the-page setter; the service still
never reads QSettings). The mark/grace machinery is untouched — it
guards INSTANTS, not meanings. UI: one more Settings row; MainWindow's
toast wording gains "in 5 minutes". The deliberate scope question to
raise before building: is it a SECOND toast (warn + start) or a moved
one? Two voices per block flirts with nagging — the addendum parks it
for exactly that reason.

**W9 `[SPOT]`** (v19.5.1) The mini timer "only stays on top if the main
window is also visible; minimize the app and the card disappears." The
card's flags include WindowStaysOnTopHint. What's the actual culprit,
and why doesn't the fix leak or zombify the app?
**A:** The culprit is the PARENT, not the flags: on Windows a parented
top-level is an OWNED window, and Windows hides owned windows while
their owner is minimized — StaysOnTop never gets a vote on a hidden
window. Fix: construct with `nullptr` parent. The two things the parent
provided silently are paid back explicitly — `~PomodoroPage()` deletes
the card (no leak), and `WA_QuitOnClose(false)` stops the parentless
card from keeping the app alive after the main window closes (no zombie
process running one tiny card). The regression test pins the
ARRANGEMENT (parentless + the attribute + the flags), since offscreen CI
can't observe real Win32 stacking.

**V13** (v19.6) The tracked block's live badge shows `● Focusing · 7:12`
in digital m:ss instead of the app's usual "7m" prose style. Defend the
inconsistency.
**A:** The badge exists BECAUSE the existing feedback moved too slowly
to be believed (a 2-hour block's bar grows ~half a pixel per minute —
the owner watched 7 minutes and saw "nothing"). A value that only
changes once a minute would re-create that silence; visibly ticking
seconds ARE the reassurance. Consistency is a value, but it lost this
trade to the feature's entire purpose. (Also note the width rule: the
TITLE yields pixels to the badge, never the reverse — an elided name is
recoverable context, a hidden live-state is the bug being fixed.)

**V14** The Pomodoro page's status line went stale in one specific
sequence during development. Which, and what's the wiring lesson?
**A:** Track a block first, THEN press Start: the link computes desired
== Focusing == current tracker state and rightly does nothing — no
tracker signal fires, and a status line wired only to tracker signals
keeps saying "press Start". The sentence depends on BOTH machines
(engine engaged/phase + tracker state), so it must listen to both
(engine::modeChanged joined the connects). Lesson: wire a derived
display to every input of its derivation, not to the input that
usually moves — the test that walks all four sentences caught it.

**V15 `[SPOT]`** With a fake `nowProvider`, `liveSeconds()` returned
values mixing two different clocks, and one old test stayed green
anyway. Both mechanisms?
**A:** Four methods inside TrackerService bypassed the seam and called
`QDateTime::currentDateTime()` directly (liveSeconds, beginInterval's
start stamp, the heartbeat, commit's end stamp) — so live time =
realStart.secsTo(fakeNow), two clocks in one subtraction. The old test
stayed green because it had adapted to the hole: `qWait(1100)` of REAL
sleep made the wall clock move — passing for the wrong reason, and
1.1 s slower every run (the suite dropped ~1.3 s → ~0.2 s when fixed).
Repair rule: seams are audited by GREP over the file, not patched at
the symptom line — holes come in families. And any sleep inside a test
is a smell: it usually means the code reads a clock the test can't
reach.

**V16** (v19.7) Adoption fires on exactly two edges — engine running
false→true, and the link being enabled while running. Give the failure
mode each EXCLUDED trigger would cause.
**A:** *Phase flips*: the pomodoro flips focus→break every few minutes;
if flips adopted, a block you explicitly **Stopped** would be re-grabbed
minutes later — the machine wrestling a direct human command (the same
courtesy rule as manual kind-switches standing). *Pauses / paused
enable*: an idle-but-engaged Pomodoro adopting would immediately stamp
**Distracted** onto a block the user never touched — recorded slander.
*Every tick*: that's V2's segment-fragmentation failure. The edge set is
the answer to one question: which triggers are *the user acting right
now*? Only those may reach for the block — and `liveEventNow()` can hand
them at most one, because the domain forbids overlap.

**W10** (v19.8) The tracker now stops itself when the tracked block's
window closes. Where does the watching happen, and why is that placement
almost free?
**A:** In `enforceWindow()`, ridden by the SAME 1-second tick that
already repaints the live UI — the watcher was already running; the
feature added a check, not a timer. It reuses `canTrackNow` (the start
door) as the exit door, so both boundaries share one honesty rule; the
committed segment keeps its REAL end stamp (a breath past the boundary),
because segments record what happened, not what was planned. Bonus
behaviour for free: a deleted block fails `canTrackNow` too, so deletion
ends its own tracking.

**W11 `[MC]`** When the block the Pomodoro was driving runs out, the
engine gets `pause()` — why not `reset()`, and why not nothing?
(a) reset loses less state, (b) pause preserves the cycle so ▶ after
lunch adopts the next block, (c) nothing would double-count time?
**A:** **(b).** Reset would throw away the round/cycle the user earned;
nothing would leave a "Focusing" Pomodoro driving no block — the exact
confusion this arc started with. Pause keeps `engaged` true, and the
rule-8 interlock (a paused Pomodoro never adopts) becomes load-bearing:
the pause must not instantly re-grab the next live block and stamp it
Distracted. Note the direction too: this is the link's ONE
tracker→engine message — adapters may be bilingual precisely so the two
machines stay monolingual.

**W12** The app plays .wav chimes but the owner received .mp4 preview
files. Explain both format choices and the build-system posture.
**A:** In-app: `QSoundEffect` wants uncompressed WAV — low-latency,
decoded without codec roulette — and the two synthesized chimes ride
INSIDE the binary as Qt resources (nothing beside the .exe to forget).
The .mp4s are courtesy previews (AAC travels well). Build: Qt Multimedia
is `OPTIONAL_COMPONENTS` behind `TICKTIMER_HAS_MULTIMEDIA`; absent, the
beep path compiles — a missing module costs sound, never features or
builds. (Also the likely original mystery: `QApplication::beep()` maps
to Windows' "Default Beep", which many sound schemes silence.)

**W13** (v19.9) The owner's build played the Windows beep instead of the
chimes and showed no popup — yet v19.8's code was "working as designed".
Name the two design mistakes that sentence contains.
**A:** (1) *A silent degrade*: Qt Multimedia is optional in the Qt
installer; the owner's kit lacked it, and the `#ifdef` fell back to the
beep with no trace — a downgrade nobody can see is indistinguishable
from a bug (fix: three tiers, and the configure step PRINTS the chosen
one). (2) *A rented pipeline for a guaranteed job*: `showMessage`
SUBMITS a balloon that Windows may decline without error (Focus Assist,
per-app settings). An alarm's whole job is to interrupt; it cannot
depend on a switch another program controls (fix: NotificationToast, an
app-owned window no pipeline can eat).

**W14 `[MC]`** Tier 2 sound calls `PlaySoundW(bytes, SND_MEMORY |
SND_ASYNC)` with the WAV bytes captured BY VALUE in the chime lambda.
What bug does that capture prevent? (a) a data race, (b) the async
player reading a freed buffer, (c) resource-file locking?
**A:** **(b).** SND_ASYNC returns immediately and the OS keeps reading
from OUR buffer while the chime plays — a locally-read QByteArray would
be destroyed at scope end, mid-playback. The by-value capture ties the
buffer's lifetime to the lambda's, and the lambda lives as long as the
connection: provably longer than any playback. (Why winmm at all: it is
ALWAYS present on Windows — the real chime with zero installs — which
is what makes it a legitimate tier between QSoundEffect and the beep.)

**W15 `[SPOT]`** The toast-stacking test failed: after clicking the
first toast away, the survivor stayed in slot 2 instead of closing
ranks. restack() was correctly connected to destroyed(). What happened?
**A:** `destroyed()` is emitted WHILE ~QObject runs, and QPointers to
the dying object are not guaranteed null at that instant — restack's
`removeAll(nullptr)` kept the departed, counted its height, and parked
the survivor one slot low. Fix: the destroyed handler removes the
SENDER explicitly (compare against the QObject* the signal carries)
before restacking. Rule: registries of QObjects must evict on
destroyed() by identity — trusting the QPointer null-out mid-destruction
is a timing bet.

**W16 `[SPOT]`** (v19.9.1) The app built; test_ui.exe failed with
`undefined reference to __imp_PlaySoundW`. The code was correct. Read
the failure's STAGE, name the cause, and give the CMake shape that
prevents the whole class.
**A:** "undefined reference" = the LINKER's voice (compile passed: the
declaration existed) — so a library is missing from THAT TARGET's link
line. MainWindow.cpp compiles into three targets; winmm was linked to
the app only, and test_ui compiled the same tier-2 code with nobody
offering the symbol. Shape: an INTERFACE library (`ticktimer_sound`)
carrying the tier's usage requirements (link + compile definition),
linked by every consumer of the source — dependencies travel WITH the
source, never sprinkled per target. Verification habit that makes the
claim testable: `-DCMAKE_DISABLE_FIND_PACKAGE_Qt6Multimedia=ON` builds
the fallback tier on demand.

## X. Repeat, made real (design-addendum-repeat)

**X1** Task.repeat existed since v7 — stored, editable, shown as a ↻
chip — yet this session's addendum calls that state "worse than
absence." Defend the phrase, and say what changed.
**A:** A stored-and-shown rule that nothing acts on is decoration
wearing behaviour's clothes: it PROMISES. Users set "Weekly", see the
chip, and trust a recurrence that never comes — silent wrongness, the
same disease as the invisible link (V-section) and the silent sound
downgrade (W13). The change: setTaskDone now spawns the next occurrence
(due date advanced by nextOccurrence, everything carried), and events
gained the same vocabulary with a calendar-driven roll.

**X2 `[MC]`** The whole recurrence design hangs on one invariant. Which?
(a) every chain has a seriesId, (b) the rule lives on the NEWEST link,
(c) spawns are logged in a ledger?
**A:** **(b).** When an occurrence advances, the old item is STRIPPED of
its rule and the new one carries it. That buys duplicate-spawn immunity
for free (done→undone→done finds no rule the second time — the state
permitting the bug is unrepresentable), honest archives (finished items
wear no lying ↻ chip), and chains without a chain table (no seriesId,
no parent pointers — history items are plain items).

**X3 `[T/F]`** Open the app after two weeks away and a daily-repeating
block backfills fourteen occurrences so your calendar is complete.
**A:** **False — no retroactive occurrences.** rollRepeats re-arms the
rule at the first rule-date >= today; an empty plan for a day you
weren't there is noise pretending to be history (and would poison the
reviews' plan-vs-actual honesty). Sibling rule: the spawn passes the
same isFree door as a hand-made block — occupied dates are SKIPPED, one
period at a time (a year's worth, then the rule stays put and tomorrow
retries), never fought.

**X4** Why does a spawned block DROP its task link (demoting it to
title text), and where has that move appeared before?
**A:** Next Monday's block shouldn't claim a deliverable that may be
done by then — and if the task itself repeats, ITS next occurrence is a
different id anyway. The demotion (reference → text, keeping meaning
while protecting integrity) is the removeTask downgrade pattern, third
appearance: the option BETWEEN refuse and cascade.

**X5 `[SPOT]`** rollRepeats collects due ids into a vector FIRST and
only then loops mutating. A teammate "simplifies" it into one loop over
m_events that appends spawns as it goes. What breaks?
**A:** appendGuardedEvent/m_events.append can REALLOCATE the vector
mid-iteration — every pointer and iterator into it (including the loop's
own) dangles: the classic invalidated-iterator trap. The code even
re-fetches `old` by id inside the second loop and comments "`old` may
dangle past this line" after the append. Rule: never grow a container
you're walking; ids are stable, pointers are not.

**X6** Event.repeat is typed `Task::Repeat`. Prosecute and defend that
naming in two sentences each.
**A:** *Prosecution:* an Event field named after Task is a lie of
provenance; a neutral `Repeat.h` costs twenty minutes and reads right
forever. *Defence:* the enum plus four helpers were BORN in Task.h and
every call site names them there — moving them churns a dozen files to
change zero behaviour, and the debt is priced (a comment at each borrow
site) and cheap to repay when a third repeater appears. The addendum
takes the debt knowingly: naming purity is a value, and so is a diff
reviewable in one sitting.

**X7 `[HANDS-ON]`** Add "every weekday (Mon–Fri)" as a repeat option,
end to end. Files?
**A:** Task.h: one enum value + one case in nextOccurrence (skip
Sat/Sun: +1 day, then while weekend +1) + repeatToString/FromString/
repeatLabel entries. TaskDetailDialog + EventDialog: one addItem each
(enum-order combos — index still IS the enum if Weekdays is appended
LAST; inserting mid-enum breaks the trick and every stored int — so
append, never insert). JSON: nothing (strings, not ints, on disk —
that's WHY strings were chosen). rollRepeats/setTaskDone: nothing —
they call nextOccurrence. One rule, one place, everything else free.

---

## Section Y — Model/View (the Upcoming page)

*Covers the v20 refactor: `TaskListModel`, `TaskFilterProxy`,
`TaskCardDelegate`, the rewired `UpcomingPage`, and
`diagrams/model_view_pipeline.puml`. See `design-addendum-model-view.md`.*

**Y1 `[MC]`** Before v20, how did the Upcoming page update when a task
changed? (a) it called `dataChanged()` on the affected row; (b) it deleted
every card widget and rebuilt the whole list from `upcomingTasks()`; (c) it
diffed old vs new and patched only what moved; (d) it did nothing — Qt
repainted automatically.
**A:** (b). Rebuild-on-`changed()` — the pattern every *other* page still
uses. Honest (no second copy to drift) but the opposite of model/view.

**Y2** What are the *only two* methods that define `TaskListModel`, and what
does each answer?
**A:** `rowCount()` — "how many rows?" — and `data(index, role)` — "what is
the value of THIS role for THIS row?". Everything else (refresh, the roles
enum) is convenience around those two. A view asks nothing else to show a list.

**Y3 `[T/F]`** The model stores `const Task*` pointers into AppData's vector to
avoid copying.
**A:** FALSE — and this is the trap. `upcomingTasks()` returns pointers *into*
`m_tasks`, which reallocates/moves the instant a task is added or edited. The
model stores a **by-value `QVector<Task>` snapshot** instead; storing the
pointers would dangle on the next edit. (The old card code copied `*task` for
the same reason.)

**Y4** Why does `refresh()` use `beginResetModel`/`endResetModel` instead of
the finer `dataChanged()` / `beginInsertRows()`?
**A:** Because the underlying list is fully *re-derived* by the domain on every
change — we cannot honestly say "row 2 changed." The honest statement is
"assume everything moved," and model-reset is exactly that. `dataChanged` /
insert/remove are the right tools when you mutate one *known* row; that is a
later lesson, not this list.

**Y5 `[MC]`** Which pair of overrides does `TaskFilterProxy` implement, and
what does each control? (a) `paint` + `sizeHint`; (b) `rowCount` + `data`;
(c) `filterAcceptsRow` + `lessThan`; (d) `insertRows` + `removeRows`.
**A:** (c). `filterAcceptsRow()` = the filter (which source rows the view
sees); `lessThan()` = the sort (which row comes first). The canonical proxy
pair.

**Y6 `[EXPLAIN THE TRADEOFF]`** A delegate paints rows instead of building a
widget per row. State the win and the cost in one sentence each.
**A:** *Win:* one delegate paints `N` rows with a `QPainter`, so a thousand
tasks cost one object, not a thousand widget trees — and the data side becomes
unit-testable. *Cost:* a painted row has no child widgets to `connect()`, so
every interaction must be recovered by hit-testing the mouse in `editorEvent`.

**Y7** How does a *flat* `QListView` get the Overdue / This week / Later
section headers?
**A:** Three parts: (1) the model exposes a `BucketRole`; (2) the proxy sorts
by due date so buckets are contiguous; (3) the delegate draws a header only
when a row's bucket differs from the row above (`startsBucket()`), reserving
the height in `sizeHint()`. One header per group, and it survives filtering
(the first surviving row is always row 0 → draws its own header).

**Y8 `[SPOT]`** A teammate "cleans up" `TaskCardDelegate` by computing the
checkbox rect inline in `paint()` and, separately, computing it inline again in
`editorEvent()`. Both look correct. What have they set up?
**A:** A drift bug waiting to happen. The whole point of the private
`geometryFor()` helper is that `paint()`, `sizeHint()`, and `editorEvent()`
read the *same* layout — so the checkbox you see is the checkbox you can click.
Two independent copies will disagree the first time anyone retunes a metric,
and the clickable zone will silently slide off the drawn one.

**Y9** In `editorEvent`, why does clicking the checkbox emit
`doneToggled(id, true)` rather than toggling a stored bool?
**A:** Because every task in Upcoming is undone *by definition* (the model only
holds undone, dated tasks), so the only meaningful action is "mark done." The
delegate holds no state; it reports intent, the page calls
`AppData::setTaskDone`, `changed()` fires, the model re-snapshots, and the row
simply drops out on the next reset. Same re-derive loop, no hand-removal.

**Y10 `[T/F]`** The edit dialog opened from a card is parented to the
`QListView`.
**A:** FALSE — it is parented to `window()`. Saving resets the model and the
clicked row is destroyed; a dialog parented to a disappearing row is the
double-free the app fixed once already. The window outlives the edit.

**Y11 `[SPOT]`** `TaskListModel.cpp` deliberately does **not** `#include
"Theme.h"`, and inlines `QColor(0x61,0x69,0x74)` for its fallback colour. Why
the ceremony?
**A:** `Theme.h` transitively includes `QApplication` (a Widgets header).
Including it would drag the whole widget toolkit into the model and break
`test_taskmodel`, which links **Gui-only** on purpose. Keeping the model
widget-free is the architectural statement; `#616974` is `theme::inkSoft()`'s
value, inlined rather than importing a GUI to read one constant.

**Y12** What broke in the existing UI test, and why is the fix a *better* test?
**A:** `upcomingLensesFilterByPriority` used to find task titles by hunting
`QPushButton`s with matching text — but titles are now *painted*, so there are
no such buttons. It was retargeted to read the view's model
(`view->model()->rowCount()` + `DisplayRole`). Better because it asserts on the
**source of truth** the delegate merely renders, and a painting bug can no
longer fool it.

**Y13 `[HANDS-ON]`** Add a "Sort by title A–Z" toggle to Upcoming (ignoring due
date). What do you touch?
**A:** `TaskFilterProxy` only: add a mode flag and a setter that
`invalidate()`s, and branch in `lessThan()` (title-first when the flag is on,
date-first otherwise). Add one `QToolButton` in the page wired to the setter.
The model, the delegate, and AppData are all untouched — which is the proxy's
whole reason to exist: sorting is view logic, so it lives in the view layer.

**Y14 `[WHAT WOULD BREAK]`** You delete the `proxy->sort(0)` call in the proxy
constructor but keep `lessThan()`. What happens on screen?
**A:** Nothing sorts — a proxy does not sort until asked, even with `lessThan`
defined. Rows appear in source order (still date-sorted here, because the model
snapshot came from the already-sorted `upcomingTasks()`), so it *looks* fine
until the model stops pre-sorting — then the section-header runs fragment,
because the delegate's contiguous-bucket assumption no longer holds.

**Y15 `[MC]`** (v20.1) Before the granular refactor, what did `refresh()` do on
*every* change? (a) emit `dataChanged` for the edited row; (b) `beginResetModel`
/ `endResetModel` — re-snapshot the whole list; (c) diff old vs new; (d)
nothing.
**A:** (b). Honest, but "rebuild-on-change wearing a model's coat" — the view
lost scroll + selection each time. v20.1 replaced it with a diff.

**Y16** Why must the model *diff* to update granularly — why can't it just emit
`dataChanged` for the row that changed?
**A:** Because the data source is a **derived query** that recomputes wholesale.
It never says "row 3 changed"; it hands back a fresh list. The model has no
row-level change events to forward, so it must reconstruct what changed by
comparing the previous snapshot to the new one.

**Y17 `[EXPLAIN THE TRADEOFF]`** `refresh()` handles insert/remove/in-place
edits granularly but *resets* when a surviving row reorders. Defend the reset
instead of writing move handling.
**A:** A minimal move sequence exists, but a *half-correct* granular diff is
strictly worse than a reset — it desyncs view from model (blank/duplicate rows,
crashes on the next click). The reorder case is rare (only a due-date edit) and
proving `beginMoveRows` correct is fiddly. So: handle what you can prove, reset
loudly for the rest, in one place, with a comment. "Granular when you can prove
it, reset when you can't." A test pins the boundary so nobody removes the reset
without replacing it.

**Y18 `[SPOT]`** (v20.1) Editing *only* a task's `description` fires no
`dataChanged`, yet the stored snapshot is still updated. Bug or intent?
**A:** Intent. `rolesEqual()` compares only fields the **card paints** (title,
category, due date, priority, repeat) — `description` isn't one, so no role
moved and repainting would be waste. The snapshot row is still reassigned to
stay fresh; the signal is just suppressed. Emitting `dataChanged` for an
invisible change would repaint for nothing.

**Y19 `[HANDS-ON]`** (v20.1) The bottom-up order of the REMOVE loop and the
top-down order of the INSERT loop aren't stylistic — swap either and something
breaks. Why each direction?
**A:** REMOVE bottom-up: removing row `i` shifts every row after it down by one;
walking high→low means the indices you haven't visited yet are still valid.
INSERT top-down: inserting at `j` in increasing `j` preserves the invariant
"`m_rows[0..j]` already matches `next[0..j]`," so each decision is a simple
"is the id here the one I expect?" — go high→low and that invariant collapses.

**Y20 `[MC]`** (v20.2) What is the ONE structural difference between
`TaskListModel` and `CategoryTaskModel`? (a) one is a QAbstractListModel, the
other isn't; (b) CategoryTaskModel's query is PARAMETERISED by a category and
re-pointable via setCategoryId(); (c) CategoryTaskModel has no roles; (d)
TaskListModel can't be unit-tested.
**A:** (b). Upcoming wraps a fixed global query; Activities wraps "tasks of WHICH
category," so it grows setCategoryId() and re-snapshots when the rail selection
changes. Everything else (QAbstractListModel, roles, testability) is shared.

**Y21 `[EXPLAIN THE TRADEOFF]`** (v20.2) Why does the Activities task list have
NO proxy, when Upcoming has one?
**A:** A proxy earns its place by filtering or sorting. Upcoming needed both (a
priority lens, due-date order). Activities has no lens and keeps insertion order,
so a proxy would be an inert layer between model and view — pure cost, zero
benefit. The restraint is the lesson: add a proxy when you need one, not by
reflex.

**Y22 `[WHAT WOULD BREAK]`** (v20.2) The add-task input is now a PERSISTENT
widget. What specific bug did that eliminate, and how?
**A:** A use-after-free. The old detail pane rebuilt everything on changed(), so
`returnPressed → addTask → changed() → rebuildDetail → delete input` freed the
very widget whose signal was still on the stack. With the input persistent, only
the task list's *model* updates on change; the input is never destroyed, so the
crash is impossible by construction (not merely deferred with deleteLater).

**Y23 `[EXPLAIN THE TRADEOFF]`** (v20.2) `CategoryTaskModel::refresh()` is a plain
reset, but `TaskListModel::refresh()` is a granular diff. Isn't that inconsistent?
**A:** No — it's right-sizing. Upcoming is a long list where a reset would yank
your scroll on every edit, so the diff pays off. A per-category list is short and
fully visible; there's no scroll to preserve, so a reset costs nothing to the eye
and a diff would be gold-plating. Same pattern, dialled to each list's need.

**Y24 `[SPOT]`** (v20.2) The task `QListView` sits inside the detail pane's
`QScrollArea`, yet has its vertical scrollbar disabled and a fixed height set from
`updateTaskViewHeight()`. Why not just let the QListView scroll?
**A:** Nesting a scroll inside a scroll is bad UX (two scroll regions fighting).
A QListView normally wants to own scrolling, but here it's one section of a taller
page, so we let it report its FULL height and hand scrolling to the outer
QScrollArea — the whole pane scrolls as one. The cost is recomputing the view's
height whenever the row count changes.

**Y25 `[T/F]`** (v20.2) Deleting `TaskRow` contradicts the "second-consumer rule"
that created it.
**A:** False — it's the rule's inverse. TaskRow was extracted when TWO pages
needed the identical row. Both now paint via delegates, leaving ZERO consumers, so
the shared widget is dead code. Extraction at two consumers and deletion at zero
are the same principle: keep shared abstractions exactly as long as they're
shared.

**Y26 `[MC]`** (v20.3) `TaskSnapshotModel` is an abstract base. Which pattern is
it? (a) Singleton; (b) Template Method — the base fixes the algorithm, subclasses
fill two holes; (c) Observer; (d) Factory.
**A:** (b). The base owns the diff (`applySnapshot`/`resetSnapshot`); each
subclass supplies only where the snapshot comes from and `rolesEqual()` (what
counts as a visible change).

**Y27 `[EXPLAIN THE TRADEOFF]`** (v20.3) Why extract the diff into a base instead
of copying it into `CategoryTaskModel`?
**A:** Two copies of a subtle ~50-line reconcile drift the day one gets a fix the
other never hears about — the exact "second-consumer" hazard, now for an
algorithm. One home means one place to fix, one place to test, and both models
provably share the same behaviour. The cost is a small abstraction (a base class
and a virtual); cheap next to two diverging diffs.

**Y28 `[SPOT]`** (v20.3) The two `rolesEqual()` overrides differ: Upcoming's omits
`done`, Activities' includes it. Bug or intent?
**A:** Intent. Upcoming lists only undone tasks, so completing one makes the row
*leave* — `done` never changes in place there. Activities shows and toggles
completion in place (checkbox + strikethrough), so `done` IS a painted role and
must trigger a repaint. Same base, two honest answers to "what does a visible
change look like on THIS list?"

**Y29 `[WHAT WOULD BREAK]`** (v20.3) `CategoryTaskModel::setCategoryId()` calls
`resetSnapshot`, but `refresh()` calls `applySnapshot`. What breaks if you make
`setCategoryId()` use `applySnapshot` too?
**A:** Nothing *incorrect* — the diff would still produce the right rows — but a
category swap shares almost no ids with the previous one, so the diff degrades to
"remove every old row, insert every new row": a burst of N+M structural signals
where a single reset is cleaner and cheaper. `applySnapshot` is for in-place
edits; a context swap is precisely the case `resetSnapshot` exists for.

---

## Section Z — Natural-language quick-add (v21)

*Covers `nlp::parseQuickAdd`, the ParsedTask contract, the Activities live
preview, and `diagrams/quickadd_flow.puml`. See
`design-addendum-quickadd.md`.*

**Z1 `[MC]`** What does `nlp::parseQuickAdd` take and return? (a) a QLineEdit
and an AppData, mutating both; (b) text + a `today` date, returning a plain
`ParsedTask` struct; (c) just text, reading the real clock internally; (d) a
JSON request for an LLM.
**A:** (b). Text and `today` in, struct out — no AppData, no widgets, no clock
of its own. `today` being a parameter is why every test anchors to a fixed date
and never flakes with the calendar.

**Z2 `[EXPLAIN THE TRADEOFF]`** Why is the first "AI feature" a deterministic
parser instead of a call to a language model?
**A:** The architecture is the durable part: pure parse → `ParsedTask` →
preview/commit. A deterministic core is instant, offline, free, and testable to
the last rule — and when an LLM fallback arrives (a wire client in the
ShareClient mould), it maps messy text onto the SAME struct, so nothing
downstream changes. Starting with the model would have coupled the UI to
network latency and non-determinism before the shape was even proven.

**Z3** Why does the parser return `categoryHint` as a string instead of
resolving `#school` to a category id itself?
**A:** Resolving needs the category list, i.e. AppData — and the moment the
parser reads AppData it stops being pure (harder to test, impossible to reuse
in a future global bar with different fallback rules). So the parser reports
what was *written*; `ActivitiesPage::resolveCategoryHint()` decides what it
*means* (exact case-insensitive name match, else the selected area). Same
report-vs-decide split as the delegates.

**Z4 `[SPOT]`** Typing "move friday aug 8" yields due = the soonest Friday and
the title "move aug 8". Bug or intent?
**A:** Intent — first-match-wins, per facet. Accepting later expressions would
mean silent overriding; leaving them in the title puts the surprise where the
live preview makes it visible, and the user can rephrase. One deterministic
rule beats a guess.

**Z5 `[T/F]`** On a Wednesday, "wednesday" parses to next week's Wednesday.
**A:** False — a bare weekday means the soonest such day that is
*today-or-later*, so it means today. "next wednesday" is the one that adds a
week. (Pinned by `weekdayMeansSoonestOnOrAfterToday`.)

**Z6 `[WHAT WOULD BREAK]`** The preview label and the Enter handler both call
`parseQuickAdd`. What breaks if someone "optimises" the commit to reuse a
cached ParsedTask stored by the preview?
**A:** A drift window: the cache captures `today` at *keystroke* time. Leave
the input sitting over midnight (or edit programmatically without focus) and
Enter would commit yesterday's interpretation of "today"/"friday". Re-parsing
at commit costs microseconds and guarantees what-you-see-is-what-you-get —
the whole trust contract of the preview.

**Z7** Why are numeric slash dates like "8/8" deliberately unsupported?
**A:** Locale ambiguity — Aug 8 in one convention, 8 Aug in another. A
quick-add that guesses wrong plants a task on the wrong date silently; leaving
the text in the title (visible in the preview) is honest. "aug 8", "8 aug" and
ISO cover the need unambiguously.

**Z8 `[HANDS-ON]`** Add support for "eod" (end of day = today) to the parser.
What do you touch, and what MUST you add?
**A:** One branch in the single-token date section of `parseQuickAdd`
(`if (t == "eod") { out.dueDate = today; … }`) — and a test in `test_nlp`
pinning it, because in this codebase a parsing rule without a test is a rule
that doesn't exist. Nothing else moves: the UI consumes `ParsedTask`, not the
grammar.

**Z9 `[SPOT]`** (v21.0.1) After the ordinal fix, `rent 28th` gets a due date
but `lab 28` does not — the same number, dated in one and not the other. Bug
or intent?
**A:** Intent, and the fix's most important rule. The ordinal suffix is the
user *stating* "this is a day"; that explicit intent licenses the guess. A
bare number carries no such statement, and plenty of titles contain plain
numbers ("lab 4", "chapter 7") — dating them would plant phantom deadlines.
The v21.0.1 patch itself came from a field report ("28th july" stayed TBD)
and was built failing-tests-first: `ordinalDaySuffixesParse` and
`bareOrdinalMeansSoonestDayOfMonth` failed on the old parser, then the fix
turned them green with `bareNumberIsNeverADate` guarding the boundary.

**Z10 `[MC]`** (v21.1) With no `#tag`, which category does the capture overlay
commit into? (a) always the first; (b) the one selected in Activities; (c) the
remembered default — the category last captured into (persisted), else the
first; (d) it refuses without a tag.
**A:** (c). "Capture memory": most brain-dumps go to the same bucket, so the
overlay remembers where you last threw one (QSettings, via the
`taskCaptured` signal MainWindow listens to). A stale remembered id — the
category got deleted — falls through to the first category rather than
ghost-writing; a test pins that fallback.

**Z11 `[EXPLAIN THE TRADEOFF]`** (v21.1) `#tag` resolution moved from
ActivitiesPage into `AppData::categoryIdByName()`. Why is the domain the right
home, and why exact-match only?
**A:** The second consumer (the overlay) exposed "what does this name mean?"
as a domain question — page-local copies would drift, and name→id lookup
belongs beside the data anyway. Exact, case-insensitive, no prefix/fuzzy
matching: a quick-add that guessed "Sch" → "School" would one day guess wrong
*silently*, and a wrong-category task is worse than a fallback the preview
openly shows. It returns an id (a value), not a pointer — user-text lookups
shouldn't hand back dangling-prone pointers.

**Z12 `[WHAT WOULD BREAK]`** (v21.1) The Ctrl+N shortcut is created with
`Qt::ApplicationShortcut` context. What breaks if someone "simplifies" it to
the default `Qt::WindowShortcut` — and why does commit-then-stay-open matter?
**A:** With window context the shortcut still fires while the MainWindow has
focus — so it *mostly* works, until a child dialog is up or focus sits
somewhere that swallows it; ApplicationShortcut is the "from anywhere"
guarantee that IS the feature. Commit-then-stay (clear the input, keep the
overlay, flash the receipt in the hint line) is brain-dump batching: five
thoughts, five Enters, one Esc. Closing per commit would turn a dump into
five summon-type-close cycles — friction exactly where the feature exists to
remove it.

**Z13 `[MC]`** (v21.2) The AI fallback fires: (a) on every keystroke; (b)
automatically when the deterministic parser finds no date; (c) only on an
explicit Ctrl+Enter; (d) on commit.
**A:** (c). Explicit, never automatic: no surprise network calls, no latency
on keystrokes, and the deterministic parser stays the instant default. The
event filter claims Ctrl+Enter BEFORE QLineEdit would turn it into
returnPressed — otherwise "ask the AI" would also commit.

**Z14 `[EXPLAIN THE TRADEOFF]`** (v21.2) Why is the LLM integration split into
`LlmQuickAddClient` (wire) and `nlp::llm` (pure) instead of one class?
**A:** Everything that can be WRONG about an LLM reply — hallucinated fields,
non-ISO dates, markdown fences, prose instead of JSON — is a MEANING problem,
and meaning is testable as a pure function fed forged payloads (microseconds,
no network, no key). The wire then only answers "did bytes arrive?" and is too
thin to hide bugs in. Same doctrine as ShareClient/stats:: — new remote, same
split. Bonus: the mapper degrades garbage fields to the deterministic
parser's own defaults, so an imperfect AI answer is never worse than none.

**Z15 `[WHAT WOULD BREAK]`** (v21.2) An AI reply "arms" `m_aiParse`, and ANY
edit to the input clears it. Remove that clearing — what breaks?
**A:** The preview's trust contract. The model answered the OLD text; if you
type " tomorrow" after the reply lands and press Enter, commit would use the
stale AI interpretation while the live preview shows the deterministic parse
of the NEW text — commit and preview disagree, the exact drift the shared-
function design forbids. `captureOverlayEditDisarmsAiParse` pins it. (Its
sibling guard: the client's generation counter drops replies that arrive
after a newer request — an answer to a question you're no longer asking.)

**Z16 `[SPOT]`** (v21.2) `LlmQuickAddClient::configuredKey()` is called inside
every `parse()` instead of caching the key in the constructor. Waste or
intent?
**A:** Intent — pref-read-at-fire-time, the Pomodoro notification doctrine.
Cache the key at construction and a user who pastes their key into Settings
mid-session keeps hitting "no API key" until restart, the most confusing
possible failure. Reading QSettings twice per request costs microseconds;
a stale credential costs a support conversation. (Same reason the click-away
dismissal uses WindowDeactivate rather than modality: `setModal(true)` would
swallow the outside click that IS the gesture.)

---

## Section AA — "Needs a block", complete: domain (v21.3), gated panel (v21.4), placement (v21.5)

*Covers `coverage::` (TaskCoverage.h), `ReturnPolicy`, the new Task fields,
the AppData doors/queries, format v10, and
`diagrams/needs_block_rule.puml`. See `design-addendum-needs-a-block.md`.*

**AA1 `[MC]`** A task is due Wednesday and has one block, on Thursday. Under
the coverage rule it is: (a) covered — a block exists; (b) covered — Thursday
is within a grace day; (c) **not** covered — the block is past the deadline;
(d) covered only if the block is 2h+.
**A:** (c). Coverage requires the block's date in `[today, deadline]`. A block
after the deadline *looks* handled and isn't — the owner's explicit "this
needs to be caught". The week view (part 3) will paint exactly this block
red-outlined.

**AA2 `[EXPLAIN THE TRADEOFF]`** Why does `deadlineOf` return
`max(dueDate, today)` instead of just `dueDate`?
**A:** Satisfiability. Without the clamp, a task due last week could never be
covered by any *placeable* block (you can't plan the past), so it would nag
forever with no action that resolves it — the worst failure mode for an app
built around anxiety-driven procrastination. With the clamp, blocking time
*today* covers an overdue task: the nag always has an exit.

**AA3 `[T/F]`** A block that already happened still counts as coverage, since
time *was* set aside.
**A:** False. `isCovered` skips dates before today: that time came and went
and the task is still open, so it didn't do the job — the task needs *new*
time. `uncoveredReason` reports this case as `BlockInPast` ("time was set
aside Monday — it didn't happen") so the flag can explain itself.

**AA4 `[MC]`** Where does the "does this need a block?" rule live? (a)
GlancePanel, since that's where it shows; (b) a pure function in
TaskCoverage.h, called through one AppData query; (c) duplicated per view for
flexibility; (d) the server.
**A:** (b). `coverage::needsBlock` is a pure function of (task, covered?,
rule, now); `AppData::tasksNeedingBlock` is THE derived list every surface
renders. Same reasoning that birthed `eventLabel()`: two screens each
computing their own version of a rule is how the versions start to disagree.

**AA5 `[EXPLAIN]`** The flag rule is two settings OR'd — a priority set and a
due window — rather than three presets. What does a user gain, and which
tasks can only ever be caught by the priority half?
**A:** Every combination exists without the app shipping a preset per
combination: "only ever urgent" = window Off; "everything this week" = no
priorities + window 7. Tasks with **no due date** can only be caught by the
priority rule — the window has nothing to measure. (Overdue is special: it
always counts, window or no.)

**AA6 `[HANDS-ON / SPOT THE BUG]`** A teammate "optimises" `needsBlock` by
removing the `dismissedUntil > now` comparison, arguing `expireDismissals`
already clears lapsed timestamps. What breaks, and when?
**A:** A task dismissed until 21:00 stays hidden *past* 21:00 until the next
housekeeping pass runs (startup/midnight) — precisely the evening-planning
moment the 21:00 default exists to serve. The comparison in `needsBlock` is
the load-bearing check; `expireDismissals` is tidiness. Correctness must
never depend on housekeeping having run.

**AA7 `[MC]`** `ReturnPolicy` exists because: (a) QTimer can't fire daily;
(b) two features — review re-arm and dismissal return — ask the same "when
does this come back?" and would otherwise grow two drifting implementations;
(c) Qt requires policies as structs; (d) it will be needed for sync.
**A:** (b). One mode+parameter value type, one pure `nextReturn(from)`, used
twice. The second-consumer rule usually fires on the second caller; here both
callers arrived on day one.

**AA8 `[EDGE CASE]`** You dismiss a task at exactly 21:00:00 with the policy
"until 21:00". When does it return?
**A:** Tomorrow at 21:00. `nextReturn` treats a time equal to `from` as
already passed (`today > from`, strict). The alternative — instant return —
would make the button a no-op at one exact second of the day; the boundary is
pinned by `returnPolicyComputesAllThreeModes`.

**AA9 `[CLASSIFY]`** Sort these into data.json (synced) vs QSettings
(per-device): `dismissedUntil` · the 21:00 dismissal policy ·
`dismissCount` · "have I reviewed today".
**A:** data.json: `dismissedUntil`, `dismissCount` — facts about the task;
dismissing on the laptop should hold on the phone, and the escalation
evidence travels with the task. QSettings: both policies (this machine's
rhythm) and the gate memory — if "have I looked" synced, opening the phone at
noon would skip the review because the laptop looked at 07:00. The pause
belongs to each device you plan from.

**AA10 `[EXPLAIN]`** The escalation *rung* is derived from `dismissCount` on
every read instead of stored. What does that buy when the user changes the
threshold from 3 to 10 in Settings?
**A:** Nothing to migrate: every task re-rungs instantly, because the rung
never existed as data — only the count does. Derive-don't-store, same as
Upcoming and the stats. (Storing the rung would also let it disagree with the
count after a settings change — a second source of truth waiting to lie.)

**AA11 `[T/F]`** `clearDismissal` ("bring back") decrements `dismissCount`,
since the user changed their mind.
**A:** False. The count is history and history is append-only here like
everywhere else — un-dismissing doesn't un-happen the dismissals. What DOES
reset the evidence is **completion** (`setTaskDone(true)` zeroes the count
and clears any live dismissal — the owner's call), and a repeat-spawned
successor starts clean because it's a fresh task.

**AA13 `[MC]`** The gate's open/closed state is stored: (a) in data.json, so
it syncs; (b) as a bool in QSettings; (c) nowhere — it is derived every
refresh as `reviewPolicy.nextReturn(lastReview) > now`; (d) in the card as a
member set by the button.
**A:** (c). Only `lastReview` (a per-device QSettings timestamp) is stored;
the openness is recomputed from it and the review policy on every refresh.
ReturnPolicy's **third** consumer, with zero new code: "when would the review
come back after that look? still ahead → the look still counts." Daily-06:00
+ reviewed 07:00 → open until tomorrow 06:00; never reviewed → closed.
Derive-don't-store, applied to UI state — pinned by
`needsBlockGateRearmsOnTheReviewClock`.

**AA14 `[SPOT THE BUG]`** A teammate simplifies `NeedsBlockCard::rebuild` to
`delete` old row widgets instead of `deleteLater`. The suite goes red — or
worse, crashes. Trace the exact death.
**A:** Dismiss click → `dismissRequested` → PlannerPage → `dismissTask` →
`changed()` (a DIRECT connection) → `GlancePanel::refresh` → `rebuild` →
delete the row **containing the very button whose clicked() handler is still
executing** → control unwinds into freed memory. Letter-for-letter the crash
that founded test_ui.cpp (the Activities Enter-to-add bug); the cure is the
same `deleteLater`. `escalatedRowDemandsADecision` walks this path on
purpose.

**AA15 `[EXPLAIN]`** `GlancePanel` holds a `const AppData*`, yet the panel is
where dismissing, deadline edits, and priority drops all *start*. How do
those mutations happen without the panel being able to make them?
**A:** They don't happen there — the card emits signals, the panel forwards
them verbatim, and `PlannerPage` (holder of the mutable pointer) performs
each one through an existing door: `dismissTask` with `until` computed from
the prefs clock at fire time, `DueDateDialog` → `setTaskDueDate`,
`setTaskPriority`. Const-correctness as architecture: the wrong call from
the panel doesn't compile, so "widget reports, page decides" is enforced by
the type system, not by discipline.

**AA16 `[T/F]`** Clicking "Show my day" dismisses the listed tasks — that's
why the numbers appear.
**A:** False, and load-bearing (§E): the button writes only the per-device
`lastReview` timestamp; every task survives into the strip, and the domain
test-visible fact (`dismissedUntil` stays invalid) is pinned by
`needsBlockGateHoldsTheNumbersUntilReviewed`. If clearing the list were the
price of the numbers, the cheapest coin would be "Not today" ×N — a machine
that trains the habit the app exists to fight.

**AA18 `[EXPLAIN]`** "Find time" works from the week view, yet part 3 added
no placement code to `WeekAgendaView` at all. How?
**A:** The interception sits at the top of `planAt` — the *single* planning
step that both the day agenda and every week column were already routed
through (a decision made sessions earlier, for a different reason). One
choke point means one interception covers every surface that plans, present
and future. `weekViewSharesTheListAndTheOnePlanningStep` proves it by
placing on tomorrow's date exactly as a week-column click would deliver it.

**AA19 `[SPOT THE DESIGN CHOICE]`** In the placing strip, a day with no free
hour is disabled-but-visible, and a day past the deadline is red-but-
*clickable*. Defend both against "just hide them".
**A:** Hidden days make the strip lie by omission — "Wednesday has 18h free"
is real information even when Wednesday is past the wire (it feeds the
"maybe the deadline is wrong" decision the escalation menu exists for). The
full day communicates "not here, and here's why" at a glance. The late day
stays clickable because the DOMAIN permits a late block — coverage just
won't count it, and both the banner warning and the why-line say so.
Refusing a legal operation would be paternalism; explaining it is §A's
whole contract. (A deliberate softening of the prototype's hard refusal —
recorded in the addendum, not slipped in.)

**AA20 `[HANDS-ON]`** The visual bug in v21.4's card (the smeared title) was
caused by `delete`-ing a nested `QHBoxLayout` during rebuild. Why did the
title and badge survive as ghosts, and what is the correct teardown?
**A:** Widgets are children of the *card*, not of the layout that positions
them — deleting a `QLayout` deletes geometry management, never the widgets.
So each rebuild orphaned the header's title + count badge, which kept
painting at stale positions under every fresh pass (screenshots:
`needsblock-bug-reproduced.png` / `-fix-verified.png`). Correct teardown
recurses: for each `QLayoutItem`, `deleteLater` its widget OR descend into
its child layout, then free the item shells (`deleteLayoutTree`). The
`deleteLater` half is the old mid-signal rule; the *recursion* is the new
lesson.

---

*Add to this as we build. Every new feature should be able to contribute at
least one `[MC]`/`[T/F]` and one `[HANDS-ON]` question — if it can't, you may not
yet understand it well enough to teach it.*

---

## V. v22 — deadline times and the four UI fixes

**V1 `[MC]`** Tasks needed a time on their deadline. Why was `QDate dueDate`
not simply replaced with a `QDateTime`?
**A:** Because a `QDateTime` **cannot say "due Aug 8, no particular time."** It
would have to invent a stand-in (midnight? 23:59?), and that invented value
leaks into every sort, overdue test and coverage check — the model would be
able to state something false. Two fields, each able to be absent
(`QDate dueDate` + `QTime dueTime`), can say every truth the domain has. Same
"make illegal states unrepresentable" instinct as the `Repeat` enum.

**V2 `[T/F]`** `dueTime` needed a companion `bool hasTime` flag.
**A:** **False**, and for the same reason `dueDate` never needed one: an
invalid `QTime` *is* the "all day" state. A parallel bool is a second source of
truth waiting to disagree with the first (§3.11).

**V3 `[HANDS-ON]`** A time with no date is meaningless. Where is that
invariant enforced, and why there?
**A:** At the three **domain doors** — `addTask`, `setTaskDueDate`,
`updateTask` — each of which does `dueTime = dueDate.isValid() ? dueTime : QTime()`.
There, because it is the only place every write passes through: no widget, no
parser and no sync path can compose the illegal state, so no later reader ever
has to ask what it would mean. The UI *also* greys the time editor, but that is
courtesy, not the guarantee.

**V4 `[MC]`** `Task::dueMoment()` returns `23:59:59` for an all-day task, not
`00:00`. Why does that choice matter more than it looks?
**A:** It is the difference between "due Aug 8" being late at 00:01 on Aug 8
and being late on Aug 9 — the most likely off-by-a-day bug in the feature. It
is stated once, in one function, and pinned by a test
(`allDayDeadlineIsEndOfDay`).

**V5 `[T/F]`** `isOverdue(QDate)` should have been changed to consider the
time, rather than adding an `isOverdue(QDateTime)` overload.
**A:** **False.** Callers that legitimately reason in whole days (the calendar
strip, buckets, sorting) are *correct* as they are; silently sharpening their
rule would change answers all over the app. An overload lets the argument type
pick the rule while both call sites read identically.

**V6 `[HANDS-ON]`** The quick-add parser reads `at 5` as 05:00 but leaves the
`4` in `lab 4` alone. What is the general principle, and where else does it
already appear?
**A:** **The parser only guesses when the user signalled intent.** "at" is the
user saying "this is a clock", exactly as the `th` in `28th` says "this is a
day" (a bare `28` is not a date either). Half a student's tasks contain plain
numbers; a parser that grabs them is worse than one that leaves text alone.

**V7 `[MC]`** `midnight` parses to 23:59. Justify or attack it.
**A:** Justify: this is a **deadline** parser. "Due midnight Friday"
universally means the end of Friday; parsing 00:00 would make the task
twenty-four hours late the instant it was typed. Attack: it is literally wrong
as a time-of-day, and a user typing `midnight` for a *reminder* would be
surprised — the honest answer is that the surrounding domain (deadlines) picks
the reading, which is why the decision is documented rather than obvious.

**V8 `[T/F]`** Adding `"dueTime"` to the JSON required a file-format version
bump and a migration branch.
**A:** **False.** An invalid `QTime` serialises to `""` and a *missing* key
reads back as `""` too — so a v21 file loads as all-day with zero
special-casing. Fifth application of the same additive-growth trick
(`taskId` v6, distracted v5, notes/repeat v4, dismissals v10). Sync and sharing
reuse `JsonStore`'s converters, so the field syncs for free.

**V9 `[HANDS-ON]`** The needs-a-block card could make the whole window
unresizable. Trace the mechanism.
**A:** Rows were appended straight into a `QVBoxLayout`. A layout's
`minimumSizeHint` is the **sum of its children**; that propagates to
`GlancePanel`, then to `QMainWindow`, and Qt refuses to shrink a window below
its minimum. Twenty due tasks therefore took the window hostage. Not a Qt
quirk — a layout asking for enough room is correct behaviour; the bug is
*unbounded content* being asked that question.

**V10 `[MC]`** v22 bounded the needs-block card with a fixed 280px scroll
area; v22.1 removed the ceiling. What survived, what died, and why?
**A:** The **QScrollArea survived** — its small, *constant* minimum is the
actual cure for the frozen window (the v21 bug was the layout's minimum
climbing to the QMainWindow). The **fixed ceiling and the "+N more" fold
died**: the card shipped squashed over an empty panel, because it was still
trying to size *itself*. v22.1 moves the decision up — `GlancePanel` hands the
card the layout stretch when the gate closes and hands it back to the day
content when it opens. Stretch factors **claim** space; size hints only
**suggest** it.

**V11 `[HANDS-ON]`** Two `QScrollArea` sizeHint traps bit this card. Name
both.
**A:** (1) The default `AdjustIgnored` policy answers `sizeHint()` with a
**hardcoded 256×192** — a two-line strip would reserve a fist of empty space;
`AdjustToContents` is needed for the strip mode to size honestly. (2) Even
then the hint is **cached**, re-read only on a `LayoutRequest`, which a
`deleteLater`-heavy rebuild can defer past the next paint — the squashed card
in the owner's screenshot. v22.1's answer is both an explicit
`updateGeometry()` nudge after each rebuild *and* not depending on the hint at
all in gate mode (the panel's stretch decides the height there).

**V12 `[MC]`** v22 folded the gate list behind "+N more"; v22.1 shows every
row and lets it scroll. What changed the answer?
**A:** The constraint moved. The fold existed to protect a fixed 280px
ceiling; once the card owns the panel's height, that ceiling is gone and the
fold was hiding tasks to protect nothing. Principle: **a bound should live
where the constraint lives** (space, via the scroll bar) — duplicating it as
UI state (an expand button) adds a click and a piece of state that can go
stale. The owner's own words settled it: "so small, can't see any task."

**V13 `[MC]`** `QPushButton` has no `rightClicked` signal. How did the view
switcher get a second gesture without subclassing?
**A:** `setContextMenuPolicy(Qt::CustomContextMenu)` turns secondary clicks
into the plain `customContextMenuRequested` signal, which can be wired
anywhere. Side benefit: it also fires for the keyboard Menu key and for
long-press on touch.

**V14 `[HANDS-ON]`** Adding `goToToday()` forced `applyDate()` to be
extracted. What class of bug does that extraction prevent?
**A:** The "new gesture forgets a step" bug. `shiftPeriod()` pushed the date
into five sub-views *and* rebuilt the due strip inline; a second navigator
would have duplicated that list and one day dropped a line. With one "the date
moved" routine, `goToToday()` cost zero new update logic.

**V15 `[T/F]`** `goToToday()` should also switch back to Day view.
**A:** **False** (design call). You asked to come home, not to be moved to a
different room. A gesture that means one thing in every view is safe to reach
for without looking; one that also changes the view is a surprise you have to
undo.

**V16 `[MC]`** v22 added Overdue/This-week/Later count chips to the Upcoming
page; v22.1 removed them (and the proxy bucket lens serving them). What is the
lesson in each direction?
**A:** Building them: a summary must read the **source** model, never the
proxy — numbers that shift as you filter are a tautology, not a landmark
(still the right rule wherever you *do* build a summary). Removing them: the
owner asked for a *bigger* screen and got more *controls*; "too small" is
fixed with size, and extra chrome makes a screen read as busier and smaller.
Features are hypotheses — the owner's review is the experiment result — and
the lens was deleted along with the chips because machinery orphaned by a
removed feature is how codebases rot.

**V17 `[T/F]`** `QToolButton::autoExclusive` groups buttons by parent
widget.
**A:** **True**, and it is a trap worth carrying even though the chip bar
that hit it is gone: put two independent button rows on one parent and all of
them silently become a single radio group — picking in one row un-picks the
other. The escape hatches are a container widget per row, or `QButtonGroup`.
Related trap: `autoExclusive` refuses to *un-check* the active button, so
"click again to clear" always needs hand-rolled exclusivity.

**V18 `[T/F]`** Flipping `prefs::pomodoroDrivesTracker()` to default `true`
overrides users who had deliberately turned the link off.
**A:** **False.** `QSettings` consults a default **only when the key is
absent**. New installs and never-touched-it users get the link on; anyone who
made an explicit choice keeps it. That asymmetry is why a default change needs
no migration code.

**V19 `[MC]`** `PomodoroLink::m_enabled` still starts `false` even though the
shipped default is now `true`. Bug or design?
**A:** **Design.** The link never reads `QSettings` — pages read, widgets are
told. The default lives in `prefs::`; the link's job is to obey. Letting it
read preferences would put a second reader of the same key in the codebase.

**V20 `[HANDS-ON]`** Both card delegates format their due text through a
single function that the geometry pass and the paint pass both call. What
breaks if you write two?
**A:** The geometry pass reserves width by **measuring the string**; the paint
pass draws it. Two formatters drift, and the first long value (`Aug 8 · 23:59`
where `Aug 8` was measured) overruns the delete button. One layout truth, two
readers — the same DRY-the-geometry rule `TaskCardDelegate::RowGeom` already
follows.

**V21 `[MC]`** GlancePanel used to end its layout with `addStretch(1)`;
v22.1 removed it. Where did the slack go, and why is that better?
**A:** The slack moved **inside** the sections: a trailing stretch inside the
day content keeps its stats top-packed, and `refresh()` assigns the
panel-level stretch factor to whichever section is on stage (card when the
gate is closed, day content otherwise). A fixed trailing stretch is a third
claimant that competes with whoever should own the room — with it in place,
giving the card stretch 1 would have split the free space 50/50 with the
stretch item, leaving half the panel blank.

**V22 `[HANDS-ON]`** The v22.1 type bump touched `setPixelSize` in *pairs* —
once in `geometryFor()`, once in `paint()`. What breaks if you bump only one?
**A:** `geometryFor()` **measures** the countdown/chip strings with its font
to reserve width; `paint()` **draws** them with its own. Bump only the paint
font and the drawn text overruns the reserved rect — straight into the ×
delete button, whose hit-zone then overlaps text. Same DRY-the-geometry rule
as `RowGeom` itself: one layout truth, and every reader of it uses the same
numbers. (The card height `kCardH` is part of the same pact: bigger fonts
without a taller card puts the two text lines against the card edges.)

**V23 `[HANDS-ON]`** "Show my day" stopped responding to real clicks while a
timer was running, yet `open->click()` in the test suite passed. Explain both
halves.
**A:** The glance panel refreshes every second while tracking, and the card
rebuilt unconditionally — destroying and recreating every widget, including
the button mid-click. A real click is press → ~100ms → release, delivered to
the widget that took the press; a rebuild inside that window means the
release hits a `deleteLater()`'d corpse and `clicked()` never fires. The test
passed because `click()` invokes the slot directly — it proves the *handler*,
not the *reachability*. Corollary: never destroy interactive widgets on a
timer unless something they show actually changed.

**V24 `[MC]`** The v22.2 fix caches a fingerprint. Doesn't that violate the
card's "derive, don't store — nothing can be stale" contract?
**A:** No, and the distinction is the lesson: the derivations still run on
every refresh (the fingerprint IS a fresh derivation), so the *data* can never
be stale. What's cached is the **rendering decision** — "would this rebuild
produce identical pixels?" Same pact as `TaskListModel::rolesEqual`: skip the
repaint for an invisible change. The cost is a maintenance pact — every new
visible field in `makeTaskRow` must join `fingerprint()` or its edits stop
repainting — which is why the pact is written at the top of the function.

**V25 `[T/F]`** Flipping a QSettings *default* (like `pomodoroDrivesTracker`
in v22) is a low-risk change because it only affects fresh installs.
**A:** **False**, twice over. It affects everyone who never touched the key,
not just fresh installs — and, the v22.2 lesson, it changes *how often other
systems run*: the link now drives the tracker, the tracker's tick drives a
per-second UI refresh, and that refresh exposed a dormant click-eating rebuild
in a card three modules away. Defaults have blast radius; when one flips, ask
"what runs more often now, and what does that path assume?"

**V26 `[MC]`** The regression test holds the button in a `QPointer` and
asserts it after five refreshes. Why a QPointer rather than calling
`findChild` again?
**A:** `findChild` would happily return a *new* button with the same
objectName — a rebuilt card passes that check while still exhibiting the bug.
`QPointer` nulls itself the instant its widget is destroyed, so its survival
asserts **object identity across refreshes**, which is the actual property the
fix guarantees.

**V27 `[MC]`** The gate shows one task instead of the full list. What makes
that defensible rather than "hiding information"?
**A:** The list is **ranked** (`coverage::rankAt`: pinned → overdue → urgent →
rest), so "the top one" is a real answer; and the full list is one click away
in the counter. The design argument: a list of five overdue things is a status
report, one thing with two buttons is a decision — and this app exists for
someone who stalls in front of status reports. Note the dependency: focus mode
borrows its credibility from the ranking. If the order were arbitrary, showing
one would be a lie.

**V28 `[T/F]`** The `1 of 5 ▾` counter is the v22 `+N more` fold coming back,
so v22.1 was wrong to delete it.
**A:** **False** — same widget, opposite motivation. v22's fold existed to
protect a fixed 280px ceiling: a *layout* constraint duplicated as UI state,
which is why deleting it and fixing the layout was right. v22.4's counter
exists because one decision beats five — an *attention* constraint, which is
genuinely a UI concern. The lesson: a bound belongs where its constraint lives.

**V29 `[HANDS-ON]`** `makeTaskRow` grew a `bool focus` parameter instead of
being forked into `makeFocusCard`. Argue both sides, then justify the call.
**A:** Fork: simpler functions, no branching on a flag, each presentation free
to diverge. Parameter (chosen): the facts, escalation rules, decision menu and
emitted signals are *identical* — only padding, title size, dot-vs-rail and
button stretch differ. A fork means the day someone adds a field to one copy
and not the other, the two presentations start lying about the same task. Rule
of thumb: fork on different *behaviour*, parameterise on different *density*.

**V30 `[MC]`** The focus card's accent rail is red when escalated **or**
overdue, else the category colour. Why is that not decoration?
**A:** It carries the same fact the meta line spells out, arriving a beat
earlier — "how worried should I be?" answered before a word is read. It also
follows the app's one-hue-per-meaning rule (danger red is the same red the
block bars and overdue pills use). And it uses `isOverdue(QDateTime)`, the
clock-aware overload, so a 09:00 deadline reddens at 09:01 rather than at
midnight.

**V31 `[T/F]`** In focus density the category dot was dropped by accident.
**A:** **False** — the accent rail already carries the category colour, so the
dot would state it twice while costing the title its room. Related discipline:
`m_showAll` self-clears when the list drops to one, or the counter would
reappear pre-expanded the next time the list grew (state outliving its
meaning, the same trap `m_decisionFor` guards against).

**V32 `[HANDS-ON]`** The project's first real compile (v22.6) found 5 test
failures that pure code review missed across five versions. Classify them.
**A:** Two were **stale tests** broken by legitimate changes: the Pomodoro
test asserted "Link off" as the fresh-page state (the v22 default flip made
that wrong — the failure was the feature *working*), and the delegate hit-test
clicked hardcoded pixels ("click(18, 57) over the checkbox") that the v22.1
resize silently moved. Three were **synchronization**: the v22.3 deferred
`reviewed()` needs one event-loop turn (`QTest::qWait(0)`) before asserting,
and `findChildren` counts `deleteLater`'d corpses until the loop drains — one
test counted 4 buttons where a user sees 3. Meta-lesson: tests that never run
are documentation, not tests.

**V33 `[MC]`** Why did the delegate hit-test get rewritten to derive its
click targets from `sizeHint()` instead of just updating the magic numbers?
**A:** Updating the numbers fixes the symptom and re-arms the trap — the next
geometry pass breaks them again, silently. Deriving from the delegate's own
measured row height means the test asks *where things are* rather than
remembering where they once were: it now survives resizes by construction.
General rule: a test coupled to constants it doesn't own should measure, not
memorise.

**V34 `[T/F]`** `panel.findChildren<QPushButton*>()` right after a rebuild
counts what the user sees.
**A:** **False** — the old widgets are `hide()`+`deleteLater()`'d, and until
the event loop turns they are still children: hidden corpses that findChildren
happily returns. Counting after `QTest::qWait(0)` (which drains deferred
deletion) counts reality. Same event-loop awareness as the deferred-signal
waits: Qt's "later" is a real place, and tests must decide which side of it
they're asserting.

**V35 `[MC]`** `showMyDayRespondsToARealMouseClick` clicks the top-level
`QWindow` at the button's mapped position instead of calling
`open->click()`. What does each prove?
**A:** `click()` invokes the slot — it proves the *handler* works and nothing
else. The QWindow-level click runs the full delivery pipeline: hit-testing
down the widget tree, overlapping siblings, event filters, grabs. Its passing
proves no widget in this tree can eat the click — which, combined with a field
report of dead clicks, points the investigation *outside* the code: stale
binary, or platform/window-manager territory.

**V36 `[HANDS-ON]`** The "Show my day" saga's true root cause (v22.7). Walk
the chain from symptom to fix.
**A:** Symptom: label flips on press, everything snaps back on release. The
flip proved input delivery worked; the *pristine* button text afterwards
proved a rebuild had run — so the click fired and the re-derivation overruled
it. Cause: the click wrote `lastReview` to QSettings, the rebuild re-read it
and got nothing (writes vanishing on that machine — `main.cpp` set no
organization name, so QSettings resolved to an anonymous path some Windows
policies refuse), so `looked` stayed false and the gate re-closed. Fix, two
layers: name the organization (settings get a real address), and a **session
witness** — the card records in RAM the review it saw, and the gate predicate
consults both. The disk may forget; the widget that watched you click does
not.

**V37 `[MC]`** Why is the session witness a *better* fix than just retrying
the QSettings write?
**A:** Retrying assumes the failure is transient; a policy-blocked registry
path fails every time. The witness changes the *dependency structure*: opening
the gate this session no longer requires storage to work at all — persistence
is only needed for what persistence is actually for (tomorrow). It also
degrades honestly: with broken storage, the gate re-arms next launch, which is
the best any design could do. General principle: don't route a user's action
through a subsystem that can fail when the action's effect doesn't need it.

**V38 `[T/F]`** Tests passing everywhere while a bug reproduces 100% on one
machine means the tests are bad.
**A:** **False** — it means the failing dependency isn't exercised: every test
environment's QSettings worked, so no test could see a world where writes
vanish. The v22.7 test *simulates* that world (`remove()` the key right after
the click) rather than trying to break the store for real — pin the contract
("session review survives storage amnesia"), not the failure mechanism. Also
the diagnosis lesson: the v22.5 instrumentation's press-flip was designed to
split delivery-vs-logic in one glance, and it did exactly that.

**V39 `[MC]`** The strip accordions became chips + a slide-over drawer. What
is the design argument, stated as a principle?
**A:** An accordion mutates the page (everything below it shoves down; two
stacked make the panel a pogo stick); a drawer layers over it (nothing behind
reflows; closing restores the exact picture). **Expansion should be a
navigation, not a mutation** — especially in a panel whose job is to be a
stable glance. Qt has no built-in drawer, so `SlidePanel` constructs the
standard one: a translucent overlay child of the host + a sheet animated on
*position* (never size — that would relayout rows every frame).

**V40 `[HANDS-ON]`** The chips kept the old objectNames (`needsBlockStrip`,
`putOffStrip`) even though the mechanism changed completely. Cost and payoff?
**A:** Payoff: every existing test that asserts "the strip exists after
review" passed **unmodified** — the suite validated the refactor instead of
being churned by it — and the names still describe the *meaning* (the handle
to the folded list), which survived the mechanism. Cost: the name no longer
describes the widget's behavior, which a future reader might find odd — paid
for with a comment at the site. Rule: rename when the *meaning* changes, not
when the implementation does.

**V41 `[HANDS-ON]`** The drawer test failed twice before passing, both times
in the harness. What did each failure teach?
**A:** First: clicking "bring back" mutated nothing, because `GlancePanel`
*forwards* action signals — `PlannerPage` owns the data call, and a bare
panel has no page. Know which layer owns the mutation before asserting it.
Second: the mutation landed but the drawer never closed, because the page's
*other* job — routing `changed()` back into `refresh()` — was also missing.
A pipeline is only a pipeline when both ends are connected; a test harness
for a mid-stack widget must stand in for every wire the real stack provides.

**V42 `[MC]`** Restyling the drawer's rows to the hero look took one boolean
(`/*focus=*/true`). What earlier decision is that cheapness the return on —
and what would the price have been the other way?
**A:** v22.4's call to make density a **parameter** of `makeTaskRow` rather
than forking a `makeFocusCard`. With the parameter, "make the drawer pretty"
is a word; with a fork it would have been a second copy to restyle, drifting
from the first at every future field. Corollary applied in the same patch:
one *meaning* gets one *look* — "this task asks for a decision" now renders
identically at the gate, pinned in the strip, and inside the drawer, so the
user learns the card once.

---

## v23 — the window's own memory

**V43 `[RECALL]`** Which two keys does the window-memory arc add, and what
type is each?
**A:** `window/geometry` (a `QByteArray`, the opaque blob from
`QWidget::saveGeometry()`) and `window/sidebarVisible` (a `bool`). Both in
`QSettings`, both per-machine.

**V44 `[TRADEOFF]`** Why store geometry as an opaque blob instead of four
integers `x`, `y`, `width`, `height` — which would be human-readable in the
settings file?
**A:** The blob carries strictly more than a rectangle: which screen the
window was on, the maximized/full-screen flag, the "normal" rectangle hiding
behind a maximized window, the DPI it was laid out at, and a version tag Qt
uses to reject data it doesn't recognise. With four integers a maximized
window comes back *restored* — silently, and only for people who maximize,
which is the worst possible bug distribution because those users assume that's
just how the app works and never report it. General rule: when a framework
hands you an opaque serialization of its own state, **store the blob**;
you are not smarter than `saveGeometry()` about what `restoreGeometry()` needs.

**V45 `[SPOT-THE-ISSUE]`** Every other getter in `Prefs.h` repairs its value on
read — clamping, snapping, falling back. `windowGeometry()` does none of that.
Is that an inconsistency?
**A:** No — it's the same rule, delegated. Repair-on-read presumes the getter
can *tell* what garbage is. Here it can't: only Qt's `restoreGeometry()` can
judge the blob, and it does so by returning `false`. `MainWindow` treats that
`false` exactly the way `agendaWindow()` treats an out-of-range integer. A
repair function you cannot write correctly is worse than none, because it
looks like a guarantee.

**V46 `[WHY]`** `sidebarVisible()` takes its default as a *parameter*, unlike
every other getter in the file. Why?
**A:** The default isn't a constant — it's a function of the screen (a
phone-sized display starts folded, since 190px of rail on a 400px screen is
half the app). `Prefs.h` has no business knowing about screen sizes, so the
caller supplies it. It composes cleanly because QSettings consults a default
only when the key is **absent** — the same property that made the v22 pomodoro
default flip safe. Reads as: "whatever you chose on this machine, and on a
machine you've never chosen on, whatever suits its screen."

**V47 `[HANDS-ON]`** `restoreGeometry()` returned `true`. Name a situation
where the window is nevertheless unusable, and explain why Qt can't catch it.
**A:** The app was closed on a second monitor that has since been unplugged.
`restoreGeometry()` validates the blob's **format** — magic number, version,
field sizes — and has no opinion on your monitor layout. `true` means "these
bytes are a geometry", not "this geometry is usable". Reading the return value
as the latter *is* the bug. Hence `overlapsAnyScreen()`.

**V48 `[WHY]`** Why is `overlapsAnyScreen()` written to take a
`QList<QRect>` of screens instead of just calling `QGuiApplication::screens()`
itself?
**A:** To make the policy pure. The impure half — *today's* screens — is a
separate two-line adapter (`availableScreenRects()`), so the policy can be
tested against a two-monitor layout, a monitor-to-the-left layout with negative
coordinates, and a zero-screen machine, none of which the CI box has. Same
domain/UI split the whole project runs on, in miniature. A policy that can only
be tested on the hardware you own is a policy that is tested once.

**V49 `[TRADEOFF]`** The reachability check uses `intersects()` (any overlap)
rather than requiring the window be fully contained on a screen. Defend that,
then argue the other side.
**A:** For: a window hanging half off the right edge is a legitimate restore —
the user dragged it there and the title bar is still grabbable. Containment
would "fix" windows nobody asked to have fixed, which is its own bug report.
Against: a window overlapping by one pixel technically passes and is nearly as
unusable as one that's fully gone. A stricter rule ("the top 30px of the frame
must intersect", approximating the title bar) would catch that. The one-pixel
case is in the test suite precisely because it's the accepted edge of the
policy — documented, not overlooked.

**V50 `[MC]`** Why does the rescue path call `setWindowState(Qt::WindowNoState)`
before resizing?
**A:** The blob may have said "maximized" — but it was maximized *on the screen
that's gone*. Re-applying that flag would maximize onto whatever display we
land on: a second surprise while recovering from the first. Recovery paths
should end in the plainest possible state, not a partially-honoured version of
the state that failed.

**V51 `[EXPLAIN THE TRADEOFF]`** The sidebar is saved on every toggle; the
geometry is saved only in `closeEvent`. Why the asymmetry, and what does it
cost?
**A:** Write immediately when the **user** makes a decision; batch when the
**window manager** does. `Ctrl+B` is a rare, deliberate intent that deserves to
survive a force-quit. Dragging a window edge is a hundred `resizeEvent`s
describing one motion — saving each would mean hundreds of writes to record one
decision, and losing the last few costs nothing anyone would notice. **The
cost, stated honestly: a hard crash loses the window geometry but keeps the
sidebar choice.** That's the correct way round.

**V52 `[WHAT-WOULD-BREAK]`** `restoreWindowState()` and `saveWindowState()`
each contain an `isCompactScreen()` guard around the geometry. What breaks if
only one of them has it?
**A:** If only *restore* skips: a phone session saves a phone-shaped rectangle,
and a later desktop session reading the same settings store restores a window
the size of a phone. If only *save* skips: a desktop rectangle already in the
store gets restored onto a 5-inch display. The guards must always agree, which
is exactly why the two functions are declared adjacent in `MainWindow.h` with a
comment saying so — **a pair of functions that must move together should be
impossible to read separately.**

**V53 `[WHY]`** Why is `restoreWindowState()` called at the *end* of the
constructor rather than near the top where `resize()` happens?
**A:** Everything above it adds widgets, and adding widgets changes the
window's size hint — restoring before the chrome exists gives the layout a vote
it shouldn't have. It's also before `show()`: restoring geometry to an
already-visible window is a visible jump. Meanwhile the *default* `resize()`
stays at the top, so every early-exit path out of `restoreWindowState()` (no
memory, rejected blob, compact screen) already stands on a sane window without
having to say so.

**V54 `[SPOT-THE-ISSUE]`** The rail's visibility used to be decided in one line
at its construction site: `nav->setVisible(!isCompactScreen())`. That line was
correct. Why did it have to move?
**A:** It was one rule in one place — fine. Once the user's choice could
outlive a launch it became *two* rules that must agree: the remembered choice,
and the screen's default when there is no choice yet. Two rules in two places
drift. Both moved into `restoreWindowState()`; the construction site now just
makes the widget. **A line that was correct as a single rule can become a bug
the moment a second rule arrives beside it — nothing about the line changed,
its neighbourhood did.**

**V55 `[HANDS-ON]`** A v22 test failed during this arc that no v23 change had
touched. What was wrong, and what did the follow-up audit reveal?
**A:** `chipsOpenTheSlidePanelAndActionsFlowThrough` dismissed a task until
`QDateTime(QDate::currentDate(), QTime(23, 0))` and asserted the put-off chip
existed. The build ran at 23:55, so the dismissal was already in the past, the
task came straight back, the list was empty, and the chip was never built. It
had been failing for one hour out of every twenty-four since it was written and
had simply never been run in that hour. Fix: `currentDateTime().addSecs(3600)` —
the requirement is "this task is currently put off", so say that. **The audit
is the better lesson:** every other clock-touching test was immune because they
drive a fake clock through the `nowProvider` seam. The one that broke was the
one that skipped the seam. A seam only protects the tests that actually use it.

**V56 `[WHAT-WOULD-BREAK]`** Suppose `overlapsAnyScreen()` returned `true` for
an empty screen list instead of `false`. What happens?
**A:** On a machine reporting no screens (a headless session, or mid-teardown)
an unreachable window would be judged reachable and the rescue path skipped —
the app opens somewhere nobody can see it, in exactly the situation where
recovering is most important. `false` routes to the default rectangle, which is
always safe. Degenerate inputs should fail toward the recoverable answer, and
this one is in the suite as `noScreensMeansNothingIsReachable`.

**V57 `[RECALL]`** What did the v23 arc discover about the README's roadmap?
**A:** `[ ] Drag-and-drop into folders` was listed as outstanding but had been
fully implemented for a long time — `CategoryTree` in `ActivitiesPage.cpp` is a
working `QTreeWidget` drop surface with a custom `dropEvent` that resolves the
target folder and routes through `setCategoryFolder`. Precisely the drift the
v22 notes warned about: *audit on ship, not on memory.*

**V58 `[HANDS-ON]`** v23.0's §E tradeoff — geometry saved only in
`closeEvent` — was revised within a day. What was the field report, why was it
the *predicted* signature, and what reclassified the tradeoff from "accepted
cost" to "bug"?
**A:** Report: "Ctrl+B works; window position and maximized state don't." That
split is exactly what the design predicts when `closeEvent` never runs — the
sidebar writes on toggle, geometry only at close. Cause: Qt Creator's **Stop
button** kills the process without `closeEvent`, and that's how a developer
ends the app dozens of times a day. "A hard crash loses geometry" was the
accepted cost; "a normal workflow loses geometry" is a bug with a
justification attached. Give-away in hindsight: the manual-test instructions
opened with a ⚠️ about which button to close the app with — **when testing a
feature needs a warning about how to exit the program, the warning is the
design apologising.**

**V59 `[MC]`** The v23.1 fix debounces saves with a 1-second single-shot
`QTimer` restarted from three event handlers. Which three, and why three
rather than the obvious two?
**A:** `moveEvent`, `resizeEvent`, and `changeEvent`. Maximize, un-maximize,
and full-screen arrive as a `QEvent::WindowStateChange` in `changeEvent` — not
as a move or a resize. Handle only the obvious two and the maximized flag is
saved late or never: the exact half of the feature the field report named.
The debounce itself is one Qt fact: `QTimer::start()` on a running timer
*restarts* it.

**V60 `[WHAT-WOULD-BREAK]`** Remove the `m_windowStateRestored` gate from
`scheduleWindowStateSave()`. What happens, and when?
**A:** On every startup: the constructor's chrome-building and
`restoreWindowState()` itself fire move/resize events describing the *default*
rectangle, a save gets scheduled, and one second after launch the stored blob
is overwritten with it — the memory erases itself, and the feature appears to
"work once then forget". The flag is set after the `restoreWindowState()`
call, not inside it, because that function has three early returns and a flag
that must be set on every exit path belongs after the call, not copy-pasted
before each `return`. Pinned by `startupDoesNotOverwriteTheStoredGeometry`,
which asserts the blob is byte-identical after a fresh construction.

**V61 `[TRADEOFF]`** After v23.1, what is `closeEvent` still *for* — and what
is the corrected rule about it?
**A:** Still the right place for shutdown *work*: committing the live tracking
interval, the final belt-and-braces save. What it no longer is: the *only*
copy of anything. Corrected rule: write immediately on intent, **debounce on
motion**, and treat write-on-close as a courtesy pass, never a load-bearing
hook — it silently doesn't run under Stop buttons, crashes, kills, and
debugger detaches, which between them cover most of how a developer actually
exits an app.

**V62 `[SPOT-THE-ISSUE]`** The v23.0 session notes, README, and question bank
all said "v23.0.0" — yet `Version.h` still read `22.0.0` when v23.1 started.
Why is that ironic, and what's the guard?
**A:** `Version.h` exists *because* version numbers drifted across four copies
(its own header comment tells the story) — and the v23.0 session updated every
prose copy while missing the single source of truth itself. A single source of
truth only works if it's in the ship checklist; otherwise it's just one more
copy that can drift. Same session also found the README claiming 166 tests
(actual: 207) and a roadmap item done for eleven versions. Three instances,
one lesson: **audit on ship, not on memory — and the audit must include the
file whose job is preventing the audit.**
**V63 `[TRADEOFF]`** The v24 provider layer is a value struct plus free
functions switching on a closed `Dialect` enum — not an
`AnthropicProvider : ProviderStrategy` hierarchy. Make the case for the choice,
and name the condition under which it should be reversed.
**A:** The dialect varies in **data** (path, header name, JSON shape), not in
stateful behaviour — there is nothing for an object to hold between calls. The
enum+switch form gets `-Wswitch` as a free registry (add a Dialect, the
compiler names every site that must learn it) and stays pure, so every request
is asserted offline in `test_nlp`. A hierarchy would add a factory, ownership
and virtual dispatch to say the same four lines. Reverse it when a dialect
must hold state **across** calls — a streaming cursor, a multi-turn tool-call
transcript — which the chat-loop step may bring. Addendum §C records this so
the future session doesn't have to re-litigate it.

**V64 `[MC]`** What is the ONE structural difference between the Anthropic and
OpenAI request bodies, and the two differences in everything around the body?
**A:** Body: the system prompt is a **top-level `system` field** for Anthropic
but the **first message** in the `messages` array for OpenAI. Around it: the
path (`/v1/messages` vs `/v1/chat/completions`) and the auth header
(`x-api-key` vs `Authorization: Bearer`). Reply envelopes differ too
(`content[].text` vs `choices[0].message.content`). That four-row table is the
*entire* vendor delta — which is why "dialect" is data, not behaviour (V63).

**V65 `[SPOT-THE-ISSUE]`** `ai::endpoint()` builds the URL as
`QUrl(base + path)` instead of the idiomatic `baseUrl.resolved(QUrl(path))`.
Someone "fixes" it back to `resolved()`. What breaks, and why is the broken
behaviour technically correct?
**A:** Groq. Its OpenAI-compatible API is mounted at
`https://api.groq.com/openai`, and RFC 3986 reference resolution — which
`resolved()` implements — says an **absolute path reference replaces the
base's entire path**: the `/openai` prefix vanishes and every request 404s.
Correct per spec (`resolved()` answers "where does this link go from this
page", a browser's question), wrong for our intent ("append the dialect path
to the vendor's mount point"). `endpointJoinsRatherThanResolves` pins it,
including the pasted-trailing-slash case.

**V66 `[WHAT-WOULD-BREAK]`** The per-provider settings keys are
`ai/keys/<id>` and `ai/models/<id>` — plural. Rename them to the singular
`ai/key/<id>` / `ai/model/<id>` on a machine that ran v21. What happens?
**A:** The migrated model silently vanishes. QSettings uses `/` as a group
separator and forbids a name being both a value and a group; v21 stored
`ai/model` as a **value**, so writing `ai/model/anthropic` asks for a group
where a value lives, and the write is dropped with no error. This actually
happened — the migration test failed on its first run, which is how the rule
was learned. Second project instance of QSettings failing *silently* (first:
the anonymous settings path, v22.7). Prevention: round-trip tests through real
storage whenever a key scheme changes shape.

**V67 `[MC]`** Why must the legacy `ai/anthropicApiKey` be **removed before**
the new `ai/keys/anthropic` is written in `migrateLegacySettings()`, rather
than after?
**A:** Defence against the V66 class of bug: if a legacy value's name were
ever a prefix of a new key, writing first would be silently swallowed by the
value-vs-group conflict. Removing first guarantees the namespace is clear.
The migration is also idempotent (a second run is a no-op) and never
overwrites a non-empty destination — so it can't resurrect a key the user
cleared, and a v24→v24 relaunch changes nothing.

**V68 `[T/F]`** For a provider with no key (Ollama), the client should still
send `Authorization: Bearer ` with an empty credential, so the header shape
stays uniform across dialects.
**A:** **False.** Some local servers reject a malformed Authorization header
outright; no key means **no auth header at all** — absence described by
absence. Same reasoning family as "a time with no date is dropped, not stored
as an orphan": don't send a shape that asserts something you don't have.
`aKeylessProviderSendsNoAuthHeader` pins it, and `needsKey=false` is also why
the "no API key set" refusal became per-provider — a global key check would
have blocked the local path entirely.

**V69 `[HANDS-ON]`** Walk the Settings AI section's stash/load buffer: the
user types an Anthropic key, switches the combo to OpenAI, types a second key,
switches back, presses OK. What happens at each step, and why are stash and
load two functions in a fixed order?
**A:** Switching away from Anthropic first **stashes** the typed key into
`m_aiKeys["anthropic"]` (it belongs to the provider being LEFT), then
**loads** OpenAI's values into the fields; the second key stashes under
"openai" on the switch back; OK writes **every provider touched**, not just
the one on screen — then the provider id itself. Fuse stash+load into one step
or swap the order and the Anthropic key is attributed to "openai". QSettings
is untouched until OK (Cancel-writes-nothing survives any number of
switches); the buffer uses `contains()` not `isEmpty()` so deliberately
clearing a key survives a round trip through another provider.
`settingsKeepsAKeyPerProvider` drives the whole dance through the real OK
button.

**V70 `[MC]`** `nlp::llm::parseApiReply` gained a `dialect` parameter in v24 —
but fence-stripping and the defensive field mapping did NOT move into the
provider layer. Where is the line, and what test would fail if it slipped?
**A:** The provider layer owns the **vendor envelope** (`ai::extractText`:
`content[].text` vs `choices[0].message.content`); everything after the
unwrap — ```json fences, the JSON parse, degrade-don't-fail mapping — is
identical for every vendor and stays in `nlp::llm`.
`fenceStrippingIsDialectIndependent` proves a fenced reply in an *OpenAI*
envelope still parses — it fails the moment fence logic gets duplicated into
a dialect branch. The parameter defaults to `Anthropic` so every pre-v24 call
site keeps compiling and meaning what it meant.

**V71 `[WHAT-WOULD-BREAK]`** Delete the `static_assert` (and its constexpr
parser) from `Version.h`. What failure mode returns, and why is checking used
instead of *generating* the string from the numeric macros?
**A:** The v23.0 failure returns: the file holds one fact in two forms (three
macros for the resource compiler, one string for the code), and they drifted —
the anti-drift file drifting internally. The guard makes disagreement a
**compile error** naming both forms. Generation via preprocessor stringify was
rejected because the `.rc` files consume `TICKTIMER_VERSION_STRING` as a
single token in a `VALUE` statement, and resource compilers are unreliable
about built-up string literals there — checking is portable, generating isn't.
Discipline was the wrong tool; the compiler is the right one.

**V72 `[SPOT-THE-ISSUE]`** The v21.2 Anthropic unwrap read `content[0].text`.
v24's `extractText` walks the array to the first block with
`type == "text"`. What real reply shape does the old code mishandle?
**A:** A reply that *leads* with a non-text block — e.g. a thinking block —
puts text later in the array; indexing `[0]` reads an empty string and the
whole reply is reported as "AI reply had no text content" despite being
perfectly good. `anthropicUnwrapWalksToTheFirstTextBlock` pins the walk.
General shape of the lesson: an envelope is a *list* by contract; treating it
as a scalar because today's replies happen to be one block long is a latent
bug with a countdown attached.

---

## v25 — the assistant (chat panel)

**V73 `[RECALL]`** The v25 feature adds *two* new pure layers. Name them, say
which test suite each lives in, and state the one rule that keeps them apart.
**A:** `brief::` (the day briefing) is **domain-pure** — it reads `AppData`
and `stats::` — so it is tested in `test_domain`. `chat::` (transcript,
window, system prompt) is **Core-pure** — it knows vendors' shapes via `ai::`
but nothing about tasks — so it lives in `test_nlp`. The rule: *`brief::`
knows tasks but no vendors; `chat::` knows vendors but no tasks.* They meet
only in `ChatPage`, as one string handed to another, and the **build**
enforces it: `ChatSession.cpp` compiles in the Core-only `test_nlp` target,
which cannot see the domain's dependencies.

**V74 `[TRADEOFF]`** Why does the chat log use one `QLabel` per turn instead
of the `QAbstractListModel` + delegate machinery the v20 arc built — and what
would the model/view version have cost here?
**A:** Model/view buys virtualisation (irrelevant for tens of rows), multiple
synchronised views (there is one), and cheap in-place edits (a sent message
never changes). Its cost lands exactly where chat hurts: `sizeHint`
arithmetic for word-wrapped prose in a delegate — the fiddliest paint code in
Qt. An append-only log of variable-height rows is the textbook case for
plain widgets in a `QVBoxLayout`. The v20 arc taught what model/view is
*for*; declining it here is the same lesson finishing.

**V75 `[WHAT-WOULD-BREAK]`** Delete the `localOnly` flag from `chat::Turn`
and send every logged turn. What goes wrong, and which test turns red?
**A:** Error notices ("⚠ couldn't reach the AI service") become part of the
history the model receives, attributed to the **assistant** — the app starts
telling the model it said things it never said, and models happily build on
that. `localOnlyTurnsNeverReachTheModel` reds: it feeds a transcript with an
error turn through `window()` and asserts no ⚠ survives. The general lesson:
*the log is a superset of the conversation* — display and transmission are
different questions, so they get different flags.

**V76 `[MC]`** `Transcript::window()` trims old turns to a **character**
budget. Why characters and not tokens, and why is the newest turn exempt
from the budget entirely?
**A:** A real tokenizer is vendor-specific — bundling one per provider would
weld vendors back into the layer the provider work just cleaned, and make it
impure. Characters/4 approximates tokens; the point is that cost is
**bounded**, not that the bound is exact — an honest approximation beats a
silent wrong one. The newest turn always ships (`oversizedNewestTurnIsStillSent`)
because refusing to transmit what the person *just typed* is not a saving;
if it truly exceeds the provider's limit, the provider says so, which is a
better failure than an app that silently won't speak.

**V77 `[DESIGN]`** The provider addendum §C promised a re-examination when
multi-turn arrived: does a dialect now need strategy objects? What was the
answer, and what mechanism keeps the one-shot and chat bodies from drifting
apart?
**A:** No — multi-turn is still **data**. The per-message shape is identical
across dialects (`{role, content}`); only the system prompt's location
differs, the same single difference the one-shot body already expressed. So
`ai::chatRequestBody` is one switch, and `requestBody` now **delegates** to
it with a one-turn list. `oneShotBodyIsAOneTurnChat` compares the two
outputs byte-for-byte — re-inline a second switch and the drift has a test
waiting. The sharpened trigger for promotion: **streaming** and tool-call
transcripts, where a dialect must hold state *across* calls.

**V78 `[SPOT-THE-ISSUE]`** The Assistant's nav button sits above Archive's,
but its page index is 6 to Archive's 5. What bug does appending the buttons
to `m_navButtons` in *visual* order create, why can't the compiler catch it,
and what is the sentence that resolves it?
**A:** `showPage(i)` does `m_navButtons[i]->setChecked(true)` — visual order
makes `showPage(5)` display the **Archive** while highlighting the
**Assistant**. Both are `QToolButton*`, so the type system is silent; only a
test (`assistantPageHighlightsItsOwnButton`, which walks both pages) can
object. The sentence: *the layout decides where a button sits;
`m_navButtons` is indexed by page identity* — two orders, deliberately
different, each documented at its site. Index 6 was appended last (not
inserted at its rail position) because `showPage(5)` is an identity old
callers already rely on.

**V79 `[RECALL]`** List the four anti-hallucination rules `brief::` encodes,
and the test that guards each.
**A:** (1) **Empty sections say so** ("nothing planned") — silence invites
invention; `briefingStatesEmptinessOutLoud`. (2) **Counts stated, cuts
visible** ("DUE TODAY (3)", "+1 more"); `briefingCapsAreStatedNotSilent`,
which shrinks `Options.maxTasks` to 2 so three tasks prove it. (3) **No
ids** — the assistant is read-only, ids are plumbing it could only misuse.
(4) **No notes/descriptions** — the most private text stays home. 3 and 4
share `briefingLeaksNoIdsAndNoDescriptions`: plant "SECRET" in a
description, assert it never appears. The briefing is the feature's privacy
page in executable form.

**V80 `[TRADEOFF]`** Why is `ChatClient` a fifth wire client instead of a
mode on `LlmQuickAddClient` — and name one thing each has that the other
doesn't.
**A:** They disagree on every axis a network call has: payload (windowed
transcript vs one line), reply (prose shown verbatim vs JSON parsed
defensively), budget (800 vs 300 tokens), timeout (60 s vs 15 s — a local 8B
model genuinely takes half a minute), cancellability (a visible Stop vs
silent supersession). Merged, that is two modes and a flag — the shape a
class takes just before becoming two classes anyway. What deserves sharing
*is* shared: both ask `ai::` for URL/body/headers/unwrap, so a dialect fix
lands once. Unique to `ChatClient`: `cancel()` + a 429 message. Unique to
quick-add's path: a pure reply-mapper (`nlp::llm`) — chat has none, because
prose *is* the answer.

**V81 `[MC]`** In `ChatClient::cancel()`, the generation counter is bumped
**before** `abort()` is called. Swap the two lines — what does the user see?
**A:** `abort()` makes the reply emit `finished()` immediately and
synchronously-ish; the handler's first act is the generation compare. With
the bump *after* abort, the handler runs while generations still match, so
the aborted request is treated as a real outcome and surfaces "couldn't
reach the AI service" — a spurious error for something the user themselves
just stopped. Bump-first turns cancellation into what it should be:
**silence**. Same stale-guard idea as quick-add's, extended to a
user-initiated staleness.

**V82 `[DESIGN]`** The chat system prompt is deliberately NOT wrapped in
`tr()`, yet the assistant still answers French users in French. Reconcile.
**A:** The prompt is a **machine contract**, not UI copy: translating it
would change the model's instructions per locale, making every behaviour
untestable in other languages (and `chatPromptStatesTheReadOnlyContract`
pins its clauses in one canonical form). Localisation enters as an
*instruction instead*: "Reply in the language the person writes to you in" —
one line, and the only place it belongs in a prompt. Bonus clause worth
knowing: the calm-never-shaming rule in the prompt is the Supplementary
Spec's usability requirement applied to a subcontractor — the model speaks
inside our app, so it is bound by our spec.

---

## v25.1 — the Settings Test button

**V83 `[DESIGN]`** The Test button builds its provider from
`aiProviderFromFields()` instead of calling `ai::configured()`. Why is
reading QSettings wrong here, what dialog promise forces that choice, and
how is "the mirror must not drift from the original" handled?
**A:** The button exists to answer "does what I just typed work?" — and the
typed key/address are NOT in QSettings yet, because the dialog's standing
promise is that only OK writes (Cancel-writes-nothing). Saving first just to
test would break that promise; reading saved settings would test last week's
setup. So the fields are read directly, with resolution rules deliberately
identical to `configured()` (custom → address+dialect from fields, non-empty
model overrides), making ✓ a promise about what OK *would* produce. The
drift risk is pinned negatively: `settingsTestUsesTheFieldsNotTheSavedSettings`
saves a valid Anthropic setup, shows a broken custom one, and asserts the
failure is the screen's.

**V84 `[TRADEOFF]`** The probe reuses `ChatClient` rather than getting its
own small wire class. Argue the reuse — and name the one new seam it cost.
**A:** ChatClient already carries everything a probe needs: provider
override, the provider-aware fail-fast messages ("no API key set for X"),
the 401/404/429 vocabulary, the timeout, the stale-generation guard. A
bespoke ProbeClient would re-implement that error taxonomy and immediately
start drifting from it — and reuse buys a stronger claim: the wire the ✓
vouches for is *literally* the wire the Assistant uses. The cost was one
seam: `setKeyOverride(std::optional<QString>)`, because `configuredKey`
reads QSettings and the probe must test the field-first/env-second
composition instead (env half extracted as `ai::envKey` so the dialog
composes rather than copies). Optional, like the provider override, because
"test with an empty key" is a real state distinct from "no override".

**V85 `[SPOT-THE-ISSUE]`** The verdict label is cleared on every provider
combo change, and the probe ChatClient is a child of the dialog. What bug
does each of those two lines prevent?
**A:** (1) A "✓ Connected" earned by Anthropic surviving a switch to the
custom endpoint would vouch for a setup nobody has tried — a stale verdict
is worse than none, the same staleness family as the wire clients'
generation guards. (2) Parent-child ownership means a probe in flight when
the dialog closes dies WITH the dialog: the reply lambda's context object is
gone, so nothing tries to write into a destroyed label. Without it, a slow
provider plus a quick Cancel is a use-after-free with a network-length fuse.

**V86 `[DESIGN]`** v25.2 calls the reasoning-model work "V72's bug class on
the other path." Name the class, and explain why the two dialects needed
*mechanically different* fixes for it.
**A:** The class is "the reply contains more than the answer." Anthropic
separates the extra *structurally* — thinking arrives as its own typed block
beside the text block — so the v21.2 fix was navigational: never index
`content[0]`, walk to the first `text` block. The OpenAI dialect ships the
deliberation *inside the string* (`<think>…</think>` in `content`) or
*beside the envelope's expectations* (all text in a `reasoning` side field,
`content` empty), so navigation can't help — the fix is a scrub plus a
fallback. Same principle ("extract the answer, not the envelope's whole
payload"), two mechanisms, because the dialects disagree about *where* the
extra lives. Recognising it as one class is what made the second fix land in
one small iteration instead of being re-derived from scratch.

**V87 `[TRADEOFF]`** `strippedOfThinking` is a hand-rolled `indexOf` scanner,
and an *unclosed* `<think>` drops everything to end-of-text. Argue against
the obvious regex alternative, and explain why the unclosed case is dropped
rather than kept.
**A:** The lazy-dotall regex (`<think>.*?</think>`) has two cliffs. First,
pathological replies can cost quadratic time. Second — the disqualifying one
— a *missing closer* makes the regex match nothing and pass the whole span
through untouched, which is exactly backwards: an unclosed `<think>` is the
streaming-truncation / died-mid-deliberation case, precisely when leaking is
most likely. The scanner states the intent in two `indexOf` calls per span
and handles the truncation case as a *feature*: everything from the orphan
tag onward is deliberation, so it drops, and the caller's empty-check plus
reasoning fallback then decide whether anything answerable remains. Kept as
its own exposed function so tests pin the edge cases (multiple spans, mixed
case, tag-only reply) without forging a full envelope each time.

**V88 `[SPOT-THE-ISSUE]`** A reviewer proposes simplifying v25.2: "send
`think: false` on every OpenAI-dialect request — it's one line, and servers
ignore what they don't understand." Two distinct bugs ship with that line.
**A:** (1) The premise is false at the worst address: **OpenAI proper
rejects unknown body fields with a 400**, so the blanket flag breaks every
cloud seat (OpenAI, and any strict compatible) to maybe-help the one local
seat — and it breaks them loudly, on every request. That's why the flag is
opt-in per *catalog entry* (`Provider.sendThinkFlag`, Ollama only), never
per dialect. (2) Custom endpoints would silently gain a field the user never
asked for on a server they defined — the "works with curl, fails in the app"
family, undebuggable from the app's side because the app is the thing
injecting the difference. Bonus honesty: even on Ollama the flag is
best-effort (honouring is reported inconsistent on the OpenAI-compatible
endpoint); the *reliable* off-switch is a Modelfile, and the scrub in
`extractText` is the guarantee either way — the flag is an optimisation, the
scrub is the correctness.

**V89 `[DESIGN]`** v25.3 didn't add a persona *on top of* the prompt — it
split the existing rules into four bands first. Name the bands, say where
each of the old five rules went, and explain why rule 4 in particular could
not survive intact.
**A:** Contract → floors → style → context. Rules 1, 2, 5 (never invent,
date arithmetic, reply-in-their-language) stayed in the locked contract.
Rule 3 (brevity) moved into the Calm preset, because verbosity is a persona
property — Brief exists to give it a different value. Rule 4 was the
problem: it fused two different kinds of instruction in one sentence —
*never shaming* (a safety property no user may trade away) and *calm tone*
(exactly the thing personas exist to vary). Ship personas with rule 4 whole
and you either lock the tone (personas can't work) or unlock the shaming
floor (personas can be phrased to remove a protection). The split — floor
up, style down — is the whole feature; the presets are decoration around it.

**V90 `[HANDS-ON]`** Write the assertion that proves "a persona may change
*how* things are said, never *what* is allowed" — not as a vibe, as code.
Then say what `everyPersonaKeepsTheContractAndTheFloors` adds that your
assertion doesn't cover.
**A:** String equality on the shared prefix:
```cpp
const int a = calm.indexOf("STYLE"), b = coach.indexOf("STYLE");
QCOMPARE(calm.left(a), coach.left(b));   // bytes above the band identical
QVERIFY(calm.mid(a) != coach.mid(b));    // and the band actually differs
```
Byte-identity above the STYLE marker means no persona can touch the
contract or floors even by accident — a stray edit that leaked persona text
upward fails the suite. What it doesn't cover: *Custom*, whose band is
arbitrary user text, and *ordering*. The walk-the-catalog test adds both —
it feeds Custom a hostile-ish band ("Talk like a pirate"), asserts the
contract, both floors, and "override any style" are still present, and pins
the index order contract < floors < style < context, which is the authority
order the layering claim depends on.

**V91 `[SPOT-THE-ISSUE]`** Three review comments on the persona patch:
(a) "Free text should be a QPlainTextEdit — one line is cramped."
(b) "Simplify: store the persona in data.json with everything else."
(c) "Give quick-add the persona too, for consistency."
Each one breaks something documented. What?
**A:** (a) The cramp is the feature. §C.4: long character prompts crowd out
the rules and measurably degrade instruction-following — a text area
*invites* the character sheet the warning is about. The QLineEdit plus
maxLength 240 is the warning enforced by the widget instead of by a doc
nobody reads. (b) data.json syncs. Persona is taste, not fact — sync it and
another machine's assistant starts speaking with *your* voice, and §E's
coming per-machine routing (local seats differ per device) makes
machine-local AI config the standing rule, not the exception. QSettings is
where taste already lives (agenda hours, week start). (c) Quick-add's
prompt is a JSON machine contract — its output is parsed, not read. A
persona there can't improve anything a parser sees and adds new ways to
break the parse ("Coach" cheering inside what should be a bare JSON
object). The persona reaches exactly one call site, and that's load-bearing.

**V92 `[DESIGN]`** v26's migration wrote nothing to QSettings — no copy, no
legacy-key removal — yet every v25 setup routes correctly. Explain the
mechanism, and why it beat the copy-once migration §E itself proposed.
**A:** Derivation at read time: `configuredRouteIds()` repairs the stored
list (unknown ids dropped, order kept), and if nothing survives — or no
route key exists — it returns `[configured().id]`, the seat the v25 key
names. Copy-once has three failure surfaces this has none of: it must
detect "already migrated" (or corrupt on the second run), removing the
legacy key breaks any downgrade, and the copy is a write that can happen at
a bad moment. Derivation can't run twice (it never runs, it just *is*),
leaves `ai/provider` untouched and still meaningful (it IS the primary
seat, which Settings edits directly), and costs one settings read. House
precedent: `personaById` and `dialectFromString` already repair-on-read;
a migration expressible as a derivation should be one.

**V93 `[TRADEOFF]`** Defend v26's harshest-looking rule: a chat route of
[Anthropic, Ollama] where Anthropic returns **401** gives the user an error
— even though a working Ollama sits right there in their own route. Then
name the class that DOES fall through and the two mechanisms that keep
falling-through honest.
**A:** A 401 is a server *saying something*: your key is wrong. Fall
through and the user gets an answer — from an 8B local model — and no
signal that their paid seat has been dead for a week. The §E phrasing:
masking a config bug costs an evening of wondering why the answers got
dumber; the error IS the feature. Only the Unreachable class (nothing
answered: refused connection, no route, timeout — status 0) moves to the
next seat, because silence carries no config information to lose. The two
honesty mechanisms: the transcript announces every hop as it happens ("⚠ X
unreachable — trying Y…"), and a fallback answer is attributed ("answered
by Y") — a conversation with two authors of different quality must say
which one said the thing you're about to act on.

**V94 `[SPOT-THE-ISSUE]`** The first draft of `ai::forcedDown()` read
`TICKTIMER_AI_DOWN` into a function-local `static QStringList`. Compiles,
passes a quick manual check, and would have broken something anyway — what,
where, and what's the general lesson?
**A:** A function-local static initialises on FIRST CALL and never again.
Any code path that calls `forcedDown()` before a test sets the variable —
another test constructing a ChatClient, an app-startup probe — freezes the
hook's answer at "nothing is down" for the whole process, and the fallback
test then fails (or worse, flickers depending on test order). The breaker
test suite in the same binary makes that ordering hazard near-certain.
Lesson: a static cache is a bet that the input never changes for the
process lifetime — env vars that tests mutate lose that bet by design. The
fix reads per call (one `qEnvironmentVariable` per attempt — nothing worth
caching), and the shipped comment names the trap so the next optimiser
doesn't reintroduce it. Related discipline from the same session: the
end-to-end test *cleans the process-wide breaker before and after*, because
shared mutable state that outlives a test is how green suites go flaky.

---

## v26.1 — the settings nav rail (design-addendum-settings-nav)

**V95 `[MC]`** `SettingsDialog::save()` is now a four-line loop over
`m_pages`. What does that buy, concretely?
**A:** Adding a settings page adds **zero lines** to `save()`. Under the old
flat shape every new preference appended another block to one shared
function that every feature also edited — the merge-conflict magnet
`AppData` avoids by having one named door per operation instead of one
`addEvent(everything)`. The catch-up feature is the test case: one new class,
one `addPage()` line, no edit here.

**V96 `[T/F]`** The refactor required updating `test_ui.cpp`.
**A:** **False**, and that's the point. Every `objectName` was preserved
(`startHourCombo`, `aiProviderCombo`, `aiTestButton`, …) and `findChild` is
recursive, so it walks into a `QStackedWidget` page that isn't currently
visible. The tests also drive widgets with `->click()` and
`->setCurrentIndex()`, which work on hidden widgets. A refactor whose tests
had to be rewritten hasn't been *proven* to be a refactor.

**V97 `[HANDS-ON]`** Why are the pages constructed eagerly instead of
lazily on first nav click? Give both reasons.
**A:** (1) `findChild<QComboBox*>("aiProviderCombo")` walks the *existing*
child tree. A hidden page is still findable; an **unconstructed** page has no
children at all, so eight tests would fail for reasons unrelated to
settings. (2) The saving is imaginary — a few dozen widgets, built once, on a
click the user made deliberately. Optimising an imperceptible cost by
breaking the suite is a bad trade twice over. The shipped comment says so, so
nobody "fixes" it later.

**V98 `[MC]`** Why `QListWidget` + `QStackedWidget` rather than
`QTabWidget`?
**A:** Tabs stop scaling past roughly six sections — labels compress, then
elide, then sprout scroll arrows. Three pages today, four the moment
catch-up lands. A vertical list holds a dozen comfortably and matches the
settings screen of essentially every app built in the last decade.

**V99 `[T/F]`** The nav and the stack are kept in sync by a lambda that
looks up the page for the clicked row.
**A:** **False.** It is a plain signal-to-slot connection:
`connect(m_nav, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex)`.
`currentRowChanged` emits an `int`, `setCurrentIndex` takes an `int`, and
`addPage()` guarantees the two index spaces agree by appending to both in one
call. When two Qt classes already speak the same language, connect them
directly — fewer moving parts, and the compiler checks the signature.

**V100 `[EXPLAIN-THE-TRADEOFF]`** "Remember the last settings page I
visited" was considered and rejected. Why, and what does that say about how
invariants get defended?
**A:** Storing the last page means writing to `QSettings` when the dialog
closes — *including on Cancel*. The dialog makes one promise out loud
("widgets edit local state, OK writes once, Cancel writes nothing") and this
would put an asterisk on it. The general lesson: a nice-to-have does not get
to weaken a stated invariant. An invariant with one documented exception is
an invariant nobody trusts, and the next exception is easier to argue for
than the first.

**V101 `[HANDS-ON]`** `SettingsPage` declares `title()` and `save()` but
deliberately no `load()`. Justify the asymmetry, and name the condition
under which you'd add one.
**A:** A page is constructed exactly once — `MainWindow` builds a fresh
`SettingsDialog` on every ⚙ click — so loading in the constructor is
sufficient and a `load()` would be ceremony for a lifecycle that doesn't
exist. Add it the day a page must be re-read *without* being rebuilt: a
long-lived settings window, or a page that has to react to an external
change (a sync pulling down new preferences). Not before.

**V102 `[SPOT-THE-ISSUE]`** `settingsui::makePolicyEditor` is a free
function, not a member. Its `connect` call reads
`QObject::connect(e.mode, &QComboBox::currentIndexChanged, host, sync)`.
Why is `host` there, and what breaks without it?
**A:** `host` is the connection's **context object** — the connection is
destroyed when `host` is. A free function has no `this` to hand over, which
is precisely why the host is a parameter. Drop the context argument and the
connection outlives the page: the lambda fires against destroyed widgets on
the next combo change. This is the same lifetime discipline as parenting a
`ChatClient` to the page so an in-flight probe dies with a closed dialog.

**V103 `[T/F]`** Moving the *"Blocks outside these hours still show…"* note
onto the Agenda page was a pure refactor with no behaviour change.
**A:** **False**, strictly — different text is on screen at different times,
so it is a behaviour change. It is also the note arriving at its correct
address: it describes the three hour combos and nothing else, but at the
bottom of the old dialog, below the AI and needs-a-block sections, it read as
a statement about all of Settings. A caveat filed under the wrong heading is
a caveat nobody applies correctly. Worth calling out in the addendum rather
than smuggling in under "no behaviour change".

**V104 `[MC]`** Three small classes went into one `SettingsPages.h` /
`.cpp` pair instead of six files. On what grounds?
**A:** They are never used apart from this dialog, they are small, and they
share the `PolicyEditor` helper. `ReviewWidgets.h` sets the precedent
(several collaborating widgets, one file). One class per file is a good
default, not a law — the law is *things that change together live together*.
The counter-case: the moment a page is reused elsewhere, or grows a test of
its own, it earns its own file.

**V105 `[EXPLAIN-THE-TRADEOFF]`** Why one `QScrollArea` wrapping the whole
stack rather than one per page?
**A:** A `QStackedWidget`'s size hint is the **maximum** of its children's,
so the dialog sizes to the tallest page and never resizes when you switch
pages. A window that jumps as you click nav rows feels broken. The cost is
that short pages carry the tall page's height and look a little empty; that
is cheaper than a jumping dialog. The scroll area is the escape valve for a
small screen, not the primary layout mechanism.

**V106 `[HANDS-ON]`** `Theme.h` gained a `QListWidget#settingsNav` block
that duplicates the hover and selected washes from `QTreeWidget#railTree`.
Defend the duplication.
**A:** QSS has no variables, so the alternative to duplicating two colour
rules is either a shared selector list (fragile — the two widgets are
different classes with different sub-controls) or generating the stylesheet
in C++ (a lot of machinery for two lines). The *palette* still lives in
exactly one place (`theme::` accessors); only the rule text repeats. The
reason it must repeat rather than diverge: two vertical pickers in one app
that highlight differently is the kind of small inconsistency that makes a
UI feel assembled rather than designed.

**V107 `[SPOT-THE-ISSUE]`** `AssistantSettingsPage`'s constructor calls
`loadFieldsFor()` — which calls `m_customRow->setVisible(...)` — *before*
`m_customRow` has been added to the `QFormLayout`. Is that a bug?
**A:** No, and it is preserved verbatim from the pre-refactor code on
purpose. Qt records an explicit show/hide flag on the widget; adding it to a
layout afterwards respects that flag. The reason to *notice* it: it is
exactly the kind of ordering that looks accidental and gets "cleaned up" by a
later reader. When a refactor moves code whose correctness depends on
ordering, the safest move is to preserve the order exactly and let the
behaviour be identical by construction — then change one thing at a time,
with the tests green in between.

---

## v26.2 — catch-up, part 1: the domain (design-addendum-catch-up)

**V108 `[MC]`** The feature stores `Event::outcome` but does *not* store "this
block was missed". State the rule and one concrete payoff.
**A:** **The judgement is derived, the decision is stored.** "Missed" is a
pure function of the planned window, the focus segments, and `now`
(`missed::judge`) — so moving the threshold from 50% to 70% in Settings
re-judges every block in history on the next read, with nothing to migrate
and no stale flag that could disagree with the segments it came from. "I
deliberately skipped this" is derivable from nothing, so it's a real field
that syncs. Same split as `coverage::rung` (derived) vs `Task::dismissCount`
(stored).

**V109 `[T/F]`** Break time inside a block counts toward whether the block
happened.
**A:** **False.** `Event::focusSeconds()` sums **Focus only**. Break time is
legitimate but isn't the work, and Distracted time is explicitly lost time —
counting either would let a block full of procrastination pass as done. Note
this is deliberately *not* a knob: every relaxation of it is a way for the app
to call a block done when nothing happened. `stats::eventTotals` keeps the
full three-way split, because the reporting screens ask a different question.

**V110 `[HANDS-ON]`** `judge()` reports a failure for a block from three
weeks ago, but `isUnresolved()` returns false for it. Why are these two
functions, and not one?
**A:** `lookBackDays` (default 3 since v26.8; 7 originally) is a *surfacing* filter, not a judgement.
Without a horizon, reinstalling the app greets you with four hundred
unresolved blocks — a wall of guilt nobody triages, so the feature gets
ignored, so it may as well not exist. But the evening review still wants to
say what actually happened, and a report that silently forgets old failures
is a different kind of lie. Two questions, two functions, one of them layered
on the other.

**V111 `[EXPLAIN-THE-TRADEOFF]`** Why does `reschedule::propose` return a
`QVector<Option>` instead of a single best slot?
**A:** Because the single-answer shape collapses the moment the week is
full — the normal state for anyone who plans. A missed block with a deadline
in two days and no free time before it isn't a puzzle with a hidden solution;
it's a **conflict** between the deadline, the full duration, and the other
blocks. Something must give and only the human can say which. Returning a
ranked list (and being allowed to return an empty one) makes the tradeoff
visible and one tap to resolve, instead of either silently cramming the block
somewhere (the calendar starts lying) or shrugging (you find out on deadline
day).

**V112 `[SPOT-THE-ISSUE]`** A first draft of `propose()` planned against
`block.plannedSeconds()`. What's wrong with that, and which type exists
specifically to prevent it?
**A:** It re-offers the **full** duration to someone who already put in part
of it — double-booking time they've spent. `missed::Verdict::shortfallSeconds()`
is the number to plan against, and the whole reason `missed::Reason`
distinguishes `Partial` from `NeverStarted` is that the two want different
proposals. Small dishonesty in a number is how users stop trusting all of
them.

**V113 `[MC]`** `Kind::FreeSlot` is only reachable when
`need <= maxPieceMinutes()`. Where does that ceiling come from and why is it
enforced in the proposer rather than at commit time?
**A:** `plan::kMaxSlotsPerEvent` (4 slots = 2 hours) — a domain invariant
`AppData` already enforces. A three-hour debt cannot be one Event no matter
how empty the week is, so it falls through to `Split`, which is the correct
offer anyway. Enforcing it in the proposer matters because an offer the
aggregate root would refuse is *worse* than no offer: the user has already
decided to accept it by the time it fails.

**V114 `[T/F]`** The app ranks bump candidates so the least important block
is offered first.
**A:** **False**, and deliberately. Ranking requires knowing how important
each block is, and the app can't: a Task block inherits priority and a due
date, a `Gym` block on an Activity has no priority at all, an ad-hoc block has
nothing. Inventing a "flexibility" field is a real modelling cost for a guess
that's often wrong — and wrong here means smugly deciding your gym session
matters less than your lab. **The app finds the candidates; the user picks the
victim.** The one filter the app *is* entitled to: a block with focus time
already in it isn't a candidate, because taking a slot someone is halfway
through is a loss, not a swap.

**V115 `[HANDS-ON]`** `rescheduleBlock` copies `src->taskId` into a local
*before* calling `appendGuardedEvent`, then re-looks-up both events
afterwards. Explain, and name the failure mode.
**A:** `appendGuardedEvent` calls `QVector::append`, which may **reallocate**
the vector's buffer and invalidate every `Event*` into it — including `src`.
Reading `src->taskId` after the append is a **use-after-free**, and the
nastiest kind: with a small vector the old buffer is usually still mapped, so
it appears to work in testing and corrupts later. The general rule: never hold
a pointer into a container across an operation that can grow it.

**V116 `[MC]`** `appendGuardedEvent` gained a `notify` parameter. What breaks
without it?
**A:** `rescheduleBlock` performs two mutations — create the replacement, mark
the original `Moved`. Qt's default connections are **direct**, so a listener on
`changed()` runs *synchronously* between the halves and would observe a
replacement block whose original still reads as unresolved. The flag lets the
pair emit exactly one `changed()`, so the operation is atomic from any
observer's point of view.

**V117 `[EXPLAIN-THE-TRADEOFF]`** Why does `Event` store `movedToId` but not
`movedFromId`?
**A:** Two pointers can disagree — a half-applied edit leaves a chain that
says different things depending on which end you read, and every reader then
has to decide which end to trust. The reverse question ("was this block
rescheduled from somewhere?") is a linear scan over events: trivially cheap
and structurally incapable of drifting. **Derive the reverse, store the
forward.** The repeat chain makes the same call — the rule lives on exactly
one link.

**V118 `[SPOT-THE-ISSUE]`** `AppData::resolveBlock` returns `false` if you
pass `BlockOutcome::Moved`. Isn't refusing a legal enum value just awkward
API design?
**A:** No — it's the aggregate root defending an invariant, which is its one
job. `Moved` isn't a state you can *assert*; it's only true if a replacement
block exists, and only `rescheduleBlock` can produce one. Allowing it here
would permit `outcome == Moved` with an empty `movedToId`: a chain link
pointing nowhere that every downstream reader must then defend against. The
enum is the vocabulary; the door decides which words a caller is allowed to
say.

**V119 `[T/F]`** `blockOutcomeToString(BlockOutcome::Unset)` returning the
empty string is a sloppy stand-in for a proper "unset" marker.
**A:** **False** — it's the migration trick, used for the fourth time in this
format. A pre-v11 event has no `"outcome"` key; `o["outcome"].toString()`
gives `""`; that reads back as `Unset`. Additive growth, tolerant read, no
migration branch (`taskId` at v6, `repeat` at v9, the dismissal fields at
v10). The companion rule: any *unrecognised* string also degrades to `Unset`,
so garbage on disk can never invent a decision the user didn't make.

**V120 `[HANDS-ON]`** `freeOn()` uses `snapUp` on interval starts and
`snapDown` on interval ends. Why two functions instead of one `snap`?
**A:** Direction matters. A free interval must **shrink** to the grid, never
grow past its real bounds — rounding a start down or an end up would offer
time that is actually occupied, and the proposal would then be rejected by
`isFree()` at commit. Same reasoning applies to "today": the window start is
`snapUp(now)`, so a proposal never begins three minutes from now. A slot you
can't realistically walk into isn't an offer, it's a taunt.

**V121 `[MC]`** Why is `Split` described in the addendum as "quietly the most
valuable rung"?
**A:** A full calendar is almost never *contiguously* full. Ninety minutes
that fit nowhere as a block often fit as 45 + 30 + 15 across three days. It's
the rung that most often turns "nothing fits" into a real recovery, and it
costs only context switches — cheaper than dropping scope (`Shorten`),
sacrificing another block (`Bump`), or slipping a deadline
(`BeyondDeadline`).

**V122 `[EXPLAIN-THE-TRADEOFF]`** `propose()` can return an empty vector.
Most scheduling code would treat that as a bug. Defend it.
**A:** It's the honest answer to a real situation: the week is full through
the horizon, nothing is bumpable, and there's no room past the deadline
either. Inventing a placement would make the calendar lie; saying nothing
would let the user discover the problem on deadline day. An empty hand is
information — it tells you something true about your week early enough to act
on it (drop scope, move the deadline, accept the slip). The surface is
expected to say so plainly rather than paper over it.

---

## v26.2 — catch-up, part 2: the surfaces (design-addendum-catch-up §K)

**V123 `[MC]`** `NeedsBlockCard` gates the panel; `CatchUpCard` deliberately
doesn't. What's the reasoning?
**A:** The gate's question — "have you looked at what needs planning?" —
blocks *planning the day*, which is what the panel is for, so holding the
numbers hostage is proportionate. A missed block from yesterday doesn't get
that power: the day can proceed while the wreckage waits. And there's a
ceiling argument: one blocking review per panel. Two gates teach the user to
click through both without reading either, which destroys the first gate's
value too.

**V124 `[T/F]`** The card stores whether it's in morning or evening mode.
**A:** **False** — the mode is derived on every refresh from `now` against
`prefs::agendaWindow()`: evening begins 90 minutes before the window closes.
Derive-don't-store applied to UI state (same as the sibling's gate), with a
bonus: "end of day" means the end of *your* day. A 6 AM–2 PM schedule flips
to evening framing at 12:30, not at some hardcoded 8 PM.

**V125 `[HANDS-ON]`** The "Later" button computes a different snooze target
depending on the mode. Walk through both, and name the principle.
**A:** Morning snooze → until the evening moment (`dayEnd − 90` as a time
today); evening snooze → until tomorrow midnight. The principle: **a
dismissal must promise a return.** A card that just disappears is how missed
blocks went unhandled for 26 versions. Second detail: the timestamp is
written to QSettings *and* mirrored in `m_sessionSnooze` — the v22.7 lesson
that the disk may forget but the widget that watched you click may not.

**V126 `[SPOT-THE-ISSUE]`** `onCuAccept` calls a door and then… doesn't call
`refresh()`. A reviewer flags the missing update. Defend the code — including
the case where the door *declines*.
**A:** The door emits `changed()`, and the ordinary pipeline repaints every
surface, card included (its fingerprint now differs). A refresh here would be
the app updating twice for one fact — the duplication `changed()` exists to
prevent. The decline case (slot taken between propose and click): nothing
changed, so nothing repaints, and the proposal stays on screen still
pressable; the next `changed()` from any source re-proposes against current
reality. Quiet, but never wrong — an error toast would be arguable, but a
stale forced repaint would not.

**V127 `[EXPLAIN-THE-TRADEOFF]`** Why is Bump shown but not one-tap
acceptable in v1?
**A:** Accepting a bump displaces the victim — and propose-don't-move applied
*consistently* means the victim then needs its own proposal, whose acceptance
might displace another block… a cascade one card row can't host honestly.
The v1 resolution keeps the valuable half: the row **names** the conflict
("in the way: Meeting, Tue 09:00") and opens that day; the user clears the
way with existing verbs (drag, resize, delete), and the freed slot surfaces
as a normal FreeSlot proposal on the next refresh. Deferring the tap changed
no shipped API — the day it's added, nothing here moves.

**V128 `[HANDS-ON]`** `rescheduleBlockSplit` validates spans against the
calendar *and* against each other. Why can't `isFree` handle the second
check, and what property does the double loop buy?
**A:** `isFree` checks against `m_events` — and the sibling spans aren't in
it yet, because nothing is appended until everything validates. Two proposed
pieces on the same afternoon would each pass `isFree` individually and
collide on append. The pairwise loop makes the door **all-or-nothing**: a
half-applied split is a calendar state nobody proposed, which is exactly the
class of outcome an aggregate-root door exists to rule out.

**V129 `[T/F]`** In the settings page, "Only if never started" storing `0`
rather than `1` is cosmetic.
**A:** **False** — it's load-bearing. `judge()` flags `NeverStarted` on zero
focus regardless of the threshold; the `Partial` branch tests
`focus×100 < minPercent×planned`, which at `minPercent = 0` is never true. So
`0` disables partial-flagging entirely — exactly the label's promise. At `1`,
a 30-second dab in a 90-minute block (0.55%) would still flag, and the label
would be lying.

**V130 `[MC]`** `brief::Options` gained a `missedRule` **field** instead of
`dayBriefing` reading `prefs::missedRule()` itself. Why?
**A:** `brief::` is domain-only — no QSettings, no widgets, no vendors — and
that layering is enforced by the build (it compiles in `test_domain`, which
links no UI). A `prefs::` read would drag QSettings into the domain suite and
break the two-pure-layers split its header celebrates. The caller
(`ChatPage`) passes the pref as a value, so the assistant and the card judge
by the same bar while the layer stays clean. Passing policy *in* is the same
move as passing `now` in: the nowProvider doctrine generalised to taste.

**V131 `[SPOT-THE-ISSUE]`** The card's fingerprint truncates `now` to minute
precision. A colleague "fixes" it to full precision for accuracy. What
regresses?
**A:** The click-eating bug returns. The glance panel refreshes once per
second while a timer runs; with seconds in the print, every tick differs,
every tick rebuilds, and the button under the user's finger is deleteLater'd
between press and release — `clicked()` never fires. Minute precision is
correct because nothing the card derives (verdicts, the evening flip, the
snooze) can change within a minute. The fingerprint isn't a cache of the
data; it's a print of the *rendering inputs* at the granularity they can
actually change.

**V132 `[HANDS-ON]`** In `makeRow`, the click lambdas capture
`const QString id = block.id` rather than `[&block]`. What goes wrong with
the reference capture?
**A:** `block` is a reference into `AppData`'s event vector. The lambda
outlives the rebuild that created it, and any mutation between click-wiring
and click — an accepted proposal appending events, a sync pull replacing the
vector — can reallocate or rewrite that storage. The reference then dangles
and the click reads freed memory. Same family as the `rescheduleBlock`
copy-before-append rule: **never hold a pointer or reference into a container
across operations that can grow it** — and a stored lambda makes "across"
arbitrarily long.

---

## v26.3 — the 320-pixel lesson (design-addendum-catch-up §K.1)

**V133 `[SPOT-THE-ISSUE]`** The shipped card rendered "apper" and "3kip it".
What was the mechanism, and why didn't the test suite catch it?
**A:** Four buttons in one `QHBoxLayout` inside a fixed-320px panel: when a
layout's minimum width exceeds the available room, Qt doesn't wrap — it
**clips**, mid-glyph. The suite drives widgets with programmatic `click()`
and `findChild`, which work regardless of geometry; nothing asserted that
text *fits*. Same family as the v22.2 click-eater: programmatic interaction
bypasses exactly the physical layer (input delivery there, pixels here)
where the bug lived. Layout truth needs eyes — or a screenshot harness.

**V134 `[MC]`** State the layout rule §K.1 distills, and how the revised row
applies it.
**A:** *On a fixed-width surface, a horizontal row of buttons may hold at
most one button whose label varies.* The revision goes further: the
variable-label button (Move → …) gets a **full-width row of its own** — it
can't lose a width fight it never enters, and full width reads as "this is
THE proposal". Secondary actions become one fixed short word each (Done,
Skip, More…), flat, with the sentence-length phrasing demoted to tooltips
where length costs nothing.

**V135 `[HANDS-ON]`** "Skip all 46" emits the id list it rendered from
instead of letting the page recompute "all unresolved". Why?
**A:** The page would need this card's exact `now` and `missed::Rule` to
derive the same set — two derivations of "the backlog", which can disagree
(a minute passes, a pref changes, a block resolves in between) so the button
would drop blocks the user never saw or miss ones they did. Carrying the ids
makes the signal mean precisely *what was on screen when you pressed it*.
Contrast with recompute-on-read elsewhere: derive-don't-store applies to
*state*; a user's consent attaches to a *snapshot*.

**V136 `[EXPLAIN-THE-TRADEOFF]`** Why a new `resolveBlocks` door instead of
the page looping `resolveBlock` 46 times? And why does a no-op batch emit
nothing?
**A:** Each `resolveBlock` emits `changed()`, and connections are direct —
46 calls means 46 synchronous full-surface repaints for one user decision.
The bulk door mutates the batch and emits **once**. Zero mutations emit
zero: `changed()` is a promise that something changed, and a spurious
emission makes every listener re-derive for nothing — the day someone
debugs "why did the agenda repaint?", a lying signal is the worst witness.
Stale ids are skipped, not fatal: one ghost in a bulk list shouldn't sink
the other 45 decisions.

**V137 `[T/F]`** "Skip all" should always be visible for consistency.
**A:** **False** — it appears only when the backlog exceeds the visible rows.
Under that, per-row Skip is the honest tool: three visible blocks deserve
three considered decisions, and a bulk exit alongside them invites
resolving-without-reading. The button exists for the wall, not the list —
matching the §C horizon reasoning: the feature must never become a wall of
guilt, *and* never make dismissal cheaper than attention when attention is
affordable.

---

## v26.4 — the eraser (design-addendum-catch-up §K.2)

**V138 `[EXPLAIN-THE-TRADEOFF]`** Undo vs a confirmation dialog for "Skip
all 46" — make the case that was made, and name the user-model behind it.
**A:** A confirm popup taxes every *legitimate* press and trains
click-through: by the third dialog nobody reads it, so it stops protecting
the accident while still costing attention — the worst trade for a user the
app explicitly serves (executive-function-limited, friction-sensitive). Undo
inverts it: the common path stays one tap, and only the rare mistake pays —
one tap again. Act immediately, offer the eraser.

**V139 `[MC]`** Why did Undo require zero new wiring?
**A:** Three existing pieces already compose into it. The signal:
`resolveAllRequested(ids, outcome)` never said *which* outcome. The door:
`resolveBlocks` refuses only `Moved` (earned, not asserted), so `Unset` was
always legal. The judgement: never stored (§B), so un-deciding is one field
write and the verdicts re-derive on the next read — no revive routine, no
repair pass, no second code path to test. The derived/stored split paying
out a third time.

**V140 `[SPOT-THE-ISSUE]`** The receipt is shown even when the unresolved
list is empty, the moment is toggled off, or the card is snoozed. Isn't that
a violation of the card's own visibility rules?
**A:** It's the point. The accident's exact shape is *the action emptied the
card* — Skip all fires, `hasAnything` goes false, and under the old rules
the card hides, taking the only way back with it. The receipt is the trace
of an action the user JUST took, so it outranks every hide rule. One
deliberate exception: "Later" clears it — a snooze is the user filing the
card away, receipt included.

**V141 `[HANDS-ON]`** The undo lambda copies `m_undoIds` into a local
before connecting, then clears the members before emitting. Walk through
why both moves matter.
**A:** The copy: the lambda outlives this rebuild, and undo must mean *that*
action — capturing the member by reference (or reading it at click time)
would replay "whatever the receipt holds now", which a later single-skip
could have replaced. The clear-before-emit: the emit mutates data →
`changed()` → surfaces repaint synchronously; if the members still held the
old receipt during that cascade, the fingerprint would show a stale receipt
for a state where the undo already happened. Order is the correctness here,
same family as stash-then-load in the provider combo.

**V142 `[T/F]`** Accepted moves should get the same Undo treatment for
consistency.
**A:** **False**, as scoped (§K.2). Done and Dropped are *silent* verdicts —
they leave no trace on any other surface, so without the receipt they are
unreachable. An accepted move is *loud*: the replacement block is sitting on
the calendar, visible, and reversible with existing verbs (drag it, delete
it). The eraser exists to cover the invisible mistakes; consistency with the
visible ones would add machinery (delete-the-new + reset-the-old, atomic)
for a mistake the user can already see and fix.

---

## v26.5 — the way back, on the map (design-addendum-catch-up §K.3)

**V143 `[MC]`** The user asked for a keyboard shortcut to recover skipped
blocks. What shipped instead, and on what grounds?
**A:** A visible chip — "N resolved recently · review" — expanding into
per-block *Bring back* plus *Bring all back*, modelled on `NeedsBlockCard`'s
put-off strip. A hidden shortcut helps only someone who already knows it
exists, at the exact moment they're panicking; recovery is precisely the
feature that must be discoverable by looking. The user's *instinct* (an
in-app way back must exist) was right; the surface was the correction.

**V144 `[EXPLAIN-THE-TRADEOFF]`** No `resolvedAt` timestamp was added —
the chip derives from the block's own date. Give both sides and the tie-
breaker.
**A:** Deriving means the chip reaches accidents that *predate the feature*
(the motivating rescue worked with no data surgery), the schema stays at
v11, and the horizon ages the chip out with zero bookkeeping. The cost: a
block resolved recently about a date past the horizon falls off the chip —
recoverable only by widening "Look back". Tie-breaker: the existing knob
covers the gap today, and `resolvedAt` can earn its schema bump the day it
doesn't. Store nothing until a question can't be answered without it.

**V145 `[SPOT-THE-ISSUE]`** Why is `Moved` excluded from the recoverable
set when `Done` and `Dropped` are in it?
**A:** A moved block has a live replacement on the calendar. "Bringing it
back" would put the same obligation on the board twice — and deleting the
replacement to prevent that is the move-undo machinery §K.2 already scoped
out (the replacement is visible and reversible with existing verbs). The
chip exists for the *silent* verdicts, the ones no other surface shows.

**V146 `[HANDS-ON]`** With the list hidden (snoozed or moment off) but the
chip visible, rebuild returns early — before the footer. Why must "Skip
all" and "Later" disappear with the list?
**A:** "Skip all" would be a bulk decision about blocks *not on screen* —
consent attaching to a snapshot the user can't see, the inverse of the V135
rule that the ids must be what was rendered. And "Later" with no list has
nothing to snooze; a control that does something invisible teaches users
the card is haunted. Each tenant carries its own controls: the chip brings
its Bring-back/Hide row along.

**V147 `[T/F]`** The chip needed a new signal, slot, and AppData door.
**A:** **False** — third time running. `resolveRequested` /
`resolveAllRequested` never fixed the outcome, the doors always accepted
`Unset`, and the panel/page wiring forwards blind. The whole feature is a
derivation (`missed::recentlyResolvedIn`) plus widgets. When the third
feature in a row costs zero wiring, that's the architecture review: signals
that carry *intent + data* rather than baked-in actions compose into
features nobody designed yet.

---

## v26.6 — hidden is indistinguishable from gone (design-addendum-catch-up §K.4)

**V148 `[SPOT-THE-ISSUE]`** The user reported 46 blocks "gone forever". The
data showed nothing dropped. Reconstruct the actual failure chain.
**A:** The accidental press was "Later": list snoozed until evening, card
hidden — and a snoozed card is pixel-identical to an empty one. The user
read hidden as destroyed, because the UI offered no evidence either way.
The failure wasn't data loss and wasn't even the snooze; it was **state
without a trace**. Diagnosis hint that cracked it: if the blocks had truly
been dropped, the v26.5 chip would have been visible in the screenshot — its
absence pointed at the snooze suppressing everything.

**V149 `[MC]`** Two rules shipped in v26.6 — state both, and connect each to
the earlier lesson it extends.
**A:** (1) *The recovery surface never obeys the snooze* — extends V143's
discoverability rule: the way back must be findable at the moment of panic,
so it can't be hostage to the very control that causes the confusion. (2)
*Invisible state that suppresses UI must announce itself* — extends §K.2 one
layer deeper: the receipt made **actions** reversible; the marker ("46
waiting · back 22:30 — Show now") makes **states** legible. General form:
whenever a feature hides content on a timer, the hiding itself must be
visible.

**V150 `[HANDS-ON]`** `snoozeTarget()` returns the LATER of the session and
stored timestamps. Why max, and why does the fingerprint now embed the
target instead of just the S flag?
**A:** Either witness alone suffices to snooze (v22.7's two-witness rule),
so the card returns only when the further promise lapses — max is the
honest answer to "when is it back?". The target rides the fingerprint
because it is now *rendered* (the strip's label): two different snooze
targets are two different cards, and a print that omitted it would skip the
rebuild when the label changed. The fingerprint's contract is "everything
the rebuild renders", and the strip just added a renderable.

**V151 `[EXPLAIN-THE-TRADEOFF]`** The card now hosts four tenants — list,
receipt, chip, marker — each with its own visibility rule inline in
`rebuild()`. When does this stop being fine, and what did the addendum do
about it?
**A:** Inline booleans are fine while a reader can hold every rule in one
screenful; the fourth tenant is at that edge, and a fifth would cross it —
pairwise interactions grow quadratically, and the "chip suppressed while
receipt shows" style of exception starts hiding in the conditions. The
addendum did the cheap, honest thing: **noted the threshold out loud** for
the next touch (a small state table in the header) rather than refactoring
speculatively now. Recording "this is the line" is itself a design act — it
turns a future smell into a planned decision.

---

## v26.7 — B′: one chip, three intensities (design-addendum-catch-up §K.5)

**V152 `[MC]`** The redesign was prototyped in HTML before any C++ moved.
What did that buy beyond "seeing it first"?
**A:** Three things. Cost: a layout debate in a throwaway page is an hour;
in Qt widgets it's a week. Bias: whatever gets *built* first tends to win
arguments by sunk cost — a mock keeps all variants equally cheap to discard.
Discovery: the owner's reaction to variant C surfaced that their preference
was a **workflow** (quiet workday, deliberate re-summon, guaranteed reset),
not a layout — a distinction the final design is built on and that no static
description had exposed.

**V153 `[EXPLAIN-THE-TRADEOFF]`** "Snooze is de-emphasis, not a lock."
Unpack the axis separation, and what died because of it.
**A:** Attention and access are separate axes. The snooze governs attention
only — chip intensity — while the drawer stays one tap away from every
visible state. Consequence: v26.6's "Show now" button ceased to exist,
because access was never what needed restoring; and the muted chip absorbed
the marker's job (hidden ≠ gone). Second-order choice: the midday tap does
NOT clear the snooze — reviewing is transient, prominence returns only at
the reset — which is what makes aggressive morning snoozing *safe* rather
than *risky*.

**V154 `[SPOT-THE-ISSUE]`** The chip's click handler uses `m_lastNow`
instead of `QDateTime::currentDateTime()`. Why is the wall clock a bug here
when it's the truth in production?
**A:** The nowProvider doctrine: time is injected so tests can walk the
widget through a day at a fixed moment. The chip is the card's one
*persistent* handler — every other lambda captures the `now` of the rebuild
that created it, but a ctor-connected handler has no rebuild, so it must
read the last injected clock. Reaching for the wall clock would make the
chip and the test disagree about what time it is — the exact "seam with a
hole" the v19.6 scar warns about.

**V155 `[T/F]`** The chip being a single persistent button restyled in
place is a styling nicety.
**A:** **False** — it's a structural elimination of a bug class. The v22.2
click-eater existed because rebuilds destroyed the button under the user's
finger between press and release; the fingerprint gate *mitigated* it. A
widget that is never destroyed cannot be destroyed under a click — the
persistent chip makes the bug impossible rather than rare. The drawer still
rebuilds via `clearContent`, where the sibling's precedent already proved
the pattern safe.

**V156 `[HANDS-ON]`** The Settings "Recovery" row was called "cheap" in
discussion and rejected in the build. Reconstruct both halves and name the
lesson.
**A:** In discussion it looked like one button on an existing page. In
code, `SettingsDialog` is deliberately AppData-free — its header's stated
contract is that closing it can't dirty the planner or trigger a sync — so
a bring-back action needs the mutable aggregate root and punctures a
boundary held since v26.1, for a need the drawer already covers. Lesson:
**estimate against the code, not the sketch** — and when a recorded claim
turns out wrong, correct it on record (§K.5 does) rather than letting the
addendum contradict the build.

**V157 `[MC]`** Why did the pie die, and why does `CategoryPie` survive?
**A:** On the glance panel it rendered the same split as the bars directly
above it — the bars were literally its legend, a chart confessing it adds
no information; on a crowded 320px column that's negative value. The class
survives because the week/month reviews use it where it *isn't* sitting
beside its own data. The general test: a visualisation earns space by
showing something the surface doesn't already show.

---

## v26.7.1 — closing the seam (design-addendum-catch-up §K.5.1)

**V158 `[SPOT-THE-ISSUE]`** The first fix draft had `GlancePanel` call
`m_catchUp->setVisible(false)` while the gate was closed. Trace the bug that
never shipped.
**A:** The card's refresh is fingerprint-gated. Panel hides the chip; the
gate later opens; panel stops hiding — but the card's data and clock are
unchanged, so its fingerprint matches, `refresh()` early-returns, and
`applyChip` (the only thing that calls `show()`) never runs. The chip stays
invisible forever. Fix: visibility gets ONE owner — the card — and the gate
arrives as an input (`setSuppressed`) whose setter clears the print, forcing
the next refresh to re-decide. General rule: when a widget caches its
rendering decisions, external state affecting them must flow *through* the
widget, never around it.

**V159 `[MC]`** "Solid = actionable, dashed gray = waiting" — why is this
worth a rule rather than a taste?
**A:** Because two features already had the same state wearing different
clothes: a put-off task ("3 put off · 14:00") and a snoozed chip
("31 · back 22:30") are both *content hidden on a timer with a visible
return*. A user who learns the dashed coat once now reads it everywhere —
that's the rail-tree/settings-nav consistency argument (V106) promoted from
colors to semantics. The moment a third waiting state appears, the rule
answers its styling question before anyone asks it.

**V160 `[EXPLAIN-THE-TRADEOFF]`** The review row accepts a documented
squeeze: pinned hero rows share 320px with the chip. Defend shipping a known
imperfection over the two alternatives.
**A:** Alternative 1 — stack when pinned rows exist — adds a layout mode for
a rare case (rung-2 escalations) before evidence says it's needed.
Alternative 2 — inject the chip into the sibling's chips layout — couples
the two cards and dies on the sibling's rebuild (`deleteLater` would destroy
the borrowed chip). The shipped call optimises the common case exactly to
the prototype, keeps the rare case functional, and writes the escape hatch
into the comment so it's *a decision waiting, not a surprise* — the same
move as §K.4's state-table note.

---

## v26.7.2 — the fatigue report (design-addendum-catch-up §K.5.2)

**V161 `[HANDS-ON]`** The stylesheets said `border-radius: 999px` and the
buttons rendered with sharp corners. Explain, and name the general lesson.
**A:** Qt's stylesheet engine silently **drops** any border-radius larger
than half the widget's height — the web's 999px pill idiom becomes a plain
rectangle, no warning, no clamp. Fixed with an explicit 14px (safely under
half of the ~30px pill). Lesson: **QSS is CSS-shaped, not CSS.** Idioms
imported from the web must be checked against what QStyleSheet actually
implements, and since it fails silently, the screenshot is the only
reliable checker — which is also why this bug survived a compile, a test
suite, and a review, and died to a user's photo.

**V162 `[SPOT-THE-ISSUE]`** A user reports "my eyes fatigue quickly" on a
two-pill row. Decompose that into mechanical causes, as §K.5.2 did.
**A:** (1) *Framed void*: stretch placed between the pills pinned them to
opposite walls, making the empty middle the subject — every comparison
costs a saccade across dead space. (2) *Dash buzz*: a 1px dashed border is
high-frequency edge detail, dozens of contrast flips the visual system
keeps re-processing. (3) *Broken geometry*: the radius clamp turned soft
pills into hard boxes. The meta-lesson: subjective discomfort ("hard to
look at") almost always has enumerable mechanical causes — treat the
feeling as a bug report and go find them.

**V163 `[EXPLAIN-THE-TRADEOFF]`** §K.5.1 shipped "solid = actionable,
dashed = waiting"; §K.5.2 revised it a version later. Was the first rule a
mistake worth avoiding, and what does the pair of corrections in this
chapter demonstrate?
**A:** The rule's *semantics* were right — waiting states should share one
coat — and its *vehicle* was wrong: line style buzzes at 1px, so the coat
became weight and contrast ("strong = actionable, faded = waiting"). It was
a reasonable call that reality falsified in a day, which is the cheapest
kind of wrong. The chapter now carries two on-record corrections (the
"cheap" settings row in §K.5, this rule) sitting next to the original
claims — demonstrating that written decisions aren't for being right, they
are for making *being wrong* fast to find and cheap to fix.

**V164 `[SPOT-THE-ISSUE]`** v26.7.2 fixed the pill radius at 14px and the
corners were STILL sharp on the owner's machine. Why, and why is
`setFixedHeight(30)` the right fix rather than a 10px radius?
**A:** The clamp is relative: 14px is honoured only on a widget ≥28px tall,
and font metrics decide the height — Windows' default 9pt Segoe UI lands
the pill near 25px, so Qt dropped the radius a second time. Shrinking the
radius to "probably safe" just moves the threshold to some other machine's
font; pinning the height makes the radius *lawful by construction* on every
machine. General rule: when a style rule depends on a size, fix the size in
code — don't hope the font agrees.

**V165 `[HANDS-ON]`** Three row layouts in three versions: stretch between
the pills (void), both size-to-hint (clipping), chip-first with the card on
the stretch (shipped). Derive the rule that makes the third one stable
rather than lucky.
**A:** The failures were dual: a stretch *between* honest widgets frames a
void; sizing an *unreliable* widget to its hint clips it (the needs-block
card wraps a QScrollArea, whose sizeHint is a cached guess — the v22 scar).
The rule: **in a row, the widget with the unreliable hint takes the
stretch** — a stretch member's width is leftover space, so the hint's error
is absorbed instead of rendered — **and honest-hint widgets pack first** so
adjacency doesn't depend on any hint at all. Stable because it assigns each
failure mode to the mechanism that neutralises it, not because the numbers
happen to fit this font.

**V166 `[SPOT-THE-ISSUE]`** After v26.7.1, both slide-over drawers broke —
one invisible, one mangled — yet compiled clean, passed every test, and
survived review for three versions. Reconstruct the failure and name why
each safety net missed it.
**A:** `drawer()` resolved its host as `parentWidget()` — an IMPLICIT
contract ("host = the glance panel") that held only by coincidence of the
old layout. The review row re-parented both cards into a pill-height
wrapper, so the sheets began opening over a ~40px strip. The compiler
missed it because the code stayed legal; the tests missed it because bare
test cards have no parent and fall back to hosting on themselves —
exercising the fallback, never the app's path; review missed it because
the assumption lived in a comment inside `drawer()`, not at the site that
changed. Fix: `setDrawerHost()`, injected by the panel. The general rule:
**an implicit assumption is a dependency without a name — name it or lose
it**, because it breaks silently the day someone moves the thing it
secretly pointed at.

**V167 `[MC]`** The pill row showed a phantom gap beneath it even after the
width fixes. Same widget, same root cause — what changed, and what completes
Coda 2's rule?
**A:** The axis. The stretch neutralised the scroll area's unreliable
sizeHint horizontally, but nothing bounded it vertically, so the card
reserved phantom height and the row inherited the gap. Fix: cap the card to
`m_body->sizeHint().height()` — the body is a plain widget, so its hint is
honest and includes pinned rows — with the cap lifted in gate mode, where
filling the panel is the point. Completed rule: **an unreliable hint must be
neutralised on every axis it can lie on.**

**V168 `[SPOT-THE-ISSUE]`** The needs-block drawer opened once per session;
the catch-up drawer reopened forever. Same SlidePanel, same card family —
localise the difference and name the rule it re-proves.
**A:** Routing. The strip pill opens through the fingerprint gate
(`m_drawerMode = 1; refresh()`), and the drawer's `closed()` handler reset
the mode WITHOUT invalidating the print — so the next click recomputed the
identical print and refresh early-returned before `fillDrawer`. The
catch-up chip is immune twice by design-accident: it calls
`fillDrawer + open()` directly, and its print includes a drawer-open flag,
self-healing on close. The rule is V158 verbatim — state affecting cached
rendering must invalidate the cache — now proven in a second widget: any
async handler (`closed()`, a timer, a signal from elsewhere) that touches
rendering state must clear the print or route through a setter that does.

---

## v26.8 — the default is the design (design-addendum-catch-up §C, revised)

**V169 `[EXPLAIN-THE-TRADEOFF]`** The look-back setting existed from day
one, with 3 days available in the combo — yet the owner still hit a
46-block wall and then *requested the feature that already existed*. What
does that prove, and why is 3 the right default rather than just a smaller
number?
**A:** It proves **defaults are design**: a knob nobody has turned yet IS
the product, and the intimidating pile arrives before anyone goes looking
for a setting — so the setting's existence protected no one. 3 is right on
the feature's own terms: catch-up recovers the *recent* past (yesterday,
the weekend); a week-old missed block is re-planned from scratch, never
honestly "rescheduled", so listing it pads the pile without adding
decisions anyone will take. And 3 loses nothing: past-horizon blocks stay
`Unset`, and temporarily widening the setting recovers them (§K.3). One
migration subtlety: users who ever pressed OK have 7 *stored* — a changed
default only reaches those who never chose, which is exactly the population
a default is for.

**V170 `[SPOT-THE-ISSUE]`** Lowering the default from 7 to 3 broke exactly
one test — the bulk-undo one. Why that test, and what's the testing lesson?
**A:** Its four blocks sat at now−1…now−4: the fourth silently fell outside
the new horizon, so "Skip all" (which carries only the ids it *rendered* —
V135) never touched it, and the undo assertions would fail for a horizon
reason disguised as an undo reason. Fixed by keeping all four blocks inside
3 days (two on the same day). Lesson: **a test that straddles a default is
a test *of* the default** — pin the values a test actually depends on
inside the invariant it means to exercise, or every defaults change becomes
an archaeology dig through unrelated failures.

---

## §L — the retrospective itself (design-addendum-catch-up §L)

**V171 `[EXPLAIN-THE-TRADEOFF]`** The catch-up feature ran ~6× its two-slice
estimate, yet §L argues the derail was mostly correct. Give the three
justifications and the one honest exception.
**A:** Justified because (1) the owner was daily-driving it — live usage is
the highest-value bug generator, and parking fresh reports ships a feature
its own user distrusts; (2) every report was small but blocking (apparent
data loss, an unopenable drawer), and severity outranks roadmap; (3) the
fixes compounded into doctrine that immediately caught second bugs (V158 →
V168, the two-axis hint rule). The exception: the v26.7.1–.4 layout
ping-pong — four rounds of *visual* bugs invisible to a programmatic test
suite — should have been one consolidated pass behind a screenshot harness.
**When a bug class is invisible to your tests, the next fix is a new kind
of test, not a next patch.**

**V172 `[MC]`** §L sorts twelve versions into a nine-item taxonomy. Name any
four categories and the estimation lesson the split teaches.
**A:** E.g.: real data breaks assumptions; cheap actions need cheap
reversals; hidden must be distinguishable from gone; layout physics; latent
debts called in by new layouts; deployment is part of the feature; defaults
are design. The lesson: the domain core was ~a fifth of the work and needed
zero fixes — **user-facing features are estimated by iteration count
against reality, not by the size of their logic**, and the tail's
categories are checklist-able in advance even when their instances aren't.

---

## v26.8 audit — documentation, diagrams, and the limits of a static_assert

*Added by the documentation & diagram audit. Unusually, several of these are
questions about the **repo's own hygiene** rather than its code — they earn a
place because every finding was a design failure of the same kind, and that
kind generalises past this project.*

**V173 `[RECALL]`** What does `app_architecture` show that no other diagram in
the folder does, and why was its absence a real gap?
**A:** The whole system in one frame: the seven stacked pages, the model/view
layer, the pure/wire split, persistence, and the single `changed()` signal that
drives every repaint and every save. Every other diagram is a close-up of one
arc — twenty-nine close-ups and no establishing shot, so anyone opening the
repo cold had to infer the system's shape from the file listing. A diagram set
without an overview is a photo album with no map.

**V174 `[MC]`** The architecture diagram draws `changed()` in green as its
emphasis. Why is one signal, rather than a family (`taskAdded`, `eventMoved`,
…), the design worth highlighting?
**A:** Because a signal family puts an obligation on *every future mutation* to
remember which member to emit, and the one that gets forgotten fails silently —
a stale view, not a crash. `changed()` cannot be forgotten. The cost is
repainting more than strictly necessary, and that cost is paid back by the
models' granular diffing (`TaskListModel::refresh` — V-series on v20.1), which
turns a coarse "something changed" into a precise `dataChanged` on the affected
rows. **Coarse signal, smart listener** beats fine signal, disciplined emitter.

**V175 `[SPOT-THE-ISSUE]`** `Version.h` carries a `static_assert` specifically
designed to stop version drift, and the file still shipped four releases stale.
Is the guard broken?
**A:** No — the guard is correct and did its job. It proves the three macros
agree with the version *string*: **internal** consistency. "Is this number the
version the work is actually at?" is an **external** fact, and no compiler has
access to it. The lesson is the generalisable one: when a compile-time check
retires a class of bug, write down explicitly what it still cannot see, or the
check's existence will be mistaken for coverage it never had.

**V176 `[EXPLAIN-THE-TRADEOFF]`** `Version.h` has three consumers. Two never
drifted; one drifted five releases. Explain the difference and the rule.
**A:** C++ code and `ticktimer.rc` both `#include` the header — the number is
*derived*, so it cannot be wrong. `installer/ticktimer.iss` cannot include a C
header (Inno's preprocessor), so a human retypes it, and it sat at `21.2.0`
while the app was at `26.8.0`. The line even carried a comment shouting
`MUST match … bump BOTH, every release`. **Mechanism beats intention; a comment
is not a mechanism.** It is the same derive-don't-store rule the domain code
follows, applied to the build.

**V177 `[WHAT-WOULD-BREAK]`** Why did the stale installer version matter beyond
tidiness?
**A:** Inno uses `AppVersion` for upgrade detection. An installer announcing
`21.2.0` over an installed `26.x` is not unambiguously an upgrade, so the
install-over-the-top path — the one path every returning user takes — was being
tested against a false version. A wrong number in a build script is a behaviour
bug wearing a documentation costume.

**V178 `[DESIGN]`** The audit worked out a mechanical fix for the installer
(read the version back off the built exe, which `ticktimer.rc` already stamps
from `Version.h`) and then deliberately shipped it **commented out**. Defend
that.
**A:** The fix cannot be exercised without Windows and Inno, and `ticktimer.iss`
is the last artifact between the project and a user's machine. An untested edit
there converts a documentation cleanup into an outage — a strictly worse
outcome than the drift it fixes, which is now at least *correct* and *labelled*.
The general rule: **the blast radius of a change sets the evidence you owe
before making it**, and "obviously right" is not evidence. It is enabled in a
session where `ISCC` can actually be run.

**V179 `[SPOT]`** Four diagrams shipped as canonical documentation with no
`.png` and no `.svg`. What does that fact prove about the review process, and
which arc did it hit?
**A:** It proves nobody had *looked at* them — a rendered diagram is the only
evidence a diagram was reviewed. It hit the entire v26.1/v26.2 arc
(`settings_pages`, `catch_up_ladder`, `catch_up_surfaces`,
`catch_up_chip_states`) — the most heavily documented feature in the repo,
which is the point: thoroughness in prose does not imply the pictures were ever
opened.

**V180 `[HANDS-ON]`** Write the two shell checks now recorded in
`diagrams/README.md`, and say what each would have caught.
**A:**
```sh
for f in *.puml; do b=${f%.puml}; [ "$b" = _style ] && continue; \
  [ -f "$b.png" ] || echo "UNRENDERED: $b"; done
for f in *.puml *.mmd; do b=${f%.*}; [ "$b" = _style ] && continue; \
  grep -q "\`$b" README.md || echo "UNINDEXED: $b"; done
```
The first catches the four never-rendered diagrams; the second catches the six
unindexed ones, including `chat_turn_flow` — the whole v25 chat picture,
invisible since the day it was drawn. Note the second uses a *prefix* match:
the planning diagrams are indexed as `` `name.*` ``, and an exact match would
report two permanent false positives, which is how a check gets ignored and
then deleted.

**V181 `[EXPLAIN-THE-TRADEOFF]`** The audit found 15 live Mermaid files against
a six-version-old claim of "Mermaid retired," and converted only 6. Why not all
15, and why not zero?
**A:** The six converted shared a defect the other nine don't: they were indexed
as canonical *and had never been rendered once*. That is broken documentation.
The nine survivors render, and their outputs are current — "uses the older of
two working tools" is debt, not a defect, so it was **named and counted** in
the index rather than fixed in a hurry or left as folklore. Converting zero
would have left six unreadable diagrams; converting fifteen would have spent a
session's budget on a cosmetic toolchain sweep during an audit whose actual
findings were two version bugs.

**V182 `[MC]`** The superseded `.mmd` files were archived under
`legacy-mermaid/` instead of deleted. Which existing project decision is this
the same shape as?
**A:** `JsonStore::migrateLegacyData`, which **copies, never moves, never
overwrites**. A conversion is a rewrite, and a rewrite can silently lose a
detail nobody notices for months; the original is the only way to settle "did
it used to say X?" A few kilobytes buys that answer permanently.

**V183 `[WHY]`** `window_memory_restore.mmd` became *two* PlantUML diagrams.
Why was one not enough?
**A:** The Mermaid original drew the restore path and the save path in a single
flowchart with two disconnected entry points. PlantUML's activity syntax cannot
express that, which forced the question — and the honest answer was that they
were always two independent stories that happen to share a preference key.
**A notation refusing to draw something is sometimes a review comment**: the
constraint surfaced a conflation the freer tool had allowed to persist.

**V184 `[SPOT-THE-ISSUE]`** The README simultaneously claimed `v27.0` and
`v26.8`, `292 tests` and `267 tests`, `60 domain tests` (actual: 102) and
`format v6` (actual: v11). What single practice produced all four, and what
replaced it?
**A:** Hardcoded totals repeated across the file — a fact stored in three places
is a fact that will hold three values. Replaced with per-suite figures taken
from source plus *"run `ctest` for the number of the day."* **Derive, don't
store — the same rule the domain enforces, applied to prose.**

**V185 `[EXPLAIN-THE-TRADEOFF]`** The audit found the v27.0 subtask code absent
from the tree while its documentation described it as shipped. Defend keeping
the documentation.
**A:** The two errors are not symmetric. If the code exists on the owner's disk
and the docs are deleted, a real design record is destroyed to fix a
bookkeeping error. If the code truly never landed and the docs stay, the cost is
a warning label — which is what both the README status line and the iteration
plan now carry, along with "resolve before v27.1 or v28," since both build on
fields that may not exist. **When two corrections disagree, pick the reversible
one.**

**V186 `[MC]`** During the audit the question bank and the design addenda were
found *current*, while session notes, version numbers, the README status line
and the diagram index had all drifted. What distinguishes the two groups?
**A:** The current ones are written **as part of the work** — the addendum is
where the design gets thought through, and the bank entry is written while the
reasoning is live. The drifted ones are all **"update at the end"** artifacts,
and the end is exactly where attention is thinnest, especially across twelve
same-day drops with no natural stopping point. The design consequence: put
bookkeeping *inside* the ritual (hence the new shipping checklist) rather than
after it.

**V187 `[WHAT-WOULD-BREAK]`** The new shipping checklist's first item is "bump
`Version.h`." What breaks if it's skipped, given the app compiles fine either
way?
**A:** Auto-update compares the running app's version against the server's
answer, so a stale header makes the app misreport itself — it can offer an
update it already has, or fail to offer one it needs. It also stamps the wrong
`FileVersion` into both `.exe`s through the `.rc` files, so Windows' own
properties dialog lies. Nothing fails loudly, which is precisely why it needed
a checklist rather than a comment.

**V188 `[EXPLAIN]`** Summarise the one finding shared by every bug in this
audit.
**A:** **A claim that no mechanism checked.** The version string had a
`static_assert` for the half that could be automated and drifted on the half
that could not. The diagram index had no check and lost six entries. "Mermaid
retired" had no check and survived six versions as a plain falsehood. The README
totals had no check and reached three different values. Documentation does not
rot from carelessness — it rots because **prose has no compiler**, so the work
is to keep finding the places where a five-line shell loop can serve as one, and
to write down honestly what is left over for a human.

---

## v28.0 — affordability: the pipeline before the model (design-addendum-affordability)

**V189 `[RECALL]`** Name the four affordability verdicts and the rule for why
it's four values rather than a score.
**A:** NotApplicable (no deadline/done/archived — the question has no
meaning), Unknown (deadline but no blocks ever planned), Comfortable, Tight.
No score because the inputs carry ~half-hour precision — a percentage would
perform an accuracy the proxy doesn't have. The verdict ships WITH its
numbers (`Report`), so the user gets evidence, not just a conclusion.

**V190 `[EXPLAIN-THE-TRADEOFF]`** The owner's "volunteer" answer to §O.1
reshaped the slice. Trace the consequence chain.
**A:** Answer-only affordability is a pure query. Volunteering makes it a
*nudge*, which needs a trigger, a surface, and manners — §F's pipeline. So
the slice was re-cut: ship that whole pipeline in 28.0 with the sentence
written by C++, and let 28.1 swap exactly one box (the sentence-writer).
That turns §A's "the model is an enhancement layer, never a dependency"
from a rule into structure: the fallback isn't a degraded mode bolted on —
it's the thing that shipped first, so violating the corollary is impossible.

**V191 `[MC]`** Why does `Unknown` exist as a verdict, and why does it never
toast?
**A:** §H.3 — the app knows what was *done*, never how big the work *is*;
with no blocks ever planned there is no proxy, and the honest output is "I
can't tell how much is left" rather than performed confidence. It never
toasts because the volunteer channel carries news of trouble only: "I don't
know" as an interruption is noise. The sentence still exists — chat serves
it on request in 28.1.

**V192 `[SPOT-THE-ISSUE]`** A teammate simplifies the `slipping` rule to
"behindOwnPlan ⇒ Tight", deleting the `daysLeft ≤ nearDays` half. What
regresses, and which test catches it?
**A:** Every task you're behind on becomes Tight regardless of deadline — a
skipped 2h block on a due-in-two-weeks essay would toast on Monday morning.
Being behind with two weeks of runway is a Tuesday problem, not an
interruption; the change converts the secretary into a nag, which for this
tool means "gets muted" = feature death.
`affordabilityBehindOwnPlanTripsOnlyNearTheDeadline` pins exactly this pair:
same shape far → Comfortable, near → Tight.

**V193 `[EXPLAIN-THE-TRADEOFF]`** The nudge bookkeeping lives in QSettings,
not `data.json`. Defend that against the project's own "facts go in the data
file" line.
**A:** Because it isn't a fact about the user's life — "what did I last say,
and how many times today" is *manners state*. Losing it costs at worst one
repeated heads-up. Syncing it would be actively wrong: a nudge shown on the
laptop must not mute the phone in the owner's hand — courtesy is
per-device. Bonus consequence: no format bump, so 28.0 stays clear of the
v11 numbering collision the audit recorded, which still gates 28.2's mood
storage.

**V194 `[MC]`** The service stores the last verdict when it *spoke* or when
the verdict is un-Tight — but stores nothing for a Tight suppressed by
quiet hours or the cap. Why is "nothing" the correct write?
**A:** The change-of-verdict rule compares against the last *delivered*
state. A suppressed Tight was never delivered — the news is still owed —
so recording it would mark it old before anyone heard it, and the heads-up
would be lost until the verdict cycled through Comfortable again. Storing
nothing means the next sweep outside the suppression gets to say it. The
silent un-Tight write is the re-arm: it's what makes a *future* Tight news.

**V195 `[WHAT-WOULD-BREAK]`** `TaskListModel::refresh()` now diffs a verdict
map itself and emits `dataChanged({AffordabilityRole})`. What breaks if you
delete that pass and rely on the snapshot base's diff?
**A:** The pill goes stale in exactly the interesting case: verdicts move
when *events* change (a block added, focus tracked, the calendar filling
up) while the Task rows stay byte-identical — and the base diffs Task
fields only, so it emits nothing. The card would show TIGHT (or not) until
some unrelated repaint. The model-level lesson: a role derived from data
*outside* the snapshot needs its own change detection, because the base
class can only diff what it snapshots.

**V196 `[DESIGN]`** "Build the joints, not the motion." List the four toast
seams and state the discipline that kept animation out of 28.0.
**A:** (1) `ToastSpec` — the toast as a value, so new kinds are new values
not new overloads; (2) kind-driven accent — colour promoted from a paint
constant to data; (3) `moveTo(QPoint)` — every position change through one
method whose body is one `move()` today and can grow a `QPropertyAnimation`
with zero caller churn; (4) the optional action row — empty text ≡ the old
toast, first consumer is §G.1's check-in. The discipline: YAGNI is right
about the *motion* (speculative, no consumer) and wrong about the *joints*
(cost nothing, and they're the difference between animation being an
afternoon and a refactor). No easing curves shipped.


**V197 `[EXPLAIN-THE-TRADEOFF]`** The v27 drop half-landed and nobody noticed
for weeks. The owner called it their mistake. Make the process argument
instead, and name the mechanism that fixes it.
**A:** Same shape as the installer bug (V176): `Version.h`'s three consumers
showed that the *derived* copies never drift and the *hand-applied* one
drifted five times — and applying a drop by unzipping is another
hand-applied step with no check. Discipline was never the missing piece; a
net was. The mechanism: every drop already bumps `Version.h`, so
`grep VERSION_STRING include/Version.h` after unzipping proves the drop
landed in five seconds — checklist item 6, printed in every CHANGES file.
Corollary worth keeping: when a human error repeats, look for the check
that would have made it impossible, not the person who made it.


---

## v28.1 — the model phrases the nudge (affordability addendum §K)

**V198 `[MC]`** `NudgeClient` is the third use of quick-add's one-shot
recipe, with one deliberate personality change. Name it and justify it.
**A:** It cannot fail loudly — no key, unreachable, timeout, 401, garbage,
essay all collapse into one `fallback()` signal. Quick-add fails into a
preview bar and chat into a ⚠ bubble because a human is *watching* those
surfaces, having just acted; a nudge fires unattended, so an error message
would be a notification about a notification. Errors go to the debug log;
the owner just gets the deterministic sentence and never learns a network
call existed.

**V199 `[EXPLAIN-THE-TRADEOFF]`** The accept gate rejects over-long replies
instead of truncating them, and validates shape but never tone. Defend both.
**A:** Truncation puts *our* ellipsis in *its* mouth — a cut-off sentence
reads as the model trailing off, worse than the plain C++ voice that is
guaranteed correct. And tone (did it forbid? did it shame?) cannot be
judged mechanically; machine-judging it would be a second model call to
check the first. The tone rules live in the locked prompt bands above the
persona — restated verbatim so the prompt is safe travelling alone — and
the *guarantee* lives in the fallback, which is the §A corollary doing its
job: the model can only make a nudge sound more human, never make it fail.

**V200 `[WHAT-WOULD-BREAK]`** Bookkeeping (cap bump, verdict store) moved
from decision time to `deliver()`. What breaks if it stays at decision
time, and what is the recorded cost of the move?
**A:** At decision time, the count and last-verdict are written seconds
BEFORE any toast exists — a crash, a superseded request, or a failure path
that never delivers would still consume a slot of the daily cap and mark
the Tight as "already said", silently eating an owed heads-up. The
recorded cost: quiet hours were checked at decision time, so a 21:59:58
request can deliver at 22:00:04 — two seconds of tardiness, chosen over a
re-check that would swallow delivered-but-late news.

**V201 `[SPOT-THE-ISSUE]`** A reviewer suggests the service call
`chat::configuredPersonaBand()` directly instead of taking an injected
`std::function` — "it's one include." What breaks, and where is the
tell?
**A:** `AffordabilityService.cpp` sits in the app's sources while its pure
core is exercised by `test_domain` — and the direct include drags
`ChatSession` (and via the client, Qt Network) toward `DOMAIN_SOURCES`,
whose own CMake comment celebrates that the two pure AI layers live in
separate suites, "neither able to drag the other's dependencies in." The
tell is that the fix is one `std::function` with a valid default (an empty
band is a complete prompt — the locked rules alone). The include graph is
architecture; a one-line convenience that adds a link edge between test
targets is not one line.


---

## v28.2 part 1 — mood & the check-in gate (design-addendum-checkin)

**V202 `[EXPLAIN-THE-TRADEOFF]`** Mood went into `data.json` (format v12);
the nudge ledger went into QSettings. Same storage question, opposite
answers — defend both.
**A:** The test is "fact about the life, or courtesy of the interface?"
Mood is a fact: §G.2's line is that the phone's assistant should know what
the laptop's does, so it syncs, costs a format bump, and gets a migration.
The nudge ledger ("what did I last say, how many times today") is
courtesy: losing it costs one repeated heads-up, and syncing it would be
actively wrong — a laptop's nudge must not mute the phone in the owner's
hand. A rule of thumb gives one answer; a real criterion can give two.

**V203 `[MC]`** Why is mood coarse (three levels) with one-per-day upsert,
rather than a 1–10 scale appended to a log?
**A:** Pattern work ("Wednesdays are rough") needs comparable values, and
a ten-point scale invites precision a 07:40 tap can't honestly deliver —
false granularity poisons the pattern it exists to serve. Upsert because a
check-in answers "how is today": re-answering replaces, so duplicates are
impossible by construction rather than prevented by cleanup — the same
argument as the domain's overlap gate.

**V204 `[SPOT-THE-ISSUE]`** A reviewer moves the 14-day trim out of
AppData into the (future) mood widget "since that's where mood lives on
screen." What breaks?
**A:** The retention promise becomes a UI accident: any path that never
opens that widget — headless sync, the server, a future phone build —
accumulates mood forever, silently turning a check-in into a dossier.
Trimming is a DOMAIN rule on the midnight knock, beside expireDismissals,
and it emits changed() because forgetting is also a write the autosave
must record. A promise about data lives behind a domain door or it isn't
a promise.

**V205 `[WHAT-WOULD-BREAK]`** The gate takes `lastOffered` as a parameter
instead of reading QSettings itself. What does that buy, and what's the
precedent?
**A:** The once-a-day rule becomes a pure assertion — five test lines pin
today/yesterday/window-edges with no settings file, no clock, no cleanup.
Precedent: afford::decide takes spokenToday/lastSpoken the same way, and
ReturnPolicy::nextReturn takes `from`. The wire owns reading and writing
the fact; the pure layer owns the judgment — QSettings at a pure boundary
is a hidden global, and hidden globals are why tests need cleanup.


---

## v28.2 part 2 — the surfaces & the leak (checkin addendum §G–§K)

**V206 `[SPOT-THE-ISSUE]`** Part 1 shipped a privacy violation that part 2
had to fix. Name it, and name the general lesson about rules in planning
docs.
**A:** The MOOD briefing line was unconditional, and the briefing rides to
whatever seat chat uses — including cloud — while §E.4 says mood never
leaves the machine. The rule existed only as prose in a planning addendum,
and prose did not survive one implementation session that never re-read
it. Rules that matter get MECHANISMS: `includeMood` defaults false (the
failure mode chooses the safe side) and `ai::isLocal` gates the opt-in to
all-local routes. A privacy rule without a default and a predicate is a
hope.

**V207 `[EXPLAIN-THE-TRADEOFF]`** `lastOffered` is marked at EMIT; the
nudge pipeline's bookkeeping (V200) moved to DELIVERY. Opposite choices —
reconcile them.
**A:** Each marks at the moment its promise is about. The nudge's promises
(the cap, change-of-verdict) are about SPEECH the owner received — marking
before delivery counts speech that may never happen. The check-in's
promise is once-a-day ASKING — and a dismissed toast was still an ask;
marking at tap would re-knock every ten minutes at someone who answered
"not now" with their thumb. Same question ("when is the fact true?"),
answered by what each fact means, not by a house convention.

**V208 `[MC]`** Why is the check-in exchange built from buttons and C++
text with no model call, when 28.1 just built model phrasing?
**A:** Three reasons stacking: a 07:40 check-in must cost one tap, and a
question with three answers needs no generation (§A: the model only
phrases, and here there is nothing to phrase); §G.3 says what lands is
evidence, which C++ can cite from data as well as a model can; and §E.4's
"local, always" is satisfied by SUBTRACTION — no conversation exists to
route, so the rule cannot be violated. The model joins when per-role
primaries exist, and joins a feature that already works — the §A corollary
for the third time running.

---

## v28.3.0 — subtasks & sizing (the re-land)

**V209 `[recall]`** Where does the parent↔piece link live, and in which
direction does it point?
**A:** On the **child**: `Task.parentId`, pointing up. The parent holds no
list of children. Three reasons in `Task.h`: the flat `m_tasks` vector
survives (forty-odd queries learn one boolean instead of recursion);
removal can't corrupt the shape (no embedded lists to dangle into); and it
round-trips as one JSON string key, so storage/sync/share get it free.
"Children of X?" becomes a scan — the codebase's standard answer to
reverse questions (`movedToId` precedent), because scans cannot drift.

**V210 `[tradeoff]`** Why exactly one level of nesting, and where is that
enforced?
**A:** Product reason: checklists are flat in how people think, and deeper
trees turn the detail panel into a tree view and the five policies into
recursive questions. Enforcement: `AppData::addSubtask` refuses a parent
that `isPiece()` — the type system can't express the rule (a Task is a
Task), so the door does, same as time-without-a-date. Belt and braces:
`resetFrom`'s orphan adoption also un-nests hand-edited files.

**V211 `[architecture]`** Name the five query policies and the question
each answers.
**A:** `upcomingTasks` and `tasksIn` = **parents only** ("what is my
workload?" — pieces would double-count their parent). `tasksDueOn` =
**dated pieces included** ("what is due this day?" — a dated piece is a
real obligation there). `taskCountIn` = **everything** (it *guards*
`removeCategory`; undercounting deletes data — and the category chip shows
the guard's number so two answers can't disagree). Completion = **no
roll-up either direction** (the tick is the reward). The queries disagree
*on purpose*: different questions, different truths.

**V212 `[spot-the-issue]`** A piece is ticked. Why would the Upcoming
card's "2/5" chip NOT repaint if the model relied on its base-class diff —
and what fixes it?
**A:** The base diff compares the snapshot's **Task fields**, and ticking
a piece changes bytes of the *piece*, which isn't a row — the parent's
Task is byte-identical, so the diff stays silent. Fix: the pieces sidecar
(`m_pieces`, per-parent `(done,total)`), recomputed each refresh and
diffed by the model itself, emitting `dataChanged` with the two pieces
roles. Same pattern, same reason as the v28 verdict cache — that's why
they sit side by side in `refresh()`.

**V213 `[recall]`** What does `estimateMinutes == 0` mean, and how must
readers ask?
**A:** "Unset", never "instant" — via `Task::hasEstimate()`, so nobody
treats an unanswered question as a five-minute job. It's an int of
minutes (not QTime) because estimates are arithmetic — summed, subtracted,
soon multiplied by §J.2's coefficient — and honesty about absence matters
precisely because §J.2 will divide by these numbers.

**V214 `[tradeoff]`** The dialog constructor GREW by two parameters
instead of gaining optional seed-setters. Defend that against "9-arg
constructors are ugly."
**A:** An optional seed a call site forgets silently saves 0/"unset" over
the user's real estimate — data loss with no symptom. Growing the ctor
makes every stale call site a **compile error**: the v22 `updateTask`
argument verbatim ("a compile error is the cheapest bug report"), and it
fired four times during the drop itself. Pieces, by contrast, seed via an
optional call — forgetting *that* only hides a section and applies as
"nothing to do", because the apply helper never deletes by omission.
Optionality tracks blast radius, not convenience.

**V215 `[architecture]`** `applyTaskDetailAnswers` and
`seedTaskDetailPieces` are free functions, not dialog methods. Why, and
what contract survives?
**A:** The dialog's contract is *pure question* — it mutates nothing and
stays `AppData`-free, which keeps it testable without a store and keeps
AppData the single door. But four call sites were about to quadruple a
nontrivial diff-and-apply loop (how new/ticked/✕ed pieces become
mutations). The pair lives *next to* the class as caller code: seed from
data, exec, apply to data — with the apply inside one `AppData::Batch`, so
N mutations paint once.

**V216 `[recall]`** What does `AppData::Batch` do, and why RAII instead of
`beginBatch()/endBatch()`?
**A:** While one is alive, every mutation's `notifyChanged()` sets a dirty
flag instead of emitting; the outermost destructor emits **once, iff**
anything changed (an all-no-op batch emits nothing — idempotence kept at
group level). RAII because a forgotten `end()` after an early return or
exception leaves the app permanently silent — a repaint-less UI with no
error is a horrible bug to find, and a destructor cannot be forgotten
(`std::lock_guard`'s instinct). Enforceable only because *no* direct
`emit changed()` remains in the file: one gate, no holes.

**V217 `[spot-the-issue]`** Before this drop, what would completing a
repeating PIECE have done, and what pins the fix?
**A:** The repeat-spawn copied title/category/description/priority/dates —
but not `parentId` — so the next occurrence would be born **top-level**: a
checklist line silently promoting itself to a full task every cycle. The
spawn now carries `parentId`, `estimateMinutes` and `chunkable`;
`repeatingPieceSpawnsAsAPiece` pins all three.

**V218 `[what-would-you-change]`** A sync delivers a piece whose parent
was deleted on another device. Walk the failure the design fears and the
mechanism that prevents it.
**A:** Feared failure: **data loss by invisibility** — the dangling piece
is hidden by every list surface (parents-only) yet still counted by the
guards, so a category can refuse deletion over a task nobody can see.
Mechanism: orphan adoption in `AppData::resetFrom` — the one door ALL
loaded data enters (file, sync, share) — promotes it to top level. Two
passes (judge everyone against loaded state, then fix) so one adoption
can't change the next verdict; `JsonStore` can't do it because it converts
one record at a time and referential repair needs the whole picture.

**V219 `[diagram]`** In `diagrams/subtask_policies.*`, why do the two
"workload" surfaces and the calendar-day surface sit on opposite sides of
the piece?
**A:** The diagram's point is that the SAME piece flows to different
answers by policy, not by accident: workload queries filter it out
(double-counting), the day query admits it when dated (real obligation),
and the guard beneath counts it regardless (protection). One fact, three
truthful projections — the picture form of §D's "disagree on purpose."

**V220 `[explain]`** The archive cascade is *unconditional* on restore —
even a piece the user archived by hand before archiving the parent comes
back. Defend it.
**A:** "Bring Lab 4 back" means the whole checklist. The alternative —
remembering which pieces were hand-archived and skipping them — makes a
restored task look like it lost lines (indistinguishable from data loss),
to save the user one click of re-hiding. Asymmetry with delete is
deliberate: archive is reversible so its cascade is generous; delete
isn't, so its cascade is honest about blast radius (and demotes titles
into any block that referenced *any* removed id).


**V221 `[tradeoff]`** The deploy script's apply check compares Version.h
against the installer's `AppVersion` instead of hardcoding the expected
version. Why, and what failure can it still miss?
**A:** A hardcoded expected version would itself need bumping every drop —
recreating the exact "bump BOTH" trap it polices. The pair-comparison is
self-maintaining: both files ship in every drop, so a half-apply that
updated one and not the other is caught mechanically, forever, with no
per-release edit. What it can miss: a *fully* stale tree (unzipped into
the wrong folder entirely) is internally consistent — both files agree on
the OLD number. That's why the check also prints the version loudly: the
mechanical half is the comparison, the human half is one eyeball against
the drop's filename, which carries its version on purpose.


**V222 `[spot-the-issue]`** v28.3.0's first build failed in every
translation unit with `redefinition of 'struct PieceCount'`. What C++
rule fired, why did EVERY file break rather than one, and what was the
process failure behind it?
**A:** The One Definition Rule: a class/struct may be defined once per
translation unit, and `Task.h` and `AppData.h` each carried a definition
— since `AppData.h` includes `Task.h` (via Event.h), every file including
`AppData.h` saw both, which is essentially the whole app; hence the
everywhere-at-once failure. Process cause: the re-land surveyed the
half-applied tree by grepping for the symbols it EXPECTED
(`parentId`, `estimateMinutes`, …) and never read `Task.h`'s tail, where
the earlier session had parked `PieceCount` and an unused `SubtaskEdit`.
Fix kept one definition (in `AppData.h`, beside the query that fills it)
and deleted the dead type. Lesson: grep answers the question you asked;
reading the whole inherited file answers the ones you didn't.


**V223 `[spot-the-issue]`** Fifteen different test_ui tests failed on the
suites' first-ever real run — settings dialogs, window geometry, a gate
rearm, chat fallback routes. How do you spot that this is ONE bug, and
what was it?
**A:** Read the failures for a shared SIGNATURE, not fifteen stories:
every one reduces to "a QSettings value written (or planted) came back
empty/default" — dialogs wrote on OK and reads saw defaults; tests that
PLANTED settings to arrange a scenario (a stale review clock, a custom
endpoint, a bogus route) found their arrangement missing, so the widget
under test behaved as if unarranged. Then difference the suites: test_nlp
does the same kind of settings work and passed — it sets BOTH
QCoreApplication identity names; test_ui set only the application name.
Default QSettings() is scoped by organization AND application; with the
organization empty, the owner's Qt/Windows setup persisted nothing. One
initTestCase setting both names fixed all fifteen. Lesson: failures that
share a signature share a cause — diagnose the signature, not the list.


**V224 `[spot-the-issue]`** A test showed the Settings dialog, switched
the provider combo to "custom", and asserted the address row became
visible — and it could never pass, while its sibling assertion
(`!isVisible()` for a known vendor) always passed. What was wrong, and
what's the general lesson about the passing half?
**A:** The AI section lives on the Assistant page of a QStackedWidget,
and the dialog opens on page 0. Qt's `isVisible()` is effective
visibility — a widget is visible only if every ancestor is — so
everything on a non-current page reports invisible regardless of its own
flag. The positive assertion was impossible; the negative one passed
VACUOUSLY, proving nothing about the row. Fix: navigate to the page
first (by nav title, so reordering can't re-vacuum it). Lesson: for any
passing negative assertion, ask "what would make this fail?" — if the
answer is "nothing reachable from this setup", it isn't testing what it
claims. Its twin lesson from the same run: the offscreen platform has a
real 800x600 screen, and `restoreGeometry()` clamps to it — geometry
tests must plant sizes that fit.


**V225 `[explain]`** The geometry test failed twice with two different
planted widths (870 -> 798, then 780 -> 798). What does the CONVERGENCE
to 798 tell you, and what should the assertion have been all along?
**A:** When the output is the same regardless of the input, the output
isn't a function of your input — something else owns it. Here two
different owners produced the same 798: restoreGeometry() clamped 870
down to the offscreen screen (800x600), and the resize itself clamped
780 UP to the main layout's computed minimumSizeHint (~798 — the rail,
pages and margins refuse to compress further; no setMinimumSize call
anywhere, the layout derives it). Both clamps prove save/restore were
faithful all along. The assertion should never have pinned a width
literal: measure what the first window actually became (`lived =
w.size()` after the resize) and assert the restored window matches THAT.
Pin invariants you own (the round-trip); never pin numbers the platform
or layout owns.


**V226 `[what-would-you-change]`** The geometry test failed three times:
expected 870 got 798; expected 780 got 798; expected the measured 1166
got 798. Reconstruct the mechanism that explains ALL THREE runs, and
state the final form of the test.
**A:** One mechanism, three views of it. The window's layout computes a
~1166 minimum width under Qt's no-fonts fallback metrics — wider than
the offscreen platform's whole 800x600 screen. So: any resize request
(870 or 780) is pushed back up to ~1166 by the next layout pass; the
debounced save honestly records ~1166; and the next construction's
restoreGeometry() clamps that blob to the screen (798) — measured
BEFORE that window's own first layout pass would re-enforce the minimum.
798 every time, regardless of input, because the inputs never survived
long enough to matter. The save/restore wiring was correct in every run.
Final form: the test asserts its NAME — restore acted (size differs from
the untouched default) and startup did not overwrite the stored blob —
and asserts no width at all. The trilogy's lesson: when an assertion
keeps losing to the environment, stop refining the number; assert the
invariant the test is named after.


---

## v28.4.0 — sizing intelligence (§J.2)

**V227 `[recall]`** How is the personal multiplier computed, and name
three inputs that are deliberately NOT samples.
**A:** The median of actual÷estimate across every task that is (a)
done, (b) has an estimate, and (c) has tracked focus — median so one
10x disaster is an outlier to survive, not a fact to average in; a flat
1.0 below 3 samples; the result clamped to [0.5, 3.0]. Non-samples: an
unfinished estimate (no "actual" exists yet), a finished task with no
estimate (nothing to divide by), and a finished estimate with zero
tracked focus (finished off the books — no measured actual). Derived,
never stored: recomputed from history each call, so it cannot go stale.

**V228 `[explain]`** Before v28.4, an estimated task with no planned
blocks was Unknown. Why was that right THEN and wrong NOW?
**A:** Unknown means "I cannot size the work". Pre-§J.1 the only sizing
source was the user's own planned blocks — with none, honesty required
Unknown (performing confidence is a bug). §J.1 added a second source:
the estimate answers the question the blocks used to, so Unknown now
requires BOTH absent. The proxy survives as the fallback because an
unestimated-but-planned task is still perfectly sizeable the old way.

**V229 `[tradeoff]`** An unsized parent borrows the SUM of its pieces'
estimates. Defend the two edges: archived pieces excluded, and a
parent's own estimate outranking the sum.
**A:** Archived pieces left the checklist and the progress count
(v28.3's coherence rule: subtasksOf and pieceProgress describe one
set) — a ✕'d piece that kept weighing on affordability would make the
same task look heavier than its own panel says. And the parent's own
estimate wins because it is the owner's DIRECT answer to "how big is
this whole thing"; the piece-sum is a derivation for when no direct
answer exists — a derived number should never overrule a stated one.
The dividend: "write lab report" is paralysis-shaped and unguessable,
"read the spec, 25min" is easy — size the pieces, the parent is sized.

**V230 `[architecture]`** Why does affordability() take the multiplier
as a defaulted parameter instead of always computing it internally?
**A:** The rate is a fact about the USER's whole history, identical for
every task — a sweep over N tasks computing it N times would be N
identical scans. The -1 sentinel keeps every existing call site and
test compiling unchanged ("compute it for me"), while
AffordabilityService and DayBriefing compute once per sweep and pass it
down — the verdict-cache reasoning, one layer lower.


**V231 `[spot-the-issue]`** The multiplier's median test read 1.0 —
"not enough samples" — despite three carefully constructed sample tasks,
while every OTHER new v28.4 test passed. Where had the samples gone, and
which two testing habits would have caught it at the cause?
**A:** The fixture's 05:00 blocks were refused by `isFree`'s day-window
guard (blocks start no earlier than 06:00) — `addTaskEvent` returned
empty ids, `appendSegment("")` quietly no-opped, tracked focus was zero,
and zero valid samples correctly yields 1.0: the query under test was
right the entire time; the fixture never existed. Habit one: when
inventing fixture values, read what the passing tests use and ask why
before deviating (they all used 09:00 blocks). Habit two: QVERIFY every
domain door's return inside fixture loops — the doors' refusals are
verdicts, and an unchecked refusal surfaces as a mystery far downstream.
The satisfying part: the domain's gates guarding against its own test
suite is the gate design working exactly as specified.


**V232 `[design]`** v28.5 made a piece's title clickable to open its own
panel. Why does clicking through ACCEPT the dialog rather than reject it,
and what named test pins this?
**A:** Accept-vs-reject encodes intent: "I'm going somewhere else" and
"undo my edits" are different wishes, and only Cancel means the second.
A tick made just before the hop must survive it — rejecting would
silently discard every edit of the sitting as the price of navigation,
punishing exactly the natural workflow (tick two pieces, open a third).
Pinned by `navigatingAwayStillSavesTheSitting`: tick piece one, click
piece two's title, verify the tick still reads from chosenPieces().


**V233 `[architecture]`** The dialog never opens the piece's panel
itself — it records `navigationTarget()` and a free function
`runTaskDetail` acts on it. Why not just open the next dialog from
inside the click handler?
**A:** The pure-question contract: a dialog gathers an answer and
mutates nothing — and navigation is a mutation of the SESSION, driven by
answers that must be applied first (by the caller, through AppData's
single door). Opening from inside would also nest exec() event loops —
each ancestor dialog alive and modal behind the current one — where
runTaskDetail's LOOP closes one panel before opening the next: flat
stack, one dialog alive at a time, and one place to swap modality when
the side-panel slice lands.


**V234 `[tradeoff]`** runTaskDetail re-reads the task from AppData at
every hop instead of carrying snapshots, which let all four call sites
delete their defensive `const Task snapshot` copies. What did those
snapshots guard against, and why is re-reading strictly better here?
**A:** They guarded dangling: `task` references point into AppData's
vector, which may move its elements on the next mutation, so handlers
copied plain values at connect time. But a snapshot taken at CLICK time
goes stale the moment the previous hop's apply runs — re-reading by id
at open time gets both properties at once: nothing held across
mutations (no dangling) and nothing older than the last apply (no
staleness). The id is the only thing safe to capture long-term, which
is the same reason the domain links pieces by parentId, not pointers.


**V235 `[what-would-break]`** A newborn checklist line (typed this
sitting) renders as a plain label — no door. What breaks if you make it
a button like the others?
**A:** Navigation targets are task IDS, and a newborn has none until
applyTaskDetailAnswers calls addSubtask at Save. The button could only
record an empty target (a no-op hop that closes and reopens the same
panel — baffling) or force an early partial apply from inside the
dialog, breaking the pure-question contract and splitting "when do my
edits land?" into two answers. The honest UI states the truth: it
doesn't exist yet; Save first. (`newbornPiecesHaveNoDoorUntilSaved`
pins the count: one seeded + one newborn = exactly one door.)


**V236 `[recall]`** After v28.5, what are the three free functions next
to TaskDetailDialog, and what is each one's single job?
**A:** `seedTaskDetailPieces` READS (task's pieces + display hints out
of AppData into the dialog), `applyTaskDetailAnswers` WRITES (every
answer through AppData's doors in one Batch), `runTaskDetail`
ORCHESTRATES (seed → exec → apply, looping while the answer names a
next task). The dialog stays a pure question in the middle of all
three; the fourth thing — the modal-vs-docked presentation — is
runTaskDetail's alone, which is what makes the future side-panel swap a
one-function change.


**V237 `[architecture]`** v28.6 split TaskDetailDialog into a form and
two containers. What single design question forced the split, and how do
the two containers answer it differently?
**A:** "What does a navigation request MEAN?" The form can't know: in a
modal dialog the only possible answer is record-the-target-and-accept
(the caller's loop closes this dialog and opens the next), while a
docked panel can swap its own content in place — guarded by the unsaved
prompt. So the form emits navigateRequested(id) and owns no policy; the
dialog connects it to {record; accept()}, the panel connects it
(queued) to openTask. One signal, two policies — the fields never
duplicated.


**V238 `[design]`** The panel's dirty state is a comparison
(currentAnswers vs. the markClean baseline), not a "was any field
touched" flag. What user-visible behavior does the comparison buy, and
what test pins it?
**A:** Honesty in the indicator: type a character, delete it, and the
Save button goes quiet again — a flag would stay lit and the button
would claim work that doesn't exist (and the exit prompt would then ask
about nothing). thePanelSavesExplicitlyWithFeedback pins it: edit →
enabled, retype the original → disabled, no save in between.


**V239 `[spot-the-issue]`** The panel connects navigateRequested to
openTask with Qt::QueuedConnection, and buildFor uses deleteLater on
the outgoing form instead of delete. What crash do these two lines
prevent, and why does it need BOTH?
**A:** The request is emitted from a button INSIDE the form that
openTask is about to replace — a direct connection would destroy the
sender mid-emission (use-after-free on the return path through the
clicked() handler). Queued lets the click finish before the swap runs;
deleteLater covers every OTHER path into buildFor (changed() reseeds,
saves) where the form might still be in a call stack. Either alone
leaves a path open; together every rebuild is safe regardless of who
triggered it.


**V240 `[tradeoff]`** While the panel shows a dirty form, AppData
changes underneath it (sync, another page). The panel keeps the user's
edits instead of reseeding. Defend the choice and name its cost.
**A:** The user's in-progress work is the one thing the app cannot
recompute — the synced change is at least SAVED somewhere. Reseeding
would silently destroy visible, unsaved intent to show a truth the user
didn't ask for. Cost: their eventual Save overwrites the synced change
(last-write-wins), which is the answer this app's sync already gives
everywhere else — consistency of policy over per-surface cleverness.
The clean-form case reseeds precisely because there's no intent to
protect; the gone-task case closes because a panel editing a ghost
writes to nothing.


**V241 `[recall]`** After v28.6, walk runTaskDetail's decision in one
sentence, and say why the four call sites have now survived two
redesigns unchanged.
**A:** Find the caller's top-level window; if it has a TaskDetailPanel,
hand the id to openTask and return (the panel owns the session); else
run the v28.5 modal loop. The call sites pass exactly (data, id,
window) — the minimum a detail session needs and nothing about HOW it's
shown — so both the v28.5 loop rewrite and the v28.6 container swap
happened entirely behind that signature. Depend on the question, not
the answer's shape.


**V242 `[design]`** v28.6.1 turned the docked panel into an overlay
within hours of the owner using it. Name the two user complaints, show
they are one fact, and state what the overlay trades away.
**A:** "It feels like part of the Activities panel" and "it made the
main screen smaller" — both are consequences of being a LAYOUT MEMBER:
a docked widget competes for space, so it resizes its siblings and
visually joins their row. The overlay floats in front (scrim-dimmed
background, no reflow), which buys foreground presence at the cost of
covering content while open — paid deliberately with the 220 px
keep-clear clamp and click-away close, so covered never means locked
out. The deeper lesson: no offscreen test can flag "reads as part of
the wrong panel"; only daily-driving finds feel bugs.


**V243 `[spot-the-issue]`** The overlay panel sets WA_StyledBackground
on itself and the scrim. Delete those two lines: what exactly renders,
and why did the 28.6.0 docked version survive without them?
**A:** A plain QWidget does not paint stylesheet backgrounds — the
#taskDetailPanel and #panelScrim rules are silently ignored, so the
drawer becomes a transparent ghost (its children float over the page)
and the scrim dims nothing while still eating clicks: an invisible
click-trap. 28.6.0 survived because the panel sat in the layout over
nothing — there was no content BEHIND it to show through, so the
missing background was invisible. The trap only detonates when a
widget starts overlapping others: exactly when you migrate docked →
overlay, which is why it is filed here.


**V244 `[spot-the-issue]`** The v28.6.1 panel shipped with a white
stylesheet background and WA_StyledBackground set — and still rendered
with a grey middle. The cause was documented in this very tree, years
of versions earlier. Name the mechanism, where it was documented, and
what the incident adds to the tripwire rule.
**A:** QScrollArea::setWidget() silently enables the child's
autoFillBackground, so TaskDetailForm — transparent its whole life —
began filling itself with the palette's Window grey inside the white
panel. Theme.h's v3 comment describes this exact mechanism (it's how
the agenda turned black on dark-mode Windows). Second re-hit of a
documented landmine (after the data-folder rename), which sharpens the
rule: a comment is a tripwire only for patches that READ it — the
upgrade is a TEST (theFormNeverFillsItsOwnBackground), which stops the
regression instead of hoping to be noticed, and pins the flag after
every setWidget site including the save-rebuild.


**V245 `[design]`** v28.7 shows pieces inside the category list —
which §D deliberately excluded. Why is this not a reversal of §D, and
where exactly does the interleaving happen (and why THERE)?
**A:** §D's exclusions were about COUNTING — upcoming, affordability,
and digests must not double-count a parent's work — and those queries
still see parents only. The category list change is DISPLAY: indented
rows are structure, not extra workload. Interleaving lives in
CategoryTaskModel::buildSnapshot, after tasksIn returns SORTED parents:
sorting a mixed list would scatter a family by date (a piece due
nowhere sorting away from its parent due Friday), so the model orders
parents first, then tucks each family in whole. tasksIn stays
parents-only so every other caller keeps the counting semantics.


**V246 `[spot-the-issue]`** The 24 px piece indent is applied inside
the delegate's geometryFor and nowhere else. A teammate "simplifies" by
shifting the drawing in paint() instead. What breaks, and what's the
general rule?
**A:** paint() and editorEvent() both call geometryFor — shifting only
the drawing moves the checkbox you SEE 24 px right of the checkbox that
ACCEPTS clicks, leaving a dead zone (clicks beside the visible box) and
a live zone (invisible, left of it). The rule: when a view has one
geometry function feeding both rendering and hit-testing, EVERY spatial
change goes through it — the same single-source argument as the
answer-based dirty check and the one-door AppData mutations.


**V247 `[tradeoff]`** startPieceUnder creates the piece BEFORE the
user names it ("New piece", panel opens with title selected). Defend
create-first over create-on-save, and name the cost plus its
mitigations.
**A:** Create-first means the panel edits a REAL task through the
standard doors (no special "pending piece" state threading through
form, panel, and apply — a whole shadow lifecycle avoided), matches the
TickTick reference exactly (their inline "No Title" row is a created
subtask), and makes walk-away lossless: whatever happened, there is an
honest row. Cost: an abandoned "New piece" can linger. Mitigations:
title arrives fully selected (one keystroke replaces it), and the row
is one ✕ from gone. The alternative's cost is worse: create-on-save
loses the typed title if anything interrupts the first save.


**V248 `[design]`** The v28.8 size dropdown uses non-uniform steps and
caps at 16h. Defend both choices against the obvious alternative (a
uniform 30-minute list to some big ceiling).
**A:** Uniform 30-minute steps to even 40h is 80 rows of noise — the
useful density of durations isn't uniform, so the picker's shouldn't
be: people distinguish 30m from 45m but nobody distinguishes 11h from
11h30. Fine rungs where tasks live (sub-hour), half-hours through a
workday, whole hours after. The cap is doctrine wearing a UI: an
estimate past two workdays isn't information, it's a task that should
be pieces — the picker refusing to say "40h" is the app teaching its
own feature at the exact moment it's relevant.


**V249 `[what-would-break]`** The dropdown inserts off-ladder values at
their sorted rung instead of snapping to the nearest step. What
corrupts if you snap, and which test pins the contract?
**A:** Snapping makes OPENING the panel an edit: a captured "~25m"
becomes 30m the moment the form seeds, before the user touches
anything — then either the dirty flag lies ("you have changes" they
didn't make) or, worse, a clean-looking Save writes the snapped value
and the estimate silently drifts. Every stored value must round-trip
the control untouched. oddEstimatesSurviveTheDropdown pins all of it:
chosen==100 after seeding, label "1h 40m", isDirty()==false, and the
rung sorted between 90 and 120.


**V250 `[design]`** v28.9's promotion rule uses one trigger — the
piece's due date — rather than "has an estimate" or an explicit
"promote" flag. Defend the choice of trigger.
**A:** The date is what already makes a piece its own line everywhere
else: affordability's first guard (no date → NotApplicable) means a
dated piece gets its own verdict, nudge, and needs-a-block entry with
no code asking permission — so counting must follow the same fact or
the two disagree. "Has an estimate" would promote pieces the sweep
never reports (undated ones), silently thinning the parent with no
visible counterpart. An explicit flag adds a stored state that can
contradict both. One observable fact, one rule, zero new state — and
computed fresh each sweep, so no migration and format v13 untouched.


**V251 `[what-would-break]`** The fully-promoted case floors the
parent's estimate at 0 instead of letting it go negative. What breaks
with a negative number, and what does the floor honestly cost?
**A:** minutesEstimated feeds outstanding-work arithmetic; a negative
parent would SUBSIDIZE unrelated tasks — the sweep would believe the
day is lighter than every number entered, the inverse lie of the bug
being fixed. The floor's cost: any un-pieced remainder of an
over-decomposed parent is invisible to the estimate basis, so the
parent degrades to the planned-blocks proxy with estimateBased=false —
stated, not hidden (fullyPromotedParentFallsBackToTheProxy pins the
flag). The honest reading: if your pieces outgrew your guess, the
pieces are the truth now.


**V252 `[recall]`** After v28.9, state the full pieces policy in three
lines — display, digest, counting — and name the section that owns
each.
**A:** Display (§M): the category list shows structure — pieces
indented under their parent, families never split. Digest (§D as
amended by §M): Upcoming stays one row per work item — a parent with
its ☑ chip. Counting (§O): minutes are believed exactly once — a dated
piece's minutes are its own, an undated piece's weigh on the parent,
and a fully-decomposed parent cedes to its pieces.

## v28.10 — the seams, reachable (design-addendum-debug, chat addendum §C.1)

**V253 `[recall]`** The v28.10 slice exists because of one sentence in the
first field report. Which, and what did it mean concretely?
**A:** "Seams only tests can reach are half a seam." Every v28 service was
built testable — `setNowProvider`, public `sweep()`s, `setProviderOverride`,
`TICKTIMER_AI_DOWN` — yet none was reachable from the running app: the
owner couldn't force a check-in (06:00–11:00 ∧ heavy day), couldn't skip
the 20-minute sweep, and had never heard the v28.0 C++ voice because a
working provider always wins. The panel (Ctrl+Shift+D) is the other half.

**V254 `[design]`** The DebugPanel's one design rule is "the panel is
glass". State it and its enforcement.
**A:** Every control presses a public method the test suite already calls
or flips an env var the tests already flip; the panel contains zero
judgement. Enforced by construction (its ctor takes services and two
`std::function`s — no AppData, no QSettings keys of its own) and by its
test, which drives buttons by objectName and asserts only that seams got
pressed. When a button seems to need logic, the logic moves into the
service — behind a test — first.

**V255 `[tradeoff]`** Why a keyboard chord and not a menu bar for the
debug entry?
**A:** The chrome is a nav rail; a QMenuBar hosting one developer item
changes every user's window for one debugging session — the wrong trade
for an ADHD-facing tool whose chrome restraint is a feature. A chord
costs the uninitiated nothing and the initiated one TESTING.md line —
consistent with the project's invisible-until-invoked debug affordances
(TICKTIMER_COMPACT, TICKTIMER_AI_DOWN).

**V256 `[design]`** `forceOffer()` skips the check-in gate but does NOT
mark `checkin/lastOffered`. Why is each half correct?
**A:** Skipping the gate is the point — rehearsing the toast→tap→chat
flow on a quiet afternoon is exactly what the gate exists to prevent for
real asks. Not marking the ledger is the manners half: the promise is
"ask once per real morning", a rehearsal is not an ask, and marking would
mean a 15:00 debug press silently cancels tomorrow's real 08:30 knock —
a surprise no debug tool may produce. The honest path (`sweep()`) still
marks at emit, unchanged.

**V257 `[spot-the-issue]`** The panel could clear the nudge manners with
`QSettings().remove("afford")` — the prefix is even documented in a
comment. What's wrong with that, and what shipped instead?
**A:** The prefix is AffordabilityService.cpp's *private* knowledge (an
anonymous-namespace constant); a second file spelling it is drift waiting
for a rename — a comment is not a mechanism. Shipped: static doors on the
services (`forgetManners()`, `clearTodaysAsk()`) that the panel presses,
so a future key rename breaks a compile, not a debug session. It also
cashes that comment's own promise ("a reset button later is one remove").

**V258 `[recall]`** `TICKTIMER_AI_DOWN` after v28.10: forms and reach.
**A:** Two forms — a comma list of seat ids (original §E hook) and `*`
(every seat, present and future). Checked per call, never cached. Reach:
formerly only the chat's route walk consulted `forcedDown`; now the
single-seat wires do too — NudgeClient collapses to silent `fallback()`
(its personality: it cannot fail loudly), quick-add names the cause in
its visible bar (someone is waiting). Same hook, per-wire manners.

**V259 `[tradeoff]`** Why is the AI-down wildcard an env-var trick rather
than the panel building a comma list of every catalog id — or a
QSettings toggle?
**A:** Enumerating ids duplicates the catalog in a second place that must
agree with it forever — the installer's version lesson (mechanism over
intention) wearing a string list; `*` keeps the catalog the only list and
covers seats invented later. Not QSettings because the state must die
with the process: a debug switch that survives a restart is a support
ticket ("the AI stopped working" — no, you left the kill-switch on).

**V260 `[design]`** The briefing's new fifth anti-hallucination rule
(chat addendum §C.1) — state it, and name the field finding behind each
of the three sections it produced.
**A:** *Computed facts are stated, never implied* — if answering needs
arithmetic over the briefing's own lines, the briefing does the
arithmetic (§A: models have no clock, can't compute dependably).
Sections: DAY STATUS (finding #3, "didn't realise my day ended" — a
spine violation of our own making); PLAN FOR TOMORROW (finding #2 —
true-but-wrong "no plan for tomorrow": the blocks existed and never
entered the context); the TRACKED TODAY disambiguation (finding #4 —
the model reconciled per-block vs day totals itself and contradicted
itself doing it).

**V261 `[what-would-break]`** PLAN FOR TOMORROW deliberately omits the
`[past]/[NOW]/[upcoming]` tags and the tracked column. What breaks if a
well-meaning patch adds them "for consistency"?
**A:** Rule 1's inverse: a future block has no phase and no tracked time,
so the columns would be constants ("upcoming", nothing) — empty structure
that invites the model to fill it, and a standing temptation to "infer"
progress on blocks that haven't happened. Consistency of shape is not a
virtue when the underlying facts differ; the honest format states only
what exists (times, label, area) and the header's ISO date.

**V262 `[hands-on]`** The markdown fix is one property on one widget —
but it's conditional. Write the condition and defend both halves.
**A:** `if (turn.role == ai::Role::Assistant && !turn.localOnly)
bubble->setTextFormat(Qt::MarkdownText);` — Assistant-only because the
user's own asterisks must stay asterisks (rendering *their* text is a
surprise edit); not localOnly because ⚠/context notices are app-authored
plain strings. And MarkdownText over RichText because markdown is the
narrower surface for model-written text: no `<img>`, no scripts, no font
tags to smuggle.

**V263 `[design]`** The fake clock freezes three consumers and leaves the
tracker, Pomodoro, alarms and the painted now-line on the wall clock.
Defend the narrow reach.
**A:** The three v28 services *judge* a moment (sweeps and the briefing
re-ask the provider per look, so a frozen answer means every look judges
the same instant — what reproducing a bug wants). The excluded systems
keep live wall-time *state* — a running interval under a frozen clock
records nonsense durations — so widening is a per-service design
decision, not a default. And the group title names the actual reach: a
debug tool that lies about its scope generates the bugs it exists to
find.

**V264 `[spot-the-issue]`** A reviewer suggests the briefing viewer cache
its text and refresh on `AppData::changed()` "to avoid recomputing".
Reject with precedent.
**A:** The briefing is derived state; the viewer already fetches fresh
per press, which is the whole contract ("the exact text the model
receives, right now"). Caching adds an invalidation path that can lag —
the catch-up drawer's stale-print bug, third telling — to save a
recomputation that costs milliseconds once per button press. Derive,
don't store, applies to debug surfaces too.

**V265 `[spot-the-issue]`** `installer/ticktimer.iss` contains a
commented-out `#define AppVersion GetStringFileInfo(ExeForVersion,
PRODUCT_VERSION)` — the derive-don't-store mechanism — yet the live line
below it is a hand-typed `"28.10.0"` with a "bump BOTH" warning. Isn't
keeping the hand-copy just inviting the drift the project keeps
lecturing about? Defend the file as it stands, and name the guard.
**A:** The mechanism is untestable where the project is maintained: the
installer script is the last thing between the tree and a user's
machine, and the audit environment has no Windows and no Inno — enabling
the derive line there means shipping an untested edit to the one
artifact that cannot be tested, which is how a tidiness fix becomes an
outage. So the file keeps the hand-copy and *names its own debt* in the
comment, and the guard is mechanical, not hopeful: deploy-windows.bat's
step-0 apply check (v28.3) findstr's both live defines and refuses to
build on mismatch. The pattern is the v27 lesson complete: when a
hand-step must exist, pair it with a check that fails loudly — and it
works; the check's third catch was the v28.10 drop itself, which shipped
half-bumped and never got past step 0. Corollary for readers: a decision
record's history comments describe the road taken; only the live line
describes the present. Read past the first mechanism you recognise.

## v29.0 — the write boundary, Slice 1 (design-addendum-write-boundary)

**V266 `[recall]`** Name §B's four stages and what v29.0 shipped for each —
and the one thing deliberately missing from the whole slice.
**A:** Proposal (the closed `verbs::` type — the debug injector composes
them), Validation (`validate()` funnelling into AppData's already-guarded
doors), Confirmation (`ProposalCard`, Apply/Discard in the chat), Recovery
(no undo by design — additive verbs plus the rolling `data.json.pre-apply`
copy-aside). Missing on purpose: the MODEL. Slice 1 is model-less so the
machine is built, forceable, and pinned before any model output exists to
cross it; Slice 2 swaps the proposer and changes nothing to its right.

**V267 `[design]`** Why is `verbs::Role` a new enum instead of reusing
`ai::Feature`?
**A:** Different axes. Feature is ROUTING — which seats may answer — and
deliberately lacks Nudge (its fallback is a C++ sentence, no route table).
Role is TRUST — which call sites may write. Folding them would let a
routing edit widen a trust scope by accident; two enums make that category
of mistake unrepresentable. Same instinct as handles-not-ids: structure
carrying a safety property, not vigilance.

**V268 `[design]`** `validate()` checks the role before anything else.
Defend the ORDER, not just the check.
**A:** Refusal texts are information. Later checks name what exists —
"T9 doesn't refer to anything", "'Lab 4' already has an estimate" — so a
forbidden role that got those answers could map the handle space and task
states by probing. Role-first means a caller outside the allow-list gets
one flat sentence and learns nothing. The gate's manners are part of the
boundary.

**V269 `[what-would-break]`** HandleMap resolves strictly: `T9`, `B1`,
`t 3` all die to `""`. A helpful patch adds fuzzy matching — trim, case-
fold, "obviously they meant T3". What breaks?
**A:** The exact bug handles exist to prevent: wrong-target application.
Models invent plausible near-misses; strict resolution turns every
invention into a readable refusal, while fuzzy resolution turns some into
silent hits on the wrong task — the worst outcome the boundary has,
worse than any refusal. Fail-safe means the failure mode is a no, never
a maybe.

**V270 `[design]`** Priority is excluded from `SetTaskDetails`. State the
principle that excludes it and why "just allow setting it if it's still
Medium" was rejected.
**A:** The verb is ADDITIVE: it fills absent fields, and absence uses the
domain's own idioms (0-estimate, invalid QDate). Priority has no absence
state — Medium is a value, the default, not a blank — so "fill if empty"
is undefined. Treating still-at-default as "empty" would make the verb
overwrite a value the owner may have deliberately left, which is an
overwrite wearing additive clothes; overwrites are a different verb with
an inverse story of their own (§B.1) and must arrive as one.

**V271 `[what-would-break]`** `apply()` re-validates even though the card
was rendered from a verdict. Walk the stale-card scene and what a
trust-the-render implementation does wrong.
**A:** Card renders (valid: estimate absent). Owner fills the estimate BY
HAND in the detail panel. Owner later taps Apply on the old card. Trust-
the-render calls setTaskSize and silently overwrites the by-hand value
with the proposal's — the assistant just destroyed the owner's own input,
the precise harm the boundary promises cannot happen. Re-validation at
the tap hits the additive check against the CURRENT world and refuses
with a reason; the test pins the by-hand value surviving.

**V272 `[design]`** The card's text comes from `Proposal::summary()`, not
from any prose the proposer supplies. What attack-shaped mistake does
this close?
**A:** Description/request divergence: a proposer saying "I'll just set
an estimate" while the structured payload does something else. The
summary is composed from the same fields `apply()` consumes, so the
description IS the request re-rendered — what you approve is what runs.
In Slice 2 this matters doubly: model prose never becomes the thing the
owner legally approved; the fields do.

**V273 `[recall]`** The NEEDS DETAILS queue: what puts a task in, what
keeps a task out, and why is it derived rather than stored?
**A:** In: open (not done, not archived) with estimateMinutes == 0 —
the estimate is the fact affordability starves without. Out: a TBD due
date alone (first-class absence, Task.h's opening comment), done or
archived tasks. Derived per briefing because a stored queue can drift
from the data; this one IS the data, asked politely — and it goes
silent when empty, the MOOD manners (an empty header invites the model
to speculate).

**V274 `[design]`** Applied proposals write a localOnly transcript
receipt; discarded ones write nothing; the card itself survives neither
a rebuild nor a restart. Untangle the three lifetimes and why each is
right.
**A:** The transcript is the RECORD; cards are live UI (they don't
survive rebuildLog, so anything durable must be a Turn). An apply
changed the world → the record gets "✓ Applied: …", localOnly so it
never enters a model's context (privacy stance of every notice). A
discard changed nothing → nothing to record; a declined suggestion
haunting the log would be noise shaped like guilt. The model's need to
know about declines is Slice 2's tool_result — the proposer's business,
not the record's.

**V275 `[tradeoff]`** The copy-aside is one rolling file, not a
timestamped archive, and it's wired in MainWindow, not ChatPage. Both
choices, defended.
**A:** Rolling: §B calls it "cheap insurance, not a feature" — the state
before the most recent apply answers "the assistant just did something
odd", while unbounded `.pre-apply-<timestamp>` files are a disk-filling
promise nobody made; archives are version control's job. MainWindow:
the file path is m_store's knowledge — composition wiring — so ChatPage
holds only a `preApplyHook` std::function (the nowProvider precedent:
the page knows WHEN, the root knows WHAT).

**V276 `[design]`** §B.3 ordered: promote Dialect to strategy objects at
tool-use, or record why not. v29.0 recorded why not — reproduce the
argument and name where the criterion should genuinely fire.
**A:** The criterion is state ACROSS calls — a dialect holding
behaviour over time, like threading tool_use/tool_result transcripts
per provider. Slice 1's proposals are single-shot and C++-composed: no
tool transcript, no provider-specific conversation shape, nothing for a
strategy object to hold. Promoting now would be speculative structure —
the YAGNI the criterion exists to prevent. Slice 2's conversing intake
model is where the state appears; if it still doesn't earn promotion
there, that answer gets recorded too. The doctrine is the recording,
not the deferral.

**V277 `[hands-on]`** `dayBriefing` gained its handle out-param LAST and
defaulted — after an earlier draft put it before `opts`. What would the
earlier draft have broken, and what general signature rule does the fix
apply?
**A:** Every existing positional call site — `dayBriefing(data, today,
now, opts)` — would have tried to pass Options into a HandleMap*
(loudly, a compile error, but a churn across tests and callers for
nothing). Growing a defaulted parameter at the END means every pre-v29
caller compiles unchanged and only callers who WANT the map mention it.
Rule: widen APIs at the tail; inserting mid-signature taxes everyone to
serve someone.

**V278 `[spot-the-issue]`** v29.0.1's bug produced a TRUE error message
("Please check your details and try again" — the input *was* in some
sense not accepted) that still cost a live debugging session. Name the
two design faults that compounded, and the fix for each — and say why
the server's exact route matching was NOT one of them.
**A:** Fault 1: unnormalized user input at a concatenation point — a
pasted base URL naturally ends in `/`, and `base + "/register"` made
`//register`. Fix: `normalizeServerUrl` applied at every entry (both
AuthClient doors AND SyncClient, the second consumer of the same base —
defuse the landmine everywhere it's buried, not where it went off).
Fault 2: the error taxonomy's catch-all — every unrecognized server
token collapsed into InvalidInput's credential-blaming sentence, so the
unforeseen case wore the costume of the common one. Fix:
`UnknownServerReply`, whose message points at the address and versions —
the catch-all must name itself, because it is by definition the case
you didn't design for. The server's strictness stays: fuzzy route
matching at a trust boundary trades a paste-convenience for ambiguity
about what endpoint was actually invoked, and the client is the right
place to be forgiving about pastes. Method note for the bank: the
owner's browser screenshot — a JSON not_found at `/` — proved network,
port, and server identity in one glance; read the evidence already on
screen before theorizing.

**V279 `[spot-the-issue]`** v29.0.1 fixed the trailing-slash bug by
normalizing inside AuthClient. Within hours, that exact fix helped
produce v29.0.2's bug. Reconstruct the causal chain, name the two
doctrine lines it minted, and say what role consumer-side normalization
keeps afterwards.
**A:** Chain: LoginDialog::serverUrl() still returned the raw field
text; AuthClient normalizing internally meant login now SUCCEEDED with a
slash-bearing URL — so the raw value was saved to settings and handed to
every service. ShareClient (unpatched) concatenated `//share`, the
server route-404'd, and ShareClient's classifier collapsed both 404s
into NotFound — printing the app's own URL bug as "check the spelling".
The asymmetry (A→B ok, B→A not) decodes as: whichever machine's SAVED
URL carries the slash can't share out. Doctrine lines: (1) *fix where
the value is born* — a consumer-side fix for a source-side problem can
actively ARM the next failure by teaching the nearest symptom to pass
while the poison persists; (2) *the taxonomy crime, second conviction* —
when one message can mean "their typo" or "our bug", the classifier must
do the work to know which (the body was already parsed; the distinction
was one comparison away, and the old comment rationalized skipping it).
Consumer-side normalization survives as defense in depth only — belt at
the birth, suspenders at the doors, and the suspenders are never again
the fix.

## v29.1 — the intake interview (design-addendum-intake)

**V280 `[recall]`** The interview answers arrive in three tiers. Name
them, their order, and the two distinct reasons crisp goes first.
**A:** (1) `intake::parseDurationAnswer` — C++, anchored, reads "2h" /
"1h30" / "90 min"; (2) the model, for prose; (3) the honest hint, which
keeps the interview open and names the path that always works. Crisp-
first is a cost rule (an already-unambiguous answer should not ride a
network round-trip) AND a sovereignty rule (the user's own crisp number
must not arrive reworded by a model). The parser's refusals enforce the
split: prose falls through to the reader that understands prose.

**V281 `[design]`** Intake uses plain JSON extraction, not the vendors'
tool-calling APIs. Give the three recorded reasons, and state what this
did to §B.3's Dialect-promotion criterion.
**A:** (1) Provider neutrality — plain JSON runs identically on every
seat, including a local Ollama with no native tool support; the
interview must not be a premium-seat feature. (2) The confirm loop
already IS the tool layer — the owner is the dispatcher; API-level
tool_use would build a second, uninspectable dispatch under the
inspectable one. (3) Single-shot by construction — no tool transcript
exists to thread per-provider. Hence §B.3's criterion (state across
calls) did not fire for the SECOND time, this time by design rather
than absence — recorded in three places, because two honest
non-firings are the doctrine working, not a dead letter.

**V282 `[design]`** Tapping "≈ 2h sounds right" could just write the
estimate — the user asked for it twice over. It presents a card
instead. Defend the extra tap.
**A:** Every write crosses the card, no convenience exceptions. The
moment trusted paths skip the confirm, the confirm stops meaning
anything — and "which paths skip it" becomes an audit burden that grows
with every feature. One tap buys a boundary with no asterisks: the
owner can reason "nothing changes without a card" as a universal, which
is the entire trust story of the arc.

**V283 `[spot-the-issue]`** The first draft of `intake::llm::systemPrompt`
took `(const AppData&, const Task&, …)`. What rejected it, and what is
the general principle the fix restored?
**A:** The nlp suite's own charter — its CMake comment: pure AI layers
must not drag AppData in (that is why brief:: lives in test_domain).
The prompt used the whole domain to format ONE category name; the fix
takes values (`Task`, `areaName`) and the caller resolves the name.
Principle: suite structure with teeth — purity enforced by what a
target compiles converts design drift into compile errors, the
cheapest place drift is ever caught. Vigilance is not a mechanism;
link lines are.

**V284 `[what-would-break]`** Intake is one header and TWO translation
units. What broke to force the split, and what breaks if a tidy-minded
patch merges them back into Intake.cpp?
**A:** `intake::llm::parseReply` calls `ai::extractText`
(LlmProvider.cpp) — which domain test targets do not link, so the merge
produces `undefined reference to ai::extractText` in test_domain and
test_taskmodel at link time. The split puts each half with its
dependency group: the brain (guess/triage/question/parser) with the
domain sources, the extraction beside LlmQuickAdd.cpp. The linker was
the design reviewer here; the addendum (§H) keeps its verdict.

**V285 `[design]`** Skip presses `dismissTask` directly; a discarded
proposal card does not. Untangle skip, discard, and `askedThisSession`.
**A:** Skip is the OWNER acting on their own data — mood-buttons
precedent, straight through the domain door, dismissed a year (§K.6's
ask-once without a year-9999 value). Discard means "not this number",
not "never ask" — inferring a dismissal from it would write domain
state from a gesture that doesn't mean it. But the loop must not
re-ask the discarded task instantly, so `askedThisSession` — session-
scoped, never persisted — remembers who was already asked until the
conversation restarts. Three lifetimes: forever-ish (skip), this
session (asked), this card (discard).

**V286 `[recall]`** `worthInterviewing` — the three substance signals,
the pre-conditions, and where "ask once" is enforced.
**A:** Pre-conditions: open (not done/archived) and unsized. Signals,
§K.6 verbatim: a real due date; Urgent priority; or a category whose
history says tasks there absorb hours (historyGuess ≥ 60m — the guess
doubling as evidence). Ask-once: `dismissedUntil > now`, compared
against now itself so a lapsed timestamp never hides a task even if
housekeeping hasn't run — the needs-a-block gate's rule, reused.

**V287 `[tradeoff]`** The guess needs only TWO finished samples; the
personal rate demands more. Reconcile the two bars.
**A:** The rate scales estimates SILENTLY — an unearned multiplier
quietly rewrites every affordability verdict, so it needs real evidence
("an anecdote is not a rate"). The guess is SPOKEN, arrives with its
basis attached ("2 finished School tasks ran ~2h each"), and is
confirmed or corrected by the owner in the same breath. Weak evidence
is acceptable when labelled — the label is the license. Same data,
same scan, different bar because different failure modes.

**V288 `[design]`** §K.1 says never interview at capture. Where ARE the
entries, and why is the check-in the designed one?
**A:** Entries: the morning check-in's offer (after the mood lands, one
button, one "Not now" that just removes the row) and the debug panel's
forcing button — both pressing the same `beginIntake()`. Capture-time
interviewing trains the user to stop capturing — the one behaviour an
ADHD tool must never tax. The check-in is the moment the user already
CHOSE to talk; the interview rides consent that exists instead of
manufacturing an interruption.

**V289 `[hands-on]`** `parseDurationAnswer("2024")` returns 0 and
`parseDurationAnswer("1.5h")` returns 0 — both look parseable. Defend
each refusal from the pattern itself.
**A:** The bare-number branch is `\\d{1,3}` — three digits caps the
form, so "2024" (a year someone pasted) cannot become a 33-hour
estimate; small whole numbers are minutes, big numbers are not
durations. "1.5h" fails because the pattern takes integers only:
fractional forms are ambiguous enough to belong to the model (or to a
clearer retype), and the parser's charter is to never guess — its
wrong-parse failure mode silently pre-empts the model that would have
read it right, which is worse than a hint.

**V290 `[spot-the-issue]`** For one commit, v29.1 held two extraction
layers (`intake::llm` in Intake.h and a fresh `IntakeLlm.h`), written
hours apart by the same author. Reconstruct how, and name the working
rule it minted.
**A:** The second was written from PLAN NOTES — camelCase fields,
narrower prompt — without re-reading the first, whose header already
declared the richer contract (snake_case fields, guess-agreement, the
do-not-restate rule). The tree disagreed with the author's memory of
the tree, and the author trusted memory. Rule minted (addendum §H):
the header is the abstract — read the tree before extending it, because
plan notes describe intentions and only the tree describes the present.
Same lesson as the .iss history comment (v28.10 postscript), one layer
up: the live line, not the recollection, is the truth.
