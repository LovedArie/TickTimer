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

## 3.51 The block picker was on the wrong side of §3.50's line

*Reported (v30.7):* "it takes the whole screen for a modal, would it be
possible to make it a little smaller."

*§3.50 named the block picker specifically* as a dialog you WORK INSIDE, and
therefore one that should fill the screen. That was wrong, and the reason it
was wrong is worth more than the fix: the taxonomy was built around how much
CONTENT a dialog has, and the property that actually matters is whether the
app is still behind it. Quick capture and the picker both got classified by
size — one line versus a list — when what separates login from the picker is
that login IS the app while it is up, and the picker is a question about a
planner you are still looking at. Covering the day you just touched makes the
question harder to answer.

*Decision:* a third fit, `card` — natural size, inset 16px, centred. The
picker and `EventDialog` ask for it; login, settings and sync keep the screen.

*The alternative rejected, and it is the tempting one:* drop the declaration
entirely and give every dialog `min(sizeHint, room)`. It sounds strictly
better and it silently reinstates the original bug — `LoginDialog`'s minimum
is 234px, so the rule would hand it the "small floating panel adrift on a
phone" that §3.47 was written to cure. **No size hint can answer a question
that is not about size.** The test that pins §3.47 caught this before the
change was written, which is the whole argument for having pinned it.

*Also folded in:* `noCompactFit` and `compactTopSheet` became one property,
`compactFit`, with named values. Three mutually-exclusive booleans is a state
space with legal nonsense in it ("no fit, and also a top sheet"); a closed set
of names cannot express the contradiction. Absent still means "screen", so
nothing that never heard of the property changed behaviour.

## 3.52 Four dialogs were over the phone budget, and nothing was measuring them

*Found while fixing §3.51, not reported.* `EventDialog` and `SharingDialog`
asked for a 420px minimum, `SyncDialog` and `TaskDetailDialog` for 380px,
against a 360px screen — and §3.48's own gate had been measuring **pages**
only. TROUBLESHOOTING predicted this exact hole after v30.5.1 ("a dialog is
its own top-level window with its own minimum, and nothing measures it") and
it stayed open for two versions.

*Decision:* the gate grows dialogs, and the fixes follow the recipes §3.32
already established — `isCompactScreen() ? 0 : N` for the hard minimums, word
wrap for the sentences, and for rows that cannot promise their width, either
a direction flip or a fold.

*The one new judgement:* `EventDialog`'s four reschedule nudges became a **2x2
grid** rather than a stack. Direction-flipping is the recipe for a row of
unequal things; these four are short and pair naturally (two earlier, two
later), so folding costs no height in a dialog that was already gaining eight
stacked buttons. A rule applied without asking what the row IS produces a
correct dialog nobody wants to scroll.

*And the gate now names its culprit.* A budget test that reports only the
total ("EventDialog is 402px") leaves the same hunt §3.32 burned a session on
before the layout probe existed. It now lists the widest offending widgets AND
layouts, because they fail differently: one unwrapped label is a wide widget,
while four reasonable buttons in a row is a wide layout. That distinction is
what turned "402px, somewhere" into "the row is 362, the grid fixes it" in one
run.

*A note on how close this was.* Before the last fix the dialog measured 378px
standalone and passed under ctest, because the two runs resolve fonts
differently and a four-button row moves ~20px between them. A gate that
depends on which font environment ran it is not a gate. The fix is not a wider
budget, it is a layout that is not near the line.

---

## 3.53 A confirm the keyboard used to provide

*Reported (v30.7):* "if I want to write the activity instead of selecting it,
there's no way to confirm."

*The picker deliberately has no OK button* — §UC1's "a click on an activity
confirms" is one tap fewer than a select-then-OK dance, and that is still
right for the LIST. It was never true of the text field. Typing your own block
was bound to `QLineEdit::returnPressed`, and the only place that was written
down was the field's placeholder, which Qt hides the moment you type the first
character. **The instruction disappeared exactly when it became relevant.**

On a phone there is no Enter key at all, so a path that was merely
undiscoverable on a desktop was closed.

*Decision:* an explicit "Plan it" button beside the field, disabled until
there is something to plan. Enter still works.

*Why not phone-only,* which §3.30 would seem to invite: this is not a layout
question and geometry has nothing to say about it. A hint that vanishes when
you start typing is broken on a desktop too — it is merely survivable there,
because the key exists. One affordance, one code path, every platform.

*The general lesson, and it is not about phones:* an affordance documented
only in a placeholder is documented nowhere. Placeholders are hints for an
EMPTY field; anything that is still true once the user starts typing needs to
live somewhere that does not disappear.

---

## 3.54 A touch standard, and a gate that keeps it

*Asked for (v30.7):* "go through every functionality and make sure its UI is
user friendly and comfortable to use... I'm sure there's a norm."

There is, and none of it is in the books this project usually reasons from.
Bass/Clements/Kazman genuinely applies one level up — usability is a quality
attribute with tactics, and "support user initiative" is exactly what a
missing confirm button fails — but the numbers come from the platform:
**48x48dp** is Material's minimum touch target and WCAG 2.5.5 (AAA); **24dp**
is WCAG 2.5.8's Level AA floor.

*The measurement that made the audit arithmetic rather than judgement.* On
this device one Qt logical pixel is exactly one Android dp — dpr 3.00, a
360x800 logical screen on a 1080x2400 panel at density 480 — so **every pixel
number in the codebase is already a dp** and can be compared to the guideline
with no conversion. Verified against the app's own `EventDialog` swatch:
`setFixedSize(15, 15)`, measured 45 physical px in a screengrab.

That identity holds only because nothing in this repo sets `QT_SCALE_FACTOR`
or a high-DPI rounding policy. Any of them would break it silently, which is
why `Touch.h` says so at the top.

*What the audit found.* The stylesheet contained exactly two `min-height`
rules and both were scrollbar handles: **no interactive control in the app
had any minimum size at all.** Every button's height was font plus padding.
The delete ✕ on every list row was ~22dp; the only door to Settings on a
phone was 34; the only door to Special days was 32.

*Decision:* `include/Touch.h` — the rule as a pure value, `compact` as a
parameter, in the Core-only suite beside `Responsive.h`. Then three
applications of it: a `min-height` floor in the compact stylesheet,
`touch::sizeFor` at the fixed-size call sites, and `touch::expand` at the
delegates' hit-test (paint stays small — a 48dp checkbox drawn on a task card
looks like a bug).

