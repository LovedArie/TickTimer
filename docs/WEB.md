# TickTimer in a browser (WebAssembly)

The same app, compiled to WebAssembly and served from your own server. It
exists for **iPhones** — building a native iOS app needs a Mac, Xcode, and
re-signing every seven days on a free Apple ID, and this project deliberately
never goes near an app store.

It is the *same* app. Not a web version, not a companion, not a re-write: the
identical C++ that runs on the desktop, drawing into a canvas. That matters
more than it sounds — see "Why not a normal web app" below.

**This file is the runbook — how to build it, deploy it and check it.** The
*decisions* behind it, in the project's choice → why → alternative-rejected
form, are `docs/design-addendum-web.md`. When the two disagree, the addendum
is the record.

---

## Status

Two different things are verified to two different degrees, and collapsing them
is how a doc starts promising a platform nobody has run.

**The build: green at v30.8.1** (measured 2026-08-31). Everything the phone arc
has shipped since the last WASM build — v30.6's `Notifier` interface, v30.7's
touch targets and phone shell, v30.8's width gate — cross-compiles to
WebAssembly with **no changes**. That is worth stating plainly because it was
the open risk: three feature versions had gone by without anyone pointing
Emscripten at this tree.

**Desktop Chrome/Edge: verified working** (v30.4.2): it loads, logs in, and —
the part that took three separate fixes — **it remembers across a reload**.

**iOS Safari: never run.** Not "probably fine" — *unmeasured*. So is
Add-to-Home-Screen. Both need an iPhone and a real HTTPS origin, i.e. the VPS.
See "What an iPhone still has to tell us" below for the specific questions,
and `docs/ROLLOUT.md` Stage 4 for the one ordered pass that answers them.

`ticktimer.wasm` is 23.3 MB raw. What a phone **actually downloads** is
**8.1 MB (zstd) or 8.5 MB (gzip)** — measured against the live server on
2026-08-31, not computed here. That is up from 5.8 MB before asyncify; see
below for what bought the extra 1.8 MB.

*This document said 7.6 MB for months and that number was never wrong so much
as never true of the server.* 7.6 MB is what `gzip -9` produces on a desktop;
Caddy compresses at its default level, which is faster and slightly looser. The
figure that matters is the one the wire carries, so it is the one quoted here.
Worse, for ten days the server was sending the file **uncompressed at 24.4 MB**
because `encode` skips `application/wasm` unless told not to — see
`deploy/Caddyfile.example`.

**None of the six test suites run here.** `CMakeLists.txt` fences them off for
Emscripten as it does for Android — every executable is its own web page, and
`QTcpServer` does not exist in this build at all. The domain they test is
identical on every platform, so desktop-green is the proof for the domain. It
is *not* proof for anything platform-shaped, which is the whole reason the
sections below exist.

---

## THE QT KIT — read this before anything else

**The stock WebAssembly kit from the Qt Maintenance Tool cannot run this app.**
You need one built with **asyncify**, and this is the single most important
fact in this document.

The failure is nasty because everything looks fine: it configures, compiles,
links, loads in the browser, and then aborts the instant anything calls
`QDialog::exec()` — which for TickTimer is the login window, so, immediately.
In a Release build the entire message is `Aborted().`

`exec()` runs a nested event loop. A browser's single main thread cannot do
that without Emscripten's **asyncify**, which unwinds and rewinds the stack.
Qt decides whether to support it when **Qt itself** is built — `QtWasmHelpers.
cmake` reads `QT_EMSCRIPTEN_ASYNCIFY` out of `qdevice.pri` — so no flag on
*our* build can rescue it.

*The alternative was rewriting all 15 `exec()` call sites as `open()` plus
signal callbacks, including restructuring startup, since `main()` blocks on
`login.exec()` and v30.2's resume/offline logic lives in that dialog. That
would make the DESKTOP code worse to suit a secondary platform, and it is
all-or-nothing: miss one and the app dies when somebody opens Settings.*

### Building the Qt kit (once, ~40 minutes)

Only **qtbase** is needed — the app uses Widgets, Core, Gui and Network, all of
which live there. You also need a **desktop Qt of the exact same version** as
the host for `moc`/`rcc`; a mismatch is the usual reason this goes wrong.

```sh
# 1. Source (~50 MB)
curl -L -o qtbase.tar.xz \
  https://download.qt.io/archive/qt/6.11/6.11.1/submodules/qtbase-everywhere-src-6.11.1.tar.xz
tar -xf qtbase.tar.xz
```

