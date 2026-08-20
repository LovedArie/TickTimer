# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

**TickTimer** — a C++17 / Qt 6 Widgets desktop app (plan-vs-actual time tracker)
plus `ticktimer-server`, a small self-hosted HTTP/JSON login + sync backend built
on `QTcpServer`. One CMake project builds both, plus six QTest suites. No
dependencies beyond Qt (Widgets, Network, Test; Multimedia optional).

## Build & test

```sh
cmake -B build -G Ninja      # or omit -G for the default generator
cmake --build build
ctest --test-dir build --output-on-failure
```

On Windows the two `tools\` scripts are the intended path, because a bare
prompt has no Qt on `PATH` (a hand-run dies with "Qt6Gui.dll was not found").
They both operate on the **`build-release/`** tree, not `build/`:

```
tools\run-tests.bat            :: rebuilds changed files, then runs all six suites
tools\run-tests.bat ui         :: ctest -R filter — one suite while debugging
tools\deploy-windows.bat       :: apply check + Release build + tests + windeployqt -> dist\TickTimer\
```

`run-tests.bat` writes `test-results.txt` in the repo root. `build/` is the Qt
Creator kit tree (`build/Desktop_Qt_6_11_0_MinGW_64_bit-Debug`); `build-release/`
is what the scripts configure and what already has built exes.

**Run one suite or one test function:**

```sh
ctest --test-dir build-release -R domain --output-on-failure   # one suite
./build-release/test_domain.exe                                # QTest binary directly
./build-release/test_domain.exe someTestFunction               # one QTest slot
```

The six ctest names are `domain`, `taskmodel`, `nlp`, `ui`, `auth`, `login_live`.
All are headless — the CMake `set_tests_properties(... QT_QPA_PLATFORM=offscreen)`
lines matter; a Gui-linked suite aborts without them on a display-less machine.
`login_live` spawns the real `ticktimer-server` binary over a real socket.

**Optional dev tool:** `cmake -B build -DBUILD_TOOLS=ON` adds `screenshot-tool`
(reuses the app's own sources; `TICKTIMER_PROBE=1` makes it print layout
minimums and the settings path).

**Running the app** needs two programs, server first, then client — the app
shows a login screen and cannot get past it without a reachable server.

## Architecture

Layers are **not** visible in the folder tree (everything is flat: headers in
`include/`, implementations in `src/`). They live in the CMake source lists and
the include graph, and `CMakeLists.txt` explains why. Read
`docs/READING_GUIDE.md` for the full file-by-file tour; the establishing
diagram is `diagrams/app_architecture.*`.

**The core spine — one signal drives everything.** `AppData` is the aggregate
root: it privately owns every container, exposes read-only accessors, enforces
every integrity rule in its mutation methods, and ends each mutation with
`emit changed()`. `MainWindow` connects that one signal to the autosave; pages
connect it to their refresh. AppData never knows the UI exists. **Put new rules
in AppData, never in a page** — otherwise the next screen re-implements them and
one day forgets.

**Layer map:**

| Layer | Files | Rule |
|---|---|---|
| domain | `AppData`, value structs (`Task`, `Event`, `Segment`, `Category`, …), `Stats`, `DayBriefing`, `TrackerService`, `PomodoroEngine` | no Qt Widgets, ever |
| pure "brains" | `SyncPlan.h::decide`, `Compare.h`, `version::decideBanner`, `MissedBlocks.h`, `Reschedule.h`, `TaskCoverage.h`, `Affordability.h`, `ChatSession.h`, `LlmProvider.h`, `QuickAddParser` | each feature's one real judgement, extracted as a pure function so a table of microsecond tests can pin it |
| storage | `JsonStore` (atomic `QSaveFile` write-then-replace), `PlannerStore`/`AccountStore`/`ShareStore` server-side | all JSON knowledge quarantined here |
| wire | `AuthClient`, `SyncClient`, `ShareClient`, `UpdateClient`, `ChatClient`, `LlmQuickAddClient`, `NudgeClient`, `IntakeClient` | async `QNetworkAccessManager` → typed `Outcome` signals; POST + timeouts only, no policy |
| policy | `SyncService`, `AffordabilityService`, `CheckInService`, `BlockAlarmService` | decides; owns injection seams (`setNowProvider`, public `sweep()`) |
| glass (UI) | pages, dialogs, custom-painted widgets | reports via signals, decides nothing |

The recurring shape across the networked and AI arcs is **wire / pure brain /
policy / glass**. When adding a networked feature, follow the existing family
rather than inventing a fifth pattern.

**UI update strategy is mixed on purpose.** Most pages rebuild/derive on
`changed()`. The two task lists are Qt model/view (`TaskSnapshotModel` →
`TaskListModel` / `CategoryTaskModel` + custom delegates) with a snapshot+diff
instead of `beginResetModel`. See `docs/design-addendum-model-view.md`.

**The AI layer** is deliberately split so almost all of it is testable offline:
`ai::Provider` (`LlmProvider.h`) is a *value* — base URL + dialect + model + key
— with free functions and two dialects (Anthropic, OpenAI-compatible: also Groq,
Ollama, LM Studio, OpenRouter). `chat::`, `nlp::llm`, and `intake::llm` are pure
(request bytes in, parsed values out); only the `*Client` classes touch a socket.
Keys/models are per-provider in `QSettings` (`ai/key/<id>`, `ai/model/<id>`).

**The write boundary** (`AssistantVerbs.h`, v29.0) is the security-sensitive
file: a closed `Verb` enum scoped per `Role`, where Nudge and CheckIn hold
**empty** verb lists and only Intake may write, additively. Models see per-turn
handles (`T1`), never UUIDs. A diff of this one header is the whole security
review — treat widening `Verb` or a role's list as a deliberate design act, and
keep `apply()` re-validating at the tap. `Role` (trust) is not `ai::Feature`
(routing); do not fold them.

**Debug panel:** `Ctrl+Shift+D` in the running app presses every injection seam
by hand (fake clock, forced sweeps, `TICKTIMER_AI_DOWN=*`, briefing viewer,
proposal injector). Recipes in `docs/TESTING.md`. The panel is glass over seams
that tests already pin — never a second implementation of them.

## Conventions that bite

- **New source files must be added to `CMakeLists.txt`** — to `DOMAIN_SOURCES`
  or `UI_SOURCES`, and **headers are listed alongside .cpp on purpose** (AUTOMOC
  only mocs what the target lists; a missing header is the classic "undefined
  reference to vtable").
- **`test_domain` / `test_taskmodel` / `test_nlp` link without Qt Widgets.**
  That is the architecture test nobody wrote: the day a domain file includes a
  widget header, those targets stop building. Don't "fix" it by linking Widgets.
- **JSON persistence grows additively only** (format v14, `src/JsonStore.cpp`).
  A missing key or an unknown enum string must read as a safe default, so old
  files load with no migration branch. Never repurpose a key.
- **Version lives in `include/Version.h` and must be bumped there *and* in
  `installer/ticktimer.iss`** — Inno Setup can't include a C header, so that
  seam is hand-synced and `deploy-windows.bat` hard-fails on a mismatch (its
  "apply check"). `Version.h` also feeds the Windows resource compiler via
  `RC_INVOKED`, and a `static_assert` pins the string against the three macros.
  Release routine: `docs/GITHUB.md`.
- **Every domain-touching feature enters through a design addendum first**
  (`docs/design-addendum-*.md`, indexed at the end of `docs/design-doc.md` §3),
  in choice → why → alternative-rejected form. Ship the addendum with the code.
- **User preferences live in `QSettings`, domain data in `data.json`** — different
  lifetimes, deliberately never mixed. Preferences never sync.
- Documentation numbers (test counts, versions, diagram counts) have drifted
  repeatedly in this repo's history; measure before writing one down.

## Qt traps this codebase already paid for

`docs/READING_GUIDE.md` §4 has the full list with the file that tells each war
story; `docs/TROUBLESHOOTING.md` is symptom-indexed. The ones that recur:

- Never name an identifier `slots`, `signals`, or `emit`.
- `QVector` reallocates → store **ids**, never pointers into a container.
- A slot must not `delete` a widget that could be (or contain) the sender —
  rebuilds triggered by `changed()` use `deleteLater()`.
- Guard signal feedback loops (widget edit → `changed()` → widget reset) with an
  `m_updatingUi`-style flag; `SyncService::m_applying` is the same idea.
- Theme through **both** stylesheet and `QPalette` (`theme::applyTheme`).
- QSS silently drops a `border-radius` larger than half the widget height.
- `QScrollArea::sizeHint` is a cached guess — neutralise it on both axes.
- Offscreen platform: `setFocus()` no-ops until `activateWindow()` +
  `qWaitForWindowActive`.

## Docs worth opening before changing anything

`docs/READING_GUIDE.md` (reading order + landmarks) · `docs/design-doc.md`
(decisions + the addendum index) · `docs/TESTING.md` (manual force recipes) ·
`docs/TROUBLESHOOTING.md` (symptom-indexed) · `docs/AI.md` (providers, keys,
what leaves the machine) · `docs/SERVER.md` · `docs/SETUP.md` ·
`docs/ANDROID.md` and `docs/WEB.md` (the two phone builds — sideloaded APK,
and the WebAssembly app for iPhones) · `docs/ROLLOUT.md` (the ordered path
from a clean checkout to two phones and a friend).
