# v26.8 — documentation & diagram audit

**No behaviour changed.** Two version numbers corrected, eleven diagrams
rendered for the first time, one new diagram, six Mermaid conversions, and
four documents brought back in line with the code.

Apply by unzipping over the project root. Nothing needs rebuilding for the
docs; the two version bumps take effect on the next build.

> **One manual step.** Unzipping adds and overwrites, but never deletes. The
> six converted Mermaid sources now live in `diagrams/legacy-mermaid/`, and
> their originals are still sitting in `diagrams/`. Delete those six so the
> folder has one source of truth per diagram:
>
> ```sh
> cd diagrams && rm -f model_refresh_decision.mmd \
>   activities_modelview_contrast.mmd glance_focus_states.mmd \
>   needsblock_height_bound.mmd window_memory_restore.mmd deadline_time_flow.mmd
> ```
>
> The archived copies under `legacy-mermaid/` are the ones to keep. Until you
> run this, the unindexed check in `diagrams/README.md` will report those six
> — which is the check doing its job.

---

## 1. Two real bugs

### `include/Version.h` — four releases stale

Said `26.0.0`. The tree contains catch-up (v26.2), the chip states (v26.7)
and `lookBackDays = 3` (v26.8 — it is right there in `MissedBlocks.h`).
Now **26.8.0**; the file's own `static_assert` guard was re-verified by
compiling it standalone.

**Why the guard didn't catch it, and why that's the interesting part.** The
guard proves the three macros agree with the version string — *internal*
consistency, which it does perfectly. "Is this the version the work is at?"
is an *external* fact and no compiler can see it. When a compile-time check
retires a class of bug, write down what it still cannot see, or its
existence gets mistaken for coverage it never had.

### `installer/ticktimer.iss` — five releases stale

Said `21.2.0`. Now **26.8.0**.

Not cosmetic: Inno uses `AppVersion` for upgrade detection, so the
install-over-the-top path — the one every returning user takes — was being
exercised against a false version.

The cause is sitting in one folder, which makes it the cleanest lesson here.
`Version.h` has three consumers:

| consumer | mechanism | result |
|---|---|---|
| C++ code | `#include`s the header | never drifted |
| `ticktimer.rc` | `#include`s the header | never drifted |
| `ticktimer.iss` | **a human retypes it** | drifted 5× |

The two mechanical consumers were right for five straight releases. The one
carrying a comment shouting `MUST match … bump BOTH, every release` was
wrong. **Mechanism beats intention. A comment is not a mechanism.**

A mechanical fix is written into the script — read the version back off the
built `.exe`, which `ticktimer.rc` already stamps from `Version.h`, making
the installer *derive* instead of copy — but is left **commented out**. It
cannot be exercised without Windows and Inno, and this file is the last
artifact between the project and a user's machine. Enable it in a session
where `ISCC` can actually run it.

---

## 2. Diagrams

### Found

- **Four had never been rendered** — no `.png`, no `.svg`, ever:
  `settings_pages`, `catch_up_ladder`, `catch_up_surfaces`,
  `catch_up_chip_states`. That is the whole v26.1/v26.2 arc, shipped with
  pictures nobody could look at. A rendered diagram is the only evidence a
  diagram was reviewed.
- **Six were unindexed**, including `chat_turn_flow` — the entire v25
  chat-turn picture, invisible since the day it was drawn.
- **"Mermaid retired" was never true.** The iteration plan has claimed it
  since v20. There were **15** live `.mmd` files.

### Done

| | |
|---|---|
| **New** | `app_architecture.puml` — the establishing shot |
| **Rendered for the first time** | the four above (`.png` + `.svg`) |
| **Converted to PlantUML** | `model_refresh_decision`, `activities_modelview_contrast`, `glance_focus_states`, `needsblock_height_bound`, `deadline_time_flow`, `window_memory_restore` |
| **Split** | `window_memory_restore` → `+ window_memory_save` |
| **Archived** | the six `.mmd` originals → `diagrams/legacy-mermaid/` |
| **Rewritten** | `diagrams/README.md` — honest counts, all 31 entries, audit commands |

**Why only six of fifteen converted.** The six shared a defect the other
nine don't: indexed as canonical *and never rendered once*. That is broken
documentation. The nine survivors render and their outputs are current —
"uses the older of two working tools" is debt, not a defect, so it is
**named and counted** in the index rather than fixed in a hurry.

**Why the originals were archived, not deleted.** Same reasoning as
`JsonStore::migrateLegacyData`: copy, never move, never overwrite. A
conversion is a rewrite, and a rewrite can lose a detail nobody notices for
months. The originals settle "did it used to say X?" permanently, for a few
kilobytes.

