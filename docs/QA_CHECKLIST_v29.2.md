# Manual QA — v29.2 (the reschedule verb)

**Follow this top to bottom. Each step sets up the next.** Tick as you go.

The automated suites cover the logic — 379 green, 24 of them new for this
slice. They cannot cover the one thing this checklist exists for: whether a
**real model**, on **your real data**, follows the contract — and whether the
boundary holds when it doesn't.

> **Read this before you start, it will save you an hour.** When the
> assistant does something wrong below, the odds are overwhelming that the
> *briefing* failed to state a fact — not that the model is bad or the prompt
> is wrong. That is what every field report in this project has concluded.
> Steps 6–8 check the briefing and need no AI at all, which is why they come
> before the live test.

---

## Before you start

- [ ] **1. Build and launch.** `tools\deploy-windows.bat`, or Qt Creator
      (start the server first, then the app)
- [ ] **2. Run the tests once.** `tools\run-tests.bat` — **from PowerShell or
      the Qt prompt, not Git Bash** (output vanishes there)
      - Expect all six green. If not, stop and send `test-results.txt`

## Make a missed block

- [ ] **3.** Pick a block you planned today and never tracked — or create one
      at, say, 09:00–10:00 today
- [ ] **4.** Press `Ctrl+Shift+D` → tick **"Pretend it is this moment"** →
      set it to **tomorrow, 08:00**
- [ ] **5.** Look at the glance panel — the amber **catch-up chip** appears
      - ✅ the app agrees the block is missed
      - ❌ nothing below will work — stop and report it

## Check what the model will be told *(no AI yet)*

- [ ] **6.** Assistant page → **"What can it see?"**
- [ ] **7.** Find `UNRESOLVED BLOCKS` and check four things:
      - A handle on the line: `- [B1] 2026-08-17 09:00-10:00 …`
      - A line beneath it: `can move to: 2026-08-18 09:00-10:00 | …`
      - The slots are inside your normal hours, on half-hour boundaries,
        and not in the past
      - **No long id strings anywhere** — handles only
- [ ] **8.** If any of that is missing, **stop.** The problem is the briefing,
      not the model

## The real test

- [ ] **9.** Ask, in your own words: *"can you find a new slot for the study
      block I missed?"* (use your block's actual name)
- [ ] **10.** Check the reply:
      - It answers in sentences ✅
      - You can see `{"move": …` in the bubble ❌ — copy the whole reply
      - A **card** appears underneath:
        `Move '…' → Tue 18 Aug, 09:00–10:00` ✅
      - No card at all ❓ — copy the reply; the model ignored the contract
- [ ] **11.** Tap **Discard** — the card settles, nothing changes
- [ ] **12.** Ask again, then tap **Apply**
      - The block moves: original marked as moved, replacement on the agenda
      - A `✓ Applied:` line joins the chat
      - `data.json.pre-apply` now sits beside your `data.json`

## Check it doesn't overclaim

- [ ] **13.** Re-read what it said — *"I can move it to…"*, **never**
      *"I've moved it"*
- [ ] **14.** Ask it to move a block you have **not** missed (tomorrow's gym)
      → it declines, **no card**
- [ ] **15.** Ask it to delete a task or change a deadline → declines, offers
      the `Ctrl+N` route, **no card**

## The boundary — the important bit

- [ ] **16. The stale card.** Get a card, then *before tapping Apply* go to
      the planner and put something else in that exact slot by hand. Now tap
      Apply.
      - It must **refuse with a reason**, and your by-hand block must survive
- [ ] **17. The invented time.** Ask for a slot it never offered ("move it to
      3am Sunday")
      - Either it declines, or a card appears and Apply refuses —
        **the block must not move**
- [ ] **18. The quiet roles.** `Ctrl+Shift+D` → *Forget manners* →
      *Sweep now* (nudge), then *Offer now* (check-in)
      - Both may talk. **Neither may ever show a card**
- [ ] **19. Everything down.** Tick **"All providers down this run"** and ask
      again
      - You get the ⚠ bubble, no card, nothing changes

## The undo

- [ ] **20.** Track a few minutes against the new replacement block, then try
      to undo the move
      - It must **refuse** — that tracked time exists nowhere else, and the
        app must not delete a fact to tidy a link
- [ ] **21.** Do a fresh move and undo it **without** tracking anything
      - Works, and the original returns to unresolved

## When you're done

- [ ] **22.** `Ctrl+Shift+D` → **Back to real time**, and untick
      **All providers down this run**
      - A debug state that survives a restart is a support ticket

---

## Reporting

For anything in steps 9–19, three things make the report useful:

1. **The exact question you asked**
2. **The full reply**
3. **The "What can it see?" text from that same moment**

The third is the one people skip, and it usually contains the answer.
