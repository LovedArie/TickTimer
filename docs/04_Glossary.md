# Glossary — TickTimer

*Unified Process artifact · 04 · Construction update — reflects the shipped v11 app*

## Revision History

| Version | Date | Description | Author |
|---|---|---|---|
| Inception draft | 2026-07-03 | First draft, from the domain model. | Mentor + you |
| Construction update | 2026-07-04 | Renamed **TickTimer**; added Task, Folder, Special Day, Derived view; added the UI component vocabulary. | Mentor + you |

## 1. Introduction

This defines the noteworthy terms of TickTimer so every stakeholder uses them the same way. It also serves as a lightweight **data dictionary**: where useful, entries record a term's data format, validation rules, and relationships. Per Larman (*Applying UML and Patterns*, ch. 7), the ranges and validation rules recorded here are themselves **requirements**, because they constrain the system's behaviour.

## 2. Domain terms

| Term | Definition | Format / data | Rules & relationships | Aliases |
|---|---|---|---|---|
| **Category** | A life-area that groups activities and tasks (e.g. Work/Study, Health, Rest). | `name`: text; `colour`: colour value; `folderId`: optional | `name` non-empty; deletable only when it contains **no activities and no tasks**; optionally lives in one Folder | life-area |
| **Folder** | A named grouping of Categories in the rail (e.g. "School"). | `name`: text | one level deep (no nesting); deletable only when it contains no categories | — |
| **Activity** | A reusable *type* of thing the user does (e.g. "Study Math", "Gym"). Never "done" — types have no completion. | `name`: text | belongs to **exactly one** Category; cannot be deleted while used by an Event | — |
| **Event** | A planned block placed on the calendar for a given day and time — the user's **intention**. | `date`; planned start/end stored as **minutes after midnight** (30-minute granularity; 1440 = midnight); `note`: text (optional) | references **exactly one** Activity; must **not overlap** another Event on the same day; up to 4 slots (2 h) | planned block, block |
| **Segment** | One continuous stretch of **actually-tracked** time inside an Event — the **reality**. | `kind`: {Focus, Break}; `start`, `end`: timestamp | `end ≥ start`; **at most one** Segment running at a time; **owned by** one Event (deleted with it) | tracked interval |
| **Task** | A one-off, **completable obligation** (e.g. "Lab 4"). The instance-twin of Activity's type — see design doc §3.9. | `title`: text; `done`: flag; `dueDate`: optional date | belongs to **exactly one** Category (directly — no Activity in between); an **invalid date is the "date TBD" state**, first-class | to-do |
| **Special Day** | A date that matters on its own: birthday, holiday, vacation start. | `title`: text; `date`; `repeatsYearly`: flag | relates to nothing; **next occurrence is derived**, never stored; Feb 29 in a common year resolves to **Mar 1** (design doc §3.14) | — |
| **Focus / Break** | The two kinds of Segment: focused work versus a pause. | enumeration value | — | — |
| **Plan vs. Actual** | The core distinction: an Event is the *plan*; its Segments are what *actually* happened. | (concept) | the gap between them reveals procrastination; rescheduling an Event never alters its Segments | — |
| **Derived view** | Any screen computed on demand from raw data, never stored: the glance panel, week/month reviews, **Upcoming**, special-day countdowns. | (concept) | "derive, don't store" (design doc §3.5): a stored summary can drift and lie; a derived one cannot | — |
| **Pomodoro** | A timed focus/break work cycle. | default 25 min focus, 5 min break; longer break every 4th round | standalone — records no Segments (v1) | — |

## 3. UI component names *(shared vocabulary for talking about the app)*

| Term | What it is |
|---|---|
| **Agenda** | The scrollable 6 AM–midnight day timeline where blocks live. |
| **Block** | An Event as drawn on the agenda (pastel fill, identity stripe, mini plan-vs-actual bar). |
| **Glance panel** | The day view's live sidebar: focus/break totals, category bars, encouragement line. |
| **View switcher** | The clickable period label ("Today") — click cycles Day → Week → Month. |
| **Rail** | The left tree on the Activities page: folders and life-areas (the *master* in master-detail). |
| **Detail pane** | The right side of the Activities page: the selected life-area's tasks and activities. |
| **Picker** | The choose-an-activity dialog opened by clicking a free slot. |
| **Event card** | A block's detail dialog: reschedule, timer controls, note, delete. |

## 4. Notes

The rules above (*no overlapping Events*, *`end ≥ start`*, the extended referential-integrity family) are behavioural requirements the implementation must enforce — and since construction, they are enforced in one place (the domain's aggregate root) and verified by the automated test suite.
