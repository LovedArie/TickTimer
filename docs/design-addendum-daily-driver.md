# Design addendum — The daily-driver pass (owner feedback, items 1–8)

*Extends `design-doc.md`. Status: shipped, v19.1.0, data format v6 → v7
(additive). This session is different in kind from the arcs before it:
every feature here came from the OWNER'S OWN LIST after living with the
app — the moment a project stops being built from a plan and starts being
built from use. Suites: 4 new domain tests; all 83 green.*

---

## A. Format v7 — four additive fields, zero migration

`Task.archived`, `Task.priority`, `Activity.archived`, `SpecialDay.color`.
Every one follows the house rule (§4): a missing key reads as a safe
default (`false`, `Medium`, `false`, invalid-colour = "no choice"), so a
v6 file loads unchanged and an unknown priority string fails safe to
Medium. Seven format versions, still not one migration branch — this is
what additive growth keeps buying.

## B. Archive — hide, never forget (items 3, 4, 5)

The model gains a third life stage: **open → done → archived**. The
distinctions carry the design:

- **Done ≠ archived.** A checked-off task stays visible — today's
  victories belong on today's list. *Archive* is the separate, deliberate
  "out of my sight" (a button that appears only once a task is done).
- **Archived ≠ deleted.** `removeActivity` has always refused to delete an
  in-use activity (history would dangle) — which made in-use activities
  *unretirable*. Archive is the missing exit: gone from every list and
  picker, still resolvable by id, so every old day keeps its name and
  colour. Fully reversible.
- **The ArchivePage** sits *below the stretch* in the nav, with Sync and
  Share — furniture, not a destination. (The owner asked for
  "settings → archive"; there is no settings system yet, so the archive
  got the tucked-away placement without the settings project. When
  settings exist, it moves in.) Tasks get Restore + delete-forever (tasks
  reference nothing — safe); activities get Restore only — the page
  doesn't advertise buttons the domain would bounce.

## C. Priority — a rank, not a reordering (items 6, 7)

`Task::Priority { Urgent, Medium, Low }`, **Medium by default** — an
unranked task is ordinary, because an urgent-by-default world makes
"urgent" meaningless within a week. Set in the detail panel (the combo's
index *is* the enum value — no mapping table to drift, same trick as
Repeat).

Display follows one rule: **chips must stay rare to stay readable.**
Medium is silent; Urgent shouts in the danger hue; Low whispers grey.
Upcoming gains four *lenses* — All / Urgent / Medium / Low — as tabs, not
four side-by-side lists (one lens at a time; the overdue / this-week /
later buckets keep working inside whichever lens is on). The filter is
view-state on the page, not data: it dies with the window, on purpose.

## D. Honest tracking — facts editable by their owner (item 2)

The owner's report: *"I studied longer than scheduled, or started without
pressing focus — false data."* Diagnosis first: running long while the
timer runs is NOT false data (segments are timestamps; overflow is
captured). The real hole is the **missing fact** — studied, never pressed
focus — and its mirror, the **false fact** (wrong button, timer left
running into lunch).

