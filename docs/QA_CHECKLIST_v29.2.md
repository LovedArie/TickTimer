# Manual QA — v29.2 (the reschedule verb)

The suites verify the *logic* — 379 green, including 24 new ones for this
slice. This pass verifies what tests cannot: that a **real model**, on your
**real data**, actually follows the contract, and that the boundary holds
when it doesn't. Work top to bottom; tick as you go. When something is off,
note *where + what you expected + what you saw*.

> **The one thing this slice has never done:** talked to a live model. Every
> proposal so far was composed by C++ or by a test. Section D is therefore
> the real point of this checklist — A–C exist so that when D misbehaves you
> already know which half is at fault.

> **Read this first, it will save you an hour:** when the assistant does
> something wrong here, the odds are overwhelming that the *briefing* failed
> to state a fact, not that the model is bad or the prompt is wrong. That is
> the pattern every field report in this project has produced. Section B is
> how you check the briefing, and it needs no model at all.

---

## A. Set the stage (no model needed)

- [ ] Build and launch: `tools\deploy-windows.bat`, or Qt Creator with the
      server started first
- [ ] Suites green before you start: `tools\run-tests.bat` — **run it from
      PowerShell or the Qt prompt**, not Git Bash (output vanishes there)
- [ ] You have at least one **missed block**: a planned block whose time has
      passed with no tracked focus, and no Done/Skip decision yet.
      Fastest route: `Ctrl+Shift+D` → **Pretend it is this moment** → set the
      clock to tomorrow morning, so today's untracked blocks become missed
- [ ] The catch-up chip appears on the glance panel (proof the domain agrees
      the block is missed — if it doesn't, nothing below will work)

## B. The context half — the model's inputs (still no model)

Assistant page → **What can it see?**

- [ ] An `UNRESOLVED BLOCKS` section is present
- [ ] Each block there carries a handle: `- [B1] 2026-08-17 09:00-10:00 …`
- [ ] Under it, a line reading `can move to: 2026-08-18 09:00-10:00 | …`
- [ ] **No UUIDs anywhere in the text** — handles only. This is the privacy
      promise; if a raw id appears, stop and report it
- [ ] The offered slots are *sane for your week*: inside your agenda hours,
      on the 30-minute grid, not in the past
- [ ] A block with genuinely nowhere to go shows **no** `can move to:` line
      at all (silence, not an empty header)

> If B fails, D cannot work, and the fix is in `DayBriefing` — not the
> prompt and not the model.

## C. The card, forced by hand (no model)

- [ ] `Ctrl+Shift+D` → **Inject sample proposal** still works for the v29.0
      intake card (proves the confirm loop is intact after this slice)
- [ ] Apply it; a `✓ Applied:` receipt joins the transcript
- [ ] `data.json.pre-apply` exists beside `data.json` afterwards

## D. The live model — the actual test

With a provider configured (Anthropic, OpenAI, Groq, or local Ollama):

- [ ] Ask plainly: **"can you find a new slot for the study block I missed?"**
      (use your real block's name)
- [ ] The reply is **prose** — a sentence or two explaining the suggestion
- [ ] **No JSON is visible in the bubble.** If you can see `{"move": …`, the
      scrub failed — copy the whole reply into a report
- [ ] A **proposal card** appears under the reply, reading
      `Move '<block>' → <day>, HH:MM–HH:MM`
- [ ] The card's slot is one of the ones from section B
- [ ] **Discard** → card settles, nothing changes, nothing enters the record
- [ ] Ask again, then **Apply** → the block moves: the original shows as
      moved, a replacement block sits in the new slot on the agenda
- [ ] The transcript gains a `✓ Applied:` receipt
- [ ] `data.json.pre-apply` was refreshed

### D2. What the model says about what it did

- [ ] It says something like *"I can move it to…"*, **never** *"I've moved
      it"* — it must not claim to have done what only your tap does
- [ ] Ask it to move a block that is **not** missed ("move tomorrow's gym
      block") → it declines in a sentence, and **no card appears**
- [ ] Ask it to do something else entirely ("delete my lab task",
      "change the deadline") → declines, offers the Ctrl+N route, no card

## E. The boundary under pressure

This is the section that matters if you ever wonder whether the confirm
loop is real.

- [ ] **The stale card.** Get a card, then — before tapping Apply — put
      something else in that slot by hand on the planner. Now tap Apply:
      it must **refuse with a reason**, and your by-hand block must survive
- [ ] **The invented time.** Ask for a specific slot the app did *not*
      offer ("move it to 3am Sunday"). Either it declines, or a card appears
      and Apply refuses it — but the block must not move
- [ ] **The other roles stay mute.** Force a nudge
      (`Ctrl+Shift+D` → Sweep now, after Forget manners) and a check-in
      (Offer now): both may *talk*, neither may ever show a proposal card
- [ ] **Every seat down.** Tick **All providers down this run**, ask again:
      you get the ⚠ bubble, no card, and nothing changes

## F. The inverse

- [ ] After an applied move, the original block reads as moved and the
      replacement exists
- [ ] Track a few minutes against the *replacement*, then try to undo the
      move (catch-up drawer / bring-back): it must **refuse** — that tracked
      time exists nowhere else, and the app must not delete it to tidy a link
- [ ] On a replacement with **no** tracked time, undo works and returns the
      original to unresolved

## G. Looks and feel

- [ ] The card sits under the sentences that explain it and reads clearly
      at your window size
- [ ] The Assistant page's subtitle now describes what it can propose, and
      is honest about the tap
- [ ] Nothing about the chat feels slower than before (the briefing grew by
      a few lines per missed block — it should be imperceptible)

---

## Reporting

For anything in D or E, the useful report is: **the exact question you
asked**, **the full reply**, and **the What-can-it-see text from the same
moment**. The third one is the piece people forget and the one that almost
always contains the answer.
