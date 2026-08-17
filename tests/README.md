# tests/

Six QTest suites, all headless, all wired into `ctest`. This file answers the
question `docs/TESTING.md` sends you here for: **what lives in which suite, and
why** — plus where a new test belongs.

*(Framework question, long settled: QTest, because Qt ships it. It needs no
`FetchContent`, it understands `QSignalSpy`, event loops and widgets, and
`qt_add_executable` + `add_test` is the whole integration.)*

---

## The six suites

| ctest name | binary | links | what belongs here |
|---|---|---|---|
| `domain` | `test_domain` | Core, Gui, Test — **no Widgets** | the rules and the maths: `AppData` integrity, overlap, stats, persistence round-trip, crash recovery, and every pure brain that reads domain types (`coverage::`, `missed::`, `reschedule::`, `affordability::`, `checkin::`, `brief::`, `intake::` triage, `verbs::`) |
| `taskmodel` | `test_taskmodel` | Core, Gui, Test — **no Widgets** | the model/view **data** side, seen as a `QListView` sees it: `rowCount`, roles off an index, proxy filter + sort. The delegate is paint code and belongs to `ui` |
| `nlp` | `test_nlp` | Core, Test only — the leanest target | the layers that are pure *and* domain-free: `nlp::parseQuickAdd`, `ai::` providers/dialects/routing, `chat::` transcripts and budgets, `intake::llm` extraction. Forged reply bytes, breaker clocks by hand, no socket |
| `ui` | `test_ui` | Widgets, Network, Test | bugs only a living widget tree can express: widget lifetime, signal re-entrancy, event-loop ordering. Born from a real crash (delete-during-signal), kept for that class of bug |
| `auth` | `test_auth` | `ticktimer_auth`, Gui, Test | the security-critical layer — password hashing, `AccountStore`, `PlannerStore`, `ShareStore`, the `sync::decide` truth table. A bug here doesn't crash, it silently lets the wrong person in |
| `login_live` | `test_login_live` | Widgets, Network, Test | end-to-end: spawns the **real** `ticktimer-server` on port 8091 and drives the real clients over a real socket — a two-'device' sync and sharing playbook |

### Where does a new test go?

Work down this list and stop at the first yes:

1. Does it need a **running server**? → `login_live`.
2. Does it need a **live widget tree** (a real page, a dialog, focus, a signal
   arriving mid-teardown)? → `ui`.
3. Is it about **credentials, permissions, or the server's stores**? → `auth`.
4. Is it about a **`QAbstractItemModel`** — rows, roles, filtering, sorting?
   → `taskmodel`.
5. Is the code **pure and domain-free** (values in, values out, no `AppData`)?
   → `nlp`.
6. Otherwise → `domain`.

Rule 5 vs 6 is the one that gets argued, and `Intake` is the worked example:
one header, two translation units split by dependency group. The interview's
brain reads `AppData` and is tested in `domain`; the extraction takes an area
*name* rather than the data — deliberately, so it can live in `nlp` and be
asserted in microseconds with no socket and no domain.

---

## Running them

```sh
ctest --test-dir build --output-on-failure          # all six
ctest --test-dir build -R domain --output-on-failure # one suite
./build/test_domain someTestFunction                 # one QTest slot
./build/test_domain -functions                       # list this suite's slots
```

On Windows use the runner instead — a bare prompt has no Qt on `PATH` and dies
with "Qt6Gui.dll was not found". It rebuilds changed files first (testing
yesterday's binaries has burned this project before), targets `build-release/`,
and writes `test-results.txt` in the repo root:

```
tools\run-tests.bat            :: all six
tools\run-tests.bat ui         :: passes its argument to ctest -R
```

Every suite that links Qt6::Gui or Widgets runs with
`QT_QPA_PLATFORM=offscreen`, set per-test in `CMakeLists.txt`. Without it the
default platform plugin aborts on a machine with no display.

## Counting them honestly

**There are three defensible numbers, and mixing them is how this repo's
counts drifted.** All three are correct; each answers a different question.

| figure | at v29.1.0 | what it counts | how to get it |
|---|---|---|---|
| **379** | the headline | QTest cases — test functions **plus** each class's `initTestCase`/`cleanupTestCase` | sum the suites' own `Totals:` lines |
| **367** | test functions | the slots you actually wrote | `-functions`, which omits init/cleanup |
| **6** | suites | one per `add_test()` | `ctest`'s summary line |

379 − 367 = 12 = two fixture slots × six suites. Per suite, QTest totals:
domain 158, ui 95, nlp 70, taskmodel 22, auth 19, login_live 15 — against
156 / 93 / 68 / 20 / 17 / 13 test functions.

```sh
# test functions per suite
for t in domain taskmodel nlp ui auth login_live; do
    printf '%s: %s\n' "$t" "$(./build/test_$t -functions | wc -l)"
done
```

```powershell
# QTest cases per suite — the 379 figure (PowerShell; see the gotcha below)
foreach ($t in @("domain","taskmodel","nlp","ui","auth","login_live")) {
    & ".\build-release\test_$t.exe" | Where-Object { $_ -like "Totals:*" }
}
```

Quote a number only with the convention beside it. The figure that went stale
here was never the total — it was the per-suite breakdown, which sat at a
v28.3.2 snapshot summing to 326 while the headline stayed right.

### Gotcha: the suites print nothing through Git Bash

On Windows, running a test binary from Git Bash (MSYS) yields **zero bytes**
on stdout and exit code 0 — the run genuinely happened, but its output is
lost to console attachment. `-functions` prints fine, which makes the failure
look selective and easy to misread as "the suite produced no output."

Use **PowerShell** or `cmd` to read the `Totals:` lines, and `ctest` for
pass/fail — ctest judges by exit code, so its verdict is trustworthy from any
shell. This is why `tools/run-tests.bat` is a `.bat`.

## The layering guarantee

`test_domain`, `test_taskmodel` and `test_nlp` link **without Qt Widgets**.
That is the architecture test nobody had to write: the day a domain file
includes a widget header, those three targets stop building and the violation
is caught at compile time. If one of them fails to link, the fix is in the
include graph — never `target_link_libraries(... Qt6::Widgets)`.

## Conventions that keep them deterministic

- **Fixed timestamps, never `QDateTime::currentDateTime()`.** Pure functions
  take `now` as a parameter; services take a `setNowProvider` seam. `test_nlp`
  anchors every case to `kToday` = Wednesday 2026-07-15, so weekday math and
  month-bumps are exercisable and the suite means the same thing next Tuesday.
- **`QTemporaryDir` for anything that touches disk** — the domain, auth and
  live suites all write into one and let it clean itself up.
- **A test-only `QSettings` scope.** `test_nlp::initTestCase` sets the
  organization and application names to `TickTimerTest` *before* touching
  `QSettings`, so clearing the `ai` group can never reach a developer's real
  provider keys. Any new suite that writes settings must do the same first.
- **`TICKTIMER_AI_DOWN`** forces a seat (or `*` for all of them) unreachable
  before a socket opens — how the fallback paths are proven with no network.
- **Offscreen focus needs an active window**: `setFocus()` silently no-ops
  until `activateWindow()` + `qWaitForWindowActive`.

## Related

`docs/TESTING.md` — the manual counterpart: force recipes for the debug panel
(Ctrl+Shift+D), whose rule is that anything a recipe presses is also pinned by
a test here. `docs/TROUBLESHOOTING.md` — symptom-indexed, and where several of
these tests' origin stories are written down.
