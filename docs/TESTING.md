# Testing TickTimer by hand — the force recipes

*New in v28.10. The first field report (PROJECT_LOG, v28.2.1) ended on an
omission bigger than any of its five findings: every v28 service ships
injection seams — `setNowProvider`, public `sweep()`s, `TICKTIMER_AI_DOWN` —
and none was reachable from the running app. The owner could not force a
check-in, could not skip a 20-minute sweep, and had never heard the v28.0
fallback voice, because a working provider always wins. **Seams only tests
can reach are half a seam.** This file is the manual for the other half.*

Everything below assumes the debug panel: **Ctrl+Shift+D** in the running
app. It is modeless on purpose — park it beside the window and watch the
app react. Design reasoning lives in `design-addendum-debug.md`; the
diagram is `diagrams/debug_seams.*`.

---

## The panel at a glance

| group | control | seam it presses |
|---|---|---|
| Clock | Pretend it is this moment / Back to real time | `setNowProvider` on affordability + check-in + the chat briefing, all at once |
| Deadline heads-up | Sweep now | `AffordabilityService::sweep()` — skip the 20-minute wait |
| | Forget manners | `AffordabilityService::forgetManners()` — clears last-spoken verdicts + today's cap |
| Morning check-in | Sweep now | `CheckInService::sweep()` — the honest gate |
| | Offer now (skip the gate) | `CheckInService::forceOffer()` — rehearsal; ledger untouched |
| | Clear today's ask | `CheckInService::clearTodaysAsk()` |
| The assistant's context | Show the briefing | `ChatPage::currentBriefing()` — the exact text every chat turn carries |
| AI | All providers down this run | `TICKTIMER_AI_DOWN=*` for this process only |
| Block alarms | Poll now | `AlarmService::poll()` — "has anything come due?", without the timer |
| | Republish | `AlarmService::republish()` — re-derive the window and hand it to the platform |
| | Show the schedule | `AlarmService::schedule()` + `Notifier::deliversSchedule()` / `canSpeak()` |

---

## Recipe: the deadline heads-up (v28.0 / v28.1)

**Goal: see a nudge toast on demand, in both voices.**

1. Give a task a due date 1–2 days out and an estimate bigger than the
   free time you have left today (Upcoming should show its pill).
2. Ctrl+Shift+D → **Sweep now**. No 20-minute wait: if the verdict is
   Tight and manners allow, the toast fires.
3. Nothing? The manners are working, not broken — a nudge only re-speaks
   when a verdict *turns* Tight (Tight→Tight is nagging). Press **Forget
   manners** and sweep again: with last-spoken verdicts cleared, the same
   Tight is news again.
4. Quiet hours and the 3-a-day cap still apply — they are the feature,
   and the panel deliberately has no button that bypasses them. Use the
   fake clock (below) to step outside quiet hours instead.

**The two voices.** With the AI checkbox OFF and a provider configured,
the toast text is the model's phrasing in your persona. Tick **All
providers down this run** and sweep again (after Forget manners): the same
pipeline delivers the v28.0 C++ sentence — the voice that is guaranteed to
work. Before v28.10 that voice was unreachable outside tests, because the
nudge wire never consulted the forcing hook; now every wire does.

## Recipe: the morning check-in (v28.2)

**Goal: the full flow — toast, tap, chat opens with the question waiting.**

The honest gate needs three things at once: 06:00–11:00, a computably
heavy day, and no ask spent today. Two ways in:

- **Rehearsal (any time):** **Offer now (skip the gate)**. Same toast,
  same tap-through into the Assistant page, and the ledger is *not*
  marked — the real morning's one ask survives your rehearsal.
- **The honest path:** set the clock to a morning (e.g. tomorrow 08:30)
  with **Pretend it is this moment**, make the day heavy (≥5h of blocks,
  or 2 tasks due within 2 days), press **Clear today's ask** if you
  already spent it, then **Sweep now**. This exercises the real gate —
  the thing the rehearsal deliberately skips.

## Recipe: the briefing (what the model is told)

**Goal: debug the assistant by reading its inputs, not its outputs.**

Ctrl+Shift+D → **Show the briefing**. This is the exact context block
every chat turn carries — day status, today's and tomorrow's plan, tracked
totals, unresolved blocks, deadline pressure, mood (local seats only).

The field report's pattern, worth internalising: every wrong assistant
answer traced to a fact this text failed to state — never to the model,
never to the prompt. When the assistant is wrong, read this first and ask
"what did we fail to tell it?". The viewer fetches fresh text per press,
so edit a block, press again, and watch the fact change.

## Recipe: every fallback at once (`TICKTIMER_AI_DOWN`)

Two forms, checked per call (never cached):

- `TICKTIMER_AI_DOWN=anthropic,ollama` — the named seats are treated as
  unreachable before any socket opens (the original §E hook).
