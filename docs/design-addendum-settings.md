# Design addendum — Settings, identity labels & the visible window

*Session: welcome header · pinned compare identities · the Settings dialog
(agenda hours, week start). Companion to `design-doc.md` §3; question bank
section U.*

> **Superseded in part (v26.1).** The *layout* of the dialog described here —
> one flat `QFormLayout` with bold pseudo-headings — was replaced by a nav
> list plus a page stack. See `design-addendum-settings-nav.md`. Everything
> below about *which* preferences exist, their defaults, and why they live in
> `QSettings` rather than `data.json` is unchanged and still current.

---

## A. Preference vs. domain rule — the line this session drew

The ask "let me change the agenda hours" has two readings, and they lead to
different programs:

1. **Change the planning grid** — make `plan::kDayStartMinutes` /
   `kDayEndMinutes` (in `Event.h`) configurable. That changes what times are
   *legal to plan*, which changes `AppData`'s validation, the meaning of
   stored events, tests, and — worst — what an old `data.json` means under a
   new setting.
2. **Change what the agenda *shows*** — a display window over an unchanged
   grid.

We took (2), and the reason is a sentence from `Event.h` itself: *"you plan
in 30-minute slots between 6 AM and midnight" would be true even if the app
were a command-line tool.* The grid is a domain rule; how much of it your
screen paints is taste. Taste lives in `QSettings` (`Prefs.h`), facts live
in `data.json` — the same wall the Pomodoro durations built in §3.31.
Consequence worth saying out loud: **preferences never sync and never
bump the data format** — v6 stays v6, and two devices may disagree about
hours without either being wrong.

## B. "Data always wins" — the window's one honesty rule

A narrowed window could hide a planned block, and a hidden block is worse
than an ugly one: it still occupies its slots (`isFree` refuses new plans
over it) while being invisible — a haunted agenda no user can debug.

So the shown range is **derived**: `AgendaWidget::windowCovering(data,
date, prefStart, prefEnd)` = the preference, *stretched (never shrunk) to
cover every event on that date*. Pure, static, public — because the rule
has multiple consumers (below). The Settings dialog states the rule where
the choice is made ("blocks outside these hours still show"), the same
write-it-on-the-door habit as the sharing dialog's whole-planner warning.

Derive-don't-store (§3.5) applies at widget scale: the shown window is
recomputed on demand, never cached — an event added at 6 AM must change
the answer *now*, and a cached range going stale is exactly the bug.

## C. The union rule — why multi-column screens compute the window

Two screens tile several `AgendaWidget`s that must stay pixel-aligned:

- **WeekAgendaView** — seven day columns + one shared hour axis.
- **CompareDialog** — your live agenda beside the peer's snapshot.

Each column's own `windowCovering` can differ (Tuesday has a 6 AM block,
Wednesday doesn't) — and misaligned columns destroy the one thing these
screens exist for ("are we both free at 7?"). So **the container that can
see all columns computes the union** (min of starts, max of ends) and
tells every column — and the axis — the same window:

- the week view folds `windowCovering` over its seven dates
  (`applyWindow`), re-running it on `setDate`, on either preference
  setter, and on `AppData::changed` (a block can land outside the window);
- the compare dialog folds it over its two *datasets* on every `refresh`.

Same formula both times — that is why `windowCovering` is a shared static
rather than private widget logic.

## D. Geometry stayed honest because it had one door

The refactor cost of the whole feature was small for one old reason: every
slot↔pixel conversion already went through a single `slotTop`. It moved
from a file-local free function into the class (it now depends on widget
state) and gained one subtraction — `(slot - firstShownSlot())` — and
painting, hover, hit-testing, and drag-resize all followed automatically.
Crucially, **slot indices in signals stay domain indices** (0 = 6 AM,
whatever the window): `emptySlotClicked(slot)` means what it always meant,
so `PickActivityDialog`, both pages, and every existing test needed zero
changes. The window moves the viewport, never the meaning of an index.

