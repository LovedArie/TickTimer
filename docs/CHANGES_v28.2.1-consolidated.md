# TickTimer v28.2.1 — consolidated tree (read this one first)

**This is the whole project, not a patch.** Delete or archive the six
incremental drops from this session; everything they contained is here,
in one folder, at one version number.

## Why consolidated

Applying a chain of six zips by hand is precisely the process that lost
v27 — a drop that was built, downloaded, and never fully applied, then
described as shipped in three documents for weeks. One folder removes the
sequencing step entirely. There is nothing to get in the wrong order.

## First thing you do

```sh
grep VERSION_STRING include/Version.h        # must print 28.2.1
```

Then **`docs/SETUP.md`** — one page, unzip to running app.

## What this tree contains that your old one didn't

**The v26.8 documentation audit** — two real bugs (Version.h four
releases stale; the installer five, which broke Inno's upgrade
detection), eleven diagrams rendered for the first time, six Mermaid
diagrams converted, the new `app_architecture` overview, and four
documents brought back in line with the code.

**v28.0 — affordability.** A verdict per deadlined task, computed from
your own plan. A TIGHT pill on Upcoming; a heads-up toast when a task
*turns* Tight, under strict manners (change-of-verdict, quiet hours, a
cap of 3, dismissal respect). No model in the loop.

**v28.1 — the model phrases it.** The assistant writes the heads-up in
your persona, with the plain C++ sentence as the guaranteed fallback. The
chat briefing gained DEADLINE PRESSURE, so "can I go out tonight?" is
answerable from data.

**v28.2 — the morning check-in.** Mood (rough/okay/good + a private
note), format v12, 14-day retention trimmed by the domain. A toast on
computably heavy mornings only; one tap to answer; no model in the loop.
Mood reaches the assistant only when every chat seat is local.

**The v27 reconciliation.** Subtasks were designed and built but never
landed. The docs now say so. The design record is preserved for a fresh
re-land — **do not apply the old v27 zip**; it predates all of the above
and would roll it back.

## Bookkeeping, settled

- **Format v12.** v11 = catch-up (by reality), 12 = mood, subtasks take
  13+ when they land.
- **Version 28.2.1** in `Version.h`, `ticktimer.iss`, and both `.rc`
  files (those derive it — they never drift).
- A `.gitignore` is included. `git init` in this folder is a good next
  five seconds.

## Honesty note, unchanged and important

The v28 code was written in an environment without Qt. Every file was
balance-checked and cross-checked against the real headers, and the
version guard was compile-proven — but **the suite has not been
executed**. Run `ctest` before trusting it. If the build breaks, send the
first error: it will be a missing include or a signature drift, and it is
a minutes-fix, not a redesign.

## Next

Per `design-addendum-assistant.md` §N: **v29 — tool use** (the confirm
loop; intake first), or the **subtasks re-land**. v29's intake would
genuinely prefer subtasks to exist first.
