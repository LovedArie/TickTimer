# Learning queue

Topics raised by real changes in this repo that deserve a proper sit-down,
rather than the three lines they got in passing. Each entry names **where in
this codebase it actually bit**, so the explanation has something concrete to
point at.

---

## Move semantics and rvalue references

**Raised by:** v29.3, `Event::movedToId` (a `QString`) becoming
`Event::movedToIds` (a `QStringList`).

`Event` holds two container members by value now — `QVector<Segment> segments`
and `QStringList movedToIds`. Every time `m_events` reallocates, every `Event`
in it is relocated, and what that costs depends on whether the container is
*copied* or *moved*.

Questions to work through: what a move actually does to the source object; why
a moved-from object must still be destructible; how Qt's implicit sharing
(copy-on-write) interacts with all this, and why it means `QStringList` copies
are cheap in a way `std::vector<std::string>` copies are not; and when
`std::move` earns its keystrokes versus when the compiler was already going to
elide the copy.

Related landmine already in the codebase: `AppData.cpp` takes **copies** of
`src->taskId` and friends before calling `appendGuardedEvent`, because the
append can reallocate and invalidate `src`. That is a lifetime problem, not a
move problem — but the two are easy to confuse and worth separating deliberately.

---

## RAII and destructor ordering

**Raised by:** `AppData::Batch`, which v29.3 leans on harder — undoing a
three-piece split is four mutations that a listener must see as one
`changed()`.

`Batch batch(*this);` is a variable whose whole purpose is its lifetime. Its
constructor suppresses emission, its destructor restores it and fires once, and
nothing in `undoReschedule` ever calls "end the batch" — the closing brace does
it. That is RAII: the scope *is* the transaction.

Questions to work through: the exact moment a destructor runs and in what order
when several objects share a scope; why this pattern survives an early `return`
(and why that is the entire point); what happens if a destructor needs to do
something that can fail; and how the same shape underlies `QMutexLocker`,
`std::lock_guard`, and Qt's own signal blockers.

Compare against the alternative the codebase did *not* choose: a manual
begin/end pair, which is one early return away from leaving the whole app with
signals permanently suppressed.

---

## Smaller notes, already answered in passing

- **Why the compatibility mirror is safe in `JsonStore` but not in `Event`.**
  Not a C++ question — a design one. Two fields holding the same fact are only
  dangerous if something can observe them *disagreeing*. In storage, one door
  writes both in a single instant and the loader always prefers the list, so
  there is no window. In memory, an `Event` is handed to arbitrary readers for
  arbitrary durations, and any of them could hold a stale opinion. The rule:
  duplicate a fact only where you control every read and every write.
