# v29.0.1 — the trailing-slash patch (a live field find)

*Found minutes after v29.0 shipped, on the first second-machine setup: a
pasted server URL ending in `/` made Create account fail with "Please
check your details and try again" — the network was fine, the password
was fine, and the message pointed at both anyway.*

## The mechanics

`base + "/register"` with a trailing-slash base posts to `//register`;
the server's route match is exact (correctly — strict beats fuzzy at a
trust boundary), answers 404 `not_found`; and the client collapsed every
unrecognized error token into InvalidInput's credential-blaming message.
Reproduced live against the real server before touching anything: the
double-slash request 404s, the identical clean request mints a token.

## The fixes

- `AuthClient::normalizeServerUrl` — trims whitespace, strips trailing
  slashes, never eats a scheme's own `//`, preserves deliberate paths.
  Applied at **every entry**: AuthClient's constructor and setter, and
  SyncClient's constructor (same base URL, same landmine, defused before
  it ever fired there). When a user can paste it, the program normalizes
  it — vigilance is not a mechanism.
- `Outcome::UnknownServerReply` — an error token the client doesn't
  recognize is *not* the owner's typo. It now says so: "The server
  answered, but not in a way this app understands…" The catch-all is
  precisely the case you didn't foresee, so it must name itself.

## Numbers

**369 tests** (login_live 11 → 14): the normalization table, the exact
failing call end-to-end (`http://localhost:8091///` registers fine), and
wrong-path → UnknownServerReply. TROUBLESHOOTING gains the
symptom-indexed entry. Both version files at 29.0.1, checked.
