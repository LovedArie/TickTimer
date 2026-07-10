# Installing & distributing TickTimer (Windows)

This is about getting TickTimer onto a computer that **doesn't have Qt** —
your girlfriend's laptop, a friend's PC, your own machine without Qt Creator
open. It's the deployment story, and it's the same skill real Windows apps
ship with.

There are two levels. Start with the first; graduate to the second when you
want a polished result.

---

## The core problem (worth understanding once)

When you press ▶ in Qt Creator, it quietly puts Qt's DLLs on the path. Your
`ticktimer.exe` **depends on** those DLLs (`Qt6Core.dll`, `Qt6Widgets.dll`,
`Qt6Network.dll`, and more) but doesn't contain them. Send the bare exe to a
computer without Qt and it dies instantly — exit code `0xC0000135`,
"a required DLL was not found" — before it can even open a window.

The fix is a Qt tool called **windeployqt**: point it at an exe and it copies
every DLL and plugin that exe needs right next to it, producing a folder that
runs *anywhere*. Both levels below are just wrappers around that idea.

---

## Level 1 — the portable folder (double-click, zip, send)

The fastest path, and all you need for testing.

1. Double-click **`tools\deploy-windows.bat`**.
   It builds a Release version, then bundles both programs with their DLLs
   into **`dist\TickTimer\`**. It finds your Qt automatically under `C:\Qt`;
   if your Qt lives elsewhere, run it once as:
   ```
   set QTDIR=D:\Qt\6.8.0\mingw_64
   tools\deploy-windows.bat
   ```

2. Inside `dist\TickTimer\` you now have:
   - `ticktimer.exe`, `ticktimer-server.exe` and all their DLLs/plugins
   - **`TickTimer.bat`** — double-click to run the app
   - **`Start TickTimer server.bat`** — double-click to run the server
     (keeps its window open on purpose — that window prints the address)
   - **`READ ME FIRST.txt`** — the tester's plain-English guide

3. Right-click the `TickTimer` folder → **Send to → Compressed (zipped)
   folder**, and send the `.zip`. The recipient unzips it anywhere and
   double-clicks the two launchers. No install, no Qt, nothing to configure.

That's genuinely enough to have your girlfriend testing today.

---

## Level 2 — a real installer (`TickTimer-Setup.exe`)

When you want Start-menu shortcuts, desktop icons, and a clean entry in
"Add or remove programs" instead of a loose folder.

1. Install **Inno Setup** (free): <https://jrsoftware.org/isdl.php>
2. Run `tools\deploy-windows.bat` first (the installer packages whatever is
   in `dist\TickTimer\`).
3. Open **`installer\ticktimer.iss`** in the Inno Setup Compiler and press
   **F9**, or from a terminal: `iscc installer\ticktimer.iss`.
4. Out comes **`installer\Output\TickTimer-Setup.exe`** — send that single
   file. Running it installs TickTimer with shortcuts and an uninstaller,
   no admin rights required (it installs into the user's profile).

---

## The icon

`installer\ticktimer.ico` (a clock face with a teal tick) is compiled into
both exes via the `.rc` files, so Explorer shows a real icon instead of the
blank-page default — one small thing that makes a sent exe feel like an app
rather than a mystery file. The `.rc` is wired into `CMakeLists.txt` behind a
`$<$<PLATFORM_ID:Windows>:...>` generator expression, so Linux/macOS builds
ignore it cleanly.

---

## What about macOS / Linux?

- **Linux:** the app builds and runs directly; distribution is usually a
  `.desktop` file plus the system Qt, or a self-contained AppImage
  (a `linuxdeployqt` job, the Linux cousin of `windeployqt`). Not scripted
  here yet — noted in the backlog.
- **macOS:** `macdeployqt` bundles a `.app`; signing/notarisation is its own
  saga. Also future work.

The server is identical on every platform (it's a headless `QCoreApplication`),
so a Linux box or a Raspberry Pi makes a fine always-on host — see
`docs/SERVER.md`.

---

## Connecting two computers (the address)

A fresh install points its **Server** field at `http://localhost:8080`,
which only works when the server runs on the *same* machine. To point the app
at another computer's server, edit the **Server** box on the login screen to
the address that computer's server window prints (the "from another device"
line, e.g. `http://192.168.1.20:8080`). Both machines must share a Wi-Fi
network. The address is remembered for next launch. See `docs/RUNNING.md`
for the full two-program walkthrough.
