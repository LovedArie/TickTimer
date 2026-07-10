# Troubleshooting Log

A symptom-indexed log of build/run problems and their fixes. When you hit an
error, **search this file for the error text** — entries are keyed on the
symptom you actually see, not on the order they happened.

> **Using this as a template for a new project?**
> Keep everything from here down to the `── LOGGED ISSUES ──` divider — that part
> is reusable. Delete the project-specific entries *below* the divider and start
> a fresh log for the new project.

---

## How to debug a build/run failure (general method)

Before reaching for a specific fix, find out **which stage failed**. A C++
program passes through three stages, each with its own failure mode and its own
class of fix. Reading the stage first stops you from fixing the wrong thing.

```
compile  →  is this one .cpp valid?        (compiler)   → "error:", "not declared"
link     →  do all the pieces connect?     (linker)     → "undefined reference"
load/run →  can it find its DLLs at start? (OS loader)  → exit 0xC0000135, no window
```

Three habits that resolved every issue in this log:

1. **Read which stage failed.** The error text tells you: a compiler `error:`, a
   linker `undefined reference`, or a launch failure with an exit code. Fix at
   the stage that actually broke.
2. **Verify with artifacts, not theories.** When unsure, look at the real files
   the build produced (generated sources, object files, the exit code) instead
   of guessing. The evidence beats confident reasoning every time.
3. **When targeted fixes keep missing, stop and research.** If two or three
   aimed fixes don't move the error, you have the cause wrong. Search the exact
   error text; read official/community sources; *then* act. One evidence-based
   fix beats six guesses.

---

## Entry format

Every logged issue uses the same four-part shape, because a stuck reader wants
the same things in the same order:

- **SYMPTOM** — the exact error or behavior, copy-pasteable (what you search for).
- **CAUSE** — why it happens (so you understand it, not just paste a fix).
- **FIX** — the precise steps that resolved it.
- **PREVENT** — how to stop it recurring (the part that makes this log compound).

### Blank template (copy this for a new entry)

```markdown
### <short symptom title>

**SYMPTOM**
<exact error text or observed behavior>

**CAUSE**
<why it happens>

**FIX**
<the steps that resolved it>

**PREVENT**
<the habit / setting / check that stops it recurring>
```

---

## ── LOGGED ISSUES (TickTimer) ──

### Program exits instantly with code -1073741515 (0xC0000135) — prints nothing, terminal returns straight away

**SYMPTOM**
Running `ticktimer.exe` or `ticktimer-server.exe` from a plain terminal
(PowerShell/cmd) returns the prompt immediately with no output. Checked via
`.\prog.exe ; echo "exited with code $LASTEXITCODE"`, the code is
**-1073741515** — hex `0xC0000135`, Windows' "a required DLL was not found".
The SAME program runs fine from Qt Creator's ▶.

**CAUSE**
Qt Creator quietly adds Qt's `bin` folder to the DLL search path when it
launches a program. A bare terminal doesn't, so Windows looks only beside
the `.exe`, can't find `Qt6Core.dll` / `Qt6Network.dll` (and, on MinGW,
`libgcc_s_seh-1.dll`, `libstdc++-6.dll`, `libwinpthread-1.dll`), and aborts
before `main()` produces any output.

**FIX**
Copy the DLLs next to the exe with Qt's deploy tool. From the **Qt command
prompt** (Start menu → "Qt 6.11.0 (MinGW 64-bit)"), in the build folder:
```
windeployqt ticktimer-server.exe
windeployqt ticktimer.exe
```
Then the program runs from any terminal. If it still fails, re-run
windeployqt and confirm the three MinGW runtime DLLs above landed beside the
exe — windeployqt usually copies them, but not always on MinGW builds.

**PREVENT**
- Deploy once with `windeployqt` after the first build of a new machine or a
  new target. It's also the exact step that lets the app run on a computer
  with no Qt installed — i.e. how you ship it.
- Quick test without deploying: run from the Qt command prompt, which
  already has Qt's DLLs on its path.
- Full launch manual: `docs/RUNNING.md`.

### "Could not find the Qt platform plugin 'windows'" / "no Qt platform plugin could be initialized"

**SYMPTOM**
The GUI app (`ticktimer.exe`) starts, then a dialog pops:
*"This application failed to start because no Qt platform plugin could be
initialized."* The console shows
`qt.qpa.plugin: Could not find the Qt platform plugin "windows" in ""`.
The headless server (`ticktimer-server.exe`) does NOT hit this.

