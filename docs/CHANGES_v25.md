# TickTimer v25.0.0 — the Assistant (chat panel)

Unzip this over your project root — every file lands at its correct relative
path. Then reconfigure and build as usual (CMake picks up the new sources
from the updated CMakeLists.txt automatically):

    cmake --build build
    ctest --test-dir build        # expect 6/6 suites, 238 tests

## New files (12)
    include/DayBriefing.h    src/DayBriefing.cpp     brief:: — the pure day briefing (domain side)
    include/ChatSession.h    src/ChatSession.cpp     chat:: — transcript, window, system prompt (Core side)
    include/ChatClient.h     src/ChatClient.cpp      the fifth wire client (60s timeout, 429, cancel)
    include/ChatPage.h       src/ChatPage.cpp        the Assistant rail page
    docs/design-addendum-chat.md                     §A–§J, incl. the §C revisit
    diagrams/chat_turn_flow.puml / .png / .svg       one turn, end to end

## Modified files (14)
    include/LlmProvider.h, src/LlmProvider.cpp   +ai::Role/Message/chatRequestBody;
                                                 requestBody now DELEGATES (one dialect switch)
    include/MainWindow.h, src/MainWindow.cpp     Assistant page (stack index 6, rail above Archive);
                                                 m_navButtons ordered by page identity, not rail position
    include/Version.h                            25.0.0 (static_assert guard passed)
    CMakeLists.txt                               brief::→DOMAIN_SOURCES; chat layer→UI_SOURCES;
                                                 ChatSession.cpp→test_nlp (Core-only, enforced)
    tests/test_nlp.cpp                           +6 (multi-turn body, delegation pin, window, localOnly, prompt)
    tests/test_domain.cpp                        +5 (briefing: labels, emptiness, partition, caps, privacy)
    tests/test_ui.cpp                            +4 (nav off-by-one guard, Enter/Shift+Enter,
                                                 failure→log bubble, live briefing on the seam clock)
    README.md                                    status v25, counts 238, suite breakdown
    docs/06_IterationPlan.md                     chat ✅, tool use marked next
    docs/design-doc.md                           §3 index row for the chat addendum
    docs/QUESTION_BANK.md                        V73–V82
    docs/SESSION_NOTES.md                        the v25 session entry

## Verified in-container (Qt 6.4.2, Linux offscreen)
    domain 86 · nlp 50 · taskmodel 19 · auth 19 · ui 64  (+ login_live e2e) — all green
    app binary builds and launches; data format unchanged at v9 (no migration)
