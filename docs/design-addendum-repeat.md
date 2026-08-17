# Design addendum — Repeat, made real

*Session: recurrence for tasks and planned blocks (owner request, via
TickTick's picker). Companion to `design-doc.md` §3; question bank
section X.*

---

## A. The archaeology: a rule that was only ever shown

`Task.repeat` had existed since v7 — stored, editable in the detail
dialog, worn as a ↻ chip on rows — and **acted on by nothing**:
`setTaskDone` flipped the boolean and went home. The feature was
decoration wearing the clothes of behaviour, which is worse than
absence: it *promises*. So this session had two halves — make the
existing hint real (tasks), and extend the vocabulary to planned blocks
(events), which had nothing.

## B. One invariant carries the whole design

**The rule lives on the newest link of the chain.** When an occurrence
advances — a repeating task completed, a repeating block rolled past —
the old item is *stripped* of its rule and the new item carries it.
That single sentence buys three things at once:

1. **Duplicate-spawn immunity, for free.** Cycle a task
   done → undone → done: the second completion finds no rule and spawns
   nothing. No "already spawned" bookkeeping, no fragile
   same-title-same-date heuristics — the state that would permit the
   bug no longer exists (make illegal states unrepresentable, again).
2. **Honest archives.** A completed task in the Archive doesn't wear a
   ↻ chip falsely claiming it still repeats; a past block doesn't
   either. The chip always means "the next one comes from me."
3. **Chain identity without a chain table.** No `seriesId`, no parent
   pointers: the chain exists only as a rule hopping forward. History
   items are plain items.

## C. Tasks: completion is the metronome

A repeating task advances **when you complete it** (the todo-app
classic): fresh id, same title/notes/priority/category, due date
advanced by `nextOccurrence`, rule carried, `done=false`. The old task
keeps its victory. Un-completing does *not* un-spawn — history is
append-only here like everywhere else in the app. The advance rule
itself is one pure function shared by both halves of the feature
(`nextOccurrence` in Task.h): daily/weekly by day arithmetic,
monthly/yearly riding Qt's clamping (Jan 31 + 1 month = Feb 28/29 — the
boring, predictable reading of "monthly on the 31st").

## D. Blocks: the calendar is the metronome

Blocks aren't "completed", so a repeating block advances when its date
has passed — `AppData::rollRepeats(today)`, called by MainWindow at
startup and at each midnight (the domain owns the door; MainWindow owns
only the calendar of when to knock — the alarm-service division,
reused). Three deliberate honesty rules, each a test:

- **No retroactive occurrences.** Twelve days unopened does not
  backfill twelve empty plans — the rule re-arms at the first rule-date
  **≥ today**. An empty plan for a day you weren't there is noise
  pretending to be history.
- **Occupied dates are skipped, not fought.** The spawn goes through
  the same `isFree` door as every hand-made block; a collision advances
  the rule one more period (up to a year, then the rule stays on the
  old block and tomorrow retries — a stuck chain must never silently
  die *or* shove another block aside).
- **Identity copies, history doesn't.** The spawn carries
  activity/title (a linked task is demoted to text — the `removeTask`
  downgrade pattern: next Monday's block shouldn't claim a deliverable
  that may be done by then); segments start empty. Each past block
  keeps what actually happened on it.

Mechanics note: `rollRepeats` collects ids first and mutates after —
`appendGuardedEvent` grows the vector, and growing mid-iteration is the
classic invalidated-pointer trap (the code comments it at both `append`
sites: "`old` may dangle past this line").

## E. The borrowed vocabulary (a naming debt, taken on purpose)

`Event.repeat` is typed `Task::Repeat`. Yes, that reads oddly. The
alternative — moving the enum plus its four helpers to a neutral header
— would churn a dozen call sites (`Task::Repeat::Weekly` everywhere,
JSON helpers, the task dialog) to change zero behaviour. The debt is
taken knowingly, priced (one comment at each borrow site), and cheap to
repay later if a third repeater ever appears. Naming purity is a value;
so is a diff that can be reviewed in one sitting.

## F. Storage: v9, migrated by absence

Events gain `"repeat"`; the format version moves 8 → 9. Old files carry
no field, and `repeatFromString("") == None` — pre-v9 data reads
exactly as it always behaved (the same absent-field migration the task
side used at v4). Preferences were never involved: recurrence is a fact
about your plan, so it lives in `data.json` and syncs.

## G. UI: the smallest honest surface

- **EventDialog** gains a "Repeats" combo that **applies on change** —
  that dialog is a control panel, not a form (the nudge buttons set the
  precedent; an OK-gated combo beside instant-apply buttons would be
  two contracts in one box). Items in enum order so `currentIndex` IS
  the enum (the TaskDetailDialog trick, same vocabulary).
- **Blocks wear the ↻ chip** on their anatomy line (`… · 2h · ⟳
  Weekly`) — the same glyph vocabulary task rows already speak, so both
  kinds of repetition read identically at a glance.
- Deliberately *not* built: repeat in the quick-create picker (set it
  from the block's dialog after creation — one home per decision), and
  "edit this vs. all occurrences" (chains share no identity, so each
  block is just a block; the newest link is where the rule is edited).

## H. Limits, named

- **Task spawn-on-complete requires a due date** — the rule advances a
  date; with none to advance, the rule stays decorative (the dialog
  could grey the combo; parked).
- **No intervals or weekday sets** ("every 2 weeks", "Mon/Wed/Fri") —
  the enum is the honest four. A Weekdays value or an interval field is
  a contained extension of `nextOccurrence` when asked for.
- **Un-completing a spawned-from task leaves the spawn** — append-only
  history; delete the extra by hand if the completion was a slip.
