# Design addendum — the window's own memory (v23)

*Roadmap item: "Remember window & sidebar state (QSettings)".*
*Diagram: `diagrams/window_memory_restore.mmd`.*

---

## §A The requirement, and why it is smaller than it looks

> Reopen TickTimer and find the window where you left it, with the rail in
> the state you chose.

Two values. It reads like an afternoon's work, and the *storing* genuinely is.
Everything interesting in this addendum is about the **restoring** — because a
remembered rectangle is the only preference in this app that can be
*syntactically valid and physically impossible* at the same time.

An agenda window of `(540, 1080)` is either legal or it isn't, and `Prefs.h`
repairs it on read. A window rectangle of `(2400, 300, 900, 700)` is perfectly
well-formed and also completely unusable — if the monitor that used to live at
x=1920 has been unplugged. No amount of reading the value tells you that. You
have to ask the world.

That single asymmetry is what the rest of this document is about.

---

## §B Where these values live: QSettings, and not for the usual reason

The project's standing rule (§3.31, and the banner at the top of `Prefs.h`) is
that `data.json` holds **facts about your time** and `QSettings` holds **taste
about how this machine shows them**.

Window geometry is the purest example the codebase has, and it's worth stating
why: it isn't merely that syncing it would be *unnecessary*. Syncing it would
be **actively wrong**. A laptop and a desktop have nothing to say to each other
about window size. If the two ever agreed, one of them would be wrong. This is
a preference that *must* disagree across devices, which makes per-machine
storage the correct answer rather than the convenient one.

Same reasoning as the needs-a-block `lastReview` clock (§E of that addendum),
arrived at from the opposite direction.

---

## §C Two keys

```
window/geometry        QByteArray   opaque, from QWidget::saveGeometry()
window/sidebarVisible  bool         the rail's last chosen state
```

### Why geometry is a blob and not four integers

The tempting shape is `window/x`, `window/y`, `window/width`, `window/height`.
It is readable in a settings file, it is trivially testable, and it is wrong.

`saveGeometry()` returns an opaque `QByteArray` that carries strictly more than
a rectangle:

| carried | what breaks without it |
|---|---|
| screen identity | multi-monitor restore lands on the wrong display |
| maximized / full-screen flag | maximized windows come back *restored* |
| the "normal" rectangle behind a maximized window | un-maximizing gives you a garbage size |
| DPI the layout was made at | mixed-DPI setups restore at the wrong scale |
| a version tag | corrupt data is *detected* instead of interpreted |

The four-integer version fails silently, and only for people who maximize — the
worst possible bug distribution, because the people it affects assume that's
just how the app works and never report it.

The general lesson, which outlives this feature: **when a framework hands you an
opaque serialization of its own state, store the blob.** Reaching in to extract
"just the useful bits" trades an invisible cost now for a class of bug you
cannot even name later. You are not smarter than `saveGeometry()` about what
`restoreGeometry()` needs.

### Why the getter does no validation

Every other accessor in `Prefs.h` repairs on read — clamping, snapping, falling
back. `windowGeometry()` deliberately doesn't, and that is not an inconsistency.

The repair-on-read rule is *"garbage tolerated on disk, never in the program."*
It presumes the getter can **tell** what garbage is. Here it can't: the blob's
validity is a question only Qt's own `restoreGeometry()` can answer, and it
answers it by returning `false`. So the rule is honoured, just delegated —
`MainWindow` treats `false` exactly the way `agendaWindow()` treats an
out-of-range integer.

> A repair function you cannot write correctly is worse than no repair
> function, because it looks like a guarantee.

### Why `sidebarVisible()` takes its own default as a parameter

```cpp
inline bool sidebarVisible(bool fallback);
```

Unusual for this file — every other getter hardcodes its default. But the
sidebar's default isn't a constant; it's a function of the screen (a phone-sized
display starts folded, because 190px of rail on a 400px screen is half the
app). `Prefs.h` has no business knowing that, so the caller supplies it.

This works cleanly because of the QSettings property the v22 pomodoro default
flip already relied on: **a default is consulted only when the key is absent.**
So the call reads as *"whatever you chose on this machine — and on a machine
you've never chosen on, whatever suits its screen."* A compact device gets the
compact default on first run and its own choice ever after. A laptop's
preference can never leak onto a phone, because they were never sharing a
settings store to begin with.

---

## §D The unreachable-window problem

This is the part that isn't boilerplate.

**The scenario.** Close the app maximized on your second monitor. Undock the
laptop. Reopen. The blob is valid, `restoreGeometry()` returns `true`, and the
window is faithfully restored to coordinates that exist on no attached display.
From the user's side, the app didn't start.

It is one of the most-reported bugs in desktop software, and it is always
exactly this.

**Why Qt doesn't save you.** `restoreGeometry()` validates the blob's *format* —
magic number, version, sane field sizes. It has no opinion on your monitor
layout. `true` means "these bytes are a geometry", not "this geometry is
usable". Reading that return value as the latter is the bug.

