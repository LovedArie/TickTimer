# Design addendum — the morning check-in (v28.2, parts 1 & 2)

*Roadmap §G, cut on the catch-up precedent: the domain slice first (this
drop — mood storage, retention, the heaviness gate; pure, fully tested),
the surfaces second (part 2 — the toast-with-action, the chat check-in,
and §E.4's local-seat rule). Catch-up's own retrospective is the argument:
its domain core was a fifth of the work and needed zero fixes; the
surfaces took twelve versions. And the v27 lesson stands behind the cut
too — a slice small enough to land whole beats a feature big enough to
half-land.*

## §A — Mood: the first fact the app cannot derive

Everything else the check-in will discuss is computed: the plan, the
tracked time, the deadlines. How the owner *feels* must be asked — and
therefore stored, which makes `Mood` the first new stored fact since
dismissals and the reason for **format v12**.

Three deliberate shapes:

- **Coarse: rough / okay / good.** Pattern work ("Wednesday mornings are
  consistently rough") needs comparable values; a 1–10 scale invites
  false precision no 07:40 tap can honestly deliver.
- **One per day, upsert.** A check-in answers "how is today", not "append
  to a feelings log". Re-answering replaces; there is never a duplicate
  by construction, not by cleanup.
- **The note is the owner's.** Optional free text, stored in full, synced
  with the file — and **never serialised into a briefing**. Only the
  coarse level enters a prompt. This is pinned by a test
  (`briefingSpeaksCoarseMoodAndNeverTheNote`) whose comment says where
  the fix goes if it ever fails: in the briefing, not in the test.

## §B — Format v12, and the end of the numbering collision

`data.json` gains a `moods` array; version 11 → 12. Migration is the
empty-loop kind: a pre-v12 file has no array, the owner starts with no
history, and that is *correct* — mood cannot be back-derived from
segments. The version comment in `JsonStore.cpp` also closes the audit's
bookkeeping thread explicitly: the unlanded v27 claimed 11, reality kept
11 for catch-up, mood takes 12, subtasks will take 13+.

Why `data.json` and not QSettings (the v28.0 nudge-ledger went the other
way): mood is a **fact about the user's life** — §G.2's line, "a phone's
assistant should know what the laptop's does" — where the nudge ledger is
courtesy state that must *not* sync. Same test, opposite answers, which
is what makes it a test.

## §C — Retention is a domain promise

14 days, trimmed by `AppData::trimMoods` on the same midnight knock that
runs `expireDismissals` — both call sites (startup and the midnight
timer). Trimming lives behind a domain door because "the app remembers
two weeks" is a promise about the data, not a UI convenience; a widget
that forgot to trim would otherwise silently turn a check-in into a
dossier. Forgetting emits `changed()` — **forgetting is also a write**,
and the autosave must record it.

## §D — The gate: stingy on purpose

`checkin::shouldOffer` = morning window ∧ not-yet-today ∧ heavy day.

- **Heavy** (either alone suffices): ≥ 5h of planned blocks, or ≥ 2 open
  tasks due within 2 days. Derived entirely from existing facts.
- **Morning is 06:00–11:00, [start, end).** A "morning" check-in at 15:40
  is a different, worse feature — if the morning was missed, the day
  already answered.
- **Once means once.** `lastOffered` arrives as a parameter (the caller's
  QSettings fact — manners, per the v28.0 doctrine), which keeps the
  once-a-day rule testable without touching settings.

§G.1's sentence is the whole spec: *being asked how you are doing five
times a day is its own stressor; a quiet Tuesday does not need a wellness
interview.*

## §E — What part 2 owes

The toast with the action row (the v28.0 seam, meeting its planned first
consumer), the ChatPage entry point that opens with the check-in in the
transcript, the mood capture UI, `lastOffered` bookkeeping — and the big
one: **§E.4's "local, always" rule**, which makes the check-in the first
feature to *force* per-role primaries in the routing table. Mood the fact
syncs; mood the conversation never leaves the machine.

## §F — Briefing

A compact `MOOD (self-reported, last 14 days)` line — coarse values only,
newest first, and **silence when there is no history**: an empty MOOD
header would invite the model to speculate about its absence.


---

# Part 2 — the surfaces (v28.2.1)

## §G — The knock

`CheckInService`: a 10-minute timer, the `checkin/lastOffered` QSettings
fact (manners → per-device, the v28.0 doctrine), and the pure gate. First
look 30 s after launch — launching *into* a toast reads as an ambush; half
a minute in, it reads as a knock.

**Marked at emit, not at tap.** The once-a-day promise is about *asking*,
and a dismissed toast was still an ask. Marking at tap would re-knock
every ten minutes at someone who already said "not now" with their thumb
— the snooze-is-a-lie bug, pre-empted this time.

## §H — The tap

The v28.0 action seam meets the consumer it was built for: an Info toast
(an invitation, not an alarm — 12 s, mornings are slow), whose button does
`showPage(6)` + `beginCheckIn()`. §G.1's shape, delivered.

## §I — The answer: buttons, not typing; C++, not a model

A 07:40 check-in must cost **one tap**. Three buttons; tapping records
through the domain door (upsert, so a second tap the same morning simply
corrects the first), the row deletes itself, and a C++ acknowledgement
follows — specific and non-shaming per §G.3, different per level, citing
the plan as *"a tool, not a judge."*

**§E.4 is satisfied in part 2 by subtraction:** there is no model in the
check-in loop, so there is no conversation to route locally. Every bubble
is `localOnly` — the exchange belongs to the human and the domain, never
to a future model turn's context window. The model joins the check-in when
per-role primaries exist; that day, "local, always" becomes a routing rule
instead of an absence.

## §J — The leak part 1 shipped, and its two-wall fix

Part 1 put the MOOD line in the briefing unconditionally — and the
briefing rides to whatever seat chat uses, including cloud. That violated
§E.4 ("the fact never leaves the machine") the day it shipped. Caught at
part-2 planning; fixed with two walls, both mechanical:

1. **`Options.includeMood`, default `false`** — a new briefing call site
   is private by accident, never leaky by accident. The failure mode
   chooses the safe side. (The privacy test now pins the default too.)
2. **`ai::isLocal`** — loopback only, deliberately conservative: a LAN
   Ollama on `192.168.x.x` counts as remote, because "my other machine"
   is still a wire the mood crossed. ChatPage opts in **only when every
   seat in the chat route is local** — primary *and* fallback, because a
   mid-turn fall-through must not become an exfiltration path.

The honest record: a privacy rule stated in a planning doc (§E.4) did not
survive contact with an implementation session that never re-read it.
Rules that matter get *mechanisms* — a default and a predicate — not
citations.

## §K — Version 28.2.**1**, and why the patch bump matters

The catch-up precedent shipped both parts as one version number — but
that predates the apply-check ritual, whose entire mechanism is *"the
number must match the CHANGES title."* Two drops sharing 28.2.0 would
make a part-1-only tree indistinguishable from a complete one — the exact
blindness that let v27 half-land. So part 2 is 28.2.1: the ritual stays
able to tell the truth.