**Why one diagram became two.** The Mermaid original drew restore and save
as a single flowchart with two disconnected entry points. PlantUML's
activity syntax cannot express that — which forced the question, and the
honest answer was that they were always two independent stories sharing a
preference key. A notation refusing to draw something is sometimes a review
comment.

### `app_architecture` — the gap that mattered

Twenty-nine close-ups of individual arcs and no overview. Someone opening
this repo cold had to infer the system's shape from the file listing.

It draws the seven stacked pages, the model/view layer, the pure/wire split,
persistence, and — in green, because it is what the picture is *about* —
the single `changed()` signal that drives every repaint and every save.
Three rules are written into the diagram's own header comment: one aggregate
and one signal; the pure/wire split that lets 282 tests run headless and
offline; and the stack index being an identity, not a position.

### New: the audit habit

`diagrams/README.md` now carries two shell checks — unrendered, and
unindexed. Both would have caught every finding above on the day it
appeared. They are run and clean as of this drop.

---

## 3. Documents

### `README.md` — contradicted itself four ways

| claim | reality |
|---|---|
| `v27.0` in the status line, `v26.8` mid-sentence | **v26.8** |
| `292 tests` in one place, `267` in another | **282 test functions**, per-suite |
| `60 domain tests` | **102** |
| storage `format v6` | **format v11** |

The hardcoded total was replaced with per-suite figures counted from source,
plus *"run `ctest` for the number of the day."* A total that must be
hand-updated in three places is a drift generator — which is exactly how it
came to hold three different values. **Derive, don't store, applied to
prose.**

### `docs/06_IterationPlan.md`

- "Mermaid retired" → the real counts, with the debt named.
- "Quick-add / AI (in progress)" → ✅; it finished at v21.2, six versions ago.
- **New row** for the v26.1–v26.8 arc, which had none.
- The v27.0 row now carries the discrepancy warning below.

### `docs/SESSION_NOTES.md`

The log jumped from v26.0 straight to v27.0 — **twelve drops with no
session notes at all.** Added a clearly-marked *reconstructed* entry
covering v26.1–v26.8 (one line each, pointing at the addenda rather than
duplicating them), plus this session's entry and the new shipping checklist.

Why the notes went missing is the part worth keeping: notes get written at
natural stopping points, and between v26.1 and v26.8 there weren't any —
each drop was a same-day response to a field report, and "I'll write it up
once this settles" never fired because it never settled. **A drop is not
shipped until its note exists.**

### `docs/QUESTION_BANK.md`

**V173–V188** appended: the architecture diagram and its single-signal
argument, the limits of a `static_assert`, mechanism-vs-discipline, the
blast-radius rule for untested changes, the two audit commands, the
convert-six-not-fifteen call, and the reversibility argument below.

---

## 4. Still open — read before starting v27.1 or v28

**The v27.0 subtask code is not in this tree.**

`SESSION_NOTES` and the iteration plan describe v27.0 as shipped with 267
tests green. But `Task.h` has no `parentId`, no `estimateMinutes`, no
`chunkable`; there is no `design-addendum-subtasks.md`; and `format v11` is
the *catch-up* v11 (`Event.outcome` / `movedToId`), not the subtasks one.
Either the drop was never applied to the tree, or it was applied partially.

**Nothing was deleted.** If the code exists on disk and the docs were
removed, a real design record is destroyed to fix a bookkeeping error. If it
never landed and the docs stay, the cost is a warning label. When two
corrections disagree, pick the reversible one.

Both the README status line and the iteration plan now carry the warning.
**Resolve it before v27.1 or v28** — both build on fields that may not exist.

---

## 5. The shipping checklist (new)

Added to `SESSION_NOTES.md`, because this session found three drift bugs a
two-minute checklist would have prevented, and one of them reached users.

1. Bump `include/Version.h` — macros *and* string.
2. Bump `installer/ticktimer.iss` `AppVersion` to match. The `.rc` files
   need nothing; they `#include` the header.
3. Write the session note. **No note, not shipped.**
4. Run the two diagram checks in `diagrams/README.md`.
5. Grep the status tables for the *previous* version string. If it appears,
   it is stale.

---

## The one finding underneath all of them

Every bug here was the same shape: **a claim that no mechanism checked.**

The version string had a `static_assert` for the half that could be
automated and drifted on the half that couldn't. The diagram index had no
check, so it lost six entries. "Mermaid retired" had no check, so it
survived six versions as a plain falsehood. The README totals had no check
and reached three different values.

Documentation doesn't rot because people are careless. It rots because
**prose has no compiler** — so the job is to keep finding the places where a
five-line shell loop can be one, and to write down honestly what's left over
for a human.
