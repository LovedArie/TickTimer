# v28.6.0 — The docked detail panel (explicit save)

*The §L.4 debt paid: task details moved from a blocking modal popup to a
TickTick-style docked side panel. UI-only; format stays v13.*

## What changed

- **`TaskDetailForm` extracted** from the dialog: every field and answer
  getter, plus dirty tracking (`isDirty` = answers vs. a `markClean`
  baseline — a comparison, not a was-touched flag). The form emits
  `navigateRequested(id)`; what that MEANS is the container's policy.
- **`TaskDetailPanel`** (new): docks as [nav][pages][panel] in the main
  window, slides 0↔360 px (180 ms, OutCubic). Navigation is a guarded
  swap-in-place. Rebuild-not-reset per task; outgoing forms
  `deleteLater`'d and the swap queued (self-delete safety).
- **Explicit save, owner's decision:** the Save button lights when
  dirty, a "Saved ✓" flashes after applying, and every exit over unsaved
  work — switch, piece click, breadcrumb, ✕, Esc — asks
  **Save / Discard / Stay** (Save is the default: Enter never discards).
  No silent discard path exists.
- **Saving reseeds**, so newborn pieces gain ids — and doors — the same
  sitting.
- **`changed()` handling:** shown task gone → panel closes; changed +
  clean form → reseed; changed + dirty form → user's edits win
  (last-write-wins, the app's sync answer everywhere).
- **`TaskDetailDialog` kept as the modal fallback**, now a thin wrapper
  (title, buttons, record-and-accept navigation). `runTaskDetail`
  prefers the window's panel via `findChild`, falls back to the v28.5
  modal loop — **zero call sites changed, again.**

## Tests

Three new (test_ui): `thePanelSavesExplicitlyWithFeedback`,
`switchingTasksWhileDirtyAsksFirst`, `pieceClickSwapsThePanelInPlace` —
the prompt driven through the injected test seam. The entire pre-existing
suite passed **unchanged through the wrapper**: the refactor's proof.
**344 tests green across six suites** (measured per-suite Totals).

## Files

New: `include/TaskDetailForm.h`, `src/TaskDetailForm.cpp`,
`include/TaskDetailPanel.h`, `src/TaskDetailPanel.cpp`.
Changed: `TaskDetailDialog.{h,cpp}` (wrapper), `MainWindow.cpp` (dock),
`CMakeLists.txt`, `tests/test_ui.cpp`, `Version.h`, `installer/*.iss`.
Docs: new `design-addendum-detail-panel.md`; subtasks §L.4 debt-paid
note; README; QA checklist v28.6 block; QB V237–V241; session notes;
project log; iteration plan; `diagrams/detail_panel_states.*`.