*Rejected: editing the ~25 failing `setFixedSize` calls to 48.* It fixes
today and nothing else. Four dialog minimums had already drifted over the
360px width budget in the two versions after §3.48 fixed the pages, for
exactly that reason — the gate covered pages and nothing measured dialogs.

*The keystone is therefore the gate, not the fix.*
`everyTouchTargetIsBigEnoughForAThumb` builds the real window at 360x800,
walks every page, and **names each offender with its size**. Its first run
listed 33. "Something is too small" costs an afternoon; `profileBtn 34x34`
costs a minute. It stands opposite `everyPageFitsAPhoneScreen` and the two
pull against each other on purpose — narrower to fit, taller to touch — which
turns out to be survivable because they act on different axes.

*Two mechanical things learned by running the gate rather than reasoning:*

- **QSS `min-height` is the CONTENT box for a button and the TOTAL for a
  `QCheckBox`.** `QPushButton { padding: 7px 9px; min-height: 34px }` yields
  48; `QCheckBox { min-height: 34px }` yields 34. Every rule in the compact
  block is tuned to its own padding, one gate run at a time.
- **The compact stylesheet had been nearly inert.** Its whole delta was two
  padding reductions, and QSS implements CSS specificity — so
  `QPushButton { padding }` lost to `QPushButton#primary` in the base sheet.
  `#primary`, `#quiet`, `#danger`, `#segment` and the rest had never received
  a single compact rule. Every id is spelled out now, which looks like
  repetition and is the only thing that works.

## 3.55 Two compromises, named rather than rounded up

Not everything can be 48, and saying which and why is the difference between
a standard and a slogan. `touch::meetsFloor()` exists to express it: above
WCAG's 24dp AA floor, below Material's 48.

**The agenda's half-hour slot: 30 → 44, not 48.** A timeline is a canvas, not
a row of buttons. Every pixel per slot is an hour less of the day on screen,
and at 48 an 18-hour day needs nearly two full screens of scrolling before
the afternoon is visible. 44 keeps the day readable and lands within 4dp.

