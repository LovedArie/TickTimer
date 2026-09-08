# Design Addendum — Schedules, and the Activity that grew up (v31)

*Status: **approved by direction (v31)**. Supersedes
`design-addendum-repeat.md` (v9), whose mechanism this replaces; that file
is kept as the historical record of the decision it documents.*

**Origin:** seven owner requests in one message, three of which turned out
to be the same request wearing different hats:

| Request | Classification | Why |
|---|---|---|
| Edit an activity's name | **domain change** | a missing door, not a missing screen |
| Add descriptions to activities | **domain change** | a new stored fact |
| Add a date + reminder to an activity | **domain change** | a new concept — see §S.1 |
| "The repeat does not work — I want to see it each week" | **domain change** | the existing mechanism cannot express it |
| Start and end dates for a repeat | **domain change** | a rule needs a lifetime |

The last three are one feature. What follows argues that first, because
everything else follows from it.

---

## §S.0 What was actually broken

Recurrence existed twice, and neither instance was a bug:

* **`Task::repeat`** (v7 stored, v19.10 acted on) spawns the next
  occurrence when you *tick* the current one. Right for a chore. Useless
  for a timetable, because nothing exists until you finish something.
* **`Event::repeat`** (v9, `AppData::rollRepeats`) rolled **one**
  occurrence forward once its date had passed. The rule lived on the newest
  link of a chain — an invariant that made duplicate spawns impossible and
  looking ahead impossible in the same stroke.

Both are *one at a time, after the fact*. "I want to see it each week" is
not a defect in either; it is the shape of both. A separate, smaller defect
made this harder to diagnose: the comment at the top of `Task.h` still
said repeat was "stored and displayed, acted on never" — true at v7, stale
since v19.10, and the first thing anyone reads when asking why repeat does
not work. **A stale comment is a confident lie.** It has been corrected.

## §S.1 A rule you can look forward along has to be its own thing

**Choice:** a new concept, `Schedule` — *a recurring plan for a block*.
It carries what recurs (an `activityId`, or a `title` for an ad-hoc block),
`startDate`, an optional `endDate`, a start and end time in minutes after
midnight, a `Task::Repeat`, a `weekdays` set (§S.11), a `reminderMinutes`
lead, and `skipDates`.

**Why:** the rule and its occurrence were the same object, so neither could
outlive the other. Separating them is what makes "every Tuesday 13:30–17:00
from Aug 25 to Dec 15" a thing the model can *say*. It is also what gives
the rule a place to hold an end date, which nothing before had anywhere to
put.

