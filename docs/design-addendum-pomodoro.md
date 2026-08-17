# Design addendum — the Pomodoro grows up

*Session: phase-end notifications · the tracker link ("you pick the block,
the Pomodoro picks the kind") · the pin-on-top mini timer. Companion to
`design-doc.md` §3; question bank section V.*

---

## A. Three asks, one refactor

The owner asked for three things — a notification when a phase ends, a
link between the Pomodoro and the tracked block, and a small always-on-top
timer. None of them is buildable while the Pomodoro's state lives as
private members of `PomodoroPage`:

- a **notification** must fire while the page is hidden — *being hidden is
  the scenario a notification exists for*;
- the **mini window** must show the *same* countdown as the page — two
  timers that could disagree would be a lie;
- the **link** needs something emitting signals that isn't a widget.

So the state machine moved out into **`PomodoroEngine`** (a `QObject`, no
widget includes, `DOMAIN_SOURCES`) and the page became a *view*. This is
the `TrackerService` lesson, second verse: *state whose lifetime is the
app's belongs in a service owned by `MainWindow`; windows are faces of
it.* Note what the extraction did **not** change — the ring, the dots, the
buttons, the transition table. Views survive extractions precisely because
they never owned the truth, only painted it.

The old page header called linking the two machines "future work" and
warned that doing it early "would tangle two clean machines." The warning
shaped the design: the machines were **not** merged (§C).

## B. The engine's shape — three signals, three grains

`PomodoroEngine` deliberately emits at three granularities:

| Signal | Fires | Consumer |
|---|---|---|
| `changed()` | every visible movement (each tick) | the two views |
| `modeChanged()` | the (running, phase, engaged) tuple flips | the link |
| `phaseEnded(finished, next)` | a countdown reaches zero | the notifier |

The middle one exists for a concrete failure it prevents: if the link
listened to `changed()`, it would re-assert the tracker's kind **once per
second**, and `TrackerService::start*` commits the current interval before
opening a new one — sixty one-second segments per minute, forever. The
grain of a signal is part of its meaning.

Two more deliberate shapes:

- **`tickOneSecond()` is public.** The internal `QTimer` calls it in
  production; tests call it in a loop. Same seam-for-determinism idea as
  `TrackerService::nowProvider`, but simpler — time needs no faking when
  the caller *is* the clock.
- **`engaged` is honest state, not a heuristic.** Paused keeps
  `engaged == true` ("pulled away, coming back" → the link reads
  *distracted*); reset clears it ("walked away" → the link takes its
  hands off). Without the flag you'd have to *infer* abandonment from
  `remaining == total && round == 1 && …` — a guess that breaks the day
  someone pauses on the first second.
- **`skip()` advances without `phaseEnded`.** No toast for something you
  just did with your own hands; notifications are for the countdown
  acting *on its own*.

## C. The link — an adapter, because the machines stay whole

`PomodoroLink` is the entire coupling, in one deletable file. The engine
still knows nothing about blocks; the tracker still knows nothing about
countdowns. Compare the two machines it joins (the old page comment,
completed): the tracker is driven by **user commands** and writes finished
facts into the domain; the engine is driven by a **countdown** and writes
nothing. An adapter can join those; a merge would have produced one
machine with two masters.

**The rule: you pick the block, the Pomodoro picks the kind.**

| Engine says | Tracker does |
|---|---|
| running, Focus | `startFocus(tracked block)` |
| running, any break | `startBreak(…)` |
| paused (engaged) | `startDistracted(…)` — the owner's spec: a pause mid-cycle is "I got pulled away" |
| reset (not engaged) | *nothing* — abandoning the Pomodoro is not a statement about the block |
| tracker is Idle | *nothing* — the link **never** chooses a block; that choice stays a human act on the agenda |

**When it listens** matters as much as what it does:

- On `modeChanged()` — the Pomodoro speaks at its own transitions.
- On the tracker's **Idle → tracking** edge only: start tracking a block
  mid-Pomodoro and it *snaps into the cycle's rhythm* (yes, even if you
  pressed "Break" to join — you also ticked "the Pomodoro drives").
  But a manual kind-switch while *already* tracking **stands** until the
  next Pomodoro transition: the human's explicit command outranks the
  machine's standing one. Implementing this took one remembered state
  (`m_lastTrackerState`) — the cost of a courtesy.

