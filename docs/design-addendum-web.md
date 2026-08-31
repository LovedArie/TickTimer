# Design Addendum — The WebAssembly Build (the iPhone path)

**Status: built and deployed; verified on desktop browsers, never run on iOS.**
Continues the decision log in `design-doc.md §3`. The runbook — how to build it,
where the Qt kit comes from, what to check — is `docs/WEB.md`; this file is the
record of *why*.

**The requirement:** she has an iPhone, and this project will never go near an
app store.

That constraint is not a preference to be traded away later, it is the premise
(`docs/ROLLOUT.md`, and the distribution rule the whole project runs on). A
native iOS app needs a Mac, Xcode, and — on a free Apple ID — re-signing every
seven days, forever, for every person holding the app. A web app served from
our own origin needs none of those and costs one `scp`.

---

## A. Compile the C++; do not write a JavaScript client

*Decision:* the browser runs the **same C++**, compiled to WebAssembly by
Emscripten. Not a companion app, not a mobile view of the API.

*Why:* `design-addendum-sync` §D deliberately keeps the server storing the
planner as an **opaque blob** — `{revision, data}` — so the format can evolve
without the server ever changing. The server does not know what a block is.
A JavaScript front-end talking to the existing HTTP API would therefore have to
re-implement `AppData`, `JsonStore`, and every pure brain (`missed::`,
`reschedule::`, `afford::`, `brief::`, `syncplan::decide`…) in a second
language. That is a second implementation of every rule in the project, which
is the exact failure the architecture exists to prevent — the same argument
`AppData` makes against putting a rule in a page, one level up.

*Alternative rejected:* a JS/React client over `/login` and `/planner`. It
would look more like a web app, ship far smaller, and have a native text-input
story — all real advantages, and all bought by duplicating the domain. The
duplicate would then drift, and the drift would be silent, because nothing
compiles both.

*The cost, named:* ~8.1-8.5 MB compressed on first load, and a UI drawn into a canvas
rather than made of DOM elements. Both are consequences of this choice and
neither is a defect to be fixed later.

## B. The Qt kit must be built with asyncify — this is the single load-bearing fact

*Decision:* the WebAssembly Qt is built from source with
`-device-option QT_EMSCRIPTEN_ASYNCIFY=1`, installed to its own prefix beside
the stock kits. `tools\build-wasm.bat` **refuses to build** against the stock
kit rather than producing something that aborts.

*Why:* `QDialog::exec()` runs a nested event loop, and a browser's single main
thread cannot do that without Emscripten's asyncify, which unwinds and rewinds
the stack. Qt decides whether to support it when **Qt itself** is built —
`QtWasmHelpers.cmake` reads the flag out of `qdevice.pri` — so no option on
*our* build can rescue it.

*Why the refusal rather than a warning:* the failure mode is uniquely cruel.
The stock kit configures, compiles, links, and loads in the browser, and then
aborts the instant anything calls `exec()` — which for TickTimer is the login
window, i.e. immediately. In a Release build the entire message is
`Aborted().`. A warning in a build log buys nothing against that; the build
must not happen.

*Alternative rejected:* rewriting all 15 `exec()` call sites as `open()` plus
signal callbacks. That means restructuring startup itself, since `main()`
blocks on `login.exec()` and v30.2's resume/offline logic lives inside that
dialog. It would make the **desktop** code worse to suit a secondary platform,
and it is all-or-nothing: miss one call site and the app dies the day somebody
opens Settings. Half an hour of compiling, once, beats that permanently.

## C. IndexedDB over Emscripten's default filesystem

*Decision:* `web/index.html` mounts **IDBFS** over `/home/web_user` before the
app starts, syncs it every five seconds and on `visibilitychange`, and the
build links `-lidbfs.js` with `-sFORCE_FILESYSTEM`.

*Why:* Qt points `QStandardPaths` at `/home/web_user`, and Emscripten mounts
**MEMFS** there by default — a filesystem in RAM that dies with the tab. The
app looks perfect and forgets everything, which is the worst available failure
because nothing reports it.

*Why `visibilitychange` and not only a timer:* on a phone, "switched apps" and
"locked the screen" are the same event, and neither is a close. A five-second
timer alone loses up to five seconds of work at exactly the moment a phone user
generates it.

*The trap this cost, kept because it is the general lesson:* Release builds
strip Emscripten's assertions, so the first failure read `Aborted().` and
nothing more. Rebuilding with `-DTICKTIMER_WASM_ASSERTIONS=ON` turned the same
abort into *"'addRunDependency' was not exported… forcing filesystem support
(-sFORCE_FILESYSTEM) can export this for you"* — the fix, spelled out. The
diagnostic build is not a nicety; it is the difference between a sentence and a
word.

