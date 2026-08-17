# Design addendum — affordability, the proactive pipeline with no model in it (v28.0)

*Assistant roadmap §H, shipped domain-first per §H.6 — and §F's nudge
pipeline arrived with it, because of a decision recorded below. The model
joins in 28.1; everything here already works without one.*

## §0 — What shipped

One pure header, one thin service, one pill, four toast seams:

- **`afford::affordability(data, task, now)`** → a `Report`: verdict
  (NotApplicable / Unknown / Comfortable / Tight) plus every number behind
  it. Pure; the clock is a parameter; thresholds live in `afford::Rule`.
- **`afford::decide(...)`** — the manners gate, also pure: change-of-verdict,
  quiet hours, daily cap, dismissal respect.
- **`afford::sentence(report)`** — the 28.0 voice, plain C++ formatting.
- **`AffordabilityService`** — a QObject that is deliberately dumb: a
  20-minute timer, a 30-second post-change debounce, a loop, a signal.
- **A `TIGHT` pill** on Upcoming cards (`AffordabilityRole`).
- **`ToastSpec`** — the toast as a value, with kind-driven accent, one
  `moveTo()` movement seam, and an optional action row.

## §A — The decision that reshaped the slice: volunteer-mode (§O.1 closed)

§O.1 asked: *does the assistant volunteer an affordability verdict, or only
answer when asked?* The owner closed it: **volunteer** — *"I want it to be
my secretary and assistant… give me a good heads up on things."*

That decision has a structural consequence: answer-only affordability is a
query; **volunteering makes it a nudge**, which needs a trigger, a surface,
and manners. So §F's pipeline moved into this slice — and the slice was
re-cut so that the *first* version of that pipeline contains **no model at
all**. Trigger → verdict → manners → C++ sentence → toast. 28.1 swaps one
box: the sentence-writer.

Why build the fallback first (§A corollary made structural): the model is
an enhancement layer, never a dependency. Shipping the no-model path as
v28.0 doesn't just respect that rule — it makes violating it impossible,
because the degraded mode isn't a mode. It's the thing that shipped.

## §B — The verdict, exactly

Four values, not a score — the inputs have ~half-hour precision and a
score would pretend otherwise.

```
NotApplicable   no deadline / done / archived — the question has no meaning
Unknown         deadline, but NO blocks were ever planned (§H.3's honesty)
Comfortable     everything else
Tight           cramped OR last-call OR slipping
```

> *v28.4 update (§J.2 shipped):* the sizing line below is now the
> **fallback**. When the task has an estimate — its own, or the sum of
> its pieces' — outstanding = **estimate × personalMultiplier − tracked**
> (the rate: median of actual÷estimate over the user's finished, tracked,
> estimated work; 1.0 under 3 samples; clamped [0.5, 3.0]; derived, never
> stored). **Unknown has shrunk** accordingly: it now requires no
> estimate AND no blocks. Everything else in this file — the bands, the
> manners, the service — is unchanged; only "what is the work" grew a
> better first answer. Diagram: `diagrams/affordability_sources.*`.

With, over the task's own blocks (the pre-28.4 proxy, kept as fallback):

```
outstanding = plannedAll − tracked          (the §H.3 proxy: their OWN plan)
capacity    = plannedAhead + freeDaytime    (06:00–22:00, minus every block)

cramped   = outstanding > capacity
last-call = daysLeft ≤ 1  and outstanding > 0
slipping  = behindOwnPlan and daysLeft ≤ 3
behindOwnPlan = tracked < 0.5 × plannedPast
```

Three deliberate shapes inside that:

- **A block straddling `now` splits.** Its elapsed part counts as past, the
  rest as ahead — a 2h block you're 30 minutes into hasn't "failed" its
  remaining 90.
- **Free time is exact, not estimated.** Busy minutes are a plain sum only
  because `AppData::isFree` guarantees blocks never overlap. A domain
  invariant paying rent in a consumer it never met.
