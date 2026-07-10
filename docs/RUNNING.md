# Running TickTimer (the two-program setup)

Since v16, TickTimer is **two programs** that must both be running:

| Program | What it is | You interact with it? |
|---|---|---|
| `ticktimer-server` | the backend — stores accounts, checks logins, holds synced planners | no — start it and leave it |
| `ticktimer` | the app you click around in | yes |

**The one rule: the server must be running BEFORE the app tries to log in.**
They're independent processes. Think of an online game — the server has to
be up before the client can connect.

---

## First time only: make the programs runnable outside Qt Creator

When you press ▶ in Qt Creator, it quietly puts Qt's DLLs on the path for
you. Run either `.exe` from a plain terminal and Windows can't find
`Qt6Core.dll` / `Qt6Network.dll`, so the program dies instantly with exit
code **-1073741515** (`0xC0000135`, "a required DLL was not found") — before
it can print anything.

**The easy way (recommended):** double-click **`tools\deploy-windows.bat`**.
It builds a Release version and bundles both programs with all their DLLs
into `dist\TickTimer\`, ready to run or zip and send. Full details, plus how
to make a real `TickTimer-Setup.exe`, are in **`docs/INSTALLING.md`**.

**The manual way (if you want to see what the script automates):** the fix
is `windeployqt`, which copies every needed DLL next to the `.exe`.

1. Open the **Qt command prompt**, not plain PowerShell: Start menu → search
   *"Qt 6.11.0 (MinGW 64-bit)"*. This terminal already knows where Qt lives.
2. Go to your build folder:
   ```
   cd "C:\Users\<you>\Documents\Project\TickTime\build\Desktop_Qt_6_11_0_MinGW_64_bit-Debug"
   ```
3. Deploy the DLLs for both programs:
   ```
   windeployqt ticktimer-server.exe
   windeployqt ticktimer.exe
   ```

Now both run from any terminal — and this is the same step that later lets
the app run on a computer that doesn't have Qt installed at all.

> **Pointing at another computer's server:** the app's login screen now has a
> **Server** field. It defaults to `http://localhost:8080`; to reach a server
> on a different machine, type that machine's address (the "from another
> device" line its server window prints) and log in. The address is
> remembered for next time.

---

## Every time: the launch sequence

**1. Build everything** (once per code change) — in Qt Creator,
*Build → Build All Projects* (`Ctrl+Shift+B`).

**2. Start the server**, in a terminal, and leave the window open:
```
.\ticktimer-server.exe
```
Success looks like this — it **holds the terminal** and prints:
```
TickTimer server listening on port 8080
  from THIS computer:      http://localhost:8080
  from another device:     http://192.168.1.42:8080
Press Ctrl+C to stop.
```
If it gives your prompt straight back instead, it exited — see
Troubleshooting below.

**3. Run the app** — two easy ways:

- **Two Qt command prompts** (simplest once deployed): leave the server
  running in Window 1, open a second Qt command prompt in the same build
  folder, and run `.\ticktimer.exe`.
- **Qt Creator:** check the **target selector** (bottom-left, above ▶) says
  **`ticktimer`**, not `ticktimer-server`, and press ▶.

Either way the login window appears, talks to the server, and you're in.

That target selector is the part people miss: click it to choose which of
the two programs the ▶ button launches. For normal use, leave it on
`ticktimer` and run the server from its own terminal.

---

## Why run the server from a terminal, not Qt Creator's ▶?

Qt Creator's ▶ runs **one** target at a time. Running the server from its own
terminal means it stays up across dozens of app restarts while you develop —
start it once in the morning, forget about it until you're done. (You *can*
switch the selector to `ticktimer-server` and press ▶ instead; you just have
to switch back to run the app, which is more clicks.)

---

## "Can't reach the server" — the checklist

Almost always one of these, in order of likelihood:

1. **The server isn't running** → start it (step 2).
2. **You closed the terminal** → the server died with it; reopen and restart.
3. **Windows firewall prompt** appeared the first time → allow it for
   **private networks**.

None of these is data loss — it's the login gate not finding the server. Your
planner (`data.json`) is untouched and separate.

---

## Troubleshooting the server itself

Run this to see the exit code the terminal otherwise hides:
```
.\ticktimer-server.exe ; echo "exited with code $LASTEXITCODE"
```

| Exit code | Meaning | Fix |
|---|---|---|
| (none — it holds the terminal) | **working** | leave it open, run the app |
| `-1073741515` | missing DLL | `windeployqt ticktimer-server.exe` (see above) |
| `1` | port already in use | a server's already running: `taskkill /IM ticktimer-server.exe /F`, then retry |
| `0`, returns immediately | started but exited | usually the DLL case; try the Qt command prompt |

---

## Pointing the app at a different server

The app reads the server address from its settings key `sync/serverUrl`,
default `http://localhost:8080`. For a phone on the same Wi-Fi, set it to the
`192.168.x.x` address the server printed on startup. Later, a Raspberry Pi's
address goes here — same app, one setting, no rebuild. See
[SERVER.md](SERVER.md) for the hosting details and consequences.
