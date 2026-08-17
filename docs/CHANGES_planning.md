# TickTimer — Assistant Roadmap (documentation only)

**No code changed. Version stays 25.1.0. Tests stay 240, 6/6 green.**

Unzip over the project root.

## New files
    docs/design-addendum-assistant.md   the roadmap, §A-§O — marked PLANNED at the top
    diagrams/assistant_spine.puml/.png/.svg      the "code computes, model phrases" rule
    diagrams/assistant_roadmap.puml/.png/.svg    iteration dependencies

## Updated files
    docs/06_IterationPlan.md   §4a rewritten: AI foundation → subtasks & sizing →
                               proactive (read-only) → tool use → memory.
                               Also fixed drift: the section still argued "why the
                               provider layer is next" — it shipped in v24.
    docs/design-doc.md         §3 index row for the new addendum
    docs/SESSION_NOTES.md      the planning session
    diagrams/README.md         indexes the two planning diagrams

## Deliberately NOT updated
    docs/QUESTION_BANK.md      the bank documents what EXISTS. Mixing shipped with
                               planned in a study aid is how you end up confidently
                               explaining a feature nobody built. It grows when the
                               code does.
    docs/AI.md                 a user guide shouldn't promise features.

## The one-line summary of the whole document
Code decides WHEN. Code computes WHAT IS TRUE. The model only PHRASES.
