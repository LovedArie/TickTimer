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

**4. It's for your home network, not the internet.** This is a development
server with no hardening. Do **not** expose it to the public internet. The
whole plan is built so that when you get a Pi, the same code moves over and we
do internet-facing setup *properly* then.

**5. Firewall prompt is normal.** The first time the server opens its port,
your OS asks "allow this app through the firewall?" — allow it for **private
networks** only. Expected, not a warning sign.

## The API (what the app says to the server)

| Call | Body | Answer |
|---|---|---|
| `POST /register` | `{username, password}` | `{ok, token}` — registering IS logging in |
| `POST /login` | `{username, password}` | `{ok, token}` |
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
live in the server's memory only — restarting the server logs everyone out,
and logging in again (which the app does every launch) mints a new one.

## Where the data lives

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
