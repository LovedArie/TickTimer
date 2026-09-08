#pragma once
// ---------------------------------------------------------------------------
// Merge.h — the three-way merge, as a PURE function over three documents.
//
// WHY THIS EXISTS. Sync exchanges one opaque whole-document blob
// (design-addendum-sync §D, and that choice is what let the WebAssembly
// client reuse this C++ instead of reimplementing the model in JavaScript).
// The cost was that the unit of conflict became THE ENTIRE PLANNER: edit
// anything on a phone and anything else on a laptop, and syncplan::decide
// could only say "Conflict" and make a person throw one device's day away.
//
// The owner hit that twice in one afternoon while testing on both devices,
// and asked the obvious question — "can't it try merging them first, and
// only ask about what's left?" It can, and the blob is not the obstacle.
// The CLIENT holds both documents in full; nothing about merging requires
// the server to understand the data.
//
// THE OBSTACLE IS ACTUALLY THIS. Given only two documents, an entity on the
// server but not on the phone is ambiguous: was it ADDED there, or DELETED
// here? A two-way union answers "added" every time, which silently
// resurrects everything you have ever deleted — the classic naive-sync bug.
//
// So the merge is THREE-WAY: the document as it stood at the last successful
// sync is kept as a BASE, and every question becomes decidable.
//
//     in base | in local | in server | verdict
//     --------+----------+-----------+---------------------------------
//        no   |   yes    |    no     | added here      -> keep
//        no   |   no     |   yes     | added there     -> keep
//        no   |   yes    |   yes     | added both; same -> keep, else CONFLICT
//       yes   |   gone   | unchanged | deleted here    -> drop
//       yes   | unchanged|   gone    | deleted there   -> drop
//       yes   |  edited  | unchanged | -> local
//       yes   | unchanged|  edited   | -> server
//       yes   |  edited  |  edited   | same? keep : CONFLICT
//       yes   |   gone   |  edited   | CONFLICT (delete vs edit)
//       yes   |  edited  |   gone    | CONFLICT (edit vs delete)
//
// This is git's model, and it needs no per-entity timestamps and no
// tombstones — "present in base, absent now" IS the delete. Rejected
// alternatives are in the addendum; the short version is that per-entity
// `updatedAt` (how TickTick and Todoist do it) would mean stamping every
// mutation in AppData and inventing tombstones, to learn what the base
// already tells us.
//
// GENERIC OVER COLLECTIONS ON PURPOSE. It walks whatever arrays-of-objects-
// with-an-"id" the documents contain, so tasks, events, activities,
// categories, folders, schedules, special days and moods are all handled by
// one pass — and the collection added next version is handled with no edit
// here. A merge that had to be taught each collection would be a merge that
// silently skipped the one somebody forgot.
//
// PURE: no AppData, no clock, no network, no Widgets. Three documents in, a
// merged document and a list of what could not be decided out. The same
// treatment SyncPlan::decide, MissedBlocks and recur::occurrences already
// get, and for the same reason — this is the one real judgement of the
// feature, so it gets a table of microsecond tests instead of a socket.
// ---------------------------------------------------------------------------

#include <QJsonArray>
#include <QJsonObject>
#include <QObject> // QObject::tr — the clash SENTENCES live here, see below
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

namespace merge
{

// One entity the merge could not decide, named well enough for a human to
// find it. `collection` is the array it lives in ("tasks"), `id` its id, and
// `label` its most human field if one is obvious — a title or a name — so a
// dialog can say "Lab 4" rather than a UUID.
struct Clash
{
    // WHICH WAY ROUND the disagreement is (v31.2).
    //
    // Three genuinely different situations used to arrive as one shapeless
    // list, so the dialog could only say "Lab 4" and a person had to pick a
    // side without being told what the sides were. Worse, only the first of
    // the three is actually governed by the choice being offered - see
    // decidedByPreference below.
    enum class Kind {
        BothEdited,             // both sides hold a different version
        EditedHereDeletedThere, // we edited it, they deleted it
        DeletedHereEditedThere, // they edited it, we deleted it
    };

