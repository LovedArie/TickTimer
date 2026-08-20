# Running the TickTimer server (login & accounts)

TickTimer now has a **login gate**. Accounts live on a small server program —
`ticktimer-server` — that you run yourself. This doc covers running it on your
laptop, and the honest consequences of doing so.

## Two programs now

| Program | What it is | Runs where |
|---|---|---|
| `ticktimer` | the app you've been building | your desktop / phone |
| `ticktimer-server` | stores accounts, checks passwords | your laptop (for now) |

They talk over HTTP+JSON. The app asks the server "is this login valid?"; the
server answers yes/no. Nothing about your *planner data* is on the server yet —
this session is only **identity**. Sync comes next.

## Running the server

From the build directory:

```
./ticktimer-server                 # default: data in the app folder, port 8080
./ticktimer-server C:\ticktimer-srv 8080   # custom data dir + port
```

On startup it prints exactly where it can be reached:

```
TickTimer server listening on port 8080
  from THIS computer:      http://localhost:8080
  from another device:     http://192.168.1.42:8080
```

That second line is the fix for "what's my laptop's IP?" — the app on another
device (your phone, later) points at that address.

## Pointing the app at the server

The app reads the server address from its settings key `sync/serverUrl`,
defaulting to `http://localhost:8080`. On the same laptop, the default just
works. For a phone on the same Wi-Fi, set that key to the `192.168.x.x` address
the server printed.

## The consequences of hosting on your laptop (read this)

**1. The server only exists while your laptop is on and the program is
running.** Close it or let the laptop sleep, and login/sync pause. Nothing is
lost — the app keeps its own local `data.json` — but a second device can't
reach the server until it's back. (A Raspberry Pi later would run it 24/7.)

**2. Same-Wi-Fi only.** `localhost` means *this machine*. Another device can
reach your laptop only on the same home network, via the `192.168.x.x`
address. It will **not** work when you're on campus and the laptop's at home.
Treat sync as "same Wi-Fi only" for now.

**3. Your laptop's IP can change.** Home routers rotate addresses, so the
`192.168.x.x` your phone used yesterday might differ today, silently breaking
the connection. The server prints its current address on every start so you're
never guessing; a permanent fix is a "static lease"/"address reservation" in
your router settings.

**4. On your own network by default — and the flag that changes that.**
Since v30.2.1 the server binds to **localhost only** unless you say otherwise.
For a phone on the same Wi-Fi, start it with `--bind any`:

```sh
ticktimer-server --bind any --port 8080
```

The old default was "every interface, always", which is right at home and
exactly wrong on a box with a public address — one forgotten flag stood
between a hand-rolled HTTP parser and the open internet. Now the risky choice
is the one you type.

**5. Firewall prompt is normal.** The first time the server opens its port,
your OS asks "allow this app through the firewall?" — allow it for **private
networks** only. Expected, not a warning sign.

**Do not put this server directly on the internet.** Not because it is
hopeless, but because there is a correct way to do it, and it is in the next
section.

## Putting it on a VPS, properly

The rule: **the reverse proxy is the only thing that talks to TickTimer.**
Caddy is a hardened, battle-tested web server that terminates TLS, gets you a
certificate automatically, and absorbs the malformed requests, slow-loris
attempts and scanner noise that a hand-rolled parser should never have to
meet. TickTimer stays on `127.0.0.1`, where only the proxy can reach it.

**1. Point a domain at the box** (an A record to its IP). Caddy needs a real
name to get a certificate.

**2. Install Caddy**, and use this `Caddyfile`:

```
ticktimer.example.com {
    # TLS is automatic — Caddy gets and renews a Let's Encrypt certificate
    # for the name above. Nothing to configure, nothing to remember to renew.
    reverse_proxy 127.0.0.1:8080
}
```

`reverse_proxy` sets `X-Forwarded-For` for you, which is what lets the
server's login brake count real clients instead of counting the proxy as one
very unlucky user.

**3. Run the server as a service**, bound to localhost, with registration
gated. `/etc/systemd/system/ticktimer.service`:

```ini
[Unit]
Description=TickTimer sync server
After=network.target

[Service]
User=ticktimer
ExecStart=/usr/local/bin/ticktimer-server --data /var/lib/ticktimer \
          --port 8080 --bind 127.0.0.1 --invite CHANGE-ME
Restart=on-failure

[Install]
WantedBy=multi-user.target
```

Then `systemctl enable --now ticktimer`.

**4. Make the downloads folder** if you want to hand people the Android APK
by URL: `mkdir -p /var/www/ticktimer`. The Caddy config above serves it at
`/download/`, and Caddy — not TickTimer — does the file serving, because a
hand-rolled parser that mishandles `../` hands out the whole disk.

