# Design Addendum — Android Port & Compact-Screen Mode

**Status: implemented** (code verified on desktop; APK built on the owner's
machine per `docs/ANDROID.md` — this sandbox has no Android toolchain).
Continues the decision log in `design-doc.md §3`.

**The requirement:** "I'd like to be able to open this application on my
Android phone."

---

## 3.30 Compact mode is decided by GEOMETRY, not platform

*Decision:* one helper, `isCompactScreen()` (shorter screen side < 600
logical px), gates every phone adaptation: rail starts collapsed (the ☰
toggle already existed — compact mode reuses it), tagline hidden, glance
panel hidden, Pomodoro settings stacked, Activities rail narrowed.

*Why not `#ifdef Q_OS_ANDROID`:* the question the layout is asking is "how
much room do I have?", so the code asks about room. A 10-inch Android tablet
gets the desktop layout it can afford; a tiny future Windows tablet gets the
compact one. Platform is the wrong proxy for the actual variable.

*Test hook:* `TICKTIMER_COMPACT=1` forces the answer — the screenshot tool
renders "the phone layout" on a desktop-size virtual display. A layout mode
you can't produce on demand is a layout mode you can't verify.

*What was deliberately NOT hidden:* nothing informational is lost for good.
The glance panel's numbers are all DERIVED (§3.5) and reappear in the
Week/Month reviews; the rail is one tap away.

## 3.31 Touch scrolling: TouchGesture, not LeftMouseButtonGesture

*Decision:* every `QScrollArea` gets
`QScroller::grabGesture(viewport, QScroller::TouchGesture)` via one shared
helper (`makeTouchScrollable`).

*Why the viewport:* it's the widget that actually clips and pans content —
grabbing there makes touch-drags scroll the area while taps fall through to
children as clicks.

*Why NOT LeftMouseButtonGesture:* it would hijack every mouse drag — the
agenda's edge-resize drag would become a scroll on desktop too. TouchGesture
leaves the mouse alone entirely.

*Accepted tradeoff (documented in ANDROID.md):* on a touchscreen, a drag on
the agenda scrolls instead of edge-resizing. Scrolling is the overwhelmingly
more common gesture; blocks are still adjusted through the block dialog's
nudge buttons. UI reachability changed; the domain door (`resizeEvent`)
didn't.

## 3.32 The minimum-width hunt — and the probe that ended it

*The problem:* the window refused to shrink below 550px. A `QStackedWidget`'s
minimum size is the **max over all its pages**, so any single page can hold
the whole window hostage — including pages that aren't visible.

*Decision:* diagnosis became a command. The screenshot tool grew a **layout
probe** (`TICKTIMER_PROBE=1`) that prints the window's `minimumSizeHint` and
every stacked page's contribution. It named the culprits in one run:

| Culprit | Why | Fix |
|---|---|---|
| Agenda subtitle label (~500px) | a QLabel's minimum width IS its text width | `setWordWrap(true)` — with wrap, the label folds instead of dictating |
| Pomodoro settings strip (~510px) | three label+spin pairs in one row | same pairs, `QBoxLayout` direction flips to vertical on compact |
| Month review page (498px) | chart/stat rows | wrapped in a `QScrollArea` — a scroll area's minimum ignores its content (the week tab already used this exact recipe) |

Result: window minimum 550 → **318 px**; verified by rendering at 412×915
(a Pixel-class portrait screen) and OCR-checking the output.

*The reusable lesson:* "the window won't shrink" is always *some* widget's
minimum-size contract. Three different contracts, three different fixes —
word wrap changes a label's contract, layout direction changes a row's,
and a scroll area severs the contract entirely.

*Also in this change:* CMake gained an `if(ANDROID)` block (package name,
version code/name, min SDK — the facts stamped into the APK) and an
`if(NOT ANDROID)` fence around tests/tools (on Android every executable
would otherwise be packaged as its own app). `qt_add_executable`, in place
since day one, is what makes Android packaging possible at all.

## What changed where

| Layer | File(s) | Change |
|---|---|---|
| Build | `CMakeLists.txt` | Android packaging block; NOT-ANDROID fence for tests/tools |
| Shared UI | `Widgets.h` | `isCompactScreen()` (+ test hook), `makeTouchScrollable()` |
| UI | `MainWindow.cpp` | rail starts collapsed on compact; tagline yields |
| UI | `PlannerPage.cpp` | glance hidden on compact; subtitle word-wrap; month scroll-wrapped; touch scrolling |
| UI | `PomodoroPage.cpp` | settings pairs stack on compact |
| UI | `ActivitiesPage / UpcomingPage / SpecialDaysPage` | touch scrolling; compact rail width |
| Tools | `tools/screenshot.cpp` | width/height args; layout probe |
| Docs | `docs/ANDROID.md` | click-by-click build guide |

Domain, storage, tracking, tests: **zero changes** — the layering promise
("domain builds without Widgets") is exactly why a whole new platform cost
only UI and build-recipe edits.
