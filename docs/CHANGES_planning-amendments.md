# TickTimer — Assistant Roadmap, three amendments (documentation only)

**No code changed. Version stays 25.1.0. Tests stay 240, 6/6 green.**

Apply ON TOP of the planning drop. Unzip over the project root. Nothing to
rebuild — no `.cpp`, no `.h`, no `CMakeLists.txt` touched.

## Updated files
    docs/design-addendum-assistant.md   +§B.4, +§E.5, +§O.7; the §E.4
                                        attribution bullet now names §E.5
    docs/SESSION_NOTES.md               the comparison session

## New files
    (none — no new diagrams; all three amendments are rules, not flows)

## Where it came from
A comparison against **OpenClaw**, an open-source personal AI agent that
configures two axes we had collapsed into one: multiple *models* (providers →
per-model allowlist → aliases) and multiple *agents* (each with its own
workspace, model registry, and tool permissions).

Three of its recommendations turned out to be things §D and §E already say —
`reasoning: false` when tools break, pin the context window per model, keep a
local seat for anything that must not leave the machine. Independent
convergence, which is better evidence than a design defended alone.

Three others were worth importing.

## The amendments

### §B.4 — verb lists are per-role, not global
Their per-agent allow/deny lists, transposed onto our call sites. Withholding
`removeTask` globally prevents damage; scoping per role prevents a category.
Nudge and check-in get **empty** verb lists. Free to design in now, expensive
after v29.

### §E.5 — seats are named
Their model aliases, adapted. Settings rows, "Test all" results, and the bubble
attribution tag all need one display string; store it once or watch three
surfaces invent three different labels. We name *seats*, not models, because a
seat is provider + address + model + key. **The name is cosmetic and never a
key.**

### §O.7 — does memory partition by role?
Their multi-agent isolation is partly about memory bleed. One residue file
serving both the planner and the check-in re-mixes the mood data §E.4 pinned to
the local seat. Recorded **undecided** — the kind of question that gets
answered by default if nobody writes it down.

## Deliberately NOT taken
    multi-agent isolation   AppData owns and guards the data, not the
                            assistant; there is no workspace to isolate
    sub-agents              cost control for an always-on agent; this one
                            answers one message when a page is open
    a JSON config file      correct for a power tool, wrong for a Settings
                            dialog

## Deliberately NOT updated
    docs/QUESTION_BANK.md   the bank documents what EXISTS. V83 remains the
                            tail. Three design sections no test has ever run
                            are exactly what must not enter a study aid.
    docs/AI.md              a user guide shouldn't promise features.
    diagrams/               nothing here is a flow; a diagram would be
                            decoration

## Carried into v29
Models under ~14B often fail multi-step tool use. `qwen3:8b` is under that
line — a fine conversation seat, a doubtful tool seat. A second, unplanned
argument for per-role routing.

## Next
**§D as v25.2** — strip `<think>`, fall back to `reasoning`, best-effort
`think: false`. Pure-layer, offline-testable against forged reply bytes, no
format bump.
