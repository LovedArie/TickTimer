# v27 reconciled — started, never finished (docs only)

Apply by unzipping over the project root. **No code changed, no version
bump** (docs-session precedent). Then verify the apply:

    grep VERSION_STRING include/Version.h        # must say 28.0.0

That command is the point of this drop. The v27 mystery is closed: the
session ran, the zip was built, the apply step never finished — and
nothing checked. Every CHANGES file now ends with the check above.

## What this settles
- **v11 = catch-up, unambiguously.** The format collision is resolved by
  reality; subtasks and mood take the next free number when THEY land.
- **28.2 (check-in + mood) is unblocked.**
- **Subtasks re-land fresh, never from the old zip** — that drop predates
  v28.0 and the audit; applying it now would roll both back. The design
  record survives in the roadmap §I–§J and the annotated session note.

## Changed
`SESSION_NOTES.md` (two entries annotated — kept, not deleted; checklist
item 6: the apply check; new session entry), `06_IterationPlan.md` (row →
"never landed / re-land fresh"), `README.md` (status line fixed, dead
addendum link removed, "Break it into pieces" marked *designed, never
landed*), `QUESTION_BANK.md` (**V197**).