```bat
:: 2. Configure. The -device-option IS the point.
call C:\emsdk\emsdk_env.bat
mkdir build && cd build
..\qtbase-everywhere-src-6.11.1\configure.bat ^
    -platform wasm-emscripten ^
    -prefix C:/Qt/6.11.1/wasm_asyncify ^
    -qt-host-path C:/Qt/6.11.1/mingw_64 ^
    -device-option QT_EMSCRIPTEN_ASYNCIFY=1 ^
    -release -nomake examples -nomake tests ^
    -- -G Ninja

:: 3. Build and install
cmake --build . --parallel
cmake --install .
```

**Check the option took** before spending the build time — a silently ignored
`-device-option` costs you the whole thing for nothing:

```
grep QT_EMSCRIPTEN_ASYNCIFY build/CMakeCache.txt
→ QT_QMAKE_DEVICE_OPTIONS:UNINITIALIZED=QT_EMSCRIPTEN_ASYNCIFY=1
```

It installs to a **new prefix** beside your existing kits. Nothing that works
today is touched, and `tools\build-wasm.bat` prefers `wasm_asyncify`
automatically — refusing to build against the stock kit, with a message
pointing back here.

---

## Building the app

```
tools\build-wasm.bat
```

It checks for all three prerequisites before doing anything:

- **Emscripten 4.0.7**, at `C:\emsdk`. Qt pins the version it was built
  against and refuses a mismatch by name, which is the good kind of failure.
- **Ninja** — the Qt kits ship none, and Emscripten cannot use MSVC's
  generator. `C:\msys64\ucrt64\bin\ninja.exe` will do.
- **A `wasm_asyncify` Qt kit**, per the section above.

Output lands in `build-wasm\serve\` — the shell page and icons from `web\`
plus the `.js`/`.wasm` from the build. Both halves are needed; neither is
useful alone.

### When something aborts

Release builds strip Emscripten's assertions, so a failure reads `Aborted().`
and nothing else. Rebuild with them on:

```
cmake -B build-wasm -DTICKTIMER_WASM_ASSERTIONS=ON
cmake --build build-wasm
```

That turns the same abort into a sentence naming the cause — it is what turned
a bare `Aborted()` into *"'addRunDependency' was not exported… forcing
filesystem support (-sFORCE_FILESYSTEM) can export this for you"*, which was
the fix, spelled out.

Also useful: **`?nostore`** on the URL skips the storage mount, so "is it the
storage layer or the app?" is answered by editing a URL rather than rebuilding.

## Trying it locally

```
cd build-wasm\serve
python -m http.server 8099
```

Then open <http://localhost:8099/>. A plain file:// open will **not** work —
WebAssembly needs a real HTTP origin.

## Putting it on the server

Copy the *contents* of `build-wasm\serve\` to `/var/www/ticktimer-app/`.
`deploy/Caddyfile.example` already has the `/app` block that serves it, with
compression on and the right cache headers. It ends up at
`https://your-domain/app/`.

**Same origin as the API, on purpose.** The app's calls to `/login` and
`/planner` are then not cross-origin at all, so most of the CORS question
evaporates. (The preflight support from v30.2.1 still earns its keep: a browser
preflights on the *headers*, and every call sends JSON or a token.)

**Redeploy on every release, or the app nags about itself.** `/app/` and
`server/version.json` are two independent copies of "what version is current",
and nothing keeps them in step. A release bumps `version.json`; the WASM app
under `/app/` stays whatever was last copied there, asks `/version`, sees a
newer number, and shows an update banner **it has no way to act on** — the
Get-it button sends a browser to a Releases page of Windows installers and
Android APKs. This is not hypothetical: `/app/` sat at v30.4.2 while
`version.json` advertised 30.8.1. Copying the folder is the fix, and it belongs
in the release routine (`docs/GITHUB.md`) rather than in someone's memory.

## Installing it on an iPhone

Safari → open the URL → Share → **Add to Home Screen**.

That is not just a bookmark. An installed web app runs without Safari's
chrome, and — the part that matters later — **Web Push on iOS is only
available to installed web apps**, and only on iOS 16.4 or newer. Phase 5's
notifications depend on this step having been done.

