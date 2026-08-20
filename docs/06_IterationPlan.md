# Iteration Plan — TickTimer

*Unified Process artifact · 06 · Roadmap review — reflects the shipped v21.2 app*

## Revision History

| Version | Date | Description | Author |
|---|---|---|---|
| Inception draft | 2026-07-03 | First draft; Iteration 1 detailed, rest sketched. | Mentor + you |
| Construction update | 2026-07-04 | Iterations 1–5 completed; two unplanned feature iterations recorded; backlog re-planned. | Mentor + you |
| Roadmap review | 2026-07-18 | Arcs v12–v21.2 recorded (§3c); backlog re-planned around the AI/secretary direction (§4a); model/view and sync retired; "tasks meet the plan" and Android flagged. | Mentor + you |

## 1. Introduction

The near-term plan, **iteration by iteration**. Each iteration is a short (~1–2 week) timeboxed cycle that ends in a **tested, runnable** (partial) application — not a throwaway. Iterations are **risk- and dependency-driven**: architecturally significant and high-risk work comes early; well-understood features come later. Only Iteration 1 is planned in detail; the rest are sketched and will be refined as we go, per the UP's "plan the next iteration in detail, outline the rest" guideline.

## 2. Iteration 1 — Foundation *(completed)*

- **Duration:** ~2 weeks.
- **Goal:** a runnable Qt application where you can create, edit, and delete **categories and activities**, and have them **saved and reloaded** from disk.
- **Use cases:** UC3 — Manage Activities & Categories.
- **Why this first:** activities are the *dependency root* — you cannot plan a day without them. It also lets us prove the scariest *foundational* unknowns early: that the Qt project builds and runs, and that data persists across restarts. Getting save/load working now de-risks everything downstream.
- **Key tasks:**
  1. Set up the Qt project (build system, main window).
  2. Implement the `Category` and `Activity` data types.
  3. UI to create / edit / delete categories and activities, enforcing the referential-integrity rules from the Glossary.
  4. Save to and load from a JSON file.
  5. A manual test pass against the goal above.
- **Risks addressed:** #3 (C++/Qt learning curve — start small and concrete) and #5 (persistence — prove it early).
- **Outcome:** delivered — and every later iteration landed on this foundation unchanged, which is what a good foundation looks like.

## 3. Iterations 2–5 *(completed)*

| Iteration | Goal | Use case | Outcome / note |
|---|---|---|---|
| **2 — Plan a Day** | Place activities onto a daily calendar of time slots. | UC1 | ✅ Custom-painted agenda (30-min slots, 6 AM–midnight); model/view deferred as a deliberate later lesson — rebuild-on-change chosen for v1 |
| **3 — Track Time** | Focus/break timer producing tracked intervals; plan vs. actual. | UC2 | ✅ State machine + **heartbeat crash insurance**, recovery proven by simulated-crash test |
| **4 — Review** | Daily chart, then week/month reviews. | UC4 | ✅ All derived live from segments; hand-painted charts (no chart dependency) |
| **5 — Pomodoro & polish** | Standalone Pomodoro; refinement and bug-fixing. | UC5 | ✅ Plus theme work: soft palette, dark-mode fix, slim scrollbars |

## 3b. Unplanned feature iterations *(completed — the process flexing, not breaking)*

| Iteration | Goal | Use case | How it entered the plan |
|---|---|---|---|
| **6 — Tasks & Deadlines** | One-off obligations with optional due dates; the Upcoming view. | UC6 | Owner request ("screenshot as spec") → **design addendum #1** → domain, tests, UI |
| **7 — Organizing & Special Days** | Folders in the rail; special days with countdowns. | UC3, UC7 | Owner requests → classified (domain vs. derived) → **design addendum #2** |

## 3c. Arcs completed since *(v12 – v21.2)*

Feature work stopped arriving as numbered iterations and started arriving as
**arcs** — a run of sessions on one theme, each shipping a version, a design
addendum, tests, and a diagram. Recorded here so the plan matches reality.

