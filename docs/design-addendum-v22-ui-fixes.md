# Design addendum — v22 UI fixes

Four owner-reported bugs, fixed in one pass. They share a theme worth naming:
**every one of them is a widget that was allowed to decide something it had no
business deciding** — its own height, the only gesture it accepts, its own
width cap, its own default.

---

## §1 The needs-a-block card could make the window unresizable

> **Revised in v22.1** — see the section at the bottom. The scroll wrapper
> survived; the fixed ceiling and the "+N more" fold did not.

> *"The needs-a-block gets extremely long if I have a lot of tasks due. It can
> get so big the window becomes unresizable."*

### Root cause

`NeedsBlockCard::rebuild()` appended one row widget per qualifying task
directly into a `QVBoxLayout`. A layout reports the **sum of its children** as
its `minimumSizeHint`, that number propagates up through `GlancePanel` to the
`QMainWindow`, and **Qt will not let you shrink a window below its minimum.**
Twenty due tasks therefore did not merely look bad — they took the window
hostage.

This is worth internalising because it is not a Qt quirk, it is Qt working as
designed: a layout's job is to ask for enough room for its contents. The bug
is a widget with *unbounded* contents being asked that question.

### Fix — two independent guards

**1. A row cap (the calm fix).** The gate shows at most `kMaxRows = 4` tasks
and folds the rest behind `+N more waiting ▼`. The list arrives ranked by
`coverage::rankAt` — pinned, then overdue, then urgent, then the rest — so the
four you see are the four worth reading first. Truncating an *unranked* list
would be arbitrary; truncating a ranked one is **editing**. A review surface
that opens with twenty rows is not a review, it is a wall, and the user this
app exists for is the one least able to face a wall.

**2. A height ceiling (the structural fix).** The rows now live inside a
`QScrollArea` capped at `kMaxBodyPx = 280`. A scroll area's minimum is small
and **constant regardless of content**, so the card can never dictate window
size again. Expanding `+N more` scrolls; it never grows the window.

Both, not either. The cap alone would still break if someone raised
`kMaxRows`; the scroll area alone would leave a wall of rows behind a
scrollbar.

### The trap in the structural fix

```cpp
m_scroll->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
```

Without this line the fix silently does nothing useful. `QScrollArea`'s
default policy is `AdjustIgnored`, whose `sizeHint()` is a **hardcoded
256×192** — a one-row card would reserve six rows of space at every window
size. `AdjustToContents` makes the hint follow the content, so the card
shrinks to fit and only begins scrolling at the ceiling.

No rebuild code had to learn about any of this: `m_layout` still points at a
`QVBoxLayout` and widgets created with `this` as parent are reparented into
the scrolled body automatically when `addWidget()` takes them. **The bound is
structural, so it cost the row-building code nothing.**

---

## §2 No way back to today

> *"We should be able to right-click to go straight to today. Otherwise we
> lose ourselves in the timeline."*

`QPushButton` has no `rightClicked` signal. Qt routes secondary clicks through
the **context-menu channel**, so the idiomatic way to give a button a second
verb — without subclassing it purely to override `mousePressEvent` — is:

```cpp
m_viewSwitcher->setContextMenuPolicy(Qt::CustomContextMenu);
connect(m_viewSwitcher, &QWidget::customContextMenuRequested,
        this, &PlannerPage::goToToday);
```

Bonus: the same signal fires for the keyboard's Menu key and for a long-press
on touch, so the shortcut is reachable without a mouse.

**`goToToday()` does not change the view mode.** You asked to come home, not
to be moved to a different room — right-clicking in Month view lands on this
month, in Day view on today. A gesture that means one thing everywhere is safe
to reach for without looking.

### The refactor it forced

`shiftPeriod()` used to push the new date into five sub-views and rebuild the
due strip *inline*. A second navigation gesture would have had to duplicate
that sequence — and one day forget a line of it. Extracted:

```cpp
void PlannerPage::applyDate();   // every navigation gesture ends here
```

`goToToday()` therefore cost **zero new update logic**, which is the entire
return on the extraction.

