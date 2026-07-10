# Iteration Plan — TickTimer

*Unified Process artifact · 06 · Construction update — reflects the shipped v11 app*

## Revision History

| Version | Date | Description | Author |
|---|---|---|---|
| Inception draft | 2026-07-03 | First draft; Iteration 1 detailed, rest sketched. | Mentor + you |
| Construction update | 2026-07-04 | Iterations 1–5 completed; two unplanned feature iterations recorded; backlog re-planned. | Mentor + you |

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

## 4. Beyond These Iterations *(the re-planned backlog)*

- **Polish & habits:** drag-and-drop into folders; remember window/sidebar state (`QSettings`); debounced saves.
- **Tasks meet the plan:** place a task onto the agenda — marrying deadlines to plan-vs-actual; deserves its own design session.
- **Model/view refactor** — the deferred Qt lesson, when list sizes justify it.
- **SQLite migration** — the format is versioned and ready.
- **Android build** — its own iteration once desktop is polished.
- **Sync spike, then a sync iteration** — still risk #1, still deliberately last.

---

*Note: this plan is re-planned at the start of each iteration, using the re-ranked Risk List and what the previous iteration taught us. Early iterations lean toward architecturally significant and risky work; later ones fill in well-understood features. This is the direct link back to the Risk List — the highest risks decide what we prove first.*
