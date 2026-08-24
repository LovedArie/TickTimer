# TickTimer in a browser (WebAssembly)

The same app, compiled to WebAssembly and served from your own server. It
exists for **iPhones** — building a native iOS app needs a Mac, Xcode, and
re-signing every seven days on a free Apple ID, and this project deliberately
never goes near an app store.

It is the *same* app. Not a web version, not a companion, not a re-write: the
identical C++ that runs on the desktop, drawing into a canvas. That matters
more than it sounds — see "Why not a normal web app" below.

---

## Status

**Verified working** (v30.4.2, desktop Chrome/Edge): it loads, logs in, and —
the part that took three separate fixes — **it remembers across a reload**.

`ticktimer.wasm` is 23.1 MB raw and **7.6 MB gzipped**, which is what a phone
actually downloads once (Caddy compresses on the fly). That is up from 5.8 MB
before asyncify; see below for what bought the extra 1.8 MB.

**Not yet verified:** iOS Safari specifically, and Add-to-Home-Screen. Those
need an iPhone and a real HTTPS origin, i.e. the VPS.

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

## Installing it on an iPhone

Safari → open the URL → Share → **Add to Home Screen**.

That is not just a bookmark. An installed web app runs without Safari's
chrome, and — the part that matters later — **Web Push on iOS is only
available to installed web apps**. Phase 5's notifications depend on this step
having been done.

---

## The first-run checklist

- [ ] **1. It loads.** A TickTimer boot screen, then the login window. First
      load fetches ~7.6 MB; after that the browser caches it.
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
- **iOS can evict browser storage** after roughly seven days without use.
  Installing to the Home Screen makes it much stickier but is not a guarantee,
  which is the real argument for logging in and letting the server hold the
  authoritative copy.
- **No sound.** Qt Multimedia is not in the WASM kit; the build says so at
  configure time and falls back to silence.
- **First load is ~7.6 MB.** Cached afterwards, and re-fetched only when you
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
