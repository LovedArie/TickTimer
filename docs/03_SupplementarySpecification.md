# Supplementary Specification — TickTimer

*Unified Process artifact · 03 · Construction update — reflects the shipped v11 app*

## Revision History

| Version | Date | Description | Author |
|---|---|---|---|
| Inception draft | 2026-07-03 | First draft, organised by FURPS+. | Mentor + you |
| Construction update | 2026-07-04 | Integrity rules extended (tasks, folders); reliability & format sections updated to the implemented mechanisms. | Mentor + you |

## 1. Introduction

This document is the repository for all requirements **not** captured in the use cases — chiefly the non-functional requirements (quality attributes). It is organised by the **FURPS+** categories so that each quality dimension acts as a checklist. Requirements are stated to be **specific and testable** wherever possible.

## 2. Functionality *(cross-cutting behaviour & rules)*

- **Single source of truth (derived data).** All day/week/month summaries are *computed* from the recorded time intervals; no separate totals are stored. A summary can never disagree with the raw data.
- **Referential integrity.** An activity used by any planned block cannot be deleted; a category can only be deleted once it contains **no activities and no tasks**; a folder only once it contains no categories. Every reference (activity→category, event→activity, task→category, category→folder) must point at an existing object at creation — references are never born broken.
- **Security.** The first release is single-user and local, requiring no authentication. Cross-device sync (future) will require a user account and a secure backend.

## 3. Usability *(human factors — the primary quality for this product)*

- The presentation must be **calm and non-shaming**: it credits every life-area (work, health, relationships, rest) and never labels healthy non-work time "unproductive."
- A first-time user can plan a day and start tracking time **without instructions**.
- Time is **colour-coded by category**, giving an at-a-glance read of where a day went.
- The interface respects **reduced-motion** preferences and maintains readable contrast.

## 4. Reliability

- A running focus/break timer's elapsed time **survives an unexpected close or crash** — implemented as a persisted running record refreshed by a **~30-second heartbeat**, converted into a real tracked interval on the next launch. At most ~30 seconds of the in-progress interval can be lost.
- A crash **during a save** must not corrupt the existing data — implemented with atomic write-then-replace (`QSaveFile`).

## 5. Performance

- The day view stays **responsive — no visible freeze** — while data is saved or loaded and while a timer ticks each second.
- Week and month reviews compute **fast enough to feel instant** for a normal person's volume of data (months of daily entries).

## 6. Supportability

- **Cross-platform:** a single C++/Qt codebase targets both **desktop and Android**.
- **Data format:** a **human-readable, versioned** JSON file (currently format v3; each growth was additive, so older files load unchanged), with a planned migration path to **SQLite** once data volume makes querying matter.
- **Configurable:** the calendar's visible window (currently 6 AM–midnight) is a default, adjustable by the user in a later iteration.

## 7. + (Constraints & Other)

- **Implementation constraint:** built with **C++** and the **Qt** framework.
- **Future capability:** cross-device **sync** via a supporting Sync service — the project's largest technical risk (see **Risk List, 05**); requires an account and a backend.
- **Legal / privacy:** the user's personal time data stays on their own device(s); no third-party sharing.

---

*Note: Kept lightly developed, as the UP advises during inception — enough to surface the quality attributes that shape architecture (reliability, cross-platform, sync) without over-specifying. It will be refined during elaboration.*
