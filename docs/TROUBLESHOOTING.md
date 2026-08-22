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

### "No account with that name — check the spelling" in Share & compare, but the account definitely exists

**SYMPTOM**
Share & compare rejects a username with *"No account with that name —
check the spelling"* — while that exact account can log in, sync, and
even share in the OTHER direction (A→B works, B→A fails).

**CAUSE**
The same trailing-slash poison as the login entry below, one layer
deeper — and armed BY v29.0.1's fix. `LoginDialog::serverUrl()` returned
the raw field text; AuthClient normalized only its own internal copy, so
login now SUCCEEDED with a slash-bearing URL and quietly saved the raw
value to settings. ShareClient (the consumer the v29.0.1 patch missed)
concatenated `base + "/share"` → `//share` → route-level 404 — and its
classifier collapsed BOTH 404s ("no such route", "no such user") into
NotFound, so the app's own URL bug was printed as the owner's spelling
mistake. The asymmetry decodes as: whichever machine's saved URL carries
the slash is the one that can't share out.

**FIX**
Immediate, any version: restart the app, remove the trailing slash from
the Server field at login, log back in (this re-saves the clean value).
Since v29.0.2: unnecessary — `LoginDialog::serverUrl()` normalizes at
the BIRTH of the value (every save and every consumer inherits it),
ShareClient normalizes defensively too, and its 404s are split:
`no_such_user` → the spelling message; anything else →
`UnexpectedReply`, "The server answered unexpectedly — check the server
address…".

