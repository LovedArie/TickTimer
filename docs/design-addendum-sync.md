# Design Addendum — Sync Between Devices

**Status: implemented.** Part 2 of the networked arc (login → **sync** →
share → auto-update). Builds directly on the login addendum: sync is what
makes the session token *do* something.

**The requirement:** the same planner on two devices (desktop now, phone
later), through the self-hosted server — with no silent data loss, ever.

---

## A. The model: full-document sync with revision numbers

The server stores **one planner document per account** plus a revision
counter that bumps on every store. A client pushing says *"I'm based on
revision N"*; if the shelf has moved past N, the push is refused with **409
Conflict** (and the current revision attached) instead of overwriting.

This is **optimistic concurrency** — the same idea as HTTP's
`ETag`/`If-Match` pair: proceed assuming no one interfered, and let the
version check catch you when someone did. It *detects* every conflict
without *merging* anything.

*Fenced for later:* per-entity merging (or CRDTs) — real machinery, its own
multi-session project. Full-document + detection is the honest v1: it can
never lose data silently, only ask.

## B. The truth table — the whole sync brain

Two questions decide everything:

| server moved? | local dirty? | action |
|---|---|---|
| no | no | **Nothing** — already in sync |
| no | yes | **Push** — upload our changes |
| yes | no | **Pull** — take the server's version |
| yes | yes | **Conflict** — a human chooses |

`sync::decide()` in `SyncPlan.h` is that table as a **pure function** — no
network, no files, no clock — so the test *is* the table (all four rows,
`test_auth`). The messy async plumbing in `SyncService` is thin wiring
around a decision that cannot surprise us. Same discipline as
`Event::isLiveAt`: extract the verdict, test it exhaustively, keep the
wiring dumb.

The two inputs are device state, persisted in QSettings (`sync/lastRevision`,
`sync/dirty`) — *where this machine stands relative to the server* is a
preference-shaped fact, not domain data. The dirty flag rides
`AppData::changed()`: every mutation already announces itself, so sync gets
change-tracking for free.

## C. Layers: wire / policy / glass

| Layer | Class | Knows about |
|---|---|---|
| wire | `SyncClient` | HTTP, tokens, status codes — moves `QJsonObject`s |
| policy | `SyncService` | revisions, dirty flag, the truth table, conflicts |
| glass | `SyncDialog` | buttons and labels — renders the service's state |

The same widgets-report/pages-decide split as the rest of the app, one level
up. `SyncClient` inherits the three QB-M lessons from day one
(`clearConnectionCache` per request, branch on `HttpStatusCodeAttribute`,
never trust `reply->error()` alone) — paid for once, applied forever.

## D. The server stays dumb

The server never parses a planner. `PlannerStore` shelves an **opaque blob**
with a revision — no Event, no Category, no format knowledge. The planner
format can go v6 → v7 → v20 and the server never needs an update; there is
simply less server code to get wrong. The only server-side "schema" is
`{"revision": N, "data": <whatever>}`.

Corollary on the client: `JsonStore` was split into conversion
(`toJsonObject` / `applyJsonObject`) and file I/O (`load` / `save` are now
thin wrappers). **The same conversion feeds the disk and the wire** — a
pushed planner is byte-for-byte the planner that would have been saved. One
format, two destinations, zero drift.

## E. Applying a pull: the loud door and the guard

`resetFrom` is deliberately silent (startup — nobody listening yet). A live
pull needs every screen to rebuild and the autosave to fire, so `AppData`
gained **`replaceAll`** = resetFrom + `changed()`. Same data motion,
different audience.

That announcement created the session's subtlest trap: the sync service
itself listens to `changed()` to set the dirty flag — so applying a pull
would **mark the pull dirty**, and every sync would immediately claim new
work to push, forever. The `m_applying` reentrancy guard (set around
`applyJsonObject`) breaks the loop. Pinned by the live playbook test:
after a pull, `dirty()` must be false.

## F. Sessions: the token becomes identity

Sync requests carry `Authorization: Bearer <token>` — **no username
anywhere in the request**. Whoever holds a valid token gets exactly that
account's shelf; a bad token gets 401 regardless of what it asks. Tokens are
minted on login/register (128 bits of system randomness) and held **in
memory only** on both ends: a server restart forgets them all, clients get
401, and the fix is logging in again — which the app does every launch
anyway. Persisting tokens would be persisting open doors.

Because usernames became server-side **filenames** (`planners/<name>.json`),
registration now restricts the charset (`[A-Za-z0-9_-]{1,32}`): never let
raw user input become a file path. Belt at the door (`AccountStore`), braces
in the store (`PlannerStore` strips anything unsafe anyway).

