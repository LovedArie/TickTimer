#pragma once

#include <QJsonObject>
#include <QString>

// ---------------------------------------------------------------------------
// PlannerStore — the server's shelf of planner documents: ONE document per
// account, each with a REVISION counter that goes up by one on every store.
//
// The single most important design fact: the server NEVER looks inside the
// document. `data` is an opaque blob to it — no Event, no Category, no
// planner knowledge at all. Dumb server, smart client: the planner format
// can grow (v6 → v7 → …) without the server ever needing an update, and
// there is simply less server code to get wrong. The only thing the server
// understands is "which version of the blob is this?"
//
// The revision is the whole concurrency story (design-addendum-sync):
// a client pushing says "I'm based on revision N"; if the shelf has moved
// past N, AuthServer refuses with 409 instead of silently overwriting.
// That check lives in AuthServer — this store just versions what it's given.
//
// Storage: <dir>/<username>.json holding {"revision": N, "data": {...}}.
// Usernames are safe as filenames because AccountStore restricts their
// charset at registration (see the note there) — defense at the door, so
// every layer below stays simple.
// ---------------------------------------------------------------------------

class PlannerStore
{
public:
    explicit PlannerStore(const QString& dirPath);

    // 0 means "no document stored yet" — which is why client revisions
    // start counting from 0 too: a fresh account and a fresh device agree.
    int         revision(const QString& username) const;

    QJsonObject planner(const QString& username) const; // {} if none

    // Store a document, bumping the revision. Returns the NEW revision.
    int store(const QString& username, const QJsonObject& data);

private:
    QString fileFor(const QString& username) const;
    QString m_dir;
};
