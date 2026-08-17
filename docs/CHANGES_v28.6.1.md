# v28.6.1 — Docked → overlay (same-day owner feedback)

*The 28.6.0 panel sat in the layout and shrank the main screen; the
owner wanted it IN FRONT. This drop is that correction. UI-only; format
stays v13.*

## What changed

- **Overlay, not layout member:** the panel is a floating child of the
  body — opening it reflows nothing. The slide animates *position*
  (x past the edge → flush), not width.
- **The scrim:** a translucent dim layer (`rgba(43,47,54,0.28)`) covers
  the content behind the drawer — the "lower contrast" that makes the
  panel read as foreground — and **clicking it closes the panel**,
  through the same Save/Discard/Stay guard as ✕ and Esc. Click-away
  never silently discards.
- **Wider:** 360 → 440 px, clamped so ≥220 px of content stays visible.
- **Resize-follow:** the panel event-filters the host and repositions
  drawer + scrim on window resize (instantly — a resize is not an
  entrance).
- **Trap paid:** `WA_StyledBackground` on both widgets — without it a
  plain QWidget ignores stylesheet backgrounds and the overlay renders
  as a see-through ghost.

## Tests

New: `clickingOutsideRunsTheSameGuard` (Stay keeps the panel and the
stray click costs nothing; Discard closes without writing).
**345 tests green across six suites** (measured).

## Files

`TaskDetailPanel.{h,cpp}` (overlay rewrite), `MainWindow.cpp` (out of
bodyLayout), `tests/test_ui.cpp`, `Version.h`, `installer/*.iss`; docs:
addendum §G, README, QA checklist, QB V242–V243, session notes, project
log, iteration plan, `detail_panel_states.puml` re-noted.
