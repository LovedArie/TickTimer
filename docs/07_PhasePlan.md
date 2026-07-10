# Phase Plan — TickTimer

*Unified Process artifact · 07 · Construction update — reflects the shipped v11 app*

## Revision History

| Version | Date | Description | Author |
|---|---|---|---|
| Inception draft | 2026-07-03 | First draft; four UP phases mapped to iterations. | Mentor + you |
| Construction update | 2026-07-04 | Phase statuses updated: Elaboration complete; Construction well advanced (desktop feature-complete). | Mentor + you |

## 1. Introduction

The **zoomed-out**, whole-project schedule: the four Unified Process phases, their goals, roughly which iterations fall in each, and the milestone that closes each phase. Crucially, these are **not** waterfall stages — every phase iterates through requirements, design, code, and test. The phases differ by *emphasis* (what we are trying to achieve), not by *activity*. This is the coarse partner to the Iteration Plan (06), which details the near-term cycle.

## 2. Phases

| Phase | Emphasis / goal | Iterations | Ending milestone |
|---|---|---|---|
| **Inception** | *Scope it.* Is this worth building? Produce the Vision, key requirements, risk list, and a rough plan. | This planning work | **Lifecycle Objectives** — ✅ **reached** |
| **Elaboration** | *Prove it.* Build the architecturally significant and riskiest core with **real, production code**; retire the top foundational risks. | Iter 1 (foundation + persistence), Iter 2 (calendar), Iter 3 (timer + plan-vs-actual state machine) | **Lifecycle Architecture** — ✅ **reached**: layered core proven end-to-end, guarded by automated domain tests |
| **Construction** | *Build the rest.* Fill in the well-understood features on the now-proven architecture. | Iter 4 (reviews), Iter 5 (Pomodoro + polish), unplanned Iters 6–7 (tasks; folders/special days), + an Android build iteration | **Initial Operational Capability** — ✅ reached **for desktop** (v11); Android iteration still ahead |
| **Transition** | *Ship it.* Final testing, polish, release, and gathering real user feedback. | Release iteration | **Product Release** — in the user's hands |

## 3. Sync (deferred, off the critical path)

Cross-device **sync** — risk #1 — is scheduled *after* the first release: a small Elaboration-style **spike** to shrink its uncertainty, then a Construction iteration to build it. It deliberately does not sit on the path to the first shippable app, so the project can reach a working, releasable state without waiting on its hardest feature.

---

*Note: Phases are goal-driven, not activity-driven — production code is written and tested from **Elaboration** onward, never "once Construction starts." Dates are intentionally rough; the UP re-plans at each iteration boundary using the re-ranked Risk List. The milestone names (Lifecycle Objectives, Lifecycle Architecture, Initial Operational Capability, Product Release) are the standard UP phase-end gates.*
