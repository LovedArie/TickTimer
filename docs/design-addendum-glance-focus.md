# Design addendum — the glance panel's focus gate (v22.4, design A)

> Owner picked option A from four prototypes: *one task, big*.

## §A What changed

The gate used to list every task that needed a block. It now presents **one** —
the top-ranked one — in a hero density: bigger title, an accent rail, a due
line, a why-line, and two full-width actions. The rest are counted in the
header (`1 of 5`) and one click away.

```
Needs a block                    1 of 5 ▾
First, the one most worth your next hour.

┃ Give Leo his bed frame
┃ Sat 18 Jul · 1 day overdue · Social
┃ ⚠ Time was set aside — it came and went
┃ [  Find time  ] [  Not today  ]

4 more after this one

[      Show my day →      ]
```

## §B Why one

The same argument that justifies the gate existing at all. **A list of five
overdue things is a status report; one thing with two buttons is a decision.**
This app is built for someone who stalls in front of the status report — so the
panel asks for one decision, then offers the next.

It only works because the list is already ranked: `coverage::rankAt` orders by
pinned → overdue → urgent → rest, so "the top one" is a real answer rather than
an arbitrary pick. Focus mode is renting credibility from that ranking; if the
ranking were arbitrary, hiding the rest would be a lie.

## §C The counter is the escape hatch

`1 of 5 ▾` is a **button** when there is more than one task and an inert badge
when there isn't. A control that appears exactly when it becomes meaningful
teaches itself — no tooltip, no settings entry.

Expanding gives the old list back, built by the same function at compact
density. So the two modes cannot disagree about a task: same facts, same
escalation rules, same decision menu, same signals.

### This is not the v22 fold returning

v22 had a `+N more` fold and v22.1 deleted it. The widget looks similar; the
motivation is opposite, and the difference is the whole point:

| | v22 fold | v22.4 counter |
|---|---|---|
| Why it existed | To protect a fixed 280px ceiling | Because one decision beats five |
| What it was | A layout bound duplicated as UI state | The feature itself |
| Correct fix | Delete it, bound the layout properly | Keep it |

A bound belongs where the constraint lives. v22's constraint was space, so the
fix was structural (the panel's stretch). v22.4's constraint is *attention*,
which genuinely is a UI concern.

## §D One builder, two densities

`makeTaskRow(task, rung, now, bool focus)` — a parameter, not a second
function. The facts, the escalation rules, the decision menu and the emitted
signals are identical; only padding, title size, the dot-versus-rail choice and
the button stretch differ.

Forking it would have been easier to write and worse to own: the day someone
adds a field to one copy and not the other is the day the two presentations
start lying about the same task.

### The accent rail

Danger red when the task is escalated *or* already overdue, otherwise the
task's own category colour. The rail is not decoration — it is the same fact
the meta line spells out, arriving a beat earlier. Overdue detection uses
`Task::isOverdue(QDateTime)`, the clock-aware overload from v22, so a task due
today at 09:00 turns red at 09:01.

In focus density the category dot is dropped: the rail already carries that
colour, and saying it twice costs the title its room.

## §E State hygiene

`m_showAll` self-clears when the list drops to one task. Otherwise the counter
would reappear pre-expanded the next time the list grew — state that outlives
its meaning, the same failure `m_decisionFor` guards against two lines below
it. It also joins `fingerprint()`, because it changes what is rendered.

---

## §F v22.9 — the strips become a drawer

> Owner: *"it would be good if it's two buttons that open a side screen."*

The strip state's two inline accordions are gone. In their place: **two
compact chips sharing one row** — `3 need a block` and `4 put off · 21:00` —
each opening a **slide-over drawer** (`SlidePanel`, new reusable widget) that
covers the glance column with the full list and its usual actions.

### Why a drawer beats the accordion it replaces

An accordion answers "show me more" by *shoving everything below it*: the
layout jumps, visual anchors move, and two accordions stacked turn the panel
into a pogo stick. A drawer answers by *layering*: the list appears on top,
the panel behind never reflows, and closing restores exactly the picture you
left. **Expansion becomes a navigation, not a mutation of the page.**

### The construction (there is no QDrawer)

Qt Widgets has no drawer, so `SlidePanel` builds the standard one: the widget
itself is a translucent scrim covering its **host** (the glance panel — the
card's parent, so the sheet spans the column, not the card's rectangle), with
a framed sheet pinned to the right edge, animated on **position** — never
size, which would relayout the rows every frame. Scrim tap, ✕, and Esc all
close it through one `closed()` signal, so "is it open" stays one piece of
state. The sheet's rows sit in a scroll area from birth — v22's
frozen-window lesson applied preemptively rather than retrofitted.

### What stayed the same on purpose

- **The objectNames.** The chips still answer to `needsBlockStrip` and
  `putOffStrip`, so every existing test — and every user's muscle memory —
  finds "the strip" where it always was. The handle's meaning survived its
  mechanism.
- **The rows.** The drawer calls the same `makeTaskRow` and emits the same
  signals. Acting inside it (plan, dismiss, bring back) flows through
  `changed()` → rebuild → `fillDrawer()` in rebuild's tail, so the open
  drawer updates in place with zero drawer-specific plumbing — and closes
  itself when its list empties.
- **The state discipline.** One `int m_drawerMode` (0/1/2) replaced two
  booleans, joins the fingerprint, and self-clears when its list vanishes.