---

## §3 The Upcoming page was cramped inside an oversized window

> **Revised in v22.1** — see the section at the bottom. The count chips and
> the bucket lens were removed after owner review.

> *"The screen that displays the upcoming and unfinished tasks is too small.
> There's a lot of space; we can refine this quite a bit to make it clearer
> and more inviting."*

Three changes, none of which touch the model/view pipeline underneath:

**Framing.** The panel's hard `720px` cap became `1040`, and the list now takes
the layout's vertical stretch (`addWidget(m_stack, 1)`, `addWidget(wrapLeft(panel), 1)`).
The cap still earns its place — an unbounded line length is genuinely harder to
read — it just sits where text stops benefiting rather than where the old
narrow rows needed it. Cards grew from 62px to 72px, and their width cap from
680 to 900.

**A count strip.** `Overdue · This week · Later`, with live counts, above the
priority lenses. The numbers answer *"how bad is it?"* before you read a single
row, and each chip is **also a filter** (a new bucket lens on
`TaskFilterProxy`), so the summary and the navigation are the same control.

Two decisions inside it worth flagging:

- Counts come from the **source** model, not the proxy. A summary that changes
  when you filter is not a summary — the numbers must stay a fixed landmark
  while you move between lenses.
- The chips live in their **own container widget**, and that is not cosmetic.
  `QToolButton::autoExclusive` groups by *parent widget*; leaving the chips on
  the same parent as the priority buttons would have made all seven one radio
  group, so picking "Urgent" would silently un-pick "Overdue". A separate
  parent gives each row its own group — the cheapest possible fix, and the
  only reason that container exists.

Exclusivity within the strip is done by hand (three lines) rather than with
`autoExclusive`, because clicking an already-active chip must **clear** it, and
`autoExclusive` refuses to un-check.

**A warmer empty state.** Three silences now get three answers: an empty
bucket, an empty priority lens, and a genuinely clear horizon. The last is
*good news* and the copy says so — the app's non-shaming rule applies to praise
as much as to blame. Centred and given a headline, because a top-left grey
sentence in a large panel reads as an error message.

---

## §4 The Pomodoro link was off by default

> *"The link checkbox to the agenda widget should be defaulted."*

One line, in `prefs::pomodoroDrivesTracker()`: the fallback is now `true`.

Worth understanding what changing a **default** does, because it is why this
needs no migration code: `QSettings` consults the default *only when the key is
absent*. So this flips the switch for new installs and for anyone who never
touched the checkbox, and **silently respects the choice of anyone who did.**
That asymmetry is exactly the behaviour you want, and it is free.

`PomodoroLink::m_enabled` still initialises to `false`. The link never reads
preferences — pages read, widgets are told — so the shipped default lives in
`prefs::` and this object's job remains to obey, not to have an opinion.


---

## v22.1 — the owner review round

v22 shipped, the owner used it, and two of the four fixes came back. Both
corrections teach more than the originals did.

### §1 revisited: the card was still deciding its own height

The v22 card arrived **squashed to two lines over an empty panel**. Two causes
stacked: `QScrollArea::sizeHint()` is a *cached guess* about its content (only
re-read on a `LayoutRequest`, which a `deleteLater`-heavy rebuild can defer
past the next paint), and the fixed 280px ceiling was wrong even when the hint
worked — the panel had hundreds of free pixels the card wasn't allowed to use.

The deeper mistake: **the card was still trying to size itself.** v22.1 moves
the decision to the one component that can see the whole panel:

```cpp
// GlancePanel::refresh()
lay->setStretchFactor(m_needsBlock, gateClosed ? 1 : 0);
lay->setStretchFactor(m_dayContent, gateClosed ? 0 : 1);
```

Whoever is on stage gets the room. Gate closed → the card *is* the panel (§E
said so all along) and fills it; gate open → the day content takes the stretch
back and the card sizes to its strip. Stretch factors **claim** space; size
hints only **suggest** it — that distinction is the whole bug.