**PREVENT**
The v29.0.1 postmortem line, upgraded by same-evening evidence: fixing
per-consumer misses consumers — normalize where the value is BORN, and
keep consumer-side normalization only as defense in depth. And the
taxonomy rule again, second conviction in one night: when a classifier
knowingly collapses "their typo" and "our bug" into one message (the old
code's comment did it with a straight face), the debugging bill lands on
whoever trusts the message. Tests pin the slash-bearing share, the
genuine-typo NotFound, and the wrong-path UnexpectedReply.

### "Please check your details and try again" when creating an account from another machine

**SYMPTOM**
On a second machine, the login dialog rejects Create account with
*"Please check your details and try again"* — while the same server address
opened in a browser DOES answer (with `{"error":"not_found","ok":false}`,
which proves the network path is fine).

**CAUSE**
The Server field ended with a slash (`http://10.61.241.202:8080/` — the
natural paste). The client appended `/register`, producing `//register`;
the server's route match is exact, so it answered 404 `not_found`; and the
client's error mapping collapsed every unrecognized token into
"InvalidInput" — a message that blames the credentials when the problem
was the URL. Two distinct causes, one misleading sentence: that collapse
cost the live debugging session.

**FIX**
Immediate: remove the trailing slash from the Server field.
Since v29.0.1: unnecessary — `AuthClient::normalizeServerUrl` strips
trailing slashes (and whitespace) at every entry point, SyncClient shares
the same rule (same landmine, same base URL), and an unrecognized server
error now shows its own message ("The server answered, but not in a way
this app understands…") instead of impersonating a typo.

**PREVENT**
Three tests pin it: the normalization table, register-with-trailing-slash
end-to-end (the exact failing call), and wrong-path →
UnknownServerReply. The doctrine line: when a user can paste it, the
program normalizes it — vigilance is not a mechanism. And error
taxonomies earn their keep at the CATCH-ALL: the unknown case must name
itself, because it is precisely the case you didn't foresee.


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
words first. (War story recorded in `include/Widgets.h`.) See also the
`redefinition of 'sync'` entry below — the same shape one layer down, where
the collision comes from the platform's libc rather than from a Qt macro.

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

### Auto-sync "stops working": edits never push, the Sync button shows ⚠, the dialog offers no choice

**SYMPTOM**
Edits stop reaching the server. The Sync nav button shows **⚠**, but
opening the Sync dialog shows only "changes waiting" and a Sync-now
button — no conflict-resolution choice anywhere. Auto-sync appears dead.

**CAUSE**
Two layers. (1) *By design*: a sync conflict PAUSES auto-sync — "auto
means auto-WHEN, never auto-WHO-WINS" — so until a human resolves,
nothing pushes. The classic trigger is a device whose sync state says
revision 0 (fresh install, new account key) meeting a server that
already holds data: dirty + rev-0 + server-ahead = conflict, sometimes
seconds after login. **If conflicts RECUR endlessly**, the usual cause is
TWO running copies of the app on the same account (dev build + installed
copy, or a leftover test window) auto-syncing against each other — each
is a "device", and because the sync unit is the whole planner, ANY
concurrent editing is a total conflict, even on different days. One
running copy per account. (2) *A real bug hid the way out*: the
"show the held conflict when the dialog opens" code had been inserted by
an anchored text edit that matched the FIRST `refreshInfo()` in the file
— inside the finished-lambda — instead of the constructor's. It
compiled, tests passed, and the dialog opened blind to the held
conflict. **Position bugs survive compilers.**

**FIX**
The check moved to the constructor (opening the dialog now shows the
resolution box whenever a conflict is held), and the ⚠ became DERIVED
from `hasPendingConflict()` after every event rather than toggled by
events — an event-toggled glyph can strand lit. To unblock: open Sync,
pick a winner (usually "Keep mine" if this device has the newest work).

**AND IF IT RECURS AFTER RESOLVING (fixed in 19.2.4):** the held-conflict
REVISION was never cleared by the resolve paths (only the held data was),
and `hasPendingConflict()` had been bolted onto that exact field — so one
conflict gated auto-sync and lit the ⚠ forever, resolved or not. The fix
is a single `clearHeldConflict()` both resolutions call; a live test now
creates a real conflict, resolves it, and asserts the service returns to
life. Lesson: when old state gains a NEW consumer, audit every write site.

**PREVENT**
Anchored/string-based edits: after applying, READ THE DIFF IN CONTEXT —
the compiler checks syntax, never placement. And UI that reflects state
should re-derive it (§3.5 for glyphs), never accumulate toggles.

---

### "This program might not have installed correctly" every time the app closes

**SYMPTOM**
On closing TickTimer (or the server), Windows shows the **Program
Compatibility Assistant**: *"This program might not have installed
correctly"*, offering to reinstall with compatibility settings — for a
program that was never an installer at all.

**CAUSE**
Two ingredients, both required. (1) PCA guesses whether a program is an
installer from **keywords in its version metadata** — "setup", "install",
"patch", **"update"** — and our server's `.rc` FileDescription proudly
said *"…sync, sharing, updates"*. A suspected installer that exits
without registering an installed program looks, to PCA, like a failed
install. (2) The heuristics only apply to programs **without an
application manifest** — and MinGW, unlike MSVC, embeds none by default,
so both exes were fair game for guessing.

**FIX**
(1) An embedded manifest (`installer/ticktimer.manifest`, resource
`1 24` in both `.rc` files): `asInvoker` + the supportedOS list formally
declares "modern app, no elevation, stop guessing" — PCA disables its
heuristics for manifested programs. (2) The trigger word removed from the
description. Users who already saw the dialog can also click *"This
program installed correctly"* once — PCA remembers per exe.

**PREVENT**
Version metadata is machine-read, not just human-read: keep installer
vocabulary ("update", "setup", "patch") OUT of FileDescription /
ProductName for anything that isn't one. And every Windows exe ships with
a manifest — it's the difference between telling Windows what you are and
letting it guess.

---

### "I updated / rebuilt, but the version didn't change" (dist exe, installer, or the banner disagree with you)

**SYMPTOM**
New code was added to the project (or downloaded from GitHub), the deploy
script ran clean — yet `dist\ticktimer.exe` → Properties still shows the
old File version, or the update banner keeps announcing a version you
believe you already installed.

**CAUSE — four flavours, all field-collected:**
(1) **Nobody bumped the number.** The version is a fact hand-written in
`include/Version.h`; the build only REPRINTS it. New code under an
unchanged number is legal — and unverifiable.
(2) **The new files never landed.** Extracting a zip over the project and
choosing *Skip* (or extracting into a nested subfolder) leaves the old
`Version.h` — and everything else — in place.
(3) **The installer wasn't recompiled.** Running an old `Setup.exe`
faithfully reinstalls the old build.
(4) **GitHub's shelf is stale.** `releases/latest` serves whatever was
last *published* — if `version.json` announces 19.1.0 but the newest
published release still holds 19.0.0 assets, "downloading the latest"
reinstalls the old version and the banner rightly returns.

**FIX**
Walk the proof points in `docs/GITHUB.md`'s release routine, in order:
`Version.h` says the new number → dist exe Properties says it → the
freshly-compiled Setup says it → the published release holds those files
→ `version.json` announces it. The first proof that fails is the culprit.

**PREVENT**
Treat the version number as the receipt: every delivery of new code comes
with a bump, so "did the update take?" is always answerable by Properties
→ Details. And bump BOTH files (`Version.h` + `ticktimer.iss`) — the one
manual seam.

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

### Mini timer vanishes when the main window is minimized

**SYMPTOM**
The always-on-top Pomodoro mini card only stays visible while the main
window is up; minimize (or un-maximize) the app and the card disappears
with it — despite `Qt::WindowStaysOnTopHint` being set. (Owner-reported,
v19.5.0.)

**CAUSE**
The card was constructed with the main window as its QWidget parent —
intended as a *memory-only* arrangement ("Qt splits who-deletes-me from
am-I-my-own-window"). But on Windows a parent on a top-level widget is
never only memory: Qt maps it to a **Win32 owner**, and Windows hides all
owned windows while their owner is minimized. A hidden window can't be
on top of anything; the flag never gets a vote.

**FIX**
Construct the card with `nullptr` parent, then pay back the two services
the parent provided silently:
1. memory — `~PomodoroPage()` deletes the card by hand;
2. quit accounting — `setAttribute(Qt::WA_QuitOnClose, false)`, so an
   open card doesn't keep the app alive after the main window closes
   (without it: close the app, and a zombie process keeps running one
   tiny floating card).

**PREVENT**
The regression test `miniTimerSurvivesTheMainWindowMinimizing` pins the
arrangement (parentless + WA_QuitOnClose off + the flags) — offscreen CI
can't observe real Win32 stacking, but it can forbid the setup that
caused it. Rule of thumb: for any window meant to outlive or float over
its opener, treat a widget parent as **platform behaviour**, not
bookkeeping — the window manager reads it as ownership.

---

### Fake-clock tests show 0s of live time (a seam with holes)

**SYMPTOM**
With `tracker.nowProvider` set to a fake clock, anything derived from the
live interval stays frozen: the live badge's clock doesn't tick between
two grabs, `liveSeconds()` returns 0 (or garbage), the glance panel's
live box shows "0s". Meanwhile one old test that SHOULD have caught it
stays green — because it `qWait(1100)`s real time instead of moving the
fake clock. (Found v19.6, while pixel-verifying the new badge.)

**CAUSE**
The `nowProvider` seam was only MOSTLY installed: four methods inside
TrackerService still called `QDateTime::currentDateTime()` directly —
`liveSeconds()`, `beginInterval()`'s start stamp, the heartbeat's
`touchRunning`, and `commitCurrentInterval()`'s end stamp. In production
both clocks are the same, so nothing ever LOOKED wrong; under a fake
clock, live time mixed two different clocks. Worse, the leak had shaped
a test: `liveDistractedTimeIsNotCountedAsBreak` slept real milliseconds
to make the wall clock move — green for the wrong reason, and 1.1 s
slower than it needed to be every run.

**FIX**
`grep -n currentDateTime` over the whole service; route every hit
through `nowProvider()` (the only legitimate `currentDateTime` left is
the default provider's own definition). Rewrite the sleeping test to
advance the fake clock by hand — the UI suite dropped from ~1.3 s to
~0.2 s, the removed sleep being the entire difference.

**PREVENT**
When installing a time seam, audit by GREP, not by symptom — holes come
in families, because the habit that made one made the others. And treat
any `qWait`/sleep inside a test as a smell: it usually means some code
under test is reading a clock the test can't reach.

---

### Notifications: system beep instead of the chime, and no popup at all

**SYMPTOM**
(v19.8 on the owner's machine) Phase/block notifications play the plain
Windows beep instead of the app's chimes, and no popup appears — though
the sound proves the handler ran. (Owner-reported.)

**CAUSE**
Two independent rented pipelines, both failing quietly:
1. *Sound* — Qt Multimedia is an OPTIONAL module in the Qt installer,
   and the owner's kit doesn't have it. The `#ifdef` fell back to
   `QApplication::beep()` exactly as designed — silently, which was the
   design's mistake. A degrade nobody can see is indistinguishable from
   a bug.
2. *Popup* — `QSystemTrayIcon::showMessage` does not show a popup; it
   SUBMITS one to the OS notification pipeline, which Windows may
   decline without error (Focus Assist, per-app notification settings,
   full-screen suppression).

**FIX**
Own both pipelines:
1. Three-tier sound: QSoundEffect → **winmm `PlaySoundW(SND_MEMORY |
   SND_ASYNC)`** (Windows' own API, always present, plays the real WAV
   from the resource with zero extra installs) → beep as the floor. The
   configure step now PRINTS the chosen tier (`TickTimer sound: …`).
2. `NotificationToast` — an app-owned always-on-top card (the mini
   timer's window recipe plus `WA_ShowWithoutActivating` and
   `WA_DeleteOnClose`), stacked top-right, auto-fading. A plain window
   cannot be suppressed by a notification pipeline. The tray icon stays
   for presence and click-to-raise only.

**PREVENT**
Two rules. *Degrade in steps, and never silently*: every fallback tier
must be visible at configure or run time. *Don't rent what must be
guaranteed*: if a feature's whole job is to interrupt (an alarm), it
cannot depend on a pipeline another program can switch off. Bonus Qt
timing scar from the toast's stacking test: `destroyed()` fires while
~QObject runs and QPointers to the dying object may not be null yet —
remove the sender from registries explicitly; trust nothing
mid-destruction.

---

### `undefined reference to __imp_PlaySoundW` — but only in test_ui.exe

**SYMPTOM**
(v19.9 on the owner's Windows/MinGW kit, Qt without Multimedia) The app
target builds; **test_ui.exe** fails at LINK with
`undefined reference to '__imp_PlaySoundW'`. Bonus warning on the same
build: `ignoring return value of QFile::open` (nodiscard in Qt 6.11).

**CAUSE**
`MainWindow.cpp` is compiled into THREE targets (ticktimer, test_ui,
screenshot-tool), but the winmm link requirement — and the
`TICKTIMER_HAS_MULTIMEDIA` definition — were attached to the app target
only. Same source, three .obj files, one of them promised a symbol
nobody offered it. The stage line in the log says it all: the compiler
was satisfied (the declaration exists); the LINKER wasn't (the library
wasn't on test_ui's line). Classic per-target sprinkling: when a
dependency is hung on one target instead of travelling with the source
that needs it, some other consumer of that source is eventually
forgotten.

**FIX**
An INTERFACE library, `ticktimer_sound`: it owns no code, only *usage
requirements* (the tier's link line + compile definition), and every
target that compiles `MainWindow.cpp` links it — app, test_ui,
screenshot-tool. One decision, made once, carried everywhere. The
nodiscard warning: check `open()`'s verdict (failed open → empty bytes
→ the existing beep fallback).

**PREVENT**
When one source file is compiled into several targets, its dependencies
must be expressed as something the targets SHARE — an interface/object
library — never repeated per target. And verify tier drops on purpose:
`cmake -DCMAKE_DISABLE_FIND_PACKAGE_Qt6Multimedia=ON` builds the
fallback configuration on demand, so "works without the module" is a
tested claim, not a hope. Zero-warning policy covers the owner's
compiler too — their Qt may be newer than the CI's (nodiscard arrived
with theirs).

---

### Sidebar state persists across launches, but window position / maximized state doesn't (v23.0)

**Symptom.** Toggle the rail with `Ctrl+B`, restart — remembered. Move or
maximize the window, restart — forgotten. One half of the same feature works,
the other doesn't, on the same machine, every time.

**Why the split is the whole diagnosis.** The two values were written at
different moments: the sidebar on every toggle (write-on-intent), the geometry
only in `closeEvent` (write-on-close). So "one persists and one doesn't" means
exactly one thing — **`closeEvent` never ran.** Qt Creator's Stop button, a
debugger detach, Task Manager, and a crash all kill the process without it.
The symptom isn't noise; it's the design's failure mode wearing a name tag.

**The trap.** It looks like a restore bug ("the app isn't reading the value"),
so the instinct is to debug `restoreGeometry()`. Check the *store* first:
`HKCU\Software\TickTimer\TickTimer` → `window\geometry`. Absent after a
"close" = the save never happened = stop debugging the restore.

**Fix (v23.1).** Geometry now debounce-saves ~1s after the window stops
moving/resizing/changing state (`moveEvent` + `resizeEvent` + `changeEvent`
restart a 1s single-shot `QTimer`; maximize arrives as `WindowStateChange`,
which is why the trio has three members). `closeEvent` keeps a final
belt-and-braces save but is no longer load-bearing. See
`design-addendum-window-memory` §E for the full tradeoff-and-revision story.

**The transferable rule.** `closeEvent` is a fine place for shutdown *work*
and a terrible place for the *only copy* of anything — a normal developer
workflow skips it dozens of times a day. And if your manual-test instructions
need a ⚠️ about how to exit the program, the warning is the design
apologising.

**Related.** If *neither* value persisted, that would be a different fault
entirely (QSettings organization/application name — see the v22 field bug).
And if geometry is skipped but the header's tagline is *also* missing, the
screen is being detected as compact (`isCompactScreen()`, min dimension < 600
logical px — display scaling counts) and the skip is by design.

### A QSettings write under `a/b/c` reads back empty — when `a/b` already exists as a value

**SYMPTOM**
`QSettings().setValue("ai/model/anthropic", …)` appears to succeed, but the
value reads back as an empty string. No error, no warning, nothing in the
debug output. (Found by `legacySettingsMigrateOnceAndOnlyIntoAnEmptySlot` on
its very first run, v24.)

**CAUSE**
QSettings uses `/` as a **group separator**, and a name cannot be both a
value and a group. v21 had stored `ai/model` as a plain value; writing
`ai/model/anthropic` asks for a group called `model` in the same place. The
backend cannot represent both, and the write is silently dropped.

**FIX**
Two changes, both kept: (1) the per-provider keys are **plural** —
`ai/keys/<id>`, `ai/models/<id>` — names nothing in v21 ever used as a value;
(2) `ai::migrateLegacySettings()` **removes the legacy value before writing
the new entry**, so even a future clash resolves in the right order.

**PREVENT**
QSettings' failure mode is *silence* — this is the project's second instance
(the first: the anonymous settings path, v22.7, logged above). Whenever a key
scheme changes shape (new nesting, renamed groups, org/app names), write a
test that round-trips through **real** storage, not just through the map you
think QSettings is. And never design a key hierarchy where a prefix of a new
key was ever a value in an old release.


### Build fails in EVERY file at once: `error: redefinition of 'struct PieceCount'`

**SYMPTOM**
The first build after applying a drop fails in dozens of translation units,
all with the same pair of lines:
```
AppData.h:59:8: error: redefinition of 'struct PieceCount'
Task.h:309:8: note: previous definition of 'struct PieceCount'
```

**CAUSE**
C++'s One Definition Rule: a struct may be defined once. It was defined in
TWO headers — one written by an interrupted session in the tail of `Task.h`,
one written by the re-land session into `AppData.h`, which had surveyed the
half-applied tree by grepping for the symbols it *expected* and never read
`Task.h` past ~line 240. Every file that (transitively) includes both
headers refuses, and widely-included headers reach the whole app — hence
everywhere-at-once.

**FIX**
`grep -rn "struct PieceCount" include/ src/` — keep exactly one definition
(it now lives in `AppData.h`, beside `pieceProgress()`, the query that
fills it), delete the other. v28.3.1.

**PREVENT**
When completing someone else's half-finished work, read every line of what
they left, not just the lines your search terms happened to match. Grep
answers the question you asked; only reading answers the ones you didn't.

### deploy-windows.bat stops immediately: "APPLY CHECK FAILED - the tree disagrees with itself"

**SYMPTOM**
The bat prints the versions from `include\Version.h` and
`installer\ticktimer.iss`, states they differ, and refuses to build.

**CAUSE**
Those two files ship together in every drop and must always agree. A
disagreement is the signature of a **half-applied drop** (unzip interrupted
or aimed at the wrong folder) — or a hand edit that bumped one file and
forgot the other. The check exists because half-applied drops happened
twice in this project and looked finished both times.

**FIX**
Re-unzip the drop over the project root, letting it overwrite everything.
Run the bat again; it re-checks before building.

**PREVENT**
Always apply drops with the full unzip-over-root, and let the bat's step 0
be the judge. The check is self-maintaining (it compares the pair rather
than hardcoding an expected number), but it cannot catch a *fully* stale
tree that agrees with itself — that's what the loud version banner is for:
eyeball it against the drop's filename.

### A test failure quotes a value that is no longer in the source

**SYMPTOM**
A fix was applied, the suite was rerun, and the SAME failure came back —
e.g. `Expected (870)` from a test whose source now says 780, at line
numbers that should have shifted.

**CAUSE**
Stale binaries. A test run proves things about the binaries it executed,
not the sources on disk. The first version of `run-tests.bat` ran ctest
without building, so "unzip the fix, run the tests" silently re-tested
yesterday's code.

**FIX**
Rebuild, rerun. Since v28.3.4 both bats build before testing
(`run-tests.bat` incrementally, `deploy-windows.bat` from scratch).

**PREVENT**
The diagnostic habit that caught it: when a failure message quotes a
literal, check whether that literal still exists in the source. If it
doesn't, you are not looking at a bug — you are looking at old code.

### Fifteen UI tests fail as "wrote a setting (or planted one), read it back empty"

**SYMPTOM**
Many unrelated-looking `test_ui` failures at once: settings dialogs save on
OK but reads return defaults; window geometry never persists; tests that
plant a setting to arrange a scenario (a stale review clock, a custom
endpoint, a chat route) find the widget behaving as if unarranged.

**CAUSE**
One cause behind all fifteen: `QSettings()` is scoped by BOTH application
identity names, and `test_ui` set only `applicationName` — with
`organizationName` empty, nothing persisted on Qt 6.11/Windows. The real
app sets both in `main.cpp`; `test_nlp` sets both and passed. This is the
project's THIRD instance of QSettings failing by *silence* (anonymous
settings path v22.7 and the plural-keys clash, both logged above).

**FIX**
A real `initTestCase()` in `test_ui.cpp` sets both names once,
process-wide. v28.3.2 — it turned 15 failures into 2.

**PREVENT**
Two habits. Any test binary that touches QSettings sets BOTH names in
`initTestCase`. And when several failures arrive together, read them for a
shared *signature* before treating them as separate bugs — failures that
share a signature share a cause.

### `moodUpsertsByDateAndRoundTripsThroughV12` fails: version is N, expected N-1

**SYMPTOM**
```
FAIL! : TestDomain::moodUpsertsByDateAndRoundTripsThroughV12()
   Actual   (root["version"].toInt()): 13
   Expected (12)                     : 12
```

**CAUSE**
That QCOMPARE is the **format-version tripwire**: it pins the storage
format's current number on purpose. v28.3.0 bumped JsonStore's literal to
13 (subtasks) and missed the pin. The failure is the tripwire working —
one drop late.

**FIX**
Bump the pin to match JsonStore's literal, in the same drop, every time.

**PREVENT**
A format bump greps the tests for the OLD number before shipping:
`grep -rn '"version"' tests/`. Two lines move together, always.

### Geometry/size assertions keep failing on the offscreen platform — differently each time

**SYMPTOM**
A window-size QCOMPARE fails; each "fix" of the expected number fails a
new way. Observed sequence here: expected 870 → got 798; expected 780 →
got 798; expected the *measured* 1166 → got 798.

**CAUSE**
The offscreen platform has a real (fake) screen — **800×600** — and no
fonts, so Qt's fallback metrics balloon layout minimums (this MainWindow
computes ~1166 minimum width, wider than the whole fake screen). A
window's width is then decided by three parties in sequence: the layout
minimum (pushes any smaller resize back up), the screen fit
(`restoreGeometry()` clamps blobs that don't fit), and WHEN each acts
relative to the first layout pass. None of them is the code under test —
the save/restore wiring was faithful in every run.

**FIX**
Stop asserting the number. The test now asserts exactly what its name
claims: restore *acted* (size differs from the untouched default) and
startup did not overwrite the stored blob. v28.3.6.

**PREVENT**
Pin invariants you own; never pin numbers the platform or layout owns.
And when an assertion keeps losing to the environment, stop refining the
number — ask what the test's NAME claims, and assert exactly that.

### A widget's `isVisible()` is false even though its show/hide wiring is correct

**SYMPTOM**
`QVERIFY(baseUrl->isVisible())` fails after programmatically switching the
provider combo to "custom" — while the matching negative check
(`!isVisible()` for a known vendor) always "passed".

**CAUSE**
Qt's `isVisible()` is *effective* visibility: a widget is visible only if
every ancestor is. The AI section lives on the Assistant page of a
QStackedWidget and the dialog opens on page 0 — so everything there
reports invisible regardless of its own flag. The positive assertion was
impossible; the negative one passed **vacuously**, proving nothing.

**FIX**
Navigate to the page first — the test finds the nav row by its title
("Assistant"), not by index, so reordering pages can't silently
re-vacuum it. v28.3.3. The app needed no change.

**PREVENT**
For any passing negative assertion, ask: *what would make this fail?* If
nothing reachable from the setup can, it isn't testing what it claims.

### `login_live` fails: "the requested timeout (3000 ms) was too short"

**SYMPTOM**
```
QTestLib: This test case check ... failed because the requested timeout
(3000 ms) was too short, 3600 ms would have been sufficient this time.
```

**CAUSE**
A `QTRY_*_WITH_TIMEOUT` ceiling was a bet on how fast this OS reports a
dead port; the machine took 3.6 s and the 3 s bet lost. OS network-stack
timing is not the code under test.

**FIX**
Raise the ceiling generously (15 s). v28.3.2.

**PREVENT**
QTRY ceilings are free: the macro returns the moment the condition holds,
so green runs stay fast. Make every ceiling generous enough that only a
genuine hang can hit it.

### ctest / a test exe dies with "Qt6Gui.dll was not found" from a plain command prompt

**SYMPTOM**
Running `ctest` (or a test exe) from a bare cmd/PowerShell pops the
Windows "code execution cannot proceed because Qt6Gui.dll was not found"
dialog. The same suite runs fine from `deploy-windows.bat`.

**CAUSE**
Same family as the 0xC0000135 entry near the top of this log: the test
executables link Qt's DLLs and a bare prompt has no Qt on PATH. The deploy
bat sets Qt's `bin` on PATH internally for its own ctest step; a hand-run
prompt doesn't.

**FIX**
Use `tools\run-tests.bat` (it does the same Qt discovery, rebuilds what
changed, runs ctest, and writes `test-results.txt`). One-off alternative:
`set PATH=C:\Qt\6.11.1\mingw_64\bin;%PATH%` first.

**PREVENT**
Hand-testing goes through `run-tests.bat`, full releases through
`deploy-windows.bat`. Neither leaves DLL resolution to the shell you
happen to be standing in.

### `fatal error: test_domain.moc: No such file or directory` — after editing the .cpp, and it survives every rebuild

**SYMPTOM**
A file that has always built suddenly can't find its own generated `.moc`,
on a line (`#include "test_domain.moc"`) you did not touch:
```
tests/test_domain.cpp:4710:10: fatal error: test_domain.moc: No such file
```
`run-tests.bat` shows only this. Rebuilding changes nothing — later runs
even report `Built target test_domain_autogen` as though the work were
done.

**CAUSE**
Two failures stacked, and the visible one is the second.

First: `moc` runs its own *simplified* C++ preprocessor before the compiler
sees anything. It does not lex raw string literals correctly. A
`R"RX( ... )RX"` added to the file left it hunting for a closing paren,
so it aborted with a message the build script never surfaced:
```
AutoMoc: tests/test_domain.cpp:1490:1: error: missing ')' in macro usage
```
No `.moc` was written, and the compiler blamed the include instead of the
cause — the same misdirection as a header missing from `CMakeLists.txt`
reporting "undefined reference to vtable".

Second, and why it looked unfixable: that aborted run still refreshed
`build-release/test_domain_autogen/timestamp`. AUTOMOC compares that stamp
against the sources, decided it was current, and skipped regenerating on
every subsequent build — including after the code was fixed.

**FIX**
See the real error first: `cmake --build build-release --target test_domain`
directly, not through `run-tests.bat`. Then rewrite the literal with
ordinary escapes (`"^#define\\s+AppVersion\\s+\"([^\"]+)\""`), and clear
`build-release/test_domain_autogen/` to break the stale-stamp loop. v29.2.

**PREVENT**
Don't use raw string literals in a file that carries `Q_OBJECT`. The
regex in `installerVersionMatchesTheHeader()` carries a comment saying so,
because the escaped version looks strictly worse and invites tidying.

### A test's own fixture data silently doesn't exist — a query over it returns the empty/default answer

**SYMPTOM**
A test creates domain objects (events, segments) and a later query
behaves as if they were never made — e.g. `personalMultiplier()`
returning its no-history default (1.0) despite three carefully built
sample tasks. No error anywhere; the failure surfaces at an assert far
from the cause.

**CAUSE**
The domain **refused the fixture** and the test never checked the door.
Here: the fixture planted 05:00–21:00 blocks, and `isFree` guards the
planner's day window (`plan::kDayStartMinutes` = 06:00) — every
`addTaskEvent` returned an empty id, and `appendSegment("")` is a quiet
no-op. The domain's gates apply to test data exactly as they apply to
users; that is them working.

**FIX**
Make the fixture legal (09:00–21:00 blocks) and `QVERIFY` **every**
door's return in fixture loops — `addTaskEvent`, `appendSegment`, all of
them — so a refusal fails AT the refusal. v28.4.1, test-only; the query
under test had been correct the whole time.

**PREVENT**
Two habits. When inventing fixture values, check what the passing tests
use and ask why before deviating (every existing test used 09:00 blocks
for a reason). And a returned id/bool from a domain door is a verdict,
not a formality — a refusal you don't check becomes a mystery three
asserts later.

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
- Qt documentation — `QSettings` default constructor (scoped by
  organization AND application names) and `QWidget::isVisible`
  (effective visibility requires visible ancestors).
- Qt documentation — the offscreen platform plugin (default 800×600
  screen; no font directory since Qt stopped shipping fonts), and
  `QWidget::restoreGeometry` (validates/fits restored rectangles
  against the current screen).

### Panel/card shows patchwork colors — white frame, grey middle

**SYMPTOM:** a styled container (white via stylesheet) renders with a
different-colored interior wherever a QScrollArea sits; the seam tracks
the scroll area's bounds exactly. Screenshot-visible only — every
offscreen test green.
**CAUSE:** `QScrollArea::setWidget()` turns ON the child widget's
`autoFillBackground`, so a widget that never painted a background
starts filling itself with the palette's Window color (Theme.h pins the
palette, so: the app grey). Documented in Theme.h since v3 (the
dark-mode agenda incident); re-hit in v28.6.1 by TaskDetailPanel.
**FIX:** `child->setAutoFillBackground(false);` immediately after every
`setWidget(child)` — including rebuilds. TaskDetailPanel does this in
buildFor.
**PREVENT:** the flag is pinned by test
(`theFormNeverFillsItsOwnBackground`), after open AND after a save's
rebuild — a tripwire that fails a patch instead of hoping to be read.

---

### `error: redefinition of 'sync' as different kind of symbol` — Android only, builds fine on Windows

**SYMPTOM**
The Android kit fails to compile a file that has built on Windows for months:

```
C:/.../include/SyncPlan.h:27:11: error: redefinition of 'sync' as different kind of symbol
   27 | namespace sync
C:/.../ndk/27.2.12479018/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/include/unistd.h:290:6:
      note: previous definition is here
  290 | void sync(void);
ninja: build stopped: subcommand failed.
```

The same file compiles cleanly with the MinGW desktop kit, and every desktop
suite is green.

**CAUSE**
Android's libc (**bionic**) declares POSIX `sync(2)` — the flush-filesystem-
buffers call — as `void sync(void)` at **global scope** in `<unistd.h>`, which
Qt headers pull in transitively. A file-scope `namespace sync` is then the same
name declared as a different kind of entity, which C++ forbids.

MinGW does not declare `sync()`, so Windows never saw a conflict. **The name
was wrong from the day it was written; only the platform was forgiving.** Note
what this means for diagnosis: nothing changed in `SyncService.cpp` (untouched
since v29.1) — the first Android compile simply reached code no Android
compiler had ever read.

**FIX**
Rename the namespace. Ours became `syncplan`, matching its header `SyncPlan.h`:
13 replacements across `SyncPlan.h`, `SyncService.h`, `Compare.h`, `Version.h`,
`SyncService.cpp` and `test_auth.cpp` — 7 real code lines, the rest comments
citing `sync::decide` as the pattern to copy. No behaviour changes, so no
version bump; the server binary does not include the header at all.

Nesting it as `ticktimer::sync` also works and was rejected: every namespace in
this project is bare (`version::`, `nlp::`, `chat::`, `missed::`, `intake::`,
`ai::`), so an outer namespace for one case costs consistency to buy exactly
what the rename buys.

**PREVENT**
**Check a new namespace name against POSIX before picking it.** The dangerous
set is larger than it looks — all of these are global-scope functions on
Linux/Android and would collide identically: `sync`, `link`, `unlink`, `index`,
`time`, `read`, `write`, `open`, `close`, `remove`, `kill`, `wait`, `select`,
`poll`, `send`, `recv`, `socket`, `stat`, `pipe`, `exec`, `div`, `chdir`.

Same family as the `slots`/`signals`/`emit` trap above, and worth pairing with
it mentally: **a name can be illegal without your compiler saying so.** There
the macro was invisible; here the platform was.

The structural lesson is bigger than the name. A desktop-only CI cannot catch
this class at all, so a platform that has never compiled the code is not
"probably fine" — it is **unmeasured**. v30.3 shipped "Android distribution"
(keystore, version stamping) without an APK ever reaching the compiler. v30.4
had the same gap for WebAssembly and *said so* at the top of `WEB.md` — "nobody
has opened it in a browser". Write that sentence when it is true.

---

### On a real Android phone the nav rail does NOT start collapsed — it eats half the screen, and a dialog on top of it swallows every tap

**SYMPTOM**
First run of the release APK on a phone (measured: Galaxy S21 Ultra,
`wm size` 1080x2400, `wm density` 480):

- the nav rail is fully expanded, about 470 of 1080 px wide
- the content pane starts around x=540 and is CLIPPED at the right edge —
  "6 AM to midnight · 30-m…", "Every dated, unfinished task across all your
  life areas — de…"
- opening Sync leaves you stuck: `SyncDialog` renders frameless and
  background-less over the main window, so it reads as part of the page, and
  because it is modal every tap on the visible nav behind it does nothing.
  `KEYCODE_BACK` does not dismiss it either. The only way out found was
  `adb shell am force-stop org.ticktimer.app`.

`docs/ANDROID.md` promised the opposite: *"Compact layout, automatically: the
nav rail starts collapsed (☰ opens it)"*.

**CAUSE**
`isCompactScreen()` (`include/Widgets.h`) returned **false** on a phone:

```cpp
const QRect g = screen->availableGeometry();
return qMin(g.width(), g.height()) < 600;
```

1080x2400 at density 480 is 360x800 in device-independent pixels, which is far
under the threshold. A fresh install has no saved preference, so
`prefs::sidebarVisible(!isCompactScreen())` fell back to the default and the
default said "not compact" — meaning `availableGeometry()` reported PHYSICAL
pixels (short side 1080), not logical ones.

**NOT YET CONFIRMED, and stated that way on purpose:** nothing has printed
`availableGeometry()` and `devicePixelRatio()` from inside the running app.
The reasoning above is inference from a fresh-install default, not a
measurement. Measure before fixing — a `qWarning` in `isCompactScreen()`,
read back with `adb logcat --pid=$(adb shell pidof org.ticktimer.app)`, settles
it in one build. This repo has been wrong before by writing down the reasonable
guess.

**FIX**
Workaround, and it is a good one — **tap ☰**. The rail collapses, the content
pane takes the full width, and the app becomes properly usable. The choice
persists per device in `QSettings`, so it is a one-time tap.

The real fix depends on the measurement. If Qt reports physical pixels, the
threshold cannot stay a raw pixel count — the question `isCompactScreen()` asks
("how much room is there?") has to be asked in units that mean the same thing
on every platform, i.e. divided by `devicePixelRatio()`, or asked of
`QScreen::physicalSize()` in millimetres.

`SyncDialog` needs its own fix regardless: a modal that cannot be dismissed is
a soft-lock, and neither its close affordance nor Back reached the user.

**PREVENT**
**A layout claim about a device is worth nothing until it has run on that
device.** `TICKTIMER_COMPACT=1` renders the compact layout on a desktop and is
what the screenshot tool uses — genuinely useful, and it verified a code path,
not a phone. The forced mode proved `isCompactScreen()`'s CONSEQUENCES were
right while the real phone proved its INPUT was wrong, and a test that supplies
its own input can never catch that.

Same family as the OpenSSL entry: both are things only a real Android run could
find, and both were shipped as documentation before anyone had one.