## G. First-run dirty = true, on purpose

A device that has never synced must assume its local data matters.
Consequences, both directions:

- A machine with existing data **pushes it up** on first sync (right).
- A brand-new second device meets a **one-time conflict prompt** and picks
  "use server version" (safe — one question beats one silent overwrite).

## I. Merge before asking (v31.2)

**The defect, in the owner's words:** *"I've been testing both the android
and desktop separately."* Two devices, real edits on each, and §B's truth
table could only answer **Conflict** — a prompt whose two buttons both threw
away a device's day. It happened twice in one afternoon, and the live test
suite had the failure written down as expected behaviour: row 4 of
`syncServiceRunsTheWholePlaybook` added *different* categories on two devices
and asserted `// the server's two, ours gone`.

**"Both sides changed" almost never means "both sides changed the same
thing."** Tasks added on a phone and schedules added on a laptop touch
nothing in common. Treating that as a conflict is not caution; it is a
failure to look.

**Choice:** a **three-way merge**, client-side, before any question is asked
(`Merge.h`). Only entities edited on *both* sides — or edited here and
deleted there — survive as a question, and the dialog names them.

**Why three-way and not two.** Given only local and server, an entity on one
side is ambiguous: added there, or deleted here? A two-way union answers
"added" every time and silently resurrects everything you have ever deleted.
So the client keeps the document as it stood at the last successful sync —
the **base** — and every question becomes decidable: present in base and now
absent *is* a delete. This is git's model.

**Rejected — per-entity `updatedAt` and tombstones** (how TickTick and
Todoist do it, and the first thing to reach for). Their sync unit is the
entity, so a timestamp per row is the natural key. Ours is a document, and
the base already answers what those timestamps would: it says what both
sides started from, which is strictly more than "when was this touched".
Adopting them would mean stamping every mutation in `AppData` and inventing
tombstones so a delete could out-rank a stale copy — machinery to learn what
we can already read.

**Rejected — newest document wins, automatically.** The owner's own first
suggestion, and it is the failure mode dressed as a feature: with
whole-document sync, "newest wins" discards the other device entirely. It
would have silently destroyed work in exactly the two cases that prompted
this.

**Generic over collections, deliberately.** The merge walks whatever
arrays-of-objects-with-an-`id` it finds, so tasks, events, activities,
categories, folders, schedules, special days and moods are one pass — and the
collection added next version needs no edit. A merge taught each collection
by hand is a merge that silently skips the one somebody forgot.

**`running` stays local.** It is the crash-insurance block naming the timer
*this* machine has going; adopting the other's would claim you are tracking a
block you are not sitting in front of. That also closes half of §H's
"sync while a timer runs" limit.

**The base is a file, not a setting** (`base-data-<user>.json`, written with
`QSaveFile`). It is a whole planner — 219 KB for the owner — and the registry
is no place for that. A truncated base would be worse than none: it would
look valid and claim deletions that never happened, so write-then-replace is
not optional here.

**Fail-safe when the base is missing** (first sync on a device, or a lost
file): everything looks added on both sides, so nothing is deleted and only
genuinely-differing rows are flagged. The pessimistic direction, chosen on
purpose — losing data because a file went missing would be the worst
possible trade.

**"Keep mine" no longer means "discard the other device."** Both candidate
documents are fully merged; they differ only in who wins the contested rows.
The dialog says so, and names them — a choice between two unnamed versions is
a coin toss, not a decision.

### §I.a Naming the two sides, and the question that wasn't one (v31.2)

§I ends by saying the dialog "names them — a choice between two unnamed
versions is a coin toss, not a decision." It named the ROWS. It did not name
the **sides**, and the owner said so: *"it states what the conflict is, but it
doesn't say from which side the conflict is coming from."* "Lab 4" is the name
of a disagreement, not a description of it, and the buttons offered are one
per device.

Reading `merge::plan` to fix that turned up the larger half. It appends a
`Clash` in three places, and they are not the same situation:

| situation | what `plan()` keeps | do the buttons decide it? |
|---|---|---|
| edited on both sides, differently | `preferServer ? server : local` | **yes** |
| we edited it, they deleted it | `local`, always | no |
| they edited it, we deleted it | `server`, always | no |

`preferServer` is not consulted in the last two — correctly, since "losing an
edit to a delete silently is the one thing a merge must never do on its own."
But the dialog listed all three together and asked one question over the lot,
so for two of the three shapes a person weighed a decision that had already
been made and whose answer was then discarded.

