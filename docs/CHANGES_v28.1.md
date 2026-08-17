# TickTimer v28.1.0 — the model phrases the nudge

Apply ON TOP of v28.0 + the reconciliation drop: unzip over the project
root, rebuild, then **verify the apply**:

    grep VERSION_STRING include/Version.h        # must say 28.1.0

Then:

    cmake --build build
    ctest --test-dir build        # domain suite: +4 cases

**Honesty note (same as v28.0):** built without Qt in the session
container — balance-checked and API-cross-checked, not executed. The wire
client mirrors the tested quick-add pattern but is not itself covered by
the suite. Run ctest; send any compile error and it's a minutes-fix.

## What it does
When a task turns Tight, the assistant now writes the heads-up — in
whatever persona your chat uses, with the safety rules locked above the
style: inform never forbid, never shame, only the given numbers, two
sentences max. If the model is unreachable, slow (8 s budget), keyless, or
over-wordy, the plain v28.0 sentence shows instead — a nudge cannot fail,
only sound more or less human. You will never see an AI error from a
nudge; failures live in the debug log.

And the chat side: the assistant's briefing now carries **DEADLINE
PRESSURE** — the same computed verdict per task, with its numbers — so
"I want to go out tonight, can I afford to?" finally gets a data-grounded
answer in conversation.

## Where things live
- `nudge::` (NudgePhrasing.h) — prompt bands, message, accept gate; pure
- `NudgeClient` — one seat, exactly-once, fails only into fallback()
- `AffordabilityService::deliver()` — the one exit; bookkeeping at delivery
- reasoning: `docs/design-addendum-affordability.md` §K
