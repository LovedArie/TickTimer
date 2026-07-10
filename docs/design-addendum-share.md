# Design addendum — Share & compare planners (networked arc, part 3)

*Extends `design-doc.md` and builds directly on `design-addendum-login.md`
(identity, tokens) and `design-addendum-sync.md` (the opaque-blob server).
Status: shipped. Suites: 5 ShareStore unit tests, 2 pure compare tests,
1 UI regression test, 1 live end-to-end test.*

---

## A. The requirement, and what it actually is

*"Being able to share and compare daily planner with others and loved ones."*

Unpacked, that is two features wearing one sentence:

1. **Share** — a *permission* feature: let a chosen person see my planner.
2. **Compare** — a *presentation* feature: my day and their day, side by
   side, in numbers.

The split matters because they live on opposite sides of the wire. Sharing
is enforced on the **server** (a permission checked only in the UI isn't a
permission, it's a suggestion), while comparing happens entirely on the
**client** — and the reason why is the single most important design fact
of the whole networked arc:

> **The server never looks inside a planner.** (`design-addendum-sync` §D)

A server that can't read the blob can't compute "just her focus totals" to
share. So the only shareable unit is *the blob itself*, and "share my
planner" honestly means **"permit this person to fetch my blob."** The
client does the understanding; the server does the permitting. Dumb server,
smart client — unchanged, and now load-bearing for a second feature.

## B. The permission model — one arrow, one meaning

A share is a **directed read grant**: *alice shares with bob* means exactly
"bob may `GET` alice's planner" — and nothing else.

- **Directed.** Alice granting bob says nothing about bob's planner. Mutual
  visibility is two grants, one made by each owner. (The obvious alternative
  — symmetric "friendship" — bundles two people's privacy decisions into one
  click. Each person should give away only what is theirs to give.)
- **Read-only, structurally.** There is no route through which anyone can
  write someone else's planner. Not "forbidden by a check" — *absent from
  the protocol.* The strongest permission system is the endpoint that
  doesn't exist.
- **Revocable, instantly.** Grants are checked on every fetch, so revoking
  closes the door on the very next request.

`ShareStore` persists the grants (`shares.json`,
`{"grants": {"alice": ["bob"]}}`) with the full house style: canonical
lowercase names (the one identity rule shared by all three server stores),
tolerant JSON load, `QSaveFile` writes, and **idempotent** grant/revoke —
a retried "share" on flaky Wi-Fi must not become an error a human sees.

## C. The wire — three routes and a path parameter

| Route | Meaning | Notable answer |
|---|---|---|
| `POST /share` `{with}` | grant read access to `with` | **404** `no_such_user` — a typo'd name fails loudly at the door instead of "working" and confusing both humans later |
| `POST /unshare` `{with}` | revoke | always `{ok}` — revoking a name that was never granted still leaves the requested end state true |
| `GET /shares` | both lists in one round-trip | `{iShareWith, sharedWithMe}` |
| `GET /planner/<user>` | fetch a peer's blob | **403** `forbidden` without a grant |

Two things here are new vocabulary for the project:

- **A path parameter.** `/planner/alice` names alice's planner the way
  `/planner` names your own — nouns in the URL, verbs in the method. The
  hand-rolled router handles it with a `startsWith("/planner/")` prefix
  match; everything after the prefix is the owner's name.
- **401 vs 403**, worth learning once and keeping forever:
  **401** = "I don't know who you are" (bad/expired token → log in again);
  **403** = "I know *exactly* who you are, and the answer is no" (→ ask the
  owner to share). Different problems, different fixes — so they are
  different status codes on the wire and different `Outcome` values in the
  client, the same reasoning that made `AccountStore::Result` an enum
  instead of a bool.

## D. The client — a third sibling, and the rule of three

`ShareClient` is the third wire client (after `AuthClient`, `SyncClient`)
and inherits the family rules verbatim: async `QNetworkAccessManager`, one
typed signal per operation, `clearConnectionCache()` before every request
(QB M3), and status read from `HttpStatusCodeAttribute`, never
`reply->error()` (QB M1).

One refactor landed with it: the "read the status honestly" if-ladder that
`AuthClient` and `SyncClient` each hand-rolled became a single `classify()`
helper in `ShareClient`. Twice is coincidence; **the third copy is when
duplication becomes a function.** (Back-porting `classify` into the two
older clients is deliberately *not* done here — it would churn tested code
inside an unrelated feature. Noted as backlog hygiene.)

## E. Compare — the feature that was already written

The client-side "hard part" turns out to be three lines, and that is the
payoff of two old decisions:

1. `stats::summarize` is a **pure function of an `AppData`** — it has never
   known or cared *whose* data it reads (§3.5, derive-don't-store).
2. `JsonStore::applyJsonObject` turns any planner blob into an `AppData` —
   it's the same deserializer sync has trusted since the arc began.

So: fetch the peer blob → pour it into a **private, snapshot `AppData`**
(owned by the dialog, wired to nothing — it isn't "the app's data", it's a
document being read) → run *the identical summarizer* on both sides →
subtract. The numbers are comparable *because the code path is the same.*

The one genuinely new decision lives in `Compare.h` (pure, header-only,
`SyncPlan.h`'s sibling): **when are two people "even"?** `focusVerdict`
answers with a tolerance — a lead within 5 minutes is `Even`, because this
feature exists to nudge ("mom tracked her walk; good moment to start my
study block"), not to hand out photo-finish rankings. The tolerance is a
parameter, the boundary is inclusive, and both facts are pinned by tests.

## F. The glass

- **`SharingDialog`** (opened from a new 👥 *Share* button that
  `MainWindow::enableSync` adds beside *Sync* — both exist only once a
  session token does). Grant by name, see both lists, *Stop sharing* per
  grantee, *Compare* per grantor. It owns **no policy**: every verdict
  (does that user exist? may you read this?) comes back over the wire.
  It also states the privacy fact where the decision is made: *sharing
  exposes the whole planner*, titles included — the honest consequence of
  the opaque-blob design, written on the door rather than buried here.
- **`CompareDialog`** — one day, two planners: a four-row grid
  (focus/break/distracted/total) × (you/them/Δ), date arrows, and a
  deliberately gentle headline. Deltas are signed at the display edge
  (`formatDelta`) because `stats::formatSeconds` clamps negatives to zero —
  a formatter for magnitudes, reused, not changed.

## G. What was tested, and why these tests

- **ShareStore (5, auth suite):** a permission store is security-adjacent —
  a bug doesn't crash, it *shows someone a planner they weren't given*. So:
  directedness, owner-always-reads-own, canonical case-insensitivity,
  idempotence, persistence across reload.
- **compare (2, domain suite):** the sign convention (mine − theirs) and
  the tolerance boundary, *exactly* (5:00 is Even, 5:01 is Ahead).
- **CompareDialog (1, UI suite):** blob → dialog → labels a user would
  read ("1h 00m" / "25m" / "+35m" / the Ahead headline), pinned to a fixed
  date — never trust "today" in a test.
- **Live (1):** the full lifecycle over a real socket: forbidden → granted
  → readable → revoked → forbidden again, plus 404-on-typo and 401-vs-403.
  The refusals *are* the feature: the happy path proves sharing works; the
  403s prove privacy does.

## H. Limits, named out loud (the honest-shipping habit)

- **Whole-planner exposure.** A grant shares everything — event titles,
  task names — not just totals. Finer-grained sharing ("totals only") would
  require either the server understanding blobs (breaks §A) or the client
  publishing a second, redacted document (a real option; backlog).
- **Compare is a snapshot.** The peer blob is fetched when *Compare* is
  clicked; it's as fresh as their last sync, and doesn't live-update.
- **No notification.** Being granted access is discovered by opening the
  dialog. A "share invite" flow needs server-pushed events — same machinery
  auto-sync would need; they'll likely arrive together.
- **Grant list scans.** `ownersSharedWith` is a linear scan of a JSON map —
  correct and instant at home-server scale, and the day this store holds
  thousands of users is the day it stops being a JSON file anyway.