**Choice.** `Clash` carries a `Kind`, and `decidedByPreference(kind)` answers
"do the buttons govern this row?". The dialog splits on it: contested rows sit
above the buttons, already-settled rows below them under *"Kept automatically
— a delete never overrides an edit."* Each row names what both sides did.

**Alternative rejected:** have `SyncDialog` re-derive the shape by diffing the
held server document against local. The dialog would then hold a second,
independent opinion about what clashed and why — the same two-answers split
§S.11a of the schedules addendum was written to close. The merge already knew;
it discarded the knowledge one line later, where `SyncService` flattened
`QVector<Clash>` down to a `QStringList` of labels.

**The sentences live in `Merge.h`, not in the dialog**, for the reason
`recur::summaryLines` lives in `Recurrence.h`: a pure function can be pinned
by a table of microsecond tests, and a `QLabel` inside a modal cannot be
reached at all. It also keeps the claim and its justification in one file —
the dialog asserts a row is "kept automatically", and `decidedByPreference` is
why that is true. They must not be editable apart.

**And the question that was never a question.** If *every* clash is an
edit-vs-delete, both candidate documents are byte-identical: the modal offers
one outcome twice. `SyncService` now takes the no-question path there, the
same one a fully-merged sync takes. A test pins the claim underneath
(`plan(..., false).merged == plan(..., true).merged` for those shapes), so
anyone "completing" `plan()` by consulting `preferServer` in those branches
finds out from the suite instead of from a user reading a dialog that lies.

**A latent bug fell out of that**, and it is the more serious half of this
section. The held-conflict fields were assigned *above* the
silent-merge branch — before it was known whether a question would be asked —
so a conflict that resolved itself perfectly still left
`m_heldServerRevision` set, and `hasPendingConflict()` reads exactly that
field. Auto-sync gated shut, the ⚠ lit, the resolution box on every open,
after a *successful* sync. That is the third appearance of the same failure
(`TROUBLESHOOTING.md`), and the fix this time is not another janitor call but
the shape the first two should have taken: **nothing is held until the path
that needs it is the path being taken.** The second merge moved down with it,
since nothing above the branch needed it.

## H. Known limits (honest list, all deliberate v1 scope)

- ~~**One account per device**~~ — **CLOSED (accounts addendum).** Local
  storage is now per-account: `data-<username>.json` instead of one global
  `data.json`, and sync-state keys are namespaced `sync/<user>/…`. Switching
  users switches local files. Your pre-accounts `data.json` is adopted by
  the first user to log in after the upgrade (copied in; the original kept
  as `.pre-accounts.bak`).
- ~~**Sync while a timer runs** pulls the other device's `running` state over
  yours.~~ — **CLOSED for the merge path (§I):** `running` is device-local
  state and the merge keeps yours. A raw *pull* (server moved, we are clean)
  still adopts theirs; that path has no competing local edit to protect, so
  it is left alone.
- **Push race** (server moves between our pull and our push): reported as
  "press Sync now again" — the next pull re-runs the truth table. Rare on a
  home setup; correct, just not automatic.
- **No auto-sync**: manual "Sync now" only. Automation is one timer away
  once the manual path has earned trust.
- **Field-level merging is not attempted** (§I). Two devices editing the
  *same* task still produce one question, even if one changed the title and
  the other the due date. Merging inside an entity needs per-field
  provenance, which is the `updatedAt` machinery §I declined — worth
  revisiting only if same-row collisions turn out to be common, and on one
  person's two devices they are not.

## What changed where

| Layer | File(s) | Change |
|---|---|---|
| Domain | `AppData.h/.cpp` | `replaceAll` — the loud replacement door |
| Storage | `JsonStore.h/.cpp` | split conversion from file I/O (sync hooks) |
| Server | `PlannerStore.h/.cpp` | versioned opaque-blob shelf, per account |
| Server | `AuthServer.h/.cpp` | session tokens; `GET/PUT /planner`; 409 check |
| Server | `AccountStore.cpp` | username charset gate (filenames!) |
| Client wire | `SyncClient.h/.cpp` | pull/push with Bearer auth |
| Client policy | `SyncService.h/.cpp` | truth table wiring, dirty bit, conflicts |
| Client glass | `SyncDialog.h/.cpp` | Sync now + explicit conflict choice |
| Client | `AuthClient`, `LoginDialog` | token passthrough |
| Shell | `MainWindow`, `main.cpp` | `enableSync`, rail button, token handover |
| Tests | `test_auth` +3, `test_login_live` +3 | table, shelf, wire, playbook |
