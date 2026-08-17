# TickTimer v26.0.0 — a fallback seat for the Assistant

Apply ON TOP of v25.3: unzip over the project root, rebuild. (CMakeLists
unchanged — edits to existing files only.)

    cmake --build build
    ctest --test-dir build        # expect 6/6 suites, 260 tests

## What it does
Settings → AI gains **If unreachable, try**: a second seat for the
Assistant (a local Ollama is the natural pick). When the primary cannot be
*reached* — connection refused, network down, timeout — the chat falls
through, says so in the transcript as it happens ("⚠ Anthropic unreachable
— trying Ollama (local)…"), and labels the fallback answer with its author.

What it refuses to do: paper over configuration. A rejected key, a 404, a
rate limit fail loudly on the seat that produced them — only *silence*
falls through, never *speech*. A ~20 s circuit breaker makes offline
messages fail instantly (naming the seats) instead of hanging on a dead
connection every time. `TICKTIMER_AI_DOWN=<ids>` forces seats down for
testing the walk without unplugging anything.

Quick-add gets no fallback row on purpose: its fallback is the
deterministic parser it already has.

## v26 of the roadmap is COMPLETE with this drop
§D reasoning models (v25.2) · §C persona (v25.3) · §E routing (v26.0).

## Changed files (12 + 3 new)
    include/LlmProvider.h, src/LlmProvider.cpp   ai::Feature + ordered routes
                                                 (migration by DERIVATION —
                                                 no settings write; see §M);
                                                 resolved(); seatName (§E.5);
                                                 Breaker + planRoute; the
                                                 TICKTIMER_AI_DOWN hook
    include/ChatClient.h, src/ChatClient.cpp     the seat walk: fall-through
                                                 on Unreachable ONLY; breaker
                                                 notes; seat identity on
                                                 replied(); seatUnreachable
    src/ChatPage.cpp                             walk + attribution notices
                                                 (local-only — never sent)
    include/SettingsDialog.h, src/SettingsDialog.cpp
                                                 the fallback row; route
                                                 saved as the full ordered
                                                 list ([x, x] collapses)
    tests/test_nlp.cpp (+6), tests/test_ui.cpp (+2)
    include/Version.h                            26.0.0
    README.md, docs/AI.md (§5c)
    docs/design-addendum-provider.md             +§M — the whole design
    docs/design-addendum-assistant.md            §E struck; v26 block done
    docs/QUESTION_BANK.md                        V92–V94
    docs/SESSION_NOTES.md                        session entry
    NEW: diagrams/chat_route_walk.puml/.png/.svg

## Verified in-container
    260 green: 86 domain · 67 nlp · 19 taskmodel · 19 auth · 69 UI (+ live e2e)
    app + server build clean against Qt 6.4

## Next (§N)
**v27 — subtasks & sizing.** No AI at all: new domain fields, a format
bump and migration, pure modelling. A different kind of iteration.
