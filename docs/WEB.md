# TickTimer in a browser (WebAssembly)

The same app, compiled to WebAssembly and served from your own server. It
exists for **iPhones** — building a native iOS app needs a Mac, Xcode, and
re-signing every seven days on a free Apple ID, and this project deliberately
never goes near an app store.

It is the *same* app. Not a web version, not a companion, not a re-write: the
identical C++ that runs on the desktop, drawing into a canvas. That matters
more than it sounds — see "Why not a normal web app" below.

---

## Status, honestly

**Verified:** it configures, compiles, links, and serves. `ticktimer.wasm` is
15.6 MB raw and **5.8 MB gzipped**, which is what a phone actually downloads
once (Caddy compresses on the fly).

**Not yet verified:** that it *runs*. Nobody has opened it in a browser at the
time of writing — there was no browser available to the machine that built it.
The first person to open it is doing real testing, and the checklist below is
in the order that finds problems fastest.

The single most important check is **step 2**: write something, reload, and see
whether it is still there.

---

## Building it

```
tools\build-wasm.bat
```

It needs three things that no other build here needs, and it checks for all
three before doing anything:

- **Emscripten 4.0.7**, at `C:\emsdk`. Qt pins the version it was built
  against and refuses a mismatch by name, which is the good kind of failure.
- **Ninja** — the Qt kits ship none, and Emscripten cannot use MSVC's
  generator. `C:\msys64\ucrt64\bin\ninja.exe` will do.
- **The `wasm_singlethread` Qt kit**, which is a *separate install from your
  desktop Qt* and usually a different Qt version. Maintenance Tool → Add
  components → Qt → *version* → WebAssembly.

Output lands in `build-wasm\serve\` — the shell page and icons from `web\`
plus the `.js`/`.wasm` from the build. Both halves are needed; neither is
useful alone.

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

## Installing it on an iPhone

Safari → open the URL → Share → **Add to Home Screen**.

That is not just a bookmark. An installed web app runs without Safari's
chrome, and — the part that matters later — **Web Push on iOS is only
available to installed web apps**. Phase 5's notifications depend on this step
having been done.

---

## The first-run checklist

- [ ] **1. It loads.** A TickTimer boot screen, then the login window. First
      load fetches ~6 MB; after that the browser caches it.
      - ❌ A stuck progress bar or a blank page: open the browser console.
        `web/index.html` reports failures there deliberately.

- [ ] **2. IT REMEMBERS. Do this before anything else.** Log in, change
      something, then **reload the page**. Is it still there?
      - This is the one thing most likely to be wrong. Qt points
        `QStandardPaths` at `/home/web_user`, but Emscripten mounts *memory*
        there by default — a filesystem that dies with the tab. `web/index.html`
        mounts IndexedDB over it instead and syncs every 5 seconds and whenever
        the tab is hidden.
      - ❌ If it forgets: the console will say so. Check that the build linked
        `-lidbfs.js` (`grep IDBFS build-wasm/ticktimer.js`).

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

- **The UI is the desktop UI.** Qt Widgets draws its own controls into a
  canvas; nothing here imitates iOS. Usable, not native-feeling. A genuinely
  mobile UI would be a QML front-end over the same domain — a real project,
  and one the domain would not notice.
- **Text input in a canvas** is the roughest part of Qt on a touchscreen. If
  the on-screen keyboard misbehaves, that is a known Qt-for-WebAssembly
  weakness, not something this app is doing wrong.
- **No background anything.** A closed tab runs no timers, so block alarms,
  nudges and the check-in do not fire. **This is not a WASM regression** — the
  Android app has the same gap, because `BlockAlarmService` is an in-process
  timer. Phase 5 fixes both at once with Web Push.
- **iOS can evict browser storage** after roughly seven days without use.
  Installing to the Home Screen makes it much stickier but is not a guarantee,
  which is the real argument for logging in and letting the server hold the
  authoritative copy.
- **No sound.** Qt Multimedia is not in the WASM kit; the build says so at
  configure time and falls back to silence.
- **First load is ~6 MB.** Cached afterwards, and re-fetched only when you
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
