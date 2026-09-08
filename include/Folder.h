#pragma once
// ---------------------------------------------------------------------------
// Folder — a named grouping of Categories in the rail ("School"), one
// level deep (addendum §3.12).
//
// Why this is a CLASS and not a naming convention: the tempting hack was
// folders-by-prefix ("School / LOG410" as a category name) — a fact
// smuggled into a string. Strings can't be guarded by rules, renames
// shatter them, and every reader parses forever. The classifier verdict:
// folder membership must survive a restart, so it is a stored fact, and
// facts get concepts. The rail merely DISPLAYS this one as a tree.
// ---------------------------------------------------------------------------

#include <QString>

struct Folder
{
    QString id;
    QString name; // e.g. "School"

    // v31: the third round of the same idea. Activities retire (v7), whole
    // life areas retire (v8), and a SEMESTER retires — "Summer 2026" holds
    // four courses and in September none of them should be on screen, but
    // none of them should be gone either.
    //
    // The rule is inherited verbatim from Category: archiving a folder sets
    // NO flags on the categories inside it. It hides them (AppData::
    // categoryHidden asks both questions), and restoring the folder brings
    // back exactly what was there, including which of its areas were
    // individually archived first. A cascade that stamped `archived` on each
    // child could not tell those two states apart on the way back.
    bool archived = false;
};
