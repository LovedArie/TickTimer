# Backlog — TickTimer

*The one queue. Read it at the start of every working session; it is the
answer to "what next?". Everything else in `docs/` is a **record** — written
once, kept forever. This file is the opposite: an item exists in order to be
**deleted**.*

---

## How this file works

**One line per item.** A title, an id, and just enough to find the thing
again. If an entry wants a second paragraph it has stopped being a queue item:

| It needs more than a line because… | Then it belongs in |
|---|---|
| you already understand the bug and how it bites | `docs/TROUBLESHOOTING.md` (+ the trap indexes) |
| you are about to design the feature | a new `docs/design-addendum-*.md` |
| it is a manual check to run before a release | `docs/QA_CHECKLIST.md` |

**Delete the line when it ships.** There is no "Done ✅" section, deliberately:
`git log -p docs/BACKLOG.md` is a complete, dated archive of every item this
file ever held, and it costs nothing to keep. A queue that accumulates its own
history stops being read — that is exactly how §4 of `06_IterationPlan.md`
turned into a 30-row museum that drifted for six versions without anyone
noticing.

**Order is the ranking, highest first.** Re-rank the top of each section when
you pick up work, not on a schedule. The three criteria are the Unified
Process's, and they are worth applying in this order (Larman, *Applying UML and
Patterns*, "Planning the Next Iteration"):

- **Risk** — technical unknowns, and anything whose effort you cannot estimate.
- **Coverage** — does it touch a part of the system nothing has exercised yet?
- **Criticality** — how much you actually want it as the person using this app.

**Ids are permanent and never reused.** `B7` means the seventh bug ever filed,
even after B1–B6 are gone. That way "do B7" is unambiguous in a session months
from now, and the git history stays searchable by id.

**A cap, not a target: ~25 open items.** Past that, the list has stopped being
a plan and become a wish. Farley's argument in *Modern Software Engineering*
for small batches applies directly — the smaller the batch, the shorter the
window in which the world can invalidate your assumptions. A feature written
down six months before it is built is very often the wrong feature by then, and
you pay to re-read it every session in between. Delete freely; a genuinely good
idea comes back on its own.

---

## Bugs

*Found in use. One line: what you did, what you expected, what you saw.*

- **B8** **A mouse wheel over an open block silently destroys its recurrence
  rule.** Reported as "not a bug, but a problem" — it is a bug, and it is the
  most destructive one open. `EventDialog`'s **Repeats** `QComboBox` is wired
  straight to `currentIndexChanged` → `applyRepeatChoice`, and Qt's combo acts
  on a wheel event while merely HOVERED, with no click and no focus. Scrolling
  one notch to "Does not repeat" calls `removeSchedule()`, which drops future
  untouched occurrences and clears `scheduleId` on every survivor — exactly
  the reported "removes all the detail of all the existing recurring block".
  Scrolling to a different cadence calls `updateSchedule()` and
  re-materialises the term. **There is no wheel guard anywhere in this
  codebase** (`grep wheelEvent` returns nothing) and combos, spin boxes and
  date edits sit inside scrollable dialogs throughout, so this is a class of
  bug, not one widget. The rule to adopt: **a control that mutates data must
  ignore the wheel unless it has focus.**
- **B9** **Changing a block's recurrence destroys a rule with no confirmation
  and no scope question.** Even by deliberate click, picking "Does not repeat"
  removes the whole Schedule instantly. This is the same missing question as
  **F8** — "only this occurrence, or all of them?" — arriving through a
  different door, and it is why B8 is destructive rather than merely annoying.
  Fixing B8 stops the accident; this stops the silent loss.
