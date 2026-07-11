# TickTimer

> See where your time really goes — a C++/Qt desktop app that turns your day
> into a colour-coded record and reveals real productivity versus
> anxiety-driven procrastination.

*Status: **v19.1 — working desktop app, Android-ready, distributable, with accounts, device sync, share & compare (now with side-by-side schedules), update notices, task priorities, and an archive.** Daily **and weekly**
planner with live focus tracking (focus / break / **distracted**),
**drag-to-resize** blocks, **blocks that say what they are** (labels, task
blocks, spontaneous blocks — with task notes and column-flowed text), tasks &
deadlines, folders, special days, and week/month reviews. **33** automated
domain tests + a headless **UI regression suite**. Login is handled by a small self-hosted server (`ticktimer-server`) you run
on your own machine — no Google, no cloud. (Formerly "Time & Focus Tracker".)*

![TickTimer — the daily planner](docs/screenshot.png)

## Why it exists

"How productive was I today?" is normally a vague feeling. **TickTimer**
makes it visible: you plan your day by activity, track your *actual* focus
and break time inside each block, and see where your hours really went. It
credits every part of a balanced life — work, health, relationships, rest —
instead of labelling non-work time "unproductive." Built especially for
people whose focus is easily derailed by anxiety-driven procrastination.

## Features

- **Plan your day** on a 30-minute-slot calendar (6 AM–midnight) — click a
  free slot, then pick an activity, pick one of your **open tasks**, or just
  **type what you're doing** (spontaneous blocks need no activity at all).
- **Blocks say what they are** — task blocks show the task's title and wear
  its life-area colour (✓ once done); spontaneous blocks need only a title (first line = the name, extra lines become its notes);
  and you can **link a task to any existing block**: type in its label field
  and pick from your open tasks ("Study GTI350" on top, "Lab 4" underneath).
- **Rich text on blocks** — the label is a real **multiline** box; a block
  stacks name → time → task → description → comments, as much as its height
  allows. A **"Task notes" toggle** (right above the agenda) shows each
  linked task's description indented beneath it, and long notes **flow into
  a second column** instead of clipping while the right half sits empty.
- **See the whole week** — a seven-day agenda sharing one time axis, built by
  reusing the day agenda for each column; plan or open a block on any day.
- **Track real time inside each block** — focus, break, and **distracted**
  (off-task) time, each a coloured slice of the plan-vs-actual bar as it grows.
  The gap between plan and reality is the whole point.
- **Resize a block by dragging its edge** — the ↕ handle snaps to the grid and
  stops at the next block or midnight; the domain refuses anything illegal.
- **Crash-safe by design**: a heartbeat keeps the live timer on disk, so
  even a power cut loses at most ~30 seconds of tracked time.
- **Tasks with due dates** ("date TBD" is a first-class state), grouped
  under your life areas next to the reusable activities.
- **Upcoming** — every dated, unfinished task across all areas: Overdue /
  This week / Later.
- **Folders** organise your life areas in the rail; **special days** count
  down to birthdays, holidays, and vacations (yearly repeats supported).
- **Review your time** by life area — bars show **productive (focused) time
  only**, while break, **distracted**, and **unaccounted** time (planned
  blocks that elapsed untouched) accumulate in their own rows, with a
  day-split **pie chart**; zero rows stay hidden — across day, week, and
  month — every number derived live from raw tracked segments, never
  stored twice.
- **Pomodoro timer** for focused work cycles (25/5, long break every 4th).
- **Sync between devices** — a Sync button pushes/pulls your planner
  through your own server, with revision checks so two devices can never
  silently overwrite each other; conflicts are always a visible human
  choice. See [docs/design-addendum-sync.md](docs/design-addendum-sync.md).
- **Your own accounts, your own server** — a login gate backed by
  `ticktimer-server`, a small program you run on your laptop (a Raspberry Pi
  later). Passwords are salted and stretched, never stored in plaintext; no
  identity provider, no cloud dependency. See [docs/SERVER.md](docs/SERVER.md).