*Accepted limit:* iOS can evict browser storage after roughly seven days
without use. Add-to-Home-Screen makes it much stickier and guarantees nothing,
which is the real argument for logging in and letting the server hold the
authoritative copy.

## D. WASM keeps `DesktopNotifier`; it does not get a third implementation

*Decision:* `notify::make()` has one `#ifdef Q_OS_ANDROID` branch, and
WebAssembly falls through to `DesktopNotifier`.

*Why:* a `Qt::Tool` window renders inside the canvas, so an **open tab** still
gets its toast, correctly, with no new code. What it does not get is anything
while the tab is **closed**, because no timer runs there either.

*Why not a `WebNotifier` over the browser's Notification API now:* it would
cover the open tab — which already works — and not the closed one, which is the
actual gap. Only a service worker plus Web Push covers a closed tab, and that
is a different project with an iOS-version floor (16.4) we have not measured.
Building the easy half first would spend effort on the case that is already
handled and leave the real one open while looking finished.

*This is why `notify::make()` has a branch shape rather than an if/else* — the
third implementation is expected, and the seam is where it will go.

## E. Diagnosis rides in the URL, because a browser has no environment

*Decision:* the switches this codebase has always used as environment variables
get a URL form. `?nostore` skips the storage mount; `?probe` sets
`TICKTIMER_PROBE=1` inside the process before `main()` runs, through
Emscripten's `ENV`.

*Why:* the philosophy is already the house one — `TICKTIMER_COMPACT`,
`TICKTIMER_AI_DOWN`, `TICKTIMER_PROBE`, the whole Ctrl+Shift+D panel:
*a behaviour you cannot produce on demand is a behaviour you cannot verify.*
A browser tab simply has no environment to set, and "rebuild with a flag" is
not available to someone holding a phone. The URL is the only surface a phone
user can edit.

*Why it matters more here than anywhere else:* **Safari on iOS has no
developer console** without a Mac and a cable. `web/index.html`'s three most
important failures go to `console.error`, so on the target device the app's own
diagnosis is invisible. Anything a phone has to report must therefore be drawn
**on the page**. That is a platform constraint, not a preference, and it is
logged in `docs/TROUBLESHOOTING.md`.

*Alternative rejected:* writing diagnostics into the DOM from C++ via `EM_ASM`.
It would introduce the repo's first Emscripten include and a platform `#ifdef`
in service of a diagnostic — and the same overlay drawn in plain Qt works
identically on the desktop and on Android, where it would have turned v30.7's
*"suspected, not measured"* into a measurement.

*Alternative rejected:* a web-only switch — a query parameter the C++ learns
about some other way, or a `--probe` argument in `Module.arguments`. Either
would work, and both would mean the app has two different questions to ask
depending on where it is running. Handing the URL parameter over as a real
environment variable keeps exactly one:
`qEnvironmentVariableIsSet("TICKTIMER_PROBE")`, the same call the screenshot
tool has always made.

*What that cost, and the trap it left behind.* Emscripten builds the module as
a closure and exports only what it is asked to; `ENV` is not exported by
default. The obvious spelling —

```cmake
target_link_options(ticktimer PRIVATE -sEXPORTED_RUNTIME_METHODS=ENV)
```

— builds cleanly, warns about nothing, and **silently does nothing**, because
that option *replaces* the list rather than adding to it and Qt appends its own
(`UTF16ToString, stringToUTF16, JSEvents, specialHTMLTargets, FS, callMain`)
afterwards, so the last one wins. The fix is Qt's own append-instead-of-replace
seam, `set_target_properties(... QT_WASM_EXTRA_EXPORTED_METHODS "ENV")` — named,
as it happens, in the error `qtloader.js` throws when `qt.environment` is used
without it. Two general lessons: **a flag that is a list is a flag someone else
can overwrite**, and when a framework offers a property for a setting, the
property exists precisely because the raw flag composes badly. Logged in
`docs/TROUBLESHOOTING.md` and `docs/READING_GUIDE.md` §4.

## F. The layout is inherited, not ported — and its input is unverified

*Decision:* nothing in the WebAssembly build knows it is a phone.
`isCompactScreen()` asks about **space**, so an iPhone-sized canvas gets the
entire v30.7 phone shell — touch targets, mobile nav, full-screen dialogs — for
free.

*Why this is the right shape:* it is `design-addendum-android` §3.30's rule
holding for a third platform without an edit. A rule that survives a platform
it was not written for is the evidence that it named the real variable.

