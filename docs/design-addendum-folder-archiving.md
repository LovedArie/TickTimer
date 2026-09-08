# Design Addendum — Retiring a whole semester (v31)

*Status: **approved by direction (v31)**. Extends
`design-addendum-organizing.md` (v11), which introduced the Folder.*

**Origin:** two owner requests about the rail, one of which turned out not
to be a bug.

| Request | Classification | Why |
|---|---|---|
| Archive a whole folder, life areas included | **domain change** | a new stored fact on Folder |
| "A folder must be deletable — the delete button does nothing" | **UI honesty** | the rule was already right; the *reason* was invisible |

---

## §F.1 The delete button was not broken

`AppData::removeFolder` has refused a folder that still holds life areas
since v11, and the rail's menu disables the action to match. The owner's
"Summer 2026" holds four courses, so the item was greyed — which is
exactly the behaviour the same message asked for ("if it is not empty, the
button should be greyed out").

**What was actually missing was the reason.** A disabled item labelled
"Delete folder" is indistinguishable from a broken one, which is how it was
reported. The label now carries the count that blocks it — *"Delete folder
— move out 4 life areas first"*.

**Choice:** the reason goes in the **label**, not a tooltip.
**Why:** a tooltip needs a hovering pointer, and this menu is opened by
long-press on the phone, where there is no such thing. A tooltip there is a
message written in ink only a desktop can see.

**The deeper gap** was that a non-empty folder had no useful action at all:
the only way to retire a finished semester was to move four areas out by
hand and archive each one. That is §F.2.

## §F.2 Archiving cascades as a QUERY, never as a stamp

**Choice:** `Folder::archived`, plus `AppData::setFolderArchived`. Archiving
a folder hides it **and** every life area inside it — and writes **nothing**
to those areas. Visibility is answered by a new query,
`AppData::categoryHidden(c)`: hidden when its own flag is set **or** its
folder is archived.

**Why:** this is the third round of one idea — activities retire (v7), life
areas retire (v8), semesters retire (v31) — and `Category.h` already stated
the rule for round two: *"sets NO flags on children: restore the area and
everything returns exactly as it was."*

The cascade-as-a-stamp version is the obvious implementation and it loses
information. If archiving the folder wrote `archived = true` onto each
course, restoring the folder could not tell "this course was hidden because
the semester ended" from "this course was archived on its own beforehand" —
so restore would resurrect something the user had deliberately put away.
A derived answer cannot lose that distinction because it never stored one.

**Rejected:** the cascading stamp (above); and a `Folder::archived` that
hides only the folder while its areas float back up to the top level, which
is worse than doing nothing — the semester would appear to have dissolved.

**Consequence, accepted:** every list that used to test `c.archived`
directly must ask `categoryHidden` instead. There were three
(`ActivitiesPage::rebuildRail`, `PickActivityDialog`, and — one level down —
`taskHidden`). Three call sites is the whole cost of the second cascade
level, precisely because the first one had already been given a single home.

## §F.3 Archiving is not a back door to deletion

**Choice:** `removeFolder`'s emptiness test counts **every** life area the
folder holds, archived ones included, and archiving the folder itself
changes nothing about it.

**Why:** an archived area is hidden, not gone. Deleting its folder would
strand it — visible nowhere, restorable to nowhere. The guard counts what
*exists*, not what is *shown*, for the same reason `taskCountIn` counts
pieces the lists hide: a number that guards must see everything.

## §F.4 The Archive page reads widest-first

**Choice:** the Folders section sits above Life areas, above Tasks, above
Activities. Restore only — no delete, matching the Activities section.

**Why the order:** a user hunting a missing course should meet the folder
that is hiding it before scrolling past an empty "no archived life areas".
Restoring a folder brings back several areas at once, so it is the biggest
lever on the page and belongs at the top.

**Why no delete:** `removeFolder` refuses a folder that still holds areas,
and this page has never advertised a dead end (the same reasoning that
silences delete on archived activities). Empty it in the rail, then delete
it there.