**But it is not an installed app either, and the difference bites.** There is
no service worker in this repo — no `sw.js`, no `navigator.serviceWorker`, and
`manifest.webmanifest` carries no offline story. So the home-screen icon is a
**chrome-less bookmark**: tapping it fetches `start_url` from the origin on
every launch. Server unreachable means Safari's error page, not TickTimer's
honest "didn't start" screen, because `index.html` is what failed to arrive.

The asymmetry is worth holding on to: the desktop app and the Android APK are
installed binaries that own their copy, so v30.2's offline gate covers them.
The web app is downloaded fresh each launch. Her data survives in IndexedDB the
whole time — intact and unreachable, which is arguably worse than losing it.
A cache-first service worker over the app shell closes this, and puts the file
Phase 5's push handler needs there early. It is not built; see "Known limits".

---

## THERE IS NO CONSOLE ON AN IPHONE

Read this before the checklist, because the checklist used to depend on one.

Safari on iOS has **no developer console**. Reaching one needs a Mac, a cable,
and Safari's Web Inspector — which is precisely the dependency this whole
approach exists to avoid. On a desktop browser F12 is free; on the target
device it is not available at all.

That matters more than it sounds, because `web/index.html` reports its three
most important failures — storage not persisting, the IndexedDB mount failing,
and "could not read stored data" — to `console.error` **and nowhere else**. The
one check this document calls *"the one thing most likely to be wrong"* is
therefore the one whose diagnosis an iPhone cannot show you.

**So anything a phone has to report must be on the page.** Two switches carry
that load, both reachable by editing a URL rather than rebuilding:

| URL | What it does |
|---|---|
| `/app/?nostore` | Skips the storage mount. Answers "is it the storage layer or the app?" |
| `/app/?probe` | Draws the layout probe **over the app**: screen geometry, available geometry, device pixel ratio, logical DPI, the `isCompactScreen()` verdict, and the window size |

`?probe` sets `TICKTIMER_PROBE=1` inside the WebAssembly process before
`main()` runs, which is the same switch `tools\screenshot.cpp` has always had —
a browser simply has no environment to set it from, so the URL is where it
lives. It works identically on the desktop and on Android; screenshot the
overlay and you have the numbers, with no cable and no Mac.

---

## What an iPhone still has to tell us

Three open questions, in the order they cost the most. None can be answered
from this machine; all three are `docs/ROLLOUT.md` Stage 4.

**1. Does the phone shell appear at all?** *Mostly answered, and the answer is
encouraging.* `isCompactScreen()` asks
`QGuiApplication::primaryScreen()->availableGeometry()` and compares the short
side against 600. On Android that input turned out to be **physical** pixels on
a 1080x2400 device, so the check said "desktop" on a phone and the app opened
with its content clipped off the right edge.

Measured in a desktop browser on 2026-08-31 with `?probe`: **it tracks the
browser window.** Resize, reload, and the number changes — so it is the
viewport in CSS pixels, not the display in device pixels. An iPhone-sized
viewport should therefore score well under 600 and get the phone shell with no
platform code at all.

What is still open is iOS Safari specifically: whether it reports the visual
viewport the same way, what the URL bar hiding and returning does to it, what
the chrome-less Add-to-Home-Screen mode does to the available height, and
whether a device pixel ratio of 3 changes the mapping. `?probe` on the phone
answers all four in one screengrab, which is why it is step 1 of the device
pass rather than an afterthought.

**2. Does it remember, on iOS specifically?** IndexedDB under Safari has its
own eviction rules, and a home-screen install changes them. Desktop Chrome
passing tells us the mount logic is right, not that iOS honours it.

**3. Is text entry usable?** Qt draws its own controls into a canvas, so the
on-screen keyboard is Qt-for-WebAssembly's weakest area. This is the known
roughest edge and the one most likely to decide whether the app is pleasant.

---

## The first-run checklist

- [ ] **0. Run `?probe` first.** Before logging in, before anything: open
      `/app/?probe` and screenshot the overlay. It costs ten seconds and it is
      the only chance to capture the layout inputs while someone is holding the
      phone.

- [ ] **1. It loads.** A TickTimer boot screen, then the login window. First
      load fetches ~8.1-8.5 MB compressed; after that the browser caches it.
      - ❌ A stuck progress bar or a blank page **on a desktop browser**: open
        the console, `web/index.html` reports failures there deliberately.
      - ❌ On an iPhone there is no console — the boot screen itself shows a
        headline and a sentence for hard failures. Photograph it.

