#pragma once
// ---------------------------------------------------------------------------
// Ids — how objects get their identity.
//
// Events reference Activities, Activities reference Categories (design-doc
// §3.4: reference, don't copy). A reference needs a stable identity that
// never changes even if the name does — so every object carries an `id`.
//
// WHY UUIDs and not an incrementing integer counter:
//   - no counter to persist (an int counter is one more piece of state that
//     can drift, reset, or collide after a bad merge of two data files);
//   - safe if data from two sources ever meets (the future sync goal on the
//     Risk List becomes a little less scary).
// Cost: ids are long and ugly in the JSON file. For a debugging-friendly
// v1 that's an acceptable trade — you never type them, only the code does.
//
// WHY a free function in a namespace, not a class: it has no state. In C++,
// a stateless helper is just a function — creating a class for it would be
// Java habit, not C++ style.
// ---------------------------------------------------------------------------

#include <QString>
#include <QUuid>

namespace ids
{
inline QString newId()
{
    // "WithoutBraces" turns {xxxx-...} into xxxx-... — shorter in the JSON.
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
} // namespace ids
