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
};