**The category row's right-hand cluster.** Delete, Archive and Due sit
side by side with a 9px gap. Growing them to 48 wide would overlap them, and
Material is explicit that sub-48 targets must not overlap — a tap in the
overlap goes silently to whichever zone is tested first, which is how a
due-date tap becomes a delete. So they take the full row height and only half
the gap sideways. The checkbox, whose only neighbour is the row's own "edit"
action, takes the full 48: the worst case there is that a near-miss means
"done" instead of "open", and that is what the user was aiming at.

*The real fix for that cluster is fewer affordances per row on a phone* —
archive and due belong in the task's own sheet — which is a behaviour change
rather than a size change. Named here as the next step instead of smuggled
into a size pass.

## 3.56 Two pages a phone could not reach, and a window drawn under the clock

Found by inventory rather than by report, which is the point of doing one.

**`ArchivePage` had no door at all on a phone.** Its single entry point was a
button on the desktop nav rail, and the rail is force-hidden on compact. Both
*restore* and *delete forever* were unreachable. **Special days** was one step
better: reachable only through the header action, and only while standing on
the Upcoming page. Both now sit in the profile menu — the phone's "everything
else" door, which already existed. Not the bottom bar: that is five
destinations you visit constantly, and these are two you visit rarely.

**targetSdk 35 → 34.** Android 15 forces edge-to-edge on any app targeting
35 or higher: the app is handed the whole panel and made responsible for
insetting itself out from under a 26dp status bar and a 48dp gesture bar.
Every layout here assumes an already-inset window, so what it produced was
content quietly underneath the system chrome. Targeting 34 opts out entirely
and Android insets the window for us.

This also retires §3.50's unsolved corner — the sheet that landed under the
status bar because `availableGeometry().y()` reads 0 while Android draws over
that strip. It was never a positioning bug; it was edge-to-edge, unhandled.
The cost is that Google removes the opt-out for apps targeting 36, which
never binds here: those deadlines belong to the Play Store, and this app is
sideloaded (`docs/ANDROID.md`).

**The Pomodoro mini-window is not offered on a phone.** Its whole premise —
`Qt::WindowStaysOnTopHint`, "visible over other apps while you work
elsewhere" — is impossible on Android without the `SYSTEM_ALERT_WINDOW`
overlay permission. What it actually floated over was TickTimer's own modal
dialogs; it was found sitting exactly on the block picker's "Plan it" button,
which is how a working control came to look like a missing one.

---

## 3.57 A width budget measured against an empty page certifies nothing

`everyPageFitsAPhoneScreen` had been green for three versions while two pages
clipped every row on the device. Both failures were the same shape.

The **first** was structural. A page inside a `QScrollArea` reports the scroll
area's minimum, and that minimum deliberately ignores the content — the
severing is the whole reason pages use one (§3.32). So the budget test asked
each page "how narrow can you be?", every page answered "as narrow as you
like", and the content simply panned sideways underneath. Horizontal scrolling
on a phone is never an answer; it *hides* an overflow.

**Choice:** ask the scrollbar, not the size hint. For every visible
`QScrollArea` on a page, `horizontalScrollBar()->maximum() > 0` means there is
something to scroll sideways, and that is a failure.

**Alternative rejected:** compare `area->widget()->sizeHint().width()` to the
viewport. Tried first, and it reported `PlannerPage 560px of content in 360px`
— a false positive. With `setWidgetResizable(true)` the content widget is
*stretched* to the viewport, so its size hint says what it would like rather
than what it needs. The scrollbar's range is the direct question, and it
answers correctly even when the bar is hidden by policy, which on compact it
always is.

The **second** was the fixture. `ArchivePage` still passed the new check,
because the test account's `data.json` holds nothing archived — three "nothing
here yet" labels fit any screen. So the gate grew a sibling,
`crowdedPagesStillFitAPhoneScreen`, which seeds an archived category, an
archived task, an archived activity and a special day, deliberately with names
longer than a phone is wide. Content **is** the width; a page measured empty is
not measured.

Both pages then failed for the same root cause, worth stating once because it
will recur: **an unwrapped `QLabel` reports its entire text as its minimum
width.** A row pairing a free-text title with "Wednesday, September 17, 2026"
before a countdown and two buttons cannot fit, and no margin change saves it.
`setWordWrap(true)` drops the minimum to the longest single word.

The same hole existed for dialogs, and closing it there caught a live defect
nothing had reported: the block picker's four duration pills came to 344dp
inside a 360dp phone, so the last one sat off-screen. 16px of side padding is
generous on a desktop and is the whole budget on a phone.