    QString collection;
    QString id;
    QString label;
    Kind    kind = Kind::BothEdited;
};

// "Do the buttons govern this row?"
//
// Only BothEdited. For the two delete shapes plan() keeps the edited version
// whichever value preferServer takes - losing an edit to a delete is the one
// thing the merge will not do on its own - so both plans come out identical
// on those rows and the user's answer is thrown away.
//
// Asked HERE rather than re-derived in the dialog, so the sentence a person
// reads cannot disagree with what the merge actually does. A test pins the
// two-plans-are-identical claim this rests on; without it, someone
// "completing" plan() by consulting preferServer in those branches would
// silently turn the dialog into a liar.
inline bool decidedByPreference(Clash::Kind k)
{
    return k == Clash::Kind::BothEdited;
}

// "Is there anything in here that the two buttons actually decide?" A list of
// clashes none of which is contested means both plans are the same document,
// so there is no question to put to a person.
inline bool anyDecidedByPreference(const QVector<Clash>& clashes)
{
    for (const Clash& c : clashes)
        if (decidedByPreference(c.kind))
            return true;
    return false;
}

// ---------------------------------------------------------------------------
// SAYING IT. The sentences a person reads about a conflict live here rather
// than in the dialog, for the reason recur::summaryLines is in Recurrence.h
// and not in the schedule editor: it is the feature's one real judgement, and
// a pure function can be pinned by a table of microsecond tests where a
// QLabel buried in a modal cannot be reached at all.
//
// It also keeps the sentence and the decision in one file. The dialog claims
// a row is "kept automatically"; decidedByPreference is why that is true.
// Those two must not be able to drift apart, and the surest way to guarantee
// it is that they cannot be edited independently.
// ---------------------------------------------------------------------------

// The heading a group of rows sits under. The seven top-level collection keys
// are single lowercase words ("tasks", "events", "activities", "categories",
// "folders", "schedules", "moods"), so uppercasing the key IS the human name.
// Deliberately NOT a key-to-label table: the JSON format grows additively
// (JsonStore.cpp), such a table must be hand-extended on every growth, and a
// forgotten entry leaks a raw camelCase key onto the screen. Nothing to keep
// in step means nothing that can drift.
inline QString headingFor(const QString& collection)
{
    return collection.toUpper();
}

// WHAT EACH SIDE DID — the whole point of the conflict box. A person choosing
// between two devices has to be told what the two devices did, and until
// v31.2 this said only "Lab 4", which names the disagreement without
// describing it.
//
// The two delete shapes also name their OUTCOME, because it is already
// settled (decidedByPreference) and saying so is the difference between
// reporting something and asking about it.
inline QString describeSides(Clash::Kind kind)
{
    switch (kind) {
    case Clash::Kind::BothEdited:
        return QObject::tr("changed here, and changed on the server");
    case Clash::Kind::EditedHereDeletedThere:
        return QObject::tr("you changed it here, the server deleted it "
                           "— your version is kept");
    case Clash::Kind::DeletedHereEditedThere:
        return QObject::tr("you deleted it here, the server changed it "
                           "— the server's version is kept");
    }
    return {};
}

// Rows under their collection heading, capped. The cap is on ROWS rather than
// groups, and the tail counts what it hid: a list of forty is not
// information, but "and 37 more" is honest where thirty-seven names are
// noise. Empty in, empty out — the caller hides the section.
//
// Expects rows already ordered by collection (SyncDialog sorts, because
// plan() walks its ids out of an unordered QSet), so each heading is printed
// once.
inline QString renderClashes(const QVector<Clash>& rows)
{
    const int kShown = 6;
    QStringList out;
    QString group;
    int shown = 0;
    for (const Clash& c : rows) {
        if (shown == kShown)
            break;
        if (c.collection != group) {
            group = c.collection;
            if (!out.isEmpty())
                out << QString();
            out << headingFor(group);
        }
        out << QStringLiteral("  • %1")
                   .arg(c.label.isEmpty() ? c.id : c.label);
        out << QStringLiteral("      %1").arg(describeSides(c.kind));
        ++shown;
    }
    if (rows.size() > shown)
        out << QObject::tr("…and %n more", nullptr, rows.size() - shown);
    return out.join(QLatin1Char('\n'));
}

struct Result
{
    QJsonObject merged;
    QVector<Clash> clashes; // empty == fully merged, nothing to ask
};

namespace detail
{
// The one human-readable field, if the object has one. Pure presentation
// help, kept here so a caller never has to know which collections call it
// "title" and which call it "name".
inline QString labelOf(const QJsonObject& o)
{
    for (const char* key : {"title", "name"}) {
        const QString v = o.value(QLatin1String(key)).toString();
        if (!v.isEmpty())
            return v;
    }
    return {};
}

inline QHash<QString, QJsonObject> byId(const QJsonArray& a)
{
    QHash<QString, QJsonObject> out;
    for (const QJsonValue& v : a) {
        const QJsonObject o = v.toObject();
        const QString id = o.value(QStringLiteral("id")).toString();
        if (!id.isEmpty())
            out.insert(id, o);
    }
    return out;
}

// "Is this key a collection of identified things?" — an array whose first
// element is an object carrying an id. Asked of the data rather than a
// hardcoded list, which is what makes the merge generic.
inline bool isCollection(const QJsonValue& v)
{
    if (!v.isArray())
        return false;
    const QJsonArray a = v.toArray();
    if (a.isEmpty())
        return true; // an empty array is a collection with nothing in it
    const QJsonValue first = a.first();
    return first.isObject()
           && first.toObject().contains(QStringLiteral("id"));
}
} // namespace detail

// ---------------------------------------------------------------------------
// base   — the document as it stood at the last successful sync
// local  — this device's document now
// server — the server's document now
//
// A missing base (first sync ever on this device, or a base that was lost)
// makes every entity look ADDED on both sides, which is exactly right: the
// merge keeps everything and flags only entities that exist on both sides
// with different contents. Nothing is silently dropped when the base is
// unknown — the pessimistic direction, deliberately.
// ---------------------------------------------------------------------------
// preferServer decides ONLY the contested entities — the ones edited on both
// sides. Everything either side added or deleted independently lands the same
// way whichever value is passed, which is what makes the eventual question
// small: it is about the clashes, never about whose device wins wholesale.
inline Result plan(const QJsonObject& base, const QJsonObject& local,
                   const QJsonObject& server, bool preferServer = false)
{
    Result out;

    // Every key either document knows about, so a collection that exists on
    // only one side is not quietly dropped.
    QStringList keys = local.keys();
    for (const QString& k : server.keys())
        if (!keys.contains(k))
            keys << k;

    for (const QString& key : keys) {
        const QJsonValue lv = local.value(key);
        const QJsonValue sv = server.value(key);

        if (!detail::isCollection(lv) && !detail::isCollection(sv)) {
            // A scalar or a non-identified object: the format version, the
            // crash-insurance "running" block. LOCAL WINS, and for "running"
            // that is not a tie-break but the correct answer — which timer
            // this device has going is a fact about this device, and taking
            // the other machine's would claim you are tracking a block you
            // are not sitting in front of.
            out.merged.insert(key, lv.isUndefined() ? sv : lv);
            continue;
        }

        const auto B = detail::byId(base.value(key).toArray());
        const auto L = detail::byId(lv.toArray());
        const auto S = detail::byId(sv.toArray());

        QSet<QString> ids;
        for (auto it = L.begin(); it != L.end(); ++it) ids.insert(it.key());
        for (auto it = S.begin(); it != S.end(); ++it) ids.insert(it.key());
        for (auto it = B.begin(); it != B.end(); ++it) ids.insert(it.key());

        QJsonArray kept;
        for (const QString& id : ids) {
            const bool inB = B.contains(id);
            const bool inL = L.contains(id);
            const bool inS = S.contains(id);

            if (inL && inS) {
                if (L[id] == S[id]) {          // agreed, however it got there
                    kept.append(L[id]);
                } else if (inB && S[id] == B[id]) {
                    kept.append(L[id]);        // only we edited it
                } else if (inB && L[id] == B[id]) {
                    kept.append(S[id]);        // only they edited it
                } else {
                    // Edited on both sides, differently — or added on both
                    // sides with different contents and no base to judge by.
                    // The one case a human has to settle.
                    out.clashes.append({key, id, detail::labelOf(L[id]),
                                        Clash::Kind::BothEdited});
                    kept.append(preferServer ? S[id] : L[id]);
                }
                continue;
            }

            if (inL && !inS) {
                if (!inB) {
                    kept.append(L[id]);        // added here
                } else if (L[id] == B[id]) {
                    // Untouched here, gone there: a real remote delete.
                } else {
                    // We edited it, they deleted it. Losing an edit to a
                    // delete silently is the one thing a merge must never
                    // do on its own.
                    out.clashes.append({key, id, detail::labelOf(L[id]),
                                        Clash::Kind::EditedHereDeletedThere});
                    kept.append(L[id]);
                }
                continue;
            }

            if (!inL && inS) {
                if (!inB) {
                    kept.append(S[id]);        // added there
                } else if (S[id] == B[id]) {
                    // Untouched there, gone here: a real local delete.
                } else {
                    out.clashes.append({key, id, detail::labelOf(S[id]),
                                        Clash::Kind::DeletedHereEditedThere});
                    kept.append(S[id]);
                }
                continue;
            }
            // In base only: deleted on both sides. Agreement — drop it.
        }
        out.merged.insert(key, kept);
    }
    return out;
}

} // namespace merge
