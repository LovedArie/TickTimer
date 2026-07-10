# Design Addendum — Tasks (v10)

*Status: **approved by use (v10)** and **merged into `design-doc.md` v3** on 2026-07-04. Kept as the historical record of the decision.*

**Origin of the requirement:** the owner's TickTick screenshot ("screenshot
as spec"). What we extracted from it: one-off, completable obligations with
an optional due date ("Lab 4 — 5% — DATE TBD"), grouped under life areas.
What we deliberately did NOT extract: tags, recurrence, reminders (see
scope fences below).

---

## §2 addition — the Task concept

- **Task** — a one-off, completable obligation: "Lab 4", "LOG410 FINAL".
  Holds a `title`, a `done` flag, an *optional* `dueDate`, and belongs to
  exactly one `Category`.

```plantuml
Task "0..*" --> "1" Category : belongs to >
```

Note what Task is deliberately **not** connected to: Activities and Events.
A task is not an instance of any reusable activity type, and planning a
task onto the calendar is future work (below), not a v1 relationship.

## §3.9 Task as its own class, not fields on Activity

**Choice:** a new `Task` concept.
**Why:** an Activity is a reusable *type* shared by every Event that
references it (§3.4). Putting `done`/`dueDate` on it fails the two-way test
of a domain model — *it must be able to express every true state and unable
to express impossible ones.* One shared "Gym" object cannot say "Lab 4 done,
Lab 5 not" (a truth it can't express), and marking it done would mark every
past and future gym block done (a falsehood it happily expresses).
Different lifecycle → different concept → its own class (same rule that
earned Category a class in §3.6).
**Rejected:** `bool done; QDate dueDate;` on `Activity`.

## §3.10 Task belongs to a Category directly

**Choice:** `Task.categoryId`, no Activity in between.
**Why:** "Lab 4" is not an occurrence of any reusable activity type — it is
its own thing that simply lives in a life area. Forcing every task through
an Activity would make users invent fake activity types ("Misc school
task") just to satisfy the model.
**Rejected:** Task → Activity → Category indirection.

## §3.11 Optional due date as an invalid QDate

**Choice:** `dueDate` is a `QDate`; `!isValid()` means "no date yet" — the
screenshot's "DATE TBD" state, first-class.
**Why:** QDate already carries a built-in "absent" state; a parallel
`bool hasDueDate` would be a second source of truth that could disagree
with the first.
**Rejected:** a separate has-date flag.

## Integrity rule extension (Supplementary Spec)

A Category may be deleted only when it contains **no Activities and no
Tasks**. Same law, one more citizen; enforced in `AppData::removeCategory`
like all the others.

## Persistence

A new top-level `"tasks"` array in the JSON file; format version bumps
1 → 2. The change is *additive*, so version-1 files load unchanged (a
missing key reads as empty — no migration needed). We bump the number
anyway: it costs nothing today and lets any future reader that must care
tell the files apart.

## Scope fences (v1 excludes — future features, not oversights)

- Tags, recurrence, reminders (straight from the screenshot, straight to
  the backlog).
- **Planning a task onto the agenda** — the exciting one: it would marry
  tasks to the app's plan-vs-actual soul, and deserves its own design
  session rather than a rushed foreign key.
- Linking tasks to activities or events.
