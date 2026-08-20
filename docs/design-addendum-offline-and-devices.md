# Design Addendum — Offline Start, Remembered Devices, and Hardening (v30.2, v30.2.1)

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

Thirteen new slots, split by what they can prove:

| Suite | What it pins |
|---|---|
| `test_auth` | the file never contains the token it issued; a device resolves across a restart; unknown/revoked/malformed/empty all resolve to **nobody**; revoking is idempotent and touches no other device; `forgetAllFor` is per account and case-blind; every token is distinct |
| `test_login_live` | the whole loop against the **real** server over a real socket: register-with-remember → resume with no password → a session that can actually pull the planner; not asking leaves no token; a revoked device is refused as **BadCredentials** rather than as a puzzling reply; and revoking the phone leaves the laptop signed in |

That last mapping is a small decision worth naming: `/session` refuses with
`error: "auth"`, which without a mapping would land in `UnknownServerReply` and
tell someone to check their server address over a revoked phone.

The v30.2.1 hardening adds three more, all in `test_login_live` because they
are HTTP-level facts: the invite gate refuses without the code and accepts
with it (and never asks for one on *login* — getting that wrong would lock
everyone out the day the code changed); the brake engages, forgives a success,
and holds against a correct password once tripped; and the preflight answers
204 with the method and header lists a browser needs, with a body length that
agrees with the header. Each of the three runs against its OWN server process,
because the throttle counter is per client address and every test here arrives
from 127.0.0.1 — isolating by process is the only way they stay
order-independent.

Six suites, **448 measured** (205 + 22 + 76 + 98 + 25 + 22; was 435).
Measured, not remembered.

## G. Hardening (v30.2.1) — earning past SERVER.md's own warning

`docs/SERVER.md` said, correctly, *"a development server with no hardening. Do
not expose it to the public internet."* A VPS is the plan, so that warning had
to stop being true rather than be ignored. Three changes, in order of how much
they matter.

**The default bind address is now `127.0.0.1`.** It was `QHostAddress::Any` —
right for a laptop serving a phone on the same Wi-Fi, and exactly wrong the day
the same binary runs on a public box. One forgotten flag was the whole distance
between those two sentences. Now the risky choice is the one somebody types
(`--bind any`), and the reverse proxy is the only thing that reaches the parser.

The cost is real and paid on purpose: a phone on the same Wi-Fi cannot reach a
laptop-hosted server until `--bind any`. That failure is **loud** (the phone
cannot connect) and one flag from fixed; the failure of the old default is
silent and not.

**Registration can be invite-gated** (`--invite CODE`). An open signup endpoint
on the internet is the actual risk here — bigger than the parser everyone
worries about. Checked **before** the account store is touched, so a wrong code
cannot reveal whether a username was free. One shared code rather than
per-account invites: tracking single-use invites needs a store, an expiry
policy and a UI to mint them, for a handful of people who can be told a word.

Registration stays **open by default**, because a closed default makes the
first account impossible to create. The startup banner warns on the
**combination** — every interface *and* open registration — rather than on
either half, because each alone is fine and only together are they an open door.

**A login brake:** five failed attempts from one address in five minutes earns
`429`, including on a correct password. Only *failures* count and a success
forgives them, so fumbling your own typing never locks you out.

*Done in the server, not the proxy, which is a change from the plan.* Stock
Caddy has no rate-limit directive — it needs a plugin and a custom build via
xcaddy. **Making the safe deployment depend on compiling your own web server is
how the safe deployment does not happen.** Twenty lines here work behind any
proxy, or none.

The brake counts per client, and behind a proxy every request arrives from
`127.0.0.1` — so `X-Forwarded-For` is honoured, but **only from a loopback
peer**. Trusting that header from an arbitrary peer would let anyone mint a
fresh identity per attempt and defeat the brake entirely.

**CORS preflight** (`OPTIONS` → 204, with `Allow-Methods` and `Allow-Headers`)
is answered before anything looks at the path. A browser preflights any request
carrying `Content-Type: application/json` or an `Authorization` header — which
is every call this API has — so without it a web client fails *before* its real
request is sent, and the failure looks like the server being down. Needed by the
WebAssembly build even served same-origin, because the preflight is triggered by
the **headers**, not only by the address.

`*` stays safe for `Allow-Origin` here specifically because this API
authenticates with a bearer header and never a cookie.

**And the gate needed a key.** Gating the server without giving the client a way
through would have been half a feature, so `AuthClient::registerUser` takes an
invite code and the login dialog grows a field for it, shown in register mode
only. Two new outcomes came with it: `InviteRequired`, and `TooManyAttempts` —
which is deliberately **not** mapped to "wrong username or password", because
the brake may well have caught a correct one and that advice would have someone
retyping something that was right all along.

Deployment templates ship as `deploy/Caddyfile.example` and
`deploy/ticktimer.service.example` rather than as prose to retype, and
`SERVER.md`'s §4 now describes the hardened deployment instead of forbidding it.

## H. What this does not do

- **No TLS in the server itself**, and none wanted: Caddy terminates it. A
  hand-rolled parser should not also be a hand-rolled TLS endpoint.
- **No revoke UI.** `DeviceStore` can list and forget; nothing surfaces that
  yet. The label is stored now precisely so that screen has something readable
  to show when it arrives.
- **No token expiry.** A device token lives until revoked. Adding an expiry is
  easy later; guessing a duration now, before anyone has lived with it, would
  be inventing a number to look thorough.
- **The live suite got slower** — 9s to ~41s, because the brake test makes many
  sequential connections and Windows slows repeated socket reuse within one
  process. A harness artifact, not a product one: the app makes one login per
  launch, not twenty. Recorded rather than hidden, and the other five suites
  still finish in about five seconds.
