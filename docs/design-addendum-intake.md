# Design Addendum — The Intake Interview (v29.1, §K shipped)

*Slice 2 of the tool-use arc: the model joins — as a proposer, and nothing
more. Companions: `diagrams/intake_flow.*`, the TESTING.md recipe, and
§K of the assistant addendum, whose six subsections this file marks paid.*

---

## A. The division of labour, drawn hard

Everything that does not need a model does not touch one. Triage (§K.6),
the question (§K.2), the guess (§K.3), and the crisp-answer parse are
pure C++ in `intake::` — deterministic, offline, microsecond-tested. The
model's entire job is one function shape: messy prose in, two nullable
fields out. The owner's entire job is unchanged from Slice 1: the tap.
Consequence worth stating plainly: **the interview works with every AI
seat down.** The question still asks, "2h" still parses, the card still
confirms, the door still fills the blank. The model upgrades the
experience ("probably two evenings but Marc never does his part" → 480);
it gates nothing.

## B. Extraction, not the tool-use API — §B.3's second recorded answer

The intake model never *calls* anything. It emits the agreed JSON object,
which the caller turns into a `verbs::Proposal`, which crosses the Slice 1
card. Three reasons this beats the vendors' native tool-calling, recorded
because §B.3 ordered the recording:

1. **Provider neutrality.** Plain JSON-out runs identically on Anthropic,
   OpenAI-compatible servers, and a local Ollama with no native tool
   support. The interview must not be a premium-seat feature.
2. **The confirm loop already IS the tool layer.** Stage 3 of §B does
   what tool dispatch would do, with the owner as the dispatcher. Adding
   API-level tool_use blocks would build a second, uninspectable dispatch
   under the inspectable one.
3. **Single-shot stays single-shot.** No tool transcript to thread
   per-provider — so the `Dialect` promotion criterion (state across
   calls) does not fire, *again*. Slice 1 recorded "no cross-call state
   because C++ composes the proposals"; Slice 2 records "no cross-call
   state because extraction is one exchange by construction." If a future
   verb genuinely converses (multi-step rescheduling might), the
   criterion is still armed and still honest.

## C. The three answer tiers, and why crisp goes first

`sendCurrentInput`, interview active: (1) `parseDurationAnswer` — "2h",
"1h30", "90 min" — free, instant, offline; (2) the model, for prose;
(3) the honest hint ("give me something like \"2h\""), which keeps the
interview open and names the path that always works. Crisp-first is a
cost rule and a sovereignty rule at once: an answer the user already
made unambiguous should not ride a network round-trip, and *must not*
arrive reworded by a model. The parser's refusals are the design's
sharpest edge: it is anchored end-to-end and rejects "probably 2h if
Marc shows up", "1.5h", and "2024" — because a cheap parse that plucks
numbers out of prose would silently pre-empt the reader that
understands the prose, and be wrong exactly when it matters.

## D. The guess crosses the card too

One tap on "≈ 2h sounds right" composes a Proposal and presents the
card — it does not write. Two taps where one might do, kept on purpose:
**every write crosses the card, no convenience exceptions.** The moment
"trusted" paths skip the confirm, the confirm stops meaning anything,
and auditing which paths skip it becomes a job. The consistency is the
trust story; it costs one tap.

## E. Skip is the owner acting; discard is not skip

Skip presses `dismissTask` directly — the mood-buttons precedent: the
owner's own tap on their own data goes through the domain door, not
through a proposal (proposals exist for *other* wills). A year's
dismissal is "ask once" (§K.6) without a year-9999 silliness value.
And a **discarded card is not a skip**: discarding says "not this
number", not "never ask" — so the loop's don't-re-ask-immediately memory
is `askedThisSession`, session-scoped and never persisted. Inferring a
dismissal from a discard would write domain state the owner didn't ask
for, from a gesture that doesn't mean it.

## F. The guess: median, two samples, and the label as the license

`historyGuess` is the personal rate's scan (tracked focus by taskId over
finished same-category tasks) with a different summary: the median of the
actuals. The floor is TWO samples where the rate demands more — defended,
not inconsistent: the rate scales estimates *silently*, so it needs real
evidence; the guess is *spoken*, carries its basis ("2 finished School
tasks ran ~2h each"), and is confirmed by the owner. Weak evidence is
acceptable when labelled — the label is the license. The test plants a
600-minute disaster among normals and pins the median shrugging it off.

## G. Entries: the check-in, the panel, and deliberately not capture

§K.1 verbatim: never interrupt at capture — an assistant that interviews
every Ctrl+N trains the user to stop capturing. The designed entry is
the morning check-in (the moment the user already chose to talk): after
the mood lands, one offer, one "Not now" that just removes the row. The
debug panel's "Start intake interview" forces the same `beginIntake()`
the offer presses — recipe and product walk the same road. A quiet badge
elsewhere in the chrome remains open for a polish slice.

## H. What the build taught (kept because it will teach again)

Two structural corrections came from the tree pushing back, not from
planning. The linker refused extraction-in-the-domain-group (`undefined
ai::extractText` in test_domain) → **one header, two translation units,
split by dependency group** — Intake.cpp with the domain, IntakeExtract.cpp
beside LlmQuickAdd.cpp. Then test_nlp's own charter comment ("pure AI
layers must not drag AppData in") rejected `systemPrompt(AppData&, …)`,
which used the whole domain to format one category name → the prompt now
takes VALUES (`Task`, `areaName`), and the caller resolves the name.
Both are the same lesson: suite structure with teeth converts design
drift into compile errors, which is the cheapest place drift ever gets
caught. (Also on the record: for one commit this slice held two
extraction layers, because the second was written from plan notes
without re-reading the first. The header was the abstract; read the
tree before extending it.)
