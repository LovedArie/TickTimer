# Reading Guide — TickTimer

You didn't write this code — you're going to *read* it, which is a skill of
its own and the one professional developers use most. This guide gives you
the reading order, tells you what each file is trying to teach you, and
lists the honest deviations from the design docs that implementation forced.

Budget: ~32,000 lines across `src/` + `include/` + `server/`, and ~10,000
more in `tests/` — measured, not remembered:
`cat src/*.cpp include/*.h server/* | wc -l`. (This line read "~2,600 lines"
from v2 until v29.1. It was true when written and had been wrong for twenty
versions; the order below matters far more than the total.) Read it in the
order below, one sitting per part. Every file opens with a banner comment explaining
*why it exists* — read those banners first, on a single pass through all
files, before reading any function bodies. That top-down pass is how
developers approach any unfamiliar codebase.

---

## 0. Build it first (before reading anything)

Seeing it run gives every file you read a face.

**Windows, Qt Creator (your setup):**
1. Copy the project folder somewhere (e.g. `Documents/Project/TickTime/app`).
2. Qt Creator → *File → Open File or Project…* → pick `CMakeLists.txt`.
3. Choose your Qt 6.8 MinGW (or MSVC) kit when asked → *Configure Project*.
4. Press the green Run button. That's it — CMake is the project file.

**Command line (any OS with Qt 6 installed):**
```
cmake -B build
cmake --build build
./build/ticktimer        # build\ticktimer.exe on Windows
```

**Run the tests:** in Qt Creator, *Build → Run All Tests*, or:
```
ctest --test-dir build --output-on-failure
```

Your data lives in one JSON file (the status bar shows the path on
startup — on Windows: `C:/Users/<you>/AppData/Roaming/TickTimer/
data.json`). Open it in a text editor after using the app for a minute.
Being able to *read your own data* is why v1 chose JSON (design-doc §4).

---

## 1. Reading order

The order follows the architecture: each layer only knows the ones before
it, so read bottom-up and nothing will reference something you haven't met.

### Part 1 — the domain — the app with no screens

*Layout note (v2): every header lives flat in `include/`, every `.cpp` in
`src/`. The layers (domain / storage / tracking / ui) are no longer visible
as folders — they live in the CMake source lists and the include graph, and
the test target still enforces them. `CMakeLists.txt` explains the
`target_include_directories` line that makes bare `#include "AppData.h"`
work everywhere.*
| File | What it is | Watch for |
|---|---|---|
| `Category.h` | life area (value struct) | why a struct with public fields is *correct* here |
| `Ids.h` | identity via UUIDs | stateless helper = free function, not a class |
| `Activity.h` | reusable thing you do | references by **id**, never by pointer — and why |
| `Segment.h` | real tracked time | `enum class`; timestamps-not-counters (crash safety) |
| `Task.h` | a one-off obligation | the type-vs-instance twin of Activity↔Event; invalid QDate as the "TBD" state |
| `Folder.h` | rail grouping | why a fact gets a class, never a name-prefix convention |
| `SpecialDay.h` | dates that matter | derived nextOccurrence(); the documented Feb 29 rule |
| `Event.h` | the plan + owned segments | composition = member by value; the `plan::` constants; minutes-not-QTime deviation |
| `AppData.h/.cpp` | aggregate root | integrity rules enforced in ONE place; the `changed()` signal (Observer pattern); pointer-lifetime rule; `std::optional`; move semantics in `resetFrom` |
| `Stats.h/.cpp` | derive, don't store | pure functions: data in, numbers out, no state |

Checkpoint: you should be able to answer — *why can't a buggy UI corrupt
this data?* and *where would a stored WeeklySummary go wrong?*

### Part 2 — storage
`JsonStore.h/.cpp` — all JSON knowledge quarantined in one file; the
`QSaveFile` write-then-replace (THE reliability rule as code); the
`version` field planted for future migrations.

### Part 3 — tracking
`TrackerService.h/.cpp` — the Idle/Focusing/OnBreak state machine
(design-doc §3.8: state as *state*, not subclasses); commit-on-transition;
the heartbeat + `RunningState` crash insurance. Read this side by side
with `AppData::recoverInterruptedTracking()` — they are two halves of one
promise.

