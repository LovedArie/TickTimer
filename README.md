# TickTimer

> See where your time really goes — a C++/Qt desktop app that turns your day
> into a colour-coded record and reveals real productivity versus
> anxiety-driven procrastination.

*Status: **v29.1 — working desktop app, with a **task-intake interview** (v29.1: the NEEDS DETAILS queue gains a voice — one C++ question per unsized task with a guess computed from your own tracked history, your one-sentence answer parsed crisp-first in C++ and by the model only for prose, and every extraction still crossing the proposal card; works with every AI seat down), atop the **write boundary** (v29.0, Slice 1: the assistant's first hands, and they only move when you tap — a closed per-role verb set where intake alone may write, and only additively; per-turn [T1] handles so no UUID ever reaches a model; proposal cards with Apply/Discard rendered from the structured request itself; data.json copied aside before any change; a NEEDS DETAILS queue of captured-but-unsized tasks; entirely model-less this slice — the debug panel plays the model, so the whole confirm loop is forceable by hand), with a **morning check-in** (on computably heavy days only, once, 06:00–11:00: a toast invites you, one tap opens the assistant, one more tap records your mood — rough/okay/good, kept 14 days, and your note never enters any AI prompt; the coarse history reaches the assistant ONLY when every configured seat is local to your machine), now with a **proactive heads-up** (an affordability verdict per deadlined task — computed from your own plan, honest when it can't know, volunteered as an alert toast under strict manners: change-of-verdict only, quiet hours, a daily cap, and a TIGHT pill on Upcoming — since 28.1 the assistant phrases the heads-up in your chosen persona — with the plain C++ sentence as the always-works voice when the model is unreachable, slow, or over-wordy — and the chat can answer "can I go out tonight?" from the same computed verdict), Android-ready, distributable, with an **AI assistant page** (chats about your day — it sees the plan, the tracked time and the tasks, rebuilt fresh every turn, and since v29.2 may propose one thing and one only: moving a block you already missed, into a slot the app computed — confirmed by your tap, never taken; pick its voice — Calm, Brief, Coach, or your own — with the safety rules locked above every style; give it a fallback seat that answers when the primary is unreachable — and only then, never masking a wrong key; "What can it see?" shows the exact context sent), natural-language quick-add ("lab 4 friday urgent weekly #school" becomes a fully-dressed task, with a live parse preview) available in Activities and via a global Ctrl+N capture overlay (click-away to dismiss, Ctrl+Enter asks an AI to parse the messy lines — any provider: Anthropic, OpenAI, Groq, a local Ollama — reasoning models handled: private <think> deliberation is scrubbed, answers routed to a reasoning field are recovered — or your own endpoint), model/view task lists (QAbstractListModel + custom delegates) on the Upcoming and Activities pages, accounts, device sync, share & compare (side-by-side schedules with pinned name headers), update notices, task priorities, an archive, a Settings dialog grown into a nav rail of pages (Agenda, Needs a block, Catch-up, Assistant), a grown-up Pomodoro (notifications, tracker link with a live status line, pin-on-top mini timer), block-start alarms, a live badge on the tracked block, real recurrence for tasks and blocks.** **Subtasks & sizing** (v28.3): any task can hold a one-level checklist of **pieces** — real tasks with optional deadlines of their own, living in the parent's detail panel with a "☑ 2/5" chip on the Upcoming card — plus an **estimate** ("90 min") and a "fits short gaps" flag, all persisted, synced, and carried through recurrence (design record: `docs/design-addendum-subtasks.md`). **Sizing intelligence** (v28.4): affordability now measures deadlines against your **estimates scaled by your personal rate** — the median of estimate-vs-actual over your own finished work, derived fresh from history and never stored — with the planned-blocks proxy as fallback, and an unsized task borrowing the sum of its pieces' estimates. **The detail overlay & pieces in the list** (v28.5–v28.8): task details live in a sliding right-side overlay — content dims behind it, click-away closes, saving is explicit (lit Save button, “Saved ✓” flash, a Save/Discard/Stay guard on every exit over unsaved work); a piece's title opens its own panel with a breadcrumb back to the parent; pieces also show as indented rows in the category list with right-click → “Add a piece” (title pre-selected for naming); and estimates come from a ladder dropdown that speaks hours (“1h 30m”, “12h”, capped at 16h — past that, break it into pieces); and since v28.9 a piece with its own date is **promoted** — it counts as its own line of work and its minutes leave the parent, so the app believes exactly what you entered, once. Design records: `docs/design-addendum-subtasks.md` §L–§N and `docs/design-addendum-detail-panel.md`. Daily **and weekly**
planner with live focus tracking (focus / break / **distracted**),
**drag-to-resize** blocks, **blocks that say what they are** (labels, task
blocks, spontaneous blocks — with task notes and column-flowed text), tasks &
deadlines, folders, special days, and week/month reviews. **379 tests across
six QTest suites** — domain (158), headless UI regression (95), NLP +
provider + chat (70), model/view (22), auth (19) and live end-to-end (15),
all green at v29.1.0.
That is QTest's own total, which counts each class's
`initTestCase`/`cleanupTestCase` alongside its test functions; counting test
*functions* alone gives **367**, and `ctest` answers **6**, because it counts
suites. Three conventions, one fact, and the reason a number here has drifted
before — `tests/README.md` carries the command for each, which is the only
honest way to quote one. Login is handled by a small self-hosted server (`ticktimer-server`) you run
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