- **`slipping` needs both halves.** Skipped blocks with the deadline two
  weeks out stays Comfortable — being behind on Monday for a due-in-14-days
  task is a Tuesday problem, not a toast. The tests pin this pair
  explicitly.

## §C — Unknown is a verdict, not a failure

No blocks ever planned → the app doesn't know how big the work is, and the
sentence says so: *"3h tracked and it's due in 2 days — no blocks were ever
planned for it, so I can't tell how much is left."*

And **Unknown never toasts.** The honest sentence exists (chat can serve
it in 28.1), but "I don't know" as an interruption is noise. The verdict
that volunteers is news of trouble, only.

## §D — The manners gate (§F.3, load-bearing)

For an ADHD-facing tool, *muted = total feature failure*, so these are the
feature:

| rule | mechanics |
|---|---|
| **Change-of-verdict** | Comfortable→Tight speaks; Tight→Tight is silence. A secretary mentions Thursday looks tight; they don't mention it four times |
| **Quiet hours** | 22:00–08:00, wrap-aware. No verdicts at 23:40 |
| **Daily cap** | 3, a hard integer |
| **Dismissal respect** | §H.5 — `dismissedUntil` already means "not now"; a nudge that ignores it teaches the owner the snooze is fake |

**The re-arm rule is the subtle one.** The service stores the last verdict
*only* when it spoke, or when the verdict is un-Tight (silent re-arm). A
Tight suppressed by quiet hours or the cap stores **nothing** — the
heads-up was never delivered, so it's still owed, and the next sweep
outside the suppression gets to say it. Recording it would mark news as
old before anyone heard it.

## §E — Where the bookkeeping lives, and why it isn't data.json

"What did I last say, and how many times today" is **manners state, not a
fact about the user's life**. Losing it costs at worst one repeated
heads-up. Syncing it would be actively wrong: a nudge shown on the laptop
should not mute the phone the owner is actually holding — each device
keeps its own courtesy ledger.

