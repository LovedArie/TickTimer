# v29.1 — the interview (Slice 2: the model joins intake)

*§K, paid in full. The NEEDS DETAILS queue gains a voice; the model
arrives — as a proposer, and nothing more. Nothing to the right of the
Slice 1 card moved.*

## The brain is C++ (`intake::`, in the domain suite)

Triage (§K.6: deadline ∨ urgent ∨ heavy-history area, never if
dismissed — ask once via the `dismissedUntil` that waited two arcs for
this), the question (§K.2, deterministic and free), the history guess
(§K.3: median of tracked actuals over finished same-category tasks,
two-sample floor, basis attached — "2 finished School tasks ran ~2h
each"; the label is the license), and the crisp-answer parser ("2h",
"1h30", "90 min") whose REFUSALS are the design: anchored end-to-end,
it rejects "probably 2h if Marc shows up", "1.5h", and "2024", because
a cheap parse that plucks numbers from prose would pre-empt the reader
that understands the prose.

## The extraction is pure (`intake::llm`, in the nlp suite)

Quick-add's split one level up, §K.2's own phrasing: prompt-as-contract
(fields, null-when-unsure, today's date stated, the offered guess so
"sounds right" extracts to that number, "do not restate" for known due
dates, the realistic-total rule), `parseReply` through the vendor
envelope with fence-stripping and string-clothed-number leniency.
**Deliberately not the tool-use API** — §B.3's second recorded
non-firing: plain JSON runs on every seat (local Ollama included), the
confirm loop already IS the tool layer, and single-shot extraction has
no cross-call state for a Dialect strategy to hold.

## The room (`ChatPage`), the wire (`IntakeClient`), the entries

`beginIntake()` triages from the live handle world and chains on card
settle; answers route crisp-parse → model → honest hint (the interview
works with every AI seat down); the guess button crosses the card too —
every write does, no convenience exceptions; Skip presses `dismissTask`
directly (the owner acting, not a proposal), and a discarded card is
NOT a skip (`askedThisSession`, session-scoped). Entries: the morning
check-in's offer after mood lands (§K.1's designed moment — never at
capture), and the debug panel's "Start intake interview" forcing the
same road. `IntakeClient` is quick-add's twin with `forcedDown` honoured
from birth.

## Build lessons kept (addendum §H)

One header, two translation units, split by dependency group — the
linker's verdict on extraction-in-the-domain-sources. And the prompt
takes VALUES, not AppData — test_nlp's own purity charter rejected the
lazier draft. Suite structure with teeth turns design drift into
compile errors.

## Numbers

**379 tests across six suites** (158 domain + 22 taskmodel + 70 nlp +
95 ui + 19 auth + 15 login_live; was 371). Format stays **v13** — the
interview added zero stored state. Versions at 29.1.0 in BOTH files,
checked before the cut.
