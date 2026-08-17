# TickTimer v25.2.0 — the reply contains more than the answer

Apply ON TOP of v25.1: unzip over the project root, rebuild. (CMakeLists is
unchanged — no new source files, only edits to existing ones.)

    cmake --build build
    ctest --test-dir build        # expect 6/6 suites, 246 tests

## What it does
Local **reasoning models** (Qwen3, DeepSeek-R1, …) now work in the Assistant
and in quick-add. Two failure modes fixed, both V72's bug class ("the reply
contains more than the answer") on the OpenAI path:
- `<think>…</think>` deliberation no longer leaks into the chat bubble —
  scrubbed on both dialects, unclosed spans dropped to the end;
- replies that route everything into a `reasoning` / `reasoning_content`
  field are **recovered** instead of discarded as "no text content" (the
  error survives, and now only fires when a reply truly holds no answer).
Plus best-effort `think: false` on the Ollama seat only — OpenAI proper
400s on unknown fields, so the flag is opt-in per catalog entry, never per
dialect. The reliable off-switch remains a Modelfile (`num_ctx 8192`,
thinking disabled); the scrub is the guarantee either way.

## Changed files (11 + 3 new)
    include/LlmProvider.h, src/LlmProvider.cpp   +ai::strippedOfThinking;
                                                 extractText scrub + fallback;
                                                 Provider.sendThinkFlag (Ollama
                                                 opts in, nobody else)
    tests/test_nlp.cpp                           +6 (scrub both dialects; unclosed
                                                 drop; fallback both spellings;
                                                 content-beats-reasoning; flag
                                                 scoping; think-wrapped quick-add
                                                 still parses — the layering proof)
    include/Version.h                            25.2.0
    README.md                                    status line, AI section, counts (246)
    docs/design-addendum-provider.md             +§L — the three decisions
    docs/design-addendum-assistant.md            §D struck to a stub (first section
                                                 promoted OUT of the roadmap)
    docs/QUESTION_BANK.md                        V86–V88 — the bank grows again,
                                                 because the code did
    docs/SESSION_NOTES.md                        session entry; also corrects the
                                                 previous entry's "V83 remains the
                                                 tail" (it was V85) — visibly, not
                                                 silently
    diagrams/README.md                           indexes the new diagram
    NEW: diagrams/extract_text_flow.puml/.png/.svg   the extraction flow

## Verified in-container
    246 green: 86 domain · 56 nlp · 19 taskmodel · 19 auth · 66 UI (+ live e2e)
    app + server both build warning-clean against Qt 6.4

## The acceptance test that ISN'T in this zip
The container proves the offline story (forged bytes through the pure
layer). The test this iteration exists for is `qwen3:8b` answering cleanly
on your machine — ideally with the §D Modelfile. If a tag other than
`<think>` shows up in the wild, the scrub is one QLatin1String and one test
away from learning it: deliberately narrow until reality votes.

## Next (§N)
v26's remainder: §C persona, §E per-role routing — with §B.4 (per-role verb
lists) and §E.5 (named seats) already amended into the plan.
