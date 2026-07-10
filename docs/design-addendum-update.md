# Design addendum — Update notices (networked arc, part 4: the arc closer)

*Extends `design-doc.md`. Status: shipped. Suites: 2 pure-logic tests
(semver + banner rule), 1 UI test (banner dismiss), 1 live end-to-end test
(the `/version` route, including edit-without-restart). This completes the
networked arc: login → sync → share & compare → update notices.*

---

## A. The requirement, and the ladder we deliberately didn't climb

*"Auto-update when a new patch ships."* That phrase hides a ladder:

1. **Notify** — the app learns a newer version exists and says so; the human
   downloads it. The app never touches its own files.
2. **Download-for-you** — the app fetches the installer, then hands over.
3. **Self-install** — the app replaces itself silently.

We shipped **Level 1, on purpose**. Level 3 fights the platform: a running
Windows exe cannot overwrite itself (the file is locked), so self-update
needs a helper process, careful sequencing, and a rollback story for the
half-failed case — real engineering whose failure mode is *bricking the
install*. Disproportionate risk for a personal app. Level 1 delivers the
valuable half — the version model, the server contract, the non-nag UX —
and is honest about the constraint instead of wrestling it. Level 2 bolts
on later without changing anything shipped here (§G).

## B. The version becomes load-bearing → it gets one home

Until now "v18" was folklore, living in four places (session notes, two
`.rc` files, the installer script) with nothing keeping them honest. The
moment the app *compares itself* against a server's answer, the version is
data, and data with four copies is four bugs waiting.

**`Version.h`** is now the single source of truth, with one trick worth
keeping: the numbers are plain `#define`s at the top, and everything C++
sits behind `#ifndef RC_INVOKED` — the symbol the Windows **resource
compiler** defines about itself. So the same header is included by C++ code
*and* by both `.rc` files (which stamp the exe's version metadata). Bump
the version in one file; the code, the exe properties, and the update check
all follow. (The Inno script can't include C headers — it carries a
keep-in-sync comment, the one honest seam left.)

## C. Semver, and the trap the tests pin

Versions are `major.minor.patch`, compared **field-by-field numerically** —
never as strings, because `"18.10.0" < "18.9.0"` in string order (`'1' <
'9'`). That exact trap is a named test. Parsing is strict (`19.0` and
`v19.0.0` are invalid) and the whole layer **fails closed**: any unparseable
version means *not newer*, because an updater built on guessed versions
nags people, and a nag built on garbage is the worst of both.

## D. The server side — `version.json`, read per request

`GET /version` (no token — "what's the newest TickTimer?" isn't private,
and the question must be answerable by arbitrarily old clients) returns
whatever **`version.json`** in the server's data directory says:
`{latest, url, notes}`.

The route re-reads the file **on every request** rather than caching at
startup. Deliberate trade: the file is ~200 bytes and requests are rare
(one per app launch), so the cost is nothing — and the benefit is that
*announcing a release is an edit*, no restart, nothing to remember. The
live test proves it: file absent → `Unavailable`; file written into the
**running** server's directory → next check sees it; file deleted → gone.

Missing or empty file = **404 `not_configured`** — an absence, not an
error. The server stays in character: it repeats what the operator wrote,
verifying nothing. You are the source of truth; typo `"190.0.0"` and every
app will believe you.

## E. The client — fetch, judge, render (three layers, one line each)

- **`UpdateClient`** *fetches* — the fourth wire sibling, and the simplest:
  one GET, one signal. The family recipe (async QNAM, cache-clear, status
  attribute, typed outcome) now writes itself — the quiet payoff of three
  earlier clients.
- **`version::decideBanner`** *judges* — the feature's one real rule as a
  pure function, `sync::decide`'s sibling: **show iff strictly newer AND
  not the exact version already dismissed.** The whole truth table is six
  `QCOMPARE`s in the domain suite.
- **`UpdateBanner`** *renders* — a strip above the header, never a modal.
  *Get it* opens the release URL in the browser and steps aside; **✕**
  writes `update/lastDismissed` to QSettings and hides.

The manners are the design: silence when current (no "you're up to date!"
popup nobody asked for), silence on every failure (server down, airplane
mode, unconfigured — a check the user never requested has no right to
report its problems), and **per-version dismissal** — waving away 19.0.0
silences 19.0.0 forever, but 19.0.1 speaks again, because a dismissal means
"not this one", not "never talk to me".

## F. Distribution lives on GitHub Releases

The `url` in `version.json` points at the project's Releases page — the
natural home for built zips (free, stable URLs, versions as first-class
tags). Setting that up is a one-time operator task, scripted step-by-step
in **`docs/GITHUB.md`**; until it's done, the URL is a marked placeholder
and everything else functions.

## G. Limits, named out loud

- **It's a notice, not an installer.** The human downloads and replaces the
  folder themselves. Level 2 (download-for-you) would bolt onto
  `UpdateBanner` — fetch the zip via QNAM, then open the folder — without
  touching the server contract or the pure rule.
- **No authenticity check.** The banner trusts whatever URL your server
  serves over plain HTTP; on a hostile network that could be tampered with
  (same trust model as the rest of the arc — fine on your home Wi-Fi,
  worth knowing before exposing the server to the internet).
- **Checked once per launch.** No periodic re-check while running; a person
  who never restarts never hears. Acceptable for a desktop app that gets
  closed daily.
- **The server can't lie usefully about *per-platform* builds.** One
  `latest` for everyone; the day an Android build ships on a different
  cadence, `version.json` grows a per-platform shape.