**Rejected — keeping `Event::repeat` alongside for ad-hoc blocks.** Two
mechanisms for one idea is the failure `CLAUDE.md` names outright ("follow
the existing family rather than inventing a fifth pattern"). The reason it
was tempting is that an ad-hoc block has no activity to hang a rule on; the
answer is that a Schedule carries a `title` for exactly that case, the same
either-or identity an `Event` already has. `rollRepeats` and
`setEventRepeat` are deleted.

## §S.2 Occurrences are REAL events, materialised ahead

**Choice:** `AppData::syncSchedules(today, horizonDays = 120)` creates a
genuine `Event` for every occurrence in `[today, today + horizon]` that
does not exist yet. Each carries `Event::scheduleId` back to its rule. It
runs at startup and at each midnight, where `rollRepeats` used to.

**Why:** the obvious alternative — expanding the rule at paint time and
drawing ghosts — costs far more than it looks. Every reader of the plan
would need a second code path: the tracker, `Stats`, `DayBriefing`,
`Alarms`, `MissedBlocks`, the catch-up card, the week view. And a ghost
**cannot be tracked**, which is fatal in a plan-vs-actual app: a lecture
you cannot start a timer on is a picture of a plan, not a plan. A term's
worth of real rows is about sixteen, and every existing consumer already
knows what to do with them — including the block alarm, which is why
reminders needed almost no new code (§S.6).

**The price, paid honestly:** an occurrence you delete must not come back,
so the rule remembers what you removed (`skipDates`). That is the one piece
of state a purely-derived design would not need. It is a fair trade for not
forking every reader in the app.

**Rejected:** virtual expansion at paint time (above); and pre-materialising
the *whole* rule to its end date, which for an open-ended daily rule is
unbounded.

## §S.3 The past is never backfilled

**Choice:** materialisation starts at `max(startDate, today)`.

**Why:** you cannot plan a day that has happened. A rule entered in
September that starts in August would otherwise conjure thirty blocks
nobody was there for — and hand the catch-up card thirty accidents that
never occurred. `rollRepeats` made the same call and was right about it
("no retroactive occurrences for days you weren't there"); the rule is
inherited verbatim rather than re-decided.

## §S.4 An occupied slot is skipped, not fought

**Choice:** materialisation asks `isFree` and silently passes over any date
whose slot is taken.

**Why:** inherited from `rollRepeats`, and still correct for a sharper
reason now that a rule can cover a whole term: a block you placed by hand
outranks one a rule merely predicted. Next term's identical rule must not
be able to bulldoze this term's actual plan. Consequence accepted: a
permanently occupied weekday produces no occurrences and says nothing about
it. Making that visible is a later question, not a silent one — it is
recorded here.

## §S.5 Editing a rule may not rewrite history

**Choice:** `updateSchedule` withdraws only **future, untouched**
occurrences and regenerates them. "Untouched" is one named predicate —
`occurrenceIsUntouched`: no tracked segments, no catch-up verdict, not the
block currently being timed. `removeSchedule` does the same and then
**downgrades** the survivors, clearing their `scheduleId` rather than
deleting them.

**Why:** "the time you already spent belongs to the day you spent it" — the
sentence `rescheduleBlock` already lives by. A schedule edit is a statement
about the future; letting it delete an hour you actually sat through would
destroy a fact to fix a pointer. Downgrade-instead-of-cascade is the third
instance of the pattern in this codebase (`removeTask` was the first).

**Both doors take `today` explicitly** rather than reading the clock,
because both *decide what counts as the future*. A hidden
`QDate::currentDate()` would make that judgement unobservable — the same
argument behind `TrackerService::nowProvider` and the old
`rollRepeats(today)`.

## §S.6 Reminders came free, and one sentence was not free

**Choice:** `reminderMinutes` lives on the **rule**, and
`alarms::upcoming` resolves it through `Event::scheduleId`.

**Why:** the block alarm has derived a start alarm for every planned block
since v19.7 and, since v30.6, publishes a forward schedule the OS can hold.
A materialised occurrence is an ordinary block, so it already chimed; only
the *lead time* was new. Putting the lead on the rule rather than copying it
onto each occurrence means one edit changes every future chime and there is
no second copy to fall out of step.

The part that was not free: the alarm's title. "Starting now", fired
fifteen minutes early, is worse than no reminder, because it is a fact the
user will act on. The words change with the lead.

## §S.7 The recurrence walk is a pure function

**Choice:** `recur::occurrences(const Schedule&, from, to)` in
`Recurrence.h` — no `AppData`, no clock, no I/O, no Widgets.

**Why:** the same move `SyncPlan::decide`, `MissedBlocks`, `Affordability`
and `version::decideBanner` all made. It is the one real judgement of the
feature, so it gets a table of microsecond tests: month boundaries, an end
date landing exactly on an occurrence, an open-ended rule, a skip, a
backwards window. Everything impure around it — creating events, refusing an
occupied slot, emitting `changed()` — stays in `AppData` where the state is.

**Naming:** `recur`, not `sched` or `schedule`. POSIX ships `<sched.h>`;
the `sync`/`syncplan` scar (`CLAUDE.md`) is one per project.

**Known limit, stated not hidden:** monthly stepping is `QDate::addMonths`,
which clamps — Jan 31 → Feb 28 → Mar 28, so the day drifts across a short
month. That is the app-wide rule already (`nextOccurrence`, v7). This file
deliberately does **not** invent a second, better arithmetic: two
recurrence rules in one app is exactly the quiet disagreement the whole
feature exists to remove. Fix it in `nextOccurrence` and everything follows.

## §S.8 The Activity becomes editable — and why that was safe

**Choice:** `renameActivity` and `setActivityDescription`, plus
`Activity::description`. The name is refused when blank; the description is
freely empty.

**Why the bug was worse than it looked:** `removeActivity` refuses an
activity used by any event, so "delete and re-create" stops working the
first time you *use* the thing — which is the moment you notice the typo. A
mistake made on day one was permanent by day two.

**Why renaming is safe at all:** every event stores the activity's **id**,
never its name (`Activity.h`, "reference, don't copy"). A rename is one
write the whole history sees at once. Had the name been copied into each
event — the tempting shortcut — this feature would have needed a migration
pass over every block ever planned, and any block it missed would disagree
forever. The v1 discipline paid for the v31 feature.

**The asymmetry between the two setters is deliberate.** A blank name is
refused; a blank description is normal. Folding them into one coarse
`updateActivity` would have forced one of those two truths to be re-decided
at every call site.

## §S.9 The task chain gets an end, and stays a chain

**Choice:** `Task::repeatUntil` (invalid = forever) and
`setTaskRepeatUntil`. The chain stops when the **next** date would fall
past it. Clearing the repeat clears the end.

**Why the next date and not today:** otherwise whether the rule is over
depends on when you happened to tick the box, which is not a property of the
rule.

**Why a separate door and not an eighth `updateTask` parameter:**
`updateTask`'s whole argument (v22) is that a forgotten seed should be a
*compile* error, and a defaulted eighth parameter is precisely a seed that
can be forgotten silently. The detail form already batches several
mutations into one `changed()`, which is what made the coarse door worth
having; one more call inside that batch costs nothing.

**What task repeat still does NOT do, deliberately:** show future
occurrences. A weekly chore listing sixteen copies of itself would bury the
list it lives in. Looking forward is what schedules are for, and they act on
the calendar, where sixteen entries are the point rather than the problem.

## §S.10 Storage

Format **v16**, additive only. v15 brought the `schedules` array,
`Event.scheduleId`, `Activity.description` and `Task.repeatUntil`; v16 adds
`Schedule.weekdays` (§S.11), whose absence reads as the pre-v31.1 meaning.

The one **upgrade path** in the loader: a pre-v15 file may carry
`Event.repeat`. That key is still *read*, for exactly what it always meant,
and converted into a Schedule so a user who set "repeats weekly" does not
find it silently forgotten. Note what this is not — no key was repurposed
(`CLAUDE.md` forbids that), and a v14 file loaded by a v14 build still
behaves as it always did. Reading an old fact into the current model is what
a tolerant loader is *for*.

## §S.11 The weekday is the rule's, not the start date's (v31.1)

**The defect.** `startDate` carried two facts: when the rule opens, and —
for a weekly rule — which weekday every occurrence lands on, because the
walk simply stepped seven days from it. §S.1 shipped that as a *feature*
("there is no separate 'on which day' field to disagree with it"), and the
editor's caption repeated the claim.

It is wrong for the case this whole feature exists to serve. Reported by
the owner: *"I have a class every Wednesday ... the start day of the repeat
is a Monday."* His LOG635 lecture opens on Monday 31 Aug and ends 7 Dec, and
the rule had been generating **Mondays** for a **Wednesday** class.

**Why the original reasoning failed.** It optimised for "one fact, not two
that can disagree" — a good instinct, applied to two things that are not
one fact. *When the term begins* and *which day the class meets* are
genuinely independent; a term that happens to open on a Monday says nothing
about the timetable inside it. Collapsing them did not remove a
disagreement, it removed the ability to state a truth (§3.9's rule, missed).

**Choice:** `Schedule::weekdays`, a set of Qt day numbers (1 = Monday). A
weekly rule names its own days; `startDate` means only "the window opens
here"; the first occurrence is the first named day on or after it. A SET
rather than a single day, because "Tuesday and Thursday" is one class with
one end date and one skip list, and the old coda's argument for splitting it
into two rules was answering the wrong question.

**Rejected:** a separate "first occurrence" date alongside a "range start" —
two dates that mean almost the same thing is exactly the disagreement §S.1
was right to fear.

**Compatibility is the load-bearing part.** An EMPTY set means "the weekday
of `startDate`" — precisely what every pre-v31.1 rule meant. So the four
rules in the owner's live file keep their current days (Fri, Fri, Mon, Tue)
with no migration branch, and `recur::effectiveWeekdays()` is the one place
that resolves the fallback, read by the walk, the summary and the editor
alike so a sentence cannot promise a day the calendar will not deliver. The
editor pre-ticks the resolved day, making the old behaviour visible rather
than implied.

**Verified against the live file**, not a fixture: format 15 → 16, one key
added, **zero values changed**, 218 events / 76 tasks / 4 schedules intact.

### §S.11a The silent discard, found in the same breath

`AppData::scheduleIsWellFormed` refused a malformed rule by returning
`false`; `ScheduleEditDialog` validated nothing. So Save closed the dialog
and the edit **vanished with no message** — two places deciding what "legal"
means, one of them silently.

The check is now one pure function, `recur::problemWith`, returning a
sentence rather than a bool. `AppData` calls it (and adds the one question
that needs the whole data set: does `activityId` still resolve?), and the
dialog calls it on Save and refuses with the reason. It also catches a
failure only a weekday set can produce — **days that never come round inside
the rule's own window** — which otherwise yields nothing at all, and
"nothing" is the one outcome a calendar cannot explain about itself.

### §S.11b The validator turned on its own draft (v31.2)

§S.11a gave one definition of "legal" and had the dialog quote it. The very
next thing that broke was a **consequence of that**, not of the old bug: the
dialog began refusing a rule the user had filled in completely, with *"Give
it something to be called."* Every field set — weekly, Friday, Sep 1 to
Dec 7, 13:30–17:00 — and Save would not close.

**Cause.** A `Schedule`'s identity is half `activityId` (Schedule.h: exactly
one of `activityId`/`title` carries it). `ActivityDetailDialog` stamped the
link on at *apply* time, on the reasoning that the dialog "never invents"
it — sound as far as authority goes. But Save is pressed **before** the
apply step runs, so what `recur::problemWith` was handed was a rule with no
link and no title, and it said so, correctly. The activity editor has no
title field and never will: a rule that belongs to an activity is named by
the activity.

So the draft was invalid by construction, and the moment validation moved
into the dialog (§S.11a) that became visible. **Editing an existing rule
still worked** — its seed came from the domain, link and all — which is
why only *adding* a rule was broken.

**Choice.** Seed the link, in a named function: `newScheduleSeed(activityId,
today)`. A draft the dialog can validate must be a draft that is complete in
every field the dialog does not offer.

**Alternative rejected:** carve the identity check out of
`recur::problemWith` so the dialog asks a weaker question. That is exactly
the two-answers-to-"is this legal?" split §S.11a had just closed, re-opened
one release later and from the other end. The apply step keeps assigning
`activityId` regardless — belt and braces, and the authority stays where it
was.

Seeding is a free function rather than a private method because it is the
whole answer to *"can a rule the user just added be saved?"*, and that
question deserves a test that does not have to `exec()` a modal to ask it.
`today` is a parameter, per the seam `TrackerService::nowProvider` opened.

## Coda — what this addendum did not do

* ~~**Weekday sets.**~~ **Done in v31.1 — and the reasoning below was
  wrong.** It answered "can one rule cover Tuesday and Thursday?" and never
  noticed the prior question: *how does a rule say which day it lands on at
  all?* See §S.11.
* **A visible signal when a rule produces nothing** because its slot is
  permanently taken (§S.4).
* **Fixing the monthly clamp** (§S.7).