- **Share & compare** — grant someone read access to your planner (one
  direction, revocable any time) and see your day next to theirs: focus,
  break, distracted, total, with a gentle who's-ahead headline. Permissions
  are enforced by the server; the comparison runs entirely on your device.
  See [docs/design-addendum-share.md](docs/design-addendum-share.md).
- **Task priorities & the Archive** — rank tasks urgent/medium/low and view
  Upcoming through four lenses; archive finished tasks and retired
  activities to a quiet page (hidden everywhere, restorable any time,
  history intact). Fix your tracked time by hand — add the focus block you
  forgot to start, retract the timer you left running.
  See [docs/design-addendum-daily-driver.md](docs/design-addendum-daily-driver.md).
- **Update notices** — the app checks your server on launch and shows a
  quiet, dismissible banner when a newer release exists (dismissing one
  version never silences the next). Announcing a release is editing one
  `version.json` on the server — no restart. See
  [docs/design-addendum-update.md](docs/design-addendum-update.md) and
  [docs/GITHUB.md](docs/GITHUB.md) for the release workflow.
- **Runs on Android** — same codebase, cross-compiled; a compact layout
  (collapsed rail, stacked panels, finger scrolling) kicks in automatically
  on phone-sized screens. See [docs/ANDROID.md](docs/ANDROID.md) for the
  one-time setup; after that it's one press of Run in Qt Creator.
- **Local and private** — one human-readable JSON file on your device
  (`AppData/Roaming/TickTimer/data.json` on Windows). Saves are atomic:
  a crash mid-save can never corrupt your history.

## Built with

- **C++17** and **Qt 6 Widgets** — no dependencies beyond Qt itself; all
  charts are custom-painted.
- **CMake** builds; **QTest** suites: 33 domain tests plus a headless UI regression
  suite (real widgets on the offscreen platform).
- Storage: versioned **JSON** (v6), migrating to **SQLite** as data grows.
  User preferences (Pomodoro durations) live in **`QSettings`**, kept separate
  from domain data.

## Project status

Built deliberately, following the Unified Process — requirements first,
then an interactive prototype, then iterative implementation:

- **Done** — full requirements set, throwaway UX prototype, and the
  implementation: all five planned iterations **plus several unplanned feature
  iterations** (tasks & deadlines; folders / upcoming / special days; Pomodoro
  settings & the due strip; the week agenda, drag-to-resize, and distracted-time
  tracking), each designed via a reviewed addendum before any code.
- **Living docs** — see [`/docs`](docs/): Vision · Use-Case Model ·
  Supplementary Specification · Glossary · Risk List · Iteration & Phase
  Plans · [design doc](docs/design-doc.md) with six design addenda
  ([tasks](docs/design-addendum-tasks.md) ·
  [organizing](docs/design-addendum-organizing.md) ·
  [task details](docs/design-addendum-task-details.md) ·
  [agenda & tracking](docs/design-addendum-agenda-and-tracking.md) ·
  [block labels](docs/design-addendum-block-labels.md) ·
  [android](docs/design-addendum-android.md)) ·
  a [reading guide](docs/READING_GUIDE.md) to the codebase ·
  a symptom-indexed [troubleshooting log](docs/TROUBLESHOOTING.md) ·
  a [question bank](docs/QUESTION_BANK.md) for self-testing.

## Getting started

### Prerequisites

| Tool | Version | Why |
|---|---|---|
| **Qt** | 6.4 or newer (built & tested on 6.4; runs on 6.8 LTS) | the GUI framework |
| **C++ compiler** | C++17-capable — GCC 11+, Clang 14+, MSVC 2022, or Apple Clang | compiles the code |
| **CMake** | 3.21 or newer | build system |
| **Ninja** *(or Make)* | any recent | build backend |
| **Qt Creator** *(optional)* | bundled with the Qt installer | open `CMakeLists.txt` directly — it *is* the project |

