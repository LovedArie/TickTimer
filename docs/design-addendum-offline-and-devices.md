# Design Addendum — Offline Start and Remembered Devices (v30.2)

*Phase 1 of the cross-platform program: the app has to survive the server
being unreachable before it can live on a phone. Companions:
`design-addendum-login.md` (the gate this reopens), `design-addendum-sync.md`
(what switches on when the server returns), `server/DeviceStore.h` and
`include/SessionStore.h` (this file's two abstracts).*

---

## A. The two problems, which are not the same problem

Scoping the mobile work turned up one blocker and one adjacent misery.

**The blocker:** `main.cpp` made login a hard gate. No reachable server meant
no app — not even to read your own local `data.json`. Invisible on a desktop
sitting next to the server; fatal on a phone, which is the device most often
away from it.

**The misery:** the session token was never persisted, so *every* launch asked
for a username and password. Fine for one developer at a desk. Not fine for
someone who opens the app six times a day.

They are separate, and separating them is what kept this slice honest:

- **Offline start needs only a NAME.** Enough to open `data-<user>.json`. No
  credential, no server change, nothing contradicted.
- **Staying signed in needs a CREDENTIAL**, which needs the server to persist
  something it deliberately refused to persist (§C).

Doing only the first would have shipped an app that opens offline and still
demands a password every time it is online — which on a phone is most of the
misery, unfixed.

## B. Offline start gives away nothing, and that is the argument

The obvious objection is that opening on a remembered name, with no password,
weakens the gate. It does not, because **the gate never protected local data.**

`data-<user>.json` sits in the account's own folder in plain JSON. Anyone who
can read that folder can read the planner, logged in or not. Login proves who
you are *to the server*; it was never a lock on the file. An offline door that
opens on a remembered name therefore hands over nothing that was not already
lying there — it just stops the app pretending otherwise.

What it does have to avoid is opening the **wrong** planner:

- The door appears **only when the server could not be REACHED** — never when
  it answered and refused. A refused credential is not an invitation to work
  offline.
- It is offered only for an account with a planner file **on this machine**.
  Opening a planner that does not exist would greet someone with an empty week
  and call it their data.
- On a machine with several accounts the door names the one it will open, so a
  shared desktop keeps today's "choose who you are" behaviour. On a phone there
  is one account and the question never appears.

**The honest gap:** a device that has never synced has no local file, so there
is nothing to open offline. The first login must happen online. Nothing can fix
that — the data has to arrive once before it can be read without a server.

## C. Device tokens, and why they do not contradict the comment they appear to

`AuthServer` keeps session tokens **in memory**, justified in its own words:

> *"tokens are session state, not records. A server restart forgets them all…
> Persisting tokens would be persisting open doors."*

That reasoning is still right, and **session tokens are untouched** — they
still die with the process. What stopped being true is the sentence it leaned
on: *"logging in again mints fresh ones — which the app already does on every
launch."* A phone does not politely re-log-in on every launch; it gets closed
and reopened constantly, often with no server in reach.

So this is a **second, different credential**, not a persisted session token —
the ordinary split between a short-lived access token and a long-lived refresh
one:

| | lifetime | proves | revocable |
|---|---|---|---|
| session token | in memory, dies on restart | a live session | by restarting |
| **device token** | persisted in `devices.json` | *this device logged in once* | per device, from either end |

The persisted door is therefore not an open one. It opens exactly **one**
thing — a fresh session token for one account — and closes on request without
touching any other device.

**Stored hashed, never raw.** A stolen `devices.json` must be a list of dead
strings, not a stack of working credentials. But the hash is plain **SHA-256,
not** the PBKDF2 that passwords get, and the difference is the point:
stretching exists to make *guessing* expensive, and nobody guesses 128 bits of
CSPRNG output. Paying 200,000 iterations per resume would buy no security and
hand anyone a cheap way to make the server work hard.

**Opt-in, asked for by the client.** Minting a durable credential for every
login — including a one-off login on someone else's machine — would leave
credentials lying around nobody asked to create. Absent key reads as false, so
a client written before this existed behaves exactly as it did.

*Alternative rejected — persist the session token itself.* It would be dead
after any server restart, so the phone would silently fall back to a password
prompt at random intervals. Worse than either honest answer.

*Alternative rejected — store the password locally to re-login.* Storing a
password to avoid typing a password is a trade in the wrong direction.

## D. The model of a launch

1. A remembered device? Offer its token for a session, before anyone sees a
   form. On success the dialog accepts without ever being shown.
2. Server **answered and refused** → the device was revoked or the account is
   gone. Forget the dead credential immediately (retrying it every launch is a
   permanent invisible failure) and show the form with the name filled in.
3. Server **never answered** → offer the offline door, if there is local data.
4. No remembered device → the form, as it always was, now with a
   *"Remember this device"* tick.

**Default ON**, deliberately. The phone this exists for has one owner, and
asking her for a password at every launch is how an app stops being opened. A
shared desktop is the case where it should be unticked — and is exactly the
case where somebody is standing there to untick it.

**Coming back online is silent.** An offline session retries its remembered
device once a minute; the first acceptance calls `enableSync` mid-session and
the Sync button appears. Nobody is asked to do anything — the phone that spent
the morning on mobile data catches up when it gets home. A refusal stops the
timer and says so once; anything else stays quiet, because an offline app that
nags about every failed poll is worse than one that waits.

`enableSync` gained a guard for this: the window may only ever have one sync
stack, or two services would race to push the same planner and the rail would
grow two Sync buttons.

## E. Where the credential lives on the client

`QSettings`, beside `sync/serverUrl`. It is neither taste nor domain data, so
neither existing home was obviously right — but it is machine-local and must
never sync, and `data.json` is exactly wrong for it: that file *does* sync, and
a per-device credential that replicated itself to every device would defeat
its own purpose.

**This is not encryption.** `QSettings` is plaintext. Anyone who can read it
can read `data.json` next door, so the token adds one thing to that exposure —
reach to the *server* copy from elsewhere. That is precisely why revoking
exists and why remembering is a choice rather than an assumption.

## F. Tests

Ten new slots, split by what they can prove:

| Suite | What it pins |
|---|---|
| `test_auth` | the file never contains the token it issued; a device resolves across a restart; unknown/revoked/malformed/empty all resolve to **nobody**; revoking is idempotent and touches no other device; `forgetAllFor` is per account and case-blind; every token is distinct |
| `test_login_live` | the whole loop against the **real** server over a real socket: register-with-remember → resume with no password → a session that can actually pull the planner; not asking leaves no token; a revoked device is refused as **BadCredentials** rather than as a puzzling reply; and revoking the phone leaves the laptop signed in |

That last mapping is a small decision worth naming: `/session` refuses with
`error: "auth"`, which without a mapping would land in `UnknownServerReply` and
tell someone to check their server address over a revoked phone.

Six suites, **445 measured** (205 + 22 + 76 + 98 + 25 + 19; was 435).
Measured, not remembered.

## G. What this does not do

- **No hardening.** The server is still the one `docs/SERVER.md` says not to
  expose. Putting it behind a TLS proxy, rate-limiting `/login`, and closing
  registration is Phase 2 of the cross-platform plan, and this slice does not
  pretend to have done it.
- **No revoke UI.** `DeviceStore` can list and forget; nothing surfaces that
  yet. The label is stored now precisely so that screen has something readable
  to show when it arrives.
- **No token expiry.** A device token lives until revoked. Adding an expiry is
  easy later; guessing a duration now, before anyone has lived with it, would
  be inventing a number to look thorough.