---

## 3.58 `QBoxLayout` takes its direction as a constructor argument

Four surfaces needed the same thing on a phone — a row of controls that has to
become a column — and Qt already had the answer.

`QHBoxLayout` and `QVBoxLayout` are not two classes; they are `QBoxLayout` with
a fixed direction. Constructing `QBoxLayout` directly and passing
`compact ? TopToBottom : LeftToRight` means the *same* sequence of `addWidget`
calls builds a row on a desktop and a column on a phone. One word of
difference, and — the part that matters — no second layout to keep in step with
the first when someone adds a control next year.

Applied to `SpecialDaysPage`'s add form (a line edit, a date, a checkbox with a
two-word label and a button: past 400dp side by side), `SpecialDaysPage`'s day
cards, `SettingsDialog`'s nav-beside-pages body, and `CompareDialog`'s
agendas-beside-stats body.

**One trap it carries:** a stretch factor is a horizontal instruction in a row
and a vertical one in a column. `addWidget(nameInput, 1)` gives the input the
spare width on a desktop and balloons a one-line field to fill the page on a
phone. The stretch has to be conditional too.

**Alternative rejected:** a `QStackedLayout` holding both arrangements, which
is what the watcher's "build both, swap between them" rule prescribes for a
*container* whose mode can change while it is on screen. These four are not
that: three are dialogs that live and die inside one presentation, and the
fourth is a page rebuilt from data. Building each surface twice to support a
transition that cannot happen is cost with no buyer.

---

## 3.59 Settings: a fixed nav column is a quarter of a phone

`SettingsDialog` spent 168dp on its nav column, 20 on spacing and 40 on
margins, leaving **132dp of a 360dp screen for the settings themselves**. It
passed the dialog budget test the whole time, for §3.57's reason: its scroll
area absorbed the overflow.

**Choice:** on a compact device the column is replaced by a single-line
`QComboBox` above the pages. Both controls are built, always; exactly one is
ever visible. That is the watcher's rule, and it also keeps the tests that
reach into settings by `objectName` working on either device.

The two switchers are kept in step **through the stack**, not through each
other: each drives `setCurrentIndex`, and `currentChanged` writes both back
under a `QSignalBlocker`. Control-to-control would be a feedback loop; going
through the thing they both change cannot be, because setting an index to the
one already held emits nothing.

This is the same preference the Activities page settled on — a labelled control
that says where you are and changes it, rather than a permanent column that
only says.

**And the forms inside it:** `QFormLayout` puts a label beside its field, so a
row needs the longest label *plus* the widest field. Qt's own answer for a
small screen is `WrapAllRows` — label above field — which makes a row's minimum
the *wider* of the two rather than their sum. One helper, `makePhoneFriendly`,
applied to all five settings forms.

---

## 3.60 The week view's axis, and what a column is for

Seven day columns plus an hour axis have to share 360dp however tidy each one
is. Measured: **41dp per column**, because the axis was still 64dp — the
desktop `kDefaultGutter` — even though the day view had had a 44dp
`kCompactGutter` since v30.5 and simply never told the week view about it.

**Choice:** one helper, `AgendaWidget::gutterWidth(bool compact)`, and both
views ask it. 20dp back off the axis is ~3dp onto every one of the seven
columns.

`compact` is a **parameter**, not a lookup, and the distinction is the one
`Responsive.h` keeps: `PlannerPage` knows its container's mode and passes that,
because a desktop window dragged narrow should get the narrow gutter too; the
week view has no mode plumbing and asks the device. Taking the argument lets
both be right. `slotHeight()` deliberately does **not** take one — seven
columns and one shared axis must agree on a row height or 9 AM stops being one
horizontal line.

**The gate asserts 40dp, not 48**, and the reason is the one §3.55 already
made for the agenda's slot height: a day column is a **canvas**, not a button.
The blocks drawn inside it are the targets. 40 clears WCAG 2.5.8's 24dp floor
comfortably and is what seven columns can actually have.

---

## 3.61 The task drawer is the wrong container at 200dp

`TaskDetailPanel` is 440dp wide, capped by "the host's width minus the 220dp it
refuses to cover" — an overlay that covers everything is a modal with extra
steps. On a 360dp phone that arithmetic gives 140, floored at **200**, and a
title, notes, a due date and time, repeat, priority, estimate and two buttons
do not go in 200dp.