- **Plan your day** on a 30-minute-slot calendar (6 AM–midnight, with the
  **visible hours yours to choose** in ⚙ Settings) — click a free slot, then
  pick an activity, pick one of your **open tasks**, or just
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
- **"Needs a block"** — the app *notices* when urgent or due-soon work has
  no time set aside (a block only counts on or before the deadline, and
  never in the past). A **gated review** holds the day's numbers until
  you've looked; **"Find time"** shows each day's free space and places a
  block in one click; "Not today" defers until *your* chosen hour, and a
  task dodged too often is asked for a **decision**, not shouted at.
- **Catch-up** — when a planned block *doesn't happen* (overslept, priorities
  shifted, forgot to track), the app notices: a quiet **chip** on the glance
  panel (amber at the morning/evening moments, faded while snoozed — never a
  nag) opens a drawer of every unresolved block from the last **3 days**
  (yours to widen in ⚙), each with a **pre-filled proposal** — "Move →
  tomorrow 09:00", a split across free fragments, a shorter slot, or an
  honest *"nothing fits before the deadline"* naming what's in the way.
  Nothing ever moves until you tap. *Done / Skip / Skip all* come with an
  **Undo receipt**, recently-resolved blocks stay one tap from **Bring
  back**, and the assistant's briefing reports the unresolved gap so it
  can't cheerfully invent a week that didn't happen.
- **Folders** organise your life areas in the rail; **special days** count
  down to birthdays, holidays, and vacations (yearly repeats supported).
- **Review your time** by life area — bars show **productive (focused) time
  only**, while break, **distracted**, and **unaccounted** time (planned
  blocks that elapsed untouched) accumulate in their own rows, with a
  day-split **pie chart**; zero rows stay hidden — across day, week, and
  month — every number derived live from raw tracked segments, never
  stored twice.
- **Pomodoro timer** for focused work cycles (25/5, long break every 4th) —
  now with **phase-end notifications** (a beep and a desktop toast from the
  tray, so a finished focus block finds you in any app), an optional
  **link to the tracked block** (*you pick the block, the Pomodoro picks
  the kind*: focus phases track focus, breaks track break, and pausing the
  Pomodoro records **distracted** time). Press ▶ during a planned block's
  hours and the Pomodoro **adopts it on the spot** — no need to start the
  block's timer first; it still never guesses between blocks (there's
  never more than one live) and never stops one, and a **mini timer**: a small always-on-top card (play/pause,
  phase, countdown) you can park over any app and drag anywhere.
- **The tracked block says so** — while you're tracking a block, it wears
  a live badge (`● Focusing · 7:12`) in the state's colour, ticking every
  second — and the Pomodoro page narrates its link in one honest sentence
  ("Driving 'Study PHY335' — recording focus", or exactly which step it's
  waiting for).
- **Repeat, for real** — tasks and planned blocks both recur. Complete a
  repeating task and its next occurrence appears (due date advanced, rule
  carried forward); give a block a repeat rule (its dialog → "Repeats")
  and the plan re-creates itself when its day passes — never backfilling
  days you missed, never colliding with existing blocks (occupied dates
  are skipped). Each past occurrence keeps its own tracked history.
- **Blocks end on time** — when a tracked block's planned window closes,
  tracking stops itself (the last interval committed honestly), a chime +
  toast says so, and if the Pomodoro was driving it, the Pomodoro pauses —
  ready to adopt your next block at ▶. Notifications play real chimes on every
  Windows build (embedded sounds via Qt Multimedia when present, or
  Windows' own audio API when not) and pop as TickTimer's own on-top
  cards — no OS notification pipeline to silence them.
- **Your plan speaks up** — the moment a planned block's start time
  arrives, TickTimer beeps and shows a desktop toast naming the block and
  its hours ("Study GTI350 · 9:00 AM – 10:30 AM"). Quiet by design: no
  toast for blocks you create mid-flight, none for a block you're already
  tracking, and a slept-through morning wakes to silence instead of ten
  stale pop-ups. Toggle in ⚙ Settings.
- **⚙ Settings** — choose the agenda's hours (a *display* window: blocks
  outside it always stretch the view back into sight, never hide) and
  whether your week starts **Monday or Sunday** — the week agenda, its
  totals, the "Week of …" label, and the month grid all follow together.
  Preferences live in `QSettings` on this machine; they never touch or
  sync your planner data.
