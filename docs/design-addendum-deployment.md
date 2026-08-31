# Design Addendum — Deployment & Distribution (three platforms, one origin)

**Status: in service.** Desktop and Android are delivered and in use; the
WebAssembly app is deployed and verified from desktop browsers but has never
run on an iPhone. Continues the decision log in `design-doc.md §3`.

**Why this file exists.** Until v30.8.1 the deployment story was spread across
five runbooks (`GITHUB.md`, `ROLLOUT.md`, `ANDROID.md`, `WEB.md`, `SERVER.md`),
a config template, and one section of an addendum about something else
(`design-addendum-update.md` §F). Each was right about its own corner and
nothing owned the shape. That was survivable while there was one artefact. It
stopped being survivable at three, and the day this file was written the
project was serving a **four-version-stale web app**, **403 on every app icon**,
and **24 MB of uncompressed WebAssembly** — three defects that live in the gaps
between those documents rather than inside any of them.

This addendum does not repeat how to build each artefact (their own addenda and
runbooks do that) or how the update banner decides to appear
(`design-addendum-update.md`). It records what is true *across* the three.

---

## A. Three channels, no app store, and that is the premise

*Decision:* TickTimer is private software for a handful of known people. It is
distributed as a **Windows zip/installer from GitHub Releases**, a **signed
Android APK sideloaded from our own `/download/`**, and a **WebAssembly app
served from our own `/app/`** for iPhones. No store, on any platform.

*Why:* every store is a review process, a fee, an account, and a policy surface
that can change under an app nobody is selling. The Play Store additionally
wants a target-SDK treadmill; the App Store additionally wants a Mac. The
audience is people we can hand a URL to, so the store buys nothing and costs a
recurring obligation.

*Alternatives rejected:*

- **A native iOS app.** Needs a Mac and Xcode, and on a free Apple ID every
  installed copy must be **re-signed every seven days, forever, for every
  person holding it**. That is not a build cost, it is a permanent chore that
  scales with users. WebAssembly costs one `scp`.
- **TestFlight.** Sounds like the private-distribution answer and is not: it
  still needs the paid developer account, still passes review, and builds
  expire after 90 days — the seven-day chore at a slower cadence.
- **F-Droid / Play internal testing.** Real options for Android alone, but they
  would make Android the only platform with a distribution mechanism the other
  two cannot use, for an audience that does not need discovery.

*The consequence to keep in view:* nothing about this is discoverable, and
nothing auto-updates. Every channel ends with a human choosing to install
something. §C is about the one mechanism that tries to prompt that choice.

## B. One version number, four claims about it, and only two are checked

*Decision:* `include/Version.h` is the single source. Everything else derives
from it or is checked against it.

*The four places a version claim ends up, and what protects each:*

| Claim | Where it comes from | Protected by |
|---|---|---|
| The exe's file metadata | `Version.h` via `RC_INVOKED` and `ticktimer.rc` | compiler; `static_assert` pins the string against the macros |
| The installer's `AppVersion` | **hand-copied** into `installer/ticktimer.iss` | `deploy-windows.bat` **hard-fails**; `installerVersionMatchesTheHeader()` pins it in the suite |
| The APK's `versionName` | `Version.h` read by CMake at **configure** time | `CMAKE_CONFIGURE_DEPENDS`, plus "check the stamp before signing" |
| The bytes under `/app/` | whatever was last copied there | **nothing** |

*Why the asymmetry is real and not an oversight to be fixed by adding a check:*
the first three seams live inside one build on one machine, where a script can
compare two files and refuse. `/app/` is a directory on another host that no
local build can see. A check would have to be a network call from a build to a
production server, which is a much larger idea than the problem deserves.

*So the protection is procedural, and named as such.* Redeploying `/app/` is
**release step 7** in `docs/GITHUB.md`, with a proof point that reads the
server rather than the browser:

```sh
ls -l --time-style=long-iso /var/www/ticktimer-app/ticktimer.wasm
```

*Recorded because it happened:* `/app/` served v30.4.2 from 21 August to 31
August while `/version` announced 30.8.1. Nothing alerted; the app simply
showed an update banner it could not act on. **A seam with no check is a seam
that will be wrong, and the honest response is to write down that it has no
check rather than to imply it has one.**

## C. The update banner is single-platform, and that is now a defect

