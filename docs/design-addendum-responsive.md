# Design Addendum — Responsive Layout (container-driven size classes)

**Status: core implemented** (v30.5). Verified on the desktop by the layout
probe and the new budget test, and on a real Galaxy S21 Ultra over `adb`.
Continues the decision log in `design-doc.md §3`.

**The requirement:** *"Let's work on responsiveness for android… the size of
everything is for desktop."* Followed, when the diagnosis was in, by: *"I don't
want something rushed and hardcoded. I'd like something nice and responsive.
Maybe some UI needs to differ from the desktop version."*

---

## What was actually wrong

Not sizing. **A minimum size.**

`MainWindow::minimumSizeHint()` in compact mode was **522 x 706** against a
phone that gives **360 x 800**, and *Qt honours a minimum it cannot fit by
letting the surplus hang off the screen* — no scrollbar, no warning, no error.
A third of every page was simply unreachable.

Because a `QStackedWidget`'s minimum is the **max over all its pages**, the
page you are looking at is rarely the page at fault. Here the culprit was one
`QCheckBox` on `PomodoroPage` labelled *"Drive the tracked block (focus →
focus, break → break, paused → distracted)"*. A `QCheckBox` cannot word-wrap,
so **its label is its minimum width**: 476 px alone — which clipped the
*Planner*.

Two measurements were needed and neither had been taken:

| Question | Answer | How |
|---|---|---|
| Does Qt report physical or logical px on Android? | **Logical.** dpr **3.00**, screen **360x800** | the running app, printed to `logcat` |
| What is the real minimum, per page? | 522, and which page | `TICKTIMER_PROBE=1` with **real fonts** |

An earlier draft of this file said dpr 2.81 and a 384px screen, derived by
measuring a `setFixedWidth(34)` widget at ~96 physical px in a screencap. The
widget's border was inside that measurement. **24px of budget that did not
exist** — and the estimate was only caught because the app was asked directly.
Measure the device, not a picture of it.

The first retires a wrong guess recorded in `TROUBLESHOOTING.md`, which had
blamed `isCompactScreen()` for reading physical pixels. It never did; compact
mode was on the whole time.

**And this had been fixed once already.** §3.32 records driving the same number
from 550 down to 318. It was 522 four versions later because `ChatPage` and
`UpcomingPage` arrived afterwards and nothing measured them. That is the real
finding: *a layout budget checked by hand is a budget that regresses.*

---

## 3.41 The layout mode is decided by the CONTAINER, not the screen

*Decision:* `responsive::modeFor(int widthPx)` (`include/Responsive.h`) maps a
width to `Compact | Medium | Expanded` at 600/840 logical px, and it is asked
about **the width a widget was actually handed**, re-evaluated on every resize.
`isCompactScreen()` survives at exactly two call sites (below); the other four
are gone.

*Why:* the old helper had three faults and only the third is about tidiness.
It was **binary** (a tablet, a landscape phone and a half-width desktop window
shared one answer); it was **sampled once at construction** (rotating a phone
changed nothing); and it asked about **the screen**, when a page does not care
how big the screen is — it cares how much width *it* got. Those differ
constantly, most obviously when the nav rail opens and takes 190 px from the
page **without the window changing size at all**.

Fixing the third fault fixes the other two for free. The question was wrong,
not the answer.

*Concrete payoff:* the phone in landscape is 854 x 384. The screen test calls
that compact (short side 384) and hides the glance panel; the container test
sees 854 px of width and can bring it back.

*Breakpoints borrowed, not invented:* 600/840 are the standard window size
classes, so "why 600?" has an answer that is not "it looked right on the
author's phone".

*Width only, not a `QSize` — rejected deliberately:* width is the only axis
that *clips* (too tall is a scroll; too wide is content nobody can reach), and
it is the only axis hysteresis can be expressed on. Decisively, **Android's
soft keyboard resizes the window: height collapses, width does not**, so a
size-driven mode would change class in the middle of typing a chat message.
Being width-only is protective. Height genuinely matters for landscape, and
that earns its own `heightClassFor()` when it is built — not a second meaning
smuggled into this one.