Everything passes through `canTrackNow` — the §3.38 honesty door. If the
block's window has passed, the link stops steering rather than forging
actuals. And the checkbox defaults **off**: the link writes real
distracted/break segments into your day, so it must be an informed opt-in.

**v19.7 — adoption on the play edge.** The owner's follow-up: "can I
start the Pomodoro *without* first clicking focus on the block?" Yes —
and it's an evolution of the rule, not a breach. "Never picks a block"
existed so the machine never *guesses*; but pressing ▶ at 10:05 over a
block planned 10:00–12:00 involves no guessing — **you picked that block
when you planned it**, and the domain's no-overlap law makes "the block
under the clock" (`TrackerService::liveEventNow()`) at most one. So: on
the **play edge only** (running flips false→true, or the link is enabled
mid-run), an idle tracker adopts the live block with the phase's kind.
Three refusals keep it civilized, each a test: phase flips are *not*
play edges, so a block you explicitly **Stopped stays stopped** through
them (your command outranks the machine's — pressing ▶ again is a fresh
command and re-adopts); a **paused** Pomodoro never adopts (stamping
DISTRACTED onto a block you never touched would be slander); and with
**no live block**, a play edge waits rather than inventing one. The
status line makes the offer concrete: *"press Start and the Pomodoro
will drive 'Study PHY335' (planned for right now)."*

## D. Notifications — a painted tray icon and a fire-time read

`MainWindow` owns the notifier (not the engine — liftable, UI-free; not
the page — hidden, see §A). Mechanics worth noting:

- Qt routes desktop balloons through `QSystemTrayIcon::showMessage`, and
  most platforms only deliver them for a **visible** tray icon — so
  TickTimer now has a permanent tray presence. Cost: one small icon.
  Bought: a native notification path with **zero new Qt modules** (no
  QtMultimedia, nothing new for `windeployqt` to bundle — the deploy
  script is untouched). The sound is `QApplication::beep()`; a richer
  chime via QtMultimedia is the named upgrade path if ever wanted.
- The icon is **painted, not shipped**: a 64-px disc in the app's focus
  green with white clock hands. No resource file, identical everywhere.
- The notify preference is read **at fire time** (`prefs::pomodoroNotify`,
  default on — a silent Pomodoro is a broken promise). Derive-don't-store
  at preference scale: untick the box mid-phase and the very next toast is
  silenced; there is no cached flag to go stale.
- Clicking the tray raises the window — it earns its keep the moment the
  mini card is floating over a maximized browser.

## E. The mini window — a second face, not a second clock

`PomodoroMiniWindow` owns **no timer state**; it is a second view of the
same engine. The UI test states the claim directly: click the card's play
button → the *engine* runs; tick the engine → the *card's* label moves.
If anyone ever gives the mini its own `QTimer`, that test fails.

The window recipe, each flag earning its place: `Qt::Tool` (floats with
the app, no taskbar entry) + `Qt::FramelessWindowHint` (the card *is* the
window) + `Qt::WindowStaysOnTopHint` (the whole point) +
`WA_TranslucentBackground` (rounded corners that are actually round —
which obliges the card to paint every pixel it shows, same own-every-pixel
rule as the custom widgets). Frameless costs the OS-provided drag, so
`mousePress/Move` reimplement it — the standard press-anchor + delta
dance — and the drop position persists in `QSettings`.

One subtlety that turned out to be a **bug** (fixed in v19.5.1, logged in
TROUBLESHOOTING.md): the card originally took the main window as a
"memory-only" parent. On Windows that parent becomes a Win32 *owner*, and
owned windows hide while their owner is minimized — the card died exactly
when it was needed. It is now parentless; `~PomodoroPage()` deletes it by
hand, and `WA_QuitOnClose(false)` keeps it from holding the app open.
Corrected lesson: Qt lets you split memory-parent from window-parent, but
the OS may not.

