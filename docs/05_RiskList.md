# Risk List — TickTimer

*Unified Process artifact · 05 · Construction update — reflects the shipped v11 app*

## Revision History

| Version | Date | Description | Author |
|---|---|---|---|
| Inception draft | 2026-07-03 | First draft, ordered by severity. | Mentor + you |
| Construction update | 2026-07-04 | Re-ranked after v11: learning-curve and data-loss risks largely retired; scope-creep mitigation upgraded (addenda process). | Mentor + you |

## 1. Introduction

A **living, prioritised** list of the project's risks — anything that could delay, derail, or degrade it — each with an **impact**, a **likelihood**, and a **mitigation**. Ordered highest-severity first (severity ≈ impact × likelihood) and re-ranked at the start of each iteration, as the UP is risk-driven: the biggest risks should be attacked early. Risks are not only technical — scope, schedule, and skill risks are included, because those are what most often kill projects.

## 2. Risks *(highest severity first)*

| # | Risk | Impact | Likelihood | Mitigation / plan |
|---|---|---|---|---|
| 1 | **Cross-device sync** — offline conflict resolution, a backend, and accounts. | High — could derail scope. | High. | Keep out of the first release; run a small throwaway **spike** to learn the hard parts early; give it its own late phase; prefer a hosted backend over building one. |
| 2 | **Scope creep** — the feature set keeps growing (we have felt this first-hand). | High — the project never ships. | Medium *(reduced)*. | **Working as designed:** two unplanned feature sets (tasks; folders/upcoming/special days) were absorbed via the **design-addendum process** — classify the request, document choice/why/rejected, fence the scope — and shipped without derailing. New ideas still go through an addendum, never straight to code. |
| 3 | **C++/Qt learning curve** — the developer is new to both the language and the framework. | Med–High — progress stalls. | Low *(largely retired)*. | v11 shipped: full app, 19 automated tests, layered architecture. Mentored learn-by-following continues; remaining depth (model/view, QSettings, SQLite) is scheduled as deliberate lessons, not blockers. |
| 4 | **Android UI adaptation** — touch input and small screens differ from desktop. | Medium. | Medium. | Design with adaptive layout in mind from the start; test on Android **early**, not at the end; avoid desktop-only assumptions. |
| 5 | **Data loss / persistence bugs** — the user loses tracked time. | Medium — erodes trust in the tool. | Low *(mitigations implemented & tested)*. | Shipped: atomic saves (`QSaveFile`), heartbeat crash insurance with recovery on launch, and automated round-trip + crash-recovery tests. A rename even shipped with a **data migration** rather than stranding the old file. |
| 6 | **JSON → SQLite migration** — moving existing data when storage changes. | Low–Medium. | Medium. | The file format is **versioned** (now v3) and has already survived two additive growths plus one location migration — the migration muscle exists. Defer SQLite until data volume justifies it. |

## 3. Notes

The list is ordered by severity and will be **re-ranked every iteration** as risks are retired or new ones surface. The top risks should drive the plan — which is exactly why **sync**, our #1, is deliberately sequenced last in the build while its uncertainty is reduced by a small early experiment. Note that risk #2 (scope creep) is on this list because we genuinely encountered it during prototyping — and the mitigation (locked scope + non-goals) is the process working as intended.