| Arc | Versions | Outcome |
|---|---|---|
| **Accounts, login, sync, share & compare** | v12–v18 | ✅ Risk #1 (sync) retired: self-hosted `ticktimer-server`, device sync, peer planner compare |
| **Daily-driver polish** | v19.x | ✅ Settings, recurrence, block alarms, Pomodoro maturity, update notices |
| **Model/View** | v20.0–v20.3 | ✅ The deferred Qt lesson, in four sessions: pipeline → granular diff → second model for contrast → shared `TaskSnapshotModel` base |
| **Diagrams → PlantUML** | *(no bump)* | 🔨 **Partly done, and this row overclaimed for six versions.** 11 diagrams re-authored and `_style.puml` shared — but "Mermaid retired" was never true. The v26.8 audit found **15 live `.mmd` files**, converted the six that were indexed as canonical and had *never once been rendered*, and recorded the remaining **9** as tracked debt in `diagrams/README.md`. Status claims are promises that somebody will eventually `ls` the folder |
| **Quick-add / AI** | v21.0–v21.2 | ✅ Pure NL parser → global Ctrl+N capture overlay → LLM fallback (wire/pure split). *Marked "in progress" until the v26.8 audit; it finished at v21.2 and the arc it grew into (provider → chat → foundation) has its own rows below.* |
| **Catch-up & settings nav** | v26.1–v26.8 | ✅ Settings as shell + pages (v26.1); catch-up domain then surfaces (v26.2, format **v11**); the chip's three intensities (v26.7); horizon default dropped to 3 days (v26.8). **Shipped with no session notes at all** — reconstructed from the addenda during the v26.8 audit |

## 4. Beyond These Iterations *(the re-planned backlog)*

### 4a. The AI arc — the current direction

The stated destination is an **AI assistant / secretary**: it chats with you,
asks how the day went, plans tomorrow, and makes decisions on your behalf.
Technically that is a *chat loop plus tool use* — the model converses and emits
function calls (`addTask`, `addEvent`, `moveBlock`) that the app executes
against `AppData`. TickTimer is unusually ready for it: the domain API is
already rule-enforcing and test-guarded, so the model gets a safe steering
wheel instead of raw data.