So EventDialog grew a *Tracked time* editor: the segment list with a ✕
per row (`removeSegment` — by position, refusing out-of-range rather than
clamping: a retraction must name exactly the fact it retracts), and an
add row (kind + start → end, pre-seeded from the *plan*, because the
likeliest correction is "I did what I planned, the timer just wasn't
running"). The deep point: a manual segment enters through **the same
door** as a timer's (`appendSegment`) — a fact is a fact, whoever typed
it, and stats/glance/compare update instantly because they derive.

## E. Compare becomes a planning screen (item 1 — v2, owner feedback on v1)

v1 showed the schedules as text lists; the owner's reaction — *"I wanted
both agendas side by side, and to edit mine right there"* — reframed the
feature: comparing isn't a scoreboard, it's **planning**. v2:

- **Two real `AgendaWidget`s in ONE shared scroll.** Same widget, same
  `kSlotHeight`, one scrollbar — 09:00 on your side is 09:00 on theirs to
  the pixel, so "are we both free at 7?" is answered by your eyes.
- **Your side is fully live**: click a free slot to plan (the same
  PickActivityDialog → three-doors recipe as PlannerPage), click a block
  for EventDialog, drag an edge to resize. The screen adds ZERO new
  mutation paths — it's a new place to reach the existing, guarded ones.
- **Their side is untouchable by physics, not discipline**:
  `WA_TransparentForMouseEvents` on the peer agenda. Merely skipping the
  signal connections wouldn't do — drag feedback lives inside the widget,
  and a block that wiggles when dragged but never saves would be a lie.
- **The const moved, honestly.** v1's `const AppData*` said "a compare
  screen mutates nothing" — true until comparing turned out to be
  planning. Requirements changed; the const moved to where it still tells
  the truth (the peer snapshot). Constness is a design statement, and
  design statements get revised like any other.
- The numbers stayed (grid + gentle headline), now a side column,
  re-deriving on every edit.

Still true from v1: the peer data is a **snapshot** from the moment
Compare opened (fresh data = reopen), and the whole thing runs on the
share blob + snapshot-AppData machinery bought two sessions ago.
Field-tested addition: the server can only hand out what the peer has
**pushed** — sync is a manual button — so a never-synced peer used to
open as a silently empty day, indistinguishable from "has no plans".
Now SharingDialog refuses with the true sentence ("ask them to press
Sync first"), and each agenda column labels its own freshness
("live — edit freely" / "as of their last sync"). An app that knows
something the user needs must SAY it.

## F. Special days grow up (item 8)

Edit dialog (name, date, yearly, colour) → one coarse `updateSpecialDay`,
same one-action-one-mutation rationale as `updateTask`, same birth rules
on edit (no empty names, no invalid dates). The colour: a **valid QColor
is "the owner chose"** and drives the card's accent; invalid means "no
choice" and the old urgency colouring (green today, warm this week, calm
later) stands — absence-as-default, the TBD-date trick again.

## G. Polish that shipped alongside

- The server now **prints its data folder** on startup — this line exists
  because it once didn't (the version.json hunt). A service should
  announce where its state lives.
- The update banner's dismiss became findable (bordered, hover state) —
  direct owner feedback from the first real sighting.
- v19.1.0 via `Version.h` — the single source of truth doing its job:
  one edit, both exes and the update check follow.

## I. Auto-sync — a debounce, not a heartbeat (owner feedback, round 2)

*"Manually syncing isn't user friendly."* Correct — and the fix is a
**debounce inside SyncService**: every `AppData::changed()` restarts a
one-shot 5-second timer, so the push fires after the *last* change in a
burst. Drag a block through six slots: the server hears one push, not six
(the live test pins exactly this — three edits, revision +1).

The boundaries are the design:
- **Auto means auto-WHEN, never auto-WHO-WINS.** The timeout calls the
  same `syncNow()` the button calls — same truth table, same
  never-silently-resolve rule. A conflict *pauses* auto-sync (re-ramming
  an unresolved conflict would spam a human who already knows).
- **A conflict can now arrive with no dialog open** — so the held state
  became *queryable* (`hasPendingConflict()`), the Sync button turns
  **⚠ Sync** as the nudge, and SyncDialog checks the state on open
  instead of only listening for future signals. Signals only reach the
  living; state answers latecomers.
- It lives in the **policy layer**, not MainWindow — which is why a live
  end-to-end test could prove it against a real server.

## J. The login door opens on Log in (and a bug confession)

The owner asked for login-first — and the fix uncovered that the ctor's
mode-setting dance flipped `toggleMode()` **twice** and had always opened
on *Create account*, while its own comment claimed login. Worse: our UI
test had already met this bug (it couldn't find a "Log in" button) and we
worked around the symptom with an objectName lookup instead of reading
three lines. Lesson, engraved: **when a test can't find what the code
claims exists, the claim is the suspect.** Now pinned by a test that
checks the button says "Log in" on open.

## K. Whole life areas retire (format v8)

The owner's real archive use case is *semesters*: a term's classes live
in one category — term ends, the **area** retires whole. `Category` gains
`archived` (v8, additive as ever), with one cascade rule named once and
reused: **hidden = own flag OR the owning category's flag**
(`AppData::taskHidden`). Crucially, archiving an area sets *no flags on
its children* — restore the category and everything returns exactly as it
was, because nothing was touched. The ActivitiesPage repeats the pattern
one level up: delete when empty, **Archive area** when in use; the
Archive page grew a Life Areas section.

## H. Limits, named out loud

- Archived things still **sync and share** (they're in the blob) — a peer
  comparing schedules could see events referencing an activity you
  archived. Correct (history is history), but worth knowing.
- Priority doesn't **reorder** Upcoming inside a lens (date still rules);
  a combined urgency-and-date sort is a real design question, deferred.
- The segment editor trusts the human: it will accept overlapping
  segments (the timers never produce them, a person correcting their day
  might legitimately mean to). Facts over validation, deliberately.
- The Archive page will migrate into a real settings area when one
  exists; today's nav placement is the honest minimum.
- Auto-sync pushes 5s after edits — but only while the app runs; edits
  made seconds before closing race the debounce. A push-on-quit hook is
  the obvious next stitch.
- Restoring an archived life area restores its *tasks and activities*
  visibility wholesale; per-child exceptions ("restore the area but keep
  one old class hidden") would need the child's own flag — which already
  exists. The two flags compose; the UI just doesn't surface the combo.