*The prediction, from `design-addendum-update.md` §G:* *"The server can't lie
usefully about per-platform builds. One `latest` for everyone; the day an
Android build ships on a different cadence, `version.json` grows a per-platform
shape."*

*That day has arrived, and this addendum amends §G rather than restating it.*
There are three artefacts now, and they do not ship together: a desktop release
is a zip and an installer, Android is a signed APK copied to `/download/`, and
the web app is a folder copied to `/app/`. One `latest` string is broadcast to
all three.

*The concrete failure:* the WebAssembly app compares itself against `latest`
and, when behind, offers a **Get it** button pointing at
`version.json`'s `url` — the GitHub Releases page, which holds a Windows zip
and an installer. A browser can do nothing with either. The banner is
technically correct and practically useless: *the one platform that could
update itself silently is the one being sent to a page of files it cannot run.*

*Decision for now: do not grow the schema yet.* `version.json` stays a single
`latest`, because the shape it should grow into depends on facts not yet in
hand — whether the web app should self-update at all (it is re-fetched every
launch, so it arguably has no update problem, only a **cache** problem), and
whether Android's cadence really will diverge or merely lagged once. Inventing
a per-platform schema before that is inventing a data format to fit a guess.

*What is done instead, today:* step 7 keeps the three artefacts in step, which
removes the symptom at its source. If they ever ship on genuinely different
cadences the schema grows then, with the evidence in hand. **The defect is
recorded rather than papered over**, which is the point of writing it here.

*Also aged out of §G, and worth correcting:* that section's "no authenticity
check … over plain HTTP" limit was written when the server was a laptop on a
LAN. Everything now goes through Caddy over Let's Encrypt TLS, so the transport
half of that concern is closed. What remains true is that there is no signature
on the artefact itself — the trust is in the origin, not in the file.

## D. Serving is part of deploying, and a config is not evidence

*The decision:* a deploy is not finished when files land. It is finished when
the **response** has been measured.

*Why, with the three defects that taught it — all found on 2026-08-31, all
invisible from the config, all live for ten days:*

| Symptom | Cause | Why nothing caught it |
|---|---|---|
| Every app icon **403** | `icons/` arrived mode 700 root-only via `scp -r`; Caddy runs as `caddy` and cannot traverse it | 403 not 404 is the tell, and nobody looked; on a phone it would have read as "Add to Home Screen gives a blank icon" — an iOS-shaped symptom with no iOS in it |
| `.wasm` served **uncompressed at 24.4 MB** | `encode zstd gzip` compresses only Caddy's default content types, and `application/wasm` is not among them | A valid 200 of the wrong size. The two small text files WERE compressed, so a spot check on the page would have looked fine |
| **No `Cache-Control`** on anything | `header` runs **before** `uri strip_prefix /app` in Caddy's fixed directive order, so `/ticktimer.wasm` matchers were tested against `/app/ticktimer.wasm` and never fired | ETag still forced revalidation, so nothing looked broken; the stated intent simply was not in effect |

*The shape they share is the same one that runs through this whole project's
worst bugs:* **the source said one thing and the artefact did another.** It is
`Version.h` versus the `.iss`. It is `-sEXPORTED_RUNTIME_METHODS=ENV` building
cleanly and exporting nothing. It is a `Caddyfile` that says `encode` over a
response with no `Content-Encoding`. In every case the written intention was
correct and unexecuted.

*So the rule, and it is the most portable thing in this file:* **verify against
the artefact, never against the source.** Read the built `.js` for the export,
the exe's properties for the version, the HTTP response headers for the
encoding. `docs/GITHUB.md`'s release steps each carry a proof point for exactly
this reason.

*Corollary, learned the same day:* `grep -c` on generated output is not a
count. Emscripten's `.js` is one enormous line, so `grep -c` answers 1 for
"present once" and 1 for "present forty times". Use `grep -o … | wc -l`.

## E. Static files belong to Caddy, never to `ticktimer-server`

*Decision:* the APK and the web app are served by Caddy from `/var/www/…`.
`ticktimer-server` never sees those requests — `handle_path /download/*` and
`handle /app*` are matched before the `reverse_proxy`.

*Why:* serving a file means parsing a path, and a hand-rolled parser that
mishandles `../` hands out the disk. `ticktimer-server` is a small
`QTcpServer` written for a JSON API; asking it to also be a static file server
would add the single highest-risk category of code in the whole deployment, to
the one process that is exposed to real traffic. Caddy has solved this
correctly for years. **The right move is to not write that code at all** —
which is the same instinct as §A's "no store": decline the obligation rather
than manage it.

