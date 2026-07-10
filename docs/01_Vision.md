# Vision — TickTimer

*Unified Process artifact · 01 · Construction update — reflects the shipped v11 app*

## Revision History

| Version | Date | Description | Author |
|---|---|---|---|
| Inception draft | 2026-07-03 | First draft. To be refined during elaboration. | Mentor + you |
| Construction update | 2026-07-04 | Renamed **TickTimer**; updated to match the shipped v11 application. | Mentor + you |
| Inception draft v2 | 2026-07-03 | Added cross-platform (desktop + Android) and cross-device sync goals. | Mentor + you |

## 1. Introduction

This is a short executive overview of the **TickTimer**: the problem it solves, who it is for, and its big ideas. Detailed behaviour lives in the **Use-Case Model (02)**; non-functional and other requirements live in the **Supplementary Specification (03)**. This Vision summarises and *points to* those rather than repeating them.

## 2. Positioning

### Problem statement

"How productive was I today?" is normally a vague feeling rather than something you can see. For a person whose focus is easily derailed — especially by anxiety-driven procrastination — that fog makes it hard to separate genuine effort from avoidance, and hard to stay motivated. The result is lost time, guilt, and no clear picture of where the hours actually went.

### Product position statement

*For* a single person who wants to understand and improve how they spend their time, *who needs* to see the gap between what they planned and what they actually did, *the* **TickTimer** *is a* personal desktop application *that* turns each day into a visible, colour-coded record of time by life-area and shows real focus-versus-break time within each planned block. *Unlike* a plain calendar or a bare to-do list, it measures **actual** focused effort against the **plan**, and credits every part of a balanced life — work, health, relationships, rest — rather than labelling non-work time "unproductive."

## 3. Stakeholders & Users

- **Primary user** — an individual tracking their own time to build self-awareness and motivation, across their own devices (desktop and phone). The first release is single-user and local; cross-device sync (a later goal) will require a lightweight account and a backend.
- No other stakeholders for this version.

## 4. Product Overview

A C++ / Qt application — targeting **both desktop and Android from a single codebase** — where the user plans a day in small time blocks (the exact granularity is pinned once, in the design doc §3.1), tracks real focus and break time inside each planned block, organises activities into life-area categories (grouped into folders), keeps one-off tasks with due dates and special days alongside the plan, and reviews how time was spent across day, week, and month. A longer-term aim is for the user's data to **sync across their own devices**, so the same picture is available on phone and computer.

## 5. Summary of System Features

The system does the following (high-level; each detailed in the Use-Case Model):

- Plan a day by placing activities into time blocks on a calendar.
- Organise activities into user-defined **categories** (life-areas), each with a colour — and group life-areas into **folders**.
- Track **actual focus time and break time** within a planned block — crash-safe.
- Show a live daily breakdown of time by category and focus-vs-break.
- Review time across **week** and **month**.
- Manage **tasks** with optional due dates, and see every **upcoming deadline** in one place.
- Track **special days** (birthdays, holidays, vacations), with yearly repeats.
- Run a **Pomodoro** focus timer.
- Save and reload the user's data locally (versioned, human-readable, safe writes).
- Run on **both desktop and Android** from one Qt codebase *(Android: future)*, with *(future)* cross-device **sync** — the largest technical challenge (see Risk List, 05).

*(Ten items — still graspable at a glance. The two marked future are goals, not shipped features; everything else is running in v11.)*

## 6. Summary of Other Requirements

Non-functional and quality requirements — local persistence, usability for an easily-distracted user, a calm and non-shaming presentation, cross-platform operation (desktop + Android), and eventual cross-device sync — are recorded in the **Supplementary Specification (03)**, not duplicated here. Cross-device sync in particular carries significant technical risk and is tracked in the **Risk List (05)**.

---

*Note: In the Unified Process this Vision is a lightweight first approximation, not a fixed contract. It was written after building a working prototype, and will be refined as the real application is built and tested.*