- **The header knows who you are** — *Welcome, \<username\>* whenever a
  session is signed in, so two accounts on one machine never blur.
- **Sync between devices — automatic** — your edits push themselves a few
  seconds after you stop making them (and a Sync button remains for
  pulling / resolving). Conflicts are never resolved silently: the button
  turns ⚠ and a human picks the winner, with revision checks so two
  devices can never silently overwrite each other.
  See [docs/design-addendum-sync.md](docs/design-addendum-sync.md).
- **Your own accounts, your own server** — a login gate backed by
  `ticktimer-server`, a small program you run on your laptop (a Raspberry Pi
  later). Passwords are salted and stretched, never stored in plaintext; no
  identity provider, no cloud dependency. See [docs/SERVER.md](docs/SERVER.md).
- **Share & compare** — grant someone read access to your planner (one
  direction, revocable any time) and see your day next to theirs: two full
  agendas side by side — **each column named with its real account, pinned
  above the scroll** so the names can never scroll out of sight — plus
  focus, break, distracted, total, and a gentle who's-ahead headline.
  Permissions are enforced by the server; the comparison runs entirely on
  your device.
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

### Pick your AI (v24)

The quick-add AI fallback works with **any provider**: Anthropic, OpenAI,
Groq, a **local Ollama** (free, private, no key), or any custom endpoint
speaking either the Anthropic or the OpenAI-compatible dialect. One dropdown
in Settings; keys and models are remembered per provider, and a **Test**
button (v25.1) fires one tiny request at whatever is on screen — unsaved
edits included — and reports ✓ or the provider-aware error inline. The same
setup powers the **Assistant** page. **Local reasoning models** (Qwen3,
DeepSeek-R1, …) work since v25.2: their private `<think>` deliberation never
reaches the chat, and replies that arrive with everything in a `reasoning`
field are recovered instead of discarded. **Setup guide: [docs/AI.md](docs/AI.md)**
(providers, keys, Ollama, custom endpoints, what gets sent, every error
decoded); design reasoning:
[docs/design-addendum-provider.md](docs/design-addendum-provider.md).

### The Assistant (v25–v26)

A chat page that sees today's plan, tracked time, and tasks — rebuilt
fresh every turn. Since v29.2 it can propose **exactly one** kind of change:
moving a block you already missed, to one of the slots the app itself
computed — it never invents a time, and nothing happens until you tap Apply
on the card. Everything else it still cannot touch, by contract and by test;
the "What can it see?" button shows the exact context sent. Pick
its voice in Settings — Calm, Brief, Coach, or your own line of
instructions — with the safety rules locked above every style, and give it
a fallback seat that answers only when the primary is *unreachable*, never
to paper over a wrong key. Local reasoning models (Qwen3, DeepSeek-R1)
work: their private `<think>` deliberation never reaches the chat. The
whole guide, including what leaves your machine and what it costs:
[docs/AI.md](docs/AI.md).

### Break it into pieces (v28.3, grown v28.5)