*This is also what makes the loopback bind safe.* `ticktimer-server` listens on
127.0.0.1 and never sees a request Caddy has not already parsed, which is what
lets a small hand-rolled HTTP parser run on a box with a public address.

## F. The origin is the one thing that must never change

*Decision:* everything lives under **one hostname, a subdomain, never the apex**
— `ticktimer.<domain>`. The API, `/download/`, and `/app/` all share it.

*Why, and this is the reason with teeth:* an installed web app's IndexedDB is
keyed to its origin. Move the app to a different hostname and she taps the same
icon, reinstalls, and **arrives at an empty planner** — her data still on the
phone, under an origin nothing points at any more. Same-origin also means the
app's calls to `/login` and `/planner` are not cross-origin at all, so most of
the CORS question evaporates, and one certificate covers everything.

*Why a subdomain rather than the apex:* one process owns port 443 and routes by
hostname. If TickTimer squats the apex, adding any second app later means
**moving TickTimer** — which is exactly the origin change above. A subdomain
costs nothing today and makes the second app a new Caddy block.

*What this buys when the hosting moves.* The VPS is a rehearsal for a Raspberry
Pi behind a Cloudflare Tunnel. Under that move the binary, the systemd unit and
the `/download` and `/app` blocks are unchanged; the site address, where TLS
comes from, and the DNS record change; and **the origin phones know does not**.
`docs/ROLLOUT.md` §2h holds the table, including the `X-Forwarded-For` line the
Pi needs that the VPS does not.

## G. The backup is the migration

*Decision:* a nightly tarball on the box (`deploy/ticktimer-backup.sh`, written
`.partial` and renamed only after `tar -tz` reads it back), plus a manual copy
off it.

*Why the write-then-rename:* it is `QSaveFile`'s discipline from the C++ stores,
for the same reason — a backup killed halfway must not be sitting there under a
name that says it finished.

*Why it belongs in a deployment addendum at all:* restoring the tarball into a
fresh box **is** the migration to the Pi. Testing the restore is therefore not
diligence about backups, it is a rehearsal of the move — which is why
`ROLLOUT.md` §2g insists the restore be performed on day one, and why a backup
nobody has restored is a hope rather than a backup.

*Known limitation, written down rather than discovered later:* the server
writes `accounts.json` and `devices.json` with a plain truncating `QFile`, so a
tarball taken mid-write can capture a truncated file. The window is
microseconds and opens only on registration and login, which is why 03:00 was
chosen — but the honest fix is `QSaveFile` in those two stores, as
`PlannerStore` and `ShareStore` already do.

## H. What is deliberately not automated

Named so that nobody mistakes the gaps for oversights:

- **No CI.** Every artefact is built on the owner's machine, by a script that
  checks its own preconditions (`deploy-windows.bat` refuses on a version
  mismatch; `build-wasm.bat` refuses without an asyncify kit). For one
  developer and three targets, a script with a proof point beats a pipeline
  nobody maintains.
- **No automatic deploy.** Two `scp` lines, run by a human who then reads the
  server's answer. §D is the argument: the value is in the verification, and
  automating the copy without automating the verification would remove the
  step that actually catches things.
- **No signature on any artefact.** Trust is in the origin (TLS) and in the
  Android signing key, not in a detached signature anybody checks.
- **No `/app/` version check.** §B; the seam is on another machine.
- **No service worker**, so the web app cannot start without the server —
  `design-addendum-web.md` §G holds that one, and it is the largest open item
  in the deployment story.

---

## What changed where

| Layer | File(s) | Change |
|---|---|---|
| Serving | `deploy/Caddyfile.example` | `encode` gains an explicit `match` including `application/wasm`; `header` matchers corrected to pre-strip `/app/…` paths; the post-copy `chmod` recipe recorded |
| Release | `docs/GITHUB.md` | step 7 — redeploy the phones' copies, with a server-side proof point |
| Runbook | `docs/WEB.md`, `docs/ROLLOUT.md` | measured download figures; the device pass |
| Record | `docs/design-addendum-update.md` §G | amended by §C above rather than rewritten |

Domain, storage, tracking: **zero changes.** Nothing in this file is a program
behaviour — which is the point. Deployment is where a correct program goes
wrong, and it had no record until now.
