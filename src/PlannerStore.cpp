#include "PlannerStore.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>

PlannerStore::PlannerStore(const QString& dirPath)
    : m_dir(dirPath)
{
}

QString PlannerStore::fileFor(const QString& username) const
{
    // Canonical lowercase — "Alice" and "alice" are the same account
    // (AccountStore's rule), so they must be the same FILE too, or the same
    // person would see different planners depending on how they typed their
    // name at login. One identity, one canonical key, everywhere.
    QString safe = username.trimmed().toLower();
    // Belt to AccountStore's braces: registration already restricts the
    // charset, but a storage layer should never TRUST that user input is
    // path-safe. Strip anything that isn't; an empty result stores nothing.
    safe.remove(QRegularExpression(QStringLiteral("[^a-z0-9_-]")));
    return QDir(m_dir).filePath(safe + QStringLiteral(".json"));
}

int PlannerStore::revision(const QString& username) const
{
    QFile f(fileFor(username));
    if (!f.open(QIODevice::ReadOnly))
        return 0; // nothing stored yet — revisions start at 0 by definition
    return QJsonDocument::fromJson(f.readAll())
        .object()
        .value(QStringLiteral("revision"))
        .toInt(0);
}

QJsonObject PlannerStore::planner(const QString& username) const
{
    QFile f(fileFor(username));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll())
        .object()
        .value(QStringLiteral("data"))
        .toObject();
}

int PlannerStore::store(const QString& username, const QJsonObject& data)
{
    const int newRevision = revision(username) + 1;

    QJsonObject root;
    root[QStringLiteral("revision")] = newRevision;
    root[QStringLiteral("data")]     = data;

    QDir().mkpath(m_dir);
    // QSaveFile, same as the client's JsonStore and for the same reason:
    // a crash mid-write must leave the PREVIOUS revision intact, not half a
    // file. The server holds the only shared copy — it has even less right
    // to corrupt data than the client does.
    QSaveFile f(fileFor(username));
    if (!f.open(QIODevice::WriteOnly))
        return newRevision - 1; // couldn't store; shelf unchanged
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!f.commit())
        return newRevision - 1;
    return newRevision;
}