Two deliberate limits, named:
- **Drag-resize reach.** You can only drag an edge to the bottom/top of the
  *shown* range (the mouse can't leave the widget meaningfully). Growing a
  block past the window is done through EventDialog's time fields — and
  the moment it commits, the window stretches to show it.
- **Widget self-observation.** `AgendaWidget` now connects to
  `AppData::changed` for *geometry* (its height depends on the data). The
  "widgets are told" doctrine is about **policy** — settings, dialogs —
  not about observing the data a widget already paints; `WeekAgendaView`
  set that precedent. Pages still own every `QSettings` read.

## E. Week start — one formula, four screens

"Week starts on Sunday" touches more than the week agenda; a half-applied
preference would *lie*:

| Screen | Without the change |
|---|---|
| Week agenda columns | Sun–Sat, fine |
| Week review totals under it | would still sum **Mon–Sun** — different seven days than the grid above them |
| "Week of Jul 6" label | would name a Monday the grid doesn't start on |
| Month grid columns | 1st placed under the wrong weekday header |

So the snap became **one named function** — `stats::weekStart(anyDay,
firstDay)` — and every consumer calls it. It lives in `Stats.h`, *not*
`Prefs.h`, for a layering reason: `summarizeWeek` gained a
`Qt::DayOfWeek firstDay = Qt::Monday` **parameter** rather than reading
the preference itself, because a pure summarizer that reads `QSettings`
stops being a function of its arguments (the `now`-parameter lesson,
§3.38, third appearance). Defaulted to Monday, so every existing caller
and test compiles to the historical behaviour.

## F. The Settings dialog — scope discipline

`SettingsDialog` (⚙ in the nav rail, below the stretch with the other
furniture) holds *only* what has no natural home: agenda hours, week
start. Pomodoro durations stay on the Pomodoro page — a preference lives
closest to where you think about it, and a dialog that hoards every knob
becomes the junk drawer. The old backlog note "the Archive page moves into
a settings area" was **deliberately not taken**: Archive is a place you go
(a destination with content), Settings is a decision you make (a dialog) —
merging them would conflate navigation with configuration.

Mechanics worth stealing:
- **Combos, not spin boxes**: 18 legal start hours is a *finite* choice
  set; a combo cannot express 6:07 AM, deleting a validation class.
- **Rebuild, don't validate**: changing the start rebuilds the end combo
  to `start+1 … midnight` — "end before start" is unpickable rather than
  checked on OK (keeping the user's end pick when it survives).
- **Write once, on OK** (Pomodoro's persist-on-use rule): Cancel writes
  nothing, and the dialog never touches `AppData` — closing Settings can't
  dirty the planner or trigger a sync.
- **Pull, not push**: after `accept()`, `MainWindow` calls
  `PlannerPage::applyDisplayPrefs()`, the one choke point that re-reads
  `prefs::` and *tells* every widget (day agenda, week view, week review,
  month grid, the "Week of…" label). The dialog stays ignorant of who
  listens; a future pref-consumer touches the page, never the dialog.

## G. Identity, said where the eyes are

Two small features, one principle — *name whose data this is, exactly
where confusion would strike*:

- **`Welcome, <username>`** in the main header (right side), only when a
  username exists. Per-account storage made "whose planner am I in?" a
  real question; the header answers it permanently. An empty username
  (tests, tools, the legacy no-login path) shows nothing — greeting a
  nameless session would just advertise plumbing.
- **Compare headers, pinned.** v2 put "You"/peer *inside* the scrolled
  host — correct at 6 AM, gone by 9 PM, which is precisely when two
  look-alike columns need names most. The headers moved **outside the
  QScrollArea** (a table's header row, in spirit), now carry real account
  names (`alice (you)` vs `mom`), and mirror the columns' layout (same
  1:1 stretch, same spacing, right margin = scrollbar width) so each name
  sits over its agenda at any scroll position. The UI test asserts the
  *structure* — no `QScrollArea` in the labels' ancestry — because a
  text-exists check would pass on the old bug.

Threading: the dialog can't know your name by itself —
`MainWindow (m_username) → SharingDialog (courier) → CompareDialog`.
One parameter each; the sharing dialog never displays it.

## H. What was tested, and why these tests

- **Domain (1):** `weekStart` at its edges (the first day maps to itself;
  the day before belongs to last week) *and* proof the parameter moves
  real totals — Sunday's focus is inside "the week of Wednesday" only
  under Sunday-first. A parameter that changed no numbers would be
  decoration.
- **UI (4):** the pinned-header *structure* (ancestry, not text); the
  window's three behaviours in one test (default = historical 36 slots,
  narrowing shrinks, an out-of-window block stretches back — with no call
  on the widget, pinning the self-observation); the week view re-snapping
  columns when told Sunday (fixed dates, never "today"); and the Settings
  dialog's persist-on-OK (selecting ≠ saving; the *real* OK button is
  clicked, because driving `accept()` would pass even if the button were
  never wired).
- **Defaults are pinned everywhere** — absent keys must mean the app's
  historical behaviour, so nobody who never opens Settings can be
  affected by this session.

## I. Limits, named out loud

- **Hours are per-machine.** `QSettings` doesn't sync — by design (taste,
  not fact). Two devices may show different hours.
- **Drag-resize stops at the window edge** (§D); EventDialog is the
  escape hatch and the window stretches after commit.
- **Week start is Monday-or-Sunday**, not any-day: those are the two real
  conventions; a 7-way combo would be choice noise. Trivial to widen if a
  Saturday-first culture request arrives.
- **The compare dialog reads `prefs::` itself.** It's session glass, not
  a reusable widget — same standing as PlannerPage — but it is the first
  *dialog* to do so; if a second one appears, consider a carried
  parameter instead.