- `TICKTIMER_AI_DOWN=*` — every seat, present and future (v28.10; this is
  what the panel's checkbox sets).

What each surface does when its seat is down — three wires, three
personalities, all deliberate:

| surface | behaviour when forced down |
|---|---|
| chat | walks the route past the seat; ⚠ bubble if the whole route is down |
| nudge | silent `fallback()` → the C++ sentence (nobody pressed anything, so no error UX) |
| quick-add | visible bar: "*seat* is forced down (TICKTIMER_AI_DOWN)" (someone is waiting, so the cause gets named) |

The checkbox sets the wildcard for **this process only** and unchecking
clears the variable entirely — a debug state that survives a restart is a
support ticket. Set the env var before launch instead when you want it to
apply from the first request (the 30-second startup sweeps).

## Recipe: the fake clock, precisely

**Pretend it is this moment** freezes `now` for the affordability service,
the check-in service, and the chat briefing — every sweep and every
briefing judges that exact instant until **Back to real time**. Frozen,
not offset, on purpose: reproducing a bug wants every look to see the same
moment, not a moving target.

What it does *not* touch: the tracker, the Pomodoro, block alarms, the
midnight roll, and the agenda's painted now-line all stay on the wall
clock. The panel's group title names its actual reach; widening it is a
per-service decision for whenever a recipe needs it, not a default.

## Recipe: the write boundary (v29.0)

**Goal: watch a proposal become a change — only via your tap.**

1. Capture a task with no estimate (Ctrl+N, e.g. "essay draft #school").
2. Ctrl+Shift+D → **Inject sample proposal** (The write boundary group).
   The status line names the target; the card is waiting in the
   Assistant chat: "Proposed change — Set 'essay draft' — estimate
   1h 30m, due …", Apply / Discard.
3. **Discard** first, if you like: the card settles, nothing changes,
   nothing enters the record. Inject again.
4. **Apply**: the estimate lands (check the task's detail panel), a
   "✓ Applied: …" receipt joins the transcript, and
   `data.json.pre-apply` now sits beside `data.json` — the state from
   just before the tap.
5. The stale-card scene, worth doing once: inject, then fill the
   estimate BY HAND in the detail panel, then tap Apply on the old
   card — it refuses with a reason and your by-hand value survives.
   That is `apply()` re-validating at the tap, live.
6. The boundary's other face: every proposal here runs as the Intake
   role. Nudge and check-in hold EMPTY verb lists — there is no recipe
   for making a toast rearrange your afternoon, and that absence is the
   feature.

## Recipe: the intake interview (v29.1)

**Goal: watch the queue gain a voice — with and without a model.**

1. Capture 2–3 tasks with no estimate; give at least one a due date
   (that's what makes it interview-worthy — §K.6's triage).
2. Ctrl+Shift+D → **Start intake interview**. The question appears in
   the Assistant chat; if you have finished, tracked tasks in that
   category, a "≈ 2h sounds right" guess button appears with it.
3. Answer three different ways across tasks:
   - **Tap the guess** — it still crosses the card (every write does).
   - **Type "2h"** — parsed in C++, instantly, no network: the card
     appears with no model involved.
   - **Type prose** ("big lab report, probably two evenings") — the
     model extracts; the card shows what it read. Discard if it read
     wrong; a discard is not a skip.
4. **Skip this one** on any task: dismissed for a year, never re-asked —
   run the interview again and it's gone from the rotation.
5. The no-model proof: tick **All providers down**, interview again,
   type "90 min" — everything works; type prose — the honest hint
   names the path that always works.
6. The designed entry: force a check-in (Offer now), answer the mood,
   and the interview offer follows — §K.1's moment, never at capture.

## Recipe: block alarms, and whether the phone will actually ring (v30.6)

**Goal: see what the OS is holding, without waiting for 09:00.**

The alarm group runs on the **wall clock**. The fake clock above does not
reach it, and that is not an oversight: `AlarmService` reads its clock in the
constructor (the high-water mark is born at "now"), so there is no
now-provider left to rewire afterwards. The panel says so rather than
offering a control that would quietly do nothing.

1. Plan a block a few minutes out. Ctrl+Shift+D → **Block alarms** →
   **Show the schedule**.
2. Read the **first line** before anything else. *Held by THIS PROCESS* means
   the app's own timer is the only thing that will ring — correct on a
   desktop, and the entire bug on a phone. *Held by the PLATFORM* means
   Android has it and will ring with the app dead.
3. The second line says whether the app is allowed to post notifications at
   all. On Android a **NO** here means every alarm below is dropped in
   silence — the exact failure v30.6 was written to end.
4. **Poll now** asks whether anything came due, without waiting out the
   timer. On a desktop that fires the toast; on a phone it deliberately does
   nothing, because the OS already owns that moment and speaking here too
   would deliver it twice.
5. The scene worth doing once: **start tracking** the block you planned, then
   press **Show the schedule** again. Its *start* alarm is gone and an *end*
   alarm has taken its place. That is the own-hands rule surviving
   out-of-process — the schedule was republished without it, which is the
   only way a mute can work when the app is closed. Stop tracking and it
   comes back.

**On the phone itself**, the same question from the other side:

```sh
adb shell dumpsys alarm | grep -i ticktimer
```

What that prints should match what the panel showed. If the panel lists
alarms and `dumpsys` lists none, the publish never reached the OS. The
reboot check is the one nothing else can substitute for: `adb reboot`, wait,
then run it again — Android discards scheduled alarms on restart, and only
`BootReceiver` puts them back.

---

## The automated suites (unchanged home)

`ctest` in the build directory runs all six: domain, taskmodel, nlp, ui,
auth, login_live — **379 tests, all green at v29.1.0**. That is the sum of
the suites' own `Totals:` lines, which count each class's
`initTestCase`/`cleanupTestCase` as cases; there are 367 test *functions*,
and `ctest`'s summary says 6, because it counts suites.
`docs/RUNNING.md` covers building; `tests/README.md` covers what lives in
which suite and why — including the command behind each of those figures. The
rule that keeps this file honest: anything a recipe above presses is also
pinned by a test — the panel is glass over tested seams, never a second
implementation of them.
