# TickTimer v25.3.0 — persona: pick the assistant's voice

Apply ON TOP of v25.2: unzip over the project root, rebuild. (CMakeLists
unchanged — edits to existing files only.)

    cmake --build build
    ctest --test-dir build        # expect 6/6 suites, 252 tests

## What it does
Settings → AI gains **Assistant style**: Calm (default — the v25 voice,
verbatim in intent), Brief, Coach, or Custom, plus one line of **Extra
instructions** that appends to any preset and IS the style for Custom.
Applies to the Assistant page only; quick-add parsing has no tone to set.

Under it, the system prompt became **four bands in authority order** —
contract → floors → style → context. Old rule 4 split in half: *never
shaming* promoted to a locked floor no style can reach (plus a new floor:
know your lane), *calm tone* demoted into the Calm preset. Rule 3
(brevity) followed — verbosity is a persona property. "A persona changes
HOW, never WHAT" is enforced as a string equality in the tests: everything
above the STYLE marker is byte-identical across the whole catalog.

## Changed files (10 + 3 new)
    include/ChatSession.h, src/ChatSession.cpp   Persona value + catalog +
                                                 repair-on-read; four-band
                                                 systemPrompt; configured
                                                 band (QSettings, read at
                                                 fire time)
    src/ChatPage.cpp                             the one call site
    include/SettingsDialog.h, src/SettingsDialog.cpp
                                                 style combo (from the
                                                 catalog) + free text
                                                 (QLineEdit, max 240 — the
                                                 short-beats-elaborate rule
                                                 as a widget); save() on OK
    tests/test_nlp.cpp (+5), tests/test_ui.cpp (+1)
    include/Version.h                            25.3.0
    README.md                                    status, counts (252), index
    docs/AI.md                                   §5b — the user-facing guide
    docs/design-addendum-chat.md                 +§K — the design reasoning
    docs/design-addendum-assistant.md            §C struck; v26 = §E alone
    docs/QUESTION_BANK.md                        V89–V91
    docs/SESSION_NOTES.md                        session entry
    NEW: diagrams/persona_prompt_bands.puml/.png/.svg

## Verified in-container
    252 green: 86 domain · 61 nlp · 19 taskmodel · 19 auth · 67 UI (+ live e2e)
    app + server build clean against Qt 6.4

## Storage note (why nothing synced)
Persona lives in QSettings (`ai/persona`, `ai/personaText`), NOT data.json:
persona is taste, the same class as agenda hours — facts sync between
machines, taste stays on the machine that chose it. No format bump.

## Next (§N)
v26.0 = §E per-role provider routing: role → ordered seat list,
fall-through on *unreachable* only, the check-in-runs-local privacy
boundary, named seats (§E.5), circuit breaker, settings migration, and the
Test button growing into "Test all".