*And the input, which had never been read.* That rule is only as good as the
number it compares. On Android, `availableGeometry()` returned physical pixels
on a 1080x2400 device, so the check answered "desktop" on a phone and the app
opened with its content clipped off the right edge — and the cause was recorded
as *suspected, not measured*, because nothing had printed the value from inside
the running app. `TICKTIMER_COMPACT=1` cannot settle that: forcing the mode
proves what the mode *does*, never what the real input *is*. **A test that
supplies its own input can never validate that input.** Which is what §E's
probe is for.

*The reading (2026-08-31, desktop browser).* `availableGeometry()` **tracks the
browser window** — resizing the window and reloading changes it. It is derived
from the page's viewport, in CSS pixels, not from the physical display in
device pixels. That is the opposite of Android's failure, and it means an
iPhone-sized viewport falls under the 600 threshold on its own: **the phone
shell should arrive on the web with no platform code at all.**

*Still unmeasured, and named rather than rounded up:* iOS Safari specifically —
whether it reports the visual viewport the same way, what happens as the URL
bar hides and returns, what the chrome-less Add-to-Home-Screen mode does to the
available height, and whether a device pixel ratio of 3 changes the mapping.
The prediction is well-founded now instead of hopeful. It is still a
prediction, and `docs/ROLLOUT.md` Stage 4b step 1 is what turns it into a fact.

## G. The service worker, named as owed

*Not built.* There is no `sw.js`, no `navigator.serviceWorker`, and
`manifest.webmanifest` carries no offline story. Add-to-Home-Screen therefore
produces a **chrome-less bookmark**: tapping the icon fetches `start_url` from
the origin on every launch, so an unreachable server is Safari's error page
rather than TickTimer's honest "didn't start" screen — `index.html` is what
failed to arrive.

*Why this is worse than it sounds:* the desktop app and the Android APK are
installed binaries that own their copy, so v30.2's offline gate covers them.
The web app is downloaded fresh each launch, while her data sits in IndexedDB
the entire time — **intact and unreachable**, which is a worse outcome than
losing it. The Caddyfile's `no-cache` on the wasm is correct for deploy
freshness and works directly against this.

*Why it is not designed here:* a cache-first service worker over the app shell
closes it and puts the file Phase 5's push handler needs there early — but the
app has never run on an iPhone, and this project has now twice paid for
designing a platform it had not measured (`docs/ANDROID.md`'s "~40 minutes",
and the compact-mode promise that was false for as long as it existed). The
device pass comes first.

## H. Two copies of "the current version", and nothing keeping them in step

*The defect, recorded because it was live:* `/app/` served v30.4.2 while
`server/version.json` advertised 30.8.1. The web app asks `/version`, sees a
newer number, and shows an **update banner it cannot act on** — the Get-it
button sends a browser to a Releases page of Windows installers and APKs.

*Why it happened:* deploying the web app is a folder copy that lives in a
runbook, while bumping `version.json` is a release step with a script behind
it. Two facts, one automated, one remembered.

*The decision:* the redeploy joins the release routine in `docs/GITHUB.md`
rather than staying in someone's memory, and the trap is logged in
`docs/TROUBLESHOOTING.md` and `docs/READING_GUIDE.md` §4. The general shape is
one this repo already knows well: `Version.h` versus `installer/ticktimer.iss`
is the same hazard, and it was solved by making a build **hard-fail** on the
mismatch. `/app/` has no equivalent check yet; saying so is better than
implying it does.

---

## What changed where

| Layer | File(s) | Change |
|---|---|---|
| Build | `CMakeLists.txt` | `if(EMSCRIPTEN)` block: `-lidbfs.js`, `-sFORCE_FILESYSTEM`, the `TICKTIMER_WASM_ASSERTIONS` option; the tests/tools fence widened from "not Android" to `TICKTIMER_DESKTOP` |
| Build | `tools/build-wasm.bat` | checks emsdk, Ninja and an **asyncify** kit before doing anything; lays out `build-wasm\serve\` |
| Shell | `web/index.html` | boot screen, IDBFS mount + sync, `?nostore`, `?probe` |
| Shell | `web/manifest.webmanifest`, `web/icons/` | Add-to-Home-Screen identity |
| Deploy | `deploy/Caddyfile.example` | the `/app*` block, same origin as the API |
| Notifications | `src/Notifier.cpp` | WASM falls through to `DesktopNotifier`, with the gap named at the seam |

Domain, storage, tracking: **zero changes.** A third platform cost build recipe,
one HTML shell, and no rule — which is the layering promise paying out for the
second time.
