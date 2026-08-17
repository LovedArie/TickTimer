# TickTimer — documentation audit after v27.0 (docs only)

**No code changed. Version stays 27.0.0, format v11, 267 tests green.**
Unzip over the project root; nothing to rebuild.

## Why
A deliberate audit pass: every doc surface checked against what shipped
in v25.2 → v27.0.

## Fixed
    design-addendum-assistant.md   §J.2 (multiplier) + §J.3 (decomposition)
                                   RESTORED — the v27.0 strike had deleted
                                   planned content along with shipped §J.1,
                                   under a stub that claimed otherwise.
                                   Visible restoration note. §N table now
                                   shows v26 shipped (three drops) and
                                   v27.0 ✓ / v27.1 remaining.
    06_IterationPlan.md            v26 ✅, v27 split ✅/⬜; the "format v10"
                                   error corrected in place with a note.
    README.md                      two new feature sections: "The Assistant
                                   (v25–v26)" and "Break it into pieces
                                   (v27)" — subtasks previously had NO
                                   user-facing documentation anywhere.
    READING_GUIDE.md               new §7: landmarks for the five arcs
                                   shipped since the guide last grew (v20
                                   model/view through v27 subtasks).
    SESSION_NOTES.md               the audit entry, including the lesson:
                                   strike on the shipped/planned boundary,
                                   not the heading boundary.

## Checked, deliberately unchanged
    FOR-TESTERS.md   a server/sync runbook — v25–v27 change nothing in it
    design-doc.md    addenda merge into it as recorded future work; the
                     subtasks addendum is canonical until then
    AI.md            §5b/§5c landed with their features
    QUESTION_BANK.md documents code; no code changed

## Next
v27.1 — the multiplier (§J.2) + affordability() (§H), planning prose now
restored where the next session will look for it.
