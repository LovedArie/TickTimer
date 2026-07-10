#include "ShareStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

ShareStore::ShareStore(const QString& filePath)
    : m_filePath(filePath)
{
    load();
}

QString ShareStore::canonical(const QString& name)
{
    // trimmed().toLower() is THE identity rule of the whole server —
    // AccountStore registers with it, PlannerStore files with it. A grant
    // stored under any other spelling would be a grant nobody can find.
    return name.trimmed().toLower();
}

bool ShareStore::grant(const QString& owner, const QString& viewer)
{
    const QString o = canonical(owner);
    const QString v = canonical(viewer);
    if (o.isEmpty() || v.isEmpty() || o == v)
        return false; // sharing with yourself is meaningless, refuse loudly

    QStringList& viewers = m_grants[o];
    if (!viewers.contains(v)) {
        viewers.append(v);
        viewers.sort(); // deterministic file contents -> diffable, testable
        save();
    }
    return true; // idempotent: "already shared" is success, not an error
}

bool ShareStore::revoke(const QString& owner, const QString& viewer)
{
    const QString o = canonical(owner);
    const QString v = canonical(viewer);

    auto it = m_grants.find(o);
    if (it == m_grants.end() || !it->contains(v))
        return true; // idempotent: the requested end state already holds

    it->removeAll(v);
    if (it->isEmpty())
        m_grants.erase(it); // no empty lists lingering in the file
    save();
    return true;
}

bool ShareStore::canRead(const QString& viewer, const QString& owner) const
{
    const QString o = canonical(owner);
    const QString v = canonical(viewer);
    if (v.isEmpty() || o.isEmpty())
        return false;
    if (v == o)
        return true; // your own planner is always yours to read
    return m_grants.value(o).contains(v);
}

QStringList ShareStore::viewersOf(const QString& owner) const
{
    return m_grants.value(canonical(owner));
}

QStringList ShareStore::ownersSharedWith(const QString& viewer) const
{
    // The reverse question has no index — we scan. Fine at home-server
    // scale (a handful of accounts); the day this store holds thousands of
    // users is the day it stops being a JSON file anyway.
    const QString v = canonical(viewer);
    QStringList owners;
    for (auto it = m_grants.constBegin(); it != m_grants.constEnd(); ++it)
        if (it.value().contains(v))
            owners.append(it.key());
    owners.sort();
    return owners;
}

void ShareStore::load()
{
    QFile f(m_filePath);
    if (!f.open(QIODevice::ReadOnly))
        return; // no file yet — an empty store, not an error

    const QJsonObject root   = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonObject grants = root.value(QStringLiteral("grants")).toObject();
    // Tolerant load, same posture as every store in this project: read what
    // we recognise, silently skip what we don't. Unknown keys are a future
    // version talking, not corruption.
    for (auto it = grants.constBegin(); it != grants.constEnd(); ++it) {
        QStringList viewers;
        const QJsonArray arr = it.value().toArray();
        for (const QJsonValue& v : arr) {
            const QString name = canonical(v.toString());
            if (!name.isEmpty() && name != canonical(it.key()))
                viewers.append(name); // re-apply the invariants on the way in
        }
        viewers.sort();
        if (!viewers.isEmpty())
            m_grants.insert(canonical(it.key()), viewers);
    }
}

void ShareStore::save() const
{
    QJsonObject grants;
    for (auto it = m_grants.constBegin(); it != m_grants.constEnd(); ++it)
        grants.insert(it.key(), QJsonArray::fromStringList(it.value()));

    QJsonObject root;
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("grants")]  = grants;

    QDir().mkpath(QFileInfo(m_filePath).absolutePath());
    // QSaveFile everywhere data matters — a crash mid-write must leave the
    // previous grants intact, not a truncated file that reads as "nobody
    // shares with anybody" (which would be a silent, invisible data loss).
    QSaveFile f(m_filePath);
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    f.commit();
}
