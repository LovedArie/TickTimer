# CHANGES — v28.3.x (subtasks & sizing, format v13)

## v28.3.6 — the geometry test asserts only its name

`lived.width(): 1166` settled it: with Qt's no-fonts fallback metrics,
this MainWindow's layout computes a ~1166 minimum width — wider than the
offscreen platform's whole 800x600 screen. The first window's resize is
pushed back up by the layout; the save records 1166; the restore clamps
to the screen (798) before the second window's own layout pass runs.
Three drafts pinned the width three ways (two literals, then a measured
round-trip) and every one lost to a different legitimate owner of that
number — the screen, the layout, and layout timing. None of them is the
code under test. The test now asserts exactly what its name says:
restore ACTED (size differs from the untouched default) and construction
did not overwrite the stored blob. No app code changed; save/restore has
been behaving correctly on every run.

Touched: `tests/test_ui.cpp`, `include/Version.h`,
`installer/ticktimer.iss`, these docs.

---

## v28.3.5 — the last red test: pin the round-trip, not the number

With the fixes actually rebuilt (28.3.4's runner did its job), one
failure remained: the geometry test planted 780 and got 798 back — the
SAME 798 as the 870 plant. The number a window ends up with belongs to
its environment: 870 was clamped DOWN by the offscreen screen, 780 was
clamped UP by the main layout's computed minimum (~798: rail + pages +
margins won't compress further). Save and restore were both doing their
jobs faithfully the whole time; the test was pinning a literal the
layout owns. It now measures what the first window actually became and
asserts the second window restores to exactly that — the save->restore
round-trip, true on any machine, any theme, any future page that widens
the minimum. No app code changed.

Touched: `tests/test_ui.cpp`, `include/Version.h`,
`installer/ticktimer.iss`, these docs.

---

## v28.3.4 — tooling: run-tests.bat now rebuilds before testing

The 28.3.3 test fixes were verified un-run: a results file quoted
`Expected (870)`, a literal the fixed source no longer contains — the
suite had executed stale binaries, because `run-tests.bat` ran ctest
without building. It now does an incremental `cmake --build` first
(seconds when little changed; the CMake cache remembers the compiler by
absolute path), so "unzip a fix, run the tests" tests the fix. No app
code changed in this patch. Lesson for the process file: **a test run
proves things about the binaries it ran, not the sources on disk — a
runner that doesn't build first invites that confusion.**

---

## v28.3.3 — hotfix 3: the last two, both never-green assertions

The org-name fix cleared 13 of 15; the two survivors turned out to be
test bugs on assertions that had NEVER executed before first light:

1. **Address-row visibility:** the Settings dialog opens on page 0 and
   the AI section lives on the Assistant page, behind the stack. A widget
   is visible only if every ancestor is — so the `!isVisible()` check was
   passing VACUOUSLY (everything on a hidden page is invisible) and the
   `isVisible()` check could never pass. The app was right all along
   (the row is properly wired to `currentIndexChanged`). The test now
   navigates to the Assistant page first — found by nav title, not row
   number, so reordering pages can't silently re-vacuum it.
2. **Geometry width:** the test planted an 870-wide window, but the
   offscreen platform's fake screen is 800x600 and `restoreGeometry()`
   clamps to fit — 870 came back 798, and the "width restores exactly"
   claim was accidentally testing Qt's clamping. The plant is now
   780x560, a size that fits any screen the suite runs on.

Also new since 28.3.2's zip: `tools/run-tests.bat` (test without
deploying; Qt DLLs on PATH; writes `test-results.txt`).

Touched: `tests/test_ui.cpp`, `include/Version.h`,
`installer/ticktimer.iss`, these docs.

---

## v28.3.2 — hotfix 2: first light for the test suites

Your 28.3.1 run was the FIRST real execution of these suites anywhere —
they were written in an environment with no Qt, so today Qt finally got a
vote. The verdict: 208 of 226 executed checks green on first light, and
the 18 red ones reduce to three causes, all fixed here:

1. **domain (1):** the mood round-trip test pins the format version and
   still said 12; the v28.3.0 bump to 13 missed it. The pin is now 13 and
   commented as the format-version tripwire it turned out to be — bump it
   in the same drop as JsonStore's literal, every time.
2. **ui (15, one cause):** every failure was the same signature — a
   QSettings value written or planted, read back empty. `test_ui` set the
   application name but NEVER the organization name; a default
   `QSettings()` is scoped by both, and with an empty organization your
   Qt 6.11/Windows setup persists nothing. (The real app sets both;
   `test_nlp` sets both and passed.) Fix: a real `initTestCase` sets both,
   once, process-wide.
3. **login_live (1):** a 3-second ceiling on how fast Windows reports a
   dead port; your machine took 3.6 s. Ceilings are now generous (15 s) —
   QTRY returns the moment the signal lands, so green runs stay fast.

Also: the README's per-suite test counts were hand-carried and had
drifted ~20 low; they are now counted mechanically (≈320 functions).

Touched: `tests/test_ui.cpp`, `tests/test_domain.cpp`,
`tests/test_login_live.cpp`, `README.md`, `include/Version.h`,
`installer/ticktimer.iss`, these docs. **New:** `tools/run-tests.bat` —
runs the suites by hand with Qt's DLLs on PATH (a bare command prompt
dies with "Qt6Gui.dll not found"); optional filter arg, e.g.
`run-tests.bat ui`; writes `test-results.txt` for sharing.

