# Design Addendum — Per-Account Local Storage & Data Adoption

**Status: implemented.** A follow-on to sync (part 2 of the networked arc),
closing its headline known-limit: *one account per device*.

**The requirement (owner, after seeing it live):** logging in as a different
user should give that user their own data — and, critically, *my existing
planner must transfer to my account, not disappear.*

---

## A. The problem it fixes

Login gated *access* to the app but every account opened the SAME local
`data.json`. So on one machine, whoever logged in saw the same planner —
accounts were isolated on the server (separate shelves) but shared one local
file. Switching users didn't switch data.

## B. The change: storage scoped to the logged-in user

- **Planner file:** `data-<username>.json` (lowercased, matching the
  server's canonical key) instead of the global `data.json`.
  `JsonStore::filePathForUser("")` still returns the legacy global path, so
  every test and tool that builds a store without a user is unchanged.
- **Sync state:** `sync/lastRevision` / `sync/dirty` become
  `sync/<user>/lastRevision` / `sync/<user>/dirty`, so two accounts on one
  machine don't share a revision counter.
- **Threading:** `MainWindow(const QString& username = {})` scopes both;
  `main()` passes `login.loggedInUser()`. The default-empty argument keeps
  the bare `MainWindow()` that every UI test constructs working untouched —
  a capability GAINED, not a constructor others must now satisfy.

## C. The adoption migration — the part that had to be right

On first launch after this upgrade, `adoptGlobalDataForUser(user)`:

1. If the user's per-account file already exists → do nothing (never clobber
   an account's own data).
2. Else if the old global `data.json` exists → **copy** it into the user's
   file. Existing data transfers to whoever logs in first — realistically
   the person whose data it was.
3. Retire the global file to `data.json.pre-accounts.bak` (rename, not
   delete) so the *next* user doesn't also adopt it, and so nothing is ever
   destroyed.

Three safety properties, each pinned by test
(`adoptionTransfersOldDataOnceThenLeavesOthersFresh`): the first user's data
arrives intact; a second user finds nothing to adopt and starts fresh;
re-running adoption is a no-op. **Copy-then-backup, never move-then-hope** —
the same reliability instinct as `QSaveFile`'s write-then-replace.

## D. Flagged assumption

Adoption gives the legacy planner to the *first user after the upgrade*. The
app can't know the old data "belongs" to a specific username — it belongs to
whoever used this machine. First-login is the honest proxy, and the
`.pre-accounts.bak` backup means a wrong guess is recoverable by hand.

## What changed where

| Layer | File(s) | Change |
|---|---|---|
| Storage | `JsonStore.h/.cpp` | `filePathForUser`, `adoptGlobalDataForUser` |
| Shell | `MainWindow.h/.cpp` | username-scoped store; adoption before load; welcome note |
| Sync | `SyncService.h/.cpp` | per-account QSettings keys |
| Entry | `main.cpp` | pass `loggedInUser()` to the window |
| Tests | `tests/test_auth.cpp` | +3: distinct paths, adoption end-to-end, isolation |