| Step | State | Note |
|---|---|---|
| Pure parser + `ParsedTask` contract | ✅ v21.0 | The vendor-neutral contract everything downstream depends on |
| Capture surfaces (Activities input, global Ctrl+N overlay) | ✅ v21.1 | Three surfaces, one parse |
| LLM fallback — wire/pure split, key at request time | ✅ v21.2 | `LlmQuickAddClient` (wire) + `nlp::llm` (pure, fully tested offline) |
| **Provider layer** | ✅ v24.0 | Provider = *base URL + dialect + model + key*. Two dialects cover the world: OpenAI-compatible (`/v1/chat/completions`, `Bearer`) and Anthropic (`/v1/messages`, `x-api-key`). Makes the vendor a dropdown — cloud **or** a local Ollama/LM Studio server on `localhost` |
| Chat panel | ✅ v25.0 | The Assistant page: transcript with a character-budget window, day briefing rebuilt at every turn (`brief::`), read-only by contract and by test. §C revisited: multi-turn is still data, not state — `chatRequestBody` is one switch and the one-shot body now delegates to it; streaming/tool transcripts remain the promotion trigger (chat addendum §E) |
| **AI foundation** | ✅ v25.2–v26.0 | Shipped as three drops: **reasoning models** (v25.2 — the `<think>` scrub, the reasoning-field fallback, `think:false` scoped to Ollama; provider addendum §L), **persona** (v25.3 — four prompt bands, two locked; non-shaming promoted to a floor; chat addendum §K), **per-role routing** (v26.0 — Unreachable-only fall-through, breaker, named seats, migration by derivation; provider addendum §M) |
| **Subtasks & sizing** | ✅ **shipped v28.3.0** (format v13 — re-landed FRESH against the v28.2 tree, as this row demanded; the old v27 zip was never applied) | **No AI.** `parentId` one level deep enforced at the door; five query policies (workload parents-only, `tasksDueOn` shows dated pieces, `taskCountIn` counts everything because it guards); no completion roll-up; archive cascades both directions; delete cascades with title-demotion; `estimateMinutes` + `chunkable` through one `setTaskSize` door; dialog stays a pure question (seed/apply free-function pair, one `AppData::Batch`); "☑ 2/5" chip on Upcoming cards; orphan adoption at the load door. Design record: `design-addendum-subtasks.md`. **Done — see the Sizing intelligence row.** |
| **Proactive assistant** | ✅ v28.0–28.2 shipped — the v28 arc is COMPLETE | **Still read-only.** v28.0: the whole pipeline with NO model in it — `afford::` verdict (planned-blocks proxy, honest Unknown), §F.3 manners, `AffordabilityService`, TIGHT pill, `ToastSpec` seams; sentences in C++. §O.1 closed: volunteer-mode. ✓ **28.1:** the model phrases the `Report` (one seat, mechanical accept gate, C++ voice as guarantee; briefing gains DEADLINE PRESSURE — §H.1's founding question answerable in chat). ✓ **28.2 part 1 (domain):** Mood (**format v12** — the collision thread closed in the version comment), one-per-day upsert, 14-day trim on the midnight knock, the stingy gate (morning ∧ once ∧ heavy), coarse-only briefing line with the note pinned OUT by test. ✓ **part 2 (28.2.1):** CheckInService knock (marked at emit), toast action → chat, one-tap mood buttons (no model in the loop — §E.4 by subtraction), and the two-wall fix for part 1's mood-briefing leak (includeMood default-false + ai::isLocal, all-local routes only). Per-role primaries arrive with the model's entry into the check-in. — Nudges on existing triggers, the morning check-in, mood (coarse, 14 days), affordability *phrased* by the model. Living with its judgment before granting it a write |
| **Sizing intelligence** | ✅ **shipped v28.4.1** (pure queries, format stays v13) | **No AI.** `afford::personalMultiplier()` — the median of actual÷estimate over finished, tracked, estimated tasks (1.0 under 3 samples; clamped [0.5, 3.0]; derived never stored); affordability rewired **estimate-first** (estimate × rate − tracked) with the planned-blocks proxy as fallback; **Unknown shrunk** to "no estimate AND no blocks"; an unsized parent borrows the sum of its pieces' estimates; sentences/nudges/briefing name their basis. Diagram: `affordability_sources.*`. |
| **The piece's own panel** | ✅ **shipped v28.5.0** (UI-only, format stays v13) | **No AI, no domain change.** A checklist piece's title opens the piece's own full detail panel (date/time/size) with a ‹-breadcrumb back to the parent; rows show an “Aug 8 · 45 min” chip; `runTaskDetail` consolidates the four seed/exec/apply call sites and owns the navigation loop — the one seam the future side-panel slice swaps. Closes the polish headline (no UI door to date a piece). Diagram: `piece_detail_sequence.*`. |
| **The docked detail panel** | ✅ **shipped v28.6.0** (UI-only, format stays v13) | **No AI, no domain change.** `TaskDetailForm` extracted (one form, two containers); `TaskDetailPanel` docks at [nav][pages][panel] with slide animation; navigation = guarded swap-in-place; **explicit save** (owner decision — lit button, “Saved ✓” flash, Save/Discard/Stay on every exit over dirty work, Enter never discards); modal dialog kept as the fallback — the whole existing suite passed through the wrapper unchanged. **v28.6.1 (same-day owner feedback):** docked → OVERLAY — out of the layout, scrim-dimmed background, click-away close through the same guard, 440 px. Diagram: `detail_panel_states.*`. |
| **Pieces in the list** | ✅ **shipped v28.7.0** (UI + display policy, format stays v13) | **No AI, no domain change.** TickTick-style: right-click → “Add a piece” (create-first, title pre-selected in the panel); pieces render as indented rows under their parent in the category list — real rows, so tick/chip/✕/open all work. §D amended for DISPLAY only; counting queries still parents-only; interleave after the parent sort so families never split. One level enforced in domain + chrome. |
| **The size ladder** | ✅ **shipped v28.8.0** (UI-only, format stays v13) | **No AI, no domain change.** Estimate spinbox → dropdown: 15/30/45m, half-hours to 8h, whole hours to 16h; cap-as-doctrine (past two workdays: break it into pieces); off-ladder values insert at their sorted rung (opening ≠ editing); `minutesLabel` becomes the one duration dialect (dropdown, piece chip, planner). **v28.9.1:** popup capped at 6 scrolling rows (setMaxVisibleItems — a hint native popup styles may ignore; QA checks on real Windows). |
| **Promotion** | ✅ **shipped v28.9.0** (domain arithmetic, format stays v13) | **No AI.** One trigger, the DATE: a dated piece answers for itself, its minutes leave the parent (sized: subtract, floor 0 → proxy; unsized: borrow only undated). Closes the double-count v28.7 made reachable; `Report.minutesPromoted` is the ledger. v28.4 borrow test green unchanged. Diagram: `piece_promotion.*`. |
| **The debug seams** | ✅ **shipped v28.10.0** (the field-report slice, format stays v13) | **No AI, no domain change.** Everything the first field report ordered, before v29 needs it: the Ctrl+Shift+D panel (pure glass over existing seams — frozen clock, sweeps, the check-in rehearsal that spends nothing, manners resets, a live briefing viewer); the briefing's three content fixes (DAY STATUS as a computed fact, PLAN FOR TOMORROW, day totals disambiguated — chat addendum §C.1's fifth rule: *computed facts are stated, never implied*); assistant markdown renders; `TICKTIMER_AI_DOWN=*` + the hook reaching every wire (the v28.0 voice becomes audible). Recipes: `docs/TESTING.md`; diagram: `debug_seams.*`. |
| **Tool use — the write boundary** | ✅ **the v29 arc is COMPLETE, four drops** — Slice 1 v29.0.0 (the machine, model-less) · Slice 2 v29.1.0 (the intake interview) · Slice 3 v29.2.0 (the reschedule verb) · v29.3.0 (the split's inverse; format v13 → **v14**) · **v30.1.0 (`Verb::UndoMove`)** | Where the secretary becomes real: proposal → guarded doors → **your tap** → effect. Opened with **intake** (purely additive, self-confirmed, tiny blast radius), then **rescheduling** — `Verb::MoveBlock`, fenced to blocks the domain already judges **missed**, and to placements `reschedule::propose()` is offering *right now*: the model selects, it never invents a time. No destructive verbs. v29.3 was the domain iteration §H.2 owed: `movedToId` → `movedToIds`, because the defect was the link's **cardinality** not its direction, which is what finally made a SPLIT invertible. **§B.3's dialect promotion did NOT fire** — three iterations, three recorded non-firings, criterion still armed. **v30.1 closed a gap rather than adding a feature:** §B.1 promised "no undo button, because every verb has an inverse it can ALSO call", and `undoReschedule` had no caller for two versions, so a move the assistant made could not be reversed by anyone. `UndoMove` carries NO fields — C++ decides which move, from `verbs::World` — so the model cannot aim it by construction. §B.1 amended in the same breath: the promise was false for `SetTaskDetails` too, and a clearing verb is withdrawn rather than built. Records: `design-addendum-write-boundary.md`, `-intake.md`, `-reschedule-verb.md`, `-split-inverse.md`, `-undo-verb.md` |
| **Memory** | ✅ **v30.0.0 shipped READ-FIRST** (a sidecar file; `data.json` untouched at v14) | The residue file (§L): only what the app *cannot* derive, **replaced not appended**, in four fixed sections in `memory-<username>.md` — plain Markdown the owner can edit by hand, which is the trust feature the sidecar was chosen for. Told to the assistant every turn as a fifth prompt band **below both locked bands**, with contract rule 4 classing it as information and never instruction. **No model write path yet, deliberately:** memory would be the first thing a model writes that a model later *reads as prompt*, so §L.4's confirm-loop half is designed against evidence in v30.1 — `AssistantVerbs.h` is untouched by this slice. Nudge and check-in stay memory-free (evidence-first, not permanent). Record: `design-addendum-memory.md` |
| **Cross-platform — phones** | 🔶 **Phase 1 shipped v30.2.0** (offline start + remembered devices); **Phase 2's code side v30.2.1** (bind localhost by default, invite-gated registration, a login brake, CORS preflight — plus `deploy/` templates; the VPS itself is the owner's to stand up); **Phase 3 shipped v30.3.0** (the Android version seam DELETED rather than pinned — CMake derives both stamps from `Version.h`; keystores gitignored; the APK served by Caddy at `/download/`, never by our own parser); Android already ports, iOS is Qt for WebAssembly | **Never a store** — sideloaded APK for Android, a WASM PWA served from our own server for iOS (Xcode ruled out). A JS front-end is explicitly NOT the answer: the server keeps the planner an opaque blob (`sync` §D), so a web UI would re-implement `AppData` and every pure brain in another language. **v30.2** closed the blocker — the app no longer needs a reachable server to open, and a remembered device signs itself in without a password. Remaining phases: standing the VPS itself up (the code side is done; `SERVER.md` now DESCRIBES the hardened deployment rather than forbidding it); and push (Phase 4, the WASM build, shipped v30.4.0 — it compiles and serves at 5.8 MB gzipped, but nobody has opened it in a browser yet; docs/WEB.md leads with that). Push on iOS is real (Safari 16.4+, Home-Screen web apps) via a narrow uploaded `{when, title}` schedule the server fires blindly — which also fixes Android, where `BlockAlarmService`'s in-process timer is equally dead when the app is not running. Record: `design-addendum-offline-and-devices.md` |