**CAUSE**
A GUI Qt app needs a **platform plugin** — on Windows, `qwindows.dll` — to
talk to the windowing system. It isn't a normal DLL beside the exe; Qt looks
for it in a `platforms\\` subfolder. This is the sibling of the missing-DLL
error above, but GUI-specific: a `QApplication` needs the platform plugin
(plus `styles\\`, `imageformats\\`) that a headless `QCoreApplication` does
not. Running `windeployqt` on the SERVER only doesn't help the APP — each
executable must be deployed, because windeployqt inspects each one and copies
what *that* program needs.

**FIX**
Deploy the GUI app specifically, from the **Qt command prompt**, in the build
folder:
```
windeployqt ticktimer.exe
```
Watch for it creating a `platforms\\` folder (with `qwindows.dll`). Then
`.\\ticktimer.exe` opens its window.

**PREVENT**
- Run `windeployqt` on EVERY executable you'll launch outside Qt Creator, not
  just one — GUI apps and headless servers need different plugin sets.
- Symptom-to-cause shortcut: *"missing DLL, exits instantly"* = core/network
  DLLs (the entry above); *"no platform plugin"* = GUI plugin folders (this
  entry). Same tool, different deploy target.
- Full launch manual: `docs/RUNNING.md`.

### QNetworkReply says NetworkError, but the server answered fine (curl works) — often on every SECOND request

**SYMPTOM**
`QNetworkAccessManager` requests intermittently finish with a network error
(e.g. `reply->error()` = 206 or similar), yet the response BODY is present
and correct — and `curl` against the same server works flawlessly every
time. Classic pattern: alternating requests fail (1 ✓ 2 ✗ 3 ✗ 4 ✓),
especially when requests are driven from a modal dialog or a test's nested
event loop.

**CAUSE**
Three independent causes STACKED — fixing any one alone leaves it broken:
1. `reply->error()` is non-zero for perfectly valid HTTP 4xx answers (409
   Conflict, 401 Unauthorized), not just transport failures. Branching on it
   throws away a correct response.
2. The hand-rolled server sent an EMPTY HTTP reason phrase
   (`HTTP/1.0 409 \r\n`) — legal per spec, mishandled by some clients under
   connection reuse.
3. QNAM pools and REUSES connections; a one-request-per-connection server
   closes the socket after each response, and a request fired from inside a
   nested event loop travels down the dead socket.

**FIX**
1. Branch on `reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)`:
   status present → the server ANSWERED, parse the body; status absent →
   real transport failure.
2. Send real reason phrases ("Conflict", "Unauthorized") from the server.
3. `manager.clearConnectionCache()` before every request against a
   one-shot server.

**PREVENT**
- Diagnose client/server splits with `curl` FIRST — if curl is clean, the
  bug is on the client side of the seam.
- Bugs that live in the seam BETWEEN two programs need a live test
  (`test_login_live` spawns the real server); a mock on either side hides
  them by construction.
- New network client code starts with the two lines above (status-attribute
  branch + cache clear) — they're the QB section M lessons, pre-paid.

### CMake Error at target_link_libraries: "Target … links to: Qt6::Network … but the target was not found"

**SYMPTOM**
Configure (not compile) fails the moment a target links a Qt module:
`Target "ticktimer-server" links to: Qt6::Network ... but the target was
not found.` Ninja may also report `rebuilding 'build.ninja': subcommand
failed`.

**CAUSE**
The module was never REQUESTED. `find_package(Qt6 REQUIRED COMPONENTS
Widgets Test)` only creates the `Qt6::Widgets` and `Qt6::Test` targets —
`Qt6::Network` doesn't exist as a name until it's in that COMPONENTS list.

**FIX**
Add the component: `find_package(Qt6 REQUIRED COMPONENTS Widgets Test
Network)`, then reconfigure.

**PREVENT**
Adding a Qt module is ALWAYS two edits: the COMPONENTS list at the top and
the `target_link_libraries` line at the target. One without the other fails
— this direction at configure time, the other direction at link time.

### `undefined reference to vtable for <Class>` / `undefined reference to <Class>::someSignal(...)`

**SYMPTOM**
Everything compiles; the LINK fails with `undefined reference to 'vtable
for AuthClient'` or an undefined reference to one of your own signals.