Open any task and add **pieces** — "read the spec", "write section 1" —
tick them off, and the Upcoming card shows "☑ 2/5". Pieces stay off the
main lists (the task represents the work), but a piece given its own due
date appears on that calendar day. Finishing 5/5 never auto-completes the
task: the tick is yours. Same panel, give the task a **size** — an
estimate from a dropdown that speaks hours ("1h 30m", "12h" — 15 minutes
to 16 hours, and past that the app's answer is: break it into pieces) and
a "Fits short gaps" switch for things you can chip at in 15-minute holes.

Since v28.7, pieces live **in the list itself**, TickTick-style:
right-click any task → "Add a piece" creates one and opens it with the
title ready to type over, and pieces show as indented rows under their
parent — tick, date-chip, archive, and click-to-open all work right
there. The parent's own panel keeps its checklist for quick ticks.

Since v28.5, **a piece's title is a door**: click it to open the piece's
own panel — set its date, time, and size there, with a "‹ back to parent"
breadcrumb up top. The checkbox still just ticks. A piece with a date and
an estimate is real scheduled work: it shows a quiet "Aug 8 · 45 min"
chip in the checklist, appears on its day, and can earn its own planned
block — "Chapter 3" under "Study for finals", planned as its own session.

And since v28.6 all of this lives in a **sliding overlay panel**, not a
popup: click a task and the panel slides in over the right side while
the rest of the app dims behind it — click anywhere outside (or Esc) to
close, with a Save/Discard/Stay guard if you have unsaved work; clicking
a piece just swaps what the panel shows. Saving is **explicit, with feedback** — the Save button lights up
when you have unsaved changes, flashes "Saved ✓" when they land, and any
attempt to leave unsaved work behind asks first (Save / Discard / Stay —
Enter always saves, never discards). Design reasoning:
[docs/design-addendum-subtasks.md](docs/design-addendum-subtasks.md) and
[docs/design-addendum-detail-panel.md](docs/design-addendum-detail-panel.md).

### Deadlines with a time (v22)

A task's deadline can now carry a clock: **"Lab 4, Aug 8, 23:59"**. The time is
optional everywhere — an all-day deadline is still just a date, and every task
you already have keeps working untouched.

- Set it in the task detail panel, the due-date picker, or straight from
  quick-add: `lab 4 friday 17:00`, `pay rent 28th 9am`, `call clinic at 5pm`.
- All-day tasks are due at the **end** of their day, so nothing goes red at
  one minute past midnight.
- A timed task that lapses turns overdue **that minute**, not the next day —
  the "needs a block" card notices at 09:01, not at midnight.
- Same-day tasks sort by clock, earliest first, all-day last.

See `docs/design-addendum-deadline-time.md` for the full design, including why
this is two fields rather than one `QDateTime`.

### The focused glance panel (v22)

The "needs a block" review now asks for **one decision at a time**: the gate
shows the single top-ranked task as a card — accent rail, due line, two
actions — with the rest counted and one click away. After review, two compact
chips ("N need a block", "N put off") open a **slide-over panel** with the
full lists, so the glance column never jumps. Deadlines can carry a time, and
a task due today at 09:00 turns overdue at 09:01 — not at midnight.

### The window remembers (v23)

Close TickTimer where you like it and it opens there again — size, position,
and maximized state, plus whether you had the sidebar folded (`Ctrl+B`). All
per-machine: a laptop and a desktop are allowed to disagree about window size,
and they do.

Restoring is guarded. If the window was last closed on a monitor you've since
unplugged, TickTimer notices the remembered rectangle is no longer on any
attached screen and opens centred at its default size instead — rather than
faithfully restoring to coordinates that no longer exist, which is what most
apps do and is why they sometimes appear not to start at all.

See `docs/design-addendum-window-memory.md`, including why the geometry is
stored as Qt's opaque blob rather than four numbers.

## Built with

- **C++17** and **Qt 6 Widgets** — no dependencies beyond Qt itself; all
  charts are custom-painted.
- **CMake** builds; **QTest** suites: six of them, 379 tests (green at
  v29.1.0 — `tests/README.md` explains that figure and how to re-derive it), all
  headless (real widgets on Qt's offscreen platform — no display needed, so
  they run the same on your laptop and in CI).
- Storage: versioned **JSON** (**format v13**), migrating to **SQLite** as data grows.
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
  Plans · [design doc](docs/design-doc.md) with a design addendum per
  feature arc (thirty-five and counting — the doc's §3 keeps the full
  index) ·
  a [reading guide](docs/READING_GUIDE.md) to the codebase ·
  a symptom-indexed [troubleshooting log](docs/TROUBLESHOOTING.md) ·
  the [force recipes](docs/TESTING.md) — **Ctrl+Shift+D** opens a debug
  panel that reaches every v28 seam by hand (v28.10) ·
  a [question bank](docs/QUESTION_BANK.md) for self-testing.

## Getting started

> **New here, or setting up a fresh machine?** Read
> **[docs/SETUP.md](docs/SETUP.md)** — one page from unzip to running app,
> including the AI configuration and the mood-privacy rule. The sections
> below are the reference detail behind it.

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

Run the test suite (six suites: domain, model/view, UI, NLP/provider/chat, auth, and live end-to-end):

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
- [x] Drag-and-drop into folders (categories onto folders, in the Activities rail)
- [x] Plan a task onto the agenda (task blocks + the block-label link)
- [x] "Needs a block" — surface unscheduled urgent work: gated review,
      dismissal clocks, escalation, one-click placement
- [x] Catch-up — blocks that didn't happen: derived verdicts, a ranked
      reschedule ladder (propose, never move), chip + drawer, undo &
      bring-back, bulk skip, briefing honesty
- [x] Per-account local data + one-time migration of existing planner
- [x] Remember window & sidebar state (QSettings)
- [ ] SQLite storage
- [x] Android build (`docs/ANDROID.md`; the CMake packaging block is live)
- [x] Cross-device sync
- [x] Share & compare planners
- [x] Update notices (networked arc complete)
- [x] Task priority + archive + honest-tracking editor (daily-driver pass)
- [x] AI provider layer — vendor as a dropdown (cloud or local Ollama)

## License

To be decided.