- [ ] **2. IT REMEMBERS. Do this before anything else.** Log in, change
      something, then **reload the page**. Is it still there?
      - This is the one thing most likely to be wrong. Qt points
        `QStandardPaths` at `/home/web_user`, but Emscripten mounts *memory*
        there by default — a filesystem that dies with the tab. `web/index.html`
        mounts IndexedDB over it instead and syncs every 5 seconds and whenever
        the tab is hidden.
      - ❌ If it forgets: on a desktop the console will say so, and you should
        check that the build linked `-lidbfs.js`
        (`grep IDBFS build-wasm/ticktimer.js`). On a phone, compare against
        `?nostore` — if both forget identically, the mount never happened.

- [ ] **3. Sync works.** Change something on the desktop, sync, reload the web
      app. The change should arrive.

- [ ] **4. It survives being backgrounded.** Make a change, switch apps for a
      minute, come back, reload. Still there? That is the `visibilitychange`
      sync working — the one that matters on a phone, where "switched apps"
      and "locked the screen" look identical.

- [ ] **5. Add to Home Screen** and confirm it opens without Safari's chrome
      and shows the TickTimer icon.

---

## Known limits, and which are permanent

- **The UI is not iOS's UI, but it is no longer the desktop's either.** This
  bullet used to read "the UI is the desktop UI", and that stopped being true
  at v30.7. `isCompactScreen()` (`include/Widgets.h`) decides the layout mode
  by **geometry, not by platform** — deliberately, so a 10-inch tablet gets the
  desktop layout and a small Windows tablet gets the compact one — so an
  iPhone-sized canvas should get the whole phone shell for free: 48dp touch
  targets, the mobile nav bar, full-screen dialogs, the reclaimed chrome.
  *Should.* Whether it actually does is the first unanswered question below.
  What remains permanently true is that Qt Widgets draws its own controls into
  a canvas, so nothing here imitates iOS's *look*. A genuinely native-feeling
  UI would be a QML front-end over the same domain — a real project, and one
  the domain would not notice.
- **Text input in a canvas** is the roughest part of Qt on a touchscreen. If
  the on-screen keyboard misbehaves, that is a known Qt-for-WebAssembly
  weakness, not something this app is doing wrong.
- **No background anything.** A closed tab runs no timers, so block alarms,
  nudges and the check-in do not fire. This used to be shared with Android
  and is not any more: **v30.6 fixed the Android half** by handing the
  schedule to the OS (`Alarms.h` derives it, `AlarmManager` holds it, and
  the app is not running when it rings — see
  `docs/design-addendum-notifications.md`). The WASM half is still open, and
  the seam it needs already exists: a third `Notifier` implementation over
  the browser's Notification API would cover an *open* tab, and only a
  service worker plus Web Push covers a closed one. That remains its own
  project, and it is the reason `notify::make()` has a branch shape rather
  than an if/else.
- **No service worker, so the app cannot start without the server.** Named
  here rather than discovered on a train: the home-screen icon fetches the
  origin on every launch, so an unreachable server is Safari's error page. Not
  permanent — a cache-first service worker over the app shell closes it — but
  not built, and deliberately not designed before the app has run on an iPhone
  once. Detail in "Installing it on an iPhone" above.
- **iOS can evict browser storage** after roughly seven days without use.
  Installing to the Home Screen makes it much stickier but is not a guarantee,
  which is the real argument for logging in and letting the server hold the
  authoritative copy.
- **No sound.** Qt Multimedia is not in the WASM kit; the build says so at
  configure time and falls back to silence.
- **First load is ~8.1-8.5 MB** (23.3 MB on the wire if compression is
  misconfigured — check `Content-Encoding`). Cached afterwards, and re-fetched only when you
  deploy a new build.

## Why not a normal web app

The tempting alternative is a JavaScript front-end talking to the existing
HTTP API. It is the wrong answer here, for a specific reason rather than a
stylistic one.

`design-addendum-sync` §D keeps the server storing the planner as an **opaque
blob** — `{revision, data}` — precisely so the format can evolve without the
server ever changing. The server does not know what a block is. A JavaScript
client would therefore have to re-implement `AppData`, every pure brain
(`missed::`, `reschedule::`, `afford::`, `brief::` …) and `JsonStore` in
another language: a second implementation of every rule in the project, which
is the exact failure its architecture exists to prevent.

Compiling the C++ keeps one implementation. That is the whole argument.
