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

## H. Known limits (honest list, all deliberate v1 scope)

- ~~**One account per device**~~ — **CLOSED (accounts addendum).** Local
  storage is now per-account: `data-<username>.json` instead of one global
  `data.json`, and sync-state keys are namespaced `sync/<user>/…`. Switching
  users switches local files. Your pre-accounts `data.json` is adopted by
  the first user to log in after the upgrade (copied in; the original kept
  as `.pre-accounts.bak`).
- **Sync while a timer runs** pulls the other device's `running` state over
  yours. Guidance: sync while idle. A tracker-aware guard is a future nicety.
- **Push race** (server moves between our pull and our push): reported as
  "press Sync now again" — the next pull re-runs the truth table. Rare on a
  home setup; correct, just not automatic.
- **No auto-sync**: manual "Sync now" only. Automation is one timer away
  once the manual path has earned trust.

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
