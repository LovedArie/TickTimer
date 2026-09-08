#pragma once
// ---------------------------------------------------------------------------
// Category — a *life area* an activity serves (design-doc §2).
//
// WHY a class and not an enum: the user can invent new life areas at runtime
// ("Volunteering") — an enum is frozen at compile time (design-doc §3.6).
//
// WHY a plain struct with public fields, and not a class with getters/setters:
// Category is a *value type* — a small bundle of data with no behaviour and no
// invariants of its own to protect. All the rules that involve categories
// ("you can't delete one that still has activities") belong to the object
// that owns the whole data set: AppData. Wrapping each field in get/set here
// would add ceremony without adding safety. C++ developers reach for structs
// when data is just data. (Contrast with AppData.h, which *does* hide its
// fields — because it has invariants to defend.)
// ---------------------------------------------------------------------------

#include <QString>
#include <QColor>

struct Category
{
    // v31 — HOW this area's task list is ordered. A closed set of two, so an
    // enum: "manaul" must not be storable, the same instinct behind Repeat
    // and Priority in Task.h.
    //
    // WHY THE MODE IS A STORED FACT AND LIVES HERE. Manual order and smart
    // order are two answers to one question, and something has to say which
    // one this list is currently giving. Deriving it ("is any sortKey
    // non-default?") would be a guess that gets louder as it ages, and a
    // preference in QSettings would be wrong for a different reason: which
    // order a list is IN is part of the list, so it must sync and survive a
    // reinstall, while preferences deliberately do neither (CLAUDE.md).
    //
    // It hangs off the CATEGORY, not off the app, because the answer is
    // genuinely per-area: a course's deliverables want the deadline order,
    // while a morning routine is a sequence and its dates mean nothing.
    enum class SortMode { Smart, Manual };

    QString id;       // unique, never changes (see Ids.h for why UUIDs)
    QString name;     // e.g. "Work / Study", "Health", "Wasting time"
    QColor  color;    // the colour every activity/event in this area uses
    QString folderId; // OPTIONAL home in the rail: id of a Folder, or ""
                      // for top level. Same reference-by-id discipline as
                      // every other link in this model (see Activity.h).

    // v8: whole LIFE AREAS retire too (owner's use case: a semester's
    // classes live in one area — new term, archive the whole thing, make
    // the next). Archiving hides the category AND its world (activities,
    // tasks) from every list and picker — but sets NO flags on children:
    // restore the area and everything returns exactly as it was.
    bool archived = false;

    // Smart is the default, so every existing area keeps the ordering it has
    // always had and nothing moves on upgrade. The first drag flips this.
    SortMode sortMode = SortMode::Smart;
};

// ---- SortMode <-> text ------------------------------------------------------
// Same shape as repeatToString/FromString in Task.h: an unknown or missing
// string reads as the default, which is what lets the JSON grow additively
// with no migration branch.
inline QString sortModeToString(Category::SortMode m)
{
    return m == Category::SortMode::Manual ? QStringLiteral("manual")
                                           : QStringLiteral("smart");
}

inline Category::SortMode sortModeFromString(const QString& s)
{
    return s == QLatin1String("manual") ? Category::SortMode::Manual
                                        : Category::SortMode::Smart;
}