The phase label doubles as a quiet **Skip** button ("Focus ›") — the
card's ⋯-style affordances from the owner's screenshot, without a menu.

## F. Settings placement — the junk-drawer rule, applied again

All three new knobs (notify, drive-the-block, mini position) live with the
**Pomodoro page**, not the Settings dialog — they change what the running
timer *does*, so they belong next to it. The Settings dialog keeps only
what has no natural home (settings addendum §F). The keys themselves moved
to `Prefs.h` though: notify and the link have consumers *outside* the page
(MainWindow, the link), and shared keys need a shared home — the page
remains the only *writer*.

## G. What was tested, and why these tests

- **Engine (domain):** the classic cycle end-to-end with 1-minute phases —
  auto-flow into the break (still running, `phaseEnded` exactly once),
  skip advancing *silently*, rounds pairing on break→focus, the 4th focus
  earning the long break (asserted by its *length*, proving which break),
  and the engaged bit's pause-vs-reset distinction.
- **Link (domain):** all seven rules as one narrative — never picks a
  block; snaps a mid-cycle join; paused→distracted→resumed→focus;
  countdown crossing switches the kind by itself; reset = hands off;
  disengaged = inert on join; disabled = inert entirely.
- **Mini + page (UI):** shared-state in both directions (§E), and the page
  telling the engine its durations (the engine never reads QSettings — the
  hand-off *is* the feature).
- Not tested: the tray balloon itself (platform-dependent delivery;
  headless CI has no tray). The engine-side trigger (`phaseEnded`) is
  covered; the last inch is OS territory.

## I. v19.6 — feedback: invisible correctness reads as broken

The owner ran a linked focus phase for seven minutes and asked, fairly,
"when does the block update? I don't see anything." Everything *worked*:
the plan-vs-actual bar was growing every second — by about half a pixel
per minute on a two-hour block. The link followed all seven of its rules
and acknowledged none of them. Two additions fix the silence:

- **The live badge on the tracked block** — `● Focusing · 7:12` at the
  top-right of line 1, in the state's own colour (green/amber/red),
  clock in digital `m:ss` precisely so the ticking seconds *are* the
  feedback (a "7m" prose value that moves once a minute would re-create
  the silence). The title yields width to it, never the reverse. Day
  view only; week columns keep the bar. It answers both halves of the
  doubt at a glance — recording? and *as what*? (a paused Pomodoro
  driving Distracted now says so in red instead of quietly growing a
  red sliver).
- **The status line under the link checkbox** — one live sentence, every
  branch naming its actor: *"Driving 'Study PHY335' — recording focus ·
  7m this interval"*, or the human act it's waiting for (*"start
  tracking a block on the Planner"*), or *"Link off"*. The sentence
  depends on **both** machines, so it listens to both — the test caught
  the half-wired version, where pressing Start while already Focusing
  changed no tracker state, fired no tracker signal, and left a stale
  "press Start" on screen.

The session also flushed a real bug (TROUBLESHOOTING: "a seam with
holes"): four wall-clock reads inside TrackerService bypassed
`nowProvider`, discovered when the pixel check's fake-clock badge
refused to tick — and one old test turned out to be green for the wrong
reason, sleeping real milliseconds to work around the very hole. Seam
repairs are audits, not patches: grep the file, holes come in families.

## H. Limits, named out loud

- **The sound is the system beep.** Portable and dependency-free; some
  Linux desktops mute it. QtMultimedia + a bundled chime is the upgrade,
  at the cost of a new Qt module in every deploy.
- **Balloons need a system tray.** On tray-less desktops the beep still
  fires; the toast doesn't. The guard is explicit
  (`isSystemTrayAvailable`).
- **The link never stops the tracker.** When a Pomodoro cycle outlives
  the block's window, the last interval keeps its kind and steering
  simply stops (honesty door). Stopping is a human act.
- **Mini stays inside the app's lifetime.** Close the main window and the
  app quits, card included (`WA_QuitOnClose(false)` — it's a face, not a
  second app). Minimizing, though, leaves the card floating: that's the
  point (v19.5.1).