What survived from v22: the `QScrollArea` (its small, constant *minimum* is
the actual cure for the frozen window) and the explicit
`updateGeometry()` nudge after each rebuild (closes the stale-hint timing
hole). What died: the 280px ceiling, and the `+N more` fold — with the card
owning the panel's height, hiding rows protected a constraint that no longer
existed. A bound should live where the constraint lives (space), not be
duplicated as UI state.

### §3 revisited: "too small" is fixed with size, not with controls

The count chips were the wrong answer to the right complaint. The owner:
*"still too small to read, but now there's other buttons that aren't really
needed."* v22.1:

* chips removed — **and the proxy's bucket lens with them**. Features are
  hypotheses; the owner's read is the experiment result, and machinery left
  behind a removed feature is how codebases rot.
* the panel's width cap removed entirely. Line-length restraint now lives in
  exactly one place: the delegate's `kMaxCardW` (1100), which caps the
  *cards* while the panel uses whatever window it was given. Two caps
  fighting over the same pixels was the old arrangement's quiet bug.
* a full type-size step: title 15→17px, subtitle 12→14, countdown 13→15,
  card 72→86px. The countdown and chip fonts are bumped **in both the
  geometry pass and the paint pass** — they measure and draw the same string,
  and a mismatch overruns the delete button.

The general lesson, worth keeping: when a user says a screen is *too small*,
the fix is bigger text and more room for the content they named. Adding
controls — however clever — makes the screen busier, which reads as smaller.


---

## v22.2 — "Show my day does nothing"

The best bug of the batch, because every individual piece was working as
designed.

**The chain.** While a timer runs, `TrackerService::tick` fires every second →
`GlancePanel::refresh()` → `NeedsBlockCard::refresh()` → `rebuild()`,
*unconditionally*. `rebuild()` destroys and recreates every widget in the
card — including "Show my day". A real click is press → ~100ms → release,
and Qt delivers the release to the widget that took the press. When a rebuild
lands inside that window, the button under the cursor is `deleteLater()`'d
mid-click; the release arrives at a corpse and `clicked()` never fires.

**Why the tests never saw it.** The suite drives the button with
`open->click()`, which invokes the handler directly — no input delivery, no
press/release window, no race. A programmatic click proves the *handler*
works; it proves nothing about whether a human can reach it.

**Why it surfaced now.** The per-second rebuild has existed since the card was
born. v22 flipped `pomodoroDrivesTracker` to default **on** — the tracker now
runs whenever the Pomodoro does, so the rebuild storm is active precisely when
the user is looking at the panel. A defensible change made a dormant bug live:
defaults have blast radius beyond their own feature.

**The fix: a fingerprint gate.** `refresh()` re-derives everything the card
renders — the gate inputs, each listed task's visible fields, the put-off
set, the card's own expansion state — flattens it to one string, and rebuilds
only when the string changed:

```cpp
const QString print = fingerprint(now);
if (print == m_lastPrint)
    return;      // pixel-identical — keep the widgets, keep the click
m_lastPrint = print;
rebuild(now);
```

Note the reconciliation with the "derive, don't store" doctrine: the
derivations still run on every call, so nothing can go stale. What's cached
is the *rendering decision*, not the data — the same pact
`TaskListModel::rolesEqual` keeps with its delegate ("no repaint for an
invisible change"), applied to a rebuild-style widget. The maintenance pact is
stated in the code: any new visible field in `makeTaskRow` must join the
fingerprint, or edits to it stop repainting.

Free upgrade: because the print includes the put-off set derived against
`now`, the per-second ticks now bring a dismissed task back the very second
its timer lapses.

**Also removed:** `makeTouchScrollable` on the card's inner scroll area.
Grabbing the viewport's touch gesture makes QScroller intercept presses on
every child, and on the week tab this card sits *inside* the already-grabbed
`weekScroll` — nested grabbed scrollers fight over the same press. The card
is buttons-first; wheel and scroll bar still work.

**The regression test** uses `QPointer` as its witness: it nulls itself the
moment its widget dies, so "still non-null after five refreshes" *is* the
property under test — widget identity across refreshes, which a programmatic
click could never check.
