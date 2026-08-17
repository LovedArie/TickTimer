# TickTimer v28.2.0 — the morning check-in, part 1: the domain

Apply ON TOP of v28.1: unzip over the project root, then verify:

    grep VERSION_STRING include/Version.h        # must say 28.2.0

Then:

    cmake --build build
    ctest --test-dir build        # domain suite: +5 cases

**Honesty note:** built without Qt in the session container —
balance-checked and API-cross-checked, not executed. Run ctest first.

## What it does (and doesn't, yet)
The DOMAIN half of the morning check-in, cut on the catch-up precedent
and the v27 lesson (land whole, or don't):
- **Mood** — rough / okay / good, plus an optional note that is yours
  alone (it never enters an AI briefing; a test pins that). One per day;
  answering again replaces. **Format v12** — old files load fine and
  simply start with no history.
- **14-day retention**, trimmed by the domain on the midnight knock.
- **The check-in gate** — once, in the morning (06:00–11:00), and only
  when the day is computably heavy (≥5h planned OR ≥2 tasks due within
  2 days). A quiet Tuesday does not get a wellness interview.
- The assistant's briefing gains a coarse **MOOD** line when history
  exists — so the chat can already use it once you record moods.

**Nothing asks you anything yet.** Part 2 wires the surfaces: the toast
whose button opens the chat with the check-in waiting, the mood picker,
and the "local, always" seat rule for the conversation itself.

## Bookkeeping
Format v12 closes the numbering thread for good: 11 = catch-up (by
reality), 12 = mood, the re-landed subtasks will take 13+.
