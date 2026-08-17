# TickTimer v25.1.0 — the Settings Test button

Apply ON TOP of v25.0: unzip over the project root, rebuild. (CMakeLists is
unchanged — no new files, only edits to existing ones.)

    cmake --build build
    ctest --test-dir build        # expect 6/6 suites, 240 tests

## What it does
Settings → AI gains a **Test** row: one tiny request at whatever is on
screen — unsaved key/address/model included — with the verdict inline
(✓ green, or the provider-aware error in red). Works for every provider,
including keyless local ones.

## Changed files (10)
    include/LlmProvider.h, src/LlmProvider.cpp     +ai::envKey (env half extracted;
                                                   configuredKey now composes it)
    include/ChatClient.h,  src/ChatClient.cpp      +setKeyOverride — the probe seam
    include/SettingsDialog.h, src/SettingsDialog.cpp  the Test row, aiProviderFromFields(),
                                                   runAiKeyTest(); verdict cleared on
                                                   provider switch; probe parented to dialog
    tests/test_ui.cpp                              +2 (offline fail-fast + writes-nothing;
                                                   fields-beat-saved-settings)
    include/Version.h                              25.1.0
    README.md                                      AI section + counts (240)
    docs/design-addendum-provider.md               §K — the three design decisions
    docs/QUESTION_BANK.md                          V83–V85
    docs/SESSION_NOTES.md                          session entry (incl. the pkill lesson)

## Bonus (not part of the repo)
    stub_llm.py           a ~60-line fake OpenAI-compatible provider on
                          localhost:8930 — reads the briefing the app sends and
                          answers from it. Run it, set provider=Custom,
                          address http://127.0.0.1:8930, dialect OpenAI-compatible,
                          press Test → ✓, then chat. Also the recipe for LM Studio.
    chat_demo_1/2.png     the Assistant page conversing with that stub in the
                          container — the v25 pipeline proven end-to-end, no key.

## Verified in-container
    240 green: 86 domain · 50 nlp · 19 taskmodel · 19 auth · 66 UI (+ live e2e)
