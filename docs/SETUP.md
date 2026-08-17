# SETUP — from this folder to a running TickTimer

*One page, start to finish. Deeper detail lives in `RUNNING.md` (launch
mechanics), `AI.md` (providers), `SERVER.md` (hosting), and
`TROUBLESHOOTING.md` (when something goes wrong). Read those when you hit
something; read this first.*

---

## 0. What you just unzipped

**v29.1.0 — a complete source tree**, not a patch. This is deliberate: the
previous six drops were incremental, and applying a chain by hand is
exactly how v27 half-landed. One folder, one number, nothing to sequence.

**Verify it landed before doing anything else:**

```sh
grep VERSION_STRING include/Version.h        # must match the version named above
# (this expectation is part of the post-ship sweep, and it is the part that
#  keeps rotting: frozen at 28.2.1 for six releases until the v28.8 docs
#  audit, then at 28.9.1 until the next one. Bumping Version.h means bumping
#  the line above with it — a check that lags the tree fails a GOOD unzip,
#  which is worse than no check at all.)
```

That line is the whole apply-check ritual. If it says anything else, the
unzip went somewhere unexpected — fix that before building, not after.

---

## 1. Prerequisites

| | |
|---|---|
| **Qt 6** (6.5+) | Widgets, Network, Test. Qt Multimedia is *optional* — chimes fall back to `winmm` on Windows, or a system beep |
| **CMake** 3.16+ | |
| **A C++17 compiler** | MSVC 2019+, GCC 9+, or Clang 10+ |
| **Ninja** *(optional)* | faster builds; any generator works |

The simplest route on Windows is the **Qt Online Installer** — it brings
Qt, a compiler kit, CMake and Qt Creator together. On Linux:
`sudo apt install qt6-base-dev qt6-multimedia-dev cmake ninja-build`.

---

## 2. Build

```sh
cmake -B build -G Ninja          # or omit -G for your default generator
cmake --build build
```

This produces **two programs**: `ticktimer` (the app) and
`ticktimer-server` (the login/sync backend).

In **Qt Creator**: *File → Open File or Project…* → `CMakeLists.txt` →
pick your kit → build. Use the target selector (bottom-left, above ▶) to
choose which program ▶ launches.

---

## 3. Run the tests first

On Windows, use the runner — it rebuilds what changed, puts Qt's DLLs on
PATH (a bare prompt dies with "Qt6Gui.dll was not found"; see
TROUBLESHOOTING), runs everything, and writes `test-results.txt`:

```
tools\run-tests.bat            :: all six suites
tools\run-tests.bat ui         :: just one, e.g. while debugging
```

(On a non-Windows shell with Qt on PATH, plain
`ctest --test-dir build --output-on-failure` still works.)

Six suites, all headless (Qt's offscreen platform — no display needed).
Do this *before* the first launch: a green suite means the tree is
internally consistent, and it takes seconds.

> Since v28.3, `tools/deploy-windows.bat` runs the suites itself as part
> of a deploy — plus an apply check up front (Version.h vs the installer
> script; a disagreement is the signature of a half-applied drop, and the
> bat refuses to build on one). `run-tests.bat` is the way to test
> *without* deploying.

> **If the build fails**, that is worth knowing about — these drops are
> written in an environment without Qt, so they are balance-checked and
> API-cross-checked but not compiled until they reach your machine. (The
> v28 arc has since compiled and run on the owner's machine — the earlier
> "never compiled" caveat did its job and retired. v28.3 starts the cycle
> again.) Send the first error; it is almost always a missing include or
> a signature drift, and it is a minutes-fix.

---

## 4. Launch

**The server must be running before the app logs in.** Two processes:

```sh
./build/ticktimer-server        # window 1 — leave it open
./build/ticktimer               # window 2
```

The server prints `TickTimer server listening on port 8080` when it is
alive. If the app says *"Can't reach the server,"* the server isn't
running — that is the answer 90% of the time.

> **Windows first-run:** running either `.exe` from a plain terminal can
> exit instantly with code `-1073741515` (missing Qt DLLs). Fix once with
> `windeployqt build\ticktimer.exe` from the Qt command prompt. Full
> details in `TROUBLESHOOTING.md`.

Your planner lives in a per-account JSON file — the app prints its exact
path on startup. **Format v12** as of this version.

---

## 5. Set up the AI (optional — everything works without it)

Nothing in TickTimer *requires* a model. Quick-add has a deterministic
parser, nudges have a C++ sentence, the check-in has no model at all. The
AI is an enhancement layer everywhere, by design.

**Settings → AI**, then pick one:

| option | what you need |
|---|---|
| **Anthropic / OpenAI / Groq** | an API key, pasted into Settings |
| **Ollama (local)** | `ollama serve` running on `localhost:11434` — no key, no cloud |
| **Custom endpoint** | LM Studio, OpenRouter, your own — base URL + dialect |

Two things worth setting while you're in there:

- **Assistant style** — Calm, Brief, Coach, or your own text. The safety
  rules sit locked above whatever you pick.
- **If unreachable, try** — a fallback seat. A local Ollama is the natural
  choice: it answers when the cloud is down, and *only* when it's
  unreachable (a wrong key still fails loudly, on the seat that rejected
  it).

### ⚠ If you want the assistant to see your mood

Your mood history reaches the assistant **only when every seat in your
chat route is local to this machine** — loopback addresses only. A LAN
Ollama on `192.168.x.x` counts as remote, on purpose: another machine is
still a wire your mood crossed.

So: to let the assistant reason about how your weeks feel, set **both**
the primary and the fallback to a local Ollama. Otherwise the mood line is
silently omitted and everything else works normally.

**Your mood *note* is never sent anywhere, under any configuration.** Only
the coarse rough/okay/good values, and only under the rule above.

---

## 6. What to expect in the first week

- **Quick-add:** type `lab 4 friday 17:00 urgent weekly #school` in
  Activities, or hit **Ctrl+N** anywhere for the capture overlay.
- **A TIGHT pill** appears on Upcoming cards when a deadline stops fitting
  your own plan.
- **A heads-up toast** when a task *turns* Tight — at most 3 a day, never
  between 22:00 and 08:00, never twice for the same verdict.
- **A morning check-in** only on computably heavy days (≥5h planned, or
  ≥2 deadlines within 2 days), once, 06:00–11:00.

If any of those fire too often or too rarely, the thresholds are all in
one place each: `afford::Rule` and `checkin::Rule`. They are
Settings-shaped on purpose but ship without a Settings page — knobs get
built when someone wants to turn one.

---

## 7. Optional: put it under version control

The tree has no `.git`. If you want one — and you should, since it makes
the whole apply-check problem disappear:

```sh
git init
git add .
git commit -m "TickTimer v28.9.1"
```

`GITHUB.md` covers pushing it somewhere. A `.gitignore` for
`build*/`, `dist/`, `installer/Output/` is the first thing to add.

---

## 8. Where to read next

| you want | read |
|---|---|
| the shape of the whole system | **`diagrams/app_architecture.png`** — start here |
| a guided path through the code | `READING_GUIDE.md` |
| why any particular thing is the way it is | `design-doc.md` + the matching `design-addendum-*.md` |
| what happened, session by session | `SESSION_NOTES.md` |
| to test yourself on it | `QUESTION_BANK.md` — 208 questions with answers |
| what's next | `design-addendum-assistant.md` §N |
