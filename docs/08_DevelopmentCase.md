# Development Case — TickTimer

*Unified Process artifact · 08 · Artifact review at v31*

## Revision History

| Version | Date | Description | Author |
|---|---|---|---|
| Inception draft | 2026-07-03 | First draft; process tailored for a small solo project. | Mentor + you |
| Construction update | 2026-07-04 | Recorded practices that emerged: design addenda, automated domain tests, approval-by-use. | Mentor + you |
| Artifact review | 2026-09-08 | Declared the two artifacts that had grown outside this document (Backlog, QA Checklist) and retired the thirty-three that had grown outside it unnoticed. See §6. | Mentor + you |

## 1. Introduction

The Unified Process is a **framework** — a large menu of possible artifacts and practices, meant to be **tailored** to each project. This Development Case records our tailoring decision: *which* artifacts we use, *how lightly*, and *when*. Because this is a **small, solo, learning / portfolio project**, the case is deliberately lightweight — enough process to think clearly and reduce real risk, never so much that documentation outweighs the code.

## 2. Artifacts We Use

Legend: **s** = started · **r** = refined · **–** = not used in that phase.

| Artifact | Used? | Detail | Incep. | Elab. | Const. | Trans. |
|---|---|---|---|---|---|---|
| Vision (01) | Yes | Light | s | r | – | – |
| Use-Case Model (02) | Yes | Light — 2 detailed, rest brief | s | r | – | – |
| Supplementary Specification (03) | Yes | Light | s | r | – | – |
| Glossary (04) | Yes | Light | s | r | – | – |
| Domain Model | Yes | Sketch (already drawn) | s | r | – | – |
| Risk List (05) | Yes | Living, re-ranked each iteration | s | r | r | r |
| Iteration Plan (06) | Yes | Next iteration detailed | s | r | r | r |
| Phase Plan (07) | Yes | Coarse | s | r | – | – |
| Development Case (08) | Yes | This document | s | – | – | – |
| **Backlog** (`BACKLOG.md`) | Yes | **The only queue.** One line per item, ranked, deleted on ship | – | – | r | r |
| **QA Checklist** (`QA_CHECKLIST.md`) | Yes | Living: a standing pass, plus a per-release part rewritten each time | – | – | r | r |
| Design Model | Informal | Sketched as needed, in code/on paper | – | s | r | – |
| Software Architecture note | Minimal | A short note; the project is small | – | s | – | – |
| Data Model | Informal | JSON schema now; SQLite later | – | s | r | – |

## 3. Artifacts We Deliberately Skip

- **Business / Domain Rules** — no external domain rules (tax laws, regulations, physics) apply, so there is nothing to record separately.
- **Formal Deployment & Test Model documents** — overkill for a solo desktop app. *(Updated in construction: testing did NOT stay manual — an automated **QTest domain suite** (19 tests) grew alongside the code and runs headless; the UI layer is exercised manually plus by a screenshot tool. The formal documents stay skipped; the tests themselves are the artifact.)*

Choosing *not* to produce an artifact is a real decision — recording it here keeps the omission intentional rather than accidental.

## 4. Practices We Adopt

- **Iterative & incremental** development in short (~1–2 week) cycles.
- **Prototype first** — already done: a throwaway HTML prototype de-risked the UX before any C++ was written.
- **Risk-driven planning** — the Risk List decides iteration order.
- **Mentored, learn-by-following** pairing; automated domain tests run at every change, manual UI pass at iteration boundaries.
- **Design addenda for domain changes** *(adopted in construction)*: any request that adds a stored fact is first classified (presentation / derived view / domain change), then documented as an addendum (choice → why → rejected, with scope fences) **before** code. Two unplanned feature iterations shipped through this gate.
- **Approval-by-use, recorded**: when a reviewed-in-silence document is built upon, its status is marked "approved by use" with the version — tacit acceptance is fine, *unrecorded* acceptance is not.

## 5. Rationale

For one developer building a portfolio app, heavy process would cost far more than it returns. The guiding rule: **use the least process that still keeps us thinking clearly and reduces real risk.** This document is the meta-decision that kept the other seven artifacts appropriately *thin* rather than exhaustive. If the project grows — a team, real users, cross-device sync — this is the first document we would revisit, adding artifacts only as the project earns them.

## 6. Artifact review at v31 — what grew here unasked

By v31 `docs/` held **108** Markdown files. Thirty-three of them had never been
declared in §2: thirty `CHANGES_v*.md` (one per version, recording what an
addendum, `SESSION_NOTES.md` and `git log` each already recorded) and three
`QA_CHECKLIST_v*.md` (one per slice, each a single-use script). All thirty-three
were retired into git history; the checklists' durable half was mined out first
and became Part 1 of the living `QA_CHECKLIST.md`.

Larman's rule for the UP is that **all artifacts are optional and none should be
created unless it adds value** — the pharmacy analogy: match the medicine to the
ailment rather than taking one of each. The failure here was not that anyone
disagreed with that. It was that artifacts were created *outside* this document,
which is the one place the tailoring decision is supposed to be made, so nobody
ever asked the question. **§3's principle cuts both ways: choosing not to produce
an artifact is a real decision, and so is choosing to produce one.**

Two artifacts already declared here had also quietly stopped being what they
claim:

- **Iteration Plan (06)** — declared *"next iteration detailed"*, had become an
  archive of finished arcs. Its §4 is now record only, and the queue it was
  pretending to be is `BACKLOG.md`.
- **Risk List (05)** — declared *"living, re-ranked each iteration"*, last
  touched at v11 and twenty versions behind. Unresolved on purpose: backlog item
  **H1** forces the choice between re-ranking it and formally retiring it here.
  A document that claims to be living and is not is worse than one that is
  honestly dead.

**The rule this review adds:** a record and a queue have opposite lifetimes and
never share a file. A record is written once and kept; a queue item exists in
order to be deleted. Every failure above is one file trying to be both.

---

*Note: The Development Case right-sizes the process. It is why the whole set reads as eight short, purposeful documents instead of a binder of ceremony.*