### Part 4 — the tests (`tests/test_domain.cpp`)
Yes, before the UI. Tests are executable documentation: each test case
states a rule from the Supplementary Spec and proves it. Note *what* is
tested (domain + storage, headless, milliseconds) and what isn't (widgets)
— testable code and layered code turn out to be the same thing.

### Part 5 — the UI
| File | What it teaches |
|---|---|
| `Theme.h` | one palette, app-wide QSS (Qt's CSS) |
| `Widgets.h` | extract shared pieces; the `scaledFont` portability guard |
| `AgendaWidget.h/.cpp` | **the big one**: custom painting with QPainter, geometry math shared between drawing and hit-testing, hover, signals out |
| `GlancePanel.h/.cpp` | derive-on-refresh; live seconds added on top; the calm non-shaming copy as a *requirement* |
| `PickActivityDialog.h/.cpp` | dialogs as pure questions (collect, don't mutate); lambda capture by value |
| `EventDialog.h/.cpp` | thin shell over services; the `m_updatingUi` guard against signal feedback loops |
| `ActivitiesPage.h/.cpp` | master-detail pattern; selection state as an id (rebuild-proof); rebuild-don't-patch; integrity rules *mirrored*, not enforced, in UI |
| `DueDateDialog.h/.cpp` | dialogs as pure questions; "no date" as an answer, not a cancel |
| `TaskRow.h/.cpp` | the "second consumer" extraction rule |
| `UpcomingPage.h/.cpp` | a pure derived view — no data, no save code, just a question |
| `SpecialDaysPage.h/.cpp` | fact vs derived, side by side (the day vs the countdown) |
| `ReviewWidgets.h/.cpp` | week/month derived from the same segments — §3.5's payoff; hand-painted bar/pie/dot-grid charts |
| `PlannerPage.h/.cpp` | a coordinator class; UC1's whole click chain |
| `PomodoroPage.h/.cpp` | a second state machine (countdown-driven) to compare with the tracker (command-driven) |
| `MainWindow.h/.cpp` | composition root; ownership diagram; member declaration order = destruction order; the one autosave `connect`; `enableSync` — a capability the window *gains* after login, not a constructor concern |
| `AssistantVerbs.h/.cpp` | v29.0 — the write boundary's heart: the per-role closed verb set (one screen = the whole security review), fail-safe HandleMap, validate/apply with the additive rule and re-validation at the tap; the header comment is the design abstract |
| `ProposalCard.h/.cpp` | v29.0 — §B stage 3 as a widget; glass (shows, emits, decides nothing); summary composed from the request's own fields, never the proposer's prose |
| `Intake.h` + `Intake.cpp` / `IntakeExtract.cpp` | v29.1 — one header, TWO translation units split by dependency group (the linker's lesson, addendum §H): the interview's C++ brain (guess, triage, question, crisp parser) compiles with the domain; the pure extraction (`intake::llm`, values-only prompt) sits with the nlp sources |
| `IntakeClient.h/.cpp` | v29.1 — the interview's wire; LlmQuickAddClient's twin with the forcing hook honoured from birth |
| `DebugPanel.h/.cpp` | v28.10 — "seams only tests can reach are half a seam": pure glass over the v28 services' injection seams (Ctrl+Shift+D); the header's three WHYs are the whole design; recipes in `docs/TESTING.md` |
| `main.cpp` | why main() stays tiny; the login gate before the window; the token handover |

### Part 6 — the networked arc (client + server)
Read this last — it's a whole subsystem bolted in *front of* the planner,
which never changed to accommodate it. Two design addenda cover the *why*
(`design-addendum-login.md`, `design-addendum-sync.md`); the code splits
into three honest layers.

| File | What it teaches |
|---|---|
| `PasswordHash.h` | the one rule of passwords (never store them); salt + stretch; a self-describing format so Argon2 can drop in later without resets |
| `AccountStore.h/.cpp` | the server's identity registry; `Result` enum over a bare bool; the username charset gate (usernames become filenames) |
| `server/AuthServer.h/.cpp` | a hand-rolled HTTP/JSON service on `QTcpServer` — you *see* what HTTP is; session tokens in memory; the 409-on-stale-revision conflict check |
| `server/server_main.cpp` | why the server is a `QCoreApplication` (headless), links Network not Widgets |
| `PlannerStore.h/.cpp` | the **opaque blob**: `{revision, data}` per account, never parsed — dumb server, smart client |
| `AuthClient` / `SyncClient` | async `QNetworkAccessManager` wrappers → typed `Outcome` signals; the three QB-M lessons baked in (clear cache, branch on status attribute, real reason phrases) |
| `SyncPlan.h` | **the whole sync brain**: `decide()` — a pure four-row truth table, tested *as* the table |
| `SyncService.h/.cpp` | policy over wire: the dirty bit, the `m_applying` reentrancy guard, conflict-as-human-choice |
| `LoginDialog` / `SyncDialog` | reactive gate + one-button sync UI; the widget-reports/service-decides split, one level up |
| `tests/test_auth.cpp` | the most careful tests in the repo (an auth bug lets the wrong person in, silently); the truth table pinned |
| `tests/test_login_live.cpp` | end-to-end: spawns the REAL server, drives a two-'device' sync playbook over a real socket |

**Part 3 of the arc — share & compare** (`design-addendum-share.md`):

| File | What it teaches |
|---|---|
| `ShareStore.h/.cpp` | permissions as data: a *directed* read grant; idempotent grant/revoke (retry-friendly APIs); the canonical-name rule shared by all three server stores |
| `AuthServer.cpp` (share routes) | a path *parameter* (`/planner/<user>`); **401 vs 403** — "who are you?" vs "you specifically, no"; validate-at-the-door (`no_such_user`) |
| `ShareClient.h/.cpp` | the third wire sibling; the `classify()` helper — the rule of three turning copied if-ladders into a function |
| `Compare.h` | `SyncPlan.h`'s sibling: the feature's one real decision (the Even tolerance) as a pure, parameterised function |
| `SharingDialog` / `CompareDialog` | glass with zero policy; a *snapshot* `AppData` (owned, wired to nothing) vs the live one — and one summarizer running on both |

**Part 4 of the arc — update notices** (`design-addendum-update.md`, the arc closer):

| File | What it teaches |
|---|---|
| `Version.h` | a load-bearing value gets ONE home; the `RC_INVOKED` trick (one header feeding C++ *and* the Windows resource compiler); strict semver, compared numerically — never as strings |
| `AuthServer.cpp` (`/version`) | a public (token-free) route; config read per-request so "announce a release" = "edit a file"; absence (404) as a state, not an error |
| `UpdateClient` | the fourth wire sibling — the family recipe now writes itself; every failure mapped to deliberate silence |
| `UpdateBanner` + `version::decideBanner` | fetch / judge / render as three layers; the non-nag rule (newer AND not dismissed) as a pure, table-tested function |

**The daily-driver pass** (`design-addendum-daily-driver.md` — the first session built from the owner's own usage feedback):

| File | What it teaches |
|---|---|
| `Task.h` / `Activity.h` | a third life stage (open → done → archived) as ONE flag, not a state machine; enum-with-safe-default string mapping, third time (`priorityFromString("") == Medium`) |
| `ArchivePage` | a page that derives everything and owns nothing; deliberately NOT offering buttons the domain would refuse (no activity-delete) |
| `EventDialog` (tracked time) | facts editable by their owner — manual segments enter the SAME door as the timer's; retract-by-exact-index (refuse, don't clamp); rebuild-only-on-change vs a 1 Hz refresh (the flicker bug that almost shipped) |
| `UpcomingPage` (lenses) | view-state vs data (the filter dies with the page); tabs as checkable QToolButtons — the nav idiom, reused |
| `CompareDialog` (schedules) | the snapshot-AppData design paying rent: `eventsOn()` answers for a peer exactly as for you |
| `SpecialDaysPage` (edit) | invalid QColor as "no choice made" — absence-as-default, third appearance (after TBD dates and missing JSON keys) |

### Part 7 — the build (`CMakeLists.txt`)
Read the comments: what CMake *is*, why headers are listed (AUTOMOC), and
how the test target doubles as an architecture check (domain must build
without Qt Widgets). Note the newer pieces: the `ticktimer_auth` static
library shared by server and tests (same code, no drift), the separate
`ticktimer-server` executable, and `Qt6::Network` threaded through the
targets that touch the wire.

---

## 2. Follow one story end to end

After the file pass, trace UC2 with the debugger (breakpoints in these
spots, then click *Start focus* in the app):

1. `EventDialog` — the button's `connect` fires `m_tracker->startFocus(id)`
2. `TrackerService::startFocus` → `beginInterval` → insurance written
3. click *Take a break* → `commitCurrentInterval` → `AppData::appendSegment`
4. `AppData` emits `changed()` →
   `MainWindow`'s lambda saves; `PlannerPage::refresh` repaints;
   `EventDialog::refresh` re-reads
5. kill the app mid-focus (Task Manager), restart →
   `MainWindow` ctor → `recoverInterruptedTracking` → status-bar message

If you can narrate those five steps from memory, you understand the app.

---

## 3. Honest deviations & doc corrections *(✅ applied 2026-07-04 — kept for history)*

Implementation always feeds back into documentation. These corrections
have now been **applied** to the docs (design-doc v3 merges both addenda and
fixes §3.1; README, Vision, Use-Case Model, Supp Spec, Glossary, Risk List,
Iteration & Phase Plans all updated 2026-07-04). The list stays as a record
of what drifted and why:

0. **Organizing:** merge `docs/design-addendum-organizing.md` into the
   design doc (Folder, SpecialDay, the Upcoming query, Feb 29 rule).
0. **Tasks:** merge `docs/design-addendum-tasks.md` into the design doc
   (new §2 concept + §3.9–§3.11 + integrity/persistence notes).
1. **§3.1 / §5:** planning slots are **30 minutes, 6 AM–midnight** (the
   validated prototype), not hourly 6 AM–11 PM. Blocks span 1–4 slots
   (30 m–2 h) via the picker's duration pills.
2. **§2 domain model:** `Event.plannedStart/End` are stored as **minutes
   after midnight (int)**, not `QTime` — QTime cannot represent 24:00 and
   our last slot ends exactly at midnight. Intent unchanged.
3. **§5 scope:** weekly & monthly reviews and the Pomodoro timer are now
   **in**, not backlog — the build includes them.
4. Crash recovery is implemented as *heartbeat + RunningState*, worth a
   sentence in §4 next to the timestamps paragraph.

Deliberate simplifications (candidates for your first solo features):
- **Move-mode / drag** from the prototype (the ⇕ grip) is not implemented;
  rescheduling uses the dialog's ▲/▼ nudge buttons. Adding move-mode to
  `AgendaWidget` is an excellent exercise — the hit-testing pattern is
  already there.
- The sidebar's **activity chips** were dropped (pure decoration).
- The encouragement rule was generalized: the prototype hard-coded seed
  category ids; the app uses the focus share of tracked time (≥ 60% →
  momentum message). Rules tied to seed data break the moment users make
  their own categories.
- Saving on every `changed()` includes **every keystroke in a note** —
  correct but chatty. A debounce timer (coalesce saves 500 ms apart) is a
  tidy exercise in `MainWindow`.
- UI updates are **rebuild/derive-on-change** on most pages — simple at this
  data size, and impossible to leave stale. The two task lists are the
  exception: Upcoming (v20.0) and the Activities task pane (v20.2) went
  **Qt model/view** (`QAbstractListModel` + custom delegates), sharing one
  diff in `TaskSnapshotModel` (v20.3). Read `design-addendum-model-view.md`
  and compare a converted page with an unconverted one — the contrast *is*
  the lesson.
- SQLite storage stays future work by design (design-doc §4).

---

## 4. Known Qt traps this codebase already survived (so you don't have to)

- Never name identifiers `slots`, `signals`, or `emit` — they're Qt macros
  (`Widgets.h` has the war story).
- Never name a **namespace** after a POSIX function either — bionic declares
  `void sync(void)` in `<unistd.h>`, so `namespace sync` built on Windows for
  a year and failed the first Android compile. It is `syncplan` now
  (`SyncPlan.h`); MinGW hides this whole class.
- `QVector` moves elements when it grows → never store pointers into it;
  store **ids** and look up (`Activity.h`, `AppData.h`).
- Signal feedback loops (widget edits → data `changed()` → widget reset)
  → the `m_updatingUi` guard (`EventDialog.cpp`).
- Deleting a `QLayout` does **not** delete its widgets
  (`ReviewWidgets.cpp::refresh`).
- Default fonts can be pixel-sized (`pointSizeF() == -1`) → `scaledFont`
  (`Widgets.h`).
- **QSS is CSS-*shaped*, not CSS**: a `border-radius` larger than half the
  widget's height is silently *dropped* (no clamp, no warning) — pin the
  height in code so the radius is lawful by construction
  (`CatchUpCard.cpp`, the pill coats; catch-up Coda 2 + V161/V164).
- `QScrollArea::sizeHint` is a **cached guess** — neutralise it on *every
  axis it can lie on*: give it the stretch horizontally, cap it to its
  body's hint vertically (`GlancePanel.cpp` review row +
  `NeedsBlockCard.cpp` HEIGHT HONESTY; Codas 2 & 4, V165/V167).
- `parentWidget()` as an implicit dependency breaks silently on
  re-parenting — **inject** hosts and collaborators instead
  (`setDrawerHost`; Coda 3, V166).
- Fingerprint-cached rebuilds: **any async handler that mutates rendering
  state must invalidate the print** (a drawer's `closed()`, a panel-side
  veto) or the widget freezes on stale state (`setSuppressed`, the
  drawer-reopen fix; V158/V168).
- `AppDataLocation` = `<org>/<app>` → setting both to the same string
  doubles the folder (`main.cpp`).
- Qt themes through TWO layers — stylesheet AND QPalette. Pinning only
  the stylesheet let dark-mode Windows paint the calendar black; the fix
  is `theme::applyTheme` (`Theme.h` tells the full story, including how
  `QScrollArea::setWidget` flips `autoFillBackground` on behind your back).
- A slot must never `delete` a widget that could be — or could CONTAIN —
  the sender: rebuilds triggered by `changed()` use `deleteLater()`
  (`ActivitiesPage.cpp`; the crash story is in `TROUBLESHOOTING.md`, its
  regression test in `tests/test_ui.cpp`).
- On the offscreen platform, focus events only flow in an ACTIVE window —
  `setFocus()` silently no-ops until `activateWindow()` +
  `qWaitForWindowActive` (`tests/test_ui.cpp`). Same platform, second lie:
  it ships **no fonts**, so every text-derived width is roughly DOUBLE the
  real one (`minimumSizeHint` 1051 where the truth is 522) until
  `QT_QPA_FONTDIR` points it at the OS fonts — which is what makes the phone
  width budget assertable at all (`CMakeLists.txt`, the `ui` test).
- `moc` reads **everything** after `private slots:` as a signal or slot
  declaration. A `static constexpr` or a helper `struct` there aborts it with
  "Not a signal or slot declaration", and the only thing you see is a missing
  `test_*.moc` on an `#include` you never touched. Declare them above the
  class or inside the function (`tests/test_ui.cpp`).
- **A top-level window is clamped UP to its `minimumSizeHint` and never back
  DOWN.** Invisible on a desktop, where the window manager keeps offering new
  sizes; on Android the platform offers one, at startup. A window born at the
  desktop default laid out wide, took the minimum that implied, and then would
  not shrink when the minimum later fell — 571px on a 360px screen
  (`MainWindow.cpp`, the constructor's compact-device size and the re-fit in
  `applyChromeMode`).
- **A touch widget must not act on `mousePressEvent`.** A tap and a scroll
  start identically, so acting on press means every attempt to scroll opens
  something. Defer: long-press to create, release-without-movement to open, and
  cancel on `QEvent::UngrabMouse` — which is how `QScroller` says it has taken
  the gesture to pan (`AgendaWidget`).
- **Qt reports `availableGeometry().y()` as 0 on Android** while the status bar
  is drawn over that strip, and there is no safe-area API to ask. Anything
  pinned to the screen's top lands underneath it
  (`installCompactDialogFitter`).
- **Read sizes after the event loop turns, not during layout.** `QEvent::Show`
  precedes polish, so stylesheet padding is missing from every size hint;
  `QEvent::LayoutRequest` arrives mid-invalidation, so minimums are still the
  old ones. This project has now paid for the same mistake three times — the
  mode dispatch, the window refit, and the dialog fit are all queued.
- **A `QDialog` is its own top-level window**, so container-driven layout does
  not reach it — `MainWindow`'s watcher governs `MainWindow`. Dialogs are a
  separate surface with a separate budget and a separate test
  (`installCompactDialogFitter`, `dialogsFitAPhoneScreen`).
- **A mode ladder must be DESCENDABLE**: the layout at one size class has to be
  able to shrink PAST the breakpoint that enters the class below, or that class
  is unreachable. A 236px unwrappable tagline hidden only at Compact kept
  Medium's floor above the threshold for entering Compact, so Compact never
  happened (`MainWindow.cpp::applyChromeMode`).
- Qt takes the app font from the system, and **Android's default is 19pt**
  against a desktop's ~9pt. It moves only what the stylesheet does not size in
  `px` — which is the chrome, and therefore the last place anyone looks
  (`TICKTIMER_FONTPT` on the screenshot tool reproduces it).
- **A minimum size is a promise the layout will keep even when it cannot.**
  Qt honours a `minimumSizeHint` wider than the screen by letting the surplus
  hang off the edge — no scrollbar, no warning. And a `QStackedWidget`'s
  minimum is the MAX over all its pages, so one unwrappable `QCheckBox` label
  on the Pomodoro page clipped the *Planner* on a phone. A `QScrollArea`
  severs the promise entirely, which is why the pages that scroll are the
  pages that fit (`Responsive.h`, `docs/design-addendum-responsive.md`).
- A header that only forward-declares a class can't inline-call its
  members — bodies touching the type go in the .cpp (`WeekAgendaView.h`).
- `QSettings` with no organizationName files preferences under
  "Unknown Organization" — deliberately NOT fixed: naming an org now would
  relocate settings AND the data folder (`main.cpp`; the screenshot tool's
  probe prints the real path).

## 5. New since v13 — landmarks worth a visit

- **Event's three identities** — `activityId` / `taskId` / `title`, "at
  least one" enforced at the doors (`AppData.cpp`: `appendGuardedEvent`,
  `setEventTitle`, `setEventTask`); every screen resolves through
  `eventLabel` / `eventCategoryId`.
- **`LabelEdit`** (`EventDialog.cpp`) — a multiline `QPlainTextEdit`
  wearing Qt's custom-completer pattern; commits on focus-out, because
  Enter means "new line" now.
- **`drawFlowedText`** (`AgendaWidget.cpp`) — budget-aware newspaper flow
  for block text (§3.34): columns appear when they earn something, and
  making room for the neighbour counts.
- **`isCompactScreen` / `makeTouchScrollable`** (`Widgets.h`) — the whole
  phone layout hangs off these two helpers.
- **`tests/test_ui.cpp`** — the first UI suite: real widgets, driven
  offscreen, reserved for bugs only a living widget tree can express.
- **`tools/screenshot.cpp`** — now takes a window size (phone renders) and
  probes layout minimums + the settings path (`TICKTIMER_PROBE=1`).
- **`docs/ANDROID.md`** — the click-by-click phone build guide.

## 6. New since v15 — the networked arc's landmarks

- **`ticktimer-server`** (`server/`) — a second program in the same repo: a
  headless `QCoreApplication` speaking hand-rolled HTTP/JSON over
  `QTcpServer`. Run it, and it prints every address a device can reach it at.
- **`PasswordHash.h`** — salted, stretched, self-describing password
  storage; the plaintext dies inside the register call and is never written.
- **`SyncPlan.h::decide()`** — the entire sync decision as a pure
  four-row truth table (*server moved? × local changed?*). If you read one
  file to understand sync, read this one.
- **`SyncService`'s `m_applying` guard** — the subtle one: applying a pull
  fires `changed()`, which the same service listens to for its dirty flag —
  the guard stops a pull from marking itself dirty forever. Same family as
  `LabelEdit`'s focus-reason guard: recognising your own reflection.
- **Session tokens** (`AuthServer` + `SyncClient`) — the password proves
  identity once; every later call carries a `Bearer` token instead. The
  token *is* the identity — sync requests carry no username at all.
- **`tests/test_login_live.cpp`** — the first end-to-end test: real server
  process, real socket, a two-'device' conflict playbook. Reserved, like
  the UI suite, for bugs that only exist in the seam between two programs.

## 7. New since v20 — the model/view, AI, and hierarchy arcs' landmarks

*(Added in the v27.0 docs audit — this guide had stopped at the networked
arc while five more shipped. Same rule as every section above: one
landmark per idea, the file that teaches it best.)*

- **`TaskSnapshotModel` → `TaskListModel` / `CategoryTaskModel`** — the
  deferred Qt lesson, done properly: snapshot + granular diff instead of
  reset-the-world. `design-addendum-model-view.md`.
- **`QuickAddParser` + `nlp::llm`** (`ParsedTask`) — one vendor-neutral
  contract, three capture surfaces, and an LLM fallback that is fully
  testable offline because the wire (`LlmQuickAddClient`) holds nothing
  but POST and timeouts. `design-addendum-quickadd.md`.
- **`LlmProvider.h`** — the whole provider layer in one header: Provider =
  base URL + dialect + model + key; two dialects cover the world. Then
  §L (reasoning models: the `<think>` scrub and the silence fallback —
  V72's bug class on the other path) and §M (per-role routing: silence
  falls through, speech does not; the breaker; migration by derivation).
  `design-addendum-provider.md`.
- **`ChatSession.h`** — a conversation as a value: the transcript, the
  send-window budget, local-only turns the model never sees, and (v25.3)
  the four-band prompt — contract → floors → persona → context, two bands
  locked. `design-addendum-chat.md`, esp. §K.
- **`AppData::addSubtask` and the five query policies** — one field
  (`parentId`), five per-query decisions, a no-auto-complete roll-up, and
  an archive cascade that must be its own inverse.
  `design-addendum-subtasks.md`; diagram `subtask_policies.*`.
- **`tests/test_nlp.cpp`** — where the pure AI layer lives its offline
  life: forged reply bytes, breaker clocks passed in by hand, and the
  `TICKTIMER_AI_DOWN` hook driving real fall-through with no network.

## 8. New since v26 — the settings-nav and catch-up arcs' landmarks

- **`SettingsPages.h` / `SettingsDialog.h`** — the junk-drawer refactor:
  a shell that knows no concrete page, `save()` as a four-line loop, and
  the receipt cashed one version later when `CatchUpSettingsPage` cost one
  class and one line. `design-addendum-settings-nav.md`.
- **`MissedBlocks.h`** — the smallest complete example of *derive the
  judgement, store the decision*: a missed block is a pure function, a
  skipped one is a fact. Read it next to `Event::BlockOutcome`.
- **`Reschedule.h`** — a proposer that returns a **ranked list and is
  allowed to return nothing**: the five-rung ladder, planning against the
  shortfall, and the empty hand as an honest answer. Diagram
  `catch_up_ladder.puml`.
- **`CatchUpCard.h`** — a chip with three intensities driving a
  `SlidePanel` drawer; the one *persistent* widget (restyled, never
  rebuilt) that makes the v22.2 bug class impossible; snooze as
  de-emphasis, never a lock. Diagram `catch_up_chip_states.puml`.
- **`design-addendum-catch-up.md` §L** — read this one *for the process*:
  how a two-slice side feature became twelve versions once real usage
  arrived, what each round taught, and when derailing from the roadmap is
  the right call. The most portfolio-worthy pages in the repo.