**Doc drift fixed 2026-07-20:** this section used to argue "why the provider
layer is next." It shipped in **v24.0**, and the chat panel in **v25.0** — the
paragraph had outlived its subject. Grep, not memory.

**Why this order (see `design-addendum-assistant.md` §N):** v26 is unblocked and
independent. v27 contains **no AI at all** — it is a domain iteration that
happens to unlock two AI features, and would be worth building if the assistant
never shipped. v28 stays **read-only** on purpose: the cheapest way to find out
whether the assistant's judgment deserves a write is to live with it proposing
for an iteration first. Only estimation strictly requires subtasks — if nudges
are wanted sooner, v28 can precede v27 at the cost of affordability using the
planned-blocks proxy for one iteration.

**The spine of the whole arc:** *code decides **when**, code computes **what is
true**, the model only **phrases**.* It drew itself three times independently
(triggers, verdicts, wording), and it is what keeps the assistant cheap,
testable, offline-capable, and unable to interrupt on its own judgment.

**On local models (checked 2026-07-18):** GLM-5.2 (Z.ai, released 2026-06-16)
is genuinely open — MIT-licensed weights, no regional or commercial
restriction. But it is a 744B mixture-of-experts model: only ~40B parameters
fire per token, yet **all** 744B must sit in memory. Unquantized ≈ 1.5 TB;
Unsloth's 2-bit dynamic GGUF ≈ 239 GB (a 256 GB Mac Studio or 4× RTX 3090 with
192 GB RAM) at ~3–9 tok/s. Not a laptop model, and too slow for conversation
anyway. The realistic local path today is a small (4B–14B) model via Ollama or
LM Studio, both of which expose an **OpenAI-compatible endpoint on localhost** —
i.e. exactly the same plumbing as a cloud provider. Cloud GLM-5.2 stays cheap
(~$1.40/$4.40 per Mtok first-party; ~$0.93/$3 via OpenRouter) if quality
matters more than privacy. The ladder changes; the provider layer does not.