Install everything at once with the
[Qt Online Installer](https://www.qt.io/download-qt-installer); on
Ubuntu/Debian, `sudo apt install qt6-base-dev cmake ninja-build g++` works
too.

### Build

```bash
git clone https://github.com/<you>/ticktimer.git
cd ticktimer
cmake -B build -G Ninja
cmake --build build
```

This builds **two programs**: `ticktimer` (the app) and `ticktimer-server`
(the login/sync backend).

Run the test suite (63 tests: domain, UI, auth, and live end-to-end):

```bash
ctest --test-dir build --output-on-failure
```

### Run

Since v16, TickTimer is a **client + self-hosted server**, so you launch two
programs — **the server first, then the app**. On Windows the smoothest way
is the **Qt command prompt** (Start menu → *"Qt 6.11.0 (MinGW 64-bit)"*),
which already has Qt on its path.

**One-time setup** — deploy the Qt DLLs and plugins next to each exe so both
run outside Qt Creator. In the Qt command prompt, from the build folder:

```
cd build\Desktop_Qt_6_11_0_MinGW_64_bit-Debug
windeployqt ticktimer-server.exe
windeployqt ticktimer.exe
```

**Every time** — two Qt command prompt windows:

```
# Window 1 — start the server, leave it open. It prints its address:
#   "TickTimer server listening on port 8080"
.\ticktimer-server.exe

# Window 2 — start the app:
.\ticktimer.exe
```

The app shows a login screen, talks to the server, and opens your planner
(first launch: **Create account**; after that, **Log in**). If it says
*"Can't reach the server,"* the server isn't running — start Window 1 first.

### Distribute (send it to someone without Qt)

To get TickTimer onto a computer that doesn't have Qt — a tester's laptop,
a friend's PC — you don't type any of the above. Just double-click
**`tools\deploy-windows.bat`**: it builds a Release version and bundles both
programs with every Qt DLL into a portable **`dist\TickTimer\`** folder with
click-to-run launchers and a plain-English readme. Zip it and send it.

For a polished `TickTimer-Setup.exe` with Start-menu shortcuts, compile
**`installer\ticktimer.iss`** with [Inno Setup](https://jrsoftware.org/).
Both paths are in **[docs/INSTALLING.md](docs/INSTALLING.md)**. Non-technical
testers get **[docs/FOR-TESTERS.md](docs/FOR-TESTERS.md)** (bundled as
*READ ME FIRST.txt*).

To point the app at a server on another machine, edit the **Server** field on
the login screen to that machine's address (both on the same Wi-Fi).

On **Linux/macOS** no deploy step is needed; just run the two binaries:

```bash
./build/ticktimer-server        # window 1, leave open
./build/ticktimer               # window 2
```

You can also run from **Qt Creator**: open `CMakeLists.txt`, pick your kit,
and use the **target selector** (bottom-left, above ▶) to launch
`ticktimer-server` first, then switch to `ticktimer`. Creator sets up the Qt
paths for you, so the `windeployqt` step above is only needed for running
from a terminal or shipping to another machine.

> **Two Windows first-run gotchas**, both fixed by the `windeployqt` step:
> exit code `-1073741515` (missing DLLs), or *"no Qt platform plugin could be
> initialized"* (missing GUI plugins — deploy `ticktimer.exe` specifically).
> Details in **[docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md)**; full
> launch manual in **[docs/RUNNING.md](docs/RUNNING.md)**.

Your planner lives in a per-account JSON file (the app shows its exact path
on startup); accounts and synced planners live in the server's own folder,
kept separate. See **[docs/SERVER.md](docs/SERVER.md)** for hosting details.

## Roadmap

- [x] Requirements set & interactive prototype
- [x] Iteration 1 — categories, activities, persistence
- [x] Iteration 2 — daily calendar planning
- [x] Iteration 3 — focus / break timer (plan vs. actual, crash recovery)
- [x] Iteration 4 — week & month reviews
- [x] Iteration 5 — Pomodoro & polish
- [x] Tasks & due dates · Upcoming view
- [x] Folders · Special days
- [ ] Drag-and-drop into folders
- [ ] Plan a task onto the agenda (tasks meet plan-vs-actual)
- [x] Per-account local data + one-time migration of existing planner
- [ ] Remember window & sidebar state (QSettings)
- [ ] SQLite storage
- [ ] Android build
- [x] Cross-device sync
- [x] Share & compare planners
- [x] Update notices (networked arc complete)
- [x] Task priority + archive + honest-tracking editor (daily-driver pass)

## License

To be decided.
