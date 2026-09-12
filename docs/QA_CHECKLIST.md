# Manual QA — TickTimer

*The living checklist. **Part 1 is permanent** and is re-ticked before every
release. **Part 2 is rewritten each release** and deleted once that release
ships — it covers only the surface that is new, because that is the only part
where the automated suites have nothing to say.*

*This replaces `QA_CHECKLIST_v28.3.md`, `_v29.2.md` and `_v30.0.md`, which were
three single-use scripts for three particular slices. Everything in Part 1 was
mined out of them; their release-specific halves are in git history.*

**Ticking:** copy the file, or just tick in place and `git checkout` it
afterwards. Tick as you go, top to bottom — each step sets up the next.

When something is off, note **where + what you expected + what you saw** (a
screenshot if it is a looks thing), and put a one-line entry in
`docs/BACKLOG.md` under Bugs. The checklist is not the bug list.

---

# Part 1 — The standing pass *(every release)*

## A. Launch the build you just made

- [ ] **Build it.** `tools\deploy-windows.bat` — or Qt Creator, server first,
      then the app.
      - **Neither of those touches the installed copy.** If you normally open
        TickTimer from the Start Menu, that is
        `%LOCALAPPDATA%\Programs\TickTimer\ticktimer.exe`, and it is still
        whatever you last installed. Run `installer\ticktimer.iss` through Inno
        Setup, or launch `dist\TickTimer\ticktimer.exe` directly.
- [ ] **Read the stamp before you believe anything.** *Help → About* must show
      the version you just built.
      - **The tests do not save you here.** On 2026-08-21 all six suites were
        green against a `build-release` whose two exes still stamped **30.4.4**:
        the bump was committed without a re-configure, so the binaries lagged
        their own sources by one release — and the release they lagged by was
        the data-loss fix. `test_domain::installerVersionMatchesTheHeader`
        guards that seam, and a *stale* `test_domain.exe` disarms it silently,
        because it compares a version compiled INTO itself against
        `ticktimer.iss` read off disk: two old numbers agree. **Build, then read
        the stamp. Never read a green run as proof of which exe ran.**
- [ ] **Run the suites once.** `tools\run-tests.bat`, **from PowerShell or the
      Qt prompt — not Git Bash**, where the output vanishes.
      - Expect all six green. If not, stop and keep `test-results.txt`.

## B. Protect the real data first

