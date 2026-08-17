# Design Addendum — The Debug Seams (v28.10)

*Companion to `docs/TESTING.md` (the recipes) and `diagrams/debug_seams.*`
(the map). This file is the reasoning. Born whole from one line of the
first field report: "seams only tests can reach are half a seam."*

---

## A. The doctrine, stated once

Every v28 service was built testable — clocks injected, sweeps public,
providers overridable, a kill-switch env var. The field report proved that
was only half the job: **the owner debugging the running app has the same
needs as a test**, and had none of the reach. The check-in demanded a
morning and a heavy day; the sweep demanded 20 minutes of patience; the
guaranteed fallback voice had never once been heard, because a working
provider always wins and no hand could unplug it.

The fix is one panel (Ctrl+Shift+D) with a single design rule:

> **The panel is glass.** Every control presses a public method the test
> suite already calls, or flips an environment variable the test suite
> already flips. The moment a button seems to need new judgement, that
> judgement moves into the service — behind a test — first, and the panel
> gets to press it afterwards.

That rule is why this slice's diff is mostly *plumbing made reachable*
rather than *behaviour added*: `forceOffer()` and the two manners resets
are the only new service surface, and each is a thin, named door over
state the service already owned.

## B. A chord, not a menu bar

The app's chrome is a nav rail; it has never had a QMenuBar. Growing one
to host a single developer entry would change every user's window for the
benefit of one debugging session — the wrong trade for an ADHD-facing
tool whose chrome discipline is a feature. A keyboard chord costs nothing
to the people who don't know it and one line of TESTING.md to the people
who do. (Compare TICKTIMER_COMPACT and TICKTIMER_AI_DOWN: the project's
debug affordances have always been invisible-until-invoked.)

## C. The rehearsal decision — forceOffer spends nothing

`forceOffer()` skips the *gate* but not the *script*: same body text, same
signal, same tap-through into the chat. It deliberately does **not** mark
`checkin/lastOffered`. The ledger's promise is "ask once per real
morning", and a rehearsal is not an ask — marking it would mean pressing
the debug button at 15:00 silently cancels tomorrow's real 08:30 knock,
which is precisely the kind of surprise a debug tool must never produce.
The mirror decision: `sweep()` still marks at emit, unchanged — the
honest path's bookkeeping was correct and stays untouched.

## D. Ownership of the resets

The panel could have called `QSettings().remove("afford")` itself — the
prefix is even documented in a comment. It doesn't, because that comment
lives in AffordabilityService.cpp's anonymous namespace: the key layout is
the service's *private knowledge*, and a second file spelling the prefix
is drift waiting for a rename. So the services grew static doors —
`forgetManners()`, `clearTodaysAsk()` — and the panel presses those. One
line each, and the cheapest possible insurance that a future key rename
breaks a compile instead of a debug session. (This also cashes the
comment's own promise: "a 'reset the assistant's memory' button later is
one remove" — the button now exists, where the comment said it would.)

## E. The wildcard, and why the hook now reaches every wire

`TICKTIMER_AI_DOWN=*` downs every seat. The alternative — the panel
enumerating catalog ids to build a comma list — is a second copy of the
catalog that must agree with it forever: the installer's version lesson
(§mechanism-over-intention) wearing a string list. One wildcard, checked
in `forcedDown` itself, keeps the catalog the only list.

The deeper fix rode along: `forcedDown` was only ever consulted by the
chat's route walk, so the single-seat wires (nudge, quick-add) could not
be forced down at all — which is the concrete mechanism behind "has never
heard the v28.0 voice". Both wires now check the hook, each with its own
manners: the nudge collapses into silent `fallback()` (its personality is
that it cannot fail loudly), quick-add names the cause in its visible bar
(someone is waiting). Same hook, two personalities — the difference is
the feature, not an inconsistency.

## F. The clock's honest reach

"Pretend it is this moment" rewires **affordability, check-in, and the
chat briefing** — the three v28 consumers of injected time — and nothing
else. The tracker, Pomodoro, alarms, and the painted now-line stay on the
wall clock. Two reasons. Narrow: those services keep live wall-time state
(a running interval under a frozen clock records nonsense durations), so
widening the fake clock is a per-service design question, not a default.
Honest: the panel's group title *names* its reach, because a debug tool
that lies about its own scope generates the bugs it exists to find. The
wiring lives in MainWindow's lambda — the composition root — because
*which objects own clocks* is composition knowledge; the panel just says
"this moment, please".

## G. The briefing viewer is the headline, not the buttons

The field report's five findings shared one root: the briefing failed to
state a fact, and the model guessed. The debugging move that finds this
class of bug is *reading the model's inputs*, not squinting at its
outputs — the context is the product. So the panel's most important
control is the humblest: a button that shows `currentBriefing()`, fetched
fresh per press (derived state is never cached — the catch-up drawer's
stale-print lesson, third application). Expected use: notice a wrong
answer, open the viewer, find the missing or ambiguous line, fix
`DayBriefing.cpp`, press again.

## H. What this deliberately is not

- **Not a cheat console.** No button bypasses quiet hours, the 3-a-day
  cap, or dismissal respect. Manners are the feature under test, not an
  obstacle to it; the fake clock lets you *step outside* quiet hours,
  which tests the rule instead of ignoring it.
- **Not discoverable chrome.** No menu entry, no settings toggle, no
  first-run tip. TESTING.md is the front door.
- **Not a second implementation.** The panel contains zero judgement —
  see §A. Its test (`debugPanelPressesTheSeams`) drives it by objectName
  and asserts only that seams got pressed, which is all it does.
