# Design addendum — deadline times (v22)

> Owner request: *"When creating a task, I'd like to be able to set a time
> deadline."*

## §A The decision that shaped everything else

A deadline can now carry a clock. The obvious implementation is to replace
`QDate dueDate` with a `QDateTime`, and it is the wrong one.

`QDateTime` **cannot express "due Aug 8, no particular time."** It would have
to pick a stand-in — midnight? 23:59? — and that invented value then leaks into
every comparison in the app: sorting, overdue tests, the needs-a-block rule.
The model would be able to say something false, which is precisely the failure
`Task.h` has been guarding against since §3.9.

So the deadline is **two fields, each able to be absent**:

```cpp
QDate dueDate;   // invalid == "DATE TBD"     (unchanged since v3)
QTime dueTime;   // invalid == "all day"      (new)
```

That reuses the existing absence convention rather than inventing a second
one. `dueDate` has meant "invalid is a real answer" for nineteen versions;
`dueTime` means it the same way, so there is one idiom in the struct to learn,
not two.

### The one illegal combination

A time with no date is meaningless. Rather than let it exist and hope every
reader handles it, **the domain refuses it at the door** — all three doors:

| Door | Rule |
|---|---|
| `addTask` | `dueTime` kept only if `dueDate.isValid()` |
| `setTaskDueDate` | clearing the date clears the time |
| `updateTask` | same pairing rule |

One invariant, three enforcers, no path in around it. The UI never even offers
the state: ticking "No due date" in `TaskDetailDialog` greys the time editor.

## §B The deadline as one moment

Two fields need one derived answer for "is this late?":

```cpp
QDateTime Task::dueMoment() const;   // dueDate + (dueTime, else 23:59:59)
```

An all-day task is due at the **end** of its day. This is the single most
likely off-by-a-day bug in the feature, so it is stated once, in one function,
and pinned by `allDayDeadlineIsEndOfDay()` in the test suite.

`isOverdue` is now **overloaded**, not replaced:

```cpp
bool isOverdue(QDate today) const;          // whole-day reasoning (unchanged)
bool isOverdue(const QDateTime& now) const; // clock-aware
```

Callers that hold a date keep the cheap answer; callers that hold an instant
get the sharp one. The argument type picks the rule, which is what overloading
is for. Quietly making the old one time-aware would have changed answers all
over the calendar — including in code that is *correct* to think in days.

## §C What the time does and does not change

| Question | Time-aware? | Why |
|---|---|---|
| Is it overdue? | **Yes** | "Due today 09:00" is late at 09:01 |
| Does it need a block? | **Yes**, one branch | A lapsed same-day deadline flags now, not at midnight |
| Is a block *covering* it? | **No** | You cannot plan a block finer than the day the deadline falls on — `coverage::deadlineOf` stays a `QDate` |
| Sort order | **Yes** | Same day: timed first, earliest first, all-day last |
| Bucket (overdue/week/later) | **No** | Buckets are day-sized by definition |

The narrowness is deliberate. Every pre-v22 test still reads the same answer,
because every new branch is guarded by `dueTime.isValid()`.

## §D Storage: additive, fifth time

```json
{ "dueDate": "2026-08-08", "dueTime": "23:59:00" }
```

An invalid `QTime` serialises to `""` and parses back invalid, so "all day"
round-trips for free and a **v21 file with no key at all loads as all-day** —
no migration branch, no version check. Because sync and sharing reuse
`JsonStore`'s converters, the time syncs with zero further work, exactly as
`taskId` did in v6 and the dismissal facts did in v10.

## §E Capture: the parser earns its keep

`nlp::parseQuickAdd` now fills `dueTime`, following the same instincts it
already applies to dates:

- **Forms:** `5pm`, `5:30pm`, `5 pm`, `17:00`, `17h`, `17h30`, `noon`,
  `midnight`, with a leading `at` or `@` swallowed.
- **A bare hour is not a time.** `lab 4` and `chapter 7` must never grow a
  deadline. Writing `at 5` licenses it — the word *at* is the user saying
  "this one is a clock," the same role the `th` in `28th` plays for dates.
- **`midnight` → 23:59, not 00:00.** This is a *deadline* parser. "Due
  midnight Friday" means the end of Friday; 00:00 would make the task
  twenty-four hours late the instant it was typed.
- **A time with no date implies today**, resolved in the parser so the UI
  never receives the orphan the domain would drop.

Everything the parser claims appears in the live preview before Enter — the
preview is quick-add's trust contract, and a silently-added 17:00 would be
worse than no time parsing at all.

The LLM path gained a `due_time` field in its schema with the same
degrade-don't-fail rule: garbage means all-day, and a time with no date is
discarded at the edge rather than relying on the door to catch it.

## §F Where it shows

| Surface | Rendering |
|---|---|
| Upcoming card | `due 17:00` / `tomorrow 17:00` / `in 3 days, 17:00` |
| Category task pill | `Aug 8 · 17:00` |
| Needs-a-block row | `Fri 24 Jul 17:00 · in 3d` |
| Calendar "due today" strip | a green time chip, rows sorted by clock |
| Legacy `TaskRow` badge | `Aug 8 · 17:00`, rose when lapsed |

One format everywhere — 24-hour `HH:mm`, via `dueTimeLabel()` — matching the
agenda's slot labels and the put-off strip. Both card delegates measure and
draw the string through **one** function, so a pill can never be sized for
`Aug 8` and then asked to render `Aug 8 · 23:59`.
