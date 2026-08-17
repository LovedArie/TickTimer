# TickTimer v28.0.0 — affordability: the heads-up, before the model

Apply ON TOP of the v26.8 audit drop: unzip over the project root, rebuild.

    cmake --build build
    ctest --test-dir build        # domain suite: +10 cases, all pure

**Honesty note:** this drop was built in a container with NO Qt — every
file passed brace/paren balance checks and API cross-checks against the
tree, and the version guard was compile-proven standalone, but the full
suite has not been executed. Run ctest before trusting it; if anything
fails to compile, send the error and it gets fixed in minutes.

## What it does
Every open task with a deadline now gets a computed **affordability
verdict** — Comfortable, Tight, or an honest "can't tell" — from your own
plan: outstanding = planned − tracked, against blocks-ahead + free daytime.
When a task TURNS Tight, TickTimer volunteers a heads-up as an alert toast
(danger-red accent), with the evidence in the sentence and the decision
left with you: "…Your call." A TIGHT pill appears on the Upcoming card.

Manners are strict by design: it speaks only on a change of verdict, never
in quiet hours (22:00–08:00), at most 3 times a day, and never about a task
you dismissed. No model is involved yet — 28.1 replaces exactly one box
(the sentence) with the assistant's phrasing.

## Where things live
- `afford::` (Affordability.h) — verdict + manners + sentence, pure
- `AffordabilityService` — 20-min sweep, 30-s post-edit debounce
- bookkeeping in QSettings under `afford/` (manners state, per-device,
  deliberately NOT in data.json — no format bump, no v11 collision)
- diagram: `diagrams/affordability_flow.*`
- full reasoning: `docs/design-addendum-affordability.md`

## Roadmap effect
§H shipped (domain-first per §H.6, §F pipeline pulled in); **§O.1 closed:
volunteer-mode**, at the owner's call. v27 skipped per §N's allowed order —
the planned-blocks proxy carries it. Next: 28.1 (model phrases the Report),
then 28.2 (check-in + mood — blocked on resolving the v11 collision).
