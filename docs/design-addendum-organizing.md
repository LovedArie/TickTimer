# Design Addendum — Organizing & Upcoming (v11)

*Status: **approved by direction (v11)** and **merged into `design-doc.md` v3** on 2026-07-04. Kept as the historical record of the decision.*

**Origin:** three owner requests, classified before anything was built —
the discipline this addendum exists to demonstrate:

| Request              | Classification | Why |
|----------------------|----------------|-----|
| Folders in the rail  | **domain change** | folder membership must survive restart → it is a stored fact |
| Upcoming deadlines   | **derived view**  | a new *question* over existing Tasks (§3.5: derive, don't store) — zero new data |
| Special days         | **domain change** | birthdays/holidays are facts the app must remember |

---

## §2 additions — two new concepts

- **Folder** — a named grouping of Categories in the rail ("School").
  Holds only a `name`. One level deep, like the owner's TickTick.
- **SpecialDay** — a date that matters on its own: a birthday, a holiday,
  a vacation start. Holds `title`, `date`, and `repeatsYearly`.

```plantuml
Category "0..*" --> "0..1" Folder : lives in >
```

A Category *optionally* belongs to one Folder (`folderId`, empty = top
level). SpecialDay relates to nothing — it is a standalone fact.

## §3.12 Folder as a stored concept, not a name convention

**Choice:** a `Folder` class; Categories reference it by id.
**Why:** the tempting hack — encoding folders in category names
("School / LOG410") — smuggles a fact into a string: renames shatter it,
no rule can guard it, and every reader must parse it forever. Facts get
concepts; the rail merely *displays* the fact.
**Rejected:** name-prefix folders; unlimited nesting (one level covers the
owner's real usage; depth is complexity bought before it's needed).

## §3.13 Upcoming is a query, not a table

**Choice:** `AppData::upcomingTasks()` — undone, dated tasks, most urgent
first. The page recomputes on every change; nothing new is saved.
**Why:** §3.5. A stored "upcoming" list goes stale the moment any task
changes. The panel joins the glance panel and the week/month reviews as
the app's fourth *derived view*.
**Rejected:** persisting the grouped list.

## §3.14 Yearly repetition on SpecialDay, and the Feb 29 rule

**Choice:** a `repeatsYearly` flag plus `nextOccurrence(today)` — the
*next* occurrence is derived, never stored.
**Edge case decided now, not discovered in production:** a Feb 29
birthday in a common year resolves to **Mar 1**. Arbitrary but documented;
the alternative (Feb 28) is equally arbitrary and equally fine — what
matters is that the program has *one* answer, written down.
**Rejected:** storing next-occurrence (it's a derived fact); date *ranges*
for vacations (deferred — v1 enters a vacation by its first day).

## Integrity rule extension

A Folder may be deleted only when **no Category lives in it**. Moving a
Category to a nonexistent Folder is refused at the door
(`setCategoryFolder`), same as every reference in this model.

## Persistence

Additive again: top-level `"folders"` and `"specialDays"` arrays; each
category gains an optional `"folderId"`. Version bumps 2 → 3; v1/v2 files
load unchanged (missing keys read as empty).

## Scope fences (v1 excludes)

- Special days on the calendar/month grid — a future session marries them
  to the agenda properly.
- Vacation date ranges; folder nesting; drag-and-drop into folders (v1
  uses a right-click menu — a drag-drop upgrade is a fine exercise).