So: QSettings, under one `afford/` prefix (a future "reset the assistant's
memory" is one `remove()`), and **v28.0 needs no format bump** — which
also keeps this slice entirely clear of the v11 numbering collision the
audit recorded. Facts go in the data file; courtesies stay local.

## §F — The service is dumb on purpose

`AffordabilityService` owns: a 20-minute timer (verdicts move at calendar
speed), a 30-second debounce after `changed()` (a half-built plan — three
blocks placed, two to go — is exactly when "Tight" would be
wrong-and-alarming), and a loop that calls the two pure functions. It
emits a signal; MainWindow owns the toast. Every judgment is reachable by
a test with no clock, no widget, no settings.

The signal carries `taskId` unused — the §G.1 seam, for when tapping a
toast has somewhere to go.

## §G — The pill

Upcoming cards paint `TIGHT` under the countdown — the card reads "when is
it due → how bad is that". Only Tight gets a pill: Comfortable is the
default state of the world, and Unknown's sentence needs more room than a
pill has.

The plumbing detail worth its comment: verdicts can change when *events*
change while the Task rows are byte-identical, which the snapshot base's
Task-field diff cannot see. `TaskListModel::refresh()` therefore diffs a
verdict map itself and emits the `dataChanged({AffordabilityRole})` the
base cannot know to.

## §H — The toast seams (joints, not motion)

Four seams for the owner's stated "distant future: animate it":

- **`ToastSpec`** — the toast as a value (the `ai::Provider` doctrine);
- **kind-driven accent** — Alert borrows the danger red; same bar,
  different voice;
- **`moveTo(QPoint)`** — every position a toast ever takes goes through one
  method whose body is one `move()` call today and can grow a
  `QPropertyAnimation` without a second call site learning about it;
- **the action row** — empty text = exactly the old toast; first consumer
  is §G.1's check-in.

No animation was written. YAGNI is right about the motion; it is wrong
about the joints, which cost nothing now and are the whole difference
between animation being an afternoon and being a refactor.

## §I — Non-goals, stated so they read as decisions

- **No Settings page for the thresholds.** `afford::Rule` is
  Settings-shaped on purpose, but knobs ship when someone wants to turn
  them (the v26.8 lesson: the intimidation knob protected no one).
- **No "Comfortable again" relief toast.** Tempting, doubles the
  interruptions. Revisit only if the owner asks.
- **No verdict in the day briefing yet.** That's 28.1's job — the model
  gets the `Report`, not the sentence.
- **It informs; it never forbids (§H.4).** Every sentence ends the
  decision with the owner: *"Your call."*

## §J — Tests

Ten new domain cases, all pure: the four verdicts, the straddle split, the
tracked-focus pay-down, and each manners rule in isolation — including the
midnight wrap at 23:40/07:30/08:00-sharp and the dismissal lapse at 18:01.
Per the V170 lesson, every fixture pins its own dates; nothing straddles a
default.


---

## §K — 28.1: the model phrases; the pipeline doesn't notice (v28.1)

The swap the whole slice was cut for. Everything up to the sentence is
byte-identical; `deliver()` is new and is the **one exit** for a speaking
nudge — model text and the C++ sentence both leave through it, so cap bump
and verdict store cannot diverge between voices.

**One seat, no route walk.** The nudge reads `ai::configured()` directly,
exactly as quick-add does, and `Feature::Nudge` is *deliberately absent*
from the routing enum (the absence is documented at the enum): a route
table for a feature whose fallback seat is `afford::sentence()` is
configuration surface with no failure to configure away.

**The client cannot fail loudly — that's its personality.** Quick-add
fails into a visible preview bar; chat fails into a ⚠ bubble; a nudge
fires when nobody is watching, so *every* failure — no key, unreachable,
timeout, 401, essay, emptiness — collapses into one `fallback()` signal
and the owner never learns a network call existed. The transfer timeout
(8 s) *is* the exactly-once timeout: Qt aborts, `finished()` fires, one
handler, one outcome. Supersede-by-silence via the generation counter,
same as quick-add.

**The acceptance gate judges shape, never vibes.** Strip markdown,
collapse whitespace; reject empty and reject essays (> 240 chars) rather
than truncating — a cut-off sentence puts *our* ellipsis in *its* mouth.
Tone rules (inform-never-forbid §H.4, the non-shaming floor) live in the
locked prompt bands, above the persona, restated verbatim because this
prompt travels alone. Machine-judging tone would be a second model call
to check the first; the fallback carries the guarantee instead.

**Bookkeeping moved to delivery** — the round trip takes seconds, and a
cap written before the toast exists would count speech that might never
happen. The one honesty cost, recorded: quiet hours are checked at
decision time, so a request fired at 21:59:58 can deliver at 22:00:04.
Two seconds of tardiness beats a re-check that silently swallows an owed
heads-up.

**The ask-side shipped too.** The day briefing now carries a `DEADLINE
PRESSURE` section — per-task verdict with its numbers, Comfortable stated
rather than omitted (the model needs "you're fine" as a citable fact, not
an absence to guess about), Unknown honest. §H.1's founding question —
*"I want to go out, can I?"* — is now answerable in chat from the same
derivation the toast uses.

**Persona is injected, not read.** The service takes a
`std::function<QString()>`; MainWindow wires `chat::configuredPersonaBand`.
Not ceremony — the direct include would have dragged ChatSession and Qt
Network into `DOMAIN_SOURCES`, undoing the test-target split the CMake
file celebrates. One assistant, one voice, zero new link edges.

**Deliberate cuts:** no settings toggle for phrasing (≤ 3 tiny unattended
calls/day; the C++ voice is one unreachable-provider away; promotion
trigger: the owner asks). No streaming, no retry — a nudge about a stale
verdict is worse than a plain one now.
