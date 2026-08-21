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

- **Compact layout, automatically**: the nav rail starts collapsed (☰ opens
  it), the tagline and glance panel yield, Pomodoro settings stack. Decided
  by *screen size*, not by platform — a tablet gets the desktop layout.
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

## If something goes wrong

| Symptom | Fix |
|---|---|
| No Android kit appears | Step 1 component not installed for **6.11.1** exactly — re-run Maintenance Tool. |
| Kit has a red/yellow icon | Preferences → Devices → Android: one of the four lights is off; re-run **Set Up SDK**. |
| Phone not in device list | Re-plug; check the USB-debugging prompt on the phone; try another cable (charge-only cables exist). |
| Gradle errors on first build | Almost always a blocked download — check internet/proxy, press Run again. |
| Installs a *second* TickTimer | The package name changed between builds. It's pinned in CMakeLists (`org.ticktimer.app`) — don't edit it. |