**Choice:** on a compact device, route to the modal the panel was built to
replace. `installCompactDialogFitter` already gives it the whole screen, which
is the platform's own answer for a complex form on a small screen.

**Why this is not a retreat:** the panel's justification is swap-in-place
navigation between tasks *without losing sight of the list*, and at this width
there is no list left in view. The modal has its own hop loop, so navigation
survives; only the split-screen premise does not.

The decision lives in one function, `dockedTaskPanelFor()`, shared by both
entry points — and it is **declared in the header**, which is a testability
choice worth naming. Both containers end in a working form, so the only
observable difference is which one opened; and the modal runs its own event
loop, so a test that let `runTaskDetail` choose would *hang* rather than fail.
It did, for five minutes, before the routing was exposed. A decision you cannot
observe without blocking is a decision you cannot test.

The gate asserts **both** directions — nullptr on a phone, the real panel on a
desktop — because a one-sided assertion would also pass if the function simply
always said nullptr and every desktop quietly lost its drawer.

---

## 3.62 The compare screen stacks instead of shrinking

Two agendas side by side plus a fixed 250dp stats column plus 48dp of margin.
On a phone the stats column alone is a quarter of the screen, sitting beside
the one thing the screen exists to show.

**Choice:** on compact, the numbers go **under** the two days rather than
beside them (§3.58's direction flip), and the fixed width comes off. The two
agendas keep the full width, because two days comparable at a glance *is* the
screen — measured minimum after the change: **215dp**.

**Alternative rejected:** one day at a time with a toggle. It fits trivially
and destroys the feature: a comparison you have to remember is not a
comparison.

---

## 3.63 The gate says what a phone must MEET; nothing said what a desktop is SPARED

**Choice.** `makeTouchScrollable`'s item-view overload and `ToggleSwitch`'s
padding now sit behind `isCompactScreen()`, like every other phone decision in
`Widgets.h`. Two `QVERIFY2`s in `test_ui` pin the desktop branch.

**Why.** §3.54 built a gate that asks whether the *phone* meets 48dp. It is a
floor, and a floor only has a bottom — nothing in it ever asked the opposite
question, which is what the *desktop* should not receive. So two touch
decisions shipped unconditionally and changed the desktop app without anyone
choosing that:

- **`QScroller::grabGesture(..., LeftMouseButtonGesture)`**, at sixteen call
  sites. The asymmetry with the `QScrollArea` overload two functions above is
  the whole story: that one grabs `TouchGesture`, which is **inert under a
  mouse**, which is why it was always safe ungated. `LeftMouseButtonGesture`
  is the opposite — it exists *because* an item view's own gesture is a mouse
  click, and `QScroller` tells a tap from a drag by a threshold. On a phone it
  is the only thing that works. On a desktop it silently converts a left-drag
  into a pan in the Activities rail, the activity picker and the Settings nav.
- **`setHorizontalScrollBarPolicy(AlwaysOff)`**, in the same function. Hiding
  a scrollbar does not narrow anything; it makes overflow unreachable instead
  of scrollable. That is §3.48's lesson and v30.8's entire release, arriving
  through a door nobody was watching.
- **`ToggleSwitch::setFixedSize(52, 48)`.** The visible switch is a 52x32
  track at both sizes; the extra 16px is invisible padding that exists so the
  *target* clears 48dp. A pointer has no such minimum — it lands where it is
  aimed — so on the desktop that padding is dead space that reads as a toggle
  floating in a gap. `paintEvent` already centres the track in `height()`, so
  both sizes draw the identical control and only the hit area differs.

**Alternative rejected: leave them ungated — "one app, one shape."** Defensible
for the toggle, where a single switch at a single size is a real position.
Not defensible for the gesture: losing left-drag in three lists is a
behaviour removed from desktop users, not a cosmetic difference. Once the
gesture had to be gated, gating the toggle in the same breath cost nothing and
kept one answer to "who is this for?" rather than two.

**Alternative rejected: grab `TouchGesture` for item views too**, so a
touchscreen desktop keeps finger-scrolling. This is the tempting fix, and it
is a different design question — the overload exists precisely because the
item-view case needs tap-versus-drag disambiguation that the plain scroll-area
case does not. Changing which gesture is grabbed deserves its own measurement
on a real touchscreen, and this fleet has none: two phones and a mouse-driven
Windows desktop. A mouse user losing drag is a certainty; a large touchscreen
user losing flick is a hypothetical. Named here so the trade is visible rather
than forgotten.

**Alternative rejected: gate on `responsive::Mode` instead.** The most
interesting one, because it looks more modern — §3.41 argues at length that
the container's width beats the screen's. It is wrong here, and the reason
draws the line between the two mechanisms this codebase now runs:

> **Layout is a question about SPACE. Gestures and hit targets are questions
> about the INPUT DEVICE.** A desktop window dragged narrow, or a page squeezed
> by the nav rail, genuinely has phone-sized *room* and should get the phone
> *layout* — that is §3.41 working. It does not thereby acquire a finger. It is
> still driven by a mouse, and giving it phone *gestures* would be answering a
> question nobody asked.

So these stay on `isCompactScreen()` — which asks about the screen, once, and
is the closest proxy for "is there a finger here?" that the app has — while
layout keeps moving to the container-driven classes. Two questions, two
mechanisms, on purpose. That is the rule to apply when the next phone
affordance lands: **if it changes what fits, ask the container; if it changes
how you touch it, ask the screen.**

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
| glass | `SpecialDaysPage`, `ArchivePage` | wrapped row labels, compact margins, `touch::sizeFor` on the ✕, add form and day cards stack (v30.8) |
| glass | `SettingsDialog` + `SettingsPages` | a `QComboBox` section switcher in place of the 168dp nav column; `makePhoneFriendly` on all five forms (v30.8) |
| glass | `CompareDialog` | stats stack under the agendas, fixed 250dp width dropped, nav arrows to `touch::sizeFor` (v30.8) |
| glass | `WeekAgendaView` + `AgendaWidget` | `gutterWidth(bool compact)` — one helper both views ask, so the week axis stops being 64dp on a phone (v30.8) |
| policy | `TaskDetailDialog.cpp` | `dockedTaskPanelFor()` — a phone gets the full-screen modal, not the 200dp drawer (v30.8) |
| glass | `Widgets.h` | `makeTouchScrollable(QAbstractItemView*)` and `ToggleSwitch` gated on `isCompactScreen()` - the touch gesture and the 48dp padding were reaching the desktop (§3.63) |
| tests | `test_ui.cpp` | `touchAffordancesStayOnThePhone`, `toggleSwitchPadsOnlyForAThumb` - the first assertions about what the DESKTOP is spared |
| theme | `Theme.h` | `#segment` padding narrowed on compact: four duration pills were 344dp inside a 360dp phone (v30.8) |
| glass | `Widgets.h` | `makePhoneFriendly(QFormLayout*)` — `WrapAllRows`, label above field (v30.8) |
| tests | `test_ui.cpp` | the sideways-overflow question added to both budget gates, plus `crowdedPagesStillFitAPhoneScreen`, `theWeekViewFitsAPhoneScreen`, `theCompareScreenFitsAPhoneScreen`, `thePhoneGetsTheTaskModalNotTheDrawer` (v30.8) |
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
- ~~**Touch density, in the direction that matters.**~~ **Done in v30.7 —
  see §3.54.** The bullet that stood here was right about the gap and wrong
  about its size, and the arithmetic error is worth preserving rather than
  quietly deleting: it said *"at dpr 3.0 a 24px close button is 8 logical px
  of thumb target"*. That divides by the ratio in the wrong direction.
  Nothing anywhere in Qt divides a widget size by the device pixel ratio —
  this document's own measurement table, two hundred lines above, records
  that Qt reports **logical** px on Android at dpr 3.00 on a 360x800 screen,
  which makes one Qt px exactly one dp. A 24px button is **24dp**: half the
  48dp guideline, not one sixth of it. The code always had it right
  (`MainWindow.cpp`'s FAB reasons "56x56 → 168 physical against a 144
  minimum"); only the prose was inverted. Planning off the wrong number
  would have overstated every gap threefold and sent someone hunting for a
  density division that does not happen.
- **`ActivitiesPage` is still a permanent two-column split**, which is the
  wrong shape on a 384 px screen however the width is divided. Narrowing its
  rail is a mitigation and is labelled as one in the code.
- **Landscape.** A width-only class hands a wide arrangement into 384 px of
  *height*. `heightClassFor()` is the deferred answer (§3.41).
- **Nested watchers.** The pruning rule is implemented and documented; nothing
  uses it yet. The two-pane stage will be the first.