**The guard**, in `Widgets.h`:

```cpp
inline bool overlapsAnyScreen(const QRect& frame, const QList<QRect>& screens);
inline QList<QRect> availableScreenRects();
```

Three decisions inside those two lines:

1. **The policy is pure.** `overlapsAnyScreen` takes the screen rectangles
   rather than asking `QGuiApplication` for them. The impure half —
   *today's* screens — is a separate two-line adapter. This is the
   domain/UI split the whole project runs on, in miniature, and it buys
   something concrete: the tests exercise a two-monitor layout, a
   monitor-to-the-left layout with negative coordinates, and a zero-screen
   machine, none of which the CI box has. A policy that can only be tested
   on the hardware you own is a policy that is tested once.

2. **The test is overlap, not containment.** Any intersection counts. A window
   hanging half off the right edge is a legitimate thing to restore — the user
   dragged it there and the title bar is still grabbable. Only a window
   *entirely* on a screen that no longer exists is a failure. Containment
   would "fix" windows nobody asked to have fixed, which is its own bug report.

3. **`availableGeometry()`, not `geometry()`.** The available rect excludes
   taskbars and docks, so a window restored entirely underneath the Windows
   taskbar counts as unreachable — which is the honest answer, since you can't
   grab a title bar you can't see.

**The rescue** clears `WindowNoState` before resizing. A blob that says
"maximized" was maximized *on the screen that is gone*; re-applying that flag
would maximize onto whatever display we land on — a second surprise while
recovering from the first. Recovery paths should end in the plainest possible
state, not a partially-honoured version of the state that failed.

---

## §E When to write: an asymmetry worth naming — and its revision (v23.1)

### The original rule (v23.0, lasted one night)

The rail is saved **on every toggle**. The geometry was saved **once, in
`closeEvent`**:

> Write immediately when the **user** makes a decision.
> Batch when the **window manager** does.

Pressing `Ctrl+B` is a rare, deliberate intent that deserves to survive a
force-quit. Dragging a window edge is a hundred `resizeEvent`s describing one
motion; saving each would mean hundreds of writes to record one decision. The
tradeoff table said, honestly: *a hard crash loses the window geometry but
keeps the sidebar choice — the correct way round.*

### How it failed

The first field report arrived within hours, and it was the exact signature
the design predicted: **Ctrl+B persisted, geometry didn't.** The cause: "a
hard crash" quietly included **the developer pressing Qt Creator's Stop
button** — which kills the process without `closeEvent`, and which is how the
person testing the feature ends the app dozens of times a day.

That reclassifies the tradeoff. A save hook that a *rare disaster* skips is an
accepted cost; a save hook that a *normal workflow* skips is a bug with a
justification attached. The give-away in hindsight: the manual-test
instructions had to open with a warning ⚠️ about which button to close the app
with. **When testing a feature requires a warning about how to exit the
program, the feature is fragile, and the warning is the design apologising.**

### The revision: debounce, not write-per-event

The v23.0 objection was never "don't save outside close" — it was "one
decision deserves one write." The revision keeps that objection fully intact:

- `moveEvent`, `resizeEvent`, and `changeEvent` (the latter for
  `WindowStateChange` — maximize/restore arrive *there*, not as a resize,
  which is why the trio has three members instead of the two everyone
  remembers) each restart a **1-second single-shot `QTimer`**.
- Only silence fires it. Drag the edge for ten seconds: one write, one second
  after you let go. `QTimer::start()` on a running timer *restarts* it — that
  single Qt fact is the entire debounce.
- `closeEvent`'s save remains as the final word, covering the app that
  outlives its last resize by hours.

The window's memory is now at most one second stale **no matter how the
process dies** — Stop button, crash, kill, power cut.

### The gate the debounce needs

Move/resize events also fire while the constructor builds the chrome and while
`restoreWindowState()` itself moves the window. A save scheduled *then* would
write the half-built default rectangle over the real memory — **the feature
erasing itself on every startup.** So `m_windowStateRestored` stays false
until restore has finished (set after the call in the constructor, not inside
`restoreWindowState()` — that function has three early returns, and a flag
that must be set on every path out belongs after the call, not copy-pasted
before each `return`). A test pins this: plant a memory, construct a window,
let the loop spin, assert the blob is byte-identical.

Two smaller guards: not while `!isVisible()` (pre-show layout churn), and not
while `isMinimized()` — a minimized window's rectangle is nobody's decision;
Windows parks it wherever it likes, and the un-minimize fires `changeEvent`
and catches the real geometry a second later.

### The rule, corrected

> Write immediately on intent. **Debounce on motion.** Reserve write-on-close
> for nothing that matters — treat it as a courtesy pass, not a load-bearing
> hook.

`closeEvent` is still the right place for *shutdown work* (committing the live
tracking interval). What it is not is a reliable place for *the only copy* of
anything.

---

## §F The compact-screen skip is non-symmetric on purpose

