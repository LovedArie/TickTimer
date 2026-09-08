#pragma once
// ---------------------------------------------------------------------------
// Activity — a reusable *type* of thing you do (design-doc §2):
// "Study Math", "Gym", "Doomscroll".
//
// Note what an Activity does NOT hold: a colour. Its colour comes from its
// Category, looked up through `categoryId`. That is design-doc §3.4
// ("reference, don't copy") in action — recolour "Health" once and every
// gym session ever planned changes with it. If we copied the colour in
// here, the data could disagree with itself.
//
// WHY `categoryId` is a QString and not a Category* pointer:
//   1. It matches what is stored on disk — JSON can hold an id, not a pointer.
//   2. It sidesteps C++'s hardest beginner trap: dangling pointers. All our
//      objects live inside QVectors in AppData, and a QVector *moves its
//      elements in memory* when it grows. Any raw pointer into it can
//      silently become garbage. An id can't dangle — at worst a lookup
//      fails loudly, which is debuggable.
// The cost is a lookup (AppData::categoryById) instead of a dereference.
// For a dataset of this size, that cost is zero in practice.
// ---------------------------------------------------------------------------

#include <QString>

struct Activity
{
    QString id;
    QString name;        // e.g. "Study Math"
    QString categoryId;  // exactly ONE category (design-doc §3.7 — v1 rule)

    // v31: free-text notes, the same field Task has carried since v7 and for
    // the same reason — "Gym" is a name, not an instruction, and the detail
    // ("upper body Mon/Thu, 45 min warm-up") has to live somewhere that is
    // not the name. Empty is the normal state; nothing reads it as absent.
    QString description;

    // v31: where this activity sits in its life area's list, as a plain
    // integer renumbered densely (0,1,2,…) on every reorder. Activities have
    // no "smart" order to fall back on — they have always been shown in the
    // accident of insertion order — so unlike tasks they are ALWAYS sorted by
    // this, and Category::SortMode does not apply to them. See Category.h for
    // why the ordering is stored data rather than a preference.
    int sortKey = 0;

    // v7: retirement without amnesia. An activity that appears in past
    // events can NEVER be deleted (removeActivity refuses — history would
    // dangle), but life moves on and "Study CS101" shouldn't clutter every
    // picker forever. Archived = hidden from lists and pickers, still
    // resolvable by id, so every old day keeps its name and colour.
    bool archived = false;
};
