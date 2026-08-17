# Manual QA — v28.3.x (subtasks & sizing)

The suites already verify the *logic* (~320 green tests). This pass
verifies what tests can't: that it works **on your real screen, with
your real data, and feels right**. Work top to bottom; tick as you go.
When something is off, note *where + what you expected + what you saw*
(screenshot if it's a looks thing) — that list becomes the next patch.

> **One known gap, so you don't hunt for it:** pieces can't be given
> their own deadline from the UI yet. The domain supports dated pieces
> (they'd appear on the planner's day strip), but the checklist row only
> takes a title, and an undated piece never appears anywhere clickable —
> so there's no door to a piece's own panel. Queued for the next polish
> patch. Everything below is testable today.

---

## A. First launch (migration sanity)

- [ ] App starts; no errors; title/about shows **28.9.1**
- [ ] All your existing categories, tasks, events, and history are
      present and unchanged (your v12 file was silently upgraded to v13)
- [ ] Close and reopen — still all there (the v13 file re-loads)

## B. The SIZE row (in any task's detail panel)

- [ ] Open any task — a **SIZE** row exists: a spinner showing
      **"No estimate"** and a **"Fits short gaps"** checkbox
- [ ] Spinner steps in 15-minute jumps and shows a " min" suffix
- [ ] Set 90 min + tick "Fits short gaps" → Save → reopen: both held
- [ ] Spin back down to "No estimate" → Save → reopen: held
- [ ] Hover the checkbox — tooltip reads clearly
- [ ] *Looks:* row spacing, caption style, and spinner width match the
      rest of the panel — nothing cramped or misaligned

## C. Creating pieces (the checklist)

- [ ] Open a task — a **PIECES** section exists with an add-row
      ("Add a piece — e.g. \"read the spec\"")
- [ ] Type a piece, press **Enter** → it becomes a checkbox row and the
      add-row clears, ready for the next (capture should feel instant)
- [ ] Add 3–4 pieces in a row this way — the flow feels smooth
- [ ] Type a piece and click **Save without pressing Enter** → reopen:
      it was still created (typed text counts)
- [ ] Type a piece, press Enter, then click its **✕**, then Save →
      reopen: it was never created (a change of mind isn't a task)
- [ ] Enter on an empty/spaces-only add-row does nothing
- [ ] *Looks:* checkbox rows and ✕ buttons are comfortable click
      targets; the dialog's height stays reasonable with ~8 pieces

## D. Ticking pieces & the card chip

- [ ] Tick some pieces → Save → reopen: ticks held
- [ ] The task's card on **Upcoming** shows **"☑ 2/5"** in the grey
      subtitle line, and the numbers match the dialog
- [ ] Tick another piece → Save → the chip updates
- [ ] A task with **no** pieces shows **no** chip (never "0/0")
- [ ] Tick **every** piece → the parent is **NOT** auto-completed —
      its checkbox is yours alone (this is deliberate: the tick is the
      reward)
- [ ] Complete the parent → its pieces' ticks are untouched
- [ ] *Looks:* the "☑" glyph renders properly in your font; chip
      spacing reads naturally; shrink the window — long title + chip
      elide gracefully

## E. The ✕ (archive, never delete)

- [ ] ✕ an existing piece → row disappears; Save → reopen: gone from
      the checklist, and the chip's denominator dropped with it
- [ ] Open the **Archive** page → that piece is there
- [ ] Restore it from Archive → it's back in the parent's checklist

## F. Where pieces deliberately DON'T appear

- [ ] Pieces never appear as their own cards on **Upcoming** — only the
      parent does (a heavy week shouldn't look heavier than it is)
- [ ] Pieces never appear as rows on the **category (Activities)** page
- [ ] The category card's **count chip includes pieces** — a category
      with 1 parent + 3 pieces reads 4. Expected: that number guards
      deletion, and a guard that undercounts deletes data
- [ ] Try deleting that category → refused while the tasks live in it

## G. Cascades

- [ ] Archive a **parent** with pieces → parent and all pieces leave
      every list together
- [ ] ✕ one piece by hand first, then archive the parent, then
      **restore the parent** from Archive → the WHOLE checklist comes
      back, including the hand-archived piece ("bring Lab 4 back" means
      all of it; re-hiding one line is one click)
- [ ] If you use delete: delete a parent → pieces go with it; a planner
      block that pointed at it keeps its title as plain text

## H. Repeat carries size

- [ ] Give a repeating task an estimate + "fits short gaps" → complete
      it → the spawned next occurrence carries **both** (same job next
      week, same size)

## I. Every door to the detail panel (all four were rewired)

Open a task's details from each place; each should show SIZE + PIECES
and save correctly:

- [ ] From an **Upcoming** card
- [ ] From the **Activities / category** page
- [ ] From the **Planner** — the day strip's task rows
- [ ] From the **Planner** — anywhere else you normally open one
- [ ] In each: make a small edit (title or estimate), Save, confirm one
      smooth refresh (no flicker storm — that's the Batch working)

## J. Persistence, sync, and the installer

- [ ] Full restart: pieces, ticks, sizes, archived pieces — all held
- [ ] If you sync between devices: pieces and sizes travel intact
- [ ] Build the installer (deploy bat → Inno), install it, run the
      installed copy → 28.9.1, your data present, spot-check B–D there

## J2. Sizing intelligence (v28.4)

- [ ] Give a **deadlined** task an estimate → it gets a real verdict
      (TIGHT or fine) even with zero blocks planned — never "can't tell"
- [ ] An estimated task due **tomorrow** with work left shows TIGHT
- [ ] A parent with no estimate but **sized pieces** is treated as their
      sum (check the assistant's numbers for it)
- [ ] After you've finished a few estimated tasks with tracked time, the
      Tight wording starts saying "at your usual N×" — your own rate

## K. The judgment pass (nothing but your taste)

Tests can never answer these — this section is the whole reason for a
manual pass:

- [ ] Does checklist entry feel *cheap*? (type-Enter-type-Enter without
      thinking)
- [ ] Is "No estimate" obviously different from a real estimate at a
      glance?
- [ ] Does the chip help you triage Upcoming, or add noise?
- [ ] Anything you keep reaching for that isn't there? (e.g. wanting to
      give a piece a date — that's the known gap; wanting to reorder
      pieces; wanting the chip on category rows too.) **Write these
      down** — they're the shortlist for the polish patch.

## v28.5 — the piece's own panel (manual pass)

The new door and the loop, hand-tested — exec()-driven navigation can't
run under offscreen ctest, so this block is its verification:

- [ ] Open a task with pieces → checklist rows show checkbox + title
      (+ chip if dated/sized) + ✕; ticking the checkbox does NOT open
      anything
- [ ] Click a piece's title → panel closes, "Piece details" opens with
      "‹ parent title" up top; NO checklist section inside
- [ ] Give the piece a date + estimate, Save → parent's panel row now
      shows "MMM d · N min" chip; the piece appears on that day's
      planner strip and (per rule) in Needs a block
- [ ] Tick a piece, then click ANOTHER piece's title → come back: the
      tick was saved (the hop saves the sitting)
- [ ] Breadcrumb click → back on the parent, piece's edits saved
- [ ] Cancel in a piece's panel → edits discarded, you STAY out (no
      navigation on Cancel)
- [ ] Type a new piece line, don't save → its row has a plain label
      (no door); Save, reopen → now it's clickable
- [ ] Open a dated piece straight from the planner strip → breadcrumb
      present (runTaskDetail serves every entry point)

Feel questions:
- [ ] Does click-through → close → reopen feel acceptable, or does the
      modal hop annoy you in practice? (Calibrates the side-panel
      slice's priority.)
- [ ] Is the chip readable at a glance next to the ✕?

## v28.6 — the docked panel (manual pass)

The slide, the prompt, and the feel — the parts offscreen tests can't
judge:

- [ ] Click a task → panel slides in OVER the right side; the content
      behind DIMS and does not resize (v28.6.1 — the layout must not
      reflow)
- [ ] Click anywhere on the dimmed area → panel closes (clean form);
      with unsaved edits → the prompt, and Stay keeps everything
- [ ] Save button starts GREY; edit anything → it lights; retype the
      original → grey again (honest dirty)
- [ ] Save → "Saved ✓" flashes ~1.5 s; button grey; nothing closed
- [ ] Edit, then click a DIFFERENT task → prompt appears; check all
      three: Stay (nothing happens), Discard (swaps, old task
      unchanged), Save (swaps, edits landed)
- [ ] **Press Enter in the prompt → it SAVES** (never discards)
- [ ] Click a piece title → same panel swaps to "Piece details" +
      breadcrumb, no popup, no blink; crumb swaps back
- [ ] Type a new piece, Save → its title is instantly clickable
- [ ] Esc with unsaved edits → the prompt, not a silent close
- [ ] Rename the shown task from the list behind the panel (clean
      form) → panel updates to the new name

Feel questions:
- [ ] Is 440 px comfortable now? (You called 360 tight.)
- [ ] Is the dim level right — does the panel read as “in front”
      without the background becoming unreadable?
- [ ] v28.6.2: the panel is ONE uniform white — no color seam where
      the scrolling middle meets the header or the Save row
- [ ] Does the prompt frequency annoy you in real use, or does explicit
      save feel right? (You chose it — verify the choice.)


## v28.7 — pieces in the list (manual pass)

- [ ] Right-click a task → "Add a piece" → panel opens on a "New
      piece" with the title SELECTED; typing replaces it; Save
- [ ] The piece appears indented under its parent in the list; ticking
      its checkbox works AT the indent (no dead zone left of it)
- [ ] Give the piece a date → its chip shows in the row
- [ ] Right-click the piece → no "Add a piece" offered
- [ ] Parent with pieces due AFTER another task: the family stays
      together (pieces hug their parent, the other task above/below)
- [ ] Archive a piece (✕ in the row or checklist) → gone from both
      the list and the parent's checklist
- [ ] Upcoming page: still ONE row per parent (☑ chip), no piece rows

Feel:
- [ ] Is 24 px indent readable as "belongs to the row above"?
- [ ] Create-first: does a stray "New piece" left behind feel okay
      (it's one ✕ away), or should walk-away auto-discard it?


## v28.8 — the size dropdown (manual pass)

- [ ] SIZE is a dropdown; scan it: 15/30/45m, half-hour steps to 8h,
      whole hours to 16h, nothing past 16h
- [ ] A task saved at 720 in the spinbox era opens showing "12h"
- [ ] A task with an odd value (e.g. captured "~25m") opens showing
      "25m" at a sensible spot in the list — and Save without touching
      it changes nothing
- [ ] A piece sized 12h shows "12h" in its checklist chip
- [ ] v28.9.1: the open dropdown shows ~6 rows and SCROLLS (on real
      Windows — native popup styles can ignore the hint)
- [ ] Feel: is 16h the right ceiling for how you actually estimate?


## v28.9 — promotion (manual pass)

Build the FINALS case: parent 12h due Aug 14; three pieces 4h each,
dated Aug 10/12/13.

- [ ] The parent's affordability sentence now reasons from ~0h of its
      own (fully decomposed → proxy basis), each chapter from 4h
- [ ] Undate one chapter → the parent's number rises by 4h (an undated
      piece weighs on the parent again)
- [ ] Unsized parent + one dated 4h piece + one undated 1h piece →
      parent borrows only the 1h
- [ ] No doubled nudges for the same hours on a heavy day
- [ ] Feel: does the parent "shrinking" as you date pieces read as
      sensible, or surprising? (§O's rule in one glance:
      diagrams/piece_promotion.png)
