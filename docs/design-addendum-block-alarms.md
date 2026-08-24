# Design addendum — block-start alarms

*Session: "notify me when a planned block is starting." Companion to
`design-doc.md` §3 and the pomodoro addendum (whose notifier this reuses);
question bank section W.*

> **Superseded in part by v30.6** — see
> `design-addendum-notifications.md`. `BlockAlarmService` is now
> `AlarmService`, and §C's ids-not-text rule is *inverted* on purpose: an
> alarm is rendered in advance, because on Android there is no process alive
> at fire time to resolve an id. The reasoning below is still the record of
> why the desktop design was right — and reading it first is what makes the
> inversion legible rather than arbitrary.

---

## A. Why a service, again — and why not the widget the owner named

The ask was phrased as "notification for the AgendaWidget," and the
widget is precisely the wrong owner, for reasons this codebase has now
met twice:

- the widget is a **painter**, and up to *ten* are alive at once (day
  view, seven week columns, two in compare) — ten owners means ten
  toasts;
- none may be visible when the alarm should fire — being elsewhere is
  the scenario a notification exists for (the Pomodoro lesson);
- a block's schedule is **data**; the thing that watches data plus the
  clock is app-lifetime state.

Hence `BlockAlarmService`, third sibling of `TrackerService` and
`PomodoroEngine`, owned by `MainWindow`, living in the domain layer with
zero widget includes.

## B. Derive-don't-store, applied to *time*

There is no alarm list. The service derives the single **next** start
from `AppData`, arms one single-shot timer for exactly then, and
re-derives whenever `AppData::changed` fires — move a block and the
alarm re-aims itself, because the timer was never anything *but* a view
of the data. Two mechanical notes that earn their comments:

- **`Qt::PreciseTimer`.** Qt's default coarse timers allow ~5% slack to
  batch wakeups — harmless for a repaint, but 5% of a 40-minute wait is
  a toast two minutes late. Alarms are the rare timer that earns
  precision.
- **The nap is capped at an hour.** `QTimer` intervals are `int`
  milliseconds (a start next month would overflow), and an hourly
  self-check heals clock jumps and suspend/resume with no
  platform-specific wake signals.

## C. Quietness as a design property

Three rules keep the feature from becoming a nag, all testable and all
tested:

1. **The high-water mark.** One remembered instant, `m_announcedUpTo`,
   born at "now": only starts strictly after it are ever announced, and
   it only moves forward. Duplicates impossible; start-up back-spam
   impossible; per-block "was announced" flags unnecessary.
2. **The grace window (2 min).** Waking from a slept laptop, anything
   staler is skipped *in silence* — but still swept behind the mark, so
   it can't resurrect. One poll can find several due blocks; the signal
   carries a vector so fresh ones share one toast. (With today's 30-min
   slot grid, two *fresh* starts in one poll can't actually happen — the
   vector is headroom, and the test says so out loud.)
3. **The own-hands rule, both halves.** Create a block that's already
   underway: its start is behind the mark — nothing. And at toast time
   `MainWindow` skips blocks you are *already tracking* — announcing
   what the user is actively doing is the machine talking over them.

## D. Ids over text, and a fire-time everything

The signal carries event **ids**, not composed strings: the receiver
resolves titles fresh from `AppData` at toast time (edited titles stay
truthful; deleted blocks simply skip). The toast names the block by its
identity ladder — activity name, task title, or ad-hoc title — the
block-labels addendum's rule, reused. The notify preference
(`prefs::blockStartNotify`, default **on**, checkbox in ⚙ Settings — an
agenda-wide behaviour, unlike the Pomodoro's page-local toggles) is read
at fire time, like every preference in this app.

## E. A constructor-injection lesson the tests forced

`TrackerService` patches its `nowProvider` seam *after* construction and
that works — because it first touches the clock later. This service
reads the clock **in its constructor** (the mark is born at "now"), so a
patched-on seam would arrive too late: the mark would already be
wall-clock real, and every test event would sit uselessly in its past.
The rule the tests taught: **a dependency used in the constructor must
come in through the constructor.** Hence the second ctor taking a
`std::function<QDateTime()>`; the default one delegates with the wall
clock.

## G. v19.8 — the other boundary (and a voice for both)

The owner at 12:00, screenshot in hand: the badge still said *Focusing*
on a 10–12 block, the Pomodoro ground on, and Lunch began unannounced.
Three additions close the loop:

- **The exit door.** `canTrackNow` always guarded starts; now
  `TrackerService::enforceWindow()` guards the end — ridden by the same
  1-second tick that repaints the UI (the watcher was already running;
  no new timer), it commits the in-flight interval with its real end
  stamp and stops the moment the tracked block's window passes. A
  deleted block ends its own tracking by the same door — the honest
  reading of deletion. New signal: `trackedBlockEnded(id)`.
- **The Pomodoro pauses with it.** The link's one tracker→engine message
  (everything else steers the other way — an adapter may be bilingual so
  the machines stay monolingual): block ends while driving → `pause()`,
  never reset. Engaged survives lunch; ▶ at 2:00 adopts the afternoon
  block. The rule-8 interlock (paused never adopts) is now load-bearing:
  the pause must not immediately re-grab and stamp Distracted.
- **A "finished" toast** joins "starting now" — at a seam like 12:00 you
  get both voices: *"Study PHY335 finished — tracking stopped and the
  Pomodoro is paused"* and *"Starting now: Lunch."* Same agenda pref
  gates both.

**Real chimes** replaced the beep (owner request — and the probable
reason 12:00 felt silent: `QApplication::beep()` rides Windows' "Default
Beep", which many sound schemes map to *nothing*). Two synthesized WAVs
ride inside the binary as Qt resources; `QSoundEffect` plays them when
Qt Multimedia is present, and the build degrades to the beep when it
isn't — a missing module costs sound, never features. The app ships WAV
(QSoundEffect wants uncompressed; no codec roulette) even though the
owner's preview files are .mp4. ~~Remaining external suspect for missing toasts: Windows Focus
Assist.~~ **Superseded in v19.9**: the owner's build proved BOTH rented
pipelines unreliable (their Qt kit has no Multimedia → silent beep
fallback; Windows declined the balloons without error). The app now
owns both ends — a three-tier chime (QSoundEffect → winmm PlaySound,
always present on Windows → beep) with the chosen tier PRINTED at
configure, and `NotificationToast`, an app-drawn always-on-top card no
notification pipeline can suppress (the mini timer's window recipe plus
ShowWithoutActivating + DeleteOnClose; stacked, fading, click-to-
dismiss). The tray icon remains for presence and click-to-raise. Full
story in TROUBLESHOOTING: "system beep instead of the chime".

## F. Limits, named

- **Toast delivery is the tray's business** — tray-less desktops get the
  beep only (same guard as the Pomodoro's).
- **No lead-time option yet** ("warn me 5 minutes before"): the service
  shape supports it trivially (announce `start - lead` instead of
  `start`), but it's a second preference and a second toast voice —
  parked in the backlog until asked for.
- **In-app surfacing** (e.g. the agenda pulsing the starting block) is a
  separate, view-side feature; the service's signal is already the hook.
