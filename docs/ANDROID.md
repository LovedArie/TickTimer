# Building TickTimer for Android

Two ways to get TickTimer onto your phone, sharing the same one-time setup
(steps 1–4, ~30–45 min, mostly downloads):

- **Option A — USB deploy:** press Run in Qt Creator; it installs and
  launches on the connected phone. Best while iterating.
- **Option B — the APK file:** build once, copy the .apk to the phone any
  way you like, tap to install. No cable, reinstallable forever.

The codebase is already Android-ready: `qt_add_executable` in CMakeLists is
what lets Qt package the app as an APK, the Android properties are set, and
the compact-screen layout kicks in automatically on a phone. Nothing to edit.

---

## 1. Install the Qt-for-Android component (one time)

1. Open the **Qt Maintenance Tool** — `MaintenanceTool.exe` in your Qt
   folder (usually `C:\Qt`).
2. Choose **Add or remove components** → expand **Qt → Qt 6.11.1**.
3. Check **Android** (this brings the `arm64-v8a` build of Qt — the
   architecture of essentially every phone from the last ~8 years).
4. Next → install. This is Qt compiled *for Android* — same Qt, different
   CPU target. Your MinGW desktop Qt stays untouched.

## 2. Let Qt Creator install the Android toolchain (one time)

1. Qt Creator → **Edit → Preferences → Devices → Android**.
2. If **JDK location** is empty, use the download button next to it
   (Android's build tools are Java-based — your Java background is literally
   part of the toolchain here: Gradle builds the APK shell around the C++).
3. Click **Set Up SDK**. Qt Creator downloads the Android SDK, NDK, and
   platform tools to a folder it manages. Accept the licenses when asked.
4. Wait for all four status lights on that page to go green.

## 2b. OpenSSL (one time) — without it the app cannot reach your server

**Qt for Android ships the TLS *backend plugin* and not OpenSSL itself.** The
resulting APK builds clean, installs, launches, renders, and does plain HTTP
perfectly — and every `https://` request fails before it even gets an HTTP
status code, which the app can only report as *"Can't reach the server. Is it
running, and is the address correct?"* Both true; neither the problem.

