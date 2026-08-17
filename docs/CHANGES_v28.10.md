# v28.10 — the seams, reachable (the field-report slice)

*Slice 0 of the road to v29: every item the first field report ordered,
shipped before tool use lands — because v29's confirm loop will need
forcing by hand even more than v28's toasts did.*

## The debug panel (Ctrl+Shift+D)

New `DebugPanel` (+ `design-addendum-debug.md`, `docs/TESTING.md`,
`diagrams/debug_seams.*`): pure glass over existing seams — a frozen
clock for affordability + check-in + the chat briefing, sweep-now
buttons, the manners resets both services' comments had promised
(`forgetManners()`, `clearTodaysAsk()`), a check-in **rehearsal**
(`forceOffer()` — gate skipped, ledger *not* spent), a briefing viewer
showing the exact text the model receives, and one checkbox that downs
every AI seat for this process. A chord, not a menu bar: the chrome is a
nav rail and stays one.

## The briefing learns what the field day proved it must state

All three content gaps closed in `DayBriefing.cpp` (chat addendum §C.1):

- **DAY STATUS** (#3) — "2 of 3 planned blocks still ahead; the last
  block ends at 16:00" / "the planned day is OVER". The phase was
  arithmetic we were quietly asking the model to do; §A says it can't.
- **PLAN FOR TOMORROW** (#2) — tomorrow's blocks enter the context,
  ISO-dated, capped, emptiness stated ("nothing planned *yet*"). One day
  only: deadlines already reach `upcomingDays` out.
- **Totals disambiguated** (#4) — "TRACKED TODAY (day totals; any
  per-block 'tracked' figures above are parts of these, not extra)". The
  self-contradiction was the model reconciling two numbers we should
  have reconciled.

## Markdown renders (#1)

`ChatPage::addBubble`: assistant bubbles get `Qt::MarkdownText` —
markdown over RichText because it is the *narrower* surface for
model-written text (no `<img>`, no scripts); user bubbles and local
notices stay plain, so your own asterisks remain asterisks.

## The forcing hook reaches every wire

`TICKTIMER_AI_DOWN` gains a `*` wildcard (every seat, present and
future — what lets the panel be one checkbox instead of a second copy of
the catalog), and the two single-seat wires finally consult it:
`NudgeClient` collapses into silent `fallback()` per its personality;
quick-add names the cause in its visible bar. This is the mechanism
behind the report's "has never heard the v28.0 voice" — fixed where the
mechanism was missing, not where the symptom showed.

## Numbers

**359 tests green across six suites** (149 domain + 22 taskmodel +
68 nlp + 90 ui + 19 auth + 11 login_live; was 353): day-phase running
and over, tomorrow's section and its stated emptiness, the totals label,
the wildcard's three states, `forceOffer`'s gate-skip + spared ledger,
and the panel driven by objectName. Format stays **v13** — nothing here
touches stored data, which is exactly what "the panel decides nothing"
predicts. `installer/ticktimer.iss` rides along at 28.10.0 — it is the
project's one allowed hand-copied version ("bump BOTH, every release"),
and this drop's first cut forgot it; the v28.3 apply check caught the
mismatch on the owner's machine before anything built, exactly as
designed.
