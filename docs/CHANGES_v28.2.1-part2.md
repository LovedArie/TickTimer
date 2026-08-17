# TickTimer v28.2.1 — the morning check-in, part 2: the surfaces

Apply ON TOP of v28.2.0 (part 1): unzip over the project root, then:

    grep VERSION_STRING include/Version.h        # must say 28.2.1

    cmake --build build
    ctest --test-dir build

**Honesty note:** built without Qt in the session container —
balance-checked and API-cross-checked, not executed. Run ctest first.

## What it does — the v28 arc is complete
On a computably heavy morning (≥5h planned or ≥2 near deadlines), once,
between 06:00 and 11:00, a quiet toast invites you: "Morning check-in —
today looks full. Got a second?" Tap **Check in** and the Assistant opens
with the question waiting. One more tap — Rough / Okay / Good — records
your mood and gets a short, specific acknowledgement. No typing, no model
call, whole exchange local-only.

## The important fix inside
Part 1 accidentally let the coarse mood history ride the chat briefing to
ANY configured seat, including cloud — against §E.4 ("mood never leaves
the machine"). Fixed with two walls: the MOOD line is now opt-in
(default off), and it is enabled only when EVERY seat in your chat route
is local (loopback only — a LAN machine counts as remote, on purpose).
Your note was never sent anywhere and still isn't; this fix is about the
coarse values.

## Why 28.2.1 and not a second 28.2.0
The apply-check ritual needs the number to change per drop — two drops
sharing one number would make a half-applied tree undetectable, which is
exactly how v27 was lost.
