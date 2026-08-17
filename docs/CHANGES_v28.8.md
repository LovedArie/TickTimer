# v28.8.0 — The SIZE dropdown (hours past the hour mark)

*Owner request: "720 min" is unreadable; a dropdown with sane steps that
says "12h". UI-only; format stays v13.*

## What changed

- **The estimate spinbox became a ladder dropdown**: No estimate ·
  15/30/45m · 1h–8h by half hours · 9h–16h by whole hours (26 entries).
  Non-uniform steps are the duration-picker trick — fine where tasks
  live, coarse where they don't. 15m stays despite the 30-step ask
  because "Fits short gaps" is built on 15-minute holes. The **16h cap
  answers "how long should the list be"**: past two workdays an estimate
  shouldn't grow — the task should break into pieces.
- **Off-ladder values survive** (spinbox-era numbers, parsed captures):
  an absent value is inserted at its sorted rung with the same label —
  opening the panel is never an edit (`oddEstimatesSurviveTheDropdown`
  pins seeding as not-dirty).
- **One duration dialect**: new `minutesLabel(minutes)` in Widgets.h is
  THE formatter; `durationLabel` (planner) now delegates to it, and the
  piece chip uses it — a 12-hour piece reads "12h", not "720 min".
  (DayBriefing's `spanLabel` keeps its zero-padded briefing style on
  purpose, with a pointer.)

## Tests

`estimateDropdownSpeaksHours` (labels, cap, rung spacing),
`oddEstimatesSurviveTheDropdown` (sorted insert, seed-is-clean).
**350 tests green across six suites** (measured).
