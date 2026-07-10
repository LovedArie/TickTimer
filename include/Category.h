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
    QString id;       // unique, never changes (see Ids.h for why UUIDs)
    QString name;     // e.g. "Work / Study", "Health", "Wasting time"
    QColor  color;    // the colour every activity/event in this area uses
    QString folderId; // OPTIONAL home in the rail: id of a Folder, or ""
                      // for top level. Same reference-by-id discipline as
                      // every other link in this model (see Activity.h).
};
