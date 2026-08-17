# Design addendum — natural-language quick-add (v21)

## A. What shipped

Type one line into the Activities "+ Add a task" input and it becomes a
fully-dressed task:

```
lab 4 report next friday ! weekly #school
→  title "lab 4 report" · due <next Friday> · Urgent · ⟳ Weekly · → School
```

A live preview under the input shows the parse on every keystroke, so you see
exactly what Enter will commit before you press it.

This is the app's first AI-flavoured feature — and it deliberately starts
**deterministic**, not model-backed. The architecture is the point: get the
*shape* right (pure parse → struct → preview/commit), and a smarter backend can
slot in later without the UI changing.

## B. The core: a pure function

```cpp
nlp::parseQuickAdd(text, today) → ParsedTask{ title, dueDate, priority,
                                              repeat, categoryHint }
```

Same family as `stats::summarizeDay` and `version::decideBanner`: no AppData,
no widgets, no clock of its own — `today` is a parameter, so every test anchors
to a fixed date (Wed 2026-07-15) and never depends on when the suite runs. That
choice is why the parser got 17 tests on its first build, running in
microseconds, linked Core-only (`test_nlp` is the leanest target in the
project).

## C. The grammar — every rule is a decision

- **Tokens match case-insensitively; the title keeps original casing** — it is
  simply every token nothing else claimed, joined back in order.
- **First match wins, per facet** (one date, one priority, one repeat, one
  `#tag`). A second date expression stays in the title, where the preview makes
  the surprise *visible* instead of silently overriding.
- **Bare weekday = soonest such day, today-or-later** — "friday" typed on a
  Friday means today. `next friday` adds a week.
- **"aug 8" with no year = the soonest such date** (this year, else next).
  An explicit year is obeyed even in the past — that's the user overriding
  "soonest", not a mistake.
- **Impossible dates ("feb 30") are not dates** — the tokens stay in the title
  rather than being guessed at.
- **Numeric slash dates ("8/8") are unsupported on purpose**: Aug 8 vs 8 Aug is
  a locale coin-flip, and a quick-add that guesses wrong is worse than one that
  leaves the text alone.
- **Ordinal suffixes read as day numbers** (v21.0.1, born from a real report:
  "lab 4 report 28th july" left the date TBD): `28th july`, `july 28th`,
  `aug 1st` all parse. The suffix is stripped loosely — "22th" still reads as
  22; quick capture forgives typos.
- **A bare ordinal is a date; a bare number is not.** `rent 28th` = the
  soonest 28th that is today-or-later (a `31st` skips the short months). But
  plain `28` stays a title word — the suffix is the user *saying* "this is a
  day", and that stated intent is what licenses the guess. Without that rule,
  half a student's tasks ("lab 4") would grow phantom dates.
- Dates: `today`, `tomorrow`/`tmrw`, weekday names + short forms, `next <day>`,
  `in N days/weeks`, `aug 8` / `8 aug` / `28th july` / `aug 8 2027`, a bare
  ordinal (`28th`), ISO `2026-09-01`. Priority: `!`(+), `urgent`, `high`,
  `low`. Repeat: `daily/weekly/monthly/yearly`, `every day/week/month/year`.
  Category: `#school`.

## D. The category hint — purity by delegation

The parser returns `categoryHint` as a *string* ("school"), never an id: it
knows no categories, which is what keeps it pure and dependency-free. The UI
resolves the hint — exact name match, case-insensitive — in
`ActivitiesPage::resolveCategoryHint()`. No match (or no tag) falls back to the
selected life area, so a typo degrades gracefully instead of blocking the add
or inventing a category. The preview shows where the task will actually land:
`#School` in the focus colour when resolved, `#schol?` in grey when not.

## E. One parse, two consumers

The preview label and the commit path call the **same function** — so the
preview cannot drift from what Enter does, because there is only one
interpretation (see `diagrams/quickadd_flow.puml`). Commit is
`addTask(title, catId, dueDate)` then, when priority/repeat were parsed, the
same `updateTask` the detail dialog uses. That second call is a single-row
`dataChanged` under the v20.3 granular model — the extra hop is visually free.

## F. What phase 2 looks like (not built yet)

Two natural extensions, in order: a **global capture bar** (the parser is
page-agnostic already; the missing piece is only "which category by default"),
and an **LLM fallback** for inputs the grammar can't crack — a wire client in
the `ShareClient`/`UpdateClient` mould that maps messy text onto the *same*
`ParsedTask`. The struct is the contract; everything downstream stays put.

## G. Tests

20 parser tests (`test_nlp`, new suite) pin every rule above, one rule per
test — including the v21.0.1 ordinal trio, written failing-first from the field
report before the fix. 3 UI tests drive the real input: one line →
fully-dressed task; `#health` re-routes past the rail selection; the preview
appears while typing and hides on clear. Totals after v21.0.1: **60 domain + 17
model + 20 nlp + 26 UI + 19 auth (+ 11 live) = 142 automated, 153 with the
server suite.**

---

## H. The global capture bar (v21.1)

**Ctrl+N, from anywhere** (or the header's "+ Capture" button — a shortcut
nobody finds is a feature nobody has): a floating overlay appears, you type a
line, Enter commits it, the input clears and stays open for the next line
(brain-dump batching), Esc dismisses. Same parser, same preview — three
surfaces now, one interpretation.

### Why an overlay, not a bar on every page

Capture is rare-but-urgent. A persistent bar pays screen rent on every page for
the 1% of moments you need it; a summoned overlay costs nothing until the
thought strikes — and then the thought goes from head to list without touching
the mouse or leaving the current page. The gap between "I'll add it when I get
to the Activities page" and *now* is precisely where tasks die.