**5. Firewall to 80 and 443 only.** Nothing should be able to reach 8080 from
outside; the proxy reaches it over loopback.

**6. Point the app at `https://ticktimer.example.com`** — the Server field on
the login screen, no port.

### What the hardening actually is

- **`--bind 127.0.0.1`** — the parser only ever sees requests Caddy already
  validated.
- **`--invite CODE`** — an open signup endpoint on the internet is the real
  risk here, more than the parser. With a code set, `/register` refuses
  before it touches the account store, so a stranger cannot even learn which
  usernames are taken. Hand the code to a friend; change it whenever.
- **The login brake** — five failed attempts from one address in five minutes
  and that address gets `429` for a while, including on a correct password.
  A successful login forgives earlier misses, so fumbling your own typing
  never locks you out. Done in the server rather than in Caddy on purpose:
  stock Caddy has no rate-limit directive (it needs a plugin and a custom
  build via xcaddy), and making the safe deployment depend on compiling your
  own web server is how the safe deployment does not happen.
- **TLS from Caddy** — which the WebAssembly client will require anyway, since
  a page served over HTTPS cannot call a plain-HTTP backend.

### Still worth knowing

Passwords are salted and stretched (PBKDF2, 200k iterations) and device tokens
are stored hashed, so `accounts.json` and `devices.json` are not a pile of
usable credentials. But this is a small hand-written service, so: keep the box
patched, keep registration gated, and back up `accounts.json`, `devices.json`
and `planners/` — that folder is everyone's data.

## The API (what the app says to the server)

| Call | Body | Answer |
|---|---|---|
| `POST /register` | `{username, password, remember?, device?}` | `{ok, token}` — registering IS logging in — plus `deviceToken` if `remember` was true |
| `POST /login` | `{username, password, remember?, device?}` | `{ok, token}`, plus `deviceToken` if `remember` was true |
| `POST /session` | `{deviceToken}` | `{ok, token, username}` — a fresh session for a remembered device, no password. **401** `{error: "auth"}` if unknown or revoked |
| `POST /session/revoke` | `{deviceToken}` | `{ok}` — always; forgetting a token that was already gone is a success |
| `GET /planner` | — (token in `Authorization`) | `{ok, revision, data}` |
| `PUT /planner` | `{baseRevision, force, data}` | `{ok, revision}` — or **409** `{error: "conflict", revision}` if the server moved past `baseRevision` |
| `POST /share` | `{with: "name"}` | `{ok}` — or **404** `{error: "no_such_user"}` for a typo'd name |
| `POST /unshare` | `{with: "name"}` | `{ok}` — always (revoking what wasn't granted still leaves the door closed) |
| `GET /shares` | — | `{ok, iShareWith: [...], sharedWithMe: [...]}` |
| `GET /planner/<user>` | — | `{ok, revision, data}` — or **403** `{error: "forbidden"}` unless `<user>` shared with you |
| `GET /version` | — (no token; public) | `{ok, latest, url, notes}` from `version.json` — or **404** `{error: "not_configured"}` if the file is absent |

All share routes need the token. Note the two different "no": **401** means
the server doesn't know who you are (log in again); **403** means it knows
exactly who you are and the owner hasn't shared with you.

The `token` is a session pass: sync calls carry it in an
`Authorization: Bearer …` header instead of re-sending your password. Tokens
live in the server's memory only — restarting the server logs everyone out.

**v30.2 — remembered devices.** A client may ask to be remembered, and gets a
second, different credential: a `deviceToken`, stored in `devices.json` (hashed
— the raw value exists only in that one reply). It survives restarts and buys
exactly one thing, a fresh session token, so a phone stops asking for a
password at every launch. Session tokens are unchanged and still die with the
process. Revoke a device with `/session/revoke`; the reasoning is in
`design-addendum-offline-and-devices.md`.
## Where the data lives

*(Since v19.1 the server prints this data folder on startup — no more
guessing where these files live.)*

- **Accounts** → `<server data dir>/accounts.json`, passwords stored as
  salted, stretched hashes (`pbkdf2$...`) — never plaintext.
- **Sharing grants** → `<server data dir>/shares.json` — who may *read*
  whose planner (`design-addendum-share.md`). Delete a line, delete a grant.
- **Update notices** → `<server data dir>/version.json` — the version the
  server *advertises* (`design-addendum-update.md`). Re-read on every
  request: edit it and the change is live, no restart. Copy
  `server/version.example.json` to get started; delete the file to turn
  update notices off.
- **Your planner** → still the app's own `data.json`, untouched and separate.
- **Synced planners** → `<server data dir>/planners/<username>.json`, one
  versioned copy per account. The server treats it as an opaque blob — it
  never reads inside your planner.

Identity and planner data are kept in different files on purpose — the same
settings-vs-domain separation the desktop app already lives by.