1. Qt Creator → **Preferences → Devices → Android** → **Download OpenSSL**
   (or clone <https://github.com/KDAB/android_openssl>).
2. That is all. `CMakeLists.txt` finds it automatically, checking
   `ANDROID_SDK_ROOT`, `ANDROID_HOME`, and the default SDK location on Windows
   and Linux. Point at it explicitly if it lives elsewhere:
   `-DTICKTIMER_ANDROID_OPENSSL=<path>`.

**The configure HARD-FAILS if OpenSSL is missing**, deliberately: the whole
deployment is HTTPS, so an Android build without TLS is not a degraded build,
it is a broken one, and a warning would scroll past in a Gradle log and collect
its debt later on somebody else's phone. Watch the configure output for:

```
-- Android OpenSSL: C:/Users/you/AppData/Local/Android/Sdk/android_openssl
```

To check a built APK yourself — it is a zip, and it must contain **both**:

```
lib/arm64-v8a/libssl_3.so
lib/arm64-v8a/libcrypto_3.so
```

If you only see `libplugins_tls_qopensslbackend_*.so`, that is the plugin
without the library, and HTTPS will not work.

## 3. Put your phone in developer mode (one time)

1. Phone: **Settings → About phone** → tap **Build number** 7 times
   ("You are now a developer!").
2. **Settings → System → Developer options** → enable **USB debugging**.
3. Plug the phone into the PC. On the phone, allow the
   "Allow USB debugging?" prompt (check *Always allow*).

## 4. Activate the Android kit for this project (one time)

1. Open the TickTimer project in Qt Creator.
2. Left sidebar → **Projects** (the wrench icon).
3. Under *Build & Run*, click the new kit —
   **Android Qt 6.11.1 Clang arm64-v8a** — to activate it.
4. Expected: a fresh CMake configure runs. The test and screenshot targets
   won't appear on this kit — that's the `if(NOT ANDROID)` fence in
   CMakeLists doing its job, not a problem.

## 5. Option A — run over USB (every time)

1. Bottom-left kit selector: pick the **Android** kit, and your phone as
   the device.
2. Press **Run** (green triangle).
3. First build only: Gradle downloads itself (a few minutes, needs
   internet). Then the APK builds, installs, and TickTimer opens on the
   phone.

Switch back to the **MinGW** kit in the same selector for desktop work —
both build directories coexist.

---

## Option B — install from the APK file (no cable, no Qt Creator on the phone side)

Every Android build produces a real **.apk file** — the app in a box. Once
you have it, installing is just "get the file onto the phone and tap it."
Great for reinstalling later, putting it on a second phone, or handing the
app to a friend.

You still need steps 1–2 and 4 above ONCE — something has to *compile* the
APK, and that's your PC. But the phone never needs a cable for this route.

### 1. Build the APK

In Qt Creator on the **Android kit**, just press **Build** (hammer icon —
no phone needed, no Run). The APK lands in the build folder:

```
<project>\build\...Android...\android-build\build\outputs\apk\debug\android-build-debug.apk
```

Easiest way to find it: right-click the project → *Show in Explorer*, open
the Android build folder, and search for `*.apk`.

### 2. Get it onto the phone

Any file transfer works — pick your favourite:
- USB file copy (phone in "File transfer" mode — no developer mode needed),
- Google Drive / email it to yourself,
- a chat app's "send file".

### 3. Install it

**Fastest route for YOUR OWN phone: skip the browser entirely.** USB debugging
is already on from §3, and this upgrades in place, keeping the app's data:

```sh
adb install -r <path>/android-build-ticktimer-release-signed.apk
```

Worth preferring, and not only for speed: on the first real run the phone's
download manager stuck at 100% and never finished, while the server was serving
the file perfectly (verified byte-for-byte against the local build). The
browser route below is how everyone ELSE installs it, so it still has to work
— it is just the slow way to iterate on the phone in your hand.

Useful companions when something looks wrong, none of which need a debug build:

```sh
adb devices -l                                    # is the phone even there
adb shell pidof org.ticktimer.app                 # is the app running
adb logcat --pid=<that pid>                       # ONLY the app's log
adb shell dumpsys package org.ticktimer.app | grep version
adb shell am force-stop org.ticktimer.app         # the way out of a soft-lock
adb shell screencap -p /sdcard/s.png && adb pull /sdcard/s.png
```

That last pair is worth knowing: a screenshot pulled off the phone is how the
frameless-`SyncDialog` soft-lock was identified, and it settled in one look
what several rounds of description could not.

### 3b. Through the browser (how anyone else installs it)

Tap the APK in the phone's file manager. Android will ask to allow
**"Install unknown apps"** for that file manager — allow it (this is
Android's normal gate for anything not from the Play Store; the app is
yours, the warning is generic). Tap Install. Done — TickTimer is in your
app drawer like any other app.

### The signing rule (read this before it bites)

Android identifies "the same app" by **package name + signing key**.

- The **debug** APK above is auto-signed with a throwaway key Qt Creator
  generates — perfect for your own phone.
- If you later build a **release** APK (Projects → Build Steps → *Build
  Android APK* → Application Signature → create a keystore), it's signed
  with a DIFFERENT key — and Android will refuse to install it *over* the
  debug one. Uninstall first, then install the release build. Same rule in
  reverse. (Your data lives in the app's private folder, so uninstalling
  deletes it — export/back up `data.json` first if it matters.)

For your own phone, staying on debug APKs forever is completely fine.

---

## Giving it to someone else (v30.3)

Debug APKs are fine forever on your own phone. The moment a *second* person
installs it, you want a **release build signed with your own key** — and you
want that key created once and kept forever.

### Why the key matters more than the build

Android identifies "the same app" by **package name + signing key**. Change
the key and every phone must **uninstall first**, taking its local planner
with it. There is no recovery and no rotation: a lost keystore means everyone
starts over.

So: make it once, back it up somewhere private, never commit it. `.gitignore`
already refuses `*.keystore`, `*.jks` and `android-release-key*`, but the rule
that actually protects you is the backup.

### 1. Create the keystore (once, ever)

```sh
keytool -genkey -v -keystore android-release-key.jks \
        -keyalg RSA -keysize 2048 -validity 10000 -alias ticktimer
```

Ten thousand days is about 27 years — long enough that it never becomes a
problem you have to remember. Keep the passwords with the file.

### 2. Point Qt Creator at it

**Projects → Build (Android kit) → Build Steps → Build Android APK →
Application Signature** → browse to the `.jks`, enter the passwords, tick
**Sign package**. Switch the build to **Release** while you are there.

Build, and the APK lands under `android-build/build/outputs/apk/release/`.

**RE-CONFIGURING CMAKE CLEARS THIS.** After any change that re-runs CMake —
including editing `CMakeLists.txt`, which you will do for OpenSSL — the *Sign
package* tick comes back OFF, silently, and the build output becomes
`…-release-unsigned.apk`. An unsigned APK will not install, and the error
Android gives says nothing about signing. **Check the filename says `signed`
before you copy it anywhere.** Verify properly with:

```sh
apksigner verify --verbose <apk>
```

`Verified using v3 scheme: true` is enough — v3 covers Android 9+, which is
this project's `QT_ANDROID_MIN_SDK_VERSION`.

### 3. The version stamps itself

Nothing to type. Since v30.3 CMake reads `include/Version.h` and derives both
Android version fields from it, so `versionName` is the app's real version and
`versionCode` is `major*10000 + minor*100 + patch` (30.2.1 → `300201`). The
configure step prints what it derived:

```
-- TickTimer version 30.2.1 (Android versionCode 300201)
```

`versionCode` must never go **backwards** — Android refuses to install an
older code over a newer one. Since it is derived from a version number that
only goes up, that takes care of itself.

*(These two used to be hand-typed and had drifted to "14" while the app said
30.2.1. The fix was not a test but removing the seam.)*

### 4. Put it where people can reach it

If you are running the server on a VPS (`docs/SERVER.md`), the Caddy config in
`deploy/Caddyfile.example` already serves a downloads folder. Copy the APK to
`/var/www/ticktimer/` and the install instruction becomes one line:

> Open **https://ticktimer.example.com/download/ticktimer.apk** on your phone,
> tap the file when it finishes, and allow "install unknown apps" for your
> browser when Android asks.

That prompt is Android's normal gate for anything outside the Play Store. It
is generic, not a warning about this app.

**Tell them the invite code too**, if you started the server with `--invite` —
they will need it on the "Create account" screen, once.

### 5. Updating someone else's phone

Same URL, new file. Because the release build is signed with the same key and
carries a higher `versionCode`, tapping the new APK **upgrades in place** and
keeps their data. That is the whole payoff for making the key once.

The app can also *tell* them an update exists: put `version.json` in the
server's data folder with `latest` set to the new version and `url` pointing at
the APK (see `server/version.example.json`). The app shows a banner; it never
downloads or installs anything by itself.

---

## What to expect on the phone

- **Compact layout — fixed in v30.5, and verified on the phone this time.**
  The layout mode is decided by how much width a page was actually handed, not
  by the screen and not by the platform, and it is re-evaluated whenever that
  width changes — so a tablet gets the desktop layout and a rotation is
  noticed. On a Galaxy S21 Ultra (360x800 logical, dpr 3.0) the whole app now
  fits: the rail starts collapsed, the tagline and account name yield, the
  Pomodoro page scrolls, the agenda subtitle wraps instead of running off the
  edge. `docs/design-addendum-responsive.md` has the decisions;
  `docs/TROUBLESHOOTING.md` has the two traps only a real phone could teach.

  **One thing is still wrong, deliberately left for the next pass:** the nav
  rail is still *in flow*, so **tapping ☰ pushes the content off the right
  edge again**. Tap it a second time to get back. Making the rail an overlay
  drawer on compact screens is the next piece of work, and it is required
  rather than polish.

- **Plan a block with a PRESS AND HOLD, not a tap** (v30.5.2). A tap is how
  you scroll the day, so tapping a free slot no longer opens anything — hold it
  for a moment instead. Tapping an existing block still opens it. The agenda's
  own caption says which gesture applies on the device you are holding.

- **Dialogs fill the screen and Back closes them** (v30.5.1). Quick capture is
  the exception: it takes the full width but only the height it needs (v30.5.2),
  since a one-line command palette has no use for the whole page. Quick capture,
  the block picker, Sync and the login screen are each their own window, so the
  page layout work did not reach them; they now take the whole screen on a
  phone, and Android's Back gesture dismisses any of them.

- **Finger scrolling** everywhere there's a list or the agenda
  (kinetic/flick, via `QScroller`).
- **Your data is local, and it syncs**: Android gives the app its own private
  data folder (`QStandardPaths` resolves it automatically), so the phone keeps
  its own `data-<you>.json`. Since v16 those copies **do** sync through the
  server — log in as the same account on both and the planner follows you.
  (This paragraph claimed "no sync between them" for fourteen versions after
  sync shipped. Corrected in v30.2.)
- **It opens without the server** (v30.2): tick *Remember this device* at
  login and the phone signs itself in on later launches with no password. With
  no server in reach it offers to work offline on the data already there, and
  syncs by itself once the server answers again. A phone that has NEVER
  synced has nothing local to open, so the first login must happen online.
- **Known limitation — edge-resize**: on a touchscreen, dragging on the
  agenda scrolls (the far more common gesture). Resize/move blocks through
  the block dialog's nudge buttons instead.
- **It looks like the desktop app**, because it is: Qt *Widgets* runs on
  Android but doesn't imitate native Android controls. Fully usable; a
  native-feeling mobile UI would be a QML front-end on the same domain
  layer — a natural future project (the domain wouldn't change at all).

## Notifications on Android (v30.6)

Before v30.6 the phone showed **nothing** — no block alarm, no Pomodoro
chime, no block-finished toast, whether the app was open or closed. The story
is in `docs/TROUBLESHOOTING.md`; what you need to know to use it is here.

**What you get now.** A planned block starting, a tracked block finishing, a
Pomodoro phase ending, and the morning check-in all arrive as real Android
notifications — heads-up banner, sound, on the lock screen — **with the app
closed and the phone asleep**. They survive a reboot and a re-install.

**One permission prompt, on first launch.** Android has required apps to ask
before notifying since Android 13. A couple of seconds after the app opens
you will be asked once. Say yes; there is no second chance from inside the
app, and if you say no the alarms are dropped in silence. To fix it later:
**Settings → Apps → TickTimer → Notifications**.

**The Samsung tax — the one thing that will bite you.** One UI runs its own
app-sleep layer on top of Android's Doze, and it is aggressive with apps you
have not opened for a few days. This is the single most likely reason an
alarm goes missing after a quiet weekend. Exclude TickTimer once:

**Settings → Battery → Background usage limits** → make sure TickTimer is
**not** in *Sleeping apps* or *Deep sleeping apps*. Then **Settings → Apps →
TickTimer → Battery → Unrestricted**.

It is a manual step on purpose: the alternative is the app demanding a
battery-optimisation exemption on first launch, which is a worse thing to do
to someone than a line in a doc.

**Known cosmetics.** The sound is your system notification sound, not
TickTimer's own chime — the app's WAVs live in Qt's resource system, which an
Android notification channel cannot reach.

**Not scheduled, deliberately:** the affordability nudge ("Lab 4 is looking
tight") only appears while the app is open. Its whole value is the live
numbers in the sentence, and an alarm set two days in advance would buzz
with stale arithmetic. See the addendum's *Limits, named*.

**Checking it yourself**, with the phone plugged in:

```sh
# what the OS is actually holding for us
adb shell dumpsys alarm | grep -i ticktimer

# are we allowed to speak, and is the channel a heads-up one
adb shell dumpsys package org.ticktimer.app | grep POST_NOTIFICATIONS
adb shell dumpsys notification --noredact | grep -A2 ticktimer
```

An empty first result with blocks planned in the next two days means the
schedule never reached the OS. In the app, `Ctrl+Shift+D` → **Block alarms**
→ *Show the schedule* prints exactly what was handed over, and its first line
says whether the platform or the app is holding it.

## If something goes wrong

| Symptom | Fix |
|---|---|
| No Android kit appears | Step 1 component not installed for **6.11.1** exactly — re-run Maintenance Tool. |
| Kit has a red/yellow icon | Preferences → Devices → Android: one of the four lights is off; re-run **Set Up SDK**. |
| Phone not in device list | Re-plug; check the USB-debugging prompt on the phone; try another cable (charge-only cables exist). |
| Gradle errors on first build | Almost always a blocked download — check internet/proxy, press Run again. |
| `error: redefinition of 'sync' as different kind of symbol` | A namespace collided with a POSIX function bionic declares and MinGW does not. Ours was renamed to `syncplan`; see `docs/TROUBLESHOOTING.md`. |
| Login says *"the server answered, but not in a way this app understands"* | The Server field has no scheme, so the app prepended `http://` and got Caddy's empty 308. Type the full `https://your-domain`. |
| Login says *"Can't reach the server"* but the server is up | OpenSSL is missing from the APK — see §2b. Plain HTTP works and HTTPS cannot start, so it looks like a network fault and is not one. |
| APK will not install, no useful error | It is the `-unsigned.apk`. A CMake re-configure clears *Sign package*; re-tick it (§"Point Qt Creator at it"). |
| Download sticks at 100% and never installs | The phone's download manager, not your server. Use `adb install -r <apk>` instead (§3). |
| Content clipped at the right edge with the rail CLOSED | Fixed in v30.5. If you see it again, the phone-width budget test in `test_ui` has been broken — run it. |
| Content clipped at the right edge with the rail OPEN | Known and expected: the rail is still in-flow and adds 190 logical px. **Tap ☰** to close it. The overlay-drawer rework is the next stage. |
| Stuck in a dialog, Back does nothing | Fixed in v30.5.1 — Back dismisses any dialog. On older builds: | Known, 2026-08-22: `SyncDialog` is modal and renders frameless over the page. `adb shell am force-stop org.ticktimer.app`. Sync runs automatically regardless. |
| Installs a *second* TickTimer | The package name changed between builds. It's pinned in CMakeLists (`org.ticktimer.app`) — don't edit it. |
| Notifications never arrive on the phone | Three checks in order: the permission prompt was answered yes (**Settings → Apps → TickTimer → Notifications**); TickTimer is excluded from Samsung's *Sleeping apps*; and `adb shell dumpsys alarm | grep -i ticktimer` lists something. See **Notifications on Android** above. |
