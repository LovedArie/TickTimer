# Design Addendum — The format floor (v31)

*Choice → why → alternative rejected, per `CLAUDE.md`. Status: **approved by
direction (v31)**. Extends nothing; it defends the rule
`CLAUDE.md` has stated since v14 — "JSON persistence grows additively only" —
by supplying the half of it that was never written down.*

**Origin:** an owner bug report on 2026-09-08. A weekly activity showed the pill
**"in use (13)"** and, one inch below it, **"Not on the calendar yet."** Both
strings were telling the truth, which is how the real defect was found.

---

## §F.0 What actually happened

The planner had been written by v31 at format **16**. It was then opened by the
**v30.8.1** copy still installed in the Start Menu — a build from 28 August that
predates the whole Schedules arc — which loaded it, ignored the two keys it had
never heard of, and saved it back at format **14**.

That save destroyed:

| Lost | Count |
|---|---|
| `schedules` (the whole array — every recurrence rule) | 5 |
| `Event.scheduleId` (the link from an occurrence to its rule) | 73 |

No error, no warning, no crash. The occurrences survived as thirteen plain
blocks seven days apart, so the app could still say "in use (13)"; the rule that
made them was gone, so it also said "Not on the calendar yet." The user's
semester timetable had become thirteen unrelated appointments.

Recovery was possible only because `base-data-<user>.json` — the sync base,
which exists for an unrelated reason — still held format 16. **That is luck, not
design**, and it is the second finding: the next such loss has no reason to be
recoverable.

## §F.1 The rule that was only half-written

`CLAUDE.md` says:

> **JSON persistence grows additively only.** A missing key or an unknown enum
> string must read as a safe default, so old files load with no migration
> branch. Never repurpose a key.

Every word is about **new code reading old files**. That direction was designed,
tested and works. The opposite direction — **old code reading new files** — was
never stated, and turns out to be the dangerous one, because the same tolerance
that makes an old file load safely makes a *new* file load **lossily**: a key
the loader does not recognise is not an error, it is silence.

The two directions are not symmetric and must not share one sentence:

| Direction | What tolerance buys | What it costs |
|---|---|---|
| new binary ← old file | loads with no migration branch | nothing |
| old binary ← new file | loads, apparently fine | **everything it did not recognise, on the next save** |

**A tolerant reader must not also be a confident writer.** That is the whole
addendum.

## §F.2 The choice: a named floor, a four-state answer, and a latch

Three parts, all of them small.

**1. The version becomes a named constant.** `JsonStore::kFormatVersion` (16)
replaces the integer literal in `toJsonObject`, so the number the binary writes
and the number it refuses to read past are provably the same one.

**2. `load()` stops answering a yes/no question.** It returned `bool`, and the
one caller reads it like this:

```cpp
if (m_store.load(m_data))
    recoveryMessage = m_data.recoverInterruptedTracking();
else
    m_data.seedDefaults();          // <-- the trap
```

`false` already meant two unrelated things — *no file yet* and *the file is
unreadable* — and both landed in `seedDefaults()`. Adding a third failure to
that `bool` would have been **worse than the bug it fixes**: the app would seed
starter categories and the very next `changed()` would autosave them over a
planner it merely failed to understand. Total loss, where v30.8.1 managed only
partial.

So `load()` returns `LoadResult` — `Loaded`, `Empty`, `Unreadable`, `TooNew` —
and the compiler makes every call site say which one it means. This is
`CLAUDE.md`'s own rule about predicates whose meaning changes, applied to a
return type: the question genuinely changed, so the answer's type changes with
it rather than gaining a third interpretation of `false`.

**3. The store latches read-only.** A `TooNew` load sets `m_readOnly`, and
`save()` refuses while it is set. Belt and braces: even if some future path
seeds defaults and triggers an autosave, the bytes on disk cannot be replaced.
The guard that matters is the one that holds when the caller is wrong.

The refusal itself lives in `applyJsonObject`, because `JsonStore.h` already
promises that it is the single conversion feeding **both** the disk and the
wire. One check therefore covers three doors: the file, a pulled sync document,
and a peer's planner in Compare. A newer document coming down the wire is the
same hazard with better aim, and `SyncService::applyServerData` was discarding
the return value.

**What the user sees:** the app refuses to open that planner and says which
version wrote it and which version is running. It does not open read-only. A
too-new document has *already* been stripped by the time it is in memory, so a
read-only view would be a lossy view presented as the truth — the exact failure
mode being fixed, moved one layer up.

## §F.3 Alternatives rejected

**Preserve unknown keys through the round-trip.** The textbook answer: keep the
raw `QJsonObject` for anything the loader did not consume and re-emit it on
save, so a downgrade is lossless instead of refused. Rejected on the decisive
ground that **it could not have prevented this incident and cannot prevent the
next one of its kind**: v30.8.1 is already built and installed, and no change
made today can give it a behaviour it shipped without. The cost is real and the
benefit is zero for the case that prompted it. It also demands a shadow data
model — per-entity leftovers keyed by id, with no honest answer for what happens
to the leftovers of a row the user deletes — inside a file whose stated virtue
is that the domain structs know nothing about JSON.

**Make the file unreadable to old binaries** (a wrapper object, a renamed file
for new formats). Actively catastrophic, and worth recording so nobody proposes
it again. An old binary that cannot parse the file takes `load()`'s `false`
branch, seeds defaults, and writes a **starter planner** over it — trading a
partial loss for a total one. The same argument kills renaming the file per
format: the old build finds no file, calls it first run, and you get two
planners diverging under one account.

**Do nothing in code; just do not run old builds.** This is what `H3` in the
backlog covers, and it is necessary but not sufficient. Four builds of this app
routinely exist on one machine against one account — the Start Menu install,
`dist\`, the sideloaded APK, and the WebAssembly app — and `docs/ROLLOUT.md`
puts two phones and a friend on the same server. A rule enforced only by
remembering which icon to click is not enforced.

## §F.4 The limitation, stated plainly

**This fix is forward-only.** It protects a v31-or-later binary from a file
written by something newer. It does nothing about the v30.8.1 copy that caused
the incident, because that binary shipped without the guard and always will
have. There is no defence against an already-released binary — the earlier
alternatives are rejected precisely because they pretend otherwise.

The consequence is a rule for releases rather than for code, and it belongs with
the other deployment seams in `design-addendum-deployment.md`: **a format bump
is the one change that makes older installed copies dangerous rather than merely
out of date.** After one, the old builds on every machine are not stale, they
are destructive, and replacing them is part of shipping the release — not
housekeeping to be done later.

## §F.5 What is pinned by test

- A document stamped one above `kFormatVersion` is refused by
  `applyJsonObject`, and the `AppData` handed to it is left untouched.
- `load()` answers `TooNew` for such a file, `Empty` for a missing one,
  `Unreadable` for malformed JSON, and `Loaded` otherwise.
- After a `TooNew` load the store is `isReadOnly()` and `save()` returns false
  **and the file on disk is byte-for-byte unchanged** — the assertion that
  actually encodes the requirement.
- A document at exactly `kFormatVersion`, and any lower one, still loads. The
  additive-growth guarantee is not narrowed by the floor.