### The category question

The Activities input sits inside a life area, so "which category?" answers
itself. The overlay has no context, so it follows three rules in order:
**1)** a `#tag` routes explicitly; **2)** otherwise the *remembered default* —
the category you last captured into, persisted in QSettings ("capture
memory": most brain-dumps go to the same bucket); **3)** otherwise the first
category. A remembered default whose category was deleted falls through to
rule 3 rather than ghost-writing — pinned by `captureOverlayStaleDefaultFallsBack`.
And since the overlay shows no context, the preview **always** names the
landing area; there, it's not a nicety but the trust story itself.

### The second-consumer rule struck twice

The overlay is quick-add's second surface, and two pieces of v21.0 promptly
demanded shared homes:

- **`#tag` resolution → `AppData::categoryIdByName()`**. "What does this name
  mean?" was never really the Activities page's question — it's a domain
  query. Exact match, case-insensitive, empty on no match, *no fuzzy prefix
  magic* (a quick-add that guessed "Sch" → "School" would one day guess wrong
  silently). `ActivitiesPage::resolveCategoryHint` now delegates to it and
  keeps only its own fallback.
- **The preview readout → `quickAddPreviewHtml()`** (`QuickAddPreview.h`,
  header-only inline like Task.h's label helpers). The preview is the trust
  contract of quick-add; two hand-rolled copies would drift the first time one
  gained a chip the other didn't.

### Wiring notes

`QuickCaptureOverlay` is a frameless modal `QDialog` owned by MainWindow —
built once, summoned by `popup()` (reset, centered in the upper third,
focused). Esc closes for free via `QDialog::reject`. The `Ctrl+N` shortcut has
`Qt::ApplicationShortcut` context, which is the entire feature: it fires with
focus on *any* page. Commit emits `taskCaptured(categoryId)`; MainWindow
persists it as the next default. The overlay performs the same
`addTask` + `updateTask` pair as the Activities input.

### Tests

+1 domain (`categoryIdByNameIsExactAndCaseInsensitive`), +3 UI (default
category honoured; `#tag` overrides *and* commit batches with a cleared,
still-open input; stale default falls back). Totals after v21.1: **61 domain +
17 model + 20 nlp + 29 UI + 19 auth (+ 11 live) = 146 automated, 157 with the
server suite.**

---

## I. Click-away dismiss + the AI fallback (v21.2)

### Click-away (the small one first)

The overlay now closes on **losing window activation** — click the main
window, another app, or alt-tab, and it's gone; Esc still works. One event
(`QEvent::WindowDeactivate` → `close()`), one dropped line (`setModal(true)`,
which would have swallowed the very outside-click that is now the gesture).
Capture is a beat, not a mode: if your attention left, the overlay's job is
over.

### The AI fallback — Ctrl+Enter, explicitly

Press **Ctrl+Enter** in the overlay and the raw line goes to the Anthropic
API; the reply maps onto the *same* `ParsedTask`, the preview re-renders with
a ✨ provenance marker, and the next Enter commits the model's interpretation.
Explicit, never automatic: no surprise network calls, no latency on
keystrokes, and the deterministic parser stays the instant default — the AI
is for the lines the grammar can't crack ("dentist end of next week").

### The architecture is the lesson: wire/pure split

```
LlmQuickAddClient (wire)          nlp::llm (pure — LlmQuickAdd.h)
--------------------------        --------------------------------
QNetworkAccessManager, POST       systemPrompt(today) — the contract
headers, timeout, staleness       parseApiReply(bytes) — the meaning
"did bytes arrive?"               fromJsonObject — defensive mapping
```

Everything that can be *wrong* about an LLM integration — hallucinated
fields, non-ISO dates, ```json fences despite the no-markdown rule, prose
where JSON should be — lives in the pure half, fed forged replies in
microsecond tests. The wire is too thin to hide bugs in (the ShareClient
doctrine, applied to a new kind of remote). Defensive mapping means an
imperfect AI answer degrades to the deterministic parser's own defaults —
never worse than nothing — except a missing title, which fails loudly rather
than committing an unnamed task.

### The trust rules

- **An armed AI parse is cleared by ANY edit** — the model answered the OLD
  text; committing a stale answer is the drift the preview exists to prevent
  (`captureOverlayEditDisarmsAiParse` pins it).
- **Stale replies are dropped** — each request bumps a generation counter; an
  answer to a question you're no longer asking dies silently in the client.
- **No key fails fast, offline, and says where the fix is** (Settings → AI,
  or `ANTHROPIC_API_KEY`) — the first failure every new user hits carries the
  manual. The key is read **at request time** (pref-read-at-fire-time, the
  Pomodoro doctrine): change it in Settings, the next Ctrl+Enter uses it.
- The prompt states today's date + weekday so relative dates resolve
  server-side to absolutes we can trust verbatim.

### Settings

One new row — "AI quick-add key" — which lands in SettingsDialog by that
dialog's own junk-drawer rule: a credential has no natural home page. Written
raw (no `prefs::` wrapper) because a one-consumer key doesn't earn a named
accessor yet — the second-consumer rule cuts both ways.

### Tests

+5 pure (`llmReplyMapsAllFields`, fences-stripping, garbage-degrades,
missing-title-fails, prompt-contract), +4 UI (click-away/Esc; no-key hint;
AI parse arms commit — driven through the slot seam with
`QMetaObject::invokeMethod`, no network; edit disarms). Totals after v21.2:
**61 domain + 17 model + 25 nlp + 33 UI + 19 auth (+ 11 live) = 155
automated, 166 with the server suite.**