If anything is still red on the next run, send the block — each remaining
failure is now individually meaningful instead of drowned in one cause.

---

## v28.3.1 — hotfix: the build error you sent

One error, repeated everywhere: **`PieceCount` was defined twice** —
`Task.h:309` (written by the earlier half-applied session, in a region of
the file the re-land session never read) and `AppData.h:59` (written by
the re-land). Two definitions of one struct violates C++'s One Definition
Rule; the compiler refused every file that saw both, which is all of them.

Fix: **one `PieceCount` survives, in `AppData.h`**, beside the query that
fills it — enriched with the deleted copy's two good helpers (`any()`,
`complete()`). `Task.h` keeps the subtask-vs-piece naming note and loses
two type definitions: the duplicate, and `SubtaskEdit` — a handoff type
for a bulk-`setSubtasks` design that was never built (the shipped design
is the seed/apply pair; a type pointing at a nonexistent door is a lie on
disk). Zero users of either, verified.

Correction, owed honestly: the note below claimed your pre-drop tree
"did not compile" because `AppData.h` referenced an undeclared
`PieceCount`. **Wrong** — Task.h carried the definition all along, in the
unread region. Your pre-drop tree was fine; the redefinition arrived WITH
v28.3.0, and 28.3.1 removes it.

Touched by the hotfix: `include/Task.h`, `include/AppData.h`,
`include/Version.h`, `installer/ticktimer.iss`, these docs.

---

# v28.3.0 — the feature drop (as originally shipped)

The v27 re-land, written fresh against the v28.2.1 tree. **Unzip the drop
over the project root, letting it overwrite.** Nothing was deleted; two
files are new (`docs/design-addendum-subtasks.md`, this file) plus the
diagram pair.

## Feature

- **Pieces**: `addSubtask` (one level, door-enforced; category inherited
  at birth), `subtasksOf` (insertion order, archived excluded),
  `pieceProgress` ("☑ 2/5"), the five query policies (see the new
  addendum §D), archive cascade both directions, delete cascade with
  title-demotion, repeat-spawn keeps `parentId`.
- **Sizing**: `Task.estimateMinutes` (0 = unset) + `chunkable`, one
  `setTaskSize` door, spawn carries both.
- **Detail dialog**: PIECES checklist (add-row, tick, ✕ = archive) +
  SIZE row. Ctor **grew** (`estimateMinutes, chunkable` before `parent`)
  — every call site was touched on purpose. New free-function pair
  `seedTaskDetailPieces` / `applyTaskDetailAnswers`; the apply runs in
  one `AppData::Batch`.
- **`AppData::Batch`** implemented: all 47 emit sites now route through
  `notifyChanged()`; batched mutations repaint once.
- **Upcoming card**: "☑ 2/5" folded into the subtitle; new model roles
  `PiecesDoneRole`/`PiecesTotalRole` on a sidecar cache diffed like the
  verdict cache.
- **Storage**: format **v13** — `parentId`, `estimateMinutes` (clamped
  ≥ 0 on read), `chunkable`; missing keys = old meaning, no migration
  branch. Orphan adoption in `resetFrom`.

## Files touched

`include/Task.h`* · `include/AppData.h` · `src/AppData.cpp` ·
`src/JsonStore.cpp` · `include/TaskDetailDialog.h` ·
`src/TaskDetailDialog.cpp` · `src/UpcomingPage.cpp` ·
`src/ActivitiesPage.cpp` · `src/TaskRow.cpp` · `src/PlannerPage.cpp` ·
`include/TaskListModel.h` · `src/TaskListModel.cpp` ·
`src/TaskCardDelegate.cpp` · `tests/test_domain.cpp` (+13) ·
`tests/test_taskmodel.cpp` (+2) · `include/Version.h` ·
`installer/ticktimer.iss` · `tools/deploy-windows.bat` · README + docs (addendum-assistant corrections,
iteration plan, SETUP, session notes, question bank) ·
**new:** `docs/design-addendum-subtasks.md`,
`diagrams/subtask_policies.puml` (+ rendered).

\* `Task.h` and parts of `AppData.h` were already in your tree — a
previous session's half-applied attempt (see SESSION_NOTES). They were
verified against §I/§J and kept. (The v28.3.0 note here claimed your tree
didn't compile before the drop; see the 28.3.1 section above — that claim
was wrong.)

## Apply-check ritual — now built into your deploy script

`tools/deploy-windows.bat` runs the whole pipeline itself since this drop:
**step 0** compares `include/Version.h` against `installer/ticktimer.iss`
and refuses to build if they disagree (the signature of a half-applied
drop), printing the version loudly so you can eyeball it against this
zip's filename; after building it **runs all six test suites** and
hard-stops on red — a red suite must not become an installer. So the
ritual is now:

1. Unzip this drop over the project root, letting it overwrite.
2. Double-click `tools\deploy-windows.bat` — it checks, builds, tests.
3. Inno Setup compiler, as always.

Checking by hand still works and is still five seconds:

```
grep VERSION_STRING include/Version.h     # must print 28.3.6
ctest --test-dir build-release            # after any build
```

Expected: **297 test functions** across the six suites, all green. If the
build fails, send the first error — see SETUP §3's note.