- [ ] Close the app and copy `data-<you>.json` somewhere safe, from
      `%APPDATA%\TickTimer\TickTimer\`.
- [ ] **Mind the double folder, and check the file before you trust it.** The
      live planner is nested one level deeper than looks right —
      `TickTimer\TickTimer\`, not `TickTimer\`. v22.7 gave the app an
      organization name, which silently moved `QStandardPaths::AppDataLocation`
      down a level, and `JsonStore::migrateLegacyData` **copies** rather than
      moves. So the parent folder still holds a same-named `data-<you>.json`
      frozen at the moment of that migration.
      - The only way to tell them apart is the `"version"` line near the end of
        the file. **If it is not the current format number, you have the decoy**
        and every read you do will lie to you. (A QA pass has already been lost
        to this once — `SESSION_NOTES.md`, "the checklist pointed at a decoy
        planner".)

## C. First launch after an upgrade

- [ ] App starts, no errors, About shows the new version.
- [ ] Every category, task, event, activity and history entry is present and
      unchanged — an older file is upgraded silently, never migrated by hand.
- [ ] Close and reopen: still all there (the upgraded file re-loads).

## D. Persistence, sync and the installer

- [ ] Full restart — everything the release touched is still there.
- [ ] Sync between two devices: the new fields travel intact.
- [ ] **Sync merges before it asks.** Edit the same row on both sides, sync, and
      confirm you are asked exactly once about exactly that row — not about
      everything, and with no deletion resurrected.
- [ ] Build the installer, install it, run the installed copy: right version,
      your data present, spot-check the release's new surface *there* too.

## E. Two accounts

- [ ] Log out, log in as a different account (register a throwaway if needed).
      - ✅ Per-user files appear side by side in `%APPDATA%\TickTimer\TickTimer\`
        — `data-<other>.json`, `memory-<other>.md`, `base-data-<other>.json`.
      - ✅ Settings → Memory is empty for the new account.
      - ✅ Log back in as yourself: your entries are exactly as you left them.
      - ❌ If the second account can see the first one's data or memory, stop
        and report — every per-user path is derived from the username the same
        way, so one leaking means all of them do.

## F. The phones *(whenever the release touches shared UI)*

- [ ] The Android APK's stamp matches, checked **before** signing:
      `aapt2 dump badging <apk> | grep versionName`.
- [ ] No page pans sideways on a phone: `horizontalScrollBar()->maximum() == 0`,
      not a size hint — a `QScrollArea` voids its page's width budget.
- [ ] Every glyph the release added actually draws on the device. U+27F3 (⟳)
      drew as tofu on Android for twelve versions across six surfaces because
      nobody had looked at a phone. Reuse a codepoint the app already draws.
- [ ] `/app/` (the WebAssembly build) has been redeployed, not just
      `server/version.json`. Nothing hard-fails on this seam.

## G. Leave no debug state behind

- [ ] `Ctrl+Shift+D` → **Back to real time**, and untick **All providers down
      this run**.
      - A debug state that survives a restart is a support ticket.

## H. The judgment pass *(nothing but your taste)*

The whole reason a manual pass exists — no test can answer these.

- [ ] Does the new thing feel *cheap* to use? (Can you do it without thinking?)
- [ ] Is the new state obviously distinguishable from the old one at a glance?
- [ ] Does it help you triage, or add noise?
- [ ] **Anything you kept reaching for that isn't there?** Write those down —
      one line each in `docs/BACKLOG.md`. This question has produced more of
      this app's good features than any other line in any document here.

---

# Part 2 — This release *(31.2.0 — move & swap, the now-line, the Undo bar, the wheel guard; and the v31 arcs it shipped on: schedules, overlaps, archiving, ordering)*

*Four addenda shipped in this arc and none of them has had a real-data pass.
Rewrite this whole part for the next release.*

## 1. Schedules — the Activity grew up *(`design-addendum-schedules.md`)*

- [ ] Rename an activity; add a description. Both survive a restart.
- [ ] Give an activity a date and a reminder; it appears where the addendum
      says it should and nowhere else.
- [ ] A schedule that repeats stops repeating when it should — check the day
      *after* the last occurrence, which is where off-by-one lives.
- [ ] The repeat chip reads correctly and carries **no glyph** — its text comes
      from `repeatChip()` in `Task.h`, the single place it is allowed to live.

## 2. Up to three blocks in one slot *(`design-addendum-overlapping-blocks.md`)*

- [ ] Two rules naming the same day and time both land, and both are **visible
      and distinguishable** in the agenda — the original report was that one
      silently won.
- [ ] A fourth block in the same slot is refused, with a reason you can read.
- [ ] **Free time is not double-counted.** With three concurrent blocks, the
      affordability verdict must not report less free time than one block
      alone would. This is the v31.3 regression the addendum was written
      about: `Affordability` summed block durations on the strength of a
      no-overlap invariant that this feature removed, and it under-reported
      with no error.
- [ ] The four `AgendaWidget` call sites that want "nothing is there" still ask
      `isFree`, and only the capacity question asks `hasRoomFor`.

## 2b. The format floor *(`design-addendum-format-floor.md`)*

The unit tests pin the mechanism; these two are the wiring, which they cannot
reach.

- [ ] Copy your planner aside, edit the copy's final `"version"` to one above
      what this build writes, put it back, and launch. The app must refuse with
      a message naming **both** version numbers and must not open. Restore your
      real file afterwards.
- [ ] Check the file's timestamp after that refusal: it must be **unchanged**.
      A refusal that still rewrites the file is the whole bug, surviving.
- [ ] Then launch normally and confirm the app opens as usual — the floor must
      not fire on an equal or older format.

## 3. Retiring a whole semester *(`design-addendum-folder-archiving.md`)*

- [ ] Archive a folder: its tasks go with it, and the rail no longer shows it.
- [ ] Un-archive it: everything comes back in the state it left in.
- [ ] The delete button on a folder that cannot be deleted now **says why**
      instead of doing nothing. That was the whole second half of the request.

## 4. Ordering by hand *(`design-addendum-manual-ordering.md`)*

- [ ] Click-hold-drag a task into a new position; it stays there after a
      restart **and** after a sync (the order is stored data, not a preference).
- [ ] On the phone: a **hold** starts the drag, and the page does not scroll
      underneath the row while it moves.
- [ ] After a drag ends, the page can still be flicked — an unbalanced
      `ungrabGesture` leaves a page that can never scroll again.
- [ ] Dragging from the grip does not open the row when you let go. The view
      must swallow the release as well as the press; the band is
      `reorder::gripWidth()` and it is 0 on a phone.
- [ ] Two drops in a row land in two different places. `dropIndicatorPosition()`
      is state, not a query — a stale one sends every drop to the last spot.

---

## 5. Move and swap a block, the now-line, the Undo bar, the wheel guard *(31.2.0 — `design-addendum-move-and-swap.md`)*

*The automated suites cover the rules and the signals. What they cannot cover
is a finger on glass and a wheel under a hand, so those are the steps here.*

**Desktop — the drag**

- [ ] Day view: drag a block to a free slot. It lands where you dropped it,
      keeping its length, and the block does not jump its top edge to the
      pointer.
- [ ] Week view: drag a block onto another day. It lands on the day under the
      pointer, not on the day it started from.
- [ ] Drop a block ON another block: they trade start times, each keeping its
      own length.
- [ ] A click that does not move still OPENS the block.
- [ ] Refusals show a red outline and a sentence under the pointer, and nothing
      moves: into the past; onto a slot that already holds three; a block that
      has tracked time; a block being timed right now.

**Desktop — a block whose time has passed (§M.11)**

- [ ] Drag it to a later free time. A copy lands there, and the ORIGINAL stays
      at its old time, faded, with "moved" on its time line.
- [ ] The catch-up card still knows it was missed — it is not silently tidied
      away.
- [ ] The old time's alarm does NOT chime; the new one does.

**Undo (§M.12)**

- [ ] After every move, swap and reschedule, a dark bar appears in the
      bottom-right of the TickTimer WINDOW (not the screen's corner, and not
      the reminders' white card).
- [ ] Undo puts it back. Resize the window: the bar stays in the corner.
- [ ] Left alone it fades after about ten seconds.
- [ ] Two drags in a row: the first bar's Undo does nothing, the second works.
- [ ] An undo that can no longer happen says why in the same bar.

**The red now-line (F19)**

- [ ] Today shows a red line at the current time, in day AND week view; other
      days show none.
- [ ] `Ctrl+Shift+D` → move the fake clock: the line moves with it.

**The wheel guard (B8)**

- [ ] Open a repeating block. Hover the **Repeats** dropdown WITHOUT clicking
      and scroll: the dropdown does not change, the recurrence survives, and
      the dialog scrolls instead. *This is the bug that lost five schedules.*
- [ ] Click the dropdown OPEN and scroll inside the list: still works.
- [ ] Settings, Activity editor, task details: hover dropdowns, number boxes
      and time boxes and scroll. The page scrolls; no value changes.
- [ ] Click INTO a time or number box, then scroll over it: it nudges the
      value, because you asked for it by clicking in.

**The phone — with a real finger (§M.8)**

*`adb` gestures are not evidence here: a synthesized swipe is not a thumb.
Every line below is a hand on the device.*

- [ ] Hold a block (about half a second): the **Open / Move or swap…** menu
      appears. A hold that MOVES scrolls the day instead, and opens nothing.
- [ ] Tap a block: it opens, as before.
- [ ] **Move or swap…**: the banner appears and the block is outlined.
- [ ] ONE tap on a free slot puts it there (not two, as planning needs).
- [ ] One tap on another block swaps them; a tap on the outlined block cancels.
- [ ] Use ‹ › while holding a block: it can land on another day.
- [ ] The Undo bar appears inside the app, clear of the capture **+** button
      and the bottom bar, and Undo works.
- [ ] A refused target keeps the block in hand and explains in the banner.

**Recurrence, overnight**

- [ ] Move ONE occurrence of a weekly rule to another day. Next morning (or
      with the debug panel's clock), the date it left is still empty — the rule
      has not re-made it — and the moved one is still where you put it.
- [ ] Edit the rule's time afterwards: the moved occurrence stays put; the
      untouched ones follow the rule.

## Known and expected — not bugs

- **Memory does not sync.** It stays on this machine. That is the trade the
  sidecar made in exchange for being a file you can open and fix by hand.
- **Memory is not private to your machine either.** It goes to your AI provider
  inside the prompt on every Assistant turn, exactly like the briefing
  (`docs/AI.md` §6). If that matters, use Ollama — or do not write it down.
- **The assistant cannot write memory.** Asking it to remember something will
  not change the file; you write it.
- **Nudges and the morning check-in never see memory.** Chat only.
- **A split cannot be undone from the UI** — only a move can.

## If you send a report

Three things, and the third is the one people skip because it is the one that
usually contains the answer:

1. The exact question you asked.
2. The full reply.
3. The **"What can it see?"** text from that same moment.

Plus `test-results.txt`, and — for anything touching stored data — the actual
`data-<you>.json` or `memory-<you>.md`. **The files are the evidence; a
description of them is not.**
