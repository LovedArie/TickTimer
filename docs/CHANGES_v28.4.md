# CHANGES — v28.4.x (sizing intelligence: the multiplier, §J.2)

## v28.4.1 — one test fixture, refused by the domain's own gate

142/143 on first light for the rewire; the one red was the median test,
whose ratios came back as "not enough samples". Cause: the fixture
planted 05:00–21:00 blocks, and `isFree` refuses anything before the
planner's day window starts (06:00) — every `addTaskEvent` politely
returned "", and the UNCHECKED empty ids made `appendSegment` a silent
no-op three asserts upstream of the failure. Fix: legal 09:00–21:00
blocks, and a QVERIFY on every door in all three fixture loops — a
refusal you don't check becomes a mystery later. Test-only; no app code
changed. (The multiplier itself was fine: `personalMultiplier` correctly
returned 1.0 for a history with no valid samples.)

---

# v28.4.0 — the feature drop

Pure queries on the shape v28.3 built. **No format change** — the file
stays v13; nothing here is stored, everything is derived.

## Feature

- **`afford::personalMultiplier()`** — your estimating rate, as
  division, not opinion: the MEDIAN of actual÷estimate over every
  finished task that had an estimate and tracked focus. Median so one
  10× disaster is an outlier to survive; flat 1.0 below 3 samples (an
  anecdote is not a rate); clamped to [0.5, 3.0]. Derived fresh from
  history on every use — it cannot go stale and improves as work
  accumulates.
- **Affordability rewired, estimate-first:** when a task has an
  estimate, outstanding work = **estimate × your rate − tracked**. The
  planned-blocks proxy survives as the fallback for unestimated tasks.
  **Unknown has shrunk**: it now needs BOTH sources absent — an
  estimated task is never Unknown again, even with zero blocks planned.
- **The decomposition dividend:** an unsized parent borrows the SUM of
  its pieces' estimates (archived pieces excluded) — size the pieces
  and the parent is sized. A parent's own estimate outranks the sum.
- **Provenance everywhere:** the Report carries `estimateBased`,
  `minutesEstimated`, and the applied `multiplier`; the Tight sentence
  says "of your estimate (at your usual 1.5x)" when the rate is doing
  real work; nudge facts and briefing lines get the same honesty.
  Sweeps (AffordabilityService, DayBriefing) compute the rate once and
  pass it down.

## Files touched

`include/Affordability.h` · `src/AffordabilityService.cpp` ·
`src/DayBriefing.cpp` · `include/NudgePhrasing.h` ·
`tests/test_domain.cpp` (+5) · `include/Version.h` ·
`installer/ticktimer.iss` · README + docs (both addenda, session notes,
question bank) · **new:** `diagrams/affordability_sources.*`.

## Apply

1. Unzip this drop over the project root, letting it overwrite.
2. Double-click `tools\deploy-windows.bat` — apply check (must say
   **28.4.1**), clean build, all six suites (expect ~325 green), dist\.
3. Inno, as always.

## What you'll see in the app

Give a deadlined task an estimate and the TIGHT/comfortable math starts
using it immediately — no blocks required. As you finish estimated
tasks with tracked time, the personal rate quietly sharpens every
verdict. Nothing to configure; nothing stored.