*Hysteresis, with the memory as an argument:* `modeFor(width, current)` keeps a
±24 px deadband so a slow window-edge drag cannot rattle the layout. `current`
is a **parameter**, not stored state, so the function stays pure and a test can
hand it any history — including ones no user could produce. Same split as
`overlapsAnyScreen()` beside `availableScreenRects()` in `Widgets.h`.

*Where `isCompactScreen()` stays, renamed in spirit if not yet in name:* the
window-geometry save/restore skip, and the rail's first-run default. Both are
genuine **device** questions ("would a rectangle stored in this settings file
make sense on this machine?"), and both must be answerable *before any layout
exists*, when no container has a width yet.

## 3.42 The mode travels as a `QEvent`, not a base class or a signal

*Decision:* `ResponsiveWatcher` (`include/ResponsiveWatcher.h`) watches one
container and sends a `ResponsiveModeEvent` down its subtree. A widget opts in
by overriding `event()`. The rule is **pull at birth, push on change**: a
widget built after the last dispatch asks `responsive::modeOf(parent)` once in
its constructor, and is told thereafter.

*Why:* Qt already has this pattern three times — `LanguageChange`,
`StyleChange`, `LayoutDirectionChange` are all ambient environment facts that
any widget may or may not care about, delivered as events and ignored by
default. Layout mode is the fourth member of that family.

The decisive reason is local: **half this codebase's widgets have no
`Q_OBJECT`**, deliberately (`Widgets.h` says so; `StatBox` and `PomodoroRing`
are both like that). Event delivery works on any `QWidget`. `PomodoroRing`
needing to shrink on a phone is a live requirement, and the event costs it
nothing.

*Alternative rejected — an abstract base class with `virtual
applyLayoutMode()`:* reads well, fails on reach. There is no common base today
(all seven pages are plain `QWidget`), so it costs seven headers to introduce
and *still* misses `NeedsBlockCard`, `TaskDetailPanel`, `PomodoroRing` and
every dialog unless each of those inherits it too — at which point the base
class means only "a QWidget that can hear about modes", which
`QWidget::event()` already is, for free. It also still needs a dispatcher, so
it saves nothing.

*Alternative rejected — a process-wide policy singleton:* it contradicts the
decision in §3.41. Two containers may legitimately disagree — a docked detail
panel is narrow while the page behind it is wide; a dialog is its own width
entirely. The repo already owns that cautionary tale: `ai::breaker()` is
process-wide, and `test_ui.cpp` warns its verdicts poison unrelated tests.

*The signal is kept, but scoped:* `modeChanged` is offered to the watcher's
**owner** only, for chrome that is not inside the container and so cannot
receive the event — `MainWindow`'s header is a sibling of the page stack, not
a descendant. Signal for the owner, events for the subtree.

*Consequence worth stating:* the chrome follows the **content area's** size
class, not the window's. Open the rail on a 700 px window and the stack drops
to ~510, so the tagline yields with it. That is the right answer anyway.

## 3.43 The watcher attaches to the page stack, and dispatches through the event loop

*Decision:* an event filter on `MainWindow::m_pages`, not
`MainWindow::resizeEvent`; and the dispatch is **queued**, not direct.

*Why the stack:* toggling the rail (Ctrl+B / ☰) changes the page's width by
190 px and produces **no window resize event at all** — hiding `m_nav` merely
re-runs the body layout. A window-level hook would miss the single most common
width change in the app. `togglingTheRailRelayoutsWithoutResizingTheWindow()`
in `test_ui.cpp` pins exactly this, and would fail under either a screen-driven
or a window-driven design.

*Why queued:* `eventFilter` runs inside `QWidget::setGeometry`, inside a layout
activation — the layout engine is part-way through arranging children. Running
page code there is the same hazard family as the delete-inside-a-signal crash
this project already paid for. One hop through the event loop means handlers
run when the loop owns the stack. **The cost, recorded because it will bite:**
a test that resizes and asserts immediately reads the state *before* delivery.

*No debounce, and why that is not an oversight:* the first line is
`if (next == m_mode) return;`. Mode changes are rare by construction, so the
guard is the coalescer, and hysteresis handles boundary jitter. The window
geometry save genuinely needed a 1 s timer because *every* event there
described a decision worth writing. Different problem, different tool.

*The sentinel earns its keep:* a container's width is 0 before its first layout
pass, and 0 classifies as Compact. Without an explicit "never classified yet",
a desktop launch would dispatch a bogus Compact before its first real answer.

## 3.44 The budget is mandatory; the mode is optional

*Decision:* two obligations, deliberately separate. **The budget** — no page's
minimum width may exceed the phone's — is a hard requirement pinned by a test,
and a page satisfies it by never making a width promise it cannot keep, with no
knowledge of any mode. **The mode** is an optional extra a page may use to be
pleasant rather than merely legal.

*Why:* this is the entire cost-control story. "Container-driven, live on
resize" otherwise means every page is, forever, a thing that must arrange
itself three ways — an unbounded tax on every future page. Because ignoring the
event is a *legal* outcome, a new page that never handles it is still correct,
and CI still proves it fits. Only pages that benefit pay.

**If that separation ever erodes — if "every page must handle every mode"
becomes the expectation — this design stops being worth its cost.**

*The three kinds of fix, because there were three kinds of broken promise* —
the same lesson §3.32 drew, re-learned on new pages:

| Page | Was | Now | Fix |
|---|---|---|---|
| PomodoroPage | 522 | **66** | shortened an unwrappable `QCheckBox` label, then wrapped the page in a `QScrollArea` — which severs the width contract entirely |
| ChatPage | 428 | **328** | `QBoxLayout::setDirection` — the header's two action buttons stack under the titles instead of beside them |
| UpcomingPage | 408 | **376** | margins: two nested boxes were spending 96 px of a 384 px screen on whitespace before the filter chips spoke |
| **window** | **522 x 706** | **356 x 393** | |

The checkbox is the instructive one. Its parenthetical was **already in the
tooltip and already narrated live by `m_linkStatus`** — a hard width contract
paying for information the page gave twice. The scroll wrap then makes the fix
structural: no future long label can pin the window again, and the page gains
vertical scrolling it needs in landscape anyway.

## 3.45 The gate, and the fonts that made it possible

*Decision:* `everyPageFitsAPhoneScreen()` asserts every page's minimum width
against a measured phone budget and **names the offending page** in its failure
message. `CMakeLists.txt` gives the `ui` suite `QT_QPA_FONTDIR`.

*Why the font variable is load-bearing, not housekeeping:* the offscreen
platform ships no fonts and substitutes a much wider one, so the same window
measures **1051 px** headlessly against a true 522. `QFontDatabase::families()`
returns **0** entries. Every text-derived width in the process is inflated, by
no constant factor. **The one platform the tests could run on was the one
platform whose numbers were meaningless** — which is precisely why the
regression survived four versions. Pointing Qt at the OS fonts brings it within
~1% (528 vs 522) and takes families from 0 to 56.

*Sharp edge, so the test does not trust it:* `QT_QPA_FONTDIR` **replaces** the
font source rather than adding to it, so a wrong path is worse than none. The
test checks `QFontDatabase::families().isEmpty()` first and **fails on Windows**
(where the directory provably exists, so an empty list is a build-configuration
fault in code we own) while **skipping elsewhere** (a bare Linux container may
genuinely lack fonts; don't punish a contributor for something not in the diff).
Skipping on Windows would let the budget rot silently — exactly how it rotted.

*The budget is 360 — the phone's real width, measured on the phone.* The
window's minimum is **356**. The next target down is **320**, which is what an
iPhone SE gives the WebAssembly build (the same C++ in a canvas); that one is
not met yet and the constant carries the sentence saying so.

## 3.46 Two rules the desktop could not have taught

Both of these were found only by installing the APK and reading `logcat`, and
both are the kind of thing that makes a layout *look* correct in every test
while being unusable in the hand.

### A top-level window is clamped UP to its minimum and never back DOWN

Qt grows a window that is smaller than its `minimumSizeHint`. Nothing ever
shrinks it again when that minimum later falls.

On a desktop this is invisible, because the window manager and the user keep
offering new sizes. On Android the platform offers a size **once**. The app was
born 1150px wide (the desktop default), laid out in Expanded — so the glance
panel was showing and the minimum was ~571 — and when Android handed it the
360px screen, Qt refused and clamped to 571. The size class then became
Compact, the glance went away, and the minimum fell to 386. **The window stayed
at 571 anyway**, with a third of every page off the edge and no scrollbar.

*Decision:* fix it at both ends. `MainWindow`'s constructor sizes itself to the
screen on a compact **device**, so the first layout is already Compact and the
inflation never happens; and `applyChromeMode()` hands back any width beyond
the screen once a mode change has lowered what is needed.

*The safety net is fenced to compact devices, and the fence is the interesting
part.* Unfenced, it silently shrank a 1150px desktop window on any screen
narrower than that — which the suite caught immediately, because a test has
encoded the 1150px default since v23. On a desktop, a window wider than the
screen is the window manager's business and the user's choice. On a phone there
is no such negotiation. That is the difference between repairing a platform
quirk and inventing a policy nobody asked for.

### A mode ladder must be DESCENDABLE

*The rule:* the layout at one mode must be able to shrink **past the breakpoint
that enters the next mode down**, or that mode can never be reached.

This is not obvious and it cost a long hunt. Every page was under budget, the
window's minimum was a healthy 426 — and the app still sat at 644px in Medium
and would not go Compact. The reason was the tagline: 236px of unwrappable
`QLabel` that the first draft hid only at Compact. With it showing, Medium's
floor was above the 576px a window must get *under* in order to become Compact.
The ladder had a rung missing, and the layout was stuck standing on it.

*Decision:* the slogan yields at **Medium**, not merely at Compact. The code
carries a comment saying so, because `!compact` is the obvious-looking
simplification and it silently re-breaks the ladder.

*The generalisation, for whoever adds the fourth mode:* whenever a widget is
hidden or shrunk at mode N+1, ask whether mode N can still get small enough to
reach N+1 without it. A breakpoint is a claim about widths the layout must
actually be able to produce.

### And one measurement: Android's default font is 19pt

Qt takes the app font from the system, and this phone reports **19pt Roboto**
against a desktop's ~9pt. Most of the UI is sized by the stylesheet in `px` and
so is unaffected — which is exactly why the difference is dangerous: it moves
only the widgets the stylesheet does *not* size, and those turned out to be the
chrome. `TICKTIMER_FONTPT` on the screenshot tool exists to reproduce it.

## 3.47 A dialog is its own surface, and on a phone it should BE the screen

Reported after the core shipped, as four separate complaints that turned out to
be one gap plus one oversight: switching panels reverted to desktop width, the
plan-a-block picker was desktop-sized, quick capture was desktop-sized, and the
login screen was "not sized to screen".

*The gap:* a `QDialog` is its **own top-level window** with its own minimum, so
nothing in §3.41–3.46 reaches it. `MainWindow`'s watcher governs `MainWindow`'s
page stack and nothing else, and `everyPageFitsAPhoneScreen()` measured exactly
that. Three separate surfaces were never measured by anything.

*Decision:* one application-level event filter,
`responsive::installCompactDialogFitter()`, installed in `main()` before the
login dialog (the first dialog shown). On a compact **device** it gives every
`QDialog` the screen on `QEvent::Show`, and it maps `Qt::Key_Back` to
`reject()`. Opt out with `setProperty("noCompactFit", true)`.

*Why device, not container:* a top-level window's container **is** the screen.
This is the same reasoning that kept `isCompactScreen()` alive for the geometry
save/restore skip, not an exception to §3.41.

*Why one filter and not a line in eleven constructors:* eleven chances to
forget, no way to notice, and a dialog written next year gets it for free.

*Why `setGeometry` and not `showFullScreen()`:* the dialog stays an ordinary
dialog — `exec()`, `reject()`, the caller's result handling all unchanged — and
only its rectangle differs. A window STATE would have to be undone on a tablet,
and "remember to undo the platform tweak" is where these things rot.

*Back is not a bonus, it is a consequence.* Qt maps `Esc` to `reject()` and
knows nothing about `Qt::Key_Back`. A dialog that now fills the screen with no
visible way out is strictly worse than a small one — so the same filter that
grants the screen must also grant the exit. **This closes the `SyncDialog`
soft-lock** that previously needed `adb shell am force-stop`, verified on the
device.

*Fitting is necessary, not sufficient — the two-part shape of this bug.*
`LoginDialog`'s minimum was only **234px**: it already fit, and was still
wrong, because a dialog that merely fits is a small panel adrift on a phone.
`QuickCaptureOverlay` (552) and `PickActivityDialog` (474) had the opposite
problem — hard promises they could not keep. Both halves needed answering:

| Surface | Was | Fix |
|---|---|---|
| `QuickCaptureOverlay` | 552 | `m_input`'s 520px floor becomes 0 when compact |
| `PickActivityDialog` | 474 | subtitle gains `setWordWrap` (right on every screen); the duration pills — one per free slot, so as wide as the day is empty — move into a `QScrollArea`, which severs the promise rather than removing choices |
| `LoginDialog` | 234, but adrift | the fitter |

## 3.48 Any layout change can lower the minimum, so any layout change is a refit

*The reported sequence:* open the rail, switch page, close the rail — and the
window stays at its widest, a third of the page off the edge.

*Why §3.46's fix did not cover it:* that safety net ran on **mode changes**.
Opening the rail adds its 190px to the window's minimum, so the window inflates;
closing it drops the minimum again **without changing the size class**, so a
mode-change hook never fires.

*Decision:* `MainWindow::refitToScreen()` is called from an event filter on
`QEvent::LayoutRequest` — Qt's own "a layout below me was invalidated", which is
the general form of "the minimum may just have changed". Watching the general
signal rather than each cause (the rail, a page switch, a mode handler hiding a
widget) means the next cause is covered without anyone remembering to add a hook.

*And it must be QUEUED.* The first attempt read `minimumSizeHint()` directly in
the filter and did nothing at all, because `LayoutRequest` arrives while the
layout is being *invalidated* — the old, larger minimum is still what you get,
so the test "has the minimum dropped below the screen?" always answers no. One
hop through the event loop and the layout engine has recomputed. Same lesson as
the watcher's queued dispatch, learned a second time: **read sizes when the
event loop owns the stack, not when the layout engine does.**

*Not unit-tested, and the reason is worth recording:* the bug only exists when
the window is wider than the screen, and the offscreen platform's virtual screen
is 800px — wider than any state this sequence produces. The condition cannot
arise there. Verified on the device instead.

## 3.49 On a touchscreen a press is not a decision

*Reported:* "when just clicking on the agenda to attempt to scroll down, it
opens the modal."

*The cause is not a bug in the handler, it is a category error.* `AgendaWidget`
acted on `mousePressEvent`, which on a desktop is exactly right — a press there
IS a decision. On a touchscreen it is the first frame of an ambiguous gesture:
a tap and a scroll begin identically and are distinguishable only by what
happens next. Qt synthesises a mouse press from the first touch, so the widget
committed before the finger had said anything.

*Decision:* defer, and split by what the gesture COSTS.

| gesture | acts on | why |
|---|---|---|
| empty slot → plan a block | **long press**, 450ms | creating something deserves deliberateness, and it makes accidental blocks impossible |
| an existing block → open it | **release**, if unmoved | opening is not creating; "hold to open" would be a surprising gesture |

Movement past `QApplication::startDragDistance()` cancels both — that is a
scroll. A real mouse (`QMouseEvent::source() == Qt::MouseEventNotSynthesized`)
keeps the old immediate behaviour: the ambiguity does not exist there, and
adding a hold to a desktop click would be a regression.

*The non-obvious half, and the one that would have shipped broken:* the timer
must also be cancelled on `QEvent::UngrabMouse`. That is how `QScroller`
announces it has taken the gesture over to pan, and it is the only reliable
signal that a press became a flick. Without it the long press still fires
mid-scroll.

*And the words change with the gesture.* The agenda's caption now reads "press
and hold a free slot" on a phone and "click a free slot" on a desktop. An
instruction that names the one gesture which no longer works is worse than no
instruction — the same standard this project applies to its own docs.

*Also in this change, because the agenda was reported as "not pretty at all":*
the hour gutter drops 64 → 44 (64 is 18% of a phone spent on "12 PM"), the
vertical scrollbar is switched off on compact — on a touchscreen the gesture is
the affordance, so the bar is width spent on decoration over the content it is
stealing from — and the panel margins tighten.

## 3.50 Fill the screen, or take the width — not one rule for every dialog

*Reported:* "capture takes the full width, which is okay, but also takes the
full height, which takes screen space for no reason."

*Correct, and §3.47 was too blunt.* "Every dialog gets the screen" is right for
a dialog you WORK INSIDE (the block picker, Sync, login) and wrong for one you
type a single line into: quick capture became a screenful of white with its
hint floating in the middle of it.

*Decision:* a second opt-in property, `compactTopSheet` — full width, natural
height. `QuickCaptureOverlay` sets it. The taxonomy is now: `noCompactFit`
(leave alone), `compactTopSheet` (width only), default (the screen).

*Two things went wrong getting the height right, and both were the same
mistake.* First, the height was computed from `sizeHint()` during
`QEvent::Show` — before the widget is polished, so the field's stylesheet
padding was missing from its hint and the sheet squeezed the text in half. The
fit is now **queued**, which is the third time this arc has landed on *read
sizes when the event loop owns the stack, not when the layout engine does*
(§3.43 and §3.48 are the other two). Second, the width must be applied BEFORE
the height is asked for, because a layout's preferred height depends on the
width it is given.

*And one thing that could not be solved, only avoided:* the sheet was pinned to
`availableGeometry().y()` and landed **under the status bar**. Qt reports that
`y` as 0 on this device — and the parent window's `geometry()` as 0 too — while
Android draws its status bar over the strip, and Qt Widgets has no safe-area
API to ask. So the fitter takes the width and **leaves the caller's `y`
alone**: `popup()` already places it in the parent's upper third, clear of any
system bar. Not knowing where the top is, and declining to guess, beat two
plausible guesses that both shipped broken to the phone.

---

## What changed where

| Layer | File(s) | Change |
|---|---|---|
| pure brain | `Responsive.h` (new) | `modeFor()`, breakpoints, hysteresis. **Zero Qt includes** |
| policy | `ResponsiveWatcher.h/.cpp` (new) | the watcher, the event, `modeOf()`, the subtree walk with its nesting prune |
| glass | `MainWindow.cpp` | watcher on `m_pages`; chrome via the signal (tagline, welcome, header margins, density, the re-fit safety net); compact-device initial size |
| theme | `Theme.h` | `appStyleSheet(bool compact)` — a narrow variant that takes padding off width-critical controls |
| policy | `ResponsiveWatcher.h/.cpp` | `installCompactDialogFitter()` — dialogs get the screen, `Qt::Key_Back` maps to `reject()` |
| glass | `QuickCaptureOverlay`, `PickActivityDialog` | hard width floors relaxed on a phone; subtitle wraps; duration pills scroll |
| glass | `MainWindow.cpp` | `refitToScreen()` on `QEvent::LayoutRequest`, queued |
| glass | `AgendaWidget` | long-press to plan, tap-to-open, both cancelled by movement or by `QScroller` taking the grab |
| glass | `PlannerPage` | compact gutter, no scrollbar on touch, tighter panel margins, gesture-accurate caption |
| glass | `PomodoroPage`, `PlannerPage`, `ActivitiesPage`, `ChatPage`, `UpcomingPage` | `event()` + `applyLayoutMode()`; five `isCompactScreen()` calls retired |
| glass | `PomodoroPage` | scroll wrap, shortened label, mode-driven ring diameter |
| tools | `tools/screenshot.cpp` | probe moved AFTER the event loop settles (the mode arrives queued); names every **widget** over budget, not just the page; `TICKTIMER_FONTPT` reproduces a phone's text metrics |
| tests | `test_nlp.cpp` | the breakpoint table + the anti-thrash sweep |
| tests | `test_ui.cpp` | the budget gate, the mode pipe, the rail-toggle proof |
| build | `CMakeLists.txt` | `QT_QPA_FONTDIR` for the `ui` suite |

`Responsive.h` is pinned by **`test_nlp`**, which links `Qt6::Core Qt6::Test`
only. A layout judgement that compiles with neither Gui nor Widgets cannot have
learned anything about widgets — the same admission rule that put `LlmProvider`
and `chat::` in that suite. *(Noted friction: the suite's name reads oddly for
a layout table. `test_domain` was the alternative and is a worse fit — this is
not domain.)*

Domain, storage, sync, AI: **zero changes.**

---

## Explicitly NOT done

- **The nav rail is still in-flow.** With the rail OPEN the minimum is
  376 + 190 = **566**, so tapping ☰ on a phone brings the clipping back. The
  rail must become an overlay drawer on compact screens. This makes phone
  navigation load-bearing rather than polish.
- **Dialogs: mostly done as of §3.47, with one piece left.** They now fill the
  screen on a phone and `Qt::Key_Back` dismisses them, and the three reported
  surfaces are under test. What is NOT done is the *sheet* presentation itself —
  `SlidePanel` (a scrim plus a sheet that is a child of its host, so bounded by
  construction) would give them an animated entrance, a visible close
  affordance, and a shape that reads as native rather than "a desktop dialog
  stretched to fill the screen". `QuickCaptureOverlay` full-screen currently
  leaves a large empty area with its hint line floating in the middle.
- **Touch density, in the direction that matters.** `appStyleSheet(compact)`
  exists, but it currently only *removes* padding so a crowded row fits. The
  work still owed pulls the other way: at dpr 3.0 a 24px close button is 8
  logical px of thumb target, and ~48dp is the guideline. Reconciling "narrower
  so it fits" with "bigger so it can be hit" is that stage's real problem —
  probably fewer controls on screen at once rather than smaller ones. After
  this work the phone looks **correct-and-clumsy, not correct-and-good.**
- **`ActivitiesPage` is still a permanent two-column split**, which is the
  wrong shape on a 384 px screen however the width is divided. Narrowing its
  rail is a mitigation and is labelled as one in the code.
- **Landscape.** A width-only class hands a wide arrangement into 384 px of
  *height*. `heightClassFor()` is the deferred answer (§3.41).
- **Nested watchers.** The pruning rule is implemented and documented; nothing
  uses it yet. The two-pane stage will be the first.
