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

*Add to this as we build. Every new feature should be able to contribute at
least one `[MC]`/`[T/F]` and one `[HANDS-ON]` question — if it can't, you may not
yet understand it well enough to teach it.*
