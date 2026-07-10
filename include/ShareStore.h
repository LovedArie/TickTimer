#pragma once

#include <QMap>
#include <QString>
#include <QStringList>

// ---------------------------------------------------------------------------
// ShareStore — the server's record of who may READ whose planner.
//
// The model is a directed grant: "alice shares with bob" means bob may read
// alice's planner — and nothing more. Not the other way around (bob's planner
// stays private until BOB grants), and never write access (compare is a
// read-only feature; the only person who can change a planner is its owner,
// through the existing sync routes). One arrow, one meaning.
//
// Why the server needs this at all: PlannerStore's whole design is "the blob
// is opaque, the server never looks inside" — which means the server CANNOT
// compute a friendly summary to share. The only thing it can share is the
// blob itself, so the real decision this store guards is "who is allowed to
// receive the blob". The client does the understanding (see Compare.h); the
// server does the permitting. Dumb server, smart client, unchanged.
//
// Same construction kit as AccountStore, on purpose: plain value-semantics
// class (no QObject — the server is request/response, nobody subscribes to
// grants changing), one JSON file, load on start, save after each mutation,
// canonical-lowercase names throughout so "Alice shares with BOB" and
// "alice shares with bob" are the same fact.
//
// Storage: {"version": 1, "grants": {"alice": ["bob", "carol"], ...}}
// meaning alice's planner is readable by bob and carol.
// ---------------------------------------------------------------------------

class ShareStore
{
public:
    explicit ShareStore(const QString& filePath);

    // Grant `viewer` read access to `owner`'s planner. Refuses self-shares
    // and empty names (false); granting twice is a harmless no-op (true) —
    // idempotent, because the CLIENT retries on flaky Wi-Fi and "you already
    // shared" is not an error a human should have to care about.
    bool grant(const QString& owner, const QString& viewer);

    // Remove a grant. Also idempotent: revoking something that wasn't there
    // returns true — the end state (no access) is what the caller asked for.
    bool revoke(const QString& owner, const QString& viewer);

    // May `viewer` read `owner`'s planner? Owners always read their own —
    // identity beats any grant table.
    bool canRead(const QString& viewer, const QString& owner) const;

    // The two directions of the question, for the /shares route:
    QStringList viewersOf(const QString& owner) const;        // I share with…
    QStringList ownersSharedWith(const QString& viewer) const; // …shared with me

private:
    // One canonicalisation, used by every entry point — the same rule as
    // AccountStore and PlannerStore, because an identity that's canonical in
    // two stores and raw in a third is a bug factory.
    static QString canonical(const QString& name);

    void load();
    void save() const;

    QString                    m_filePath;
    QMap<QString, QStringList> m_grants; // owner -> sorted viewers (canonical)
};