On a phone-sized screen, geometry is neither saved nor restored. Restoring a
1150×780 desktop rectangle onto a 5-inch display isn't a preference, it's a
bug; and *saving* a phone-shaped rectangle would poison a later desktop
session reading the same settings file.

This means `restoreWindowState()` and `saveWindowState()` each contain the same
`isCompactScreen()` guard, and **they must always agree**. That is precisely
why the two are declared adjacent in `MainWindow.h` with a comment saying so.
A pair of functions that must move together should be impossible to read
separately.

The rail's visibility is *not* skipped on compact screens — it's remembered
there like anywhere else. Only its default changes.

---

## §G What this arc touched

| file | change |
|---|---|
| `include/Prefs.h` | new `window/*` section: two getters, two setters |
| `include/Widgets.h` | `overlapsAnyScreen()` (pure) + `availableScreenRects()` |
| `include/MainWindow.h` | `restoreWindowState()` / `saveWindowState()`; `m_nav`; *(v23.1)* the debounce trio declarations, `scheduleWindowStateSave()`, `m_saveWindowTimer`, `m_windowStateRestored` |
| `src/MainWindow.cpp` | `kDefaultWidth/Height`; `isReachable()` adapter; the toggle now persists; `closeEvent` saves; *(v23.1)* debounced save on move/resize/state-change, gated until restore has finished |
| `tests/test_ui.cpp` | 4 policy tests + 3 wiring tests; *(v23.1)* +2: save-without-close, startup-doesn't-erase-the-memory |
| `include/Version.h` | *(v23.1)* stamped 23.1.0 — the v23.0 session bumped every prose copy of the version and missed the single source of truth itself |

**One behaviour moved rather than changed.** The rail used to decide its own
visibility inline (`nav->setVisible(!isCompactScreen())`). That was one rule in
one place, which was fine. The moment the user's choice could outlive a launch
it became *two* rules that have to agree — the remembered choice, and the
screen's default when there is no choice yet. Two rules in two places drift, so
both moved into `restoreWindowState()` and the construction site now just makes
the widget.

> A line that was correct as a single rule can become a bug the moment a second
> rule arrives beside it. Nothing about the original line changed; its
> *neighbourhood* did.

---

## §H Testing: four cheap, five expensive

The policy is pure, so it gets exhaustive cheap tests: overlap, edge-hanging,
one-pixel, the unplugged second monitor, negative coordinates (a monitor
arranged to the *left* of primary has negative x — the naive `x < 0 means
broken` check that people write before they've owned a multi-monitor machine
would reject a perfectly good window), and the zero-screen degenerate case,
which must answer `false` rather than crash or accidentally answer `true`.

The wiring gets five expensive tests that build a real `MainWindow`: the
sidebar choice surviving a relaunch, a corrupt blob falling back to the default
size, geometry being written on close, and — since v23.1 — the two that pin
the debounce:

- **`geometryIsWrittenWithoutAClose`** resizes, waits past the debounce, and
  *never calls `close()`*: the blob must already be on disk. This is the field
  bug as a test — the exact sequence "Stop button after fiddling with the
  window" produces. Note `QTest::qWait`, not a sleep: a timer can only fire in
  a *running* event loop.
- **`startupDoesNotOverwriteTheStoredGeometry`** plants a memory, constructs a
  fresh window, lets the loop spin, and asserts the stored blob is
  byte-identical — the gate that stops the feature erasing itself.

The second test failed twice before passing, both times in the *test*: first
by reading its baseline before `close()` (baking in an assumption — that the
debounce write and the close write are byte-identical — which the claim under
test never needed), then by asserting a pixel-exact restored size (the
offscreen platform's frame margins differ before/after first show, and
`restoreGeometry()` does frame math; 870×628 came back for 870×610 stored).
Both fixes were the same move: **assert the property, not the platform's
arithmetic.**

That ratio is still the point. **Push the thinking into something pure, and
the slow tests only have to prove the plumbing is connected.**

---

## §I Bycatch: a test that failed for one hour a day

The first full run in this arc came back red on
`chipsOpenTheSlidePanelAndActionsFlowThrough` — a v22 test, untouched by any
v23 change.

It dismissed a task until `QDateTime(QDate::currentDate(), QTime(23, 0))` and
then asserted that the put-off chip existed. The build ran at **23:55**. The
dismissal was already in the past, the task came straight back, the put-off list
was empty, and the chip under test was never built.

That test had been failing for one hour out of every twenty-four since the day
it was written, and had simply never been run in that hour.

The fix is `QDateTime::currentDateTime().addSecs(3600)`. The test's actual
requirement is *"this task is currently put off"* — so it now says that, and
lets the clock do whatever it likes.

**The audit that followed is the more useful finding.** Every other
clock-touching test in the suite turned out to be immune, and for one reason:
they inject time through the `nowProvider` seam and drive a fake clock. The one
test that broke was the one that skipped the seam and let the real clock reach
the code under test.

> A seam only protects the tests that actually use it. The dependency didn't
> stop being injectable — one test just declined to inject it, and inherited
> every property of real time, including the time of day.