### 4b. Still open from the original backlog

- ~~**Tasks meet the plan**~~ — **stale entry, corrected 2026-07-18:** this
  shipped long ago (task blocks via the picker, task-linking on existing
  blocks — `design-doc.md §5` retired it as "done twice over"; this bullet
  was doc drift). The *real* remaining gap it pointed at — the app never
  **noticing** that an urgent task has no block — is now its own arc:
  **"Needs a block"** (`design-addendum-needs-a-block.md`). **All three parts
  shipped** — domain (v21.3), gated glance panel (v21.4), placement +
  week-view card (v21.5); the arc is complete. Still relevant to the secretary:
  a model proposing "block the lab report Thursday?" will read
  `tasksNeedingBlock` and write through the same doors.
- **Android build** — its own iteration once desktop is polished. Code is
  Android-ready and `ANDROID.md` is written, but ⚠️ **no APK has been built or
  deployed**; the README has claimed "Android-ready" for many versions on the
  strength of the build config alone. Deserves a short reality-check session.
- **Polish & habits** — drag-and-drop into folders; remember window/sidebar
  state (`QSettings`); debounced saves.
- **SQLite migration** — the format is versioned and ready; no pressing need
  while JSON holds up.

### 4c. Retired

- ~~**Model/view refactor**~~ — ✅ delivered in full across v20.0–v20.3.
- ~~**Sync spike, then a sync iteration**~~ — ✅ risk #1 retired; sync, accounts,
  and share & compare all shipped.

---

*Note: this plan is re-planned at the start of each iteration, using the re-ranked Risk List and what the previous iteration taught us. Early iterations lean toward architecturally significant and risky work; later ones fill in well-understood features. This is the direct link back to the Risk List — the highest risks decide what we prove first.*
