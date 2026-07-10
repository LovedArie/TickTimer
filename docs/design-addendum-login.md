# Design Addendum — Login & Local Accounts

**Status: implemented.** First feature of the "networked TickTimer" arc
(login → sync → share → auto-update). Continues the decision log; this is a
new subsystem, so it gets its own file rather than a §3.x slot.

**The requirement:** a login system, hosted on the user's own laptop for now
(a Raspberry Pi later), with **no dependency on Google or any identity
provider** — the user explicitly wanted to own their credentials.

---

## A. Two programs, one repo

TickTimer becomes a **client + server** pair sharing code:

- `ticktimer` — the desktop/phone app (unchanged in spirit; gains a login
  gate).
- `ticktimer-server` — a headless service storing accounts and checking
  passwords.

They share `AccountStore` + `PasswordHash` via a small static library
(`ticktimer_auth`) so the server and the tests link the *exact same* code —
no chance of testing a different build than production runs. The server is a
`QCoreApplication` (no GUI) linking only `Qt6::Network`; honest about being
headless.

## B. Authentication vs storage — kept separate

Two jobs people conflate: *proving who you are* (auth) and *where data lives*
(storage). Google login only does auth, and chains you to Google. We do our
own auth (username + hashed password) so **storage location is a free
choice** — laptop now, Pi later, all without an identity provider.

## C. Password storage — the one security-critical decision

`PasswordHash.h` never stores the password. It stores
`pbkdf2$<iterations>$<salt>$<hash>`:

- **Salt** (random per user) so identical passwords get different hashes and
  rainbow tables don't work.
- **Stretching** (200k SHA-256 rounds) so brute-forcing a stolen file is
  ruinously slow.
- **Self-describing format** — the leading tag + stored iteration count means
  a future upgrade to **Argon2/bcrypt** (the production-grade choice, flagged
  in-code) dispatches on the tag and nobody has to reset their password.

Verify re-derives using the salt and iteration count *read from the stored
string*, never the current constants — so old hashes keep verifying after a
parameter change. Every property (never-plaintext, per-hash uniqueness,
reject-garbage, case-insensitive names, persistence round-trip) is pinned by
`test_auth` — the most careful tests in the project, because an auth bug
doesn't crash, it silently lets the wrong person in.

## D. The server: QTcpServer, not Qt HttpServer

`Qt::HttpServer` is an optional module absent from many installs (including
the build environment), so depending on it would make the project fail to
configure on some machines. We build on `QTcpServer` + a hand-rolled HTTP
parser instead — always present, and it means the reader SEES what HTTP is: a
request line, headers, blank line, body. One connection, one request, one
response, close. Deliberately minimal, deliberately private (see SERVER.md).

The startup banner prints every reachable address (`localhost` + each LAN IP)
— the fix for "what's my laptop's IP?" the moment the server starts.

## E. The client: async by necessity

`AuthClient` wraps `QNetworkAccessManager`. Network calls can't block the UI,
so the client is fire-and-forget: POST, and a `resultReady` signal delivers a
typed `Outcome` later. `LoginDialog` is purely reactive — submit, disable
inputs, react to the signal — and `accept()`s on success. `main()` shows the
app only if the dialog was accepted: login is a genuine gate without tangling
auth into `MainWindow`.

The server URL is injected from `QSettings` (`sync/serverUrl`, default
`localhost:8080`) — one setting, three deployments (localhost / LAN IP / Pi),
zero code changes. Same seam philosophy as `nowProvider`.

## F. The networking bugs (the session's real cost)

Three stacked bugs made valid 4xx responses arrive as `NetworkError` on
alternating requests — a genuinely nasty debugging session, worth recording:

1. **`reply->error()` is non-zero for HTTP 4xx too**, not just transport
   failures. Testing it alone misreported a valid `409 username_taken` as a
   network error and discarded the body. Fix: branch on
   `HttpStatusCodeAttribute` — if an HTTP status came back, the server
   *answered*; parse the body. Only a truly absent status is a NetworkError.
2. **Empty HTTP reason phrase** (`HTTP/1.0 409 `) — legal but mishandled by
   some clients under connection reuse. Fix: send real phrases ("Conflict").
3. **Connection reuse across a one-shot server** — the server closes each
   socket after responding; QNAM pools and reuses connections, and driving a
   request from inside a nested event loop (exactly how a modal dialog and
   the tests both run) reused a dead socket. Fix:
   `clearConnectionCache()` before each request — correctness over a few
   microseconds of pooling, against a server that never wanted pooling.

Pinned by `test_login_live`, which spawns the REAL server and drives the REAL
client over a real socket through the full register→dup→wrong→login sequence
— the exact path that exposed all three.

## What changed where

| Layer | File(s) | Change |
|---|---|---|
| Security | `include/PasswordHash.h` | salted, stretched, self-describing hashing |
| Server data | `include/AccountStore.h`, `src/AccountStore.cpp` | account registry, register/login doors, tolerant JSON |
| Server | `server/AuthServer.*`, `server/server_main.cpp` | QTcpServer HTTP/JSON service |
| Client | `include/AuthClient.h`, `src/AuthClient.cpp` | async QNAM wrapper → typed Outcome |
| Client UI | `include/LoginDialog.h`, `src/LoginDialog.cpp` | two-mode gate dialog |
| Gate | `src/main.cpp` | dialog before window; server URL from QSettings |
| Build | `CMakeLists.txt` | `ticktimer_auth` lib, server + two test targets, Qt6::Network |
| Tests | `tests/test_auth.cpp`, `tests/test_login_live.cpp` | 9 unit + 4 end-to-end |
| Docs | `docs/SERVER.md` | run instructions + laptop-hosting consequences |

The planner domain (AppData, Stats, tracking): **zero changes** — identity is
a new subsystem bolted in front, not a rewrite of what exists.
