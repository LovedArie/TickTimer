# v28.5.0 — The piece's own panel

*The pieces polish headline, closed. UI-only: no domain change, no format
change (stays v13), no migration.*

## What changed

- **A piece's title is now a door.** In the parent's checklist, the
  checkbox ticks the piece done; clicking the **title** opens the piece's
  own full detail panel — date, time, size, notes. A newborn line (typed
  this sitting, not yet saved) shows a plain label instead: no door until
  it exists.
- **The breadcrumb.** A piece's panel opens with "‹ parent title" above
  the form (window retitled "Piece details"); clicking it saves the piece
  and opens the parent. Because the panel is still a modal dialog, the hop
  is close-and-reopen — recorded as deliberate debt for the side-panel
  slice (addendum §L.4).
- **The scheduled-work chip.** Checklist rows show a quiet
  "Aug 8 · 45 min" when a piece carries a date or an estimate —
  display-only, edited in the piece's own panel.
- **`runTaskDetail`** — new free function beside `seedTaskDetailPieces` /
  `applyTaskDetailAnswers`: the whole seed → exec → apply session plus the
  navigation loop (a loop, not recursion). All four call sites
  (TaskRow, ActivitiesPage, UpcomingPage, PlannerPage) shrank to one line
  and dropped their defensive `Task` snapshots — each hop re-reads fresh
  by id.
- **Navigation is part of the ANSWER.** The dialog records
  `navigationTarget()` and performs nothing; clicking through *accepts*
  (edits survive the hop), only Cancel discards. The pure-question
  contract, extended.

## What this unlocks

A heavy piece — "Chapter 3" under "Study for finals" — can now be given a
date and estimate from the UI. It then flows into `tasksDueOn` and
`tasksNeedingBlock` (which never filtered pieces) and can earn its own
planned block. Zero new domain rules; the surfaces were ready.

## Docs fixed in passing

The README's pieces section still carried the pre-re-land **"designed,
never landed"** banner, contradicting the status line above it (v28.3
shipped the re-land). Banner removed; section retitled and brought
current through v28.5.

## Tests

Four new UI tests (test_ui): `openingAPieceIsRecordedNotPerformed`,
`navigatingAwayStillSavesTheSitting`, `newbornPiecesHaveNoDoorUntilSaved`,
`theBreadcrumbGoesUpAndSaves`. 341 tests green across six suites (measured, not estimated).

## Files

Code: `include/TaskDetailDialog.h`, `src/TaskDetailDialog.cpp`,
`src/TaskRow.cpp`, `src/ActivitiesPage.cpp`, `src/UpcomingPage.cpp`,
`src/PlannerPage.cpp`, `include/Version.h`, `installer/*.iss`,
`tests/test_ui.cpp`.
Docs: subtasks addendum §J strike + §L, README, QA checklist (v28.5
block + version refs), question bank V232–V236, session notes, project
log, `diagrams/piece_detail_sequence.puml`.