- **B7** **The installer declares a version it does not verify it is shipping
  — and this is the root cause of B3.** `%LOCALAPPDATA%\Programs\TickTimer\`
  was installed on **4 Sep** by an installer stamped **31.0.0**, and the two
  exes it laid down are **30.8.1, built 28 Aug**. Add/Remove Programs says
  "TickTimer version 31.0.0", so the owner had every reason to believe the
  Start Menu icon was current — and it was the binary that stripped the
  planner. `deploy-windows.bat`'s apply check compares `Version.h` to
  `ticktimer.iss`: **two declarations, neither of them evidence.** Nothing
  compares the `.iss` string to the `ProductVersion` actually stamped on the
  exe being packaged, which is the only check that would have caught this.
  Same shape as the APK stamp trap in `CLAUDE.md`, on the Windows side, and it
  belongs in `design-addendum-deployment.md` — "a config is not evidence that
  anything was served" was already that file's thesis.
- **B4** **A downgraded document propagates through sync as a deletion.**
  After B3, the local planner has zero schedules while `base-data-<user>.json`
  still lists five, so a three-way merge reads it as "the user deleted them"
  and pushes that to the server and every other device. The base being the
  survivor here is luck, not design.
- **B5** **`syncSchedules` dedupes only on `scheduleId@date`, so a rule cannot
  adopt occurrences that already exist.** Combined with v31.3 relaxing the
  guard from `isFree` to `hasRoomFor` (up to three blocks in a slot), adding
  the weekly rule back onto LOG635 - TP would stack a second identical block
  onto all 13 Tuesdays, silently. The comment above the guard still claims
  "the block you put there by hand outranks the one a rule predicted" — true
  under `isFree`, false now. `CLAUDE.md`'s own trap: a comment that justifies
  code by citing an invariant IS a dependency on it.
- **B6** **"in use (13)" and "Not on the calendar yet." contradict each other
  in plain English**, one inch apart on the same activity. They answer
  different questions — `eventCountUsing()` counts blocks, the dialog asks
  whether a *rule* exists — and both were telling the truth. The word
  "calendar" is doing two jobs. Same class as the folder-delete complaint in
  `design-addendum-folder-archiving.md`: the rule was right, the reason was
  invisible.
- **B2** `/app/` (the WebAssembly build served to iPhones) has not been
  redeployed since v30.4, so the web app and `server/version.json` disagree —
  release step 7, `docs/GITHUB.md`. Nothing hard-fails on this seam by design.

<!-- Add yours here, newest concern at the top of the section. -->

## Features

*One line of intent only. The reasoning belongs in the design addendum you
write when you pick it up — never here.*

- **F12** **Drag a planned block to another slot or day** on the calendar —
  "move Friday's block to Saturday" without opening anything. Note what it has
  to answer that a dialog does not: dropping an occurrence of a recurring rule
  is F7 and F8 all over again, so the scope question has to be settled first or
  the drag will invent its own answer.
- **F13** **Make the day summary drillable.** The glance panel says you spent
  time in a life area; clicking one should open what you actually worked on
  inside it. The data is already there and already derived — Segments carry
  their Event, and `Stats` computes the totals — so this is a view, not a new
  fact.
- **F14** **The assistant should behave like one.** Today it answers when
  spoken to. Wanted: it notices — an open block with nothing tracked, a day
  drifting off plan — and asks. Deliberately large and deliberately vague at
  this stage; needs its own conversation before any addendum. Note it lands
  squarely on ground the project has already surveyed: the v28 arc's whole
  thesis is "code decides WHEN, code computes WHAT IS TRUE, the model only
  PHRASES", and `AssistantVerbs.h` gives Nudge and CheckIn deliberately EMPTY
  verb lists. Anything here is a change to that boundary and must be argued as
  one.
- **F15** An expand icon, top-right of every description box, opening the text
  larger for reading and editing. One control, applied everywhere a description
  is edited — activity notes, task notes, block notes — rather than per screen.
- **F16** **Pieces do not serve a repeating routine, and that needs a
  conversation.** A morning routine is the case: a repeating parent with a
  checklist you want back, unticked, every morning. Two things to establish
  before designing — (a) whether ticking a repeating parent regenerates its
  pieces at all (the spawn carries `parentId` and size, but nothing in
  `setTaskDone` clones a parent's children, so the next occurrence looks empty)
  and (b) what "link multiple pieces" means: one piece shared by several
  parents, or ticking several at once. The owner should say which; guessing
  here would design the wrong feature.
- **F6** Delete a single occurrence of a recurring block, leaving the rule and
  every other date alone. **The domain already does this** —
  `AppData::skipOccurrence(scheduleId, date)` exists and `Schedule::skipDates`
  is persisted; `Schedule.h` calls it "the one piece of state a purely-derived
  design would not need". This is a missing UI door, not a domain change.
- **F7** Move a single occurrence of a recurring block to another date. Unlike
  F6 there is no domain door yet, and it has to answer what happens to the
  occurrence's `scheduleId` — a moved lecture is still that lecture, but the
  rule must not re-materialise the slot it left.
- **F8** The scope question F6 and F7 both need: **"only this occurrence" or
  "this and all future ones"**, asked once and answered the same way by both.
  Every calendar app has this; the reason it is its own item is that answering
  it twice, differently, is how the two verbs drift apart.
- **F9** When picking what to put in an open block, show **only activities** —
  a task gets linked onto the activity afterwards instead of appearing as a
  peer. **Reverses a deliberate decision**: `PickActivityDialog::
  buildChoiceList` lists activities *and* open tasks on purpose ("the picker
  mirrors what the rail already teaches: activities and tasks live side by
  side under a category"), and task blocks were their own arc (design-doc
  §3.25–§3.29). Worth doing, but it is a real design reversal and needs the
  addendum to say why the old reason no longer holds.
- **F10** Make the picker's text box a **live search** over existing
  activities, creating a new one only when nothing matches — instead of
  scrolling a grouped list. The two halves already exist separately
  (`confirmAdHoc`/`enteredTitle` creates; the list browses); this joins them so
  one box does both. Pairs naturally with F9, which is what makes a single flat
  result list possible.
- **F11** **Tell two activities in the same life area apart on the agenda.**
  Restated from the owner after the first filing got the problem wrong: this
  is not "override the life area's colour", it is that "LOG635 - COURS" and
  "LOG635 - TP" are drawn identically, because `AgendaWidget` takes a block's
  colour from `categoryById(eventCategoryId(e))` — the life area — and nothing
  below that level has a colour at all. The lead worth trying first: a block
  already paints TWO colour surfaces, a pastel fill and a 5px raw-hue
  "identity stripe". Fill stays the life area (so a day still reads by area at
  a glance, which is what design-doc §3.4 actually protects), stripe carries
  the activity. An unset activity colour follows its area, which keeps
  §3.4's promise — recolour "Health" and everything that never overrode it
  moves with it — and keeps the JSON additive.
- **F5** A door from "these 13 blocks look weekly" to a Schedule that **adopts**
  them instead of duplicating them — the thing B5 makes impossible today, and
  the only way an activity planned before v31 can ever get a rule.
- **F1** Push notifications, via a narrow `{when, title}` schedule uploaded to
  the server, which fires it blindly. Named in the phones arc as the iOS answer
  (Safari 16.4+ home-screen web apps); it also fixes Android, where
  `BlockAlarmService`'s in-process timer is equally dead when the app is closed.
- **F2** The memory write path — §L.4's confirm loop. Held back deliberately at
  v30.0: memory would be the first thing a model writes that a model later
  reads as prompt.
- **F3** Drag-and-drop a category into a folder in the rail. Carried over from
  the old §4b "polish & habits"; check what v31's manual ordering already gave
  you before designing anything.
- **F4** SQLite instead of one JSON document. Deferred, not rejected — the
  format is versioned and has survived fourteen additive growths plus a
  location migration, so the migration muscle exists. No pressing need while
  JSON holds up; revisit when load time is felt rather than imagined.

## Tests owed

*Code that shipped without a test, and manual passes not yet done.*

- **T1** No automated test covers `Merge.h` when both sides delete the same
  row — the three-way merge's most obviously symmetrical case.
- **T2** v31.1.0 has had no manual QA pass at all: overlapping blocks,
  schedules, folder archiving and manual ordering all shipped since the last
  checklist was written. Run `docs/QA_CHECKLIST.md`.

## Housekeeping

*Documentation and process debt. Small, but it is what rots first.*

- **H5** **The phones are still on pre-v31 binaries.** Windows is done — 31.1.0
  is installed, running, and its registry entry agrees with its exes. The
  sideloaded APK and the WebAssembly app at `/app/` were never updated past
  v30, so both can still strip a format-16 planner through sync exactly as the
  Start Menu copy did. The APK needs `JAVA_HOME` set from
  `%APPDATA%\QtProject\QtCreator.ini` and its stamp checked with `aapt2 dump
  badging` before signing; `/app/` is release step 7 and nothing hard-fails on
  it.
- **H1** `docs/05_RiskList.md` is stamped "reflects the shipped v11 app" and
  declares itself "re-ranked at the start of each iteration". It has not been
  touched in twenty versions. Either re-rank it against v31 (risks #1, #3 and
  #5 read as retired; Android and the web build are live risks it never names)
  or record in `08_DevelopmentCase.md` that it has been retired — the one
  unacceptable state is a document that claims to be living and is not.