**CAUSE**
The class has `Q_OBJECT`, but its HEADER isn't listed in the target's
sources — so AUTOMOC never scanned it, and the moc-generated code (which
defines the vtable and every signal's body) was never compiled into this
target. Common when a second target (a test, a tool) reuses a .cpp whose
Q_OBJECT header lives in `include/`.

**FIX**
List the header explicitly in the target's sources:
`qt_add_executable(test_x  include/AuthClient.h  src/AuthClient.cpp …)`.

**PREVENT**
House rule: every new `Q_OBJECT` header goes into the source list of EVERY
target that compiles its .cpp. Signals have no hand-written bodies — if moc
didn't run, they don't exist, and only the linker notices.

### AutoMoc: "contains a Q_OBJECT macro, but does not include <file>.moc"

**SYMPTOM**
`AutoMoc error: "SRC:/tests/test_login_live.cpp" contains a "Q_OBJECT"
macro, but does not include "test_login_live.moc"!`

**CAUSE**
For a Q_OBJECT class defined inside a .cpp, the file must end with
`#include "<own-basename>.moc"` — and the name is derived from the FILE'S
name. This file was written as `logintest.cpp` (with `#include
"logintest.moc"`) and then renamed; the stale include no longer matched.

**FIX**
Make the `.moc` include match the .cpp's actual basename.

**PREVENT**
Renaming or copying a test file = updating its `.moc` include in the same
motion. The error message names the expected include exactly — read it.

### "return-statement with no value" inside a test HELPER using QTRY_*/QVERIFY/QCOMPARE

**SYMPTOM**
`error: return-statement with no value, in function returning
'AuthClient::Outcome' [-fpermissive]` — pointing at a QTest macro line.

**CAUSE**
QTest's assertion macros expand to a bare `return;` on failure. That only
compiles inside a `void` function — i.e. a test slot. A value-returning
helper (like an `await()` that returns the outcome) cannot use them.

**FIX**
In value-returning helpers, use the QEventLoop idiom instead: connect,
fire, `loop.exec()` with a `QTimer::singleShot` timeout guard, return the
captured value. Assert with QCOMPARE back in the (void) test slot.

**PREVENT**
QTest macros live in test slots only; helpers return data and let the slot
judge it.

### A test HANGS (or only its timeout fires) while awaiting a signal

**SYMPTOM**
A QEventLoop-based await never returns — the test sits until its safety
timeout (or forever without one), even though the operation "obviously"
completed.

**CAUSE**
The awaited signal fired SYNCHRONOUSLY, during the call that triggered it —
before `loop.exec()` ever started. The lambda's `loop.quit()` ran against a
loop that wasn't running yet, so the quit was lost. Here:
`SyncService::resolveUseServer()` applies held data and emits `finished()`
with no network hop in between.

**FIX**
Arm the loop FIRST, fire from WITHIN it:
`QTimer::singleShot(0, fire);` then `loop.exec();` — the deferred call runs
as the loop's first event, so the emission (sync or async) lands inside a
running loop.

**PREVENT**
When awaiting a signal that MIGHT be synchronous, never call the trigger
directly before `exec()`. `singleShot(0, …)` costs nothing and is correct
for both cases.

### A test that passed for HOURS goes red — with no code change touching it

**SYMPTOM**
A previously green test fails "spontaneously". The failing value looks like
the OPPOSITE case of what the test set up (e.g. a block pinned to the
future being reported as "passed").

**CAUSE**
Two clocks in one decision. The verdict used an injected test clock
(`nowProvider`, pinned at 11:30), but a related output — the dialog's
explanation text — read `QDateTime::currentDateTime()`. The REAL clock
crossed the test block's 5 PM boundary mid-session, and verdict and
explanation started disagreeing.

**FIX**
Derive the decision AND everything describing it from the SAME `now`
(here: the hint now reads `m_tracker->nowProvider()`).

**PREVENT**
A verdict and its justification must share inputs. If a test can only fail
at certain times of day, some code path is still on the wall clock — grep
for `currentDateTime` in everything the test touches.

### AppData::addEvent (or another domain door) returns an EMPTY id — nothing was created, no error shown

**SYMPTOM**
An event/task/activity you just "created" doesn't exist; the returned id is
`""`. In tests: a later lookup or tracker call fails bewilderingly far from
the real cause.

**CAUSE**
The guarded door REFUSED — by design, silently (UC1 3a: decline and
indicate; the indication is the empty id). For events the usual reasons:
outside the domain day (`plan::kDayStartMinutes` — the day runs
**06:00–24:00**, so an "all-day" 0–1440 block is invalid), overlapping an
existing block, or a missing identity (unknown activity/task id).

**FIX**
Check the window bounds (360–1440), overlaps, and identity. In tests,
assert creation immediately: `QVERIFY(!id.isEmpty());` right after every
door call — it turns a distant mystery into a named failure at the source.

**PREVENT**
Doors refuse silently on purpose; CALLERS assert. Any test fixture that
creates domain objects verifies each id before using it.


### "AutoMoc subprocess error … Not a signal or slot declaration" — and tests failing "impossibly" right after

**SYMPTOM**
Build output contains `AutoMoc subprocess error` and
`<Header>.h:NN: Not a signal or slot declaration`. Often paired with a
test failure that makes no sense against the code you just wrote.

**CAUSE**
Something other than a function declaration sits inside a `signals:` or
`slots:` section — here, a member variable
(`std::function<QDateTime()> nowProvider`) pasted into `public slots:`.
moc parses those sections itself and accepts only functions; the C++
compiler would have been fine with it, which is why the code "looks right".

The "impossible" test failure is the second half: the build DIED at moc, so
`ctest`/the test binary you ran was the STALE previous build — its
failures describe old code, not yours.

**FIX**
Move member variables (and non-slot helper queries) to a plain `public:` /
`private:` section; only functions belong under `signals:` / `slots:`.
Rebuild, confirm zero errors, THEN rerun tests.

**PREVENT**
- House rule: `slots:` sections contain function declarations only —
  data members and seams go in normal access sections.
- Never trust test output without checking the build log first: a failed
  build means the tests you just "ran" are yesterday's binary.

*Second sighting (sync session):* a `struct PullResult` declared inside a
test class's `private slots:` section died the same way — moc rejects TYPE
declarations there too, not just member variables. Diagnosed in minutes
because the error message was already logged here. That's this file working.

### 'makeTouchScrollable' / 'isCompactScreen' was not declared in this scope

**SYMPTOM**
After applying an update, the build fails with
`'makeTouchScrollable' was not declared in this scope` (and/or
`'isCompactScreen'`) across several .cpp files, then
`ninja: build stopped: subcommand failed.`

**CAUSE**
A PARTIAL update: the .cpp files that CALL the helpers landed, but
`include/Widgets.h` — the file that DECLARES them — didn't. Callers and
declarations shipped in the same update; applying half of it splits them.
"Not declared in this scope" is the compiler's way of saying "the caller
arrived, its header didn't" — the error names the missing FILE, not a bug
in the code you can see.

The usual way this happens on Windows: "Extract All" on a zip creates a
WRAPPER folder (`TickTime-android-ready\include\...`), so dragging things
across merges some folders into the project and misses others.

**FIX**
Re-apply the update so that every file in the zip replaces its counterpart —
in this case, restore the up-to-date `include/Widgets.h` (it must contain
`isCompactScreen()` and `makeTouchScrollable()`). A full sync pack of all
changed files is the belt-and-braces version: extract it INTO the project
folder (so `include`, `src`, `docs` merge with the existing ones), say Yes
to all overwrites, then Build → Rebuild.

**PREVENT**
- When extracting an update: open the zip, and drag its CONTENTS onto the
  project folder — don't let "Extract All" park them in a wrapper folder
  next to it. The overwrite prompt is the confirmation it merged; NO prompt
  means nothing was replaced.
- Quick self-check after applying: the compiler is the checksum. Rebuild
  immediately — "was not declared" right after an update almost always
  means a header from that update is missing, not that the code is wrong.

### App crashes when adding a task (or activity) in the Activities panel — but the task IS saved

**SYMPTOM**
Type a task name in the detail panel, press Enter (or click Add) → the whole
app dies instantly. Relaunch: the task is there, correctly created. Same can
happen when ticking a task done, deleting an activity, or any button inside
the Activities detail / Upcoming / Special-days content.

**CAUSE**
Use-after-free by self-destruction during a signal. The chain:

```
returnPressed ──► addTask() ──► emit changed()   [DIRECT connection = runs NOW]
      ──► rebuildDetail() ──► delete m_detail->takeWidget()
      ──► ...that widget CONTAINS the QLineEdit whose returnPressed
          handler is still executing on the call stack
      ──► stack unwinds into freed memory ──► crash
```

The task survives because the storage listener (save) completed before the
stack unwound. Data safe, app dead — which is exactly the diagnostic tell:
*crash-after-successful-mutation means the explosion is in a UI listener.*

Why it crashed on Windows but not the Linux test box: use-after-free is
UNDEFINED BEHAVIOUR — freed memory often still "works" until the allocator
reuses it. It even passed under AddressSanitizer here. Never read "it
doesn't crash for me" as "it's correct."

**FIX**
`deleteLater()` instead of `delete` at every rebuild site that discards
widgets which can emit into the rebuild:

```cpp
if (QWidget* old = m_detail->takeWidget())
    old->deleteLater();   // destruction deferred to the event loop
```

Applied in `ActivitiesPage::rebuildDetail`, `UpcomingPage`, and
`SpecialDaysPage`. (The due strip in PlannerPage already did this — the cure
existed in the codebase; three sites hadn't adopted it.)

**PREVENT**
- Rule of thumb: **a slot must never `delete` a widget that could be the
  sender or contain the sender.** If a rebuild runs off `changed()`, the old
  content dies via `deleteLater()`, always.
- The regression is now pinned by the project's first **UI test**
  (`tests/test_ui.cpp`, target `test_ui`): it replays the exact gesture and
  asserts the lifetime CONTRACT with a `QPointer` — the input must still be
  alive the instant the keystroke returns, and gone after the event loop
  spins. Deterministic red/green, immune to allocator luck.
- Run it like the domain suite: `ctest` (offscreen platform is set by CMake),
  or `QT_QPA_PLATFORM=offscreen ./test_ui` by hand.

*Everything below is specific to this project. Delete it when reusing this file
as a template.*

---

### Nonsense compile errors around a variable named `slots` (or `signals`, `emit`)

**SYMPTOM**
Compile errors that make no sense at the reported line, e.g.:
```
error: expected unqualified-id before '=' token
error: invalid type argument of unary '*' (have 'int')
```
…pointing at perfectly ordinary code like `const int slots = …;`.

**CAUSE**
Qt defines `slots`, `signals`, and `emit` as **preprocessor macros** for its
signal/slot syntax. A variable or parameter named `slots` is macro-erased
before the compiler sees it, so the compiler reports garbage about the line
that remains. The error location is honest; the visible code is not what got
compiled.

**FIX**
Rename the identifier (ours became `slotCount`). Rebuild — code-only change.

**PREVENT**
Never use `slots`, `signals`, or `emit` as identifiers in a Qt project. If a
baffling error points at an innocent-looking line, check it for those three
words first. (War story recorded in `include/Widgets.h`.)

---

### The calendar (or any scroll-area content) renders BLACK on Windows dark mode

**SYMPTOM**
App builds and runs; most of the UI is themed correctly, but the agenda /
scrollable regions have a near-black background. Only on machines with the OS
in dark mode (the window title bar is dark too).

**CAUSE**
Qt themes through **two layers**: the stylesheet AND the `QPalette`. Qt 6.5+
follows the OS colour scheme, handing the app a **dark palette** — and any
widget that paints from the palette shows it. The agenda specifically:
`QScrollArea::setWidget()` silently turns ON the child's
`autoFillBackground`, so a widget that never painted a background starts
filling itself with the palette's (now dark) Window colour. A stylesheet-only
theme leaves this whole layer to the OS.

**FIX**
Pin both layers in one place: `theme::applyTheme(app)` sets the Fusion style,
an explicit light `QPalette`, and the stylesheet (plus, on Qt ≥ 6.8, requests
the light colour scheme so the title bar follows). The agenda also paints its
own background explicitly in `paintEvent`.

**PREVENT**
A theme must pin **both** layers, and `theme::applyTheme()` is the only
sanctioned setup — every new entry point (tools, harnesses) must call it.
Custom-painted widgets own every pixel they show, background included.
Reproduce-then-verify: this fix was proven by simulating a dark OS palette
and pixel-checking before/after (screenshots in `docs/`).

---

### A toolbar button renders EMPTY (its Unicode icon is missing)

**SYMPTOM**
A button that should show a glyph (e.g. the hamburger `☰`, U+2630) renders as
a blank button. No error anywhere. May look fine on one machine and empty on
another.

**CAUSE**
Unicode-as-icon is a **font-coverage lottery**: not every font carries every
codepoint, and what renders depends on the machine's font fallback chain.
U+2630 is missing from many common fonts.

**FIX**
Switch to a glyph virtually every font carries (`≡`, U+2261) — or ship a real
`QIcon`.

**PREVENT**
For icons, prefer `QIcon` resources; if using a text glyph, pick from the
boring well-covered ranges and verify on a second environment (our
screenshot tool + pixel check caught this one headless).

---

### `QFont::setPointSizeF: Point size <= 0` warnings; oddly-sized text

**SYMPTOM**
Runtime stderr warnings:
```
QFont::setPointSizeF: Point size <= 0 (-2.500000), must be greater than 0
```

**CAUSE**
On some platforms the default `QFont` is **pixel-sized**, so
`font().pointSizeF()` returns **-1**. Naive arithmetic like
`pointSizeF() - 1.5` then produces an invalid size. Multiplication is just as
broken (`-1 * 2.6`).

**FIX**
Route every size tweak through one guarded helper — `scaledFont()` in
`include/Widgets.h` — which falls back to a sane point baseline when the font
is pixel-sized.

**PREVENT**
Never do raw arithmetic on `pointSizeF()`; always use the helper. One guard
in one place beats the same guard remembered in eight places.

---

### App data lands in a DOUBLED folder (`TickTimer/TickTimer/`) — or "vanishes" after a rename

**SYMPTOM**
The data file appears at `…/AppData/Roaming/<Name>/<Name>/data.json` (doubled
folder). Related: after renaming the application, the app starts **empty** as
if all data were lost — while the old file sits intact under the old name.

**CAUSE**
`QStandardPaths::AppDataLocation` is built as `<organization>/<application>`.
Setting both names to the same string doubles the folder. And because
`setApplicationName` **decides the data folder**, renaming the app silently
points it at a new, empty location.

**FIX**
Set only the application name (no organization). For the rename, ship a
one-time bridge — `JsonStore::migrateLegacyData()` — that **copies** (never
moves) the legacy file into the new home on first launch.

**PREVENT**
Treat `setApplicationName` as **persisted-state-adjacent**: renaming anything
that decides where data lives requires a data bridge for existing users, and
migrations copy rather than move so the old file remains a free backup.

---

### `ctest` fails suites that pass when run by hand (aborts, or "waitForStarted returned FALSE")

**SYMPTOM**
Running each test binary directly: all green. Running `ctest`: the auth
suite **aborts** ("could not connect to display… could not load the Qt
platform plugin"), and the live suite fails in `initTestCase` because the
server never starts.

**CAUSE**
Two hidden assumptions about *how* the tests are launched. (1) Any test
linking Qt GUI libraries boots a platform plugin; with no display attached
(CI, ssh, ctest on a server) the default `xcb` aborts before `main()` gets
going — we'd only ever exported `QT_QPA_PLATFORM=offscreen` by hand.
(2) The live test spawned the server via the *relative* path
`./build-linux/ticktimer-server`, which only resolves if the current
directory happens to be the repo root — ctest runs tests from the build
directory, and Qt Creator's build folder isn't even called `build-linux`.

**FIX**
(1) `set_tests_properties(<test> PROPERTIES ENVIRONMENT
"QT_QPA_PLATFORM=offscreen")` in CMake — the launch requirement now travels
*with the test* instead of living in someone's shell history. (2) The test
finds the server **next to itself**: `QCoreApplication::applicationDirPath()
+ "/ticktimer-server"` — true by construction, since both binaries are built
into the same folder (Windows resolves the missing `.exe` itself).

**PREVENT**
A test that needs special launching isn't finished — encode environment and
paths in the test/build system, never in "remember to run it from the root."
Rule of thumb: `cd /tmp && ctest --test-dir <build>` must pass; if it
doesn't, the harness has an undeclared dependency on your habits.

---

## Sources

- Qt documentation — Signals & Slots (the `signals`/`slots`/`emit` macros and
  the `QT_NO_KEYWORDS` escape hatch).
- Qt documentation — `QPalette`, Qt 6.5+ colour-scheme following, and
  `QScrollArea::setWidget` (`autoFillBackground` behaviour).
- Qt documentation — `QStandardPaths` (organization/application path rules)
  and `QSaveFile` (atomic writes).
- General: font-fallback behaviour for uncovered codepoints is
  platform-dependent (general knowledge, verified empirically here).
