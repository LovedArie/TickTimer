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
};
